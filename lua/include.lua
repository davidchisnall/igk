function resolve_relative_path(textTree, path)
	if path:find("/", 1, true) ~= 1 then
		local dirname = textTree:source_directory_name()
		path = dirname .. "/" .. path
	end
	return path
end

function visit_includes(tree)
	if type(tree) ~= 'string' then
		if tree.kind == "include" then
			local newTree = read_file(resolve_relative_path(tree, tree:text()))
			if newTree then
				return {newTree}
			end
			tree:report_error("Failed to parse included file: " .. tree:text())
		end
		tree:visit(visit_includes)
	end
	return { tree }
end

function process(tree)
	tree:visit(visit_includes)
	return tree 
end
