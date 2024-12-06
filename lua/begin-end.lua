
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
local begin = nil
local squashedNodes = {}

function squashBegins(textTree)
	if (not (type(textTree) == "string")) then
		if textTree.kind == "begin" then
			beginDepth = beginDepth+1
			if beginDepth == 1 then
				begin = textTree
				textTree.kind = textTree:text()
				textTree:clear()
				return {begin}
			end
		end
		if textTree.kind == "end" then
			beginDepth = beginDepth-1
			if beginDepth == 0 then
				if not textTree:text() == begin.kind then
					textTree:error("Mismatched begin and end!")
				end
				for i, node in ipairs(squashedNodes) do
					if (type(node) == "string") then
						begin:append_text(node)
					else
						begin:append_child(node)
					end
				end
				squashedNodes = {}
				begin = nil
				return {} 
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
		begin = nil
		-- The visit function will squash every top-level begin-end pair in the
		-- same scope, so we can skip nodes that we've already rewritten
		if node.kind == "begin" then
			if node:parent() then
				node:parent():visit(squashBegins)
			end
		end
	end
	return textTree
end
