#pragma once
#include "icu.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fmt/color.h>
#include <fmt/core.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Class that manages a set of source files.
 */
class SourceManager
{
	public:
	struct SourceLocation
	{
		uint32_t fileID = std::numeric_limits<uint32_t>::max();
		uint32_t line   = std::numeric_limits<uint32_t>::max();
		uint32_t offset = std::numeric_limits<uint32_t>::max();
	};

	class CompressedSourceLocation
	{
		/// The compressed data.
		uint32_t data = std::numeric_limits<uint32_t>::max();
		/**
		 * Number of bits used as a tag to indicate whether this is a large
		 * source location.
		 */
		static constexpr size_t LargeTagBits = 1;
		/// Offset of the large tag.
		static constexpr size_t LargeTagOffset = 0;
		/**
		 * Number of bits used for an index for source locations that can't be
		 * compressed.
		 */
		static constexpr size_t LargeIndexBits =
		  (sizeof(decltype(data)) * CHAR_BIT) - LargeTagBits;
		/**
		 * Offset for an index for source locations that can't be compressed.
		 */
		static constexpr size_t LargeIndexOffset =
		  LargeTagOffset + LargeTagBits;
		/**
		 * Number of bits used for a file ID.
		 */
		static constexpr size_t FileIDBits = 5;
		/**
		 * Offset for a file ID.
		 */
		static constexpr size_t FileIDOffset = LargeTagOffset + LargeTagBits;
		/**
		 * Number of bits used for a line number.
		 */
		static constexpr size_t LineBits = 10;
		/**
		 * Offset for a line number.
		 */
		static constexpr size_t LineOffset = FileIDOffset + FileIDBits;
		/**
		 * Number of bits used for a file offset.
		 */
		static constexpr size_t OffsetBits = 16;
		/**
		 * Offset for a file offset.
		 */
		static constexpr size_t OffsetOffset = LineOffset + LineBits;
		// Make sure that we've used all of the bits, but not used any bits that
		// don't exist.
		static_assert(
		  OffsetOffset + OffsetBits == 32,
		  "Compressed location size does not use the right number of bits");
		friend class SourceManager;

		/**
		 * Helper to extract a range of bits from the compressed data.
		 */
		template<size_t Start, size_t Length>
		decltype(data) bits()
		{
			return (data >> Start) & ((decltype(data)(1) << Length) - 1);
		}

		/**
		 * Helper to set a range of bits in the compressed data.
		 */
		void set_bits(decltype(data) value, size_t start, size_t length)
		{
			data &= ~(((decltype(data)(1) << length) - 1) << start);
			data |= value << start;
		}

		SourceLocation expand(SourceManager &manager)
		{
			if (!is_valid())
			{
				return SourceLocation{};
			}
			if (bits<LargeTagOffset, LargeTagBits>() == 0)
			{
				SourceLocation loc{
				  bits<FileIDOffset, FileIDBits>(),
				  bits<LineOffset, LineBits>(),
				  bits<OffsetOffset, OffsetBits>(),
				};
				return loc;
			}
			else
			{
				return manager.location_for_index(
				  bits<LargeIndexOffset, LargeIndexBits>());
			}
		}

		CompressedSourceLocation(SourceManager &manager,
		                         uint32_t       fileNumber,
		                         uint32_t       line,
		                         uint32_t       offset)
		  : data(0)
		{
			auto msb = [](size_t x) -> size_t {
				if (x == 0)
				{
					return 0;
				}
				return (sizeof(size_t) * CHAR_BIT) - std::countl_zero(x);
			};
			if ((msb(fileNumber) > FileIDBits) || (msb(line) > LineBits) ||
			    (msb(offset) > OffsetBits))
			{
				SourceLocation loc{fileNumber, line, offset};
				set_bits(1, LargeTagOffset, LargeTagBits);
				set_bits(manager.uncompressedLocations.size(),
				         LargeIndexOffset,
				         LargeIndexBits);
				manager.uncompressedLocations.push_back(loc);
				return;
			}
			set_bits(0, LargeTagOffset, LargeTagBits);
			set_bits(fileNumber, FileIDOffset, FileIDBits);
			set_bits(line, LineOffset, LineBits);
			set_bits(offset, OffsetOffset, OffsetBits);
		}
		public:
		/**
		 * Default constructor, creates an invalid source location.
		 */
		CompressedSourceLocation()  {}

		/**
		 * Check if this source location is valid.
		 */
		bool is_valid()
		{
			return data != std::numeric_limits<uint32_t>::max();
		}
	};

	private:
	/**
	 * Source locations that could not be compressed.  These are stored in full
	 * form and the compressed location contains an index into this array.
	 */
	std::vector<SourceLocation> uncompressedLocations;
	/**
	 * File names and their contents.  This must not be a vector because we take
	 * references to the values.
	 */
	std::unordered_map<size_t, std::pair<const std::string, const std::string>>
	  fileNames;

	/**
	 * Get the uncompressable source location stored at a given index.
	 */
	SourceLocation location_for_index(size_t index)
	{
		return uncompressedLocations.at(index);
	}
	friend class SourceManager;

	public:
	std::pair<size_t, const std::string &> add_file(std::string   name,
	                                                std::string &&contents)
	{
		size_t index = fileNames.size();
		fileNames.emplace(index, std::pair{name, std::move(contents)});
		return {index, fileNames.at(index).second};
	}

	CompressedSourceLocation
	compress(size_t fileNumber, uint32_t line, uint32_t offset)
	{
		return CompressedSourceLocation(*this, fileNumber, line, offset);
	}

	SourceLocation expand(CompressedSourceLocation &loc)
	{
		return loc.expand(*this);
	}

	enum class Severity
	{
		Warning,
		Error,
		Fatal,
	};

	void report_error(CompressedSourceLocation start,
	                  CompressedSourceLocation end,
	                  std::string              message,
	                  Severity                 severity = Severity::Error)
	{
		bool isError = severity != Severity::Warning;
		if (end.is_valid())
		{
			end = start;
		}
		if (!start.is_valid())
		{
			fmt::print("Unknown source location {}:\n{}\n",
			           fmt::styled(isError ? "Error" : "Warning",
			                       isError
			                         ? fmt::fg(fmt::terminal_color::red)
			                         : fmt::fg(fmt::terminal_color::yellow)),
			           message);
			return;
		}
		SourceLocation startLoc = expand(start);
		SourceLocation endLoc   = expand(end);
		assert(startLoc.fileID == endLoc.fileID);
		const auto &[fileName, fileContents] = fileNames.at(startLoc.fileID);
		// Location of the start
		auto startIter     = fileContents.begin() + startLoc.offset;
		auto endIter       = fileContents.begin() + endLoc.offset;
		auto lineStartIter = startIter;
		auto lineEndIter   = startIter;
		// Scan backwards to find the line start then skip the newline.
		while ((lineStartIter != fileContents.begin()) &&
		       (*lineStartIter != '\n'))
		{
			lineStartIter--;
		}
		if (*lineStartIter == '\n')
		{
			lineStartIter++;
		}
		// Scan forwards to find the line end.
		while ((lineEndIter != fileContents.end()) && (*lineEndIter != '\n'))
		{
			lineEndIter++;
		}
		// If the end is on a different line, then for now just treat the range
		// as from the start to the end of the line.
		if (endLoc.line != startLoc.line)
		{
			endIter = lineEndIter;
		}
		// Approximate the number of terminal characters in a run.  This should
		// be done properly with a grapheme iterator.
		auto characters = [](auto start, auto end) {
			char32_t    c;
			std::string ret;
			size_t      characters = 0;
			while (start != end)
			{
				int32_t offset = 0;
				U8_NEXT(start, offset, std::distance(start, end), c);
				start += offset;
				auto width = u_getIntPropertyValue(c, UCHAR_EAST_ASIAN_WIDTH);
				characters++;
				// If it's a double-width character, count it as two.
				if (u_getIntPropertyValue(c, UCHAR_EAST_ASIAN_WIDTH) ==
				    U_EA_WIDE)
				{
					characters++;
				}
			}
			return characters;
		};
		// Number of characters in each run.  We will subtract one from the
		// number in the middle because we always print the caret.
		size_t charsBefore = characters(lineStartIter, startIter);
		size_t charsMiddle =
		  std::max<size_t>(1, characters(startIter, endIter));
		size_t charsAfter = characters(endIter, lineEndIter);
		// Print the message!
		fmt::print("{}:{}:{}: {}: {}:\n{}\n{}{}{}{}\n",
		           fileName,
		           startLoc.line,
		           charsBefore,
		           fmt::styled(isError ? "Error" : "Warning",
		                       isError ? fmt::fg(fmt::terminal_color::red)
		                               : fmt::fg(fmt::terminal_color::yellow)),
		           message,
		           std::string(lineStartIter, lineEndIter),
		           std::string(charsBefore, ' '),
		           fmt::styled('^', fmt::fg(fmt::terminal_color::green)),
		           fmt::styled(std::string(charsMiddle - 1, '~'),
		                       fmt::fg(fmt::terminal_color::green)),
		           std::string(charsAfter, ' '));
	}

	/**
	 * Returns a singleton instance of this class.
	 */
	static SourceManager &shared_instance()
	{
		static SourceManager instance;
		return instance;
	}
};

using SourceLocation = SourceManager::CompressedSourceLocation;
using SourceRange    = std::pair<SourceLocation, SourceLocation>;
