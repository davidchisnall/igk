function process(textTree)
	textTree:match("title", function(title)
		config.title = title:text()
		return {}
	end)
	textTree:match("author", function(author)
		config.author = author:text()
		return {}
	end)
	return textTree
end
