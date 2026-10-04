#pragma once
#include "Dsp.h"
#include "Parameters.h"

namespace fxblock {
// One block's settings as stored (P1..P3 in the units of Parameters.h: Hz, %, ms, dB...); the flavor decides what
// they mean.
struct BlockSettings {
    bool on = false;
    uint32_t flavor = 0;
    double p1 = 0, p2 = 0, p3 = 0;
    double mix = 1;                  // 0..1
    uint32_t sync = 0;               // the delay's note value (see syncBeats); 0 is free
};

// ---------------------------------------------------------------- the six effects
// Each takes a stereo sample and returns the processed ("wet") sample. `additive` effects (delay, reverb) return only
// what they add, and the stage puts the dry back in at full level; the rest return the whole signal and the stage
// crossfades. `ducks` effects can not change flavor smoothly, so the stage fades them out, switches, and fades back.

class FilterFx {
public:
    static constexpr bool additive = false, ducks = false;
    void prepare(double rate);
    void reset() noexcept;
    void set(const BlockSettings& s, bool immediate) noexcept;
    uint32_t flavor() const noexcept { return flavor_; }
    void setFlavor(uint32_t) noexcept {}
    Stereo process(Stereo in) noexcept;
    size_t tailSamples() const noexcept { return 0; }
private:
    void update() noexcept;
    double rate_ = 48000, smooth_ = .001, morph_ = .002;
    double resonance_ = .25, slope_ = 0;                                  // targets
    double logCutoff_ = std::log(1000.), logGoal_ = std::log(1000.), resonanceNow_ = .25, slopeNow_ = 0;
    double lastLog_ = -1, lastResonance_ = -1, lastSlope_ = -1;
    double weight_[3] {1, 0, 0}, goal_[3] {1, 0, 0};                      // lowpass, bandpass, highpass
    uint32_t flavor_ = 0;
    Svf stage_[2][2];                                                     // [channel][stage]
};

class DriveFx {
public:
    static constexpr bool additive = false, ducks = true;
    void prepare(double rate);
    void reset() noexcept;
    void set(const BlockSettings& s, bool immediate) noexcept;
    uint32_t flavor() const noexcept { return flavor_; }
    void setFlavor(uint32_t flavor) noexcept { flavor_ = flavor; retarget(); reset(); }
    Stereo process(Stereo in) noexcept;
    size_t tailSamples() const noexcept { return 0; }
private:
    void retarget() noexcept;
    static double clip(uint32_t flavor, double v) noexcept;                    // the curve
    static double antiderivative(uint32_t flavor, double v) noexcept;          // its integral, for anti-aliasing
    double clipSmooth(size_t channel, double v) noexcept;                      // the curve with first-order antiderivative anti-aliasing
    double compensation(double gain) const noexcept;
    static constexpr int compensationSteps = 57;                               // gain from 0 to 70 dB in 1.25 dB steps
    double rate_ = 48000, smooth_ = .001;
    uint32_t flavor_ = 0;
    BlockSettings target_;
    std::array<std::array<double, compensationSteps>, 2> compensation_ {};      // per clipper: what puts the loudness back
    double gainGoal_ = 1, levelGoal_ = 1, toneGoal_ = .5, bitsGoal_ = 16, divisorGoal_ = 1;
    double gain_ = 1, level_ = 1, tone_ = .5, bits_ = 16, divisor_ = 1;        // smoothed copies
    double overdrivePre_ = .1, fuzzPre_ = .01, dcR_ = .999;
    std::array<Oversampler, 2> over_;
    std::array<OnePole, 2> pre_, tone1_, tone2_;
    std::array<DcBlock, 2> dc_;
    std::array<float, 2> held_ {};
    std::array<double, 2> previous_ {};                                        // last input to the clipper, per channel
    double phase_ = 1;
};

class ModulationFx {
public:
    static constexpr bool additive = false, ducks = true;
    void prepare(double rate);
    void reset() noexcept;
    void set(const BlockSettings& s, bool immediate) noexcept;
    uint32_t flavor() const noexcept { return flavor_; }
    void setFlavor(uint32_t flavor) noexcept { flavor_ = flavor; clearState(); }
    Stereo process(Stereo in) noexcept;
    size_t tailSamples() const noexcept;
private:
    void clearState() noexcept;
    static constexpr int stages = 6;
    double rate_ = 48000;
    uint32_t flavor_ = 0;
    BlockSettings target_;
    double lfo_ = 0;
    std::array<Line, 2> line_;
    std::array<float, 2> feedback_ {};
    std::array<std::array<float, stages>, 2> allpass_ {};
};

class DelayFx {
public:
    static constexpr bool additive = true, ducks = true;
    void prepare(double rate);
    void reset() noexcept;
    void set(const BlockSettings& s, bool immediate) noexcept;
    uint32_t flavor() const noexcept { return flavor_; }
    void setFlavor(uint32_t flavor) noexcept { flavor_ = flavor; clearState(); retarget(); tone_ = toneGoal_; }
    // The host's tempo, for a synced delay. The first one heard is taken at once; after that the time glides.
    void setTempo(double bpm) noexcept;
    Stereo process(Stereo in) noexcept;
    size_t tailSamples() const noexcept;
    static constexpr double maxMs = 4000;
private:
    void clearState() noexcept;
    void retarget() noexcept;
    double timeMs() const noexcept;                                         // the Time knob, or the note value at the tempo
    double rate_ = 48000, timeSmooth_ = .0005, smooth_ = .001;
    double tempo_ = 120;
    bool tempoSeen_ = false;
    uint32_t flavor_ = 0;
    BlockSettings target_;
    double ms_ = 300, tone_ = .3, toneGoal_ = .3;                          // the tone is kept as a filter coefficient
    std::array<Line, 2> line_;
    std::array<OnePole, 2> lowpass_, highpass_;
    double tapePhase_[2] {0, 0}, wowPhase_ = 0;
};

class ReverbFx {
public:
    static constexpr bool additive = true, ducks = true;
    static constexpr int lines = 8, diffusers = 4;
    void prepare(double rate);
    void reset() noexcept;
    void set(const BlockSettings& s, bool immediate) noexcept;
    uint32_t flavor() const noexcept { return flavor_; }
    void setFlavor(uint32_t flavor) noexcept;
    Stereo process(Stereo in) noexcept;
    size_t tailSamples() const noexcept;
private:
    struct Allpass {
        Line line; double delay = 100;
        float process(float x, float g) noexcept { const float d = line.readLinear(delay); const float v = x+g*d; line.push(v); return d-g*v; }
    };
    void clearState() noexcept;
    void tune() noexcept;
    double rate_ = 48000, smooth_ = .001;
    uint32_t flavor_ = 0;
    BlockSettings target_;
    double preMs_ = 0;
    Line pre_;
    std::array<Allpass, diffusers> diffuser_;
    std::array<Line, lines> tank_;
    std::array<double, lines> length_ {}, modPhase_ {}, modRate_ {};
    std::array<float, lines> damp_ {}, gain_ {};
    double dampCoef_ = .3, modDepth_ = 4, diffuserGain_ = .6;
    int activeDiffusers_ = 2;
};

class WidthFx {
public:
    static constexpr bool additive = false, ducks = true;
    void prepare(double rate);
    void reset() noexcept;
    void set(const BlockSettings& s, bool immediate) noexcept;
    uint32_t flavor() const noexcept { return flavor_; }
    void setFlavor(uint32_t flavor) noexcept { flavor_ = flavor; clearState(); retarget(); snap(); }
    Stereo process(Stereo in) noexcept;
    size_t tailSamples() const noexcept { return 0; }
private:
    struct Schroeder {
        Line line; double delay = 10;
        float process(float x, float g) noexcept { const float d = line.readLinear(delay); const float v = x+g*d; line.push(v); return d-g*v; }
    };
    void clearState() noexcept;
    void retarget() noexcept;
    void snap() noexcept { width_ = widthGoal_; haas_ = haasGoal_; spread_ = spreadGoal_; bass_ = bassGoal_; bassCutoff_ = cutoffGoal_; bassFilter_.set(bassCutoff_, .7071, rate_); }
    static constexpr int sections = 4;
    double rate_ = 48000, smooth_ = .001;
    uint32_t flavor_ = 0;
    BlockSettings target_;
    double widthGoal_ = 1, haasGoal_ = 0, spreadGoal_ = 0, bassGoal_ = 0, cutoffGoal_ = 120;     // what the knobs ask for in this flavor
    double width_ = 1, haas_ = 0, spread_ = 0, bass_ = 0, bassCutoff_ = 120;                    // smoothed copies
    Line haasLine_;
    std::array<std::array<Schroeder, sections>, 2> smear_;
    Svf bassFilter_;
};

// ---------------------------------------------------------------- the stage around an effect
// Fades the effect in and out as its switch moves (so toggling never clicks), ducks it through a flavor change,
// and applies the mix. A switched-off stage costs nothing: it passes the signal and keeps its effect cleared.
template <class Fx>
class Stage {
public:
    Fx fx;
    void prepare(double rate) {
        fx.prepare(rate);
        onCoef_ = timeCoefficient(.012, rate); duckCoef_ = timeCoefficient(.004, rate); mixCoef_ = timeCoefficient(.02, rate);
        applied_ = false; reset();
    }
    void reset() noexcept {
        fx.reset();
        active_ = target_.on ? 1 : 0; duck_ = 1; mix_ = target_.mix;
        running_ = target_.on;
    }
    void set(const BlockSettings& s, bool immediate) noexcept {
        const bool wasOn = target_.on;
        target_ = s;
        fx.set(s, immediate);
        if (immediate || !applied_) {
            applied_ = true;
            if constexpr (Fx::ducks) if (fx.flavor() != s.flavor) fx.setFlavor(s.flavor);
            active_ = s.on ? 1 : 0; mix_ = s.mix; duck_ = 1; running_ = s.on;
        } else if (s.on && !wasOn && !running_) {
            // Starting from silence: a flavor chosen while off is applied now, to a clear effect.
            if constexpr (Fx::ducks) if (fx.flavor() != s.flavor) fx.setFlavor(s.flavor);
        }
    }
    Stereo process(Stereo in) noexcept {
        follow(active_, target_.on ? 1. : 0., onCoef_);
        if (!target_.on && active_ <= 0) {
            if (running_) { fx.reset(); running_ = false; }
            return in;
        }
        running_ = true;
        follow(mix_, target_.mix, mixCoef_);
        double duckGoal = 1;
        if constexpr (Fx::ducks) {
            if (fx.flavor() != target_.flavor) {
                duckGoal = 0;
                if (duck_ < .002) { fx.setFlavor(target_.flavor); duck_ = .002; }
            }
        }
        follow(duck_, duckGoal, duckCoef_);
        const Stereo wet = fx.process(in);
        const float k = static_cast<float>(mix_*duck_*active_);
        if constexpr (Fx::additive) return {in.l+k*wet.l, in.r+k*wet.r};
        else return {in.l+k*(wet.l-in.l), in.r+k*(wet.r-in.r)};
    }
    size_t tailSamples() const noexcept { return target_.on ? fx.tailSamples() : 0; }
    bool busy() const noexcept { return running_; }
private:
    BlockSettings target_;
    double active_ = 0, duck_ = 1, mix_ = 1, onCoef_ = .002, duckCoef_ = .005, mixCoef_ = .001;
    bool running_ = false, applied_ = false;
};
} // namespace fxblock
