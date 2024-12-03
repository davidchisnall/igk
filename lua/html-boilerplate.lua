function process(textTree)
	local body = TextTree.new("body")
	local html = TextTree.new("html")
	local head = TextTree.new("head")
	local link = TextTree.new("link")
	link:attribute_set("rel", "stylesheet")
	link:attribute_set("href", "book.css")
	if config.title then
		local title = head:new_child("title")
		title:append_text(config.title)
	end
	head:append_child(link)
	local metaCharset = head:new_child("meta")
	metaCharset:attribute_set("charset", "UTF-8")
	html:append_child(head)
	if config.title then
		local h1 = body:new_child("h1")
		h1:attribute_set("class", "booktitle")
		h1:append_text(config.title)
		local details = body:new_child("div")
		details:attribute_set("class", "details")
		if config.author then
			local author = details:new_child("span")
			author:attribute_set("class", "author")
			author:attribute_set("id", "author")
			details:append_text(config.author)
		end
	end

	body:append_child(textTree)
	html:append_child(body)
	return html
end
