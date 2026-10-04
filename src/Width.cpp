#include "Blocks.h"

namespace fxblock {
namespace {
constexpr double smearDelay[2][4] {{151, 53, 19, 7}, {137, 61, 23, 11}};      // samples at 48 kHz, different in each ear
}

void WidthFx::prepare(double rate) {
    rate_ = rate;
    smooth_ = timeCoefficient(.02, rate);
    haasLine_.prepare(static_cast<size_t>(rate*.04)+8);
    for (size_t c = 0; c < 2; ++c) for (size_t i = 0; i < sections; ++i) {
        smear_[c][i].delay = smearDelay[c][i]*rate/48000;
        smear_[c][i].line.prepare(static_cast<size_t>(smear_[c][i].delay)+8);
    }
    reset();
}

void WidthFx::clearState() noexcept {
    haasLine_.clear();
    for (auto& channel : smear_) for (auto& s : channel) s.line.clear();
    bassFilter_.clear();
}

void WidthFx::reset() noexcept { clearState(); retarget(); snap(); }

void WidthFx::set(const BlockSettings& s, bool immediate) noexcept {
    target_ = s;
    retarget();
    if (immediate) snap();
}

// What the knobs ask for in this flavor.
void WidthFx::retarget() noexcept {
    widthGoal_ = flavor_ == 0 ? widthFactor(std::clamp(target_.p1, 0., 100.)) : 1;
    haasGoal_ = flavor_ == 1 ? haasMs(std::clamp(target_.p1, 0., 100.)) : 0;
    spreadGoal_ = flavor_ == 2 ? std::clamp(target_.p1, 0., 100.)*.01 : 0;
    bassGoal_ = target_.p2 >= 20 ? 1 : 0;
    cutoffGoal_ = std::clamp(target_.p2, 20., 400.);
}

Stereo WidthFx::process(Stereo x) noexcept {
    follow(width_, widthGoal_, smooth_); follow(haas_, haasGoal_, smooth_); follow(spread_, spreadGoal_, smooth_);
    follow(bass_, bassGoal_, smooth_);
    if (bassGoal_ > 0) {
        const double before = bassCutoff_;
        follow(bassCutoff_, cutoffGoal_, smooth_);
        if (bassCutoff_ != before) bassFilter_.set(bassCutoff_, .7071, rate_);
    }

    if (width_ != 1.) {                                       // mid/side: the mid, and so the mono sum, is not touched
        const float mid = .5f*(x.l+x.r), side = .5f*(x.l-x.r)*static_cast<float>(width_);
        x.l = mid+side; x.r = mid-side;
    }
    if (flavor_ == 1) {
        haasLine_.push(x.r);                                  // always fed, so the delay is ready the moment Haas is turned up
        if (haas_ > 1e-3) x.r = haasLine_.readLinear(1+haas_*rate_/1000.);
    } else if (flavor_ == 2) {
        float l = x.l, r = x.r;
        for (size_t i = 0; i < sections; ++i) { l = smear_[0][i].process(l, .5f); r = smear_[1][i].process(r, .5f); }
        const float a = static_cast<float>(spread_);
        x.l += a*(l-x.l); x.r += a*(r-x.r);
    }
    if (bass_ > 0) {                                          // keep what is low in the middle: the side loses its lows
        const float mid = .5f*(x.l+x.r), side = .5f*(x.l-x.r);
        const auto s = bassFilter_.process(side);
        const float kept = side+static_cast<float>(bass_)*(s.hp-side);
        x.l = mid+kept; x.r = mid-kept;
    } else bassFilter_.clear();
    return {clean(x.l), clean(x.r)};
}
} // namespace fxblock
