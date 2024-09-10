
local skipNodes = {
	"chapter",
	"section",
	"subsection",
	"subsubsection",
	"itemize",
	"enumerate",
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

function findBreaks(textTree)
	if type(textTree) == "string" then
		print("Processing string")
		local lineBreak = string.find(textTree, "\n\n")
		while lineBreak do
			if lineBreak == 1 then
				textTree = string.sub(textTree, 3)
			else
				pushNode()
				openNode:append_text(string.sub(textTree, 1, lineBreak - 1))
				textTree = string.sub(textTree, lineBreak + 2)
			end
			lineBreak = string.find(textTree, "\n\n")
		end
		if not (textTree == "") then
			openNode:append_text(textTree)
		end
	else
		print("Processing " .. textTree.kind)
		if skipNodes[textTree.kind] == nil  then
			pushNode()
			table.insert(nodes, textTree)
		else
			openNode.append_child(textTree)
		end
	end
	return {}
end

function process(textTree)
	textTree:visit(findBreaks)
	pushNode()
	for _, node in ipairs(nodes) do
		textTree:append_child(node)
	end
	print("Done")
	--textTree:dump()
	return textTree 
end
