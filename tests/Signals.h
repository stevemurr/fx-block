#pragma once
#include "Chain.h"
#include "Test.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace fxblock;

using Setting = std::pair<uint32_t, double>;
inline Values settings(std::initializer_list<Setting> list) {
    Values v = defaults();
    for (const auto& [id, value] : list) v[id] = sanitize(id, value);
    return v;
}
inline uint32_t P(uint32_t block, BlockKey key) { return blockParam(block, key); }
// A chain with one block switched on, in a flavor, with its knobs set.
inline Values oneBlock(uint32_t block, uint32_t flavor, double p1, double p2, double p3, double mix) {
    return settings({{P(block, On), 1}, {P(block, Flavor), flavor}, {P(block, fxblock::P1), p1}, {P(block, fxblock::P2), p2},
                     {P(block, fxblock::P3), p3}, {P(block, BlockMix), mix}});
}
inline Chain make(const Values& v, double rate = 48000) { Chain c; c.prepare(rate); c.set(v, true); return c; }

inline double rms(const std::vector<float>& x, size_t from, size_t to) {
    double sum = 0; for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i])*x[i];
    return std::sqrt(sum/static_cast<double>(std::max<size_t>(1, to-from)));
}
// Amplitude of the component at `hz` (a single-bin DFT over whole samples).
inline double amplitudeAt(const std::vector<float>& x, size_t from, size_t to, double hz, double rate) {
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        const double phase = 2*pi*hz*static_cast<double>(i)/rate;
        re += x[i]*std::cos(phase); im += x[i]*std::sin(phase);
    }
    return 2*std::sqrt(re*re+im*im)/static_cast<double>(to-from);
}
inline double db(double ratio) { return 20*std::log10(std::max(ratio, 1e-12)); }

// Feeds a sine through the chain and returns the left output.
inline std::vector<float> renderSine(Chain& c, double hz, double amplitude, double seconds, double rate = 48000, bool stereoSame = true) {
    const size_t n = static_cast<size_t>(seconds*rate);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const float x = static_cast<float>(amplitude*std::sin(2*pi*hz*static_cast<double>(i)/rate));
        out[i] = c.process({x, stereoSame ? x : 0.f}).l;
    }
    return out;
}
// Gain in dB at one frequency once the effect has settled.
inline double gainDb(const Values& v, double hz, double rate = 48000, double amplitude = .1) {
    Chain c = make(v, rate);
    const auto y = renderSine(c, hz, amplitude, 1., rate);
    const size_t n = y.size();
    return db(amplitudeAt(y, n/2, n, hz, rate)/amplitude);
}
inline std::vector<Stereo> renderImpulse(Chain& c, size_t n) {
    std::vector<Stereo> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = c.process(i == 0 ? Stereo {1.f, 1.f} : Stereo {});
    return out;
}
inline std::vector<float> noise(size_t n, float amplitude = .3f, uint32_t seed = 1) {
    Rng r; r.state = 0x6d2b79f5u+seed*7919u;
    std::vector<float> x(n); for (auto& v : x) v = amplitude*r.bipolar();
    return x;
}

// Power spectrum of a Hann-windowed block (size a power of two), as amplitude per bin relative to a full-scale sine.
inline std::vector<double> spectrum(const std::vector<float>& x, size_t from, size_t n) {
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) a[i] = x[from+i]*(.5-.5*std::cos(2*pi*static_cast<double>(i)/static_cast<double>(n)));
    for (size_t i = 1, j = 0; i < n; ++i) {                      // bit reversal
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const auto w = std::polar(1., -2*pi/static_cast<double>(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> k = 1;
            for (size_t j = 0; j < len/2; ++j) { const auto u = a[i+j], v = a[i+j+len/2]*k; a[i+j] = u+v; a[i+j+len/2] = u-v; k *= w; }
        }
    }
    std::vector<double> out(n/2);
    for (size_t i = 0; i < n/2; ++i) out[i] = 2*std::abs(a[i])/(static_cast<double>(n)*.5);
    return out;
}
