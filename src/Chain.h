#pragma once
#include "Blocks.h"

namespace fxblock {
// What the editor shows of the signal.
struct Status {
    float inputPeak = 0, outputPeak = 0;      // roughly 0..1
    double tempo = 0;                          // the host's tempo in beats per minute, 0 until it has said (filled in by the plugin)
};

// Input trim, then Filter, Drive, Modulation, Delay, Reverb and Width in that order (the filter can move to after
// the drive), then optionally a gain that matches the loudness to the dry's, then the Mix against the dry signal,
// Output and Bypass. A block that is off costs nothing, and with
// every block off, Mix at 100% and the gains at 0 dB the signal passes untouched.
class Chain {
public:
    void prepare(double rate);
    void reset() noexcept;
    void set(const Values& values, bool immediate = false) noexcept;
    Stereo process(Stereo in) noexcept;
    // Samples the effects keep sounding after the input stops, for the settings now in force.
    size_t tailSamples() const noexcept;
    // The host's tempo, which a synced delay follows.
    void setTempo(double bpm) noexcept { delay_.fx.setTempo(bpm); }
    Status status() const noexcept { Status s; s.inputPeak = inputPeak_; s.outputPeak = outputPeak_; return s; }
private:
    double rate_ = 48000, smooth_ = .001, peakDecay_ = .9998;
    double inputGoal_ = 1, outputGoal_ = 1, mixGoal_ = 1, bypassGoal_ = 0;
    double inputNow_ = 1, outputNow_ = 1, mixNow_ = 1, bypassNow_ = 0;
    // Auto gain: slow power averages of the dry signal and of what the chain makes of it, and the gain that equals them.
    bool autoOn_ = false;
    double dryPower_ = 0, wetPower_ = 0, autoGoal_ = 1, autoNow_ = 1, powerCoef_ = .0001, autoCoef_ = .0001, autoWeight_ = 0, autoWeightCoef_ = .001;
    bool filterAfter_ = false;
    float inputPeak_ = 0, outputPeak_ = 0;
    Stage<FilterFx> filter_;
    Stage<DriveFx> drive_;
    Stage<ModulationFx> modulation_;
    Stage<DelayFx> delay_;
    Stage<ReverbFx> reverb_;
    Stage<WidthFx> width_;
};
} // namespace fxblock
