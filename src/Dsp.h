#pragma once
#include "Common.h"

namespace fxblock {
// A state-variable filter in the zero-delay-feedback (trapezoidal) form: stable under fast modulation, and the
// low-pass, band-pass and high-pass all come from the same state.
struct Svf {
    double g = 0, k = 1.4142, a1 = 0, a2 = 0, a3 = 0;
    double ic1 = 0, ic2 = 0;
    struct Out { float lp, bp, hp; };
    // `bp` is the raw band-pass (gain Q at the center); k*bp has unity gain there.
    void set(double cutoff, double q, double rate) noexcept {
        g = std::tan(pi*std::min(cutoff, .49*rate)/rate);
        k = 1/q;
        a1 = 1/(1+g*(g+k)); a2 = g*a1; a3 = g*a2;
    }
    void clear() noexcept { ic1 = ic2 = 0; }
    Out process(float x) noexcept {
        const double v3 = x-ic2;
        const double v1 = a1*ic1+a2*v3;
        const double v2 = ic2+a2*ic1+a3*v3;
        ic1 = 2*v1-ic1; ic2 = 2*v2-ic2;
        if (std::abs(ic1) < 1e-20) ic1 = 0;
        if (std::abs(ic2) < 1e-20) ic2 = 0;
        return {static_cast<float>(v2), static_cast<float>(v1), static_cast<float>(x-k*v1-v2)};
    }
};

struct OnePole {
    float z = 0;
    float lowpass(float x, float coefficient) noexcept { z += coefficient*(x-z); z = tiny(z); return z; }
    float highpass(float x, float coefficient) noexcept { return x-lowpass(x, coefficient); }
};

// Removes DC and rumble below about 10 Hz.
struct DcBlock {
    float x1 = 0, y1 = 0;
    float process(float x, float r) noexcept { const float y = x-x1+r*y1; x1 = x; y1 = tiny(y); return y; }
};

// A 2x oversampler with no latency to speak of: two branches of allpass sections, one per phase of the high rate
// (see tools/gen_halfband.py). 80 dB down by 0.58 of the base rate, flat to 19.8 kHz at 48 kHz.
class Oversampler {
public:
    void clear() noexcept { for (auto& s : section_) s = {}; previous_ = 0; }
    // One input sample becomes two at the high rate.
    void up(float x, float& first, float& second) noexcept {
        first = run(0, x);
        second = run(1, x);
    }
    // Two samples at the high rate become one at the base rate.
    float down(float first, float second) noexcept {
        const float out = .5f*(run(2, first)+previous_);
        previous_ = run(3, second);
        return out;
    }
private:
    static constexpr int sectionsA = 3, sectionsB = 2;
    static constexpr double coefficientA[sectionsA] {0.061861712319425971, 0.43390374988559605, 0.87808803481475906};
    static constexpr double coefficientB[sectionsB] {0.22343256424115462, 0.65414024630636469};
    struct Section { double x1 = 0, y1 = 0; };
    // Chains 0 and 2 are the first branch, 1 and 3 the second: the up path and the down path each have their own state.
    float run(int chain, float x) noexcept {
        const bool first = chain == 0 || chain == 2;
        const int count = first ? sectionsA : sectionsB;
        const int base = chain*sectionsA;
        double v = x;
        for (int i = 0; i < count; ++i) {
            const double c = first ? coefficientA[i] : coefficientB[i];
            Section& s = section_[static_cast<size_t>(base+i)];
            const double y = c*v+s.x1-c*s.y1;
            s.x1 = v; s.y1 = std::abs(y) < 1e-20 ? 0. : y;
            v = s.y1;
        }
        return static_cast<float>(v);
    }
    std::array<Section, 4*sectionsA> section_ {};
    float previous_ = 0;
};
} // namespace fxblock
