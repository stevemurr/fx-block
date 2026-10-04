#include "Editor.h"
#include "Chain.h"
#include "UiQueue.h"
#include <clap/clap.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
using namespace fxblock;
static_assert(std::atomic<double>::is_always_lock_free, "Parameters must be lock-free on the audio thread");
const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MULTI_EFFECTS, CLAP_PLUGIN_FEATURE_FILTER,
    CLAP_PLUGIN_FEATURE_DISTORTION, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_REVERB, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t descriptor {CLAP_VERSION, "com.stevemurr.fx-block", "FX Block",
    "Steve Murr", "https://github.com/stevemurr/fx-block", "", "", "1.0.0",
    "Swiss-army effects box: filter, drive, modulation, delay, reverb and width, each in three flavors", features};

struct Plugin {
    clap_plugin_t api {};
    const clap_host_t* host;
    const clap_host_params_t* hostParams = nullptr;
    Chain dsp;
    std::array<std::atomic<double>, ParamCount> values {};
    Values modulation {};
    UiQueue uiQueue;
    Editor* editor = nullptr;
    TripleBuffer<Status> statusBuffer;
    Status shownStatus;
    bool active = false, processing = false;

    explicit Plugin(const clap_host_t* h) : host(h) {
        const auto initial = defaults();
        for (uint32_t id = 0; id < ParamCount; ++id) values[id].store(initial[id]);
        api = {&descriptor, this, init, destroy, activate, deactivate, start, stop, reset, process, extension, mainThread};
    }
    static Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }
    Values snapshot(bool modulate = false) const noexcept {
        Values result {};
        for (uint32_t id = 0; id < ParamCount; ++id)
            result[id] = sanitize(id, values[id].load(std::memory_order_relaxed) + (modulate ? modulation[id] : 0));
        return result;
    }
    static bool CLAP_ABI init(const clap_plugin_t* p) {
        auto& s = self(p);
        if (s.host->get_extension)
            s.hostParams = static_cast<const clap_host_params_t*>(s.host->get_extension(s.host, CLAP_EXT_PARAMS));
        return true;
    }
    static void CLAP_ABI destroy(const clap_plugin_t* p) { auto& s = self(p); delete s.editor; delete &s; }
    static bool CLAP_ABI activate(const clap_plugin_t* p, double rate, uint32_t min, uint32_t max) {
        auto& s = self(p);
        if (s.active || min == 0 || max < min) return false;
        try {
            s.modulation.fill(0);
            s.dsp.prepare(rate);
            s.dsp.set(s.snapshot(), true);
            s.active = true;
            return true;
        } catch (...) { return false; } // No C++ exceptions cross the C ABI.
    }
    static void CLAP_ABI deactivate(const clap_plugin_t* p) { self(p).active = self(p).processing = false; }
    static bool CLAP_ABI start(const clap_plugin_t* p) { return self(p).processing = self(p).active; }
    static void CLAP_ABI stop(const clap_plugin_t* p) { self(p).processing = false; }
    static void CLAP_ABI reset(const clap_plugin_t* p) {
        auto& s = self(p);
        s.modulation.fill(0);
        s.dsp.reset();
        s.dsp.set(s.snapshot(), true);
    }
    void event(const clap_event_header_t* header) noexcept {
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (header->type) {
        case CLAP_EVENT_PARAM_VALUE:
            if (header->size >= sizeof(clap_event_param_value_t)) {
                const auto& e = *reinterpret_cast<const clap_event_param_value_t*>(header);
                if (e.param_id < ParamCount && e.note_id == -1 && e.port_index == -1 && e.channel == -1 && e.key == -1 && std::isfinite(e.value))
                    values[e.param_id].store(sanitize(e.param_id, e.value), std::memory_order_relaxed);
            }
            break;
        case CLAP_EVENT_PARAM_MOD:
            if (header->size >= sizeof(clap_event_param_mod_t)) {
                const auto& e = *reinterpret_cast<const clap_event_param_mod_t*>(header);
                if (e.param_id < ParamCount && !paramInfo(e.param_id).stepped && e.note_id == -1 && e.port_index == -1 && e.channel == -1 && e.key == -1 && std::isfinite(e.amount))
                    modulation[e.param_id] = e.amount;
            }
            break;
        default: break;
        }
    }
    void drainUi(const clap_output_events_t* out) noexcept {
        while (const auto* change = uiQueue.peek()) {
            if (!out || !out->try_push) break;
            bool accepted;
            if (change->kind == 0) {
                clap_event_param_value_t e {};
                e.header = {sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,CLAP_EVENT_IS_LIVE};
                e.param_id = change->id; e.value = change->value;
                e.note_id = e.port_index = e.channel = e.key = -1;
                accepted = out->try_push(out,&e.header);
                if (accepted) values[change->id].store(change->value,std::memory_order_relaxed);
            } else {
                clap_event_param_gesture_t e {};
                e.header = {sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,static_cast<uint16_t>(change->kind == 1 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END),CLAP_EVENT_IS_LIVE};
                e.param_id = change->id;
                accepted = out->try_push(out,&e.header);
            }
            if (!accepted) break;
            uiQueue.pop();
        }
    }
    bool edit(uint32_t id, double value, int kind) {
        if (id >= ParamCount || !uiQueue.push({id,sanitize(id,value),kind})) return false;
        if (hostParams && hostParams->request_flush) hostParams->request_flush(host);
        else if (host->request_process) host->request_process(host);
        return true;
    }
    static clap_process_status CLAP_ABI process(const clap_plugin_t* p, const clap_process_t* block) {
        auto& s = self(p);
        if (!s.processing || !block || block->audio_inputs_count != 1 || block->audio_outputs_count != 1 ||
            !block->audio_inputs || !block->audio_outputs) return CLAP_PROCESS_ERROR;
        const auto& in = block->audio_inputs[0];
        auto& out = block->audio_outputs[0];
        if (in.channel_count != 2 || out.channel_count != 2 || !in.data32 || !out.data32 ||
            !in.data32[0] || !in.data32[1] || !out.data32[0] || !out.data32[1]) return CLAP_PROCESS_ERROR;
        out.constant_mask = 0;
        s.drainUi(block->out_events);
        s.dsp.set(s.snapshot(true));
        const auto* events = block->in_events;
        const uint32_t count = events ? events->size(events) : 0;
        uint32_t next = 0;
        const clap_event_header_t* pending = count ? events->get(events, 0) : nullptr;
        for (uint32_t i = 0; i < block->frames_count; ++i) {
            bool changed = false;
            while (next < count && (!pending || pending->time <= i)) {
                s.event(pending); changed = true;
                pending = ++next < count ? events->get(events, next) : nullptr;
            }
            if (changed) s.dsp.set(s.snapshot(true));
            const Stereo input {in.data32[0][i], in.data32[1][i]};
            const auto sample = s.dsp.process(input);
            out.data32[0][i] = sample.l; out.data32[1][i] = sample.r;
        }
        s.statusBuffer.writeSlot() = s.dsp.status();
        s.statusBuffer.publish();
        return CLAP_PROCESS_CONTINUE;
    }
    static void CLAP_ABI mainThread(const clap_plugin_t*) {}
    static const void* CLAP_ABI extension(const clap_plugin_t*, const char* id);
};

uint32_t CLAP_ABI portCount(const clap_plugin_t*, bool) { return 1; }
bool CLAP_ABI portInfo(const clap_plugin_t*, uint32_t index, bool input, clap_audio_port_info_t* info) {
    if (index != 0 || !info) return false;
    *info = {};
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", input ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;
    return true;
}
const clap_plugin_audio_ports_t audioPorts {portCount, portInfo};

// The Width block's unused third knob is not offered, so the host's index and the parameter id differ by one past it.
void moduleName(uint32_t id, char* out, size_t size) {
    if (isBlockParam(id)) std::snprintf(out, size, "%s", blockNames[blockOf(id)]);
    else if (id == FilterOrder) std::snprintf(out, size, "%s", blockNames[FilterBlock]);
    else std::snprintf(out, size, "%s", "Master");
}
void paramName(uint32_t id, char* out, size_t size) {
    if (!isBlockParam(id)) { std::snprintf(out, size, "%s", paramInfo(id).name); return; }
    const BlockKey key = keyOf(id);
    if (key >= P1 && key <= P3) std::snprintf(out, size, "%s", knobNames[blockOf(id)][key-P1]);
    else std::snprintf(out, size, "%s", paramInfo(id).name);
}
bool isLabelled(uint32_t id) { return id == Bypass || id == FilterOrder || id == AutoGain || (isBlockParam(id) && (keyOf(id) == On || keyOf(id) == Flavor)); }
uint32_t flavorOf(const Plugin& p, uint32_t id) {
    return isBlockParam(id) ? static_cast<uint32_t>(p.values[blockParam(blockOf(id), Flavor)].load(std::memory_order_relaxed)) : 0;
}

uint32_t CLAP_ABI paramCount(const clap_plugin_t*) { return exposedCount; }
bool CLAP_ABI paramGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index >= exposedCount || !info) return false;
    const uint32_t id = exposedId(index);
    const auto def = paramInfo(id);
    *info = {};
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    else info->flags |= CLAP_PARAM_IS_MODULATABLE;
    if (isLabelled(id)) info->flags |= CLAP_PARAM_IS_ENUM;
    if (id == Bypass) info->flags |= CLAP_PARAM_IS_BYPASS;
    paramName(id, info->name, sizeof(info->name));
    moduleName(id, info->module, sizeof(info->module));
    info->min_value = def.min; info->max_value = def.max; info->default_value = def.initial;
    return true;
}
bool CLAP_ABI paramValue(const clap_plugin_t* p, clap_id id, double* value) {
    if (id >= ParamCount || !paramUsed(id) || !value) return false;
    *value = Plugin::self(p).values[id].load(std::memory_order_relaxed);
    return true;
}
bool CLAP_ABI valueText(const clap_plugin_t* p, clap_id id, double value, char* out, uint32_t capacity) {
    if (id >= ParamCount || !paramUsed(id) || !out || !capacity || !std::isfinite(value)) return false;
    return formatValue(out, capacity, id, value, flavorOf(Plugin::self(p), id)) >= 0;
}
bool CLAP_ABI textValue(const clap_plugin_t* p, clap_id id, const char* text, double* out) {
    if (id >= ParamCount || !paramUsed(id) || !text || !out) return false;
    return parseValue(id, text, flavorOf(Plugin::self(p), id), *out);
}
void CLAP_ABI flush(const clap_plugin_t* p, const clap_input_events_t* events, const clap_output_events_t* out) {
    auto& s = Plugin::self(p);
    s.drainUi(out);
    if (!events) return;
    for (uint32_t i = 0, n = events->size(events); i < n; ++i) s.event(events->get(events, i));
}
const clap_plugin_params_t params {paramCount, paramGetInfo, paramValue, valueText, textValue, flush};

// A versioned little-endian wire format, independent of struct padding and ABI:
//   "FXBK", version, parameter count, then (id u32, value f64) pairs.
// Parameters not in the stream keep their defaults, and ids this version does not know are skipped, so sessions
// survive parameters being appended.
constexpr uint32_t stateVersion = 1;
void encode(uint8_t* to, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) to[i] = static_cast<uint8_t>(value >> (8*i));
}
uint64_t decode(const uint8_t* from, size_t bytes) {
    uint64_t result = 0;
    for (size_t i = 0; i < bytes; ++i) result |= static_cast<uint64_t>(from[i]) << (8*i);
    return result;
}
bool readAll(const clap_istream_t* stream, uint8_t* dst, size_t size) {
    while (size) {
        const auto n = stream->read(stream, dst, size);
        if (n <= 0 || static_cast<uint64_t>(n) > size) return false;
        dst += n; size -= static_cast<size_t>(n);
    }
    return true;
}
bool CLAP_ABI save(const clap_plugin_t* p, const clap_ostream_t* stream) {
    if (!stream || !stream->write) return false;
    auto& s = Plugin::self(p);
    constexpr size_t total = 12+ParamCount*12;
    std::array<uint8_t, total> data {};
    std::memcpy(data.data(), "FXBK", 4);
    encode(data.data()+4, stateVersion, 4); encode(data.data()+8, ParamCount, 4);
    const auto values = s.snapshot();
    for (uint32_t i = 0; i < ParamCount; ++i) {
        encode(data.data()+12+i*12, i, 4);
        uint64_t bits = 0;
        std::memcpy(&bits, &values[i], 8);
        encode(data.data()+16+i*12, bits, 8);
    }
    size_t offset = 0;
    while (offset < total) {
        const auto n = stream->write(stream, data.data()+offset, total-offset);
        if (n <= 0 || static_cast<uint64_t>(n) > total-offset) return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}
bool CLAP_ABI load(const clap_plugin_t* p, const clap_istream_t* stream) {
    if (!stream || !stream->read) return false;
    std::array<uint8_t, 12> data {};
    if (!readAll(stream, data.data(), data.size()) || std::memcmp(data.data(), "FXBK", 4) != 0) return false;
    if (decode(data.data()+4, 4) != stateVersion) return false;
    const auto count = decode(data.data()+8, 4);
    if (count > 4096) return false;
    Values restored = defaults();
    std::array<bool, ParamCount> seen {};
    for (uint64_t i = 0; i < count; ++i) {
        if (!readAll(stream, data.data(), data.size())) return false;
        const auto id = decode(data.data(), 4);
        const uint64_t bits = decode(data.data()+4, 8);
        double value = 0;
        std::memcpy(&value, &bits, 8);
        if (!std::isfinite(value)) return false;
        if (id < ParamCount) {
            if (seen[id]) return false;
            seen[id] = true;
            restored[id] = sanitize(static_cast<uint32_t>(id), value);
        }
    }
    auto& s = Plugin::self(p);
    for (uint32_t id = 0; id < ParamCount; ++id) s.values[id].store(restored[id], std::memory_order_relaxed);
    if (s.hostParams && s.hostParams->rescan) s.hostParams->rescan(s.host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}
const clap_plugin_state_t state {save, load};
// The effects keep sounding after the input ends: a reverb's tail, a delay's repeats.
uint32_t CLAP_ABI tailGet(const clap_plugin_t* p) { return static_cast<uint32_t>(Plugin::self(p).dsp.tailSamples()); }
const clap_plugin_tail_t tail {tailGet};

// One page per block (on, flavor, the three knobs, mix) and one for the whole box.
constexpr uint32_t remotePageCount = BlockCount+1;
uint32_t CLAP_ABI remoteCount(const clap_plugin_t*) { return remotePageCount; }
bool CLAP_ABI remotePage(const clap_plugin_t*, uint32_t index, clap_remote_controls_page_t* page) {
    if (!page || index >= remotePageCount) return false;
    *page = {};
    page->page_id = index;
    std::snprintf(page->section_name, sizeof(page->section_name), "FX Block");
    std::array<clap_id, CLAP_REMOTE_CONTROLS_COUNT> ids;
    ids.fill(CLAP_INVALID_ID);
    if (index == BlockCount) {
        std::snprintf(page->page_name, sizeof(page->page_name), "Master");
        const clap_id master[] {Input, Output, Mix, Bypass, AutoGain, FilterOrder};
        std::copy(std::begin(master), std::end(master), ids.begin());
    } else {
        std::snprintf(page->page_name, sizeof(page->page_name), "%s", blockNames[index]);
        const clap_id keys[] {blockParam(index, On), blockParam(index, Flavor), blockParam(index, P1), blockParam(index, P2),
            index == WidthBlock ? CLAP_INVALID_ID : blockParam(index, P3), blockParam(index, BlockMix)};
        std::copy(std::begin(keys), std::end(keys), ids.begin());
    }
    std::copy(ids.begin(), ids.end(), page->param_ids);
    return true;
}
const clap_plugin_remote_controls_t remotes {remoteCount, remotePage};
#ifdef __APPLE__
bool CLAP_ABI guiSupported(const clap_plugin_t*, const char* api, bool floating) {
    return api && !floating && !std::strcmp(api,CLAP_WINDOW_API_COCOA);
}
bool CLAP_ABI guiPreferred(const clap_plugin_t*, const char** api, bool* floating) {
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA; *floating = false; return true;
}
bool CLAP_ABI guiCreate(const clap_plugin_t* p, const char* api, bool floating) {
    auto& s = Plugin::self(p);
    if (s.editor || !guiSupported(p,api,floating)) return false;
    EditorCallbacks callbacks;
    callbacks.context = &s;
    callbacks.get = [](void* ctx, uint32_t id) { return static_cast<Plugin*>(ctx)->values[id].load(std::memory_order_relaxed); };
    callbacks.edit = [](void* ctx, uint32_t id, double value, int kind) { return static_cast<Plugin*>(ctx)->edit(id,value,kind); };
    callbacks.status = [](void* ctx, Status* out) {
        auto* plugin = static_cast<Plugin*>(ctx);
        if (plugin->statusBuffer.acquire()) plugin->shownStatus = plugin->statusBuffer.readSlot();
        *out = plugin->shownStatus;
    };
    try { s.editor = Editor::create(callbacks); } catch (...) { return false; }
    return s.editor != nullptr;
}
void CLAP_ABI guiDestroy(const clap_plugin_t* p) { auto& s = Plugin::self(p); delete s.editor; s.editor = nullptr; }
bool CLAP_ABI guiScale(const clap_plugin_t*, double) { return false; }
bool CLAP_ABI guiSize(const clap_plugin_t* p, uint32_t* w, uint32_t* h) {
    if (!Plugin::self(p).editor || !w || !h) return false;
    *w = Editor::width; *h = Editor::height; return true;
}
bool CLAP_ABI guiCanResize(const clap_plugin_t*) { return false; }
bool CLAP_ABI guiHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool CLAP_ABI guiAdjust(const clap_plugin_t*, uint32_t* w, uint32_t* h) {
    if (!w || !h) return false;
    *w = Editor::width; *h = Editor::height; return true;
}
bool CLAP_ABI guiSetSize(const clap_plugin_t* p, uint32_t w, uint32_t h) { return Plugin::self(p).editor && w == Editor::width && h == Editor::height; }
bool CLAP_ABI guiParent(const clap_plugin_t* p, const clap_window_t* window) {
    auto* editor = Plugin::self(p).editor;
    return editor && window && window->api && !std::strcmp(window->api,CLAP_WINDOW_API_COCOA) && window->cocoa && editor->setParent(window->cocoa);
}
bool CLAP_ABI guiTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void CLAP_ABI guiTitle(const clap_plugin_t*, const char*) {}
bool CLAP_ABI guiShow(const clap_plugin_t* p) { auto* e = Plugin::self(p).editor; if (!e) return false; e->show(true); return true; }
bool CLAP_ABI guiHide(const clap_plugin_t* p) { auto* e = Plugin::self(p).editor; if (!e) return false; e->show(false); return true; }
const clap_plugin_gui_t gui {guiSupported,guiPreferred,guiCreate,guiDestroy,guiScale,guiSize,guiCanResize,guiHints,guiAdjust,guiSetSize,guiParent,guiTransient,guiTitle,guiShow,guiHide};
#endif
const void* CLAP_ABI Plugin::extension(const clap_plugin_t*, const char* id) {
    if (!id) return nullptr;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audioPorts;
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
    if (!std::strcmp(id, CLAP_EXT_TAIL)) return &tail;
    if (!std::strcmp(id, CLAP_EXT_REMOTE_CONTROLS) || !std::strcmp(id, CLAP_EXT_REMOTE_CONTROLS_COMPAT)) return &remotes;
#ifdef __APPLE__
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &gui;
#endif
    return nullptr;
}
uint32_t CLAP_ABI pluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* CLAP_ABI pluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* CLAP_ABI createPlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!host || !id || !clap_version_is_compatible(host->clap_version) || std::strcmp(id, descriptor.id)) return nullptr;
    try { return &(new Plugin(host))->api; } catch (...) { return nullptr; }
}
const clap_plugin_factory_t factory {pluginCount, pluginDescriptor, createPlugin};
bool CLAP_ABI entryInit(const char*) { return true; }
void CLAP_ABI entryDeinit() {}
const void* CLAP_ABI getFactory(const char* id) { return id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr; }
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {CLAP_VERSION, entryInit, entryDeinit, getFactory};
