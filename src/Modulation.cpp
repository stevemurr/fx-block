#include "Blocks.h"

namespace fxblock {
void ModulationFx::prepare(double rate) {
    rate_ = rate;
    for (auto& l : line_) l.prepare(static_cast<size_t>(rate*.03)+8);
    reset();
}

void ModulationFx::clearState() noexcept {
    for (auto& l : line_) l.clear();
    feedback_.fill(0.f);
    for (auto& a : allpass_) a.fill(0.f);
}

void ModulationFx::reset() noexcept { clearState(); lfo_ = 0; }

void ModulationFx::set(const BlockSettings& s, bool) noexcept { target_ = s; }

size_t ModulationFx::tailSamples() const noexcept {
    const double fb = std::clamp(target_.p3, 0., 95.)*.01;
    return static_cast<size_t>((flavor_ == 0 ? .05 : .15+.85*fb)*rate_);
}

Stereo ModulationFx::process(Stereo in) noexcept {
    lfo_ += std::clamp(target_.p1, .05, 10.)/rate_;
    lfo_ -= std::floor(lfo_);
    const double depth = std::clamp(target_.p2, 0., 100.)*.01;
    const float fb = static_cast<float>(std::clamp(target_.p3, 0., 95.)*.01);
    const float input[2] {in.l, in.r};
    float out[2];
    for (size_t c = 0; c < 2; ++c) {
        const double phase = lfo_+(c ? .25 : 0.);                  // the right channel a quarter of a cycle behind
        float wet;
        if (flavor_ == 0) {
            // Chorus: two taps around 10 and 15 ms, half a cycle apart, up to +/-6 ms.
            line_[c].push(input[c]+.4f*fb*feedback_[c]);
            wet = 0;
            for (int t = 0; t < 2; ++t) {
                const double ms = (t ? 15. : 10.)+depth*6.*std::sin(2*pi*(phase+(t ? .5 : 0.)));
                wet += .5f*line_[c].read(ms*rate_/1000.);
            }
        } else if (flavor_ == 1) {
            // Flanger: one short tap sweeping exponentially, up to +/-2.75 octaves around 1.5 ms (0.2 to 10 ms).
            const double ms = 1.5*std::exp2(std::sin(2*pi*phase)*.5*5.5*depth);
            line_[c].push(tiny(input[c]+.88f*fb*feedback_[c]));
            wet = line_[c].read(ms*rate_/1000.);
        } else {
            // Phaser: six first-order allpass stages swept together, +/-2 octaves around 1 kHz at full depth.
            const double f = 1000.*std::exp2(std::sin(2*pi*phase)*.5*4.*depth);
            const double t = std::tan(pi*std::min(f, .45*rate_)/rate_);
            const float a = static_cast<float>((t-1)/(t+1));
            float u = input[c]+.8f*fb*feedback_[c];
            for (int s = 0; s < stages; ++s) {
                const float y = a*u+allpass_[c][static_cast<size_t>(s)];
                allpass_[c][static_cast<size_t>(s)] = tiny(u-a*y);
                u = y;
            }
            wet = u;
        }
        feedback_[c] = tiny(clean(wet));
        out[c] = wet;
    }
    return {out[0], out[1]};
}
} // namespace fxblock
