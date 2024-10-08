-- TODO: Colour differently for print

local colours = {
	Declaration  = "#0000FF",
	Punctuation  = "#000000",
	TypeRef  = "#0000FF",
	DeclRef  = "#00a000",
	FunctionName  = "#00a000",
	ParamName  = "#00a000",
	Literal  = "#a00000",
	Keyword  = "#ff0000",
	Comment  = "#008000",
}


function visitCodeRuns(textTree)
	if type(textTree) ~= "string" then
		if (textTree.kind == "code-run") then
			textTree.kind = "color"
			local kind = textTree:attribute("token-kind")
			textTree:attribute_erase("token-kind")
			textTree:attribute_set("color", colours[kind])
		end
	end
	return {textTree}
end

function visit(textTree)
	if (type(textTree) ~= "string") then
		if textTree.kind == "clang-doc" then
			-- Not done yet!
			textTree:dump()
			textTree.kind = "div";
			textTree:attribute_set("class", "code-documentation")
			textTree:visit(visit)
			local innerDiv = TextTree.new("div")
			innerDiv:attribute_set("class", "code-documentation-inner")
			local heading = innerDiv:new_child()
			heading.kind = "span"
			heading:append_text("Documentation for the " .. textTree:attribute("code-declaration-entity") .. " " .. textTree:attribute("code-declaration-kind"))
			heading:attribute_set("class", "code-documentation-heading")
			innerDiv:take_children(textTree)
			textTree:attribute_erase("code-declaration-entity")
			textTree:attribute_erase("code-declaration-kind")
			textTree:append_child(innerDiv)
			textTree:dump()
			return {textTree}
		elseif textTree.kind ~= "code" then
			textTree:visit(visitCodeRuns)
			textTree:visit(visit)
		else
			textTree:visit(visitCodeRuns)
			textTree.kind = "verbatim"
			local code_line = TextTree.new()
			print("new node ")
			code_line:take_children(textTree)
			print("taken children")
			local linebreak = code_line:find_string("\n")
			local line = nil
			if textTree:has_attribute("first-line") then
				line = textTree:attribute("first-line")
			end
			while not (linebreak == -1) do
				local split = code_line:split_at_byte_index(linebreak)
				print("Split:")
				split[1]:dump()
				print("Adding first half")
				split[1]:dump()
				if line then
					textTree:append_text(tostring(line) .. ' ')
					line = line + 1
				end
				textTree:append_child(split[1])
				textTree:new_child("break")
				textTree:append_text("\n")
				code_line = split[2]:split_at_byte_index(1)[2]
				linebreak = code_line:find_string("\n")
			end
			textTree:new_child("break")
			textTree:append_text("\n")
			if line then
				textTree:append_text(tostring(line) .. ' ')
			end
			textTree:append_child(code_line)
			textTree:new_child("break")
			textTree:append_text("\n")
			textTree:append_text("\n")
			if textTree:has_attribute("caption") then
				local caption = textTree:new_child("center")
				caption:append_text("Figure ")
				local counter = caption:new_child("show-counter")
				counter:attribute_set("id", "figure")
				caption:append_text(". ")
				caption:append_text(textTree:attribute("caption"))
			end
			--if textTree:has_attribute("first-line") then
			textTree:attribute_erase("label")
			textTree:attribute_erase("filename")
			textTree:attribute_erase("caption")
			textTree:attribute_erase("first-line")
			textTree:attribute_erase("code-kind")
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
