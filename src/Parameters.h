#pragma once
#include "Common.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fxblock {
// Six blocks in a chain; each has three flavors, a switch, three knobs that mean what the flavor says they mean,
// and a mix. IDs are persistent: append, never reorder.
enum Block : uint32_t { FilterBlock, DriveBlock, ModBlock, DelayBlock, ReverbBlock, WidthBlock, BlockCount };
inline constexpr uint32_t flavorCount = 3;

enum Global : uint32_t {
    Input,        // dB, into the chain (the dry signal that Mix blends back in is not touched)
    Output,       // dB
    Mix,          // whole-chain dry/wet
    Bypass,
    FilterOrder,  // 0: filter first, 1: filter after the drive
    GlobalCount
};
enum BlockKey : uint32_t { On, Flavor, P1, P2, P3, BlockMix, BlockKeyCount };
// Appended after the blocks so older sessions keep their IDs: matches the chain's loudness to the dry signal's.
inline constexpr uint32_t AutoGain = GlobalCount+BlockCount*BlockKeyCount;
inline constexpr uint32_t ParamCount = AutoGain+1;

inline constexpr uint32_t blockParam(uint32_t block, BlockKey key) { return GlobalCount+block*BlockKeyCount+key; }
inline constexpr bool isBlockParam(uint32_t id) { return id >= GlobalCount && id < AutoGain; }
inline constexpr uint32_t blockOf(uint32_t id) { return (id-GlobalCount)/BlockKeyCount; }
inline constexpr BlockKey keyOf(uint32_t id) { return static_cast<BlockKey>((id-GlobalCount)%BlockKeyCount); }

struct ParamInfo {
    const char* name;
    const char* unit;
    double min, max, initial;
    bool stepped = false, log = false;
};

inline constexpr std::array<const char*, BlockCount> blockNames {"Filter", "Drive", "Modulation", "Delay", "Reverb", "Width"};
inline constexpr std::array<std::array<const char*, flavorCount>, BlockCount> flavorNames {{
    {"Lowpass", "Highpass", "Bandpass"},
    {"Overdrive", "Fuzz", "Crush"},
    {"Chorus", "Flanger", "Phaser"},
    {"Digital", "Ping-Pong", "Tape"},
    {"Room", "Hall", "Plate"},
    {"Stereo", "Haas", "Spread"},
}};
// What the three knobs are called in a host's parameter list, whatever the flavor. The panel is more specific.
inline constexpr std::array<std::array<const char*, 3>, BlockCount> knobNames {{
    {"Cutoff", "Resonance", "Slope"},
    {"Drive", "Tone", "Level"},
    {"Rate", "Depth", "Feedback"},
    {"Time", "Feedback", "Tone"},
    {"Size", "Damping", "Pre-delay"},
    {"Amount", "Mono Below", ""},
}};

// What a flavor calls its knobs (null: the flavor does not use that knob) and a line saying what it sounds like.
struct FlavorInfo { const char* knob[3]; const char* caption; };
inline constexpr FlavorInfo flavorInfo(uint32_t block, uint32_t flavor) {
    constexpr FlavorInfo table[BlockCount][flavorCount] {
        {{{"CUTOFF", "RESONANCE", "SLOPE"}, "Darkens: keeps what is below the cutoff."},
         {{"CUTOFF", "RESONANCE", "SLOPE"}, "Thins: removes what is below the cutoff."},
         {{"CENTER", "RESONANCE", "SLOPE"}, "Keeps a band around the center: telephone, vowel and wah tones."}},
        {{{"DRIVE", "TONE", "LEVEL"}, "Warm soft clipping that thickens as you push it."},
         {{"FUZZ", "TONE", "LEVEL"}, "Fat, square-edged, uneven fuzz with a spitty bite."},
         {{"BITS", "RATE", "LEVEL"}, "Digital grit: fewer bits and a lower sample rate."}},
        {{{"RATE", "DEPTH", "FEEDBACK"}, "Slowly drifting copies that thicken and widen."},
         {{"RATE", "DEPTH", "FEEDBACK"}, "A short sweeping delay: jet-plane comb filtering."},
         {{"RATE", "DEPTH", "FEEDBACK"}, "Moving notches from a chain of allpass filters."}},
        {{{"TIME", "FEEDBACK", "TONE"}, "Clean, even repeats, the same on both sides."},
         {{"TIME", "FEEDBACK", "TONE"}, "Echoes that bounce between left and right."},
         {{"TIME", "FEEDBACK", "TONE"}, "Dark, wobbly, saturating repeats like an old tape echo."}},
        {{{"DECAY", "DAMPING", "PRE-DELAY"}, "A small, close space."},
         {{"DECAY", "DAMPING", "PRE-DELAY"}, "A big, slow space with a long tail."},
         {{"DECAY", "DAMPING", "PRE-DELAY"}, "Bright and dense, with no sense of a room."}},
        {{{"WIDTH", "MONO BELOW", nullptr}, "Widens or narrows the image. The mono sum never changes."},
         {{"TIME", "MONO BELOW", nullptr}, "Delays one ear a few milliseconds for a wide, offset feel."},
         {{"AMOUNT", "MONO BELOW", nullptr}, "Smears the phase so a centered source fans out across the field."}},
    };
    return table[block < BlockCount ? block : 0][flavor < flavorCount ? flavor : 0];
}

inline constexpr ParamInfo blockKnobInfo(uint32_t block, BlockKey key) {
    constexpr ParamInfo table[BlockCount][3] {
        {{"Cutoff", "Hz", 20, 20000, 1000, false, true}, {"Resonance", "%", 0, 100, 25}, {"Slope", "", 0, 1, 0, true}},
        {{"Drive", "%", 0, 100, 40}, {"Tone", "%", 0, 100, 60}, {"Level", "dB", -24, 12, 0}},
        {{"Rate", "Hz", .05, 10, .8, false, true}, {"Depth", "%", 0, 100, 50}, {"Feedback", "%", 0, 95, 30}},
        {{"Time", "ms", 20, 1000, 300, false, true}, {"Feedback", "%", 0, 90, 35}, {"Tone", "%", 0, 100, 60}},
        {{"Size", "%", 0, 100, 50}, {"Damping", "%", 0, 100, 40}, {"Pre-delay", "ms", 0, 100, 0}},
        {{"Amount", "%", 0, 100, 60}, {"Mono Below", "Hz", 0, 400, 0}, {"", "", 0, 1, 0, true}},
    };
    return table[block][key-P1];
}
inline constexpr double defaultBlockMix[BlockCount] {100, 100, 50, 30, 25, 100};

inline constexpr ParamInfo paramInfo(uint32_t id) {
    constexpr ParamInfo globals[GlobalCount] {
        {"Input", "dB", -24, 12, 0}, {"Output", "dB", -24, 12, 0}, {"Mix", "%", 0, 100, 100},
        {"Bypass", "", 0, 1, 0, true}, {"Filter Order", "", 0, 1, 0, true}};
    if (id < GlobalCount) return globals[id];
    if (id >= ParamCount) return {"", "", 0, 0, 0};
    if (id == AutoGain) return {"Auto Gain", "", 0, 1, 0, true};
    const uint32_t block = blockOf(id);
    switch (keyOf(id)) {
    case On: return {"On", "", 0, 1, 0, true};
    case Flavor: return {"Flavor", "", 0, flavorCount-1, 0, true};
    case BlockMix: return {"Mix", "%", 0, 100, defaultBlockMix[block]};
    default: return blockKnobInfo(block, keyOf(id));
    }
}
// The Width block has no third knob.
inline constexpr bool paramUsed(uint32_t id) {
    return id < ParamCount && !(isBlockParam(id) && blockOf(id) == WidthBlock && keyOf(id) == P3);
}
// What a host sees: every parameter but the unused one, in id order.
inline constexpr uint32_t exposedCount = ParamCount-1;
inline constexpr uint32_t exposedId(uint32_t index) { return index < blockParam(WidthBlock, P3) ? index : index+1; }
inline constexpr bool isSwitch(uint32_t id) { return id == Bypass || id == FilterOrder || id == AutoGain || (isBlockParam(id) && (keyOf(id) == On || keyOf(id) == Flavor)); }

using Values = std::array<double, ParamCount>;
inline Values defaults() noexcept {
    Values values {};
    for (uint32_t i = 0; i < ParamCount; ++i) values[i] = paramInfo(i).initial;
    return values;
}
inline double sanitize(uint32_t id, double value) noexcept {
    if (id >= ParamCount) return 0;
    const auto p = paramInfo(id);
    if (!std::isfinite(value)) value = p.initial;
    value = std::clamp(value, p.min, p.max);
    return p.stepped ? std::trunc(value) : value;
}
// Knob position <-> value. Log parameters are exponential across their range.
inline double toNormalized(uint32_t id, double value) noexcept {
    const auto p = paramInfo(id);
    value = std::clamp(value, p.min, p.max);
    return p.log ? std::log(value/p.min)/std::log(p.max/p.min) : (value-p.min)/(p.max-p.min);
}
inline double fromNormalized(uint32_t id, double position) noexcept {
    const auto p = paramInfo(id);
    position = std::clamp(position, 0., 1.);
    return p.log ? p.min*std::pow(p.max/p.min, position) : p.min+(p.max-p.min)*position;
}

// ---------------------------------------------------------------- what a flavor's knobs mean in real units
// The values of P1..P3 are stored the same for every flavor; a flavor maps them to what it needs. The DSP and the
// text shown to the user use these same functions, so a number on screen is the number heard.
inline double crushBits(double percent) noexcept { return 16-14*percent/100; }                              // 16 down to 2
inline double crushDivisor(double percent) noexcept { return std::pow(32., 1-percent/100); }                 // 100%: full rate, 0%: a 32nd
inline double widthFactor(double percent) noexcept { return 2*percent/100; }                                 // 0..200%
inline double haasMs(double percent) noexcept { return 30*percent/100; }
// Decay time (RT60) in seconds across the Size knob, per reverb flavor.
inline double reverbDecay(uint32_t flavor, double percent) noexcept {
    constexpr double low[flavorCount] {.15, 1., .4}, high[flavorCount] {1.6, 12., 5.};
    const uint32_t f = flavor < flavorCount ? flavor : 0;
    return low[f]*std::pow(high[f]/low[f], percent/100);
}
inline double reverbDecayToPercent(uint32_t flavor, double seconds) noexcept {
    constexpr double low[flavorCount] {.15, 1., .4}, high[flavorCount] {1.6, 12., 5.};
    const uint32_t f = flavor < flavorCount ? flavor : 0;
    return 100*std::log(std::max(seconds, 1e-3)/low[f])/std::log(high[f]/low[f]);
}

// ---------------------------------------------------------------- text
// "flavor" is the flavor of the parameter's own block, which decides what its knobs are measured in.
inline int formatValue(char* out, size_t capacity, uint32_t id, double value, uint32_t flavor) {
    value = sanitize(id, value);
    if (id == Bypass || id == AutoGain) return std::snprintf(out, capacity, "%s", value ? "On" : "Off");
    if (id == FilterOrder) return std::snprintf(out, capacity, "%s", value ? "After drive" : "Before drive");
    if (id == Input || id == Output) return std::snprintf(out, capacity, "%+.1f dB", value);
    if (id == Mix) return std::snprintf(out, capacity, "%.0f %%", value);
    const uint32_t block = blockOf(id);
    switch (keyOf(id)) {
    case On: return std::snprintf(out, capacity, "%s", value ? "On" : "Off");
    case Flavor: return std::snprintf(out, capacity, "%s", flavorNames[block][static_cast<size_t>(value)]);
    case BlockMix: return std::snprintf(out, capacity, "%.0f %%", value);
    default: break;
    }
    const BlockKey key = keyOf(id);
    switch (block) {
    case FilterBlock:
        if (key == P1) return value >= 1000 ? std::snprintf(out, capacity, "%.2f kHz", value*.001) : std::snprintf(out, capacity, "%.0f Hz", value);
        if (key == P3) return std::snprintf(out, capacity, "%s", value ? "24 dB" : "12 dB");
        break;
    case DriveBlock:
        if (key == P3) return std::snprintf(out, capacity, "%+.1f dB", value);
        if (flavor == 2 && key == P1) return std::snprintf(out, capacity, "%.1f bit", crushBits(value));
        if (flavor == 2 && key == P2) return crushDivisor(value) < 1.05 ? std::snprintf(out, capacity, "full") : std::snprintf(out, capacity, "1/%.0f", crushDivisor(value));
        break;
    case ModBlock:
        if (key == P1) return std::snprintf(out, capacity, "%.2f Hz", value);
        break;
    case DelayBlock:
        if (key == P1) return value >= 1000 ? std::snprintf(out, capacity, "1.00 s") : std::snprintf(out, capacity, "%.0f ms", value);
        break;
    case ReverbBlock:
        if (key == P1) return std::snprintf(out, capacity, "%.2f s", reverbDecay(flavor, value));
        if (key == P3) return std::snprintf(out, capacity, "%.0f ms", value);
        break;
    case WidthBlock:
        if (key == P1 && flavor == 0) return std::snprintf(out, capacity, "%.0f %%", 100*widthFactor(value));
        if (key == P1 && flavor == 1) return std::snprintf(out, capacity, "%.1f ms", haasMs(value));
        if (key == P2) return value < 20 ? std::snprintf(out, capacity, "off") : std::snprintf(out, capacity, "%.0f Hz", value);
        break;
    default: break;
    }
    return std::snprintf(out, capacity, "%.0f %%", value);
}

// The inverse: "1.5k", "-3 dB", "250 ms", "70%", "8 bit", "2.5 s", "1/8", "off", "after drive", flavor names.
inline bool parseValue(uint32_t id, const char* text, uint32_t flavor, double& out) {
    if (!text) return false;
    while (*text == ' ') ++text;
    if (!*text) return false;
    const auto equal = [&](const char* word) {
        const char* a = text; const char* b = word;
        while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
        return !*b && (!*a || *a == ' ');
    };
    if (isSwitch(id)) {
        if (isBlockParam(id) && keyOf(id) == Flavor) {
            for (uint32_t f = 0; f < flavorCount; ++f) if (equal(flavorNames[blockOf(id)][f])) { out = f; return true; }
        } else if (equal("on") || equal("after drive") || equal("after") || equal("post")) { out = 1; return true; }
        else if (equal("off") || equal("before drive") || equal("before") || equal("pre")) { out = 0; return true; }
        char* end = nullptr;
        const double v = std::strtod(text, &end);
        if (end == text) return false;
        out = sanitize(id, v);
        return true;
    }
    const BlockKey key = isBlockParam(id) ? keyOf(id) : BlockKeyCount;
    const uint32_t block = isBlockParam(id) ? blockOf(id) : 0;
    if (key == P2 && block == WidthBlock && equal("off")) { out = 0; return true; }
    if (block == DriveBlock && key == P2 && flavor == 2) {
        if (equal("full")) { out = 100; return true; }
        if (text[0] == '1' && text[1] == '/') { const double d = std::strtod(text+2, nullptr); if (d >= 1) { out = sanitize(id, 100*(1-std::log(d)/std::log(32.))); return true; } return false; }
    }
    char* end = nullptr;
    double v = std::strtod(text, &end);
    if (end == text || !std::isfinite(v)) return false;
    while (*end == ' ') ++end;
    // What follows the number must be a unit this parameter could have shown, or nothing.
    char unit[8] {};
    size_t length = 0;
    for (; end[length] && end[length] != ' '; ++length) { if (length >= sizeof(unit)-1) return false; unit[length] = static_cast<char>(std::tolower(static_cast<unsigned char>(end[length]))); }
    for (const char* rest = end+length; *rest; ++rest) if (*rest != ' ') return false;
    static const char* const units[] {"", "%", "hz", "khz", "k", "ms", "s", "db", "bit", "bits", "x"};
    bool known = false;
    for (const char* u : units) known = known || !std::strcmp(unit, u);
    if (!known) return false;
    const bool kilo = !std::strcmp(unit, "k") || !std::strcmp(unit, "khz");
    const bool milli = !std::strcmp(unit, "ms");
    const bool seconds = !std::strcmp(unit, "s");
    if (kilo) v *= 1000;
    if (block == FilterBlock && key == P3) { out = v >= 18 ? 1 : 0; return true; }           // "12 dB" or "24 dB"
    if (id < GlobalCount || block == FilterBlock || block == ModBlock) { out = sanitize(id, v); return true; }
    if (block == DelayBlock && key == P1) { out = sanitize(id, seconds && !milli ? v*1000 : v); return true; }
    if (block == ReverbBlock && key == P1) { out = sanitize(id, reverbDecayToPercent(flavor, milli ? v*.001 : v)); return true; }
    if (block == DriveBlock && flavor == 2 && key == P1) { out = sanitize(id, 100*(16-v)/14); return true; }
    if (block == WidthBlock && key == P1 && flavor == 1) { out = sanitize(id, 100*v/30); return true; }
    if (block == WidthBlock && key == P1 && flavor == 0) { out = sanitize(id, v*.5); return true; }
    out = sanitize(id, v);
    return true;
}
} // namespace fxblock
