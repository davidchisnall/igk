
function process(textTree)
	local document = TextTree.new("document")
	local addUse = function(module)
		local use = document:new_child("use")
		use:attribute_set("module", "packages." .. module)
	end
	addUse("lists")
	addUse("verbatim")
	addUse("color")
	addUse("labelrefs")
	addUse("ptable")
	addUse("framebox")
	if config.sile_packages then
		for package in string.gmatch(config.sile_packages, "([^;]+)") do
			print("Adding package from config:", package)
			addUse(package)
		end
	end
	document:attribute_set("class", "resilient.book")
	document:attribute_set("layout", "division 9")
	document:attribute_set("papersize", "6in x 9in")
	local contentsChapter = document:new_child("chapter")
	contentsChapter:attribute_set("numbering", "false")
	contentsChapter:attribute_set("toc", "false")
	contentsChapter:append_text("Contents")
	document:new_child("tableofcontents")
	document:append_child(textTree)
	return document 
end
