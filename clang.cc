#include "document.hh"
#include "sol.hh"
#include <clang-c/Documentation.h>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
	class ClangTextBuilder
	{
		/**
		 * RAII wrapper for CXString.
		 */
		class String
		{
			/// The underlying CXString.
			CXString str;

			public:
			/// Construct a new String from a CXString.
			String(CXString str) : str(str) {}
			/// Destroy the String and release the CXString.
			~String()
			{
				clang_disposeString(str);
			}
			/// Implicitly convert the String to a std::string_view.
			operator std::string_view()
			{
				return clang_getCString(str);
			}
			/// Implicitly convert the String to a std::string.
			operator std::string()
			{
				return clang_getCString(str);
			}
			/// Explicitly concert to a C string.
			const char *c_str()
			{
				return clang_getCString(str);
			}
		};
		CXIndex index = clang_createIndex(0, 1);

		CXTranslationUnit translationUnit;

		public:
		ClangTextBuilder() = delete;

		ClangTextBuilder(const std::string              &fileName,
		                 const std::vector<std::string> &args)
		{
			std::vector<const char *> argv;
			for (auto &arg : args)
			{
				argv.push_back(arg.c_str());
			}
			translationUnit = clang_parseTranslationUnit(
			  index,
			  fileName.c_str(),
			  argv.data(),
			  argv.size(),
			  nullptr,
			  0,
			  CXTranslationUnit_DetailedPreprocessingRecord);
		}

		std::vector<std::string> usrs_for_entity(const std::string &name,
		                                         CXCursorKind       entityKind)
		{
			std::unordered_set<std::string> usrs;
			auto visit = [&](CXCursor cursor, CXCursor parent) {
				if (entityKind == cursor.kind)
				{
					String spelling = String(clang_getCursorSpelling(cursor));
					if (name == std::string_view{spelling})
					{
						usrs.insert(String(clang_getCursorUSR(cursor)));
					}
				}
				return CXChildVisit_Recurse;
			};
			clang_visitChildren(
			  clang_getTranslationUnitCursor(translationUnit),
			  [](CXCursor cursor, CXCursor parent, CXClientData clientData) {
				  return (*static_cast<decltype(visit) *>(clientData))(cursor,
				                                                       parent);
			  },
			  &visit);
			return std::vector<std::string>{usrs.begin(), usrs.end()};
		}

		std::vector<std::string>
		usrs_for_function(const std::string &functionName)
		{
			auto functionUSRs =
			  usrs_for_entity(functionName, CXCursor_FunctionDecl);
			auto templateUSRs =
			  usrs_for_entity(functionName, CXCursor_FunctionTemplate);
			functionUSRs.insert(
			  functionUSRs.begin(), templateUSRs.begin(), templateUSRs.end());
			return functionUSRs;
		}

		std::vector<std::string> usrs_for_macro(const std::string &macroName)
		{
			return usrs_for_entity(macroName, CXCursor_MacroDefinition);
		}

		const char *token_kind(CXToken token)
		{
			switch (clang_getTokenKind(token))
			{
				case CXToken_Comment:
					return "Comment";
				case CXToken_Identifier:
					return "Identifier";
				case CXToken_Punctuation:
					return "Punctuation";
				case CXToken_Keyword:
					return "Keyword";
				case CXToken_Literal:
					return "Literal";
			}
		}

		TextTreePointer build_line_range(std::string fileName,
		                                 unsigned    firstLine,
		                                 unsigned    lastLine)
		{
			CXFile file = clang_getFile(translationUnit, fileName.c_str());
			CXSourceLocation startLocation =
			  clang_getLocation(translationUnit, file, firstLine, 1);
			CXSourceLocation endLocation =
			  clang_getLocation(translationUnit, file, lastLine + 1, 1);
			auto tree =
			  build_source_range(fileName, startLocation, endLocation, true);
			tree->attribute_set("first-line", std::to_string(firstLine));
			return tree;
		}

		TextTreePointer build_source_range(std::string      fileName,
		                                   CXSourceLocation startLocation,
		                                   CXSourceLocation endLocation,
		                                   bool skipTrailingLine = false,
		                                   bool stopAtBrace      = false)
		{
			unsigned start, end;
			clang_getExpansionLocation(
			  startLocation, nullptr, nullptr, nullptr, &start);
			clang_getExpansionLocation(
			  endLocation, nullptr, nullptr, nullptr, &end);
			if (skipTrailingLine)
			{
				end--;
			}

			std::string text;
			text.resize(end - start);
			std::ifstream fileStream(fileName);
			fileStream.seekg(start);
			fileStream.read(text.data(), end - start);
			auto     range = clang_getRange(startLocation, endLocation);
			CXToken *tokens;
			unsigned tokenCount;
			clang_tokenize(translationUnit, range, &tokens, &tokenCount);
			if (tokenCount == 0)
			{
				return nullptr;
			}
			off_t                 lastOffset = start;
			std::vector<CXCursor> cursors;
			cursors.resize(tokenCount);
			auto tree  = TextTree::create();
			tree->kind = "code";
			tree->attribute_set("code-kind", "listing");
			clang_annotateTokens(
			  translationUnit, tokens, tokenCount, cursors.data());
			CXCursor        last      = clang_getNullCursor();
			auto            tokenTree = TextTree::create();
			TextTreePointer currentToken;
			CXTokenKind     lastKind = CXTokenKind(-1);
			for (unsigned i = 0; i < tokenCount; i++)
			{
				CXToken       token  = tokens[i];
				CXCursor      cursor = cursors[i];
				CXSourceRange cursorRange =
				  clang_getTokenExtent(translationUnit, token);
				CXSourceLocation cursorStart = clang_getRangeStart(cursorRange);
				CXSourceLocation cursorEnd   = clang_getRangeEnd(cursorRange);
				unsigned         startOffset;
				unsigned         endOffset;
				clang_getExpansionLocation(
				  cursorStart, nullptr, nullptr, nullptr, &startOffset);
				clang_getExpansionLocation(
				  cursorEnd, nullptr, nullptr, nullptr, &endOffset);
				String spelling =
				  clang_getTokenSpelling(translationUnit, token);
				if (stopAtBrace && (cursor.kind == CXCursor_CompoundStmt))
				{
					break;
				}
				// If we've gone past the end of the range, stop.  We will scan
				// up to the line with the end on it, to make sure that we get
				// the last token.
				if (startOffset > end)
				{
					break;
				}
				// If two tokens are part of the same cursor, combine them.
				if (clang_equalCursors(cursor, last) &&
				    (lastKind == clang_getTokenKind(token)))
				{
					if (endOffset > lastOffset)
					{
						currentToken->append_text(text.substr(
						  lastOffset - start, startOffset - lastOffset));
					}
					currentToken->append_text(spelling);
					lastOffset = endOffset;
					continue;
				}
				last     = cursor;
				lastKind = clang_getTokenKind(token);
				if (startOffset > lastOffset)
				{
					tree->append_text(text.substr(lastOffset - start,
					                              startOffset - lastOffset));
				}
				currentToken = tree->new_child();
				currentToken->append_text(spelling);
				const char *tokenKind = "UnknownToken";
				switch (cursors[i].kind)
				{
					case CXCursor_FirstRef ... CXCursor_LastRef:
						tokenKind = "TypeRef";
						break;
					case CXCursor_MacroDefinition:
						tokenKind = "MacroDefinition";
						break;
					case CXCursor_MacroInstantiation:
						tokenKind = "MacroInstantiation";
						break;
					case CXCursor_FirstDecl ... CXCursor_LastDecl:
						tokenKind = "Declaration";
						break;
					case CXCursor_ObjCMessageExpr:
						tokenKind = "ObjCMessage";
						break;
					case CXCursor_DeclRefExpr:
						tokenKind = "DeclRef";
						break;
					case CXCursor_InclusionDirective:
					case CXCursor_PreprocessingDirective:
						tokenKind = "PreprocessingDirective";
						break;
					case CXCursor_StaticAssert:
					case CXCursor_IntegerLiteral ... CXCursor_CharacterLiteral:
						tokenKind = "Literal";
						break;
					default:
						// If we don't have a kind for the cursor, fall back to
						// the token.
						tokenKind = token_kind(token);
						break;
				}
				if (clang_getTokenKind(token) == CXToken_Punctuation)
				{
					tokenKind = "Punctuation";
				}
				currentToken->kind = "code-run";
				currentToken->attribute_set("token-kind", tokenKind);
				lastOffset = endOffset;
			}
			clang_disposeTokens(translationUnit, tokens, tokenCount);
			return {tree};
		}

		void build_doc_tree(TextTreePointer parent, CXComment comment)
		{
			auto addChildren = [&](TextTreePointer tree) {
				for (unsigned i = 0; i < clang_Comment_getNumChildren(comment);
				     i++)
				{
					auto child = clang_Comment_getChild(comment, i);
					build_doc_tree(tree, child);
				}
			};
			switch (clang_Comment_getKind(comment))
			{
				case CXComment_FullComment:
				{
					addChildren(parent);
					return;
				}
				case CXComment_Paragraph:
				{
					auto paragraph  = parent->new_child();
					paragraph->kind = "p";
					addChildren(paragraph);
					return;
				}
				case CXComment_Text:
				{
					String text = clang_TextComment_getText(comment);
					parent->append_text(text);
					parent->append_text("\n");
					return;
				}
				default:
					std::cerr << "Unhandled comment kind: "
					          << clang_Comment_getKind(comment) << std::endl;
					break;
			}
		}

		TextTreePointer build_doc_comment(const std::string &usr)
		{
			CXCursor declaration = clang_getNullCursor();
			auto     visit       = [&](CXCursor cursor, CXCursor parent) {
                if (clang_isDeclaration(cursor.kind) ||
                    (cursor.kind == CXCursor_MacroDefinition))
                {
                    if (usr ==
                        std::string_view{String(clang_getCursorUSR(cursor))})
                    {
                        declaration = cursor;
                        return CXChildVisit_Break;
                    }
                }
                return CXChildVisit_Recurse;
			};
			auto typeSpelling = [](CXType type) {
				std::string typeName = String(clang_getTypeSpelling(type));
				// Remove all occurrences of __capability
				std::string_view from      = " __capability";
				size_t           start_pos = 0;
				while ((start_pos = typeName.find(from, start_pos)) !=
				       std::string::npos)

				{
					typeName.erase(start_pos, from.length());
				}
				return typeName;
			};
			clang_visitChildren(
			  clang_getTranslationUnitCursor(translationUnit),
			  [](CXCursor cursor, CXCursor parent, CXClientData clientData) {
				  return (*static_cast<decltype(visit) *>(clientData))(cursor,
				                                                       parent);
			  },
			  &visit);
			if (clang_Cursor_isNull(declaration))
			{
				std::cerr << "Failed to find declaration for USR: " << usr
				          << std::endl;
				return nullptr;
			}
			auto tree  = TextTree::create();
			tree->kind = "clang-doc";
			if (clang_getCursorKind(declaration) == CXCursor_FunctionDecl)
			{
				auto functionTree = tree->new_child();
				auto addToken     = [&](const std::string &kind,
                                    const std::string &text) {
                    auto token  = functionTree->new_child();
                    token->kind = "code-run";
                    token->attribute_set("token-kind", kind);
                    token->append_text(text);
                    return token;
				};
				functionTree->kind = "code";
				functionTree->attribute_set("code-kind", "listing");
				tree->attribute_set("code-declaration-kind", "function");
				CXType type     = clang_getCursorType(declaration);
				auto   typeName = typeSpelling(clang_getResultType(type));
				addToken("TypeRef", typeName);
				functionTree->append_text(" ");
				String spelling = clang_getCursorSpelling(declaration);
				addToken("FunctionName", spelling);
				tree->attribute_set("code-declaration-entity", spelling);
				addToken("Punctuation", "(");
				for (int i = 0; i < clang_Cursor_getNumArguments(declaration);
				     i++)
				{
					CXCursor argument =
					  clang_Cursor_getArgument(declaration, i);
					CXType argumentType     = clang_getCursorType(argument);
					auto   argumentTypeName = typeSpelling(argumentType);
					String argumentName     = clang_getCursorSpelling(argument);
					addToken("TypeRef", argumentTypeName);
					functionTree->append_text(" ");
					addToken("ParamName", argumentName);
					if (i < clang_Cursor_getNumArguments(declaration) - 1)
					{
						addToken("Punctuation", ",");
						functionTree->append_text(" ");
					}
				}
				addToken("Punctuation", ")");
			}
			else if (clang_getCursorKind(declaration) ==
			         CXCursor_MacroDefinition)
			{
				auto macroTree = tree->new_child();
				auto addToken  = [&](const std::string &kind,
                                    const std::string &text) {
                    auto token  = macroTree->new_child();
                    token->kind = "code-run";
                    token->attribute_set("token-kind", kind);
                    token->append_text(text);
                    return token;
				};
				macroTree->kind = "code";
				macroTree->attribute_set("code-kind", "listing");
				tree->attribute_set("code-declaration-kind", "macro");
				String spelling = clang_getCursorSpelling(declaration);
				addToken("MacroName", spelling);
				tree->attribute_set("code-declaration-entity", spelling);
				// If this is a function-like macro, we can't just walk its
				// arguments (as we do for a function) because they're not
				// exposed.  Instead, we have to walk the tokens and stop when
				// we get to the close bracket that matches the first open
				// bracket.
				if (clang_Cursor_isMacroFunctionLike(declaration) > 0)
				{
					CXToken *tokens;
					unsigned tokenCount;
					clang_tokenize(translationUnit,
					               clang_getCursorExtent(declaration),
					               &tokens,
					               &tokenCount);
					unsigned bracketCount = 0;
					for (unsigned i = 1; i < tokenCount; i++)
					{
						String tokenSpelling =
						  clang_getTokenSpelling(translationUnit, tokens[i]);
						std::string_view tokenText = tokenSpelling;
						if (tokenText == "(")
						{
							bracketCount++;
						}
						else if (tokenText == ")")
						{
							bracketCount--;
						}
						addToken(token_kind(tokens[i]),
						         String{clang_getTokenSpelling(translationUnit,
						                                       tokens[i])});
						if (tokenText == ",")
						{
							macroTree->append_text(" ");
						}
						if (bracketCount == 0)
						{
							break;
						}
					}
					clang_disposeTokens(translationUnit, tokens, tokenCount);
				}

				// libclang doesn't expose parsed comments for macros, so we
				// have to do the parsing ourself.

				// Start by looking on the line before the macro definition.
				auto     location = clang_getCursorLocation(declaration);
				CXFile   file;
				unsigned line;
				clang_getExpansionLocation(
				  location, &file, &line, nullptr, nullptr);
				auto commentTree  = tree->new_child();
				commentTree->kind = "p";
				bool commentFound = false;
				line--;
				CXToken *commentTokens;
				unsigned commentTokenCount;
				// Clang reports comments as a single token.  If the line
				// before is the middle of a multi-line comment, we need to
				// keep going back until we find the start of the comment.
				//
				// FIXME: This currently finds the first comment.  If we have a
				// multi-line comment that uses /// instead of /** then we will
				// get only the last line.  CHERIoT's style guide tells you not
				// to do this, but we should handle it.
				do
				{
					CXSourceLocation commentLocation =
					  clang_getLocation(translationUnit, file, line, 1);
					clang_tokenize(
					  translationUnit,
					  clang_getRange(commentLocation, commentLocation),
					  &commentTokens,
					  &commentTokenCount);
					String comment =
					  clang_getTokenSpelling(translationUnit, commentTokens[0]);
					std::vector<CXCursor> cursors;
					cursors.resize(commentTokenCount);
					clang_annotateTokens(translationUnit,
					                     commentTokens,
					                     commentTokenCount,
					                     cursors.data());
					if (clang_getTokenKind(commentTokens[0]) == CXToken_Comment)
					{
						commentFound = true;
					}
					else
					{
						clang_disposeTokens(
						  translationUnit, commentTokens, commentTokenCount);
					}
				} while (!commentFound && (--line > 0));
				for (unsigned i = 0; i < commentTokenCount; i++)
				{
					// If we are using libclang's comment support, leading
					// asterisks and so on are stripped, but we can't currently
					// so do some basic stripping ourselves here.
					if (clang_getTokenKind(commentTokens[i]) == CXToken_Comment)
					{
						String tokenSpelling = clang_getTokenSpelling(
						  translationUnit, commentTokens[i]);
						std::stringstream ss(tokenSpelling);
						std::string       line;
						bool              isBlock;
						while (std::getline(ss, line, '\n'))
						{
							// Remove leading whitespace
							line =
							  std::regex_replace(line, std::regex("^ +"), "");
							// Remove leading /// or /*
							if (line.starts_with("///"))
							{
								line = line.substr(3);
							}
							else if (line.starts_with("/**"))
							{
								line    = line.substr(3);
								isBlock = true;
							}
							// If we're in a block comment, remove any leading *
							else if (isBlock)
							{
								if (line.starts_with("* "))
								{
									line = line.substr(2);
								}
								// If this is a single asterisk with no space
								// after it, skip it.
								if (line == "*")
								{
									commentTree       = tree->new_child();
									commentTree->kind = "p";
									continue;
								}
							}
							// If we're at the end, remove trailing */
							if (line.ends_with("*/"))
							{
								line = line.substr(0, line.size() - 2);
							}
							commentTree->append_text(line);
							commentTree->append_text(" \n");
						}
					}
				}
				clang_disposeTokens(
				  translationUnit, commentTokens, commentTokenCount);
				return tree;
			}
			else
			{
				CXSourceRange    range = clang_getCursorExtent(declaration);
				CXSourceLocation start = clang_getRangeStart(range);
				CXFile           file;
				clang_getExpansionLocation(
				  start, &file, nullptr, nullptr, nullptr);
				auto declarationTree =
				  build_source_range(String{clang_getFileName(file)},
				                     start,
				                     clang_getRangeEnd(range),
				                     false,
				                     true);
				tree->append_child(declarationTree);
				if (clang_getCursorKind(declaration) ==
				    CXCursor_FunctionTemplate)
				{
					tree->attribute_set("code-declaration-kind", "function");
					CXType type     = clang_getCursorType(declaration);
					String spelling = clang_getCursorSpelling(declaration);
					tree->attribute_set("code-declaration-entity", spelling);
				}
			}
			auto docComment = clang_Cursor_getParsedComment(declaration);
			build_doc_tree(tree, docComment);
			return tree;
		}

		~ClangTextBuilder()
		{
			clang_disposeTranslationUnit(translationUnit);
			clang_disposeIndex(index);
		}
	};
} // namespace

extern "C" void register_lua_helpers(sol::state &lua)
{
	lua.new_usertype<ClangTextBuilder>(
	  "ClangTextBuilder",
	  // We have to explicitly convert the table to a vector of strings,
	  // sol does not have generic machinery for going in this direction.
	  sol::factories(
	    [](const std::string &fileName, sol::table argumentsAsTable) {
		    std::vector<std::string> arguments;
		    for (auto &pair : argumentsAsTable)
		    {
			    arguments.push_back(pair.second.as<std::string>());
		    }
		    return std::make_shared<ClangTextBuilder>(fileName, arguments);
	    }),
	  "usrs_for_function",
	  &ClangTextBuilder::usrs_for_function,
	  "usrs_for_macro",
	  &ClangTextBuilder::usrs_for_macro,
	  "build_doc_comment",
	  &ClangTextBuilder::build_doc_comment,
	  "build_line_range",
	  &ClangTextBuilder::build_line_range);
}
