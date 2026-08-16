#include "Quantizer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace bmbo {

void Quantizer::setScale(const Scale& scale) {
	cachedScale = scale;
	rebuildCache();
}

void Quantizer::rebuildCache() {
	sortedCents.clear();
	if (!cachedScale.isValid())
		return;

	sortedCents.reserve(cachedScale.intervals.size());
	for (const Interval& iv : cachedScale.intervals) {
		// Fold every interval into [0, 1200) cents. Octave-repeating
		// (Closed) scales treat the octave itself as equivalent to
		// unison, so e.g. an interval written as 2/1 (1200 cents)
		// folds down to 0.
		double c = std::fmod(iv.cents, 1200.0);
		if (c < 0.0)
			c += 1200.0;
		if (c >= 1200.0)
			c -= 1200.0;
		sortedCents.push_back(c);
	}

	std::sort(sortedCents.begin(), sortedCents.end());
	sortedCents.erase(std::unique(sortedCents.begin(), sortedCents.end()), sortedCents.end());

	// Always guarantee a unison (0 cents) entry exists, even if the BTF
	// author forgot to list "1/1" explicitly -- otherwise very small
	// input voltages would have nothing sensible to snap to.
	if (sortedCents.empty() || sortedCents.front() > 1e-6) {
		sortedCents.insert(sortedCents.begin(), 0.0);
	}
}

float Quantizer::quantize(float relativeVoltage) const {
	if (sortedCents.empty())
		return relativeVoltage;

	// Split the input into a whole-octave part and a fractional part in
	// [0, 1) volts, i.e. [0, 1200) cents. Only the fractional part needs
	// to be quantized; the octave is just carried through unchanged
	// (that's what "octave repeating" means).
	double octave = std::floor(relativeVoltage);
	double fracVolts = relativeVoltage - octave;
	double fracCents = fracVolts * 1200.0;

	// Find the nearest interval, checking wraparound to the next/previous
	// octave (e.g. an input very close to 1200 cents might be closer to
	// the *next* octave's 0-cents unison than to this octave's highest
	// interval).
	double bestCents = sortedCents.front();
	double bestDist = std::numeric_limits<double>::infinity();
	int bestOctaveDelta = 0;

	for (double c : sortedCents) {
		// Compare against this octave, the octave below, and the octave
		// above, and keep whichever is closest.
		const double candidates[3] = { c - 1200.0, c, c + 1200.0 };
		const int deltas[3] = { -1, 0, 1 };
		for (int i = 0; i < 3; i++) {
			double dist = std::abs(fracCents - candidates[i]);
			if (dist < bestDist) {
				bestDist = dist;
				bestCents = c;
				bestOctaveDelta = deltas[i];
			}
		}
	}

	double quantizedVolts = octave + bestOctaveDelta + (bestCents / 1200.0);
	return (float) quantizedVolts;
}

} // namespace bmbo
