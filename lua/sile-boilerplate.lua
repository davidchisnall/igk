
function process(textTree)
	local document = TextTree.new("document")
	local addUse = function(module)
		local use = document:new_child("use")
		use:attribute_set("module", "packages." .. module)
	end
	addUse("lists")
	addUse("verbatim")
	addUse("color")
	document:attribute_set("class", "book")
	document:append_child(textTree)
	return document 
end
