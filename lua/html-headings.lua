local headingTypes = {
	chapter = "h1",
	section = "h2",
	subsection = "h3",
	subsubsection = "h4"
}

function visit(textTree)
	if type(textTree) ~= "string" then
		textTree:visit(visit)
		if headingTypes[textTree.kind] then
			textTree.kind = headingTypes[textTree.kind]
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
