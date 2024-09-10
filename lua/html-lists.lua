local visit
function itemToLi(textTree)
	if not (type(textTree) == "string") then
		if (textTree.kind == "item") then
			textTree.kind = "li"
			textTree:visit(visit)
		end
	end
	return {textTree}
end

visit = function (textTree)
	if not (type(textTree) == "string") then
		if (textTree.kind == "itemize") then
			textTree.kind = "ul"
			textTree:visit(itemToLi)
		elseif (textTree.kind == "enumerate") then
			textTree.kind = "ol"
			textTree:visit(itemToLi)
		else
			textTree:visit(visit)
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
