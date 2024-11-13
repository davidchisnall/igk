
local skipNodes = {
	chapter = true,
	section = true,
	subsection = true,
	subsubsection = true,
	itemize = true,
	enumerate = true,
}

local nodes = {}
local openNode = nil

function pushNode()
	if openNode ~= nil then
		if openNode:is_empty() then
			return
		end
		table.insert(nodes, openNode)
	end
	openNode = TextTree.new("p")
end

function node()
	if openNode == nil then
		openNode = TextTree.new("p")
	end
	return openNode
end

-- Find pairs of line breaks in direct children of root
function findBreaks(textTree)
	-- If this is a string, split it.  The split points will be paragraph breaks.
	if type(textTree) == "string" then
		local lineBreak = string.find(textTree, "\n\n")
		-- Iterate over line breaks
		while lineBreak do
			if lineBreak == 1 then
				-- If the line break is at the start, create a new paragraph and move on
				pushNode()
				textTree = textTree:sub(3)
			else
				-- If the line break is not at the start, add the part before
				-- to the existing paragraph, then create a new paragraph and
				-- process the rest of the string.
				node():append_text(string.sub(textTree, 1, lineBreak - 1))
				textTree = textTree:sub(lineBreak + 2)
				pushNode()
			end
			-- Find the next line break
			lineBreak = string.find(textTree, "\n\n")
		end
		if not (textTree == "") then
			-- Add the remaining text to the existing paragraph
			node():append_text(textTree)
		end
	else
		if skipNodes[textTree.kind] then
			print("skipping node: ", textTree.kind)
			pushNode()
			table.insert(nodes, textTree)
		else
			print("not skipping node: ", textTree.kind)
			node():append_child(textTree)
		end
	end
	return {}
end

function process(textTree)
	textTree:visit(findBreaks)
	for _, node in ipairs(nodes) do
		textTree:append_child(node)
	end
	return textTree 
end
