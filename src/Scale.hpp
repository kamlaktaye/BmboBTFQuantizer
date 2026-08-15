#pragma once

#include <string>
#include <vector>
#include "Interval.hpp"

namespace bmbo {

enum class Structure {
	Closed, // Octave-repeating (v0.1 supports this)
	Open    // Non-repeating (reserved for a future version)
};

/**
 * A fully parsed BTF tuning: metadata plus the ordered list of intervals.
 *
 * Scale is a plain data holder with no VCV/UI dependencies, so it can be
 * shared between the parser, the DSP quantizer, and the display widget
 * without any of them depending on each other.
 */
class Scale {
public:
	std::string title = "(none)";
	double rootFrequency = 261.6256; // Hz. Defaults to C4 if unspecified.
	Structure structure = Structure::Closed;
	std::vector<Interval> intervals;

	bool isValid() const {
		return !intervals.empty();
	}

	int numIntervals() const {
		return (int) intervals.size();
	}

	std::string structureName() const {
		return structure == Structure::Closed ? "Closed" : "Open";
	}
};

} // namespace bmbo
