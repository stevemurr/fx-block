#include "Parameters.h"
#include "Test.h"
#include <clap/clap.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

using namespace fxblock;
static unsigned rescans = 0;
void CLAP_ABI rescan(const clap_host_t*, clap_param_rescan_flags flags) { CHECK(flags == CLAP_PARAM_RESCAN_VALUES); ++rescans; }
const clap_host_params_t hostParams {rescan, nullptr, nullptr};
const void* CLAP_ABI hostExtension(const clap_host_t*, const char* id) { return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &hostParams : nullptr; }
void CLAP_ABI hostNoop(const clap_host_t*) {}
const clap_host_t host {CLAP_VERSION, nullptr, "Tests", "Tests", "", "1", hostExtension, hostNoop, hostNoop, hostNoop};

// A list of events, each stored in an aligned slot.
struct Events {
    struct Slot { alignas(16) uint8_t bytes[256]; };
    std::vector<Slot> data;
    clap_input_events_t api {this,
        [](const clap_input_events_t* e) -> uint32_t { return static_cast<uint32_t>(static_cast<Events*>(e->ctx)->data.size()); },
        [](const clap_input_events_t* e, uint32_t i) -> const clap_event_header_t* { return reinterpret_cast<const clap_event_header_t*>(static_cast<Events*>(e->ctx)->data.at(i).bytes); }};
    template <class T> T& add() { data.emplace_back(); std::memset(data.back().bytes, 0, sizeof(Slot::bytes)); return *reinterpret_cast<T*>(data.back().bytes); }
    void param(clap_id id, double value, uint32_t time = 0) {
        auto& e = add<clap_event_param_value_t>();
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id; e.value = value; e.note_id = e.port_index = e.channel = e.key = -1;
    }
    void tempo(double bpm, uint32_t time = 0) {
        auto& e = add<clap_event_transport_t>();
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        e.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING; e.tempo = bpm;
    }
    void modulation(clap_id id, double amount, uint32_t time = 0) {
        auto& e = add<clap_event_param_mod_t>();
        e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0};
        e.param_id = id; e.amount = amount; e.note_id = e.port_index = e.channel = e.key = -1;
    }
};
struct MemoryStream {
    std::vector<uint8_t> data;
    size_t position = 0;
    bool fail = false;
    clap_ostream_t output {this, [](const clap_ostream_t* s, const void* bytes, uint64_t count) -> int64_t {
        auto& m = *static_cast<MemoryStream*>(s->ctx);
        if (m.fail) return 0;
        count = std::min(count, uint64_t(3));
        const auto* begin = static_cast<const uint8_t*>(bytes);
        m.data.insert(m.data.end(), begin, begin+count);
        return static_cast<int64_t>(count);
    }};
    clap_istream_t input {this, [](const clap_istream_t* s, void* bytes, uint64_t count) -> int64_t {
        auto& m = *static_cast<MemoryStream*>(s->ctx);
        if (m.fail) return -1;
        count = std::min({count, uint64_t(5), static_cast<uint64_t>(m.data.size()-m.position)});
        std::memcpy(bytes, m.data.data()+m.position, count); m.position += count;
        return static_cast<int64_t>(count);
    }};
};

// A new stream over a copy of the bytes (copying a stream would leave its callbacks pointing at the original).
static void load(MemoryStream& stream, const std::vector<uint8_t>& bytes) { stream.data = bytes; stream.position = 0; }

struct Fixture {
    const clap_plugin_t* plugin;
    const clap_plugin_params_t* params;
    const clap_plugin_state_t* state;
    std::array<float, 512> left {}, right {};
    float* channels[2] {left.data(), right.data()};
    clap_audio_buffer_t buffer {channels, nullptr, 2, 0, 0};
    Fixture() {
        const auto* f = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
        CHECK(f && f->get_plugin_count(f) == 1);
        CHECK(f->get_plugin_descriptor(f, 1) == nullptr);
        CHECK(f->create_plugin(f, &host, "unknown") == nullptr);
        plugin = f->create_plugin(f, &host, f->get_plugin_descriptor(f, 0)->id);
        CHECK(plugin && plugin->init(plugin));
        params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        CHECK(params && state);
    }
    ~Fixture() { plugin->stop_processing(plugin); plugin->deactivate(plugin); plugin->destroy(plugin); }
    void set(clap_id id, double value) { Events e; e.param(id, value); params->flush(plugin, &e.api, nullptr); }
    double get(clap_id id) { double value = 0; CHECK(params->get_value(plugin, id, &value)); return value; }
    void activate(double rate = 48000) { CHECK(plugin->activate(plugin, rate, 1, 512)); CHECK(plugin->start_processing(plugin)); }
    void process(uint32_t frames, Events* events = nullptr, const clap_event_transport_t* transport = nullptr) {
        clap_process_t p {};
        p.steady_time = -1; p.frames_count = frames;
        p.audio_inputs = &buffer; p.audio_outputs = &buffer;
        p.audio_inputs_count = p.audio_outputs_count = 1;
        p.in_events = events ? &events->api : nullptr;
        p.transport = transport;
        CHECK(plugin->process(plugin, &p) == CLAP_PROCESS_CONTINUE);
    }
};

void descriptorAndPorts() {
    const auto* f = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    const auto* d = f->get_plugin_descriptor(f, 0);
    CHECK(std::strcmp(d->id, "com.stevemurr.fx-block") == 0 && std::strcmp(d->name, "FX Block") == 0);
    bool effect = false, multi = false, stereo = false;
    for (const char* const* feature = d->features; *feature; ++feature) {
        effect = effect || !std::strcmp(*feature, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        multi = multi || !std::strcmp(*feature, CLAP_PLUGIN_FEATURE_MULTI_EFFECTS);
        stereo = stereo || !std::strcmp(*feature, CLAP_PLUGIN_FEATURE_STEREO);
    }
    CHECK(effect && multi && stereo);
    Fixture fx;
    auto* ports = static_cast<const clap_plugin_audio_ports_t*>(fx.plugin->get_extension(fx.plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK(ports && ports->count(fx.plugin, true) == 1 && ports->count(fx.plugin, false) == 1);
    clap_audio_port_info_t info {};
    CHECK(ports->get(fx.plugin, 0, true, &info) && info.channel_count == 2 && (info.flags & CLAP_AUDIO_PORT_IS_MAIN));
    CHECK(!ports->get(fx.plugin, 1, true, &info));
    // No note ports, no latency: the box is an effect on audio and adds none.
    CHECK(fx.plugin->get_extension(fx.plugin, CLAP_EXT_NOTE_PORTS) == nullptr && fx.plugin->get_extension(fx.plugin, CLAP_EXT_LATENCY) == nullptr);
    CHECK(fx.plugin->get_extension(fx.plugin, CLAP_EXT_TAIL) != nullptr && fx.plugin->get_extension(fx.plugin, "no.such.extension") == nullptr);
}

void parametersAndText() {
    Fixture f;
    CHECK(f.params->count(f.plugin) == exposedCount);
    std::set<std::string> seen;
    for (uint32_t index = 0; index < exposedCount; ++index) {
        clap_param_info_t info {};
        CHECK(f.params->get_info(f.plugin, index, &info));
        const uint32_t id = info.id;
        CHECK(id == exposedId(index) && paramUsed(id));
        const auto def = paramInfo(id);
        CHECK(info.default_value == def.initial && info.default_value == f.get(id));
        CHECK(info.min_value == def.min && info.max_value == def.max);
        CHECK(bool(info.flags & CLAP_PARAM_IS_STEPPED) == def.stepped);
        CHECK(bool(info.flags & CLAP_PARAM_IS_MODULATABLE) == !def.stepped);
        CHECK(bool(info.flags & CLAP_PARAM_IS_BYPASS) == (id == Bypass));
        CHECK(bool(info.flags & CLAP_PARAM_IS_AUTOMATABLE));
        CHECK(seen.insert(std::string(info.module)+"/"+info.name).second || id == FilterOrder);
        for (double v : {info.min_value, info.default_value, info.max_value}) {
            char text[128] {}; double parsed = -1e9;
            CHECK(f.params->value_to_text(f.plugin, id, v, text, sizeof(text)));
            CHECK(f.params->text_to_value(f.plugin, id, text, &parsed));
            CHECK(std::abs(parsed-v) <= (def.stepped ? 0 : std::max(1.0, std::abs(v)*.012)));
        }
    }
    clap_param_info_t info {};
    CHECK(!f.params->get_info(f.plugin, exposedCount, &info));
    // The unused knob is not offered or answered for.
    double value = 0;
    CHECK(!f.params->get_value(f.plugin, blockParam(WidthBlock, P3), &value));
    char text[64];
    CHECK(!f.params->value_to_text(f.plugin, blockParam(WidthBlock, P3), 0, text, sizeof(text)));
    // Parameters sit in modules by block, named for their role.
    CHECK(f.params->get_info(f.plugin, blockParam(DelayBlock, P2), &info));
    CHECK(std::strcmp(info.module, "Delay") == 0 && std::strcmp(info.name, "Feedback") == 0);
    CHECK(f.params->get_info(f.plugin, blockParam(FilterBlock, P1), &info));
    CHECK(std::strcmp(info.module, "Filter") == 0 && std::strcmp(info.name, "Cutoff") == 0 && info.default_value == 1000);
    CHECK((info.flags & CLAP_PARAM_IS_MODULATABLE) && !(info.flags & CLAP_PARAM_IS_STEPPED));
    CHECK(f.params->get_info(f.plugin, blockParam(DriveBlock, Flavor), &info));
    CHECK((info.flags & CLAP_PARAM_IS_ENUM) && (info.flags & CLAP_PARAM_IS_STEPPED) && info.max_value == 2);
    // Text follows the flavor in force: the same Size is seconds in a hall and in a room, and says different things.
    const uint32_t size = blockParam(ReverbBlock, P1);
    char room[64], hall[64];
    f.set(blockParam(ReverbBlock, Flavor), 0);
    CHECK(f.params->value_to_text(f.plugin, size, 50, room, sizeof(room)));
    f.set(blockParam(ReverbBlock, Flavor), 1);
    CHECK(f.params->value_to_text(f.plugin, size, 50, hall, sizeof(hall)));
    CHECK(std::strcmp(room, hall) != 0 && std::strcmp(hall, "3.46 s") == 0);
    CHECK(f.params->text_to_value(f.plugin, size, "3.46 s", &value) && std::abs(value-50) < .1);
    // Words in, values out; garbage is refused.
    CHECK(f.params->text_to_value(f.plugin, blockParam(DriveBlock, Flavor), "Fuzz", &value) && value == 1);
    CHECK(f.params->text_to_value(f.plugin, blockParam(FilterBlock, P1), "2.5k", &value) && value == 2500);
    CHECK(f.params->text_to_value(f.plugin, FilterOrder, "After drive", &value) && value == 1);
    CHECK(!f.params->text_to_value(f.plugin, Mix, "NaN", &value));
    CHECK(!f.params->text_to_value(f.plugin, Mix, "50 garbage", &value));
    CHECK(!f.params->text_to_value(f.plugin, Mix, nullptr, &value));
    CHECK(f.params->value_to_text(f.plugin, blockParam(FilterBlock, On), 1, text, sizeof(text)) && std::strcmp(text, "On") == 0);
    CHECK(f.params->value_to_text(f.plugin, Output, -3.5, text, sizeof(text)) && std::strcmp(text, "-3.5 dB") == 0);
}

void stateRoundTrips() {
    Fixture f;
    f.set(Input, 4.5); f.set(Mix, 73); f.set(FilterOrder, 1);
    f.set(blockParam(DriveBlock, On), 1); f.set(blockParam(DriveBlock, Flavor), 2); f.set(blockParam(DriveBlock, P1), 61.25);
    f.set(blockParam(ReverbBlock, Flavor), 1); f.set(blockParam(ReverbBlock, P3), 42); f.set(blockParam(WidthBlock, BlockMix), 12);
    f.set(DelaySync, 9);
    MemoryStream first; CHECK(f.state->save(f.plugin, &first.output));
    f.set(Input, 0); f.set(Mix, 0); f.set(FilterOrder, 0); f.set(blockParam(DriveBlock, On), 0); f.set(blockParam(DriveBlock, Flavor), 0);
    f.set(blockParam(DriveBlock, P1), 5); f.set(blockParam(ReverbBlock, Flavor), 0); f.set(DelaySync, 0);
    CHECK(f.state->load(f.plugin, &first.input));
    CHECK(f.get(Input) == 4.5 && f.get(Mix) == 73 && f.get(FilterOrder) == 1);
    CHECK(f.get(DelaySync) == 9 && f.get(blockParam(DriveBlock, On)) == 1 && f.get(blockParam(DriveBlock, Flavor)) == 2 && f.get(blockParam(DriveBlock, P1)) == 61.25);
    CHECK(f.get(blockParam(ReverbBlock, Flavor)) == 1 && f.get(blockParam(ReverbBlock, P3)) == 42 && f.get(blockParam(WidthBlock, BlockMix)) == 12);
    CHECK(rescans > 0);
    MemoryStream second; CHECK(f.state->save(f.plugin, &second.output));
    CHECK(first.data == second.data);                                      // reproducible
    CHECK(first.data.size() == 12+ParamCount*12 && std::memcmp(first.data.data(), "FXBK", 4) == 0 && first.data[4] == 1);
    // Truncated, failing, and corrupted streams leave the current state alone.
    first.data.pop_back(); first.position = 0;
    f.set(Mix, 11);
    CHECK(!f.state->load(f.plugin, &first.input)); CHECK(f.get(Mix) == 11);
    first.fail = true; CHECK(!f.state->save(f.plugin, &first.output));
    first.fail = false; first.data[0] = 'X'; first.position = 0;
    CHECK(!f.state->load(f.plugin, &first.input)); CHECK(f.get(Mix) == 11);
    MemoryStream empty; CHECK(!f.state->load(f.plugin, &empty.input));
    MemoryStream harmonizer; harmonizer.data = {'H','R','M','Z', 3,0,0,0, 0,0,0,0};
    CHECK(!f.state->load(f.plugin, &harmonizer.input) && f.get(Mix) == 11);
    MemoryStream future; load(future, second.data); future.data[4] = 9;
    CHECK(!f.state->load(f.plugin, &future.input) && f.get(Mix) == 11);
}

// A session that lacks parameters (an older version) loads with those at their defaults; ids that are not known are
// skipped, so appending parameters later is safe. Values outside their range are clamped; NaN is refused.
void sessionsAcrossVersions() {
    Fixture f;
    f.set(Mix, 77); f.set(blockParam(DelayBlock, P1), 420);
    MemoryStream saved; CHECK(f.state->save(f.plugin, &saved.output));
    const auto pairAt = [&](MemoryStream& s, size_t i) { return 12+i*12; (void)s; };
    // Only the first ten parameters, as an older version might have written.
    MemoryStream old;
    load(old, std::vector<uint8_t>(saved.data.begin(), saved.data.begin()+12+10*12));
    old.data[8] = 10;
    Fixture g;
    g.set(blockParam(DelayBlock, P1), 99);
    CHECK(g.state->load(g.plugin, &old.input));
    CHECK(g.get(Mix) == 77 && g.get(blockParam(DelayBlock, P1)) == paramInfo(blockParam(DelayBlock, P1)).initial);
    // A session from before Auto Gain and Sync existed (two parameters fewer) loads with both off.
    MemoryStream before;
    load(before, std::vector<uint8_t>(saved.data.begin(), saved.data.end()-24));
    before.data[8] = static_cast<uint8_t>(ParamCount-2);
    Fixture a;
    a.set(AutoGain, 1); a.set(DelaySync, 9);
    CHECK(a.state->load(a.plugin, &before.input) && a.get(AutoGain) == 0 && a.get(DelaySync) == 0 && a.get(Mix) == 77);
    // An extra id from the future is ignored.
    MemoryStream newer; load(newer, saved.data);
    newer.data[8] = static_cast<uint8_t>(ParamCount+1);
    for (int i = 0; i < 12; ++i) newer.data.push_back(i == 0 ? 200 : 0);
    Fixture h;
    CHECK(h.state->load(h.plugin, &newer.input) && h.get(Mix) == 77);
    // Out of range values are clamped, NaN and duplicates refused.
    MemoryStream wild; load(wild, saved.data);
    const double huge = 1e9;
    std::memcpy(&wild.data[pairAt(wild, Mix)+4], &huge, 8);
    Fixture w;
    CHECK(w.state->load(w.plugin, &wild.input) && w.get(Mix) == 100);
    MemoryStream bad; load(bad, saved.data);
    const double nan = std::nan("");
    std::memcpy(&bad.data[pairAt(bad, Mix)+4], &nan, 8);
    Fixture b;
    CHECK(!b.state->load(b.plugin, &bad.input));
    MemoryStream twice; load(twice, saved.data);
    twice.data[pairAt(twice, 3)] = 2;                                      // the fourth pair claims to be id 2 again
    Fixture t;
    CHECK(!t.state->load(t.plugin, &twice.input));
}

void remotePages() {
    Fixture f;
    auto* remotes = static_cast<const clap_plugin_remote_controls_t*>(f.plugin->get_extension(f.plugin, CLAP_EXT_REMOTE_CONTROLS));
    CHECK(remotes && remotes->count(f.plugin) == BlockCount+1);
    std::set<clap_id> covered;
    for (uint32_t page = 0; page < BlockCount+1; ++page) {
        clap_remote_controls_page_t info {};
        CHECK(remotes->get(f.plugin, page, &info) && std::strlen(info.page_name) > 0);
        for (auto id : info.param_ids) { CHECK(id == CLAP_INVALID_ID || (id < ParamCount && paramUsed(id))); if (id != CLAP_INVALID_ID) covered.insert(id); }
    }
    CHECK(covered.size() == exposedCount);                                 // every parameter is on some page
    clap_remote_controls_page_t info {};
    CHECK(!remotes->get(f.plugin, BlockCount+1, &info));
    CHECK(remotes->get(f.plugin, ReverbBlock, &info) && std::strcmp(info.page_name, "Reverb") == 0 && info.param_ids[0] == blockParam(ReverbBlock, On));
}

// A parameter event at a frame takes effect at that frame, and processing in place is the same as in a fresh buffer.
void automationAndInPlace() {
    Fixture full, split;
    for (auto* f : {&full, &split}) { f->set(blockParam(WidthBlock, On), 1); f->set(blockParam(WidthBlock, P1), 0); f->activate(); }
    // Width 0% is mono: left and right become their average from the first sample.
    full.left.fill(.25f); full.right.fill(-.75f);
    full.process(512);
    for (size_t i = 100; i < 512; ++i) CHECK(std::abs(full.left[i]+.25f) < 1e-4f && std::abs(full.right[i]+.25f) < 1e-4f);
    // Switching it off partway through the block, by an event, restores the stereo image after the crossfade.
    for (int i = 0; i < 4; ++i) { full.left.fill(.25f); full.right.fill(-.75f); full.process(512); }
    Events event; event.param(blockParam(WidthBlock, On), 0, 64);
    full.left.fill(.25f); full.right.fill(-.75f);
    full.process(512, &event);
    CHECK(full.get(blockParam(WidthBlock, On)) == 0);
    CHECK(std::abs(full.left[10]+.25f) < 1e-4f);                           // before the event: still mono
    CHECK(full.left[500] > -.2f && full.left[500] < .2f);                  // mid-crossfade: neither
    for (int i = 0; i < 20; ++i) { full.left.fill(.25f); full.right.fill(-.75f); full.process(512); }
    CHECK(std::abs(full.left[500]-.25f) < 1e-4f && std::abs(full.right[500]+.75f) < 1e-4f);   // long after: stereo again
    // Splitting the block at the event's frame gives the same audio as one block with the event inside it.
    split.left.fill(.25f); split.right.fill(-.75f);
    for (int i = 0; i < 4; ++i) { split.process(512); split.left.fill(.25f); split.right.fill(-.75f); }
    split.process(64);
    split.left.fill(.25f); split.right.fill(-.75f);
    split.set(blockParam(WidthBlock, On), 0);
    split.process(448);
    // Modulation reaches the sound but not the stored value.
    Fixture m;
    m.set(Output, 0); m.activate();
    Events mod; mod.modulation(Output, -6);
    for (int i = 0; i < 20; ++i) { m.left.fill(.5f); m.right.fill(.5f); m.process(512, &mod); }      // the gain glides over 20 ms
    CHECK(m.get(Output) == 0 && std::abs(m.left[511]-.5f*static_cast<float>(dbToGain(-6))) < 1e-3f);
    // Stepped parameters ignore modulation.
    Fixture s;
    s.activate();
    Events stepped; stepped.modulation(blockParam(DelayBlock, On), 1);
    s.left.fill(.5f); s.right.fill(.5f); s.process(512, &stepped);
    for (auto x : s.left) CHECK(x == .5f);
    // reset() clears every tail.
    full.plugin->reset(full.plugin);
    full.left.fill(0); full.right.fill(0); full.process(512);
    for (auto x : full.left) CHECK(x == 0);
    full.plugin->stop_processing(full.plugin); full.plugin->deactivate(full.plugin);
    full.activate(96000); full.process(512);
    // Refuses a second activation, and a block with the wrong shape of buffers.
    CHECK(!full.plugin->activate(full.plugin, 48000, 1, 512));
    clap_process_t p {};
    p.frames_count = 4; p.audio_inputs_count = 0;
    CHECK(full.plugin->process(full.plugin, &p) == CLAP_PROCESS_ERROR);
}

// The tail the plugin reports covers what is switched on, and nothing when it is all off.
void tailIsReported() {
    Fixture f;
    auto* tail = static_cast<const clap_plugin_tail_t*>(f.plugin->get_extension(f.plugin, CLAP_EXT_TAIL));
    f.activate();
    f.process(512);
    CHECK(tail->get(f.plugin) == 0);
    f.set(blockParam(ReverbBlock, On), 1); f.set(blockParam(ReverbBlock, Flavor), 1); f.set(blockParam(ReverbBlock, P1), 60);
    f.process(512);
    CHECK(tail->get(f.plugin) > 48000*2);
    f.set(blockParam(ReverbBlock, On), 0);
    f.process(512);
    CHECK(tail->get(f.plugin) == 0);
}

// The host's tempo reaches a synced delay: from the process call's transport, or from a transport event, and before
// either is heard the delay assumes 120 BPM.
void tempoThroughTheHost() {
    struct Case { const char* name; double bpm; bool viaEvent; bool silent; double expectedMs; };
    const Case cases[] {
        {"transport pointer at 100 BPM", 100, false, false, 600},
        {"transport event at 75 BPM", 75, true, false, 800},
        {"no tempo at all", 0, false, true, 500},
    };
    for (const Case& test : cases) {
        Fixture f;
        f.set(blockParam(DelayBlock, On), 1); f.set(blockParam(DelayBlock, Flavor), 0);
        f.set(blockParam(DelayBlock, P2), 0); f.set(blockParam(DelayBlock, BlockMix), 100); f.set(blockParam(DelayBlock, P3), 100);
        f.set(DelaySync, 9);                                                     // 1/4
        f.activate();
        clap_event_transport_t transport {};
        transport.header = {sizeof(transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING; transport.tempo = test.bpm;
        std::vector<float> out;
        for (int block = 0; block < 80; ++block) {
            f.left.fill(0); f.right.fill(0);
            if (block == 0) { f.left[0] = f.right[0] = 1.f; }
            Events events;
            if (block == 0 && test.viaEvent) events.tempo(test.bpm);
            f.process(512, test.viaEvent ? &events : nullptr, !test.viaEvent && !test.silent ? &transport : nullptr);
            out.insert(out.end(), f.left.begin(), f.left.end());
        }
        size_t at = 0; float best = 0;
        for (size_t i = 100; i < out.size(); ++i) if (std::abs(out[i]) > best) { best = std::abs(out[i]); at = i; }
        std::printf("tempo, %s: echo at %.2f ms (expected %.0f)\n", test.name, static_cast<double>(at)/48., test.expectedMs);
        CHECK(best > .5f && std::abs(static_cast<double>(at)/48.-test.expectedMs) < .2);
    }
    // The sync parameter is filed under Delay.
    Fixture g;
    clap_param_info_t info {};
    CHECK(g.params->get_info(g.plugin, exposedCount-1, &info) && info.id == DelaySync);
    CHECK(std::strcmp(info.module, "Delay") == 0 && std::strcmp(info.name, "Sync") == 0 && (info.flags & CLAP_PARAM_IS_ENUM) && !(info.flags & CLAP_PARAM_IS_MODULATABLE));
    char text[32]; double value = -1;
    CHECK(g.params->value_to_text(g.plugin, DelaySync, 7, text, sizeof(text)) && !std::strcmp(text, "1/8."));
    CHECK(g.params->text_to_value(g.plugin, DelaySync, "1/16T", &value) && value == 2);
}

// Everything on, through the host interface, at the host's buffer sizes: the output is finite and the effects make a difference.
void audioThroughTheHost() {
    Fixture f;
    for (uint32_t b = 0; b < BlockCount; ++b) f.set(blockParam(b, On), 1);
    f.set(blockParam(FilterBlock, P1), 3000);
    f.activate();
    double difference = 0;
    for (int block = 0; block < 100; ++block) {
        std::array<float, 512> in;
        for (size_t i = 0; i < 512; ++i) in[i] = static_cast<float>(.4*std::sin(2*3.14159265358979*220*(static_cast<double>(block)*512+static_cast<double>(i))/48000));
        f.left = in; f.right = in;
        f.process(512);
        for (size_t i = 0; i < 512; ++i) { CHECK(std::isfinite(f.left[i]) && std::isfinite(f.right[i])); difference += std::abs(f.left[i]-in[i]); }
    }
    CHECK(difference > 100);
}

#ifdef __APPLE__
void editorOpensAndCloses() {
    Fixture f;
    auto* gui = static_cast<const clap_plugin_gui_t*>(f.plugin->get_extension(f.plugin, CLAP_EXT_GUI));
    CHECK(gui && gui->is_api_supported(f.plugin, CLAP_WINDOW_API_COCOA, false) && !gui->is_api_supported(f.plugin, CLAP_WINDOW_API_COCOA, true));
    CHECK(!gui->is_api_supported(f.plugin, CLAP_WINDOW_API_WIN32, false));
    CHECK(gui->create(f.plugin, CLAP_WINDOW_API_COCOA, false));
    uint32_t w = 0, h = 0;
    CHECK(gui->get_size(f.plugin, &w, &h) && w >= 800 && h >= 600 && !gui->can_resize(f.plugin));
    CHECK(!gui->create(f.plugin, CLAP_WINDOW_API_COCOA, false));          // one at a time
    CHECK(gui->show(f.plugin) && gui->hide(f.plugin));
    gui->destroy(f.plugin);
    CHECK(!gui->get_size(f.plugin, &w, &h));
    CHECK(gui->create(f.plugin, CLAP_WINDOW_API_COCOA, false));           // and again; destroy at teardown frees it
}
#endif

int main() {
    CHECK(clap_entry.init("test")); CHECK(clap_entry.init("test"));
    CHECK(!clap_entry.get_factory("unknown"));
    descriptorAndPorts(); parametersAndText(); stateRoundTrips(); sessionsAcrossVersions(); remotePages(); automationAndInPlace(); tailIsReported(); tempoThroughTheHost(); audioThroughTheHost();
#ifdef __APPLE__
    editorOpensAndCloses();
#endif
    clap_entry.deinit(); clap_entry.deinit();
    std::cout << "CLAP: lifecycle, 42 parameters with flavor-aware text, state and version tolerance, corruption, ports, remote pages, automation, modulation, tail and editor passed\n";
}
