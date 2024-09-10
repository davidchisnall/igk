#include <cstdlib>

#include <CLI/CLI.hpp>

#include <bit>
#include <cassert>
#include <fmt/color.h>
#include <fstream>
#include <iostream>

// This hackery is needed only because of Homebrew shipping a broken ICU
// package.
#include <dlfcn.h>
#include <memory>
#include <type_traits>
#include <unordered_map>
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
		current              = current->new_child();
		current->sourceRange = range;
		current->kind        = command;
	}

	virtual void command_end(SourceRange range)
	{
		current->sourceRange.second = range.second;
		current                     = current->parent();
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
		std::cout << "start command: " << command << std::endl;
		sourceManager.report_error(range.first,
		                           range.second,
		                           "start command",
		                           SourceManager::Severity::Warning);
	}

	void command_end(SourceRange)
	{
		std::cout << "end command" << std::endl;
	}

	void
	command_argument(SourceRange range, std::string argument, std::string value)
	{
		sourceManager.report_error(range.first,
		                           range.second,
		                           "argument",
		                           SourceManager::Severity::Error);
		std::cout << "argument: " << argument << " value: " << value
		          << std::endl;
	}

	void text(SourceRange, std::string text)
	{
		std::cout << "text: " << text << std::endl;
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
			skip_comments();
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
		std::string    command = read_word();
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
					  std::cout << '\\' << child->kind;
				  }
				  if (!child->attributes().empty())
				  {
					  std::cout << '[';
					  bool first = true;
					  for (auto &attr : child->attributes())
					  {
						  if (!first)
						  {
							  std::cout << ',';
						  }
						  if (attr.first.empty())
						  {
							  std::cout << attr.second;
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
								  std::cout << attr.first << "=\"" << escaped
								            << '"';
							  }
							  else
							  {
								  std::cout << attr.first << '=' << attr.second;
							  }
						  }
						  first = false;
					  }
					  std::cout << ']';
				  }
				  if (!(child->kind.empty() && child->attributes().empty()))
				  {
					  std::cout << '{';
				  }
				  child->const_visit(
				    [this](auto node) { return visitor(node); });
				  if (!(child->kind.empty() && child->attributes().empty()))
				  {
					  std::cout << '}';
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
					  std::cout << escaped;
				  }
				  else
				  {
					  std::cout << child;
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
	TeXOutputPass().process(shared_from_this());
}

template<bool XMLTags>
class XHTMLOutputPass : public OutputPass
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
					  if (!child->kind.empty())
					  {
						  out() << "</" << child->kind << '>';
					  }
				  }
			  }
			  else
			  {
				  // FIXME: Escape more XML entities
				  std::string escaped = replace_all(child, "\"", "&quot;");
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

	private:
	inline static std::vector<std::function<void(sol::state &)>> plugins;
	sol::state                                                   lua;
	std::function<TextTreePointer(TextTreePointer)> processFunction;

	TextTreePointer process(TextTreePointer tree) override
	{
		struct Test
		{
			int x;
			Test(int x) : x(x) {}
		};
		lua.new_usertype<Test>(
		  "Test", "new", sol::constructors<Test(int)>(), "x", &Test::x);
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
		  "kind",
		  &TextTree::kind,
		  "visit",
		  &TextTree::visit,
		  "is_empty",
		  &TextTree::is_empty,
		  "children",
		  &TextTree::children,
		  "new_child",
		  &TextTree::new_child,
		  "append_child",
		  &TextTree::append_child,
		  "extract_children",
		  &TextTree::extract_children,
		  "append_text",
		  &TextTree::append_text,
		  "split_at_byte_index",
		  [](TextTree &textTree,
		     size_t    index) -> std::array<TextTree::Child, 2> {
			  auto [left, right] = textTree.split_at_byte_index(index);
			  return {left, right};
		  },
		  "dump",
		  &TextTree::dump,
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
		  "clear",
		  &TextTree::clear,
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
		sol::usertype<TextPass> textPassType =
		  lua.new_usertype<TextPass>("TextPass", "process", &TextPass::process);
		lua["create_pass"] = &TextPassRegistry::create;
		for (auto &plugin : plugins)
		{
			plugin(lua);
		}
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
	CLI::App                           app;
	app
	  .add_option(
	    "--lua-directory",
	    luaPaths,
	    "Directory containing Lua passes (may be specified more than once)")
	  ->check(CLI::ExistingDirectory);
	app.add_option("--file", inputPath, "Input file to process")
	  ->required()
	  ->check(CLI::ExistingFile);
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
	TeXStyleScanner(sourceManager, fileID, contents, treeBuilder);
	auto tree = treeBuilder.complete();
	for (auto &name : passNames)
	{
		auto pass = TextPassRegistry().create(name);
		if (pass)
		{
			std::cerr << "Running pass: " << name << std::endl;
			tree = pass->process(tree);
		}
		else
		{
			std::cerr << "Unknown pass: " << name << std::endl;
		}
	}
	return 0;
}
