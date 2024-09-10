
function visit(textTree)
	if type(textTree) ~= "string" then
		textTree:visit(visit)
		if textTree.kind == "" then
			return textTree:extract_children()
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
