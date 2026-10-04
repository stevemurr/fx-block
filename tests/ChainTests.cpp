#include "Signals.h"
#include <chrono>

// Everything off, Mix 100% and no gain: the signal passes untouched, bit for bit.
void neutralIsTransparent() {
    Chain c = make(defaults());
    Rng rng;
    for (int i = 0; i < 50000; ++i) {
        const Stereo in {rng.bipolar(), rng.bipolar()};
        const Stereo out = c.process(in);
        CHECK(out.l == in.l && out.r == in.r);
    }
    CHECK(c.tailSamples() == 0);
}

Values everything() {
    Values v = defaults();
    for (uint32_t b = 0; b < BlockCount; ++b) v[P(b, On)] = 1;
    return v;
}

void everythingOnIsStable() {
    for (double rate : {22050., 44100., 48000., 96000., 192000.}) for (uint32_t flavor = 0; flavor < 3; ++flavor) {
        Values v = everything();
        for (uint32_t b = 0; b < BlockCount; ++b) v[P(b, Flavor)] = flavor;
        // a harsh setting: high drive and resonance, long decays and feedback
        v[P(FilterBlock, fxblock::P2)] = 100; v[P(DriveBlock, fxblock::P1)] = 100; v[P(ModBlock, fxblock::P3)] = 95;
        v[P(DelayBlock, fxblock::P2)] = 90; v[P(ReverbBlock, fxblock::P1)] = 100;
        Chain c = make(v, rate);
        const auto in = noise(static_cast<size_t>(rate*2), .5f, flavor+1);
        float peak = 0;
        for (float x : in) { const auto y = c.process({x, -.7f*x}); CHECK(std::isfinite(y.l) && std::isfinite(y.r)); peak = std::max(peak, std::max(std::abs(y.l), std::abs(y.r))); }
        CHECK(peak < 60);
        CHECK(c.tailSamples() > 0);
    }
}

void gainsMixAndBypass() {
    // Input and Output are plain gains on an otherwise empty chain.
    const auto in = noise(4800);
    auto level = [&](Values v) { Chain c = make(v); std::vector<float> y; for (float x : in) y.push_back(c.process({x, x}).l); return rms(y, 2000, y.size())/rms(in, 2000, in.size()); };
    CHECK(std::abs(db(level(settings({{Input, 6}})))-6) < .01 && std::abs(db(level(settings({{Output, -9}})))+9) < .01);
    // With Mix at 0 the dry comes through whatever the chain is doing, and the input trim does not touch it.
    Values wet = everything(); wet[Mix] = 0; wet[Input] = 12; wet[Output] = 0;
    Chain dry = make(wet);
    for (float x : in) { const auto y = dry.process({x, -x}); CHECK(y.l == x && y.r == -x); }
    // Bypass is the dry too, and takes Output with it.
    Values bypassed = everything(); bypassed[Bypass] = 1; bypassed[Output] = 12;
    Chain b = make(bypassed);
    for (float x : in) { const auto y = b.process({x, .5f*x}); CHECK(y.l == x && y.r == .5f*x); }
    // Toggling bypass crossfades: no jump.
    Chain t = make(everything());
    float previous = 0; double worst = 0;
    for (size_t i = 0; i < 48000; ++i) {
        if (i % 12000 == 6000) { Values v = everything(); v[Bypass] = (i/12000) % 2 == 0 ? 1 : 0; t.set(v); }
        const float x = static_cast<float>(.3*std::sin(2*pi*110*static_cast<double>(i)/48000));
        const float y = t.process({x, x}).l;
        if (i > 1000) worst = std::max(worst, static_cast<double>(std::abs(y-previous)));
        previous = y;
    }
    CHECK(worst < .1);
    // A half mix is the average of the dry and the wet, in amplitude.
    Values half = oneBlock(DriveBlock, 0, 100, 100, 0, 100); half[Mix] = 50;
    Values full = oneBlock(DriveBlock, 0, 100, 100, 0, 100);
    Chain h = make(half), f = make(full);
    for (size_t i = 0; i < 4800; ++i) {
        const float x = static_cast<float>(.5*std::sin(2*pi*300*static_cast<double>(i)/48000));
        const float yh = h.process({x, x}).l, yf = f.process({x, x}).l;
        CHECK(std::abs(yh-.5f*(x+yf)) < 1e-4f);
    }
}

// The filter can sit before or after the drive: after it, it tames the fizz the drive makes.
void filterOrderMatters() {
    auto highShare = [&](double after) {
        Values v = oneBlock(DriveBlock, 0, 90, 100, 0, 100);
        v[P(FilterBlock, On)] = 1; v[P(FilterBlock, Flavor)] = 0; v[P(FilterBlock, fxblock::P1)] = 2000; v[P(FilterBlock, fxblock::P2)] = 0; v[FilterOrder] = after;
        Chain c = make(v);
        const auto y = renderSine(c, 700, .3, 1.);
        constexpr size_t n = 32768;
        const auto s = spectrum(y, 8000, n);
        double high = 0, all = 0;
        for (size_t i = 1; i < s.size(); ++i) { const double hz = static_cast<double>(i)*48000./n; all += s[i]*s[i]; if (hz > 6000) high += s[i]*s[i]; }
        return high/all;
    };
    const double before = highShare(0), after = highShare(1);
    std::printf("filter order: share of energy above 6 kHz %.5f with the filter first, %.5f after the drive\n", before, after);
    CHECK(after < before*.3);
}

void tailsFollowTheSettings() {
    CHECK(make(everything()).tailSamples() > 48000);
    Values v = defaults(); v[P(ReverbBlock, On)] = 1; v[P(ReverbBlock, Flavor)] = 1; v[P(ReverbBlock, fxblock::P1)] = 50; v[P(ReverbBlock, fxblock::P3)] = 0;
    const size_t hall = make(v).tailSamples();
    v[P(ReverbBlock, Flavor)] = 0;
    const size_t room = make(v).tailSamples();
    CHECK(hall > room && hall > static_cast<size_t>(3.4*48000) && room < static_cast<size_t>(1.*48000));
    v[P(ReverbBlock, On)] = 0;
    CHECK(make(v).tailSamples() == 0);
    // After the input stops, a delay really does fall silent inside the tail it reported.
    Values d = oneBlock(DelayBlock, 1, 250, 60, 60, 80);
    Chain c = make(d);
    const size_t tail = c.tailSamples();
    for (size_t i = 0; i < 2000; ++i) c.process({.5f, .5f});
    float last = 0;
    for (size_t i = 0; i < tail+48000; ++i) { const auto y = c.process({}); if (i > tail) last = std::max(last, std::abs(y.l)); }
    CHECK(last < 2e-3f);
}

// reset() clears every tail; the same input then gives the same output as a fresh chain.
void resetAndDeterminism() {
    Values v = everything();
    Chain a = make(v), b = make(v);
    const auto in = noise(20000, .4f, 3);
    std::vector<Stereo> first;
    for (float x : in) first.push_back(a.process({x, .5f*x}));
    for (size_t i = 0; i < in.size(); ++i) { const auto y = b.process({in[i], .5f*in[i]}); CHECK(y.l == first[i].l && y.r == first[i].r); }
    // After reset() it is a fresh chain again: same input, same output, to the bit.
    a.reset(); a.set(v, true);
    Chain fresh = make(v);
    for (size_t i = 0; i < in.size(); ++i) { const auto y = a.process({in[i], .5f*in[i]}), z = fresh.process({in[i], .5f*in[i]}); CHECK(y.l == z.l && y.r == z.r); }
    // and a reset chain has no tail left to ring: silence in, silence out.
    a.reset(); a.set(v, true);
    for (size_t i = 0; i < 5000; ++i) { const auto y = a.process({}); CHECK(y.l == 0 && y.r == 0); }
}

// Silence through feedback paths must not slow down (denormals) and a switched-off block costs nothing.
void speed() {
    using clock = std::chrono::steady_clock;
    auto time = [&](Chain& c, bool silent) {
        const auto in = noise(48000, .3f, 9);
        double best = 1e9;                                          // the fastest of ten: a busy machine only makes passes slower
        for (int pass = 0; pass < 10; ++pass) {
            const auto start = clock::now();
            for (float x : in) { volatile auto y = c.process(silent ? Stereo {} : Stereo {x, x}); (void)y; }
            best = std::min(best, std::chrono::duration<double>(clock::now()-start).count());
        }
        return best;
    };
    Chain all = make(everything());
    const double busy = time(all, false);
    for (int i = 0; i < 48000*3; ++i) all.process({});            // let the tails die down to denormal territory
    const double quiet = time(all, true);
    Chain off = make(defaults());
    const double idle = time(off, false);
    std::printf("speed: one second of stereo audio takes %.1f ms with every block on (%.0fx real time), %.1f ms of silence after the tails, %.2f ms with everything off\n",
                busy*1000, 1/busy, quiet*1000, idle*1000);
    // The real-time floor is for the build that ships: an unoptimized or sanitized one is slow by design.
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(FXBLOCK_SANITIZED)
    CHECK(1/busy > 20);
#endif
    CHECK(quiet < busy*2.5+.001);                                 // silence through the feedback paths is no slower than sound
    CHECK(idle < busy*.1);                                        // and a box with everything off is nearly free
}

// Auto gain: with it on, the chain comes out as loud as the dry, whatever the blocks do to the level.
void autoGainMatchesTheDry() {
    const double rate = 48000;
    const auto in = noise(static_cast<size_t>(rate*6), .25f, 4);
    auto measure = [&](Values v, bool autoOn) {
        v[AutoGain] = autoOn ? 1 : 0;
        Chain c = make(v);
        // Loudness is the power of both channels together (what the effect matches), the dry being left and 0.8 of it right.
        double wet = 0, dry = 0;
        for (size_t i = 0; i < in.size(); ++i) {
            const auto y = c.process({in[i], .8f*in[i]});
            if (i >= static_cast<size_t>(rate*4)) { wet += static_cast<double>(y.l)*y.l+static_cast<double>(y.r)*y.r; dry += static_cast<double>(in[i])*in[i]*1.64; }
        }
        return 10*std::log10(wet/dry);
    };
    struct Case { const char* name; Values v; };
    const Case cases[] {
        {"lowpass at 300 Hz", oneBlock(FilterBlock, 0, 300, 20, 1, 100)},
        {"highpass at 6 kHz", oneBlock(FilterBlock, 1, 6000, 20, 0, 100)},
        {"overdrive 90%", oneBlock(DriveBlock, 0, 90, 100, 0, 100)},
        {"fuzz, level +9 dB", oneBlock(DriveBlock, 1, 60, 80, 9, 100)},
        {"crush, 4 bits", oneBlock(DriveBlock, 2, 100*(16-4.)/14, 100, 0, 100)},
        {"mono width", oneBlock(WidthBlock, 0, 0, 0, 0, 100)},
        {"hall reverb at full mix", oneBlock(ReverbBlock, 1, 60, 40, 0, 100)},
        {"everything on", everything()},
    };
    for (const Case& test : cases) {
        const double off = measure(test.v, false), on = measure(test.v, true);
        std::printf("auto gain, %-24s: %+.1f dB without, %+.2f dB with\n", test.name, off, on);
        CHECK(std::abs(on) < .75);
    }
    // It does something: a 300 Hz lowpass is far quieter without it.
    CHECK(measure(cases[0].v, false) < -6);
    // Output is still the final level, and Mix still blends the matched signal against the dry.
    Values trimmed = cases[0].v; trimmed[Output] = -6;
    CHECK(std::abs(measure(trimmed, true)+6) < .75);
    // Off, and every block off: bit for bit the dry, and turning it on changes nothing.
    Values flat = defaults(); flat[AutoGain] = 1;
    Chain c = make(flat);
    for (size_t i = 0; i < 48000; ++i) { const Stereo s {in[i], -.5f*in[i]}; const auto y = c.process(s); CHECK(y.l == s.l && y.r == s.r); }
}

// The gain is learned from the dry, so when the input stops it holds, and it is limited to +/-24 dB.
void autoGainHoldsAndIsLimited() {
    const double rate = 48000;
    Values v = oneBlock(DelayBlock, 0, 200, 70, 60, 100); v[AutoGain] = 1;
    Chain c = make(v);
    const auto in = noise(static_cast<size_t>(rate*3), .3f, 8);
    for (float x : in) c.process({x, x});
    // Input stops: the repeats die away as the delay's feedback says, and the gain does not boost them as the dry fades.
    std::vector<float> after;
    for (size_t i = 0; i < static_cast<size_t>(rate*2); ++i) after.push_back(c.process({}).l);
    float peak = 0;
    for (float x : after) { CHECK(std::isfinite(x)); peak = std::max(peak, std::abs(x)); }
    CHECK(peak < 2.0f);
    CHECK(rms(after, after.size()-4800, after.size()) < rms(after, 0, 4800)*.2);
    // A wet far quieter than 24 dB below the dry is not made up beyond +24 dB.
    Values quiet = oneBlock(FilterBlock, 0, 300, 20, 1, 100);                   // -17.6 dB on this noise...
    quiet[P(DriveBlock, On)] = 1; quiet[P(DriveBlock, Flavor)] = 2; quiet[P(DriveBlock, fxblock::P1)] = 0; quiet[P(DriveBlock, fxblock::P2)] = 100;
    quiet[P(DriveBlock, fxblock::P3)] = -24;                                    // ...and another -24 dB: about -42 dB in all
    quiet[AutoGain] = 1;
    Chain q = make(quiet);
    std::vector<float> y;
    for (float x : in) y.push_back(q.process({x, x}).l);
    const double level = db(rms(y, static_cast<size_t>(rate*2), y.size())/rms(in, static_cast<size_t>(rate*2), in.size()));
    std::printf("auto gain: a wet about 42 dB below the dry comes back to %+.1f dB (the gain is limited to +24 dB)\n", level);
    CHECK(level < -13 && level > -22);
    // Switching it on and off glides: no jump.
    Values flip = oneBlock(DriveBlock, 0, 100, 100, 0, 100);
    Chain f = make(flip);
    float previous = 0; double worst = 0;
    for (size_t i = 0; i < 48000*4; ++i) {
        if (i % 24000 == 12000) { flip[AutoGain] = (i/24000) % 2 == 0 ? 1 : 0; f.set(flip); }
        const float x = static_cast<float>(.2*std::sin(2*pi*110*static_cast<double>(i)/48000));
        const float s = f.process({x, x}).l;
        if (i > 1000) worst = std::max(worst, static_cast<double>(std::abs(s-previous)));
        previous = s;
    }
    CHECK(worst < .1);
}

int main() {
    neutralIsTransparent(); everythingOnIsStable(); gainsMixAndBypass(); filterOrderMatters(); tailsFollowTheSettings(); resetAndDeterminism(); autoGainMatchesTheDry(); autoGainHoldsAndIsLimited(); speed();
    std::cout << "Chain: transparency, stability at five sample rates, gain/mix/bypass, filter order, tails, reset and speed passed\n";
}
