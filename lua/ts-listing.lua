-- FIXME: Move this to a helper and stop copying and pasting it.
function resolve_relative_path(textTree, path)
	if path:find("/", 1, true) ~= 1 then
		local dirname = textTree:source_directory_name()
		path = dirname .. "/" .. path
	end
	return path
end

function handleListing(textTree)
	if not textTree:has_attribute("marker") then
		textTree:error(textTree.kind .." requires a marker attribute")
		return { textTree }
	end
	if not textTree:has_attribute("filename") then
		textTree:error(textTree.kind .." requiresa filename attribute")
		return { textTree }
	end
	local filename = resolve_relative_path(textTree, textTree:attribute("filename"))
	local file = io.open(filename, "rb")
	if not file then
		textTree:error("Listing source file '" .. filename .. "' does not exist")
		return { textTree }
	end
	local content = file:read("*all")
	file:close()
	local builder
	if textTree.kind == "lualisting" then
		builder = LuaTextBuilder.new()
	elseif textTree.kind == "regolisting" then
		builder = RegoTextBuilder.new()
	end
	print("Builder:", builder)
	local code = builder:find_code(content, textTree:attribute("marker"))
	if textTree:has_attribute("caption") then
		code:attribute_set("caption", textTree:attribute("caption"))
	end
	code:attribute_set("filename", textTree:attribute("filename"))
	if textTree:has_attribute("label") then
		code:attribute_set("label", textTree:attribute("label"))
	end
	return { code }
end

function process(textTree)
	textTree:match_any({"regolisting", "lualisting"}, handleListing)
	return textTree
end
