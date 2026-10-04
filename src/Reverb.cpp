#include "Blocks.h"

namespace fxblock {
namespace {
constexpr double baseLength[ReverbFx::lines] {1013, 1129, 1283, 1409, 1511, 1667, 1801, 1949};   // samples at 48 kHz, mutually prime
constexpr double modulationRate[ReverbFx::lines] {.13, .17, .21, .25, .29, .31, .19, .23};          // Hz
constexpr double baseDiffuser[ReverbFx::diffusers] {142, 107, 79, 61};
constexpr double longest = 1.4;                                                                     // the biggest length scale, for allocation

// What makes the three flavors different spaces: how long the lines are (the size of the room), how bright the tail
// can be, how many diffusers smear the input and how much the lines wander.
struct Voicing {
    double lengthScale, dampHigh, dampLow, diffuserGain, modulation, diffuserScale;
    int diffusers;
};
constexpr Voicing voicing[flavorCount] {
    {.55, 14000, 3000, .60, 1.5, .8, 2},      // room: short lines, a little diffusion, quickly dark
    {1.35, 11000, 1200, .65, 5.0, 1.5, 3},    // hall: long lines, slow and warm
    {.85, 16000, 5500, .72, 3.0, 1.0, 4},     // plate: dense and bright, nothing like a room
};
}

void ReverbFx::prepare(double rate) {
    rate_ = rate;
    smooth_ = timeCoefficient(.05, rate);
    const double scale = rate/48000;
    pre_.prepare(static_cast<size_t>(rate*.11)+8);
    for (size_t i = 0; i < diffuser_.size(); ++i) diffuser_[i].line.prepare(static_cast<size_t>(baseDiffuser[i]*1.5*scale)+16);
    for (size_t i = 0; i < tank_.size(); ++i) {
        tank_[i].prepare(static_cast<size_t>(baseLength[i]*longest*scale+16*scale)+32);
        modRate_[i] = modulationRate[i];
    }
    tune();
    reset();
}

void ReverbFx::clearState() noexcept {
    pre_.clear();
    for (auto& d : diffuser_) d.line.clear();
    for (auto& t : tank_) t.clear();
    damp_.fill(0.f);
}

void ReverbFx::reset() noexcept {
    clearState();
    for (size_t i = 0; i < modPhase_.size(); ++i) modPhase_[i] = static_cast<double>(i)/static_cast<double>(modPhase_.size());
    preMs_ = std::clamp(target_.p3, 0., 100.);
}

void ReverbFx::setFlavor(uint32_t flavor) noexcept {
    flavor_ = std::min<uint32_t>(flavor, flavorCount-1);
    clearState();
    tune();
}

void ReverbFx::set(const BlockSettings& s, bool immediate) noexcept {
    target_ = s;
    tune();
    if (immediate) preMs_ = std::clamp(s.p3, 0., 100.);
}

// Everything that follows from the flavor and the Decay and Damping knobs.
void ReverbFx::tune() noexcept {
    const Voicing& v = voicing[std::min<uint32_t>(flavor_, flavorCount-1)];
    const double scale = rate_/48000;
    // The lines' own gains are a little too generous to be named exactly: measured around 1 kHz the tail ran 87 to 97% of
    // the time the knob shows, so the gains are set for 7% longer, which puts the three flavors within about 7% of it.
    const double rt60 = reverbDecay(flavor_, std::clamp(target_.p1, 0., 100.))*1.07;
    for (size_t i = 0; i < length_.size(); ++i) {
        length_[i] = baseLength[i]*v.lengthScale*scale;
        gain_[i] = static_cast<float>(std::pow(10., -3*length_[i]/(rt60*rate_)));           // each line's gain gives that decay over its own length
    }
    const double damp = std::clamp(target_.p2, 0., 100.)*.01;
    dampCoef_ = std::exp(-2*pi*v.dampHigh*std::pow(v.dampLow/v.dampHigh, damp)/rate_);
    modDepth_ = v.modulation*scale;
    diffuserGain_ = v.diffuserGain;
    activeDiffusers_ = v.diffusers;
    for (size_t i = 0; i < diffuser_.size(); ++i) diffuser_[i].delay = baseDiffuser[i]*v.diffuserScale*scale;
}

size_t ReverbFx::tailSamples() const noexcept {
    const double seconds = reverbDecay(flavor_, std::clamp(target_.p1, 0., 100.))+std::clamp(target_.p3, 0., 100.)*.001+.1;
    return static_cast<size_t>(std::min(14., seconds)*rate_);
}

Stereo ReverbFx::process(Stereo in) noexcept {
    follow(preMs_, std::clamp(target_.p3, 0., 100.), smooth_);
    // The pre-delay: zero is the current sample, so the knob is continuous from the bottom.
    pre_.push(.5f*(in.l+in.r));
    float m = pre_.readLinear(1+preMs_*rate_/1000.);
    for (int i = 0; i < activeDiffusers_; ++i) m = diffuser_[static_cast<size_t>(i)].process(m, static_cast<float>(diffuserGain_));
    // Eight modulated lines, damped, mixed by a Hadamard matrix.
    float y[lines], v[lines];
    for (size_t i = 0; i < lines; ++i) {
        modPhase_[i] += modRate_[i]/rate_; modPhase_[i] -= std::floor(modPhase_[i]);
        y[i] = tank_[i].read(length_[i]+modDepth_*std::sin(2*pi*modPhase_[i]));
        damp_[i] = tiny(y[i]+static_cast<float>(dampCoef_)*(damp_[i]-y[i]));
        v[i] = damp_[i]*gain_[i];
    }
    // Fast Walsh-Hadamard transform, normalised: an orthogonal matrix, so the loop only loses what the gains take.
    for (size_t len = 1; len < lines; len <<= 1)
        for (size_t i = 0; i < lines; i += len << 1)
            for (size_t j = i; j < i+len; ++j) { const float a = v[j], b = v[j+len]; v[j] = a+b; v[j+len] = a-b; }
    constexpr float norm = .35355339f;
    for (size_t i = 0; i < lines; ++i) tank_[i].push(tiny(clean(v[i]*norm+m*(i & 1 ? -.25f : .25f))));
    return {.5f*(y[0]-y[2]+y[4]-y[6]), .5f*(y[1]-y[3]+y[5]-y[7])};
}
} // namespace fxblock
