#pragma once

#include <string>
#include "Scale.hpp"

namespace bmbo {

/**
 * Result of a parse attempt: either a valid Scale, or a human-readable
 * error message. Never both, never neither.
 *
 * Keeping this as its own struct (rather than throwing exceptions or
 * returning a bare bool) lets the caller decide what to do with a
 * failure -- in our case, the Module keeps whatever Scale was already
 * loaded and just surfaces the error message on the display / LED.
 */
struct ParseResult {
	bool success = false;
	Scale scale;
	std::string errorMessage;

	static ParseResult ok(Scale s) {
		ParseResult r;
		r.success = true;
		r.scale = std::move(s);
		return r;
	}

	static ParseResult fail(const std::string& message) {
		ParseResult r;
		r.success = false;
		r.errorMessage = message;
		return r;
	}
};

/**
 * Parses BTF (Bmbo Tuning Format) text into a Scale.
 *
 * BTF is a small, line-oriented, human-writable text format:
 *
 *   BTF 1.0
 *
 *   Title: Ethiopian Example
 *   Root: 440
 *   Structure: Closed
 *
 *   Intervals:
 *   1/1
 *   16/15
 *   9/8
 *   ...
 *
 * Rules:
 *   - Blank lines are ignored anywhere in the file.
 *   - Lines starting with '#' or ';' are treated as comments and ignored.
 *   - The header line ("BTF <version>") must be the first non-blank,
 *     non-comment line.
 *   - "Title:", "Root:", and "Structure:" are optional metadata fields
 *     and may appear in any order, before the "Intervals:" section.
 *   - Everything after the "Intervals:" line is parsed as one interval
 *     per line, until end of file.
 *   - Each interval line is either:
 *       * a ratio, written as "num/den" (e.g. "3/2") or a bare integer
 *         (e.g. "2", meaning 2/1), or
 *       * a cents value, written with a decimal point (e.g. "701.955").
 *   - At least one interval is required.
 *
 * This class only parses text -> Scale. It performs no file I/O itself,
 * so it can be unit-tested without touching disk, and reused verbatim if
 * BTF text ever arrives from somewhere other than a file (e.g. pasted,
 * or embedded in a patch).
 */
class BTFParser {
public:
	// Parses the full contents of a BTF file (already read into a string).
	static ParseResult parse(const std::string& text);

private:
	static std::string trim(const std::string& s);
	static bool isCommentOrBlank(const std::string& line);
	static bool parseIntervalLine(const std::string& line, Interval& outInterval, std::string& outError);
};

} // namespace bmbo
