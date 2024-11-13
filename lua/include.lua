function resolve_relative_path(textTree, path)
	if path:find("/", 1, true) ~= 1 then
		local dirname = textTree:source_directory_name()
		path = dirname .. "/" .. path
	end
	return path
end

function visit_includes(tree)
	if type(tree) ~= 'string' then
		print("Node kind: ", tree.kind)
		if tree.kind == "include" then
			print("Parsing include")
			local newTree = read_file(resolve_relative_path(tree, tree:text()))
			if newTree then
				return {newTree}
			end
		end
		tree = tree:visit(visit_includes)
	end
	return { tree }
end

function process(tree)
	tree:visit(visit_includes)
	return tree 
end
