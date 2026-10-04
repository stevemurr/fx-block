// Renders every flavor of every block over a short synthetic phrase to WAV files, to listen to what each one does.
//   render_demo out_dir
// Each file is a plucked-note arpeggio, a held chord and a noise burst: one block on, in one flavor, at its default settings
// (a little stronger where the default is shy), then its tail.
#include "Chain.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <sys/stat.h>

using namespace fxblock;

static std::vector<Stereo> phrase(double rate) {
    std::vector<Stereo> out(static_cast<size_t>(rate*5));
    Rng rng;
    const double notes[] {220, 261.63, 329.63, 440, 329.63, 261.63, 220, 164.81};
    for (size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i)/rate;
        double x = 0;
        if (t < 3) {                                                       // an arpeggio of plucks
            const size_t n = static_cast<size_t>(t/.375);
            const double local = t-static_cast<double>(n)*.375, f = notes[n%8], envelope = std::exp(-local*5);
            for (int h = 1; h <= 6; ++h) x += std::sin(2*pi*f*h*t)/h*envelope*.35;
        } else if (t < 4.2) {                                              // a held chord
            for (double f : {110., 138.59, 164.81}) for (int h = 1; h <= 4; ++h) x += std::sin(2*pi*f*h*t)/h*.12;
            x *= std::min(1., (4.2-t)*4);
        } else if (t < 4.35) {                                             // a burst of noise
            x = .3*rng.bipolar()*std::exp(-(t-4.2)*20);
        }
        out[i] = {static_cast<float>(x), static_cast<float>(x*.8)};
    }
    return out;
}

static void writeWav(const std::string& path, const std::vector<Stereo>& audio, uint32_t rate) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) { std::perror(path.c_str()); std::exit(1); }
    // A resonant filter can legitimately go past full scale (a plugin's output is float); a file can not, so scale it down.
    float peak = 0;
    for (const auto& s : audio) peak = std::max({peak, std::abs(s.l), std::abs(s.r)});
    const float scale = peak > .98f ? .98f/peak : 1.f;
    const uint32_t bytes = static_cast<uint32_t>(audio.size()*4), byteRate = rate*4;
    auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, file); };
    auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, file); };
    std::fwrite("RIFF", 1, 4, file); put32(36+bytes); std::fwrite("WAVEfmt ", 1, 8, file); put32(16); put16(1); put16(2); put32(rate); put32(byteRate); put16(4); put16(16);
    std::fwrite("data", 1, 4, file); put32(bytes);
    for (const auto& s : audio) {
        const int16_t l = static_cast<int16_t>(std::lround(std::clamp(s.l*scale, -1.f, 1.f)*32767)), r = static_cast<int16_t>(std::lround(std::clamp(s.r*scale, -1.f, 1.f)*32767));
        std::fwrite(&l, 2, 1, file); std::fwrite(&r, 2, 1, file);
    }
    std::fclose(file);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s out_dir\n", argv[0]); return 2; }
    const std::string dir = argv[1];
    mkdir(dir.c_str(), 0755);
    const double rate = 48000;
    const auto input = phrase(rate);
    // A few settings are pushed from their defaults so the character is plain at a glance.
    struct Tweak { uint32_t block, flavor; double p1, p2, p3, mix; };
    const Tweak tweaks[] {
        {FilterBlock, 0, 900, 55, 0, 100}, {FilterBlock, 1, 1200, 40, 0, 100}, {FilterBlock, 2, 1000, 60, 0, 100},
        {DriveBlock, 0, 55, 60, 0, 100}, {DriveBlock, 1, 50, 55, 0, 100}, {DriveBlock, 2, 55, 70, 0, 100},
        {ModBlock, 0, .7, 60, 20, 50}, {ModBlock, 1, .25, 70, 60, 50}, {ModBlock, 2, .4, 80, 50, 50},
        {DelayBlock, 0, 375, 45, 60, 35}, {DelayBlock, 1, 375, 45, 60, 35}, {DelayBlock, 2, 375, 50, 55, 40},
        {ReverbBlock, 0, 50, 40, 0, 30}, {ReverbBlock, 1, 50, 40, 15, 30}, {ReverbBlock, 2, 50, 30, 0, 30},
        {WidthBlock, 0, 90, 0, 0, 100}, {WidthBlock, 1, 55, 0, 0, 100}, {WidthBlock, 2, 100, 0, 0, 100},
    };
    for (const Tweak& t : tweaks) {
        Values v = defaults();
        v[blockParam(t.block, On)] = 1; v[blockParam(t.block, Flavor)] = t.flavor;
        v[blockParam(t.block, P1)] = t.p1; v[blockParam(t.block, P2)] = t.p2; v[blockParam(t.block, P3)] = t.p3; v[blockParam(t.block, BlockMix)] = t.mix;
        Chain chain; chain.prepare(rate); chain.set(v, true);
        std::vector<Stereo> out;
        for (const auto& s : input) out.push_back(chain.process(s));
        for (size_t i = 0; i < static_cast<size_t>(rate*1.5); ++i) out.push_back(chain.process({}));
        const std::string name = dir+"/"+std::string(blockNames[t.block])+"-"+flavorNames[t.block][t.flavor]+".wav";
        writeWav(name, out, static_cast<uint32_t>(rate));
        std::printf("%s\n", name.c_str());
    }
    return 0;
}
