#include "Chain.h"

namespace fxblock {
namespace {
BlockSettings settingsFor(const Values& v, uint32_t block) {
    BlockSettings s;
    s.on = v[blockParam(block, On)] >= .5;
    s.flavor = static_cast<uint32_t>(std::clamp(v[blockParam(block, Flavor)], 0., flavorCount-1.));
    s.p1 = v[blockParam(block, P1)]; s.p2 = v[blockParam(block, P2)]; s.p3 = v[blockParam(block, P3)];
    s.mix = std::clamp(v[blockParam(block, BlockMix)], 0., 100.)*.01;
    return s;
}
}

void Chain::prepare(double rate) {
    rate_ = rate;
    smooth_ = timeCoefficient(.02, rate);
    peakDecay_ = std::exp(-1/(.35*rate));
    powerCoef_ = timeCoefficient(.4, rate); autoCoef_ = timeCoefficient(.15, rate); autoWeightCoef_ = timeCoefficient(.03, rate);
    filter_.prepare(rate); drive_.prepare(rate); modulation_.prepare(rate);
    delay_.prepare(rate); reverb_.prepare(rate); width_.prepare(rate);
    reset();
}

void Chain::reset() noexcept {
    filter_.reset(); drive_.reset(); modulation_.reset(); delay_.reset(); reverb_.reset(); width_.reset();
    inputNow_ = inputGoal_; outputNow_ = outputGoal_; mixNow_ = mixGoal_; bypassNow_ = bypassGoal_;
    inputPeak_ = outputPeak_ = 0;
    dryPower_ = wetPower_ = 0; autoGoal_ = autoNow_ = 1; autoWeight_ = autoOn_ ? 1 : 0;
}

void Chain::set(const Values& v, bool immediate) noexcept {
    inputGoal_ = dbToGain(v[Input]); outputGoal_ = dbToGain(v[Output]);
    mixGoal_ = std::clamp(v[Mix], 0., 100.)*.01; bypassGoal_ = v[Bypass] >= .5 ? 1 : 0;
    filterAfter_ = v[FilterOrder] >= .5;
    autoOn_ = v[AutoGain] >= .5;
    if (immediate) autoWeight_ = autoOn_ ? 1 : 0;
    filter_.set(settingsFor(v, FilterBlock), immediate); drive_.set(settingsFor(v, DriveBlock), immediate);
    modulation_.set(settingsFor(v, ModBlock), immediate); delay_.set(settingsFor(v, DelayBlock), immediate);
    reverb_.set(settingsFor(v, ReverbBlock), immediate); width_.set(settingsFor(v, WidthBlock), immediate);
    if (immediate) { inputNow_ = inputGoal_; outputNow_ = outputGoal_; mixNow_ = mixGoal_; bypassNow_ = bypassGoal_; }
}

Stereo Chain::process(Stereo in) noexcept {
    follow(inputNow_, inputGoal_, smooth_); follow(outputNow_, outputGoal_, smooth_);
    follow(mixNow_, mixGoal_, smooth_); follow(bypassNow_, bypassGoal_, smooth_);
    const float g = static_cast<float>(inputNow_);
    Stereo x {in.l*g, in.r*g};
    if (filterAfter_) { x = drive_.process(x); x = filter_.process(x); }
    else { x = filter_.process(x); x = drive_.process(x); }
    x = modulation_.process(x);
    x = delay_.process(x);
    x = reverb_.process(x);
    x = width_.process(x);
    // Auto gain: the chain's output is scaled to the loudness of the dry, so wet and dry compare at the same level. Both are
    // measured over 0.4 s (so it follows the level of the material, not the waveform) and the gain glides over 150 ms. It
    // only learns while the dry is audible: when the input stops, the last gain is held, so a reverb tail is not pumped
    // back up (or ducked) as the input falls away. The gain is limited to +/-24 dB.
    const float dryNow = .5f*(in.l*in.l+in.r*in.r), wetNow = .5f*(x.l*x.l+x.r*x.r);
    dryPower_ += powerCoef_*(dryNow-dryPower_); wetPower_ += powerCoef_*(wetNow-wetPower_);
    if (dryPower_ > 1e-6 && wetPower_ > 1e-12) autoGoal_ = std::clamp(std::sqrt(dryPower_/wetPower_), .0631, 15.85);
    follow(autoNow_, autoGoal_, autoCoef_);
    follow(autoWeight_, autoOn_ ? 1. : 0., autoWeightCoef_);
    if (autoWeight_ > 0) {
        const float a = static_cast<float>(1+autoWeight_*(autoNow_-1));
        x.l *= a; x.r *= a;
    }
    // Mix blends the chain back against the dry; Output is the final level; Bypass takes the dry around all of it.
    const float m = static_cast<float>(mixNow_), o = static_cast<float>(outputNow_), b = static_cast<float>(bypassNow_);
    // Written as (1-k)*a + k*b so that both ends are exact: Mix 0% and Bypass are the dry signal bit for bit.
    Stereo out {(1-m)*in.l+m*x.l, (1-m)*in.r+m*x.r};
    out.l *= o; out.r *= o;
    out.l = (1-b)*out.l+b*in.l; out.r = (1-b)*out.r+b*in.r;
    out.l = clean(out.l); out.r = clean(out.r);
    const float pin = std::max(std::abs(in.l), std::abs(in.r)), pout = std::max(std::abs(out.l), std::abs(out.r));
    inputPeak_ = std::max(pin, inputPeak_*static_cast<float>(peakDecay_));
    outputPeak_ = std::max(pout, outputPeak_*static_cast<float>(peakDecay_));
    return out;
}

size_t Chain::tailSamples() const noexcept {
    return std::max({filter_.tailSamples(), drive_.tailSamples(), modulation_.tailSamples(), delay_.tailSamples(),
                     reverb_.tailSamples(), width_.tailSamples()});
}
} // namespace fxblock
