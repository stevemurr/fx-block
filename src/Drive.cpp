#include "Blocks.h"

namespace fxblock {
namespace {
constexpr double overdriveBias = .12, fuzzBelow = .8, fuzzBias = .08;
}

void DriveFx::prepare(double rate) {
    rate_ = rate;
    smooth_ = timeCoefficient(.015, rate);
    overdrivePre_ = lowpassCoefficient(700, rate);
    fuzzPre_ = lowpassCoefficient(90, rate);
    dcR_ = 1-2*pi*10/rate;
    // How much each clipper raises a reference signal at each gain, so the loudness can be put back. The curves are
    // measured, not assumed: the table is the rms of one cycle of a -16 dBFS sine through the clipper, against the sine's.
    constexpr int cycle = 512;
    constexpr double reference = .16;
    for (uint32_t f = 0; f < 2; ++f) for (int k = 0; k < compensationSteps; ++k) {
        const double g = dbToGain(k*1.25);
        double sum = 0, sumSquares = 0;
        for (int n = 0; n < cycle; ++n) {
            const double y = clip(f, g*reference*std::sin(2*pi*n/cycle));
            sum += y; sumSquares += y*y;
        }
        const double mean = sum/cycle, spread = std::sqrt(std::max(sumSquares/cycle-mean*mean, 1e-12));
        compensation_[f][static_cast<size_t>(k)] = reference/std::sqrt(2.)/spread;
    }
    retarget();
    reset();
}

void DriveFx::reset() noexcept {
    for (auto& o : over_) o.clear();
    for (auto& f : pre_) f = {};
    for (auto& f : tone1_) f = {};
    for (auto& f : tone2_) f = {};
    for (auto& d : dc_) d = {};
    held_.fill(0.f); previous_.fill(0.); phase_ = 1;
    gain_ = gainGoal_; level_ = levelGoal_; tone_ = toneGoal_; bits_ = bitsGoal_; divisor_ = divisorGoal_;
}

void DriveFx::set(const BlockSettings& s, bool immediate) noexcept {
    target_ = s;
    retarget();
    if (immediate) { gain_ = gainGoal_; level_ = levelGoal_; tone_ = toneGoal_; bits_ = bitsGoal_; divisor_ = divisorGoal_; }
}

double DriveFx::inputGain(uint32_t flavor, double drive) noexcept {
    drive = std::clamp(drive, 0., 100.);
    return flavor == 0 ? dbToGain(drive*.46)               // 0 to 46 dB into the clipper
         : flavor == 1 ? dbToGain(20+drive*.45)            // a fuzz is never clean: 20 to 65 dB
         : 1.;
}

// What the three knobs ask for, in the units the signal path uses: the flavor decides what Drive and Tone mean.
void DriveFx::retarget() noexcept {
    const double drive = std::clamp(target_.p1, 0., 100.), tone = std::clamp(target_.p2, 0., 100.);
    levelGoal_ = dbToGain(std::clamp(target_.p3, -24., 12.));
    if (flavor_ < 2) gainGoal_ = inputGain(flavor_, drive);
    if (flavor_ < 2) levelGoal_ *= .8*compensation(gainGoal_);    // the more drive, the more it is pulled back (and the pre-emphasis's 2 dB taken off)
    toneGoal_ = lowpassCoefficient(700*std::pow(16000./700., tone*.01), rate_);
    bitsGoal_ = crushBits(drive);
    divisorGoal_ = crushDivisor(tone);
}

// The clippers run at twice the sample rate, so what they make above the audio band is filtered away instead of
// folding back into it. A hard clip makes harmonics that fall off by only 6 dB an octave, more than that can stop,
// so each clipper also uses first-order antiderivative anti-aliasing: instead of the curve at a sample, it takes the
// average of the curve between this sample and the last, from the curve's integral. That is a smoother stand-in for
// the continuous clip, and its harmonics are much quieter.
//   overdrive: tanh(v+b)-tanh(b), a soft clip, lopsided so that it makes even harmonics too
//   fuzz:      a hard clip that stops at +1 on top and -0.8 below: square-edged and uneven
double DriveFx::clip(uint32_t flavor, double v) noexcept {
    if (flavor == 0) return std::tanh(v+overdriveBias)-std::tanh(overdriveBias);
    return std::clamp(v+fuzzBias, -fuzzBelow, 1.);
}
double DriveFx::antiderivative(uint32_t flavor, double v) noexcept {
    if (flavor == 0) {
        const double u = v+overdriveBias, a = std::abs(u);
        return a+std::log1p(std::exp(-2*a))-std::log(2.)-std::tanh(overdriveBias)*v;          // ln cosh(u), without overflow
    }
    const double u = v+fuzzBias;
    if (u > 1) return u-.5;
    if (u < -fuzzBelow) return -fuzzBelow*u-.5*fuzzBelow*fuzzBelow;
    return .5*u*u;
}
double DriveFx::clipSmooth(size_t channel, double v) noexcept {
    const double before = previous_[channel], span = v-before;
    previous_[channel] = v;
    if (std::abs(span) < 1e-5) return clip(flavor_, .5*(v+before));
    return (antiderivative(flavor_, v)-antiderivative(flavor_, before))/span;
}

// Interpolated between the table's entries, in decibels of gain.
double DriveFx::compensation(double gain) const noexcept {
    const double position = std::clamp(20*std::log10(std::max(gain, 1.))/1.25, 0., compensationSteps-1.000001);
    const size_t i = static_cast<size_t>(position);
    const double f = position-static_cast<double>(i);
    const auto& table = compensation_[flavor_ < 2 ? flavor_ : 0];
    return table[i]+(table[i+1]-table[i])*f;
}

Stereo DriveFx::process(Stereo in) noexcept {
    follow(gain_, gainGoal_, smooth_); follow(level_, levelGoal_, smooth_); follow(tone_, toneGoal_, smooth_);
    follow(bits_, bitsGoal_, smooth_); follow(divisor_, divisorGoal_, smooth_);
    const float x[2] {in.l, in.r};
    float y[2];
    if (flavor_ == 2) {
        // Crush: hold a sample for `divisor` frames, rounded to the nearest of 2^(bits-1) steps either side of zero.
        phase_ += 1/divisor_;
        if (phase_ >= 1) {
            phase_ -= 1;
            const float steps = static_cast<float>(std::exp2(bits_-1));
            for (size_t c = 0; c < 2; ++c) held_[c] = std::round(x[c]*steps)/steps;
        }
        const float level = static_cast<float>(level_);
        return {clean(held_[0]*level), clean(held_[1]*level)};
    }
    const float comp = static_cast<float>(level_);
    const float toneCoef = static_cast<float>(tone_);
    for (size_t c = 0; c < 2; ++c) {
        float v;
        if (flavor_ == 0) v = x[c]+.45f*pre_[c].highpass(x[c], static_cast<float>(overdrivePre_));    // a mid hump into the clipper, as a screamer has
        else v = pre_[c].highpass(x[c], static_cast<float>(fuzzPre_));
        float a, b;
        over_[c].up(v, a, b);
        a = static_cast<float>(clipSmooth(c, static_cast<double>(a)*gain_)); b = static_cast<float>(clipSmooth(c, static_cast<double>(b)*gain_));
        float o = dc_[c].process(over_[c].down(a, b), static_cast<float>(dcR_));
        o = tone2_[c].lowpass(tone1_[c].lowpass(o, toneCoef), toneCoef);
        y[c] = clean(o*comp);
    }
    return {y[0], y[1]};
}
} // namespace fxblock
