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

// Cutoff, resonance and slope can all be moving; the coefficients are only recomputed when one is.
void FilterFx::update() noexcept {
    const double fc = std::exp(logCutoff_);
    const double boost = std::pow(maxQ/butterworthQ, resonanceNow_);
    const double q1 = butterworthQ*boost*(1-slopeNow_)+stageOneQ*slopeNow_;   // a 12 dB filter is one stage; 24 dB is two
    const double q2 = stageTwoQ*boost;
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
