#pragma once

// The value grid of a slider: positions min, min+step, ..., max. The native slider is driven in "index units" (the
// provider reports the grid index, 0..positions-1, with an integer step of 1), which keeps the knob exact for any
// step and lets the host decide whether the step dividers are drawn. The real value is printed by the formatter.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace lever::detail {

struct SliderGrid {
    double min  = 0;
    double step = 1;
    int    positions = 1; // >= 1

    /// Dividers are drawn by default only for sliders with at most this many positions.
    static constexpr int kMaxDividerPositions = 10;
    static constexpr int kMaxPositions        = 100000;

    static SliderGrid of(double min, double max, double step) {
        SliderGrid g;
        g.min  = min;
        g.step = step > 0 ? step : 1;
        if (!(max > min)) {
            g.positions = 1;
            return g;
        }
        double n = std::floor((max - min) / g.step + 1e-9) + 1;
        if (n < 1) n = 1;
        if (n > kMaxPositions) n = kMaxPositions;
        g.positions = static_cast<int>(n);
        return g;
    }

    [[nodiscard]] double max() const { return min + step * (positions - 1); }

    /// Nearest grid index of `value`, clamped.
    [[nodiscard]] int indexOf(double value) const {
        if (!(value > min)) return 0;
        double i = std::round((value - min) / step);
        if (i > positions - 1) i = positions - 1;
        return static_cast<int>(i);
    }

    /// Value at `index`, clamped to the grid. The product is rounded to the step's decimals so 7 * 0.05 is 0.35,
    /// not 0.35000000000000003.
    [[nodiscard]] double valueAt(int index) const {
        if (index < 0) index = 0;
        if (index > positions - 1) index = positions - 1;
        const double raw = min + step * index;
        const int    d   = decimals();
        if (d == 0) return std::round(raw);
        const double p = std::pow(10.0, d);
        return std::round(raw * p) / p;
    }

    /// Decimals needed to print a value on the grid (0.05 -> 2, 1 -> 0, 2.5 -> 1), at most 6.
    [[nodiscard]] int decimals() const {
        for (int d = 0; d <= 6; ++d) {
            const double p = std::pow(10.0, d);
            if (std::fabs(step * p - std::round(step * p)) < 1e-9 && std::fabs(min * p - std::round(min * p)) < 1e-9) return d;
        }
        return 6;
    }

    /// Whether the step dividers are drawn: the LEVER_SETTING_FLAG_* override, else by position count.
    [[nodiscard]] bool showSteps(std::uint32_t flags) const {
        if (flags & 2u /* LEVER_SETTING_FLAG_HIDE_STEPS */) return false;
        if (flags & 1u /* LEVER_SETTING_FLAG_SHOW_STEPS */) return true;
        return positions <= kMaxDividerPositions;
    }

    [[nodiscard]] std::string format(int index) const {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.*f", decimals(), valueAt(index));
        return buf;
    }
};

} // namespace lever::detail
