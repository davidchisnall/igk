#include <cstdlib>

#include <CLI/CLI.hpp>

#include <bit>
#include <cassert>
#include <filesystem>
#include <fmt/color.h>
#include <fstream>
#include <iostream>

// This hackery is needed only because of Homebrew shipping a broken ICU
// package.
#include <dlfcn.h>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "document.hh"
#include "passes.hh"
#include "sol.hh"
#include "sol.hpp"
#include "source_manager.hh"

struct TokenHandler
{
	virtual void command_start(SourceRange, std::string command) = 0;
	virtual void command_end(SourceRange)                        = 0;
	virtual void
	command_argument(SourceRange, std::string argument, std::string value) = 0;
	virtual void text(SourceRange, std::string text)                       = 0;
};

class TextTreeBuilder : public TokenHandler
{
	TextTreePointer root    = TextTree::create();
	TextTreePointer current = root;

	virtual void command_start(SourceRange range, std::string command)
	{
		current = current->new_child();
		assert(current);
		current->sourceRange = range;
		current->kind        = command;
	}

	virtual void command_end(SourceRange range)
	{
		current->sourceRange.second = range.second;
		current                     = current->parent();
		if (!current)
		{
			SourceManager::shared_instance().report_error(
			  range.first,
			  range.second,
			  "Terminating unopened command",
			  SourceManager::Severity::Fatal);
			throw std::logic_error("Terminating unopened command");
		}
	}

	virtual void
	command_argument(SourceRange, std::string argument, std::string value)
	{
		current->attribute(argument) = value;
	}
	virtual void text(SourceRange, std::string text)
	{
		current->append_text(text);
	}

	public:
	std::shared_ptr<TextTree> complete()
	{
		return root;
	}
};

struct DebugTokenHandler : public TokenHandler
{
	SourceManager &sourceManager;
	DebugTokenHandler(SourceManager &sourceManager)
	  : sourceManager(sourceManager)
	{
	}

	void command_start(SourceRange range, std::string command)
	{
		std::cerr << "start command: " << command << std::endl;
		sourceManager.report_error(range.first,
		                           range.second,
		                           "start command",
		                           SourceManager::Severity::Warning);
	}

	void command_end(SourceRange)
	{
		std::cerr << "end command" << std::endl;
	}

	void
	command_argument(SourceRange range, std::string argument, std::string value)
	{
		sourceManager.report_error(range.first,
		                           range.second,
		                           "argument",
		                           SourceManager::Severity::Error);
		std::cerr << "argument: " << argument << " value: " << value
		          << std::endl;
	}

	void text(SourceRange, std::string text)
	{
		std::cerr << "text: " << text << std::endl;
	}
};

/**
 * UTF8Stream is a class that wraps a `std::string` that contains UTF-8 text
 * and provides an interface that lets you safely examine individual unicode
 * code points.
 */
class UTF8Stream
{
	/**
	 * The start of the current run being examined.
	 */
	std::string::const_iterator tokenStart;
	/**
	 * The start of the entire string, used to calculate the index of the
	 * current iterator.
	 */
	std::string::const_iterator beginning;
	/**
	 * The current location in the string being examined.
	 */
	std::string::const_iterator current;
	/**
	 * The end of the string.
	 */
	std::string::const_iterator end;
	/**
	 * The current line number.
	 */
	size_t currentLine = 1;

	public:
	/**
	 * Construct a new UTF8Stream from a string.
	 */
	UTF8Stream(const std::string &text)
	{
		current    = text.begin();
		end        = text.end();
		tokenStart = current;
		beginning  = current;
	}

	/**
	 * Returns the current byte index in the string.
	 */
	[[nodiscard]] size_t index() const
	{
		return std::distance(beginning, current);
	}

	/**
	 * Returns the size of the wrapped string, in bytes.
	 */
	size_t size()
	{
		return std::distance(current, end);
	}

	/**
	 * Returns the current unicode code point for the first character in the
	 * string that hasn't yet been consumed.  Returns 0 if we have reached the
	 * end of the string.
	 */
	char32_t peek()
	{
		if (current == end)
		{
			return 0;
		}
		char32_t c;
		int32_t  offset = 0;
		U8_NEXT(current, offset, size(), c);
		return c;
	}

	/**
	 * Returns the unicode code point for the character after the next one, or
	 * zero if that would be after the end of the string.
	 */
	char32_t peek_ahead()
	{
		char32_t c;
		int32_t  offset = 0;
		U8_NEXT(current, offset, size(), c);
		U8_NEXT(current, offset, size(), c);
		// U_NEXT sets c to <0 in case of error
		c = std::max<char32_t>(c, 0);
		return c;
	}

	/**
	 * Advance to the next code point in the string, returning the new value.
	 */
	char32_t next()
	{
		if (current == end)
		{
			return 0;
		}
		char32_t c;
		int32_t  offset = 0;
		U8_NEXT(current, offset, size(), c);
		current += offset;
		if (c == U'\n')
		{
			currentLine++;
		}
		return c;
	}

	/**
	 * Construct a string from the token start marker to the current location,
	 * and advance the token start marker.
	 */
	std::string token()
	{
		std::string ret(tokenStart, current);
		tokenStart = current;
		return ret;
	}

	/**
	 * Advance the token-start marker to the current location, effectively
	 * discarding all characters before the current point that have not been
	 * handled.
	 */
	void drop()
	{
		next();
		tokenStart = current;
	}

	/**
	 * Returns true if the current character is a whitespace character as
	 * defined by Unicode, false otherwise.
	 */
	bool isspace()
	{
		return u_isUWhiteSpace(peek());
	}

	/**
	 * Returns true if the current character is an alphanumeric character as
	 * defined by Unicode, false otherwise.
	 */
	bool isalnum()
	{
		return u_isalnum(peek());
	}

	/**
	 * If the current character is c, consume it and return true.  Otherwise,
	 * return false.
	 *
	 * Consuming a character advances the token-start marker to after that
	 * character.
	 */
	bool consume(char32_t c)
	{
		if (peek() == c)
		{
			drop();
			return true;
		}
		return false;
	}

	/**
	 * Drop all whitespace characters until a non-whitespace character is
	 * found.
	 */
	void drop_space()
	{
		while (isspace())
		{
			drop();
		}
	}

	size_t line()
	{
		return currentLine;
	}
};

/**
 * Scan a string of text in a TeX / SILE-style format.
 */
class TeXStyleScanner
{
	/**
	 * The handler that will be called when tokens are found.
	 */
	TokenHandler &handler;
	/**
	 * The UTF8Stream that wraps the text being scanned.
	 */
	UTF8Stream stream;

	/**
	 * The source manager that keeps track of file names and locations.
	 */
	SourceManager &sourceManager;

	/**
	 * Identity of the current file.
	 */
	uint32_t fileID;

	/**
	 * Helper that returns the current source location.
	 */
	SourceLocation current_location()
	{
		return sourceManager.compress(fileID, stream.line(), stream.index());
	}

	/**
	 * Helper that returns a sequence of characters that are all alphanumeric.
	 */
	std::string read_word()
	{
		while (stream.isalnum())
		{
			stream.next();
		}
		return stream.token();
	}

	std::string read_command()
	{
		if (!stream.isalnum())
		{
			return "";
		}
		while (!(stream.isspace() || (stream.peek() == U'[') ||
		         (stream.peek() == U'{')))
		{
			stream.next();
		}
		return stream.token();
	}

	void skip_comments()
	{
		while (stream.peek() == U'%')
		{
			while (stream.peek() != U'\n')
			{
				stream.drop();
			}
			stream.drop();
		}
	}

	/**
	 * Read a run of text until we see a backslash or a closing brace.
	 */
	std::string read_text_run()
	{
		bool        escaped = false;
		std::string text;
		while ((stream.peek() != U'}') && (stream.peek() != 0))
		{
			char32_t c = stream.peek();
			if (c == U'\\')
			{
				char32_t next = stream.peek_ahead();
				// If we see \%, drop the \ and keep the %.
				if (next == U'%')
				{
					stream.drop();
					continue;
				}
				// std::cerr << "Found \\, next is " << (char)next << std::endl;
				if ((next == U'\\') || (next == U'}'))
				{
					text += stream.token();
					text.push_back(next);
					stream.next();
					stream.drop();
					continue;
				}
				break;
			}
			if (stream.peek() == U'%')
			{
				text += stream.token();
				skip_comments();
			}
			stream.next();
		}
		text += stream.token();
		return text;
	}

	/**
	 * Parse the text, alternating between runs of text and commands.  This
	 * does not maintain a stack of open commands, it is the responsibility of
	 * the handler to do that.
	 */
	void parse_text()
	{
		while (stream.peek() != 0)
		{
			SourceLocation start = current_location();
			if (stream.consume(U'}'))
			{
				SourceLocation end = current_location();
				handler.command_end({start, end});
				continue;
			}
			start               = current_location();
			std::string    text = read_text_run();
			SourceLocation end  = current_location();
			if (!text.empty())
			{
				handler.text({start, end}, text);
			}
			if (stream.consume(U'\\'))
			{
				scan_command();
			}
		}
	}

	/**
	 * Scan a command.  Commands are one of the following forms:
	 *
	 *  - \command - no arguments
	 *  - \command[argument1=value1,argument2=value2,...] - arguments but no
	 *    body
	 *  - \command[argument1=value1,argument2=value2,...]{body} - arguments and
	 *    a body
	 *  - \command{body} - no arguments but a body
	 */
	void scan_command()
	{
		SourceLocation start   = current_location();
		std::string    command = read_command();
		handler.command_start({start, current_location()}, command);
		if (stream.consume(U'['))
		{
			do
			{
				stream.drop_space();
				SourceLocation argumentStart = current_location();
				std::string    argumentName  = read_word();
				std::string    value;
				stream.drop_space();
				if (stream.consume(U'='))
				{
					stream.drop_space();
					if (stream.consume(U'"'))
					{
						while (stream.peek() != U'"')
						{
							if (stream.peek() == U'\\' &&
							    stream.peek_ahead() == U'"')
							{
								value += stream.token();
								stream.next();
							}
							stream.next();
						}
						value += stream.token();
						stream.consume(U'"');
					}
					else
					{
						// Skip until we see a comma for the next argument or a
						// closing bracket for the end of arguments.
						while ((stream.peek() != (U',')) &&
						       (stream.peek() != (U']')))
						{
							stream.next();
						}
						value = stream.token();
					}
					// If we see a comma, drop it and then we'll loop and parse
					// the next one.
					stream.consume(U',');
				}
				else if (!stream.consume(U',') && (stream.peek() != U']'))
				{
					SourceManager::shared_instance().report_error(
					  current_location(),
					  current_location(),
					  "Unexpected value parsing arguments",
					  SourceManager::Severity::Fatal);
					throw std::logic_error(
					  "Unexpected value parsing arguments");
				}
				handler.command_argument(
				  {argumentStart, current_location()}, argumentName, value);
			} while (!stream.consume(U']'));
		}
		if (!stream.consume(U'{'))
		{
			handler.command_end({start, current_location()});
		}
	}

	public:
	/**
	 * Constructor.  This class is ephemeral: it scans `text` and calls
	 * `handler`, then it is finished.
	 */
	TeXStyleScanner(SourceManager     &sourceManager,
	                size_t             fileID,
	                const std::string &text,
	                TokenHandler     &&handler)
	  : stream(text),
	    handler(handler),
	    sourceManager(sourceManager),
	    fileID(fileID)
	{
		parse_text();
	}
	TeXStyleScanner(SourceManager     &sourceManager,
	                size_t             fileID,
	                const std::string &text,
	                TokenHandler      &handler)
	  : stream(text),
	    handler(handler),
	    sourceManager(sourceManager),
	    fileID(fileID)
	{
		parse_text();
	}
};

TextTreePointer read_file(const std::filesystem::path &inputPath)
{
	std::ifstream file{inputPath, std::ios::binary};
	std::string   text;
	auto          size = std::filesystem::file_size(inputPath);
	text.resize(size);
	text.assign(std::istreambuf_iterator<char>(file),
	            std::istreambuf_iterator<char>());
	TextTreeBuilder treeBuilder;
	SourceManager  &sourceManager = SourceManager::shared_instance();
	auto [fileID, contents] =
	  sourceManager.add_file(inputPath, std::move(text));
	try
	{
		TeXStyleScanner(sourceManager, fileID, contents, treeBuilder);
		return treeBuilder.complete();
	}
	catch (const std::exception &e)
	{
		return nullptr;
	}
}

std::string
replace_all(std::string str, const std::string &from, const std::string &to)
{
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
	return str;
}

class TeXOutputPass : public OutputPass
{
	void visitor(const TextTree::Child &node)
	{
		std::visit(
		  [this](auto &child) {
			  if constexpr (std::is_same_v<
			                  TextTreePointer,
			                  std::remove_cvref_t<decltype(child)>>)
			  {
				  if (!child->kind.empty())
				  {
					  out() << '\\' << child->kind;
				  }
				  if (!child->attributes().empty())
				  {
					  out() << '[';
					  bool first = true;
					  for (auto &attr : child->attributes())
					  {
						  if (!first)
						  {
							  out() << ',';
						  }
						  if (attr.first.empty())
						  {
							  out() << attr.second;
						  }
						  else
						  {
							  // If the string contains a comma, quote it and
							  // escape quotes.
							  if (attr.second.contains(','))
							  {
								  std::string escaped = attr.second;
								  for (size_t pos = 0; pos < escaped.size();
								       pos++)
								  {
									  if (escaped[pos] == '"')
									  {
										  escaped.insert(pos, 1, '\\');
										  pos++;
									  }
								  }
								  out()
								    << attr.first << "=\"" << escaped << '"';
							  }
							  else
							  {
								  out() << attr.first << '=' << attr.second;
							  }
						  }
						  first = false;
					  }
					  out() << ']';
				  }
				  if (!(child->kind.empty() && child->attributes().empty()))
				  {
					  out() << '{';
				  }
				  child->const_visit(
				    [this](auto node) { return visitor(node); });
				  if (!(child->kind.empty() && child->attributes().empty()))
				  {
					  out() << '}';
				  }
			  }
			  else
			  {
				  if (child.contains('\\') || child.contains('}'))
				  {
					  std::string escaped = child;
					  for (size_t pos = 0; pos < escaped.size(); pos++)
					  {
						  if ((escaped[pos] == '\\') || (escaped[pos] == '}'))
						  {
							  escaped.insert(pos, 1, '\\');
							  pos++;
						  }
					  }
					  out() << escaped;
				  }
				  else
				  {
					  out() << child;
				  }
			  }
		  },
		  node);
	}

	public:
	TextTreePointer process(TextTreePointer tree) override
	{
		if (!tree)
		{
			return nullptr;
		}
		visitor(tree);
		return tree;
	}

	static std::string name()
	{
		return "TeXOutputPass";
	}
};

void TextTree::dump()
{
	TeXOutputPass out;
	out.output_stderr();
	out.process(shared_from_this());
}

template<bool XMLTags>
class XHTMLOutputPass : public OutputPass
{
	/**
	 * HTML defines some tags as void (they do not need a close element).
	 */
	inline static std::unordered_set<std::string> VoidTags = {"area",
	                                                          "base",
	                                                          "br",
	                                                          "col",
	                                                          "command",
	                                                          "embed",
	                                                          "hr",
	                                                          "img",
	                                                          "input",
	                                                          "keygen",
	                                                          "link",
	                                                          "meta",
	                                                          "param",
	                                                          "source",
	                                                          "track",
	                                                          "wbr"};

	void visitor(const TextTree::Child &node)
	{
		std::visit(
		  [this](auto &child) {
			  if constexpr (std::is_same_v<
			                  TextTreePointer,
			                  std::remove_cvref_t<decltype(child)>>)
			  {
				  if (!child->kind.empty())
				  {
					  out() << "<" << child->kind;
				  }
				  if (!child->attributes().empty())
				  {
					  for (auto &attr : child->attributes())
					  {
						  out() << ' ';
						  if (attr.first.empty())
						  {
							  out() << attr.second;
						  }
						  else
						  {
							  std::string escaped =
							    replace_all(attr.second, "\"", "&quot;");
							  out() << attr.first << "=\"" << escaped << "\"";
						  }
					  }
				  }
				  if (XMLTags && child->children.empty())
				  {
					  if (!child->kind.empty())
					  {
						  out() << " />";
					  }
				  }
				  else
				  {
					  if (!child->kind.empty())
					  {
						  out() << ">";
					  }
					  child->const_visit(
					    [this](auto node) { return visitor(node); });
					  if (!child->kind.empty() &&
					      (XMLTags || !VoidTags.contains(child->kind)))
					  {
						  out() << "</" << child->kind << '>';
					  }
				  }
			  }
			  else
			  {
				  // FIXME: Escape more XML entities
				  std::string escaped = replace_all(child, "&", "&amp;");
				  escaped             = replace_all(escaped, "\"", "&quot;");
				  escaped             = replace_all(escaped, "<", "&lt;");
				  escaped             = replace_all(escaped, ">", "&gt;");
				  out() << escaped;
			  }
		  },
		  node);
	}

	TextTreePointer process(TextTreePointer tree) override
	{
		if (!tree)
		{
			return nullptr;
		}
		visitor(tree);
		return tree;
	}

	public:
	static std::string name()
	{
		return XMLTags ? "XMLOutputPass" : "HTMLOutputPass";
	}
};

using HTMLOutputPass = XHTMLOutputPass<false>;
using XMLOutputPass  = XHTMLOutputPass<true>;

class LuaPass : public TextPass
{
	public:
	using PluginHook = std::function<void(sol::state &)>;

	static void config_set(std::string_view key, std::string_view value)
	{
		std::string k(key);
		if ((value.size() == 0) || (value == "true"))
		{
			config[k] = true;
			return;
		}
		if (value == "false")
		{
			config[k] = false;
			return;
		}
		std::string v(value);
		size_t      end = 0;
		try
		{
			double doubleValue = std::stod(v, &end);
			if (end == value.size())
			{
				config[k] = doubleValue;
				return;
			}
		}
		catch (...)
		{
		}
		config[k] = std::move(v);
	}

	private:
	/**
	 * State object shared across all passes that can be used to pass
	 * information between passes.  Each pass is run in a clean Lua VM, so this
	 * can store only strings.
	 */
	inline static std::unordered_map<std::string,
	                                 std::variant<double, bool, std::string>>
	  config;

	inline static std::vector<std::function<void(sol::state &)>> plugins;
	sol::state                                                   lua;
	std::function<TextTreePointer(TextTreePointer)> processFunction;

	static TextTreePointer create_from_lua(sol::table object)
	{
		auto tree = TextTree::create();
		auto kind = object["kind"];
		if (kind == nullptr)
		{
			return nullptr;
		}
		tree->kind      = kind;
		auto attributes = object["attributes"];
		if (attributes.is<sol::table>())
		{
			for (auto &[key, value] : object.get<sol::table>("attributes"))
			{
				tree->attribute_set(key.as<std::string>(),
				                    sol::utility::to_string(value));
			}
		}
		auto children = object["children"];
		if (children.is<sol::table>())
		{
			for (auto &[key, value] : object.get<sol::table>("children"))
			{
				if (value.is<TextTree>())
				{
					tree->append_child(value.as<TextTreePointer>());
				}
				else if (value.get_type() == sol::type::string)
				{
					tree->append_text(value.as<std::string>());
				}
				else
				{
					auto child = create_from_lua(value);
					if (child)
					{
						tree->append_child(child);
					}
				}
			}
		}
		return tree;
	}

	TextTreePointer process(TextTreePointer tree) override
	{
		return processFunction(tree);
	}

	public:
	LuaPass(std::string filename)
	{
		lua.open_libraries(sol::lib::base,
		                   sol::lib::package,
		                   sol::lib::io,
		                   sol::lib::table,
		                   sol::lib::string);
		lua.new_usertype<TextPass>("TextPass",
		                           sol::no_constructor,
		                           "process",
		                           &TextPass::process,
		                           "as_output_pass",
		                           [](TextPass &pass) -> OutputPass * {
			                           return dynamic_cast<OutputPass *>(&pass);
		                           });
		lua.new_usertype<OutputPass>("OutputPass",
		                             "output_file",
		                             &OutputPass::output_file,
		                             "output_stderr",
		                             &OutputPass::output_stderr,
		                             "output_stdout",
		                             &OutputPass::output_stdout,
		                             "process",
		                             &TextPass::process);
		sol::usertype<TextTree> textTreeType = lua.new_usertype<TextTree>(
		  "TextTree",
		  "new",
		  sol::factories([](std::optional<std::string> kind) {
			  auto tree = TextTree::create();
			  if (kind)
			  {
				  tree->kind = *kind;
			  }
			  return tree;
		  }),
		  "create",
		  &create_from_lua,
		  "kind",
		  &TextTree::kind,
		  "visit",
		  &TextTree::visit,
		  "match",
		  [](
		    TextTree &textTree, std::string kind, TextTree::Visitor &&visitor) {
			  return textTree.match(kind, visitor);
		  },
		  "match_any",
		  [](TextTree                       &textTree,
		     std::unordered_set<std::string> kinds,
		     TextTree::Visitor             &&visitor) {
			  return textTree.match_any(kinds, visitor);
		  },
		  "is_empty",
		  &TextTree::is_empty,
		  "children",
		  &TextTree::children,
		  "new_child",
		  sol::factories(
		    [](TextTree &textTree, std::optional<std::string> kind) {
			    auto tree = textTree.new_child();
			    if (kind)
			    {
				    tree->kind = *kind;
			    }
			    return tree;
		    }),
		  "append_child",
		  &TextTree::append_child,
		  "extract_children",
		  &TextTree::extract_children,
		  "append_text",
		  &TextTree::append_text,
		  "insert_text",
		  [](TextTree &textTree, size_t index, const std::string &text) {
			  // Convert from Lua 1-based index to C++ 0-based index.
			  textTree.insert_text(index - 1, text);
		  },
		  "split_at_byte_index",
		  [](TextTree &textTree,
		     size_t    index) -> std::array<TextTree::Child, 2> {
			  auto [left, right] = textTree.split_at_byte_index(index);
			  return {left, right};
		  },
		  "dump",
		  &TextTree::dump,
		  "source_file_name",
		  [](TextTree &textTree) {
			  auto &sm = SourceManager::shared_instance();
			  return sm.file_for_id(
			    sm.expand(textTree.sourceRange.first).fileID);
		  },
		  "source_directory_name",
		  [](TextTree &textTree) {
			  auto &sm = SourceManager::shared_instance();
			  auto &file =
			    sm.file_for_id(sm.expand(textTree.sourceRange.first).fileID);
			  std::filesystem::path path = file;
			  if (path.has_parent_path())
			  {
				  return path.parent_path().string();
			  }
			  return std::filesystem::current_path().string();
		  },
		  "deep_clone",
		  &TextTree::deep_clone,
		  "shallow_clone",
		  &TextTree::shallow_clone,
		  "find_string",
		  &TextTree::find_string,
		  "take_children",
		  &TextTree::take_children,
		  "error",
		  [](TextTree &textTree, const std::string &message) {
			  SourceManager::shared_instance().report_error(
			    textTree.sourceRange.first,
			    textTree.sourceRange.second,
			    message,
			    SourceManager::Severity::Error);
		  },
		  "fatal_error",
		  [](TextTree &textTree, const std::string &message) {
			  SourceManager::shared_instance().report_error(
			    textTree.sourceRange.first,
			    textTree.sourceRange.second,
			    message,
			    SourceManager::Severity::Fatal);
		  },
		  "clear",
		  &TextTree::clear,
		  "text",
		  &TextTree::text,
		  "parent",
		  static_cast<TextTreePointer (TextTree::*)() const>(&TextTree::parent),
		  "has_attribute",
		  &TextTree::has_attribute,
		  "attributes",
		  &TextTree::attributes,
		  "attribute_erase",
		  &TextTree::attribute_erase,
		  "attribute_set",
		  &TextTree::attribute_set,
		  "attribute",
		  &TextTree::attribute);
		lua["create_pass"] = &TextPassRegistry::create;
		lua["config"]      = &config;
		lua["read_file"]   = [](std::string path) { return read_file(path); };
		for (auto &plugin : plugins)
		{
			plugin(lua);
		}
		// Redirect stdout to stderr
		lua.script("print = function(...)\n"
		           "for i,arg in ipairs({...}) do\n"
		           "if i > 1 then\n"
		           "io.stderr:write('\\t')\n"
		           "end\n"
		           "io.stderr:write(tostring(arg))\n"
		           "end\n"
		           "io.stderr:write('\\n')\n"
		           "end\n");
		lua.script_file(filename);
		processFunction = lua["process"];
	}

	static void add_plugin(PluginHook hook)
	{
		plugins.push_back(hook);
	}
};

class LuaPassFactory : public TextPassFactory
{
	std::filesystem::path path;
	LuaPassFactory(std::filesystem::path path) : path(path) {}
	std::string name() override
	{
		return path.stem();
	}
	std::shared_ptr<TextPass> create() override
	{
		return std::make_shared<LuaPass>(path);
	}

	public:
	static void register_lua_directory(const std::filesystem::path path)
	{
		for (const auto &entry : std::filesystem::directory_iterator(path))
		{
			if (entry.path().extension() == ".lua")
			{
				struct MakeSharedEnabler : public LuaPassFactory
				{
					MakeSharedEnabler(const std::filesystem::path path)
					  : LuaPassFactory(path)
					{
					}
				};
				TextPassRegistry::add(
				  std::make_shared<MakeSharedEnabler>(entry.path()));
			}
		}
	}
};

int main(int argc, char *argv[])
{
	std::vector<std::filesystem::path> luaPaths;
	std::vector<std::filesystem::path> pluginPaths;
	std::filesystem::path              inputPath;
	std::vector<std::string>           passNames;
	bool                               printAfterAll = false;

	auto registerConfigOption = [&](std::string_view option) {
		auto eq = option.find('=');
		if ((eq == std::string_view::npos) || (eq == 0))
		{
			throw CLI::ValidationError(
			  "config options must be key=value pairs");
		}
		auto key   = option.substr(0, eq);
		auto value = option.substr(eq + 1);
		LuaPass::config_set(key, value);
	};

	CLI::App app;
	app.add_option("--config")
	  ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll)
	  ->each(registerConfigOption);
	app
	  .add_option(
	    "--lua-directory",
	    luaPaths,
	    "Directory containing Lua passes (may be specified more than once)")
	  ->check(CLI::ExistingDirectory);
	app.add_option("--file", inputPath, "Input file to process")
	  ->required()
	  ->check(CLI::ExistingFile);
	app.add_flag("--print-after-all",
	             printAfterAll,
	             "Print the tree after each pass runs");
	app
	  .add_option("--plugin",
	              pluginPaths,
	              "Path to a plugin (may be specified more than once)")
	  ->check(CLI::ExistingFile);
	app.add_option(
	  "--pass", passNames, "Passes to run (may be specified more than once)");

	CLI11_PARSE(app, argc, argv);

	// Open plugins
	for (auto &path : pluginPaths)
	{
		auto handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
		if (!handle)
		{
			std::cerr << "Failed to open plugin " << path << ": " << dlerror()
			          << std::endl;
			return EXIT_FAILURE;
		}
		auto luaRegisterHook = reinterpret_cast<void (*)(sol::state &)>(
		  dlsym(handle, "register_lua_helpers"));
		if (luaRegisterHook)
		{
			LuaPass::add_plugin(luaRegisterHook);
		}
		else
		{
			std::cerr << "No register hook found in plugin " << path
			          << std::endl;
		}
	}

	// Register built-in passes
	TextPassRegistry::add<XMLOutputPass>();
	TextPassRegistry::add<HTMLOutputPass>();
	TextPassRegistry::add<TeXOutputPass>();
	// Register Lua passes
	for (auto &dir : luaPaths)
	{
		LuaPassFactory::register_lua_directory(dir);
	}
	auto tree = read_file(inputPath);
	for (auto &name : passNames)
	{
		auto pass = TextPassRegistry().create(name);
		if (pass)
		{
			std::cerr << "Running pass: " << name << std::endl;
			tree = pass->process(tree);
			if (printAfterAll)
			{
				std::cerr << "After pass: " << name << std::endl;
				tree->dump();
			}
		}
		else
		{
			std::cerr << "Unknown pass: " << name << std::endl;
		}
	}
	return 0;
}
