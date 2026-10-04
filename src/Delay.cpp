#include "Blocks.h"

namespace fxblock {
void DelayFx::prepare(double rate) {
    rate_ = rate;
    smooth_ = timeCoefficient(.02, rate);
    timeSmooth_ = timeCoefficient(.05, rate);
    for (auto& l : line_) l.prepare(static_cast<size_t>(rate*(maxMs*.001*1.02))+16);
    retarget();
    reset();
}

void DelayFx::clearState() noexcept {
    for (auto& l : line_) l.clear();
    for (auto& f : lowpass_) f = {};
    for (auto& f : highpass_) f = {};
}

void DelayFx::reset() noexcept {
    clearState();
    wowPhase_ = tapePhase_[0] = tapePhase_[1] = 0;
    ms_ = timeMs();
    tone_ = toneGoal_;
}

void DelayFx::set(const BlockSettings& s, bool immediate) noexcept {
    target_ = s;
    retarget();
    if (immediate) { ms_ = timeMs(); tone_ = toneGoal_; }
}

// Free: the Time knob. Synced: the note value at the tempo, held between 20 ms and 4 s (a slow tempo with a long note
// would not fit in the line, and a fast one with a short note is not a delay).
double DelayFx::timeMs() const noexcept {
    if (target_.sync == 0 || target_.sync >= syncCount) return std::clamp(target_.p1, 20., 1000.);
    return std::clamp(syncBeats[target_.sync]*60000./tempo_, 20., maxMs);
}

void DelayFx::setTempo(double bpm) noexcept {
    bpm = std::isfinite(bpm) ? std::clamp(bpm, 20., 999.) : 120.;
    tempo_ = bpm;
    if (!tempoSeen_) { tempoSeen_ = true; ms_ = timeMs(); }
}

// The tone knob is the cutoff of the filter in the feedback: the repeats lose their top as they fade, a tape's more so.
void DelayFx::retarget() noexcept {
    const double t = std::clamp(target_.p3, 0., 100.)*.01;
    const double hz = flavor_ == 2 ? 300*std::pow(20., t) : 400*std::pow(30., t);
    toneGoal_ = lowpassCoefficient(hz, rate_);
}

size_t DelayFx::tailSamples() const noexcept {
    const double fb = std::clamp(target_.p2, 0., 90.)*.01;
    const double repeats = fb < .01 ? 1. : std::min(60., std::ceil(std::log(.001)/std::log(fb)));
    return static_cast<size_t>(std::min(10., timeMs()*.001*repeats)*rate_);
}

Stereo DelayFx::process(Stereo in) noexcept {
    follow(ms_, timeMs(), timeSmooth_);
    follow(tone_, toneGoal_, smooth_);
    const double d = std::max(3., ms_*rate_/1000.);
    const float fb = static_cast<float>(std::clamp(target_.p2, 0., 90.)*.01);
    const float tone = static_cast<float>(tone_);
    const float input[2] {in.l, in.r};
    float wet[2];
    if (flavor_ == 2) {
        // Tape: the delay time drifts (a slow wow and a fast flutter, a little different in each ear), and the loop
        // thins out below 120 Hz and saturates, so each repeat is darker, looser and rounder than the last.
        wowPhase_ += .55/rate_; wowPhase_ -= std::floor(wowPhase_);
        for (size_t c = 0; c < 2; ++c) {
            tapePhase_[c] += (6.3+.4*static_cast<double>(c))/rate_; tapePhase_[c] -= std::floor(tapePhase_[c]);
            const double wow = std::sin(2*pi*(wowPhase_+.3*static_cast<double>(c)));
            const double time = d*(1+.0035*wow+.0006*std::sin(2*pi*tapePhase_[c]));
            wet[c] = line_[c].read(time);
            float f = lowpass_[c].lowpass(wet[c], tone);
            f = highpass_[c].highpass(f, static_cast<float>(lowpassCoefficient(120, rate_)));
            f = std::tanh(1.3f*f)*(1/1.3f);
            line_[c].push(tiny(clean(input[c]+fb*f)));
        }
    } else if (flavor_ == 1) {
        // Ping-pong: the input goes into the left line; each line feeds the other.
        wet[0] = line_[0].read(d); wet[1] = line_[1].read(d);
        const float l = lowpass_[0].lowpass(wet[0], tone), r = lowpass_[1].lowpass(wet[1], tone);
        line_[0].push(tiny(clean(.5f*(input[0]+input[1])+fb*r)));
        line_[1].push(tiny(clean(fb*l)));
    } else {
        // Digital: each side repeats itself.
        for (size_t c = 0; c < 2; ++c) {
            wet[c] = line_[c].read(d);
            line_[c].push(tiny(clean(input[c]+fb*lowpass_[c].lowpass(wet[c], tone))));
        }
    }
    return {wet[0], wet[1]};
}
} // namespace fxblock
