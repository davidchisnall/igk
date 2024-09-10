
local arguments = {
	"c++",
	"-std=c++17",
	"-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1",
	"-I/Library/Developer/CommandLineTools/usr/lib/clang/15.0.0/include",
	"-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include"};


function file_exists(file)
  local f = io.open(file, "rb")
  if f then f:close() end
  return f ~= nil
end


function lines_from(textTree, file, marker)
  if not file_exists(file) then
	  textTree:error("File not found: " .. file)
	  return nil
  end
  local lines = {}
  local i=1
  for line in io.lines(file) do 
	  if string.find(line, marker .. "#begin") then
		  lines.start = i+1
	  end
	  if string.find(line, marker .. "#end") then
		  lines.finish = i-1
	  end
	  i = i + 1
  end
  if (not lines.start) or (not lines.finish) then
	  textTree:error("Marker '" .. marker .. "'not found in file: " .. file)
	  return nil
  end
  if (lines.start >= lines.finish) then
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

function handleCodeListing(textTree)
		if not (check_attribute(textTree, "filename") and 
			check_attribute(textTree, "marker") and 
			check_attribute(textTree, "caption")) then
			return {textTree}
		end
		local fileName = textTree:attribute("filename")
		local clang = ClangTextBuilder.new(fileName, arguments)
		local lineRange = lines_from(textTree, fileName, textTree:attribute("marker"))
		if not lineRange then
			return {textTree}
		end
		local code = clang:build_line_range(fileName, lineRange.start, lineRange.finish)
		code:attribute_set("caption", textTree:attribute("caption"))
		code:attribute_set("filename", textTree:attribute("filename"))
		if textTree:has_attribute("label") then
			code:attribute_set("label", textTree:attribute("label"))
		end
		return {code}
end

local function isempty(s)
  return s == nil or s == ''
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
				local i, e = string.find(line, w)
				if i > start + 1 then
					table.insert(results, string.sub(line, start, i-2))
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
		if (textTree.kind == "p") then
			textTree:visit(cleanMarkdown)
		else
			print("Unhandled kind: ", textTree.kind)
		end
		if listNode then
			local ret = {listNode, textTree}
			return ret;
		end
	end
	return {textTree}
end

function visit(textTree)
	if not (type(textTree) == "string") then
		if (textTree.kind == "codelisting") then
			return handleCodeListing(textTree)
		end
		if (textTree.kind == "functiondoc") then
			-- FIXME: Memoise.
			local clang = ClangTextBuilder.new('test.cc', arguments)
			local usr = textTree:attribute("usr")
			if isempty(usr) then
				local USRs = clang:usrs_for_function(textTree.children[1])
				if #USRs == 0 then
					textTree:error("Function not found: " .. textTree.children[1])
					return {textTree}
				end
				if not (#USRs == 1) then
					textTree:error("Function name is ambiguous, please provide a USR for '" .. textTree.children[1] .. "'.")
					for _, usr in pairs(USRs) do
						print("Valid USR: ", usr)
					end
					return {textTree}
				end
				usr = USRs[1]
			end
			local doc = clang:build_doc_comment(usr)
			print("Parsed doc:")
			doc:dump()
			doc:visit(cleanMarkdown)
			return {doc}
		end
	end
	return {textTree}
end

function process(textTree)
	textTree:visit(visit)
	return textTree
end
