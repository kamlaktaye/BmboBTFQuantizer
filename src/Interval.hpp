#pragma once

#include <string>
#include <cmath>

namespace bmbo {

/**
 * A single scale degree ("interval") within a BTF tuning.
 *
 * BTF stores intervals in whichever form the author wrote them in
 * (a frequency ratio like "3/2", or a cents value like "701.955").
 * Internally we always keep BOTH the ratio (frequency multiplier
 * relative to the root / 1-of-1) and the derived cents value, because:
 *
 *  - the ratio is the "source of truth" a musician wrote and wants to see
 *    on the display,
 *  - cents is what the quantizer's nearest-neighbor search actually needs,
 *    since 1V/oct maps linearly onto cents (1200 cents per volt).
 *
 * Converting cents -> ratio loses information for non-just intervals
 * written directly in cents (there's no "nice" ratio to show), so we
 * keep an `isRatio` flag and the original source text for display.
 */
struct Interval {
	// Frequency ratio relative to the root (e.g. 1.5 for a perfect fifth).
	double ratio = 1.0;

	// Cents relative to the root, derived from ratio: 1200 * log2(ratio).
	double cents = 0.0;

	// True if this interval was written as a ratio (e.g. "3/2") in the
	// source file, false if it was written directly as cents (e.g. "701.955").
	bool isRatio = true;

	// The exact text as it appeared in the BTF file, for display purposes.
	std::string sourceText;

	Interval() = default;

	static Interval fromRatio(double num, double den, const std::string& source) {
		Interval iv;
		iv.isRatio = true;
		iv.sourceText = source;
		iv.ratio = (den != 0.0) ? (num / den) : 1.0;
		iv.cents = centsFromRatio(iv.ratio);
		return iv;
	}

	static Interval fromCents(double centsValue, const std::string& source) {
		Interval iv;
		iv.isRatio = false;
		iv.sourceText = source;
		iv.cents = centsValue;
		iv.ratio = ratioFromCents(centsValue);
		return iv;
	}

	static double centsFromRatio(double ratio) {
		if (ratio <= 0.0)
			return 0.0;
		return 1200.0 * std::log2(ratio);
	}

	static double ratioFromCents(double cents) {
		return std::pow(2.0, cents / 1200.0);
	}
};

} // namespace bmbo
