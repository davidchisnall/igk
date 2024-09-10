#include "document.hh"
#include "sol.hh"
#include <clang-c/Documentation.h>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
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
		};
		CXIndex index = clang_createIndex(0, 1);

		CXTranslationUnit translationUnit;

		CXChildVisitResult visit(CXCursor cursor, CXCursor parent)
		{
			if (CXCursor_FunctionDecl == (cursor.kind))
			{
				std::cerr
				  << "Cursor USR: "
				  << std::string_view(String(clang_getCursorUSR(cursor)))
				  << " Spelling: "
				  << std::string_view(String(clang_getCursorSpelling(cursor)))
				  << std::endl;
			}

			return CXChildVisit_Recurse;
		}

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

		std::vector<std::string>
		usrs_for_function(const std::string &functionName)
		{
			std::unordered_set<std::string> usrs;
			auto visit = [&](CXCursor cursor, CXCursor parent) {
				if (CXCursor_FunctionDecl == cursor.kind)
				{
					String spelling = String(clang_getCursorSpelling(cursor));
					if (functionName == std::string_view{spelling})
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
		                                   bool skipTrailingLine = false)
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

			std::cerr << "Start: " << start << " End: " << end << std::endl;
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
			clang_annotateTokens(
			  translationUnit, tokens, tokenCount, cursors.data());
			CXCursor        last      = clang_getNullCursor();
			auto            tokenTree = TextTree::create();
			TextTreePointer currentToken;
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
				// If we've gone past the end of the range, stop.  We will scan
				// up to the line with the end on it, to make sure that we get
				// the last token.
				if (startOffset > end)
				{
					break;
				}
				// If two tokens are part of the same cursor, combine them.
				if (clang_equalCursors(cursor, last))
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
				last = cursor;
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
						switch (clang_getTokenKind(token))
						{
							case CXToken_Comment:
								tokenKind = "Comment";
								break;
							case CXToken_Identifier:
								tokenKind = "Identifier";
								break;
							case CXToken_Punctuation:
								tokenKind = "Punctuation";
								break;
							case CXToken_Keyword:
								tokenKind = "Keyword";
								break;
							case CXToken_Literal:
								tokenKind = "Literal";
								break;
						}
						break;
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
                if (clang_isDeclaration(cursor.kind))
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
				auto functionTree  = tree->new_child();
				auto addToken 	= [&](const std::string &kind, const std::string &text) {
					auto token = functionTree->new_child();
					token->kind = "code-run";
					token->attribute_set("token-kind", kind);
					token->append_text(text);
					return token;
				};
				functionTree->kind = "function";
				CXType type        = clang_getCursorType(declaration);
				String typeName =
				  clang_getTypeSpelling(clang_getResultType(type));
				addToken("TypeRef", typeName);
				functionTree->append_text(" ");
				auto last = functionTree->children.back();
				if (!std::holds_alternative<std::string>(last))
				{
					std::cerr << "Last is not a string!" << std::endl;
				}
				else
				{
					std::cerr << "Last is a string: "
					          << std::get<std::string>(last) << std::endl;
				}
				String spelling = clang_getCursorSpelling(declaration);
				addToken("FunctionName", spelling);
				addToken("Punctuation", "(");
				for (unsigned i = 0;
				     i < clang_Cursor_getNumArguments(declaration);
				     i++)
				{
					CXCursor argument =
					  clang_Cursor_getArgument(declaration, i);
					CXType argumentType = clang_getCursorType(argument);
					String argumentTypeName =
					  clang_getTypeSpelling(argumentType);
					String argumentName = clang_getCursorSpelling(argument);
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
				functionTree->dump();
			}
			else
			{
				CXSourceRange    range = clang_getCursorExtent(declaration);
				CXSourceLocation start = clang_getRangeStart(range);
				CXFile           file;
				String printed = clang_getCursorPrettyPrinted(declaration, 0);
				std::cerr << "Pretty printed: " << (std::string_view)printed
				          << std::endl;
				clang_getExpansionLocation(
				  start, &file, nullptr, nullptr, nullptr);
				auto declarationTree =
				  build_source_range(String{clang_getFileName(file)},
				                     start,
				                     clang_getRangeEnd(range));
				tree->append_child(declarationTree);
			}
			auto docComment = clang_Cursor_getParsedComment(declaration);
			build_doc_tree(tree, docComment);
			tree->dump();
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
	  "build_doc_comment",
	  &ClangTextBuilder::build_doc_comment,
	  "build_line_range",
	  &ClangTextBuilder::build_line_range);
}
