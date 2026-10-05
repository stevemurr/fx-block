#include "Blocks.h"

namespace fxblock {
namespace {
constexpr double maxQ = 8, butterworthQ = .70710678, stageOneQ = .5411961, stageTwoQ = 1.3065630;
}

void FilterFx::prepare(double rate) {
    rate_ = rate;
    smooth_ = timeCoefficient(.008, rate);
    morph_ = timeCoefficient(.01, rate);
    reset();
}

void FilterFx::reset() noexcept {
    for (auto& channel : stage_) for (auto& s : channel) s.clear();
    logCutoff_ = logGoal_; resonanceNow_ = resonance_; slopeNow_ = slope_;
    for (int i = 0; i < 3; ++i) weight_[i] = goal_[i];
    lastLog_ = lastResonance_ = lastSlope_ = -1;                          // force the coefficients to be computed
    update();
}

void FilterFx::set(const BlockSettings& s, bool immediate) noexcept {
    logGoal_ = std::log(std::clamp(s.p1, 20., 20000.));
    resonance_ = std::clamp(s.p2, 0., 100.)*.01;
    slope_ = s.p3 >= .5 ? 1. : 0.;
    flavor_ = std::min<uint32_t>(s.flavor, flavorCount-1);
    for (int i = 0; i < 3; ++i) goal_[i] = static_cast<uint32_t>(i == 0 ? 0 : i == 1 ? 2 : 1) == flavor_ ? 1. : 0.;
    if (immediate) reset();
}

// The two stages' Q for a resonance (0..1) and a slope blend (0 = 12 dB, 1 = 24 dB).
void FilterFx::qs(double resonance, double slope, double& q1, double& q2) noexcept {
    const double boost = std::pow(maxQ/butterworthQ, resonance);
    q1 = butterworthQ*boost*(1-slope)+stageOneQ*slope;       // a 12 dB filter is one stage; 24 dB is two
    q2 = stageTwoQ*boost;
}

double FilterFx::responseDb(uint32_t flavor, double cutoffHz, double resonance, bool slope24, double hz, double rate) noexcept {
    // The trapezoidal state-variable filter is the analog one with frequency warped: x is hz relative to the cutoff,
    // each in tan(pi f / rate).
    const double fc = std::clamp(cutoffHz, 20., .49*rate), f = std::clamp(hz, 1., .499*rate);
    const double x = std::tan(pi*f/rate)/std::tan(pi*fc/rate);
    double q1, q2;
    qs(std::clamp(resonance, 0., 1.), slope24 ? 1. : 0., q1, q2);
    auto stage = [&](double q) {
        const double denominator = std::sqrt((1-x*x)*(1-x*x)+(x/q)*(x/q));
        const double numerator = flavor == 0 ? 1. : flavor == 1 ? x*x : x/q;           // lowpass, highpass, bandpass (unity at the center)
        return numerator/denominator;
    };
    double magnitude = stage(q1);
    if (slope24) magnitude *= stage(q2);
    return 20*std::log10(std::max(magnitude, 1e-12));
}

// Cutoff, resonance and slope can all be moving; the coefficients are only recomputed when one is.
void FilterFx::update() noexcept {
    const double fc = std::exp(logCutoff_);
    double q1, q2;
    qs(resonanceNow_, slopeNow_, q1, q2);
    for (auto& channel : stage_) { channel[0].set(fc, q1, rate_); channel[1].set(fc, q2, rate_); }
    lastLog_ = logCutoff_; lastResonance_ = resonanceNow_; lastSlope_ = slopeNow_;
}

Stereo FilterFx::process(Stereo in) noexcept {
    follow(logCutoff_, logGoal_, smooth_);
    follow(resonanceNow_, resonance_, smooth_);
    follow(slopeNow_, slope_, morph_);
    for (int i = 0; i < 3; ++i) follow(weight_[i], goal_[i], morph_);
    if (logCutoff_ != lastLog_ || resonanceNow_ != lastResonance_ || slopeNow_ != lastSlope_) update();
    const float wl = static_cast<float>(weight_[0]), wb = static_cast<float>(weight_[1]), wh = static_cast<float>(weight_[2]);
    const float s = static_cast<float>(slopeNow_);
    float out[2];
    const float input[2] {in.l, in.r};
    for (size_t c = 0; c < 2; ++c) {
        const auto a = stage_[c][0].process(input[c]);
        const float first = wl*a.lp+wb*static_cast<float>(stage_[c][0].k)*a.bp+wh*a.hp;
        const auto b = stage_[c][1].process(first);
        const float second = wl*b.lp+wb*static_cast<float>(stage_[c][1].k)*b.bp+wh*b.hp;
        out[c] = clean(first+s*(second-first));
    }
    return {out[0], out[1]};
}
} // namespace fxblock
