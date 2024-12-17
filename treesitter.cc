#include "document.hh"
#include "sol.hh"

#include <iostream>
#include <string>
#include <tree_sitter/api.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" const TSLanguage *tree_sitter_lua(void);
extern "C" const TSLanguage *tree_sitter_rego(void);

template<typename LanguageTraits>
class TreeSitterTextBuilder
{
	std::vector<std::string> currentIdentifierKind;

	struct HighlightRange
	{
		uint32_t    start;
		uint32_t    end;
		std::string kind;
	};
	std::vector<HighlightRange> ranges;
	std::string_view            currentSource;
	std::string                 beginMarker;
	std::string                 endMarker;
	uint32_t                    lastByte;
	uint32_t                    firstLine;
	bool                        print = false;

	void dfs(TSTreeCursor *cursor)
	{
		auto recurse = [&]() {
			if (ts_tree_cursor_goto_first_child(cursor))
			{
				dfs(cursor);
			}
		};
		do
		{
			auto        node  = ts_tree_cursor_current_node(cursor);
			auto        start = ts_node_start_byte(node);
			auto        end   = ts_node_end_byte(node);
			auto        text  = currentSource.substr(start, end - start);
			std::string kind  = ts_node_type(node);
			// Uncomment when debugging new grammars.
			// std::cerr << "Kind: " << kind << std::endl;
			// std::cerr << "Text: " << text << std::endl;
			if ((kind == "comment_content") || (kind == "comment"))
			{
				if (text.contains(beginMarker))
				{
					firstLine = ts_node_end_point(node).row + 1;
					print     = true;
					continue;
				}
				if (text.contains(endMarker))
				{
					print    = false;
					lastByte = start;
				}
			}
			if (!print)
			{
				recurse();
				continue;
			}

			if (text == kind)
			{
				if (ispunct(text[0]))
				{
					kind = "Punctuation";
				}
				else
				{
					kind = "Keyword";
				}
				ranges.push_back({start, end, kind});
				continue;
			}
			if (LanguageTraits::finalKinds.contains(kind))
			{
				ranges.push_back({start, end, kind});
				continue;
			}
			if (LanguageTraits::tokenInfoKinds.contains(kind))
			{
				currentIdentifierKind.push_back(kind);
				recurse();
				assert(kind == currentIdentifierKind.back());
				currentIdentifierKind.pop_back();
				continue;
			}
			if (LanguageTraits::idenfitierKinds.contains(kind))
			{
				if (currentIdentifierKind.empty())
				{
					ranges.push_back({start, end, "Identifier"});
				}
				else
				{
					ranges.push_back(
					  {start, end, currentIdentifierKind.back()});
				}
			}
			recurse();
		} while (ts_tree_cursor_goto_next_sibling(cursor));
		ts_tree_cursor_goto_parent(cursor);
	}

	public:
	TextTreePointer process_string(const std::string &source,
	                               std::string_view   commentMarker)
	{
		ranges.clear();
		currentSource = source;
		beginMarker   = commentMarker;
		beginMarker += "#begin";
		endMarker = commentMarker;
		endMarker += "#end";
		lastByte  = 0;
		firstLine = 0;
		print     = false;

		TSParser *parser = ts_parser_new();
		ts_parser_set_language(parser, LanguageTraits::create_language());
		TSTree *tree = ts_parser_parse_string(
		  parser, nullptr, source.c_str(), source.size());
		TSNode root_node = ts_tree_root_node(tree);

		auto cursor = ts_tree_cursor_new(root_node);
		dfs(&cursor);
		auto root  = TextTree::create();
		root->kind = "code";
		root->attribute_set("first-line", std::to_string(firstLine));

		if (!ranges.empty())
		{
			uint32_t last = ranges[0].start;
			for (auto r : ranges)
			{
				if (r.start > last)
				{
					root->append_text(source.substr(last, r.start - last));
				}
				auto        run  = root->new_child();
				std::string kind = r.kind;
				if (auto i = LanguageTraits::kindNames.find(kind);
				    i != LanguageTraits::kindNames.end())
				{
					kind = i->second;
				}
				run->kind = "code-run";
				run->attribute_set("token-kind", kind);
				run->append_text(source.substr(r.start, r.end - r.start));
				last = r.end;
			}
		}
		ts_tree_delete(tree);
		ts_parser_delete(parser);
		return root;
	}
	TreeSitterTextBuilder() = default;
};

struct LuaTraits
{
	static inline const std::unordered_set<std::string> finalKinds = {"comment",
	                                                                  "string",
	                                                                  "number"};
	static inline const std::unordered_set<std::string> tokenInfoKinds = {
	  "method_index_expression",
	  "arguments",
	  "function_call"};
	static inline const std::unordered_map<std::string, std::string> kindNames =
	  {{"comment", "Comment"},
	   {"arguments", "Argument"},
	   {"string", "String"},
	   {"method_index_expression", "MethodCall"},
	   {"function_call", "FunctionCall"},
	   {"number", "Number"}};
	static inline const std::unordered_set<std::string> idenfitierKinds = {
	  "identifier"};
	static auto create_language()
	{
		return tree_sitter_lua();
	}
};

using LuaTextBuilder = TreeSitterTextBuilder<LuaTraits>;

struct RegoTraits
{
	static inline const std::unordered_set<std::string> finalKinds = {
	  "comment",
	  "package",
	  "string",
	  "package",
	  "import",
	  "open_curly",
	  "close_curly",
	  "open_bracket",
	  "close_bracket",
	  "open_paren",
	  "close_paren",
	  "import",
	  "number"};
	static inline const std::unordered_set<std::string> tokenInfoKinds = {
	  "method_index_expression",
	  "rule_head",
	  "rule_args",
	  "arguments",
	  "function_call"};
	static inline const std::unordered_map<std::string, std::string> kindNames =
	  {{"comment", "Comment"},
	   {"package", "keyword"},
	   {"import", "keyword"},
	   {"rule_head", "RuleHead"},
	   {"string", "String"},
	   {"rule_args", "ParamName"},
	   {"open_paren", "Punctuation"},
	   {"close_paren", "Punctuation"},
	   {"open_curly", "Punctuation"},
	   {"close_curly", "Punctuation"},
	   {"open_bracket", "Punctuation"},
	   {"close_bracket", "Punctuation"},
	   {"close_bracket", "Punctuation"},
	   {"number", "Number"}};
	static inline const std::unordered_set<std::string> idenfitierKinds = {
	  "var"};
	static auto create_language()
	{
		return tree_sitter_rego();
	}
};

using RegoTextBuilder = TreeSitterTextBuilder<RegoTraits>;

template<typename Builder>
void register_builder(sol::state &lua, std::string_view name)
{
	lua.new_usertype<Builder>(name,
	                          "new",
	                          std::make_unique<Builder>,
	                          "find_code",
	                          &Builder::process_string);
}

extern "C" void register_lua_helpers(sol::state &lua)
{
	register_builder<LuaTextBuilder>(lua, "LuaTextBuilder");
	register_builder<RegoTextBuilder>(lua, "RegoTextBuilder");
}
