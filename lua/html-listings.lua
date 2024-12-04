
function visitCodeRuns(textTree)
	if type(textTree) ~= "string" then
		if (textTree.kind == "code-run") then
			textTree.kind = "span"
			local kind = textTree:attribute("token-kind")
			textTree:attribute_erase("token-kind")
			if not kind then
				textTree:attribute_set("class", "code")
			else
				textTree:attribute_set("class", "code code-" .. kind)
			end
		end
	end
	return {textTree}
end

function visit(textTree)
	if (type(textTree) ~= "string") then
		if textTree.kind == "clang-doc" then
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
			return {textTree}
		elseif textTree.kind ~= "code" then
			textTree:visit(visitCodeRuns)
			textTree:visit(visit)
		else
			textTree:visit(visitCodeRuns)
			local div = TextTree.new("div")
			div:attribute_set("class", "listing")
			div:append_child(textTree)
			local code_line = TextTree.new()
			code_line.kind = "code"
			code_line:attribute_set("class", "listing-line")
			code_line:take_children(textTree)
			local linebreak = code_line:find_string("\n")
			while not (linebreak == -1) do
				local split = code_line:split_at_byte_index(linebreak)
				textTree:append_child(split[1])
				textTree:append_text("\n")
				code_line = split[2]:split_at_byte_index(1)[2]
				linebreak = code_line:find_string("\n")
			end
			textTree:append_child(code_line)
			textTree.kind = "pre"
			local class = "listing-code"
			if textTree:has_attribute("first-line") then
				class = class .. " listing-code-numbered"
				local firstLine = textTree:attribute("first-line")
				textTree:attribute_erase("first-line")
				textTree:attribute_set("style", "counter-reset: listing-line " .. tostring(tonumber(firstLine) - 1))
			end
			textTree:attribute_set("class", class)
			local label = div:new_child()
			label.kind = "p"
			label:attribute_set("id", textTree:attribute("label"))
			label:attribute_set("class", "listing-caption")
			label:append_text(textTree:attribute("caption"))
			local exampleOrigin = label:new_child()
			exampleOrigin.kind = "span"
			if textTree:has_attribute("filename") then
				exampleOrigin:attribute_set("class", "listing-origin")
				exampleOrigin:append_text(textTree:attribute("filename"))
			end
			textTree:attribute_erase("label")
			textTree:attribute_erase("filename")
			textTree:attribute_erase("caption")
			textTree:attribute_erase("code-kind")
			return {div}
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
