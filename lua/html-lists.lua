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

function itemToDtDl(textTree)
	if not (type(textTree) == "string") then
		if (textTree.kind == "item") then
			local dt = TextTree.new("dt")
			if not textTree:has_attribute("tag") then
				textTree:error("missing tag attribute")
				return {textTree}
			end
			dt:append_text(textTree:attribute("tag"))
			textTree:attribute_erase("tag")
			textTree.kind = "dd"
			textTree:visit(visit)
			return {dt, textTree}
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
		elseif (textTree.kind == "description") then
			textTree.kind = "dl"
			textTree:visit(itemToDtDl)
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
