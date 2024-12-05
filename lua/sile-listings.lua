-- TODO: Colour differently for print

local colours = {
	Declaration = "#0000FF",
	Punctuation = "#000000",
	TypeRef = "#0000FF",
	DeclRef = "#00a000",
	FunctionName = "#00a000",
	MacroName = "#bb0000",
	MacroInstantiation = "#bb0000",
	ParamName = "#00a000",
	Literal = "#a00000",
	Keyword = "#ff0000",
	Comment = "#008000",
	Identifier = "#000000",
}

function visitCodeRuns(textTree)
	textTree.kind = "color"
	local kind = textTree:has_attribute("token-kind") and textTree:attribute("token-kind") or "Identifier"
	textTree:attribute_erase("token-kind")
	textTree:attribute_set("color", colours[kind])
	return { textTree }
end

function visitClangDoc(textTree)
	local center = TextTree.create({
		kind = "center",
		children = {
			{
				kind = "framebox",
				attributes = { shadow = true },
				children = {
					{
						kind = "parbox",
						attributes = {
							valign = "middle",
							width = "90%fw",
						},
						children = {
							{
								kind = "font",
								attributes = { weight = 900 },
								children = {
									"Documentation for the ",
									{
										kind = "font",
										attributes = { family = "Hack" },
										children = {
											textTree:attribute("code-declaration-entity"),
										},
									},
									" " .. textTree:attribute("code-declaration-kind"),
								},
							},
						},
					},
				},
			},
		},
	})
	textTree:attribute_erase("code-declaration-entity")
	textTree:attribute_erase("code-declaration-kind")
	center.children[1].children[1]:take_children(textTree)
	center.children[1].children[1]:take_children(textTree)
	return { center }
end

function visitCode(textTree)
	textTree.kind = "verbatim"
	local code_line = TextTree.new()
	code_line:take_children(textTree)
	local linebreak = code_line:find_string("\n")
	local line = nil
	if textTree:has_attribute("first-line") then
		line = textTree:attribute("first-line")
	end
	while not (linebreak == -1) do
		local split = code_line:split_at_byte_index(linebreak)
		if line then
			textTree:append_text(tostring(line) .. " ")
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
		textTree:append_text(tostring(line) .. " ")
	end
	textTree:append_child(code_line)
	textTree:new_child("break")
	textTree:append_text("\n")
	textTree:append_text("\n")
	if textTree:has_attribute("caption") then
		local caption = textTree:new_child("center")
		caption:append_text("Listing ")
		local counter = caption:new_child("show-counter")
		counter:attribute_set("id", "listing")
		caption:append_text(". ")
		if textTree:has_attribute("label") then
			local label = caption:new_child("refentry")
			label:attribute_set("marker", textTree:attribute("label"))
			if not textTree:has_attribute("number") then
				textTree:error("Missing number attribute")
			end
			label:attribute_set("number", textTree:attribute("number"))
			label:attribute_set("section", textTree:attribute("number"))
			label:append_text(textTree:attribute("number"))
			textTree:attribute_set("type", "listing")
		end
		caption:append_text(textTree:attribute("caption"))
	end
	--if textTree:has_attribute("first-line") then
	textTree:attribute_erase("label")
	textTree:attribute_erase("filename")
	textTree:attribute_erase("caption")
	textTree:attribute_erase("first-line")
	textTree:attribute_erase("code-kind")
	return { textTree }
end

function process(textTree)
	textTree:match("clang-doc", visitClangDoc)
	textTree:match("code", visitCode)
	textTree:match("code-run", visitCodeRuns)
	return textTree
end
