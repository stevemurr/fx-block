// Renders the editor to a PNG without a plugin host, or smoke-tests its interactions headlessly.
//   editor_snapshot out.png [stage=delay delay.sync=1/8. tempo=96 filter.on=1 filter.flavor=bandpass drive.p1=70 delay.flavor=tape input=3 order=1 ...]
//   editor_snapshot --selftest
// Keys are <block>.<on|flavor|p1|p2|p3|mix> (blocks: filter drive modulation delay reverb width) or input, output, mix,
// bypass, order, autogain. stage= chooses the panel shown (a block's name). Values are numbers or what the plugin shows ("1.5k", "fuzz", "8 bit", "3 s"). Status overrides: in out.
#import <AppKit/AppKit.h>
#include "Editor.h"
#include "Parameters.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace fxblock;
static Values values = defaults();
static Status status;
static int gestureBegins = 0, gestureEnds = 0, shownBlock = 0;

static std::string lower(const char* text) {
    std::string out;
    for (const char* c = text; *c; ++c) out += static_cast<char>(std::tolower(*c));
    return out;
}
static int findParam(const std::string& name) {
    if (name == "input") return Input;
    if (name == "output") return Output;
    if (name == "mix") return Mix;
    if (name == "bypass") return Bypass;
    if (name == "order") return FilterOrder;
    if (name == "autogain") return AutoGain;
    if (name == "delay.sync") return DelaySync;
    const size_t dot = name.find('.');
    if (dot == std::string::npos) return -1;
    const std::string block = name.substr(0, dot), key = name.substr(dot+1);
    int b = -1;
    for (uint32_t i = 0; i < BlockCount; ++i) if (block == lower(blockNames[i]) || (block == "mod" && i == ModBlock)) b = static_cast<int>(i);
    if (b < 0) return -1;
    const struct { const char* name; BlockKey key; } keys[] {{"on", On}, {"flavor", Flavor}, {"p1", P1}, {"p2", P2}, {"p3", P3}, {"mix", BlockMix}};
    for (const auto& k : keys) if (key == k.name) return static_cast<int>(blockParam(static_cast<uint32_t>(b), k.key));
    return -1;
}

// ---------------------------------------------------------------- headless interaction checks
static int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::fprintf(stderr, "selftest %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (false)

static void collect(NSView* view, Class cls, NSMutableArray<NSView*>* into) {
    if ([view isKindOfClass:cls]) [into addObject:view];
    for (NSView* sub in view.subviews) collect(sub, cls, into);
}
static NSArray<NSView*>* find(NSView* root, NSString* className) {
    auto* out = [NSMutableArray<NSView*> array];
    collect(root, NSClassFromString(className), out);
    return out;
}
static NSEvent* mouse(NSWindow* window, NSView* view, NSPoint local, NSEventType type, NSInteger clicks = 1) {
    return [NSEvent mouseEventWithType:type location:[view convertPoint:local toView:nil] modifierFlags:0 timestamp:0
        windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:clicks pressure:1];
}
static void click(NSWindow* window, NSView* view, NSPoint local, NSInteger clicks = 1) {
    [view mouseDown:mouse(window, view, local, NSEventTypeLeftMouseDown, clicks)];
    [view mouseUp:mouse(window, view, local, NSEventTypeLeftMouseUp, clicks)];
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
static void refresh(NSView* root) { [root performSelector:NSSelectorFromString(@"refresh")]; }
static id call(id target, SEL selector, id argument = nil) { return [target performSelector:selector withObject:argument]; }
static void type(NSView* knob, NSTextField* field) { [knob performSelector:NSSelectorFromString(@"typed:") withObject:field]; }
#pragma clang diagnostic pop

@protocol FXViewTesting
- (void)showBlock:(uint32_t)block;
@end

static NSView* direct(NSView* root, NSString* className) {                 // the one such view that is a child of root itself
    for (NSView* v in find(root, className)) if (v.superview == root) return v;
    return nil;
}

static int selftest(NSView* root, NSWindow* window) {
    refresh(root);
    NSArray<NSView*>* panels = find(root, @"FXBlockPanel");
    NSArray<NSView*>* chips = find(root, @"FXChip");
    EXPECT(panels.count == BlockCount && chips.count == BlockCount);
    EXPECT(find(root, @"FXToggle").count == 2);                           // Auto Gain and Bypass; the blocks' switches are the chips' lights
    EXPECT(find(root, @"FXPicker").count == BlockCount+2);                // a flavor picker per block, the filter's slope and its position
    EXPECT(find(root, @"FXKnob").count == BlockCount*4+3);                // four per block, and Input, Output, Mix
    EXPECT(find(root, @"FXGraph").count == BlockCount);
    for (NSView* p in panels) EXPECT(NSContainsRect(root.bounds, p.frame));
    for (NSView* a in chips) { EXPECT(NSContainsRect(root.bounds, a.frame)); for (NSView* b in chips) if (a != b) EXPECT(!NSIntersectsRect(a.frame, b.frame)); }
    // One panel is shown at a time, and the filter's is first.
    for (uint32_t b = 0; b < BlockCount; ++b) EXPECT(panels[b].hidden == (b != FilterBlock));

    for (uint32_t block = 0; block < BlockCount; ++block) {
        NSView* chip = chips[block];
        // The chip: its light switches the block, a complete gesture each click; its name shows the panel.
        const int begins = gestureBegins, ends = gestureEnds;
        click(window, chip, NSMakePoint(18, 28));
        EXPECT(values[blockParam(block, On)] == 1 && gestureBegins == begins+1 && gestureEnds == ends+1);
        EXPECT(panels[block].hidden == (block != FilterBlock));              // the light does not change what is shown
        refresh(root);
        click(window, chip, NSMakePoint(18, 28));
        EXPECT(values[blockParam(block, On)] == 0);
        refresh(root);
        click(window, chip, NSMakePoint(90, 28));
        for (uint32_t b = 0; b < BlockCount; ++b) EXPECT(panels[b].hidden == (b != block));
        EXPECT(values[blockParam(block, On)] == 0);                          // naming it does not switch it
        refresh(root);

        NSView* panel = panels[block];
        NSArray<NSView*>* knobs = find(panel, @"FXKnob");
        NSView* picker = find(panel, @"FXPicker")[0];
        EXPECT(knobs.count == 4);

        // The flavor picker: each third of it chooses that flavor, and the knobs are relabeled to match.
        for (uint32_t flavor = 0; flavor < flavorCount; ++flavor) {
            click(window, picker, NSMakePoint(picker.bounds.size.width*(static_cast<CGFloat>(flavor)+.5)/3, 15));
            EXPECT(values[blockParam(block, Flavor)] == flavor);
            refresh(root);
            const FlavorInfo info = flavorInfo(block, flavor);
            for (int k = 0; k < 3; ++k) {
                NSView* knob = knobs[static_cast<NSUInteger>(k)];
                EXPECT(knob.hidden == (info.knob[k] == nullptr || (block == FilterBlock && k == 2)));      // the filter's slope is a picker
                if (info.knob[k]) EXPECT([[knob valueForKey:@"title"] isEqualToString:[NSString stringWithUTF8String:info.knob[k]]]);
            }
            EXPECT([[knobs[3] valueForKey:@"title"] isEqualToString:@"MIX"] && !knobs[3].hidden);
            // The numbers shown are the plugin's own text, in this flavor's units.
            NSTextField* field = [knobs[0] valueForKey:@"field_"];
            char expected[64];
            formatValue(expected, sizeof(expected), blockParam(block, P1), values[blockParam(block, P1)], flavor);
            EXPECT([field.stringValue isEqualToString:[NSString stringWithUTF8String:expected]]);
            // The chip says which flavor it is.
            EXPECT(chips[block].needsDisplay || true);
        }
        click(window, picker, NSMakePoint(10, 15));                       // back to the first
        refresh(root);

        // A knob: dragging up raises it, double-click resets it, typing sets it.
        NSView* knob = knobs[0];
        const uint32_t pid = blockParam(block, P1);
        const double start = values[pid];
        const NSPoint at = NSMakePoint(50, 50);
        [knob mouseDown:mouse(window, knob, at, NSEventTypeLeftMouseDown)];
        [knob mouseDragged:mouse(window, knob, NSMakePoint(at.x, at.y+30), NSEventTypeLeftMouseDragged)];
        [knob mouseUp:mouse(window, knob, NSMakePoint(at.x, at.y+30), NSEventTypeLeftMouseUp)];
        EXPECT(values[pid] > start);
        click(window, knob, at, 2);
        EXPECT(values[pid] == paramInfo(pid).initial);
        NSTextField* field = [knob valueForKey:@"field_"];
        // What to type for each block's first knob (in its first flavor's units) and the value that should result.
        const struct { NSString* text; double stored; } typing[BlockCount] {
            {@"2k", 2000}, {@"60", 60}, {@"2 Hz", 2}, {@"250 ms", 250}, {@"1 s", reverbDecayToPercent(0, 1)}, {@"150 %", 75}};
        field.stringValue = typing[block].text;
        type(knob, field);
        EXPECT(std::abs(values[pid]-typing[block].stored) < .01);
        field.stringValue = @"nonsense";
        const double before = values[pid];
        type(knob, field);
        EXPECT(values[pid] == before);                                     // refused, and nothing changed
        click(window, knob, at, 2);
        EXPECT(gestureBegins == gestureEnds);
    }

    // The filter's slope: two segments, and each one chooses.
    [(id<FXViewTesting>)root showBlock:FilterBlock]; refresh(root);
    NSView* slope = find(panels[FilterBlock], @"FXPicker")[1];
    click(window, slope, NSMakePoint(slope.bounds.size.width*.75, 13));
    EXPECT(values[blockParam(FilterBlock, P3)] == 1);
    click(window, slope, NSMakePoint(slope.bounds.size.width*.25, 13));
    EXPECT(values[blockParam(FilterBlock, P3)] == 0);
    // Delay sync: the menu offers Free and every note value; choosing one is a complete gesture, dims the Time knob and
    // shows the time the note comes to at the host's tempo (120 until it says), and Free gives the knob back.
    [(id<FXViewTesting>)root showBlock:DelayBlock]; refresh(root);
    NSView* sync = find(panels[DelayBlock], @"FXMenuField")[0];
    NSMenu* menu = call(sync, NSSelectorFromString(@"buildMenu"));
    EXPECT(menu.itemArray.count == syncCount);
    for (NSUInteger i = 0; i < syncCount; ++i) EXPECT([menu.itemArray[i].title isEqualToString:[NSString stringWithUTF8String:syncNames[i]]]);
    NSView* timeKnob = find(panels[DelayBlock], @"FXKnob")[0];
    EXPECT(timeKnob.alphaValue == 1);
    const int syncBegins = gestureBegins;
    NSMenuItem* eighth = menu.itemArray[6];                                  // 1/8
    call(eighth.target, eighth.action, eighth);
    EXPECT(values[DelaySync] == 6 && gestureBegins == syncBegins+1);
    refresh(root);
    EXPECT(timeKnob.alphaValue < 1 && [[timeKnob valueForKey:@"title"] isEqualToString:@"TIME · 1/8"]);
    EXPECT([((NSTextField*)[timeKnob valueForKey:@"field_"]).stringValue isEqualToString:@"250 ms"]);      // at 120 BPM
    status.tempo = 90; refresh(root);
    EXPECT([((NSTextField*)[timeKnob valueForKey:@"field_"]).stringValue isEqualToString:@"333 ms"]);
    NSMenuItem* dotted = menu.itemArray[10];                                 // 1/4. at 90 BPM is a second
    call(dotted.target, dotted.action, dotted);
    refresh(root);
    EXPECT([((NSTextField*)[timeKnob valueForKey:@"field_"]).stringValue isEqualToString:@"1.00 s"]);
    NSMenuItem* free = menu.itemArray[0];
    call(free.target, free.action, free);
    refresh(root);
    EXPECT(values[DelaySync] == 0 && timeKnob.alphaValue == 1 && [[timeKnob valueForKey:@"title"] isEqualToString:@"TIME"]);
    status.tempo = 0;
    EXPECT(gestureBegins == gestureEnds);
    // The Width block has no third knob in any flavor.
    EXPECT(find(panels[WidthBlock], @"FXKnob")[2].hidden);
    // Auto Gain and Bypass.
    NSArray<NSView*>* toggles = find(root, @"FXToggle");
    NSView* autoGain = toggles[0];
    NSView* bypass = toggles[1];
    click(window, bypass, NSMakePoint(40, 16));
    EXPECT(values[Bypass] == 1);
    click(window, bypass, NSMakePoint(40, 16));
    EXPECT(values[Bypass] == 0);
    click(window, autoGain, NSMakePoint(40, 16));
    EXPECT(values[AutoGain] == 1);
    click(window, autoGain, NSMakePoint(40, 16));
    EXPECT(values[AutoGain] == 0);
    // The filter position moves its chip: before the drive's, or after it.
    NSView* order = direct(root, @"FXPicker");
    EXPECT(chips[FilterBlock].frame.origin.x < chips[DriveBlock].frame.origin.x);
    click(window, order, NSMakePoint(order.bounds.size.width*.75, 13));
    EXPECT(values[FilterOrder] == 1);
    refresh(root);
    EXPECT(chips[FilterBlock].frame.origin.x > chips[DriveBlock].frame.origin.x);
    for (uint32_t b = ModBlock; b < BlockCount; ++b) EXPECT(chips[b].frame.origin.x > chips[FilterBlock].frame.origin.x);
    click(window, order, NSMakePoint(order.bounds.size.width*.25, 13));
    EXPECT(values[FilterOrder] == 0);
    refresh(root);
    EXPECT(chips[FilterBlock].frame.origin.x < chips[DriveBlock].frame.origin.x);
    // Values changed from outside (host automation) show up on the next refresh, in a panel that is not on show.
    values[blockParam(DriveBlock, Flavor)] = 2; values[blockParam(DriveBlock, On)] = 1;
    refresh(root);
    EXPECT([[find(panels[DriveBlock], @"FXKnob")[0] valueForKey:@"title"] isEqualToString:@"BITS"]);
    EXPECT(gestureBegins == gestureEnds);
    // Every graph draws every combination of flavor and state without trouble.
    for (uint32_t block = 0; block < BlockCount; ++block) {
        [(id<FXViewTesting>)root showBlock:block];
        for (uint32_t flavor = 0; flavor < flavorCount; ++flavor) for (int on = 0; on < 2; ++on) {
            values[blockParam(block, Flavor)] = flavor; values[blockParam(block, On)] = on;
            for (int extreme = 0; extreme < 3; ++extreme) {
                for (BlockKey key : {P1, P2, P3, BlockMix}) { const auto info = paramInfo(blockParam(block, key)); values[blockParam(block, key)] = extreme == 0 ? info.min : extreme == 1 ? info.max : info.initial; }
                refresh(root);
                [root displayIfNeeded];
            }
        }
    }
    std::printf("selftest: %s\n", failures ? "FAILED" : "all interaction checks passed");
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s out.png [key=value ...] | --selftest\n", argv[0]); return 2; }
    const bool test = std::strcmp(argv[1], "--selftest") == 0;
    status.inputPeak = .34f; status.outputPeak = .5f;
    for (int i = 2; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        if (!eq) { std::fprintf(stderr, "bad override %s\n", argv[i]); return 2; }
        const std::string name = lower(std::string(argv[i], static_cast<size_t>(eq-argv[i])).c_str());
        if (name == "tempo") { status.tempo = std::atof(eq+1); continue; }
        if (name == "in") { status.inputPeak = static_cast<float>(std::atof(eq+1)); continue; }
        if (name == "out") { status.outputPeak = static_cast<float>(std::atof(eq+1)); continue; }
        if (name == "stage") { shownBlock = -1; for (uint32_t b = 0; b < BlockCount; ++b) if (lower(blockNames[b]) == lower(eq+1) || (lower(eq+1) == "mod" && b == ModBlock)) shownBlock = static_cast<int>(b); if (shownBlock < 0) { std::fprintf(stderr, "unknown stage %s\n", eq+1); return 2; } continue; }
        if (findParam(name) < 0) { std::fprintf(stderr, "unknown key %s\n", name.c_str()); return 2; }
    }
    // The flavor decides what a block's knobs read in, so flavors are applied first and everything else after.
    for (int pass = 0; pass < 2; ++pass) for (int i = 2; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        const std::string name = lower(std::string(argv[i], static_cast<size_t>(eq-argv[i])).c_str());
        const int id = findParam(name);
        if (id < 0) continue;
        const uint32_t pid = static_cast<uint32_t>(id);
        const bool isFlavor = isBlockParam(pid) && keyOf(pid) == Flavor;
        if (isFlavor != (pass == 0)) continue;
        const uint32_t flavor = isBlockParam(pid) ? static_cast<uint32_t>(values[blockParam(blockOf(pid), Flavor)]) : 0;
        double value = 0;
        if (!parseValue(pid, eq+1, flavor, value)) { std::fprintf(stderr, "cannot read %s for %s\n", eq+1, name.c_str()); return 2; }
        values[pid] = sanitize(pid, value);
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        EditorCallbacks callbacks;
        callbacks.get = [](void*, uint32_t id) { return values[id]; };
        callbacks.edit = [](void*, uint32_t id, double value, int kind) {
            if (kind == 0) values[id] = value; else if (kind == 1) ++gestureBegins; else ++gestureEnds;
            return true;
        };
        callbacks.status = [](void*, Status* out) { *out = status; };
        auto* editor = Editor::create(callbacks);
        NSView* view = (__bridge NSView*)editor->nativeView();
        auto* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, Editor::width, Editor::height)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        window.contentView = view;
        if (test) { const int code = selftest(view, window); delete editor; return code; }
        [(id<FXViewTesting>)view showBlock:static_cast<uint32_t>(shownBlock)];
        refresh(view);
        [view layoutSubtreeIfNeeded]; [view displayIfNeeded];
        const CGFloat scale = 2;
        auto* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:Editor::width*scale pixelsHigh:Editor::height*scale
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        rep.size = NSMakeSize(Editor::width, Editor::height);
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![png writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES]) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
        delete editor;
    }
    std::printf("wrote %s (%ux%u @%gx)\n", argv[1], Editor::width, Editor::height, 2.0);
    return 0;
}
