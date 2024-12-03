#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "source_manager.hh"

class TextTree;
using TextTreePointer = std::shared_ptr<TextTree>;
/**
 * A text tree.  Each node may have a kind and a set of attributes, and
 * contains a sequence of child nodes or text runs.
 */
class TextTree : public std::enable_shared_from_this<TextTree>
{
	public:
	using Child        = std::variant<std::string, TextTreePointer>;
	using Visitor      = std::function<std::vector<Child>(Child &)>;
	using ConstVisitor = std::function<void(const Child &)>;

	std::string             kind;
	std::weak_ptr<TextTree> parentPointer;
	std::vector<Child>      children;

	std::unordered_map<std::string, std::string> attributeStorage;

	SourceRange sourceRange;

	void visit(Visitor &&visitor)
	{
		for (size_t i = 0; i < children.size();)
		{
			auto child = std::move(children[i]);
			children.erase(children.begin() + i);
			auto newChildren = visitor(child);
			// Detach the current child from the tree.  We may reattach it
			// later.
			if (std::holds_alternative<TextTreePointer>(child))
			{
				auto childNode = std::get<TextTreePointer>(child);
				if (childNode != nullptr)
				{
					childNode->parent(nullptr);
				}
			}
			if (newChildren.size() > 0)
			{
				for (auto &newChild : newChildren)
				{
					if (std::holds_alternative<TextTreePointer>(newChild))
					{
						std::get<TextTreePointer>(newChild)->parent(
						  shared_from_this());
					}
				}
				children.insert(
				  children.begin() + i, newChildren.begin(), newChildren.end());
			}
			i += newChildren.size();
		}
	}

	void match_any(const std::unordered_set<std::string> &kinds, Visitor &visitor)
	{
		visit([&kinds, &visitor](Child &child) {
			if (std::holds_alternative<TextTreePointer>(child))
			{
				auto childNode = std::get<TextTreePointer>(child);
				if (kinds.contains(childNode->kind))
				{
					return visitor(child);
				}
				childNode->match_any(kinds, visitor);
			}
			return std::vector<Child>{child};
		});
	}

	void match(const std::string &kind, Visitor &visitor)
	{
		visit([&kind, &visitor](Child &child) {
			if (std::holds_alternative<TextTreePointer>(child))
			{
				auto childNode = std::get<TextTreePointer>(child);
				if (childNode->kind == kind)
				{
					return visitor(child);
				}
				childNode->match(kind, visitor);
			}
			return std::vector<Child>{child};
		});
	}

	void const_visit(ConstVisitor &&visitor) const
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			visitor(children[i]);
		}
	}

	size_t length()
	{
		size_t length = 0;
		for (auto &child : children)
		{
			if (std::holds_alternative<std::string>(child))
			{
				length += std::get<std::string>(child).size();
			}
			else
			{
				length += std::get<TextTreePointer>(child)->length();
			}
		}
		return length;
	}

	TextTreePointer shallow_clone()
	{
		auto clone         = create();
		clone->kind        = kind;
		clone->sourceRange = sourceRange;
		for (auto &attribute : attributeStorage)
		{
			clone->attributeStorage[attribute.first] = attribute.second;
		}
		return clone;
	}

	TextTreePointer deep_clone()
	{
		auto clone = shallow_clone();
		for (auto &child : children)
		{
			if (std::holds_alternative<std::string>(child))
			{
				clone->append_text(std::get<std::string>(child));
			}
			else
			{
				clone->append_child(
				  std::get<TextTreePointer>(child)->deep_clone());
			}
		}
		return clone;
	}

	std::pair<Child, Child> split_at_byte_index(size_t index)
	{
		TextTreePointer left  = shallow_clone();
		TextTreePointer right = shallow_clone();
		size_t          i;
		for (i = 0; i < children.size(); i++)
		{
			auto  &child       = children[i];
			size_t childLength = 0;
			if (std::holds_alternative<std::string>(child))
			{
				childLength = std::get<std::string>(child).size();
			}
			else
			{
				childLength = std::get<TextTreePointer>(child)->length();
			}
			if (index < childLength)
			{
				if (std::holds_alternative<std::string>(child))
				{
					auto &text = std::get<std::string>(child);
					if (index > 0)
					{
						left->append_text(text.substr(0, index));
					}
					if (index < text.size())
					{
						right->append_text(text.substr(index));
					}
				}
				else
				{
					auto &node  = std::get<TextTreePointer>(child);
					auto  split = node->split_at_byte_index(index);
					left->append_child(split.first);
					right->append_child(split.second);
				}
				i++;
				break;
			}
			index -= childLength;
			left->append_child(std::move(child));
		}
		for (; i < children.size(); i++)
		{
			right->append_child(std::move(children[i]));
		}
		children.clear();
		return {left, right};
	}

	ssize_t find_string(const std::string needle)
	{
		size_t startOffset = 0;
		size_t i           = 0;
		for (auto &child : children)
		{
			if (std::holds_alternative<std::string>(child))
			{
				auto &text = std::get<std::string>(child);
				if (auto found = text.find(needle); found != std::string::npos)
				{
					return startOffset + found;
				}
				startOffset += text.size();
			}
			else
			{
				auto childTree = std::get<TextTreePointer>(child);
				if (auto found = childTree->find_string(needle);
				    found != std::string::npos)
				{
					return startOffset + found;
				}
				startOffset += childTree->length();
			}
		}
		return std::string::npos;
	}

	void take_children(TextTreePointer other)
	{
		if (other == nullptr)
		{
			return;
		}
		children.insert(children.end(),
		                std::make_move_iterator(other->children.begin()),
		                std::make_move_iterator(other->children.end()));
		other->children.clear();
	}

	bool has_attribute(const std::string &name) const
	{
		return attributeStorage.find(name) != attributeStorage.end();
	}

	const decltype(attributeStorage) &attributes() const
	{
		return attributeStorage;
	}

	void attribute_erase(const std::string &name)
	{
		attributeStorage.erase(name);
	}

	void attribute_set(const std::string &name, const std::string &value)
	{
		attributeStorage[name] = value;
	}

	auto &attribute(const std::string &name)
	{
		return attributeStorage[name];
	}

	TextTreePointer parent() const
	{
		return parentPointer.lock();
	}

	void parent(TextTreePointer parent)
	{
		parentPointer = parent;
	}

	TextTreePointer new_child()
	{
		auto child             = create();
		child->parentPointer   = shared_from_this();
		auto endSourceLocation = child->sourceRange.second;
		for (auto &child :
		     std::ranges::subrange(children.rbegin(), children.rend()))
		{
			if (std::holds_alternative<TextTreePointer>(child))
			{
				endSourceLocation =
				  std::get<TextTreePointer>(child)->sourceRange.second;
			}
		}
		child->sourceRange = {endSourceLocation, endSourceLocation};
		children.push_back(child);
		return child;
	}

	void append_text(const std::string &text)
	{
		if (!children.empty() &&
		    std::holds_alternative<std::string>(children.back()))
		{
			std::get<std::string>(children.back()) += text;
		}
		else
		{
			children.push_back(text);
		}
	}

	void remove_child(TextTreePointer child)
	{
		for (auto it = children.begin(); it != children.end(); ++it)
		{
			if (std::holds_alternative<TextTreePointer>(*it) &&
			    std::get<TextTreePointer>(*it) == child)
			{
				children.erase(it);
				return;
			}
		}
	}

	void append_child(Child child)
	{
		if (std::holds_alternative<TextTreePointer>(child))
		{
			auto &childNode = std::get<TextTreePointer>(child);
			if (childNode->parent() != nullptr)
			{
				childNode->parent()->remove_child(childNode);
			}
			childNode->parent(shared_from_this());
		}
		children.push_back(child);
	}

	decltype(children) extract_children()
	{
		decltype(children) extracted = std::move(children);
		return extracted;
	}

	bool is_empty()
	{
		return children.empty() && attributeStorage.empty();
	}

	void clear()
	{
		// Children that are nodes may be referenced elsewhere.  Detach them
		// first.
		for (auto &child : children)
		{
			if (std::holds_alternative<TextTreePointer>(child))
			{
				std::get<TextTreePointer>(child)->parent(nullptr);
			}
		}
		children.clear();
	}

	static TextTreePointer create()
	{
		struct MakeSharedEnabler : public TextTree
		{
		};
		return std::make_shared<MakeSharedEnabler>();
	}

	/**
	 * Return the body of this as text.
	 */
	std::string text()
	{
		// Special case if we have only one child and it's a string: just return
		// it.
		if ((children.size() == 1) &&
		    std::holds_alternative<std::string>(children[0]))
		{
			return std::get<std::string>(children[0]);
		}
		std::string result;
		for (auto &child : children)
		{
			std::visit(
			  [&result](auto &&arg) {
				  if constexpr (std::is_same_v<std::decay_t<decltype(arg)>,
				                               std::string>)
				  {
					  result += arg;
				  }
				  else
				  {
					  result += arg->text();
				  }
			  },
			  child);
		}
		return result;
	}

	void dump();

	private:
	TextTree() = default;
};
