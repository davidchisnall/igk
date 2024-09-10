
local beginNodes = {}

function findBegins(textTree)
	if (not (type(textTree) == "string")) then
		if textTree.kind == "begin" then
			table.insert(beginNodes, textTree)
		end
		textTree:visit(findBegins)
	end
	return {textTree}
end

local beginDepth = 0
local beginName
local squashedNodes = {}
function squashBegins(textTree)
	if (not (type(textTree) == "string")) then
		if textTree.kind == "begin" then
			beginDepth = beginDepth+1
			if beginDepth == 1 then
				beginName = textTree.children[1]
				return {}
			end
		end
		if textTree.kind == "end" then
			beginDepth = beginDepth-1
			if beginDepth == 0 then
				if not textTree.children[1] == beginName then
					textTree:error("Mismatched begin and end!")
				end
				textTree.kind = textTree.children[1]
				textTree:clear()
				for _, node in ipairs(squashedNodes) do
					textTree:append_child(node)
				end
				return {textTree}
			end
		end
	end
	if beginDepth == 0 then
		return {textTree}
	end
	table.insert(squashedNodes, textTree)
	return {}
end

function process(textTree)
	textTree:visit(findBegins)
	for _, node in ipairs(beginNodes) do
		beginDepth = 0
		squashedNodes = {}
		if node:parent() then
			node:parent():visit(squashBegins)
		end
	end
	return textTree
end
