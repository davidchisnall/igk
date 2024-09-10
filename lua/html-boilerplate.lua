function process(textTree)
	local body = TextTree.new("body")
	body:append_child(textTree)
	local html = TextTree.new("html")
	local head = TextTree.new("head")
	local link = TextTree.new("link")
	link:attribute_set("rel", "stylesheet")
	link:attribute_set("href", "book.css")
	head:append_child(link)
	html:append_child(head)
	html:append_child(body)
	return html 
end
