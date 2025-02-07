
local ebookColours = {
	Declaration = "#0000FF",
	Punctuation = "#000000",
	TypeRef = "#0000FF",
	DeclRef = "#00a000",
	FunctionName = "#00a000",
	RuleHead = "#00a000",
	MacroName = "#bb0000",
	MacroInstantiation = "#bb0000",
	ParamName = "#00a000",
	Literal = "#a00000",
	String = "#a00000",
	Number = "#a00000",
	Keyword = "#ff0000",
	Comment = "#008000",
	Identifier = "#000000",
}

local italic = { kind = "font", attributes = { style = "italic" }}
local bold = { kind = "font", attributes = { weight = "700" }}

-- These are not great print choices, but they're fine to start with

local printNodes = {
	Declaration = bold,
	RuleHead = bold,
	MacroName = bold,
	MacroInstantiation = bold,
	Literal = italic,
	String = italic,
	Number = italic,
	Keyword = italic,
	Comment = italic,
}

function visitCodeRuns(textTree)
	local kind = textTree:has_attribute("token-kind") and textTree:attribute("token-kind") or "Identifier"
	if config.print then
		if printNodes[kind] then
			local run = TextTree.create(printNodes[kind])
			run:take_children(textTree)
			return { run }
		end
		return textTree:extract_children()
	else
		textTree:attribute_erase("token-kind")
		textTree.kind = "color"
		textTree:attribute_set("color", ebookColours[kind] or ebookColours.Identifier)
	end
	return { textTree }
end

function visitClangDoc(textTree)
	local center = TextTree.create({
		kind = "center",
		children = {
			{
				kind = "skip",
				attributes = { height = "0.3em" },
			},
			{
				kind = "framebox",
				attributes = { shadow = true },
				children = {
					{
						kind = "parbox",
						attributes = {
							valign = "middle",
							width = "90%fw",
							padding = "1em",
						},
						children = {
							{
								kind = "noindent",
							},
							{
								kind = "font",
								attributes = { weight = 900 },
								children = {
									"Documentation for the ",
									{
										kind = "font",
										attributes = { family = "Hack", size = "0.8em" },
										children = {
											textTree:attribute("code-declaration-entity"),
										},
									},
									" " .. textTree:attribute("code-declaration-kind"),
								},
							},
							{
								kind = "skip",
								attributes = { height = "0.3em" },
							},
						},
					},
				},
			},
			{
				kind = "skip",
				attributes = { height = "0.3em" },
			},
		},
	})
	-- Add a noindent at the start and some vertical spacing at the end of each
	-- paragraph or code block.
	local lastSkip = nil
	textTree:match_any({ "code", "p" }, function(p)
		local noindent = TextTree.new("noindent")
		local skip = TextTree.new("skip")
		skip:attribute_set("height", "0.5em")
		lastSkip = skip
		return { noindent, p, skip }
	end)
	-- Remove a trailing vertical skip.
	if lastSkip then
		textTree:remove_child(lastSkip)
	end
	-- Visit the code block first so that we don't make it small twice.
	textTree:match("code", visitCode)

	-- Move all of the children of the text tree into the parbox in the
	-- framebox in the center.
	center.children[2].children[1]:take_children(textTree)
	local parbox = TextTree.new("floating")
	parbox:attribute_set("width", "100%fw")
	parbox:append_child(center)
	return { parbox }
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
	local caption = nil
	if textTree:has_attribute("caption") then
		caption = TextTree.new("listingcaption")
		if textTree:has_attribute("label") then
			caption:attribute_set("marker", textTree:attribute("label"))
		end
		caption:append_text(textTree:attribute("caption"))
		if textTree:has_attribute("filename") then
			local from = caption:new_child("font")
			from:attribute_set("size", "0.8em")
			from:append_text(" [ from: " .. textTree:attribute("filename") .. " ]")
		end
	end
	textTree:attribute_erase("label")
	textTree:attribute_erase("filename")
	textTree:attribute_erase("caption")
	textTree:attribute_erase("first-line")
	textTree:attribute_erase("code-kind")
	textTree:match("code-run", visitCodeRuns)

	-- Prevent page breaks in code:
	local parbox = TextTree.new("floating")
	parbox:attribute_set("width", "100%fw")
	parbox:append_child(textTree)
	if caption then
		parbox:append_child(caption)
	end

	return { parbox }
end

function process(textTree)
	textTree:match("clang-doc", visitClangDoc)
	textTree:match("code", visitCode)

	-- Visit each inline code block so that we make it the correct font before
	-- we make it the correct colour.
	textTree:match("code-run", function(codeRun)
		codeRun = visitCodeRuns(codeRun)[1]
		local code = TextTree.new("font")
		code:attribute_set("family", "Hack")
		code:attribute_set("size", "0.8em")
		code:append_child(codeRun)
		return { code }
	end)

	return textTree
end
