function visit(textTree)
	if (type(textTree) ~= "string") then
		textTree:visit(visit)
		if textTree.kind == "p" then
			local paragraph = { "\n\n"}
			local children  = textTree:extract_children()
			for _,child in ipairs(children) do
				table.insert(paragraph, child)
			end
			table.insert(paragraph, "\n\n")
			return paragraph
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
