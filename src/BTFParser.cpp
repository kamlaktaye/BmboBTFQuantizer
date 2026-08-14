#include "BTFParser.hpp"
#include <sstream>
#include <cctype>
#include <algorithm>

namespace bmbo {

std::string BTFParser::trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

bool BTFParser::isCommentOrBlank(const std::string& line) {
	if (line.empty())
		return true;
	return line[0] == '#' || line[0] == ';';
}

bool BTFParser::parseIntervalLine(const std::string& line, Interval& outInterval, std::string& outError) {
	// Ratio form: "num/den"
	size_t slashPos = line.find('/');
	if (slashPos != std::string::npos) {
		std::string numStr = trim(line.substr(0, slashPos));
		std::string denStr = trim(line.substr(slashPos + 1));
		try {
			size_t numEnd = 0, denEnd = 0;
			double num = std::stod(numStr, &numEnd);
			double den = std::stod(denStr, &denEnd);
			if (numEnd != numStr.size() || denEnd != denStr.size()) {
				outError = "invalid ratio \"" + line + "\"";
				return false;
			}
			if (den == 0.0) {
				outError = "division by zero in ratio \"" + line + "\"";
				return false;
			}
			if (num <= 0.0 || den < 0.0) {
				outError = "ratio must be positive in \"" + line + "\"";
				return false;
			}
			outInterval = Interval::fromRatio(num, den, line);
			return true;
		}
		catch (const std::exception&) {
			outError = "could not parse ratio \"" + line + "\"";
			return false;
		}
	}

	// Cents form: contains a decimal point, e.g. "701.955"
	if (line.find('.') != std::string::npos) {
		try {
			size_t end = 0;
			double cents = std::stod(line, &end);
			if (end != line.size()) {
				outError = "invalid cents value \"" + line + "\"";
				return false;
			}
			outInterval = Interval::fromCents(cents, line);
			return true;
		}
		catch (const std::exception&) {
			outError = "could not parse cents value \"" + line + "\"";
			return false;
		}
	}

	// Bare integer form: "2" means 2/1
	try {
		size_t end = 0;
		double num = std::stod(line, &end);
		if (end != line.size()) {
			outError = "unrecognized interval \"" + line + "\"";
			return false;
		}
		if (num <= 0.0) {
			outError = "interval must be positive in \"" + line + "\"";
			return false;
		}
		outInterval = Interval::fromRatio(num, 1.0, line);
		return true;
	}
	catch (const std::exception&) {
		outError = "unrecognized interval \"" + line + "\"";
		return false;
	}
}

ParseResult BTFParser::parse(const std::string& text) {
	std::istringstream stream(text);
	std::string rawLine;

	Scale scale;
	bool sawHeader = false;
	bool sawIntervalsKeyword = false;
	int lineNumber = 0;

	while (std::getline(stream, rawLine)) {
		lineNumber++;
		std::string line = trim(rawLine);

		if (isCommentOrBlank(line))
			continue;

		// First meaningful line must be the header: "BTF <version>"
		if (!sawHeader) {
			if (line.rfind("BTF", 0) != 0) {
				return ParseResult::fail(
					"line " + std::to_string(lineNumber) +
					": expected a \"BTF <version>\" header, found \"" + line + "\"");
			}
			sawHeader = true;
			continue;
		}

		// Once we're inside the Intervals section, every remaining
		// non-blank/non-comment line is an interval.
		if (sawIntervalsKeyword) {
			Interval iv;
			std::string err;
			if (!parseIntervalLine(line, iv, err)) {
				return ParseResult::fail("line " + std::to_string(lineNumber) + ": " + err);
			}
			scale.intervals.push_back(iv);
			continue;
		}

		// Metadata / keyword lines, before "Intervals:"
		auto colonPos = line.find(':');
		if (colonPos == std::string::npos) {
			return ParseResult::fail(
				"line " + std::to_string(lineNumber) +
				": expected \"Key: value\", found \"" + line + "\"");
		}

		std::string key = trim(line.substr(0, colonPos));
		std::string value = trim(line.substr(colonPos + 1));

		// Case-insensitive key comparison.
		std::string keyLower = key;
		std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
			[](unsigned char c) { return std::tolower(c); });

		if (keyLower == "title") {
			scale.title = value;
		}
		else if (keyLower == "root") {
			try {
				size_t end = 0;
				double hz = std::stod(value, &end);
				if (end != value.size() || hz <= 0.0) {
					return ParseResult::fail(
						"line " + std::to_string(lineNumber) + ": invalid Root frequency \"" + value + "\"");
				}
				scale.rootFrequency = hz;
			}
			catch (const std::exception&) {
				return ParseResult::fail(
					"line " + std::to_string(lineNumber) + ": invalid Root frequency \"" + value + "\"");
			}
		}
		else if (keyLower == "structure") {
			std::string valueLower = value;
			std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(),
				[](unsigned char c) { return std::tolower(c); });
			if (valueLower == "closed") {
				scale.structure = Structure::Closed;
			}
			else if (valueLower == "open") {
				scale.structure = Structure::Open;
			}
			else {
				return ParseResult::fail(
					"line " + std::to_string(lineNumber) +
					": Structure must be \"Open\" or \"Closed\", found \"" + value + "\"");
			}
		}
		else if (keyLower == "intervals") {
			sawIntervalsKeyword = true;
		}
		else {
			return ParseResult::fail(
				"line " + std::to_string(lineNumber) + ": unknown field \"" + key + "\"");
		}
	}

	if (!sawHeader) {
		return ParseResult::fail("file is empty or missing the \"BTF <version>\" header");
	}
	if (!sawIntervalsKeyword) {
		return ParseResult::fail("missing \"Intervals:\" section");
	}
	if (scale.intervals.empty()) {
		return ParseResult::fail("no intervals found after \"Intervals:\"");
	}

	return ParseResult::ok(std::move(scale));
}

} // namespace bmbo
