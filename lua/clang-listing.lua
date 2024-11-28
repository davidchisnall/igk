-- FIXME: Don't hard code these paths.
local arguments = {
	"c++",
	"-std=c++17",
	"-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1",
	"-I/Library/Developer/CommandLineTools/usr/lib/clang/15.0.0/include",
	"-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include",
}

local parsedFiles = {}

function parse_file(fileName)
	if parsedFiles[fileName] == nil then
		parsedFiles[fileName] = ClangTextBuilder.new(fileName, arguments)
	end
	return parsedFiles[fileName]
end

function file_exists(file)
	local f = io.open(file, "rb")
	if f then
		f:close()
	end
	return f ~= nil
end

function lines_from(textTree, file, marker)
	if not file_exists(file) then
		textTree:error("File not found: " .. file)
		return nil
	end
	local lines = {}
	local i = 1
	for line in io.lines(file) do
		if string.find(line, marker .. "#begin") then
			lines.start = i + 1
		end
		if string.find(line, marker .. "#end") then
			lines.finish = i - 1
		end
		i = i + 1
	end
	if (not lines.start) or not lines.finish then
		textTree:error("Marker '" .. marker .. "'not found in file: " .. file)
		return nil
	end
	if lines.start >= lines.finish then
		textTree:error("Marker '" .. marker .. "' is not valid in file: " .. file)
		return nil
	end
	return lines
end

function check_attribute(textTree, attribute)
	if not textTree:has_attribute(attribute) then
		textTree:error("Missing attribute: " .. attribute)
		return false
	end
	return true
end

function resolve_relative_path(textTree, path)
	if path:find("/", 1, true) ~= 1 then
		local dirname = textTree:source_directory_name()
		path = dirname .. "/" .. path
	end
	return path
end

function handleCodeListing(textTree)
	if
		not (
			check_attribute(textTree, "filename")
			and check_attribute(textTree, "marker")
			and check_attribute(textTree, "caption")
		)
	then
		return { textTree }
	end
	local fileName = resolve_relative_path(textTree, textTree:attribute("filename"))

	local clang = ClangTextBuilder.new(fileName, arguments)
	local lineRange = lines_from(textTree, fileName, textTree:attribute("marker"))
	if not lineRange then
		return { textTree }
	end
	local code = clang:build_line_range(fileName, lineRange.start, lineRange.finish)
	code:attribute_set("caption", textTree:attribute("caption"))
	code:attribute_set("filename", textTree:attribute("filename"))
	if textTree:has_attribute("label") then
		code:attribute_set("label", textTree:attribute("label"))
	end
	return { code }
end

local function isempty(s)
	return s == nil or s == ""
end

local listNode = nil

function cleanMarkdown(textTree)
	if type(textTree) == "string" then
		local results = {}
		for line in textTree:gmatch("[^\r\n]+") do
			local listPrefix = string.gmatch(line, "%s*-")()
			if listPrefix then
				line = string.sub(line, #listPrefix + 1)
			end
			local start = 1
			for w in string.gmatch(line, "`([^`]*)`") do
				-- Find the location of the matched string, treating it as a
				-- string not a pattern
				local i, e = string.find(line, w, 1, true)
				if i > start + 1 then
					table.insert(results, string.sub(line, start, i - 2))
				end
				local run = TextTree.new("code-run")
				run:append_text(w)
				table.insert(results, run)
				start = e + 2
			end
			if start < #line then
				table.insert(results, string.sub(line, start, #line))
			end
			if listPrefix then
				if listNode == nil then
					listNode = TextTree.new("itemize")
				end
				local item = TextTree.new("item")
				listNode:append_child(item)
				for _, r in pairs(results) do
					if type(r) == "string" then
						item:append_text(r)
					else
						item:append_child(r)
					end
				end
				results = {}
			else
				if listNode then
					table.insert(results, 1, listNode)
					listNode = nil
				end
			end
		end
		return results
	else
		if textTree.kind == "p" then
			textTree:visit(cleanMarkdown)
		end
		if listNode then
			local ret = { listNode, textTree }
			listNode = nil
			return ret
		end
	end
	return { textTree }
end

local docfile = nil

function visit(textTree)
	if not (type(textTree) == "string") then
		if textTree.kind == "codelisting" then
			return handleCodeListing(textTree)
		end
		if textTree.kind == "compileflags" then
			local argsFileName = resolve_relative_path(textTree, textTree.children[1])
			local argsFile = io.open(argsFileName, "rb")
			if not argsFile then
				textTree:error("Unable to open compile flags file: " .. argsFileName)
			end
			arguments = {}
			for line in argsFile:lines() do
				arguments[#arguments + 1] = line
			end
			argsFile:close()
			return {}
		elseif textTree.kind == "docfile" then
			local docfileName = resolve_relative_path(textTree, textTree.children[1])
			docfile = ClangTextBuilder.new(docfileName, arguments)
			return {}
		elseif (textTree.kind == "functiondoc") or (textTree.kind == "macrodoc") then
			local DocumentedKinds = {
				"functiondoc",
				"Function",
				"macrodoc",
				"Macro",
			}
			if not docfile then
				textTree:error(
					"Clang documentation directives must be preceded by a \\docfile{} instruction giving the source file to parse"
				)
				return { textTree }
			end
			local usr = textTree:attribute("usr")
			if isempty(usr) then
				local USRs
				if textTree.kind == "macrodoc" then
					USRs = docfile:usrs_for_macro(textTree.children[1])
				else
					USRs = docfile:usrs_for_function(textTree.children[1])
				end
				if #USRs == 0 then
					textTree:error("Function not found: " .. textTree.children[1])
					return { textTree }
				end
				if not (#USRs == 1) then
					textTree:error(
						DocumentedKinds[textTree.kind]
							.. "name is ambiguous, please provide a USR for '"
							.. textTree.children[1]
							.. "'."
					)
					for _, usr in pairs(USRs) do
						print("Valid USR: ", usr)
					end
					return { textTree }
				end
				usr = USRs[1]
			end
			local doc = docfile:build_doc_comment(usr)
			doc:visit(cleanMarkdown)
			return { doc }
		else
			textTree:visit(visit)
		end
	end
	return { textTree }
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
