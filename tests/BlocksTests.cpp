#include "Signals.h"

// ---------------------------------------------------------------- filter
void filterFlavorsAndSlopes() {
    for (uint32_t slope : {0u, 1u}) {
        const double octaves = 3.32;                                                   // 1 kHz to 10 kHz, and 100 Hz to 1 kHz
        const auto lowpass = oneBlock(FilterBlock, 0, 1000, 0, slope, 100);
        const auto highpass = oneBlock(FilterBlock, 1, 1000, 0, slope, 100);
        const auto bandpass = oneBlock(FilterBlock, 2, 1000, 0, slope, 100);
        const double expectedSlope = slope ? 24. : 12.;
        const double lpLow = gainDb(lowpass, 100), lpCut = gainDb(lowpass, 1000), lpHigh = gainDb(lowpass, 10000);
        const double hpLow = gainDb(highpass, 100), hpCut = gainDb(highpass, 1000), hpHigh = gainDb(highpass, 10000);
        const double bpLow = gainDb(bandpass, 100), bpCut = gainDb(bandpass, 1000), bpHigh = gainDb(bandpass, 10000);
        std::printf("filter %2.0f dB: lowpass %.1f / %.1f / %.1f dB, highpass %.1f / %.1f / %.1f dB, bandpass %.1f / %.1f / %.1f dB at 100 Hz / 1 kHz / 10 kHz\n",
                    expectedSlope, lpLow, lpCut, lpHigh, hpLow, hpCut, hpHigh, bpLow, bpCut, bpHigh);
        CHECK(std::abs(lpLow) < 1 && lpCut < -2 && lpCut > -5 && lpHigh < -expectedSlope*octaves*.85);
        CHECK(std::abs(hpHigh) < 1 && hpCut < -2 && hpCut > -5 && hpLow < -expectedSlope*octaves*.85);
        CHECK(std::abs(bpCut) < 1.5 && bpLow < -8 && bpHigh < -8);
    }
}

void filterResonanceAndStability() {
    const auto flat = oneBlock(FilterBlock, 0, 2000, 0, 0, 100), peaky = oneBlock(FilterBlock, 0, 2000, 100, 0, 100);
    const double peak = gainDb(peaky, 2000), none = gainDb(flat, 2000);
    std::printf("filter resonance: %.1f dB at the cutoff with resonance 100%%, %.1f dB with none\n", peak, none);
    CHECK(peak > 12 && peak < 24 && none < 0);
    // Extremes of cutoff and resonance on noise stay finite and bounded, in every flavor and slope.
    for (uint32_t flavor = 0; flavor < 3; ++flavor) for (double cutoff : {20., 20000.}) for (double res : {0., 100.}) for (double slope : {0., 1.}) {
        Chain c = make(oneBlock(FilterBlock, flavor, cutoff, res, slope, 100), 44100);
        const auto in = noise(44100);
        float peakOut = 0;
        for (float x : in) { const auto y = c.process({x, -x}); CHECK(std::isfinite(y.l) && std::isfinite(y.r)); peakOut = std::max(peakOut, std::abs(y.l)); }
        CHECK(peakOut < 40);
    }
}

// Switching flavor, or sweeping the cutoff, must not click: no sample jumps far from the one before.
void filterMovesSmoothly() {
    Chain c = make(oneBlock(FilterBlock, 0, 1000, 20, 0, 100));
    double worst = 0; float previous = 0;
    for (size_t i = 0; i < 96000; ++i) {
        if (i == 24000 || i == 48000 || i == 72000) {
            Values v = oneBlock(FilterBlock, static_cast<uint32_t>(i/24000), 1000, 20, 0, 100);
            c.set(v);
        }
        const float x = static_cast<float>(.3*std::sin(2*pi*150*static_cast<double>(i)/48000));
        const float y = c.process({x, x}).l;
        if (i > 100) worst = std::max(worst, static_cast<double>(std::abs(y-previous)));
        previous = y;
    }
    std::printf("filter: largest step between samples across three flavor changes: %.3f (a 150 Hz sine steps at most %.3f)\n", worst, .3*2*pi*150/48000);
    CHECK(worst < .3*2*pi*150/48000*3);
}

// ---------------------------------------------------------------- oversampler
void oversamplerIsClean() {
    const double rate = 48000;
    // Up then down returns the input, delayed a little, flat across the audio band.
    for (double hz : {100., 1000., 5000., 10000., 15000., 19000.}) {
        Oversampler o;
        std::vector<float> y(48000);
        for (size_t i = 0; i < y.size(); ++i) {
            float a, b;
            o.up(static_cast<float>(.5*std::sin(2*pi*hz*static_cast<double>(i)/rate)), a, b);
            y[i] = o.down(a, b);
        }
        const double gain = db(amplitudeAt(y, 24000, 48000, hz, rate)/.5);
        CHECK(std::abs(gain) < .1);
    }
    // Images of a 10 kHz tone at the high rate (96 kHz): 86 kHz and 106 kHz do not exist, but 48-10 = 38 kHz does, and must be 70 dB down.
    Oversampler o;
    std::vector<float> high;
    for (size_t i = 0; i < 48000; ++i) {
        float a, b;
        o.up(static_cast<float>(.5*std::sin(2*pi*10000*static_cast<double>(i)/rate)), a, b);
        high.push_back(a); high.push_back(b);
    }
    const double image = db(amplitudeAt(high, 48000, 96000, 38000, 96000)/.5);
    // A tone above the base Nyquist at the high rate folds back unless the decimator removes it.
    Oversampler d;
    std::vector<float> low;
    for (size_t i = 0; i < 48000; ++i) {
        const float a = static_cast<float>(.5*std::sin(2*pi*30000*static_cast<double>(2*i)/96000)), b = static_cast<float>(.5*std::sin(2*pi*30000*static_cast<double>(2*i+1)/96000));
        low.push_back(d.down(a, b));
    }
    const double folded = db(amplitudeAt(low, 24000, 48000, 18000, rate)/.5);      // 30 kHz folds to 18 kHz
    std::printf("oversampler: image %.1f dB, a 30 kHz tone folds to %.1f dB\n", image, folded);
    CHECK(image < -70 && folded < -70);
}

// ---------------------------------------------------------------- drive
void driveAddsHarmonicsAndKeepsLevel() {
    const double rate = 48000;
    for (uint32_t flavor : {0u, 1u}) {
        double lowest = 1e9, highest = -1e9;
        for (double drive : {0., 25., 50., 75., 100.}) {
            Chain c = make(oneBlock(DriveBlock, flavor, drive, 100, 0, 100));
            const auto y = renderSine(c, 1000, .125, 1.);               // -18 dBFS
            const double level = db(rms(y, 24000, 48000)/(.125/std::sqrt(2.)));
            lowest = std::min(lowest, level); highest = std::max(highest, level);
        }
        std::printf("drive %s: output level %.1f to %.1f dB against the input across Drive 0..100%%\n", flavorNames[DriveBlock][flavor], lowest, highest);
        CHECK(lowest > -9 && highest < 9);
        // The more drive, the more harmonics.
        double previous = -200;
        for (double drive : {10., 40., 80.}) {
            Chain c = make(oneBlock(DriveBlock, flavor, drive, 100, 0, 100));
            const auto y = renderSine(c, 1000, .125, 1.);
            const double fundamental = amplitudeAt(y, 24000, 48000, 1000, rate);
            const double third = db(amplitudeAt(y, 24000, 48000, 3000, rate)/fundamental);
            const double second = db(amplitudeAt(y, 24000, 48000, 2000, rate)/fundamental);
            std::printf("  drive %.0f%%: 2nd harmonic %.1f dBc, 3rd %.1f dBc\n", drive, second, third);
            CHECK(third > previous); previous = third;
        }
        CHECK(previous > -25);
    }
    // Overdrive and fuzz make even harmonics too (a lopsided clip); with Drive 0 and a small signal the overdrive is nearly clean.
    Chain quiet = make(oneBlock(DriveBlock, 0, 0, 100, 0, 100));
    const auto y = renderSine(quiet, 1000, .05, 1.);
    CHECK(db(amplitudeAt(y, 24000, 48000, 3000, rate)/amplitudeAt(y, 24000, 48000, 1000, rate)) < -45);
}

// A hard-driven tone. Its harmonics above 24 kHz would fold back below it as tones that are not harmonics of
// anything; the oversampling and the anti-aliased clippers have to stop them. Everything between 200 Hz and 16 kHz that is not
// within a few bins of a true harmonic is aliasing. (Full drive on a pure tone is the worst case there is: a square
// wave, whose harmonics fall by only 6 dB an octave.)
void driveDoesNotAlias() {
    const double rate = 48000;
    struct Case { double f0, limit; };
    for (uint32_t flavor : {0u, 1u}) for (const Case& test : {Case {1003, -54}, Case {2503, -48}, Case {5003, -42}}) {
        Chain c = make(oneBlock(DriveBlock, flavor, 100, 100, 0, 100));
        const auto y = renderSine(c, test.f0, .3, 1.);
        constexpr size_t n = 32768;
        const auto s = spectrum(y, 8000, n);
        const double binHz = rate/static_cast<double>(n);
        double fundamental = 0, worst = 0, worstAt = 0;
        for (size_t i = 0; i < s.size(); ++i)
            if (std::abs(static_cast<double>(i)*binHz-test.f0) < 4*binHz) fundamental = std::max(fundamental, s[i]);
        for (size_t i = 0; i < s.size(); ++i) {
            const double hz = static_cast<double>(i)*binHz;
            if (hz < 200 || hz > 16000) continue;                       // below the lowest note, and above what is audible as a tone
            double nearest = 1e9;
            for (int k = 1; k*test.f0 < 24000; ++k) nearest = std::min(nearest, std::abs(hz-k*test.f0));
            if (nearest < 12*binHz) continue;
            if (s[i] > worst) { worst = s[i]; worstAt = hz; }
        }
        std::printf("drive %-9s %.0f Hz at full drive: strongest inharmonic component %.1f dBc (at %.0f Hz)\n",
                    flavorNames[DriveBlock][flavor], test.f0, db(worst/fundamental), worstAt);
        CHECK(db(worst/fundamental) < test.limit);
    }
}

void crushQuantizesAndHolds() {
    // Bits: 4 bits is at most 2^4+1 distinct levels on a slow ramp; rate: a 1/8 divisor holds each sample 8 frames.
    Chain bits = make(oneBlock(DriveBlock, 2, 100*(16-4.)/14, 100, 0, 100));
    std::vector<float> seen;
    for (size_t i = 0; i < 48000; ++i) {
        const float x = static_cast<float>(.9*std::sin(2*pi*50*static_cast<double>(i)/48000));
        const float y = bits.process({x, x}).l;
        if (std::find(seen.begin(), seen.end(), y) == seen.end()) seen.push_back(y);
    }
    std::printf("crush: 4 bits gave %zu distinct levels\n", seen.size());
    CHECK(seen.size() <= 17 && seen.size() >= 12);
    Chain rate = make(oneBlock(DriveBlock, 2, 0, 100*(1-std::log(8.)/std::log(32.)), 0, 100));
    std::vector<float> out;
    for (size_t i = 0; i < 4000; ++i) out.push_back(rate.process({static_cast<float>(i)*1e-4f, 0.f}).l);
    size_t longest = 0, run = 1; size_t runs = 0, total = 0;
    for (size_t i = 1; i < out.size(); ++i) { if (out[i] == out[i-1]) ++run; else { longest = std::max(longest, run); total += run; ++runs; run = 1; } }
    std::printf("crush: a divisor of 8 holds each sample %.2f frames (longest %zu)\n", static_cast<double>(total)/static_cast<double>(runs), longest);
    CHECK(std::abs(static_cast<double>(total)/static_cast<double>(runs)-8) < .6);
    // Full resolution and full rate is the input to within a rounding error.
    Chain clean = make(oneBlock(DriveBlock, 2, 0, 100, 0, 100));
    double worst = 0;
    for (size_t i = 0; i < 4800; ++i) { const float x = static_cast<float>(.5*std::sin(2*pi*440*static_cast<double>(i)/48000)); worst = std::max(worst, static_cast<double>(std::abs(clean.process({x, x}).l-x))); }
    CHECK(worst < 1e-4);
}

// ---------------------------------------------------------------- modulation
void modulationMoves() {
    const double rate = 48000;
    // At 50% mix the sweep moves the comb across the tone, so its level flutters; with no depth it stays put.
    for (uint32_t flavor = 0; flavor < 3; ++flavor) {
        auto flutter = [&](double depth) {
            Chain c = make(oneBlock(ModBlock, flavor, 1.5, depth, 0, 50));
            const auto y = renderSine(c, flavor == 2 ? 1200 : 440, .5, 4.);
            double low = 1e9, high = 0;
            for (size_t i = static_cast<size_t>(rate); i+480 < y.size(); i += 480) { const double r = rms(y, i, i+480); low = std::min(low, r); high = std::max(high, r); }
            return high/low;
        };
        const double moving = flutter(100), still = flutter(0);
        std::printf("%s: level flutter %.2fx with depth 100%%, %.2fx with none\n", flavorNames[ModBlock][flavor], moving, still);
        CHECK(moving > 1.12 && still < 1.05);
    }
    // Feedback at its maximum stays finite and bounded, in every flavor.
    for (uint32_t flavor = 0; flavor < 3; ++flavor) {
        Chain c = make(oneBlock(ModBlock, flavor, 3, 100, 95, 100));
        float peak = 0;
        for (float x : noise(96000)) { const auto y = c.process({x, x}); CHECK(std::isfinite(y.l)); peak = std::max(peak, std::abs(y.l)); }
        CHECK(peak < 12);
    }
}

// ---------------------------------------------------------------- delay
void delayRepeats() {
    for (double rate : {44100., 48000., 96000.}) {
        const size_t n = static_cast<size_t>(rate*1.0);
        const size_t d = static_cast<size_t>(rate*.1);
        for (uint32_t flavor : {0u, 1u}) {
            Chain c = make(oneBlock(DelayBlock, flavor, 100, 60, 100, 100), rate);
            const auto out = renderImpulse(c, n);
            auto peakNear = [&](size_t center, bool left) {
                float best = 0; size_t at = 0;
                for (size_t i = center-50; i < center+50; ++i) { const float v = std::abs(left ? out[i].l : out[i].r); if (v > best) { best = v; at = i; } }
                return std::pair<float, size_t> {best, at};
            };
            const auto l1 = peakNear(d, true), r1 = peakNear(d, false), l2 = peakNear(2*d, true), r2 = peakNear(2*d, false);
            if (flavor == 0) {
                // Digital: both sides repeat themselves, every echo about 0.6 of the last (less the damping).
                CHECK(std::abs(static_cast<double>(l1.second)-static_cast<double>(d)-1) < 3 && std::abs(static_cast<double>(l2.second)-2.*static_cast<double>(d)-1) < 4);
                CHECK(l1.first > .5 && r1.first > .5 && l2.first < l1.first*.7 && l2.first > l1.first*.3);
            } else {
                // Ping-pong: the first echo is on the left only, the second on the right only.
                CHECK(l1.first > .4 && r1.first < .02 && r2.first > .2 && l2.first < .02);
            }
        }
        // Tape: the first echo lands within the wobble of the set time, and the repeats are darker than the digital's.
        Chain tape = make(oneBlock(DelayBlock, 2, 100, 60, 60, 100), rate);
        const auto t = renderImpulse(tape, n);
        float best = 0; size_t at = 0;
        for (size_t i = 0; i < t.size(); ++i) if (std::abs(t[i].l) > best && i > 100) { best = std::abs(t[i].l); at = i; }
        CHECK(std::abs(static_cast<double>(at)-static_cast<double>(d)) < rate*.0015);
    }
    // Tone: the repeats of a bright burst lose their top as the knob comes down.
    auto brightness = [&](double tone) {
        Chain c = make(oneBlock(DelayBlock, 0, 100, 70, tone, 100));
        std::vector<float> y;
        Rng rng;
        for (size_t i = 0; i < 48000; ++i) y.push_back(c.process(i < 200 ? Stereo {rng.bipolar(), rng.bipolar()} : Stereo {}).l);
        // the third repeat, one pole difference as a measure of high frequencies against the whole
        const size_t a = 3*4800+100, b = a+300;
        double hf = 0, all = 0;
        for (size_t i = a+1; i < b; ++i) { hf += (y[i]-y[i-1])*(y[i]-y[i-1]); all += y[i]*y[i]; }
        return hf/std::max(all, 1e-30);
    };
    const double dark = brightness(0), bright = brightness(100);
    std::printf("delay tone: high-frequency share of the third repeat %.3f at 0%% against %.3f at 100%%\n", dark, bright);
    CHECK(dark < bright*.5);
}

// ---------------------------------------------------------------- reverb
// Reverberation time from the impulse response by backward integration (the Schroeder curve), fitted from -5 to -35 dB
// and extended to -60.
double measureRt60(std::vector<Stereo> ir, double rate, double centerHz = 0) {
    ir[0] = {};                                              // the dry impulse is not part of the reverb
    if (centerHz > 0) {                                      // the decay of a band, as reverberation times are quoted
        Svf band[2];
        for (auto& b : band) b.set(centerHz, 1.0, rate);
        for (auto& s : ir) { s.l = band[0].process(s.l).bp*static_cast<float>(band[0].k); s.r = band[1].process(s.r).bp*static_cast<float>(band[1].k); }
    }
    std::vector<double> energy(ir.size());
    double sum = 0;
    for (size_t i = ir.size(); i-- > 0;) { sum += static_cast<double>(ir[i].l)*ir[i].l+static_cast<double>(ir[i].r)*ir[i].r; energy[i] = sum; }
    size_t a = 0, b = 0;
    for (size_t i = 0; i < ir.size(); ++i) { const double level = 10*std::log10(energy[i]/energy[0]); if (!a && level < -5) a = i; if (!b && level < -35) { b = i; break; } }
    if (!a || !b) return 0;
    return 2.*static_cast<double>(b-a)/rate;
}
void reverbDecaysAsNamed() {
    const double rate = 48000;
    double measured[3] {};
    for (uint32_t flavor = 0; flavor < 3; ++flavor) {
        for (double size : {25., 50., 75.}) {
            Chain c = make(oneBlock(ReverbBlock, flavor, size, 20, 0, 100));
            const size_t n = static_cast<size_t>(rate*std::min(16., reverbDecay(flavor, size)*1.6+1));
            const auto ir = renderImpulse(c, n);
            const double rt = measureRt60(ir, rate, 1000), named = reverbDecay(flavor, size), wide = measureRt60(ir, rate);
            std::printf("reverb %-5s size %.0f%%: named %.2f s, measured %.2f s around 1 kHz, %.2f s across the band\n", flavorNames[ReverbBlock][flavor], size, named, rt, wide);
            CHECK(rt > named*.9 && rt < named*1.12);              // the number on the knob is the decay you hear, within about 10%
            if (size == 50) measured[flavor] = rt;
        }
    }
    CHECK(measured[0] < measured[2] && measured[2] < measured[1]);          // a room is shorter than a plate, which is shorter than a hall
    // The two ears hear different tails.
    Chain c = make(oneBlock(ReverbBlock, 1, 50, 20, 0, 100));
    const auto ir = renderImpulse(c, 48000);
    double ll = 0, rr = 0, lr = 0;
    for (size_t i = 4800; i < ir.size(); ++i) { ll += static_cast<double>(ir[i].l)*ir[i].l; rr += static_cast<double>(ir[i].r)*ir[i].r; lr += static_cast<double>(ir[i].l)*ir[i].r; }
    std::printf("reverb: left-right correlation %.3f\n", lr/std::sqrt(ll*rr));
    CHECK(std::abs(lr/std::sqrt(ll*rr)) < .3);
    // Pre-delay: nothing comes out before it.
    Chain pre = make(oneBlock(ReverbBlock, 0, 50, 20, 100, 100));
    const auto p = renderImpulse(pre, 12000);
    float early = 0, later = 0;
    for (size_t i = 1; i < 4780; ++i) early = std::max(early, std::max(std::abs(p[i].l), std::abs(p[i].r)));
    for (size_t i = 4800; i < 12000; ++i) later = std::max(later, std::max(std::abs(p[i].l), std::abs(p[i].r)));
    CHECK(early == 0 && later > 1e-3);
    // Decay at the longest setting settles to nothing: stable.
    Chain longest = make(oneBlock(ReverbBlock, 1, 100, 0, 0, 100));
    const auto big = renderImpulse(longest, 48000*30);
    float tailPeak = 0;
    for (size_t i = big.size()-48000; i < big.size(); ++i) tailPeak = std::max(tailPeak, std::abs(big[i].l));
    CHECK(tailPeak < 1e-3f && std::isfinite(tailPeak));
}

void reverbDampingDarkensTheTail() {
    auto share = [&](double damping) {
        Chain c = make(oneBlock(ReverbBlock, 1, 50, damping, 0, 100));
        std::vector<float> y;
        Rng rng;
        for (size_t i = 0; i < 96000; ++i) y.push_back(c.process(i < 480 ? Stereo {rng.bipolar(), rng.bipolar()} : Stereo {}).l);
        double hf = 0, all = 0;
        for (size_t i = 48001; i < 96000; ++i) { hf += (y[i]-y[i-1])*(y[i]-y[i-1]); all += y[i]*y[i]; }
        return hf/std::max(all, 1e-30);
    };
    const double bright = share(0), dark = share(100);
    std::printf("reverb damping: high-frequency share of the tail %.3f at 0%% against %.3f at 100%%\n", bright, dark);
    CHECK(dark < bright*.5);
}

// ---------------------------------------------------------------- width
void widthFlavors() {
    // Stereo: 0 is mono, 50% (100% width) is the input, 100% (200%) doubles the side; the mono sum never changes.
    for (double amount : {0., 25., 50., 75., 100.}) {
        Chain c = make(oneBlock(WidthBlock, 0, amount, 0, 0, 100));
        const Stereo out = c.process({1.f, 0.f});
        const double width = widthFactor(amount);
        CHECK(std::abs(out.l+out.r-1.f) < 1e-6f);
        CHECK(std::abs((out.l-out.r)-static_cast<float>(width)) < 1e-6f);
    }
    // Haas: the right channel is delayed by the setting, at any rate.
    for (double rate : {44100., 48000., 96000.}) {
        Chain c = make(oneBlock(WidthBlock, 1, 40, 0, 0, 100), rate);            // 40% of 30 ms = 12 ms
        std::vector<float> right;
        for (int i = 0; i < 4000; ++i) right.push_back(c.process({i == 100 ? 1.f : 0.f, i == 100 ? 1.f : 0.f}).r);
        const size_t peak = static_cast<size_t>(std::max_element(right.begin(), right.end())-right.begin());
        const double ms = (static_cast<double>(peak)-100)*1000/rate;
        std::printf("Haas 12 ms at %.0f Hz: right channel arrives %.2f ms late\n", rate, ms);
        CHECK(std::abs(ms-12) < .1);
    }
    // Spread: a centered source comes out decorrelated, at about the same power.
    Chain spread = make(oneBlock(WidthBlock, 2, 100, 0, 0, 100));
    const auto in = noise(96000, .3f, 5);
    double ll = 0, rr = 0, lr = 0, inEnergy = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        const auto y = spread.process({in[i], in[i]});
        if (i > 2000) { ll += static_cast<double>(y.l)*y.l; rr += static_cast<double>(y.r)*y.r; lr += static_cast<double>(y.l)*y.r; inEnergy += static_cast<double>(in[i])*in[i]; }
    }
    const double corr = lr/std::sqrt(ll*rr), power = 10*std::log10((ll+rr)/2/inEnergy);
    std::printf("width spread: left-right correlation %.2f, power %.2f dB\n", corr, power);
    CHECK(corr < .6 && std::abs(power) < 1.5);
    // Mono Below: out-of-phase lows go to the middle (and vanish from a pure-side signal), highs stay.
    auto sideGain = [&](double hz, double monoBelow) {
        Chain c = make(oneBlock(WidthBlock, 0, 50, monoBelow, 0, 100));
        std::vector<float> y(48000);
        for (size_t i = 0; i < y.size(); ++i) { const float x = static_cast<float>(.3*std::sin(2*pi*hz*static_cast<double>(i)/48000)); y[i] = c.process({x, -x}).l; }
        return db(amplitudeAt(y, 24000, 48000, hz, 48000)/.3);
    };
    const double lowKept = sideGain(60, 0), lowGone = sideGain(60, 200), highKept = sideGain(2000, 200);
    std::printf("mono below: 60 Hz side signal %.1f dB without, %.1f dB with 200 Hz; 2 kHz %.1f dB\n", lowKept, lowGone, highKept);
    CHECK(std::abs(lowKept) < .1 && lowGone < -10 && std::abs(highKept) < .5);
}

// ---------------------------------------------------------------- the stage: switches and flavor changes do not click
void stageIsSmooth() {
    // Toggle every block and change every flavor under a steady low sine; no sample steps further than a sine alone allows
    // (the effects themselves add content, so the bound is loose: it catches clicks, not coloration).
    for (uint32_t block = 0; block < BlockCount; ++block) {
        Chain c = make(settings({}));
        Values v = oneBlock(block, 0, paramInfo(P(block, fxblock::P1)).initial, paramInfo(P(block, fxblock::P2)).initial, paramInfo(P(block, fxblock::P3)).initial,
                            paramInfo(P(block, BlockMix)).initial);
        v[P(block, On)] = 0;
        c.set(v);
        double worst = 0; float previous = 0;
        for (size_t i = 0; i < 48000*3; ++i) {
            if (i % 12000 == 6000) {                                              // a change every quarter second
                const size_t step = i/12000;
                v[P(block, On)] = step % 2 == 0 ? 0 : 1;
                if (step == 1 || step == 3 || step == 5 || step == 7 || step == 9) v[P(block, Flavor)] = static_cast<double>((step/2) % 3);
                c.set(v);
            }
            const float x = static_cast<float>(.2*std::sin(2*pi*110*static_cast<double>(i)/48000));
            const auto y = c.process({x, x});
            CHECK(std::isfinite(y.l) && std::isfinite(y.r));
            if (i > 100) worst = std::max(worst, static_cast<double>(std::abs(y.l-previous)));
            previous = y.l;
        }
        std::printf("%-10s: largest step between samples while toggling and switching flavors: %.3f\n", blockNames[block], worst);
        CHECK(worst < .25);
    }
}

int main() {
    filterFlavorsAndSlopes(); filterResonanceAndStability(); filterMovesSmoothly();
    oversamplerIsClean(); driveAddsHarmonicsAndKeepsLevel(); driveDoesNotAlias(); crushQuantizesAndHolds();
    modulationMoves(); delayRepeats(); reverbDecaysAsNamed(); reverbDampingDarkensTheTail(); widthFlavors(); stageIsSmooth();
    std::cout << "Blocks: filter, oversampler, drive, modulation, delay, reverb, width and stage switching passed\n";
}
