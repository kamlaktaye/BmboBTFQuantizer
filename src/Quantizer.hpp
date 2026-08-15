#pragma once

#include <vector>
#include "Scale.hpp"

namespace bmbo {

/**
 * Pure DSP quantizer: maps an input 1V/oct voltage to the nearest note in
 * a Scale's tuning.
 *
 * Deliberately stateless per call (aside from a small internal cache
 * rebuilt whenever the Scale changes) so a single Quantizer instance can
 * process every polyphonic channel identically, with no per-channel
 * bookkeeping needed. This is what makes "each channel processed
 * independently" trivial: the Module just calls quantize() once per
 * channel per sample.
 *
 * v0.1 implements Closed (octave-repeating) scales only, per spec. The
 * Structure is still read from Scale so an Open-scale code path can be
 * added later without touching the Module or Widget.
 */
class Quantizer {
public:
	// Replaces the tuning used for quantization. Rebuilds the internal
	// sorted-cents cache used for fast nearest-neighbor lookup.
	void setScale(const Scale& scale);

	bool hasScale() const { return cachedScale.isValid(); }
	const Scale& getScale() const { return cachedScale; }

	// Quantizes a single 1V/oct voltage (already relative to the tuning's
	// root, i.e. with root offset/transpose subtracted out by the caller)
	// to the nearest interval in the loaded scale, and returns the
	// quantized relative voltage.
	//
	// If no scale is loaded, returns the input unchanged (pass-through)
	// so the module never produces silence/garbage before a file is
	// loaded.
	float quantize(float relativeVoltage) const;

private:
	Scale cachedScale;

	// Interval cents values, sorted ascending, always includes 0 (unison).
	// Rebuilt by setScale(). Kept sorted so nearest-neighbor search can
	// be a simple linear scan with early-out (interval counts are small,
	// typically < 100, so this stays fast without needing binary search).
	std::vector<double> sortedCents;

	void rebuildCache();
};

} // namespace bmbo
