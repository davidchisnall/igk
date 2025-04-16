I got Knuth'd
=============

Donald Knuth famously took a multi-year digression from writing The Art of Computer Programming to build a typesetting system.
When I started writing The CHERIoT Programmers' Handbook, I found myself similarly dissatisfied with the state of document preparation systems, though I had several decades more prior work to build on so didn't need to invent everything from scratch.

For modern electronic documents, I want to target several output formats with different requirements:

 - HTML for online viewing.
 - XHTML plus additional metadata in ePub for eBook distribution.
 - PDFs with hyperlinks for online reading.
 - PDFs for printing.

Roughly speaking, this is two formats—PDF and HTML—with some minor variations.
These two formats have very different properties.

PDF is intended to be close to printer output.
Text and graphics are laid out in fixed locations and everything that ends up in the final version (with the exception of hyperlinks and document-level metadata) is *presentation markup*.
This includes text attributes such as 'italic' or 'red'.

HTML is a *reflowable* medium.
It does not define a paper size and renderers will dynamically reflow text and images based on the size of the viewer.
HTML also supports some degree of *semantic markup*.
In HTML, headings are not just big text, they are headings.
CSS classes can define other semantic classes, such as keywords, and these can compose.
For example, in a code listing, something might be both code and a comment, or code and an identifier.
This will adopt the style for code (for example, monospaced and of a particular size) *and* the style specific to that kind of listing (for example, a colour, or italic).
The styling is managed separately from the text, which makes it possible to provide different styles for colour-blind readers and so on.

I wrote my first two books using LaTeX and found that it produced great PDFs.
When my publisher generated ePubs, they mangled a lot of the text.
For my third book, I wrote semantic markup in TeX syntax that I could render with LaTeX or parse into a document tree and export as HTML.

This flow worked well, but the code for doing that depended on a bunch of things that weren't maintained anymore, so for the CHERIoT book, I decided to try AsciiDoc.
The experience was not great.
The AsciiDoc syntax is complex (I had to remember to type `{cpp}` instead of `C++` every time because `++` is apparently some markup thing).
I wanted to typeset listings from C++ source files using libclang, but I couldn't work out how to write a plugin that did this.
In particular, I wanted to be able to style Lua, Rego, and C++ in a consistent way, but the documentation for plugins suggested that you effectively generated HTML or PDF-specific markup.

So what is this igk thing?
--------------------------

In simple terms, `igk` is a compiler for documents.
It reads input in a regular TeX-like syntax and builds a tree.
It then runs passes (which can be written in C++ or Lua - almost all of the ones I use are Lua) to transform the document.
Finally, an output pass writes some output.

This is used to generate the PDF, online HTML, and ePub editions of the CHERIoT Programmers' Guide.

Passes are usually written in Lua.
They are provided as stand-alone files that expose a function called `process`
This takes a text tree as input and produce a text tree as output.

The input is expected to be tailored to your project.
You can define any semantic markup you want and then passes can lower it as you choose.
You define the sequence of passes to run.
Some [come with igk](lua/), the CHERIoT book [includes a load of its own](https://github.com/CHERIoT-Platform/book/tree/main/lua) (some of which probably should live in this repo).
If you don't like how some markup is transformed, you can add a pass that does it differently.

Why not {other thing}?
----------------------

I had several requirements for a document system and no existing tool that I found met all of them.
The two most important are discussed below.

### Don't favour presentation markup over semantic

Markup languages such as AsciiDoc and Markdown have short forms for emphasised text or code.
Unfortunately, 'code' is not sufficiently specific.
In my Cocoa book, for example, I had a custom markup command for class names, which also let me build an index of class names automatically.
In the CHERIoT book, I want to differentiate between commands, flags for commands, C++ code, Lua code, Rego code, and so on.
If marking something up as 'code' is easier than marking it up as a terminal command or as C++ code, then I will use the simpler markup, because I am lazy.
This causes problems later.

The nice thing about LaTeX was that *everything* used the same syntax and everything was a macro.
Whether you write `\section{A section heading}` or `\textit{some italic text}`, the effort is the same and the processing flows are the same.
It's easy to start writing a new kind of markup and the later go and define how it's presented (and anything else that happens while it's processed).

**It should be no harder to write a new custom semantic markup element than it is to use any pre-defined markup.**

In `igk`, there is *no* predefined markup.
All markup starts with a backslash, followed by the tree-node kind.
It optionally has key-value attribute pairs provided as a comma-separated list in square brackets.
It then has children in braces.
The meaning of any node is defined by passes that operate on it and these passes are provided by the user.

### An easily extensible model

My first two books used the LaTeX listings package, which does syntactic highlighting.
When I generated the HTML for the third one, I used libclang, which can fall back to token types but also has a richer set of semantic information from a complete parse.
For example, it knows the difference between a class reference, a macro instantiation, or a local variable reference.
This let me do better syntax highlighting in the ePub edition than the print or PDF versions.

This requires an easy plugin interface that lets you build text trees that can be processed later.

In `igk`, the document model is provided by the core but everything else is extensible in two ways:

 - Plugins are shared objects that can provide additional functions to Lua code or can provide C++ passes.
 - Lua passes can use functions provided by simply writing a `.lua` file and can use functions exported by plugins.

This makes it easy to write a pass that matches a tree node that takes a name of a file and some markers as attributes and then uses TreeSitter or libClang to parse them and build a tree of nodes containing semantic markup for a fragment of a source file.

What is the document model?
---------------------------

The document model is similar to the core ideas of SGML or XML.
A document is a tree of nodes.
Each node may have attributes that are key-value pairs.
Nodes have children that are either other nodes or strings (character data, in XML terminology).

If you write `\section[label=start]{In the beginning}` then you have created a node whose kind is `section` that contains one attribute (`label`) with the value `start`.
This node has a single child, which is the string `In the beginning`.

That's it.
The operations on tree nodes are defined in [`document.hh`](document.hh).

Most passes will use the `visit` or `match` / `match_any` functions to traverse the tree.

Your Lua interop copies a load of strings, that looks slow
----------------------------------------------------------

Copying strings in L1 cache is *very* fast on modern hardware.
In processing the CHERIoT book, over 90% of the total run time is spent in libclang.

How do I write a pass?
----------------------

A pass in Lua is just a Lua file that contains a function called `process`.
This is a no-op pass:

```lua
function process(textTree)
    return textTree
end
```

If you want to handle a particular tree node kind, use the `match` function.
The following is a pass that removes all nodes whose kind is `comment`:


```lua
function process(textTree)
    textTree:match("comment", function(comment)
        return {}
    end)
    return textTree
end
```

The visitor functions (`visit`, `match`, and `match_any`) take a visitor function that returns an *array* of zero or more nodes to replace it with.
Here, we return an empty array to delete the node.

Should I use igk?
-----------------

Probably not.
It was designed to solve a problem that I have.
It may or may not solve any problems that you have.
