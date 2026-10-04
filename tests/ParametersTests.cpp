#include "Parameters.h"
#include "Test.h"
#include <cstring>
#include <set>
#include <string>

using namespace fxblock;

void layoutIsStable() {
    // The documented layout: five globals, then six blocks of six. Appending is allowed; moving is not.
    CHECK(GlobalCount == 5 && BlockKeyCount == 6 && BlockCount == 6 && ParamCount == 5+36+2 && AutoGain == 41 && DelaySync == 42);     // Auto Gain and Sync are appended after the blocks
    CHECK(Input == 0 && Output == 1 && Mix == 2 && Bypass == 3 && FilterOrder == 4);
    CHECK(blockParam(FilterBlock, On) == 5 && blockParam(DriveBlock, On) == 11 && blockParam(WidthBlock, BlockMix) == AutoGain-1);
    for (uint32_t id = GlobalCount; id < AutoGain; ++id) CHECK(blockParam(blockOf(id), keyOf(id)) == id && blockOf(id) < BlockCount);
    // The host's index skips the one unused knob, and covers every other parameter exactly once.
    std::set<uint32_t> seen;
    for (uint32_t i = 0; i < exposedCount; ++i) { const uint32_t id = exposedId(i); CHECK(id < ParamCount && paramUsed(id) && seen.insert(id).second); }
    CHECK(seen.size() == exposedCount && exposedCount == ParamCount-1 && !paramUsed(blockParam(WidthBlock, P3)));
}

void infoIsConsistent() {
    std::set<std::string> names;
    for (uint32_t id = 0; id < ParamCount; ++id) {
        const auto p = paramInfo(id);
        CHECK(p.min < p.max && p.initial >= p.min && p.initial <= p.max);
        CHECK(!p.log || p.min > 0);
        CHECK(std::strlen(p.name) < 200);
        if (!paramUsed(id)) continue;
        // A host shows module and name together; they must be unique within the box.
        std::string module = moduleOf(id);
        const std::string name = isBlockParam(id) && keyOf(id) >= P1 && keyOf(id) <= P3 ? knobNames[blockOf(id)][keyOf(id)-P1] : p.name;
        CHECK(!name.empty());
        CHECK(names.insert(module+"/"+name).second || id == FilterOrder);
        if (p.stepped) CHECK(p.min == std::trunc(p.min) && p.max == std::trunc(p.max) && p.initial == std::trunc(p.initial));
    }
    const auto d = defaults();
    for (uint32_t id = 0; id < ParamCount; ++id) CHECK(d[id] == paramInfo(id).initial);
    // Everything starts off, so a fresh instance does nothing until a block is switched on.
    for (uint32_t b = 0; b < BlockCount; ++b) CHECK(d[blockParam(b, On)] == 0 && d[blockParam(b, Flavor)] == 0);
    CHECK(d[AutoGain] == 0 && d[DelaySync] == 0 && d[Mix] == 100 && d[Input] == 0 && d[Output] == 0 && d[Bypass] == 0);
}

void sanitizeAndNormalize() {
    for (uint32_t id = 0; id < ParamCount; ++id) {
        const auto p = paramInfo(id);
        CHECK(sanitize(id, std::nan("")) == sanitize(id, p.initial));
        CHECK(sanitize(id, 1e30) == p.max && sanitize(id, -1e30) == p.min);
        CHECK(sanitize(id, sanitize(id, p.initial*.7+p.min*.3)) == sanitize(id, p.initial*.7+p.min*.3));
        if (p.stepped) CHECK(sanitize(id, p.min+.9) == p.min);
        for (double position : {0., .25, .5, .75, 1.}) {
            const double v = fromNormalized(id, position);
            CHECK(v >= p.min-1e-9 && v <= p.max+1e-9);
            CHECK(std::abs(toNormalized(id, v)-position) < 1e-9);
        }
    }
    CHECK(sanitize(ParamCount, 5) == 0);
}

// The note values: Free, then twelve lengths in beats, shortest to longest, with triplets a third shorter and dots half
// again as long than the plain note beside them.
void syncValues() {
    CHECK(syncCount == 13 && syncBeats[0] == 0);
    // Listed by note value, as a host lists them: within a note the triplet, the plain note, then the dotted one; across notes, longer.
    CHECK(syncBeats[3] > syncBeats[2] && syncBeats[4] > syncBeats[3] && syncBeats[6] > syncBeats[5] && syncBeats[7] > syncBeats[6] && syncBeats[9] > syncBeats[8] && syncBeats[10] > syncBeats[9]);
    CHECK(syncBeats[1] < syncBeats[3] && syncBeats[3] < syncBeats[6] && syncBeats[6] < syncBeats[9] && syncBeats[9] < syncBeats[11] && syncBeats[11] < syncBeats[12]);
    CHECK(syncBeats[syncCount-1] == 4);                                      // 1/1 is a bar of four beats
    const auto beats = [&](const char* name) { for (uint32_t i = 0; i < syncCount; ++i) if (!std::strcmp(syncNames[i], name)) return syncBeats[i]; return -1.; };
    CHECK(beats("1/4") == 1 && beats("1/8") == .5 && beats("1/16") == .25 && beats("1/2") == 2);
    CHECK(std::abs(beats("1/8T")-beats("1/8")*2/3) < 1e-12 && std::abs(beats("1/4T")-beats("1/4")*2/3) < 1e-12 && std::abs(beats("1/16T")-beats("1/16")*2/3) < 1e-12);
    CHECK(beats("1/8.") == beats("1/8")*1.5 && beats("1/4.") == beats("1/4")*1.5 && beats("1/16.") == beats("1/16")*1.5);
    double v = -1;
    for (uint32_t i = 0; i < syncCount; ++i) { char text[32]; formatValue(text, sizeof(text), DelaySync, i, 0); CHECK(!std::strcmp(text, syncNames[i]) && parseValue(DelaySync, text, 0, v) && v == i); }
    CHECK(parseValue(DelaySync, "1/8", 0, v) && v == 6 && parseValue(DelaySync, "1/8.", 0, v) && v == 7 && parseValue(DelaySync, "1/8t", 0, v) && v == 5);
    CHECK(parseValue(DelaySync, "free", 0, v) && v == 0 && parseValue(DelaySync, "OFF", 0, v) && v == 0);
    CHECK(!parseValue(DelaySync, "1/3", 0, v) && !parseValue(DelaySync, "dotted", 0, v));
    CHECK(std::strcmp(moduleOf(DelaySync), "Delay") == 0 && std::strcmp(moduleOf(AutoGain), "Master") == 0 && std::strcmp(moduleOf(blockParam(ReverbBlock, P2)), "Reverb") == 0);
    CHECK(isSwitch(DelaySync) && paramInfo(DelaySync).stepped && paramInfo(DelaySync).max == 12);
}

void flavorsAreDescribed() {
    for (uint32_t b = 0; b < BlockCount; ++b) for (uint32_t f = 0; f < flavorCount; ++f) {
        const auto info = flavorInfo(b, f);
        CHECK(info.knob[0] && info.knob[1] && std::strlen(info.caption) > 10 && std::strlen(info.caption) < 90);
        CHECK((info.knob[2] == nullptr) == (b == WidthBlock));
        CHECK(flavorNames[b][f] && std::strlen(flavorNames[b][f]) > 2);
    }
    // Three flavors in every block, each with its own name.
    for (uint32_t b = 0; b < BlockCount; ++b) CHECK(std::strcmp(flavorNames[b][0], flavorNames[b][1]) && std::strcmp(flavorNames[b][1], flavorNames[b][2]));
}

// What is shown can be typed back: format, then parse, lands within the rounding of what was displayed.
void textRoundTrips() {
    for (uint32_t id = 0; id < ParamCount; ++id) {
        if (!paramUsed(id)) continue;
        const auto p = paramInfo(id);
        for (uint32_t flavor = 0; flavor < flavorCount; ++flavor) {
            for (double v : {p.min, p.initial, p.max, p.min+(p.max-p.min)*.37, p.min+(p.max-p.min)*.81}) {
                const double value = sanitize(id, v);
                char text[64] {}; double parsed = -1e9;
                CHECK(formatValue(text, sizeof(text), id, value, flavor) > 0);
                if (!parseValue(id, text, flavor, parsed)) { std::cerr << "cannot parse \"" << text << "\" for parameter " << id << " flavor " << flavor << '\n'; CHECK(false); }
                // Choices must come back exactly; continuous values to within the rounding of what was displayed.
                const double tolerance = p.stepped ? 0 : std::max(1.0, std::abs(value)*.012);
                if (std::abs(parsed-value) > tolerance) { std::cerr << "\"" << text << "\" read back as " << parsed << ", was " << value << " (parameter " << id << ", flavor " << flavor << ")\n"; CHECK(false); }
            }
        }
    }
}

void textIsStrict() {
    double v = 0;
    CHECK(!parseValue(Mix, "abc", 0, v) && !parseValue(Mix, "", 0, v) && !parseValue(Mix, "50 garbage", 0, v) && !parseValue(Mix, "50 %% extra", 0, v));
    CHECK(!parseValue(Mix, "nan", 0, v) && !parseValue(Mix, "1 2", 0, v));
    CHECK(parseValue(Mix, "70", 0, v) && v == 70 && parseValue(Mix, "70 %", 0, v) && v == 70 && parseValue(Mix, "70%", 0, v) && v == 70);
    CHECK(parseValue(Input, "-6 dB", 0, v) && v == -6 && parseValue(Input, "+3", 0, v) && v == 3 && parseValue(Output, "40", 0, v) && v == 12);
    CHECK(parseValue(blockParam(FilterBlock, P1), "1.5k", 0, v) && v == 1500);
    CHECK(parseValue(blockParam(FilterBlock, P1), "2 kHz", 0, v) && v == 2000 && parseValue(blockParam(FilterBlock, P1), "440", 0, v) && v == 440);
    CHECK(parseValue(blockParam(DelayBlock, P1), "250 ms", 0, v) && v == 250 && parseValue(blockParam(DelayBlock, P1), "0.5 s", 0, v) && v == 500);
    CHECK(parseValue(blockParam(ReverbBlock, P1), "3 s", 1, v) && std::abs(reverbDecay(1, v)-3) < .01);
    CHECK(parseValue(blockParam(DriveBlock, P1), "8 bit", 2, v) && std::abs(crushBits(v)-8) < .01);
    CHECK(parseValue(blockParam(DriveBlock, P2), "1/8", 2, v) && std::abs(crushDivisor(v)-8) < .01 && parseValue(blockParam(DriveBlock, P2), "full", 2, v) && v == 100);
    CHECK(parseValue(blockParam(WidthBlock, P1), "12 ms", 1, v) && std::abs(haasMs(v)-12) < .01);
    CHECK(parseValue(blockParam(WidthBlock, P1), "150 %", 0, v) && std::abs(widthFactor(v)-1.5) < .01);
    CHECK(parseValue(blockParam(WidthBlock, P2), "off", 0, v) && v == 0);
    CHECK(parseValue(blockParam(DriveBlock, Flavor), "fuzz", 0, v) && v == 1 && parseValue(blockParam(DelayBlock, Flavor), "TAPE", 0, v) && v == 2);
    CHECK(parseValue(blockParam(ReverbBlock, Flavor), "plate", 0, v) && v == 2 && !parseValue(blockParam(ReverbBlock, Flavor), "spring", 0, v));
    CHECK(parseValue(blockParam(ModBlock, On), "on", 0, v) && v == 1 && parseValue(blockParam(ModBlock, On), "Off", 0, v) && v == 0);
    CHECK(parseValue(AutoGain, "on", 0, v) && v == 1 && parseValue(AutoGain, "Off", 0, v) && v == 0);
    CHECK(parseValue(FilterOrder, "after drive", 0, v) && v == 1 && parseValue(FilterOrder, "Before drive", 0, v) && v == 0);
    char text[64];
    formatValue(text, sizeof(text), blockParam(ReverbBlock, P1), 50, 1);
    CHECK(std::strcmp(text, "3.46 s") == 0);
    formatValue(text, sizeof(text), blockParam(DriveBlock, P1), 100*(16-8.)/14, 2);
    CHECK(std::strcmp(text, "8.0 bit") == 0);
    formatValue(text, sizeof(text), blockParam(FilterBlock, P1), 1500, 0);
    CHECK(std::strcmp(text, "1.50 kHz") == 0);
    formatValue(text, sizeof(text), blockParam(WidthBlock, P1), 50, 0);
    CHECK(std::strcmp(text, "100 %") == 0);
    // A tiny buffer truncates rather than overruns.
    char small[4] {'x', 'x', 'x', 'x'};
    formatValue(small, sizeof(small), blockParam(FilterBlock, P1), 1500, 0);
    CHECK(small[3] == 0);
}

// The same knob reads differently in each flavor, and the DSP uses the same mapping.
void flavorsMeasureTheirOwnWay() {
    char a[32], b[32], c[32];
    const uint32_t size = blockParam(ReverbBlock, P1);
    formatValue(a, sizeof(a), size, 50, 0); formatValue(b, sizeof(b), size, 50, 1); formatValue(c, sizeof(c), size, 50, 2);
    CHECK(std::strcmp(a, b) && std::strcmp(b, c) && std::strcmp(a, c));
    CHECK(reverbDecay(0, 100) < reverbDecay(2, 100) && reverbDecay(2, 100) < reverbDecay(1, 100));
    CHECK(std::abs(reverbDecay(0, 0)-.15) < 1e-9 && std::abs(reverbDecay(1, 100)-12) < 1e-9);
    CHECK(crushBits(0) == 16 && crushBits(100) == 2 && crushDivisor(100) == 1 && std::abs(crushDivisor(0)-32) < 1e-9);
}

int main() {
    layoutIsStable(); infoIsConsistent(); syncValues(); sanitizeAndNormalize(); flavorsAreDescribed(); textRoundTrips(); textIsStrict(); flavorsMeasureTheirOwnWay();
    std::cout << "Parameters: layout, ranges, text conversion in every flavor and strict parsing passed\n";
}
