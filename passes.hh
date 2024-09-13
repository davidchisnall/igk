#pragma once
#include "document.hh"
#include <memory>
#include <fstream>

/**
 * A pass that processes a text tree.
 */
struct TextPass
{
	/**
	 * The name of the pass.
	 */
	virtual TextTreePointer process(TextTreePointer tree) = 0;

	/**
	 * Virtual destructor.
	 */
	virtual ~TextPass() = default;
};

struct OutputPass : public TextPass
{
	std::unique_ptr<std::fstream> file;
	enum OutputType {
		StdOut,
		StdErr,
		File
	} output_type = StdOut;

	std::ostream &out()
	{
		switch (output_type)
		{
			case StdOut:
				return std::cout;
			case StdErr:
				return std::cerr;
			case File:
				return *file;
		}
	}

	void output_stdout()
	{
		file.reset();
		output_type = StdOut;
	}

	void output_stderr()
	{
		file.reset();
		output_type = StdErr;
	}

	void output_file(std::string filename)
	{
		file = std::make_unique<std::fstream>(
		  filename, std::ios::out | std::ios::binary | std::ios::trunc);
		output_type = File;
	}
};

struct TextPassFactory
{
	virtual std::string               name()   = 0;
	virtual std::shared_ptr<TextPass> create() = 0;
};

class TextPassRegistry
{
	inline static std::unordered_map<std::string,
	                                 std::shared_ptr<TextPassFactory>>
	  passes;

	template<typename T>
	struct SingletonTextPassFactory : public TextPassFactory
	{
		std::string name() override
		{
			return T::name();
		}

		std::shared_ptr<TextPass> create() override
		{
			static std::shared_ptr<T> instance = std::make_shared<T>();
			return instance;
		}

		static std::shared_ptr<TextPassFactory> factory()
		{
			return std::make_shared<SingletonTextPassFactory<T>>();
		}
	};

	public:
	static void add(std::shared_ptr<TextPassFactory> pass)
	{
		passes[pass->name()] = pass;
	}

	static std::shared_ptr<TextPass> create(const std::string &name)
	{
		auto it = passes.find(name);
		if (it == passes.end())
		{
			return nullptr;
		}
		return it->second->create();
	}

	template<typename T>
	static void add()
	{
		add(SingletonTextPassFactory<T>::factory());
	}
};

