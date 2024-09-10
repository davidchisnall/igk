function visit(textTree)
	if (type(textTree) == "string") then
		io.write(textTree)
	else
		io.write("\\" .. textTree.kind)
		io.write("[")
		local first = true
		for i, v in pairs(textTree:attributes()) do
			if first then
				first = false
			else
				io.write(", ")
			end
			io.write(i .. "=".. v)
		end
		io.write("]{")
		textTree:visit(visit)
		io.write("}")
	end
	return {textTree}
end

function process(textTree)
	print("Pass starting")
	print("Tree: ", textTree)
	print("Kind:", textTree.kind)
	textTree:visit(visit)
	return textTree
end
