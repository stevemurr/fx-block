#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "Editor.h"
#include "ui/Ui.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace fxblock;

static const CGFloat kMargin = 24, kChipY = 108, kChipW = 150, kChipH = 56, kPanelY = 182, kPanelW = Editor::width-2*24, kPanelH = 420;
static const CGFloat kKnobW = 100, kKnobH = 108, kGraphY = 46, kGraphH = 250, kKnobY = 308;
static NSString* ns(const char* s) { return [NSString stringWithUTF8String:s]; }

// ---------------------------------------------------------------- block chip
// One block in the signal path: a light that switches it, its name, and the flavor it is set to. Clicking the name shows its panel.
@interface FXChip : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t block;
@property (nonatomic) BOOL selected;
@property (copy) void (^onSelect)(uint32_t);
- (void)sync;
@end
@implementation FXChip {
    BOOL on_;
    uint32_t flavor_;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setSelected:(BOOL)selected { if (_selected != selected) { _selected = selected; self.needsDisplay = YES; } }
- (void)setBlock:(uint32_t)block { _block = block; self.needsDisplay = YES; }
- (void)sync {
    const BOOL on = [_host valueFor:blockParam(_block, On)] >= .5;
    const uint32_t flavor = static_cast<uint32_t>(std::clamp([_host valueFor:blockParam(_block, Flavor)], 0., flavorCount-1.));
    if (on != on_ || flavor != flavor_) { on_ = on; flavor_ = flavor; self.needsDisplay = YES; }
}
- (void)mouseDown:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    [self.window makeFirstResponder:nil];
    if (p.x < 34) { [_host commit:blockParam(_block, On) value:on_ ? 0 : 1]; on_ = !on_; self.needsDisplay = YES; return; }
    if (_onSelect) _onSelect(_block);
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityButtonRole; }
- (NSString*)accessibilityLabel { return ns(blockNames[_block]); }
- (id)accessibilityValue { return on_ ? @"on" : @"off"; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    NSColor* tint = uiColor(blockTint(_block));
    [NSGraphicsContext saveGraphicsState];
    if (_selected) { auto* glow = [[NSShadow alloc] init]; glow.shadowColor = [tint colorWithAlphaComponent:.3]; glow.shadowBlurRadius = 10; glow.shadowOffset = NSZeroSize; [glow set]; }
    uiFillPanel(b, 12, _selected ? uiColor(kPanelHi) : uiColor(kPanel), _selected ? tint : uiColor(kLine));
    [NSGraphicsContext restoreGraphicsState];
    // the light
    const NSRect led = NSMakeRect(12, b.size.height/2-7, 14, 14);
    auto* dot = [NSBezierPath bezierPathWithOvalInRect:led];
    [NSGraphicsContext saveGraphicsState];
    if (on_) { auto* glow = [[NSShadow alloc] init]; glow.shadowColor = [tint colorWithAlphaComponent:.8]; glow.shadowBlurRadius = 8; glow.shadowOffset = NSZeroSize; [glow set]; }
    [(on_ ? tint : uiColor(kTrack)) setFill]; [dot fill];
    [NSGraphicsContext restoreGraphicsState];
    [uiColor(kBackground) setStroke]; dot.lineWidth = 1; [dot stroke];
    uiDrawText(ns(blockNames[_block]).uppercaseString, NSMakeRect(38, 12, b.size.width-44, 14), 10.5, on_ ? uiColor(kText) : uiColor(kDim), NSFontWeightHeavy, 1.3, NSTextAlignmentLeft);
    uiDrawText(ns(flavorNames[_block][flavor_]).uppercaseString, NSMakeRect(38, b.size.height-24, b.size.width-44, 11), 8, on_ ? uiColor(kFaint) : uiColor(kTrack), NSFontWeightBold, 1, NSTextAlignmentLeft);
}
@end

// ---------------------------------------------------------------- one block's panel
@interface FXBlockPanel : FXFlipped
@property (nonatomic) uint32_t block;
@end
@implementation FXBlockPanel
@end

// ---------------------------------------------------------------- the editor view
@interface FXView : NSView <FXHost> {
    EditorCallbacks cb_;
    Status status_;
    NSTimer* timer_;
    NSMutableArray<NSView*>* controls_;          // everything with a sync, in every panel
    NSMutableArray<FXGraph*>* graphs_;
    FXChip* chips_[BlockCount];
    FXBlockPanel* panels_[BlockCount];
    FXKnob* knobs_[BlockCount][4];
    FXPicker* flavor_[BlockCount];
    NSTextField* caption_[BlockCount];
    NSTextField* slopeLabel_;
    FXPicker* slope_;
    FXMenuField* sync_;
    uint32_t shown_;
    uint32_t shownFlavor_[BlockCount];
    int shownOn_[BlockCount];
    FXKnob* input_; FXKnob* output_; FXKnob* mix_;
    FXToggle* bypass_; FXToggle* autoGain_;
    FXPicker* order_;
    FXMeter* inMeter_; FXMeter* outMeter_;
}
- (instancetype)initWithCallbacks:(EditorCallbacks)callbacks;
- (void)refresh;
- (void)start;
- (void)stop;
@end

@implementation FXView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

// ---- host protocol
- (double)valueFor:(uint32_t)pid { return cb_.get && pid < ParamCount ? cb_.get(cb_.context, pid) : paramInfo(pid).initial; }
- (void)begin:(uint32_t)pid { if (cb_.edit) cb_.edit(cb_.context, pid, [self valueFor:pid], 1); }
- (void)set:(uint32_t)pid value:(double)value { if (cb_.edit) cb_.edit(cb_.context, pid, value, 0); }
- (void)end:(uint32_t)pid { if (cb_.edit) cb_.edit(cb_.context, pid, [self valueFor:pid], 2); }
- (void)commit:(uint32_t)pid value:(double)value { [self begin:pid]; [self set:pid value:value]; [self end:pid]; }
- (uint32_t)flavorOf:(uint32_t)pid {
    return isBlockParam(pid) ? static_cast<uint32_t>(std::clamp([self valueFor:blockParam(blockOf(pid), Flavor)], 0., flavorCount-1.)) : 0;
}
- (NSString*)textFor:(uint32_t)pid value:(double)value { char text[64]; formatValue(text, sizeof(text), pid, value, [self flavorOf:pid]); return ns(text); }
- (BOOL)parse:(NSString*)text for:(uint32_t)pid into:(double*)value { return parseValue(pid, text.UTF8String, [self flavorOf:pid], *value); }
- (Status)status { return status_; }
- (Values)allValues { Values v; for (uint32_t i = 0; i < ParamCount; ++i) v[i] = [self valueFor:i]; return v; }
- (double)tempo { return status_.tempo > 0 ? status_.tempo : 120; }

- (FXKnob*)headerKnob:(uint32_t)pid title:(NSString*)title detail:(NSString*)detail x:(CGFloat)x {
    FXKnob* k = [[FXKnob alloc] initWithFrame:NSMakeRect(x, 10, 72, 78)];
    k.compact = YES; k.host = self; k.param = pid; k.title = title; k.tint = uiColor(0xA9BCC8); k.detail = detail;
    [self addSubview:k]; [controls_ addObject:k];
    return k;
}
- (NSTextField*)label:(NSString*)text frame:(NSRect)frame size:(CGFloat)size weight:(NSFontWeight)weight color:(NSColor*)color {
    NSTextField* f = [NSTextField labelWithString:text];
    f.frame = frame; f.font = [NSFont systemFontOfSize:size weight:weight]; f.textColor = color;
    f.lineBreakMode = NSLineBreakByTruncatingTail;
    return f;
}

- (instancetype)initWithCallbacks:(EditorCallbacks)callbacks {
    self = [super initWithFrame:NSMakeRect(0, 0, Editor::width, Editor::height)];
    if (!self) return nil;
    cb_ = callbacks; shown_ = FilterBlock;
    controls_ = [NSMutableArray array]; graphs_ = [NSMutableArray array];
    for (uint32_t b = 0; b < BlockCount; ++b) { shownFlavor_[b] = 99; shownOn_[b] = -1; }
    self.wantsLayer = YES;

    // ---- header
    input_ = [self headerKnob:Input title:@"INPUT" detail:@"Level into the chain. The dry signal that Mix blends back in is not touched." x:600];
    output_ = [self headerKnob:Output title:@"OUTPUT" detail:@"Final level of the whole box." x:684];
    mix_ = [self headerKnob:Mix title:@"MIX" detail:@"The whole chain against the dry signal. 0% is dry, 100% is all effect." x:768];
    autoGain_ = [[FXToggle alloc] initWithFrame:NSMakeRect(Editor::width-kMargin-110, 14, 110, 32)];
    autoGain_.host = self; autoGain_.param = AutoGain; autoGain_.title = @"AUTO GAIN"; autoGain_.tint = uiColor(0x6FD6A8);
    autoGain_.toolTip = @"Matches the loudness of the effected signal to the dry, so wet and dry compare at the same level. Learns over about half a second, and holds its gain when the input stops.";
    [self addSubview:autoGain_]; [controls_ addObject:autoGain_];
    bypass_ = [[FXToggle alloc] initWithFrame:NSMakeRect(Editor::width-kMargin-110, 52, 110, 32)];
    bypass_.host = self; bypass_.param = Bypass; bypass_.title = @"BYPASS"; bypass_.tint = uiColor(kRed);
    bypass_.toolTip = @"Takes the dry signal around everything, smoothly.";
    [self addSubview:bypass_]; [controls_ addObject:bypass_];
    order_ = [[FXPicker alloc] initWithFrame:NSMakeRect(kMargin, 68, 260, 24)];
    order_.host = self; order_.param = FilterOrder; order_.titles = @[@"FILTER → DRIVE", @"DRIVE → FILTER"]; order_.tint = uiColor(blockTint(FilterBlock));
    order_.spoken = @"Filter position";
    order_.toolTip = @"Where the filter sits in the chain: before the drive, or after it to tame the fizz the drive makes.";
    [self addSubview:order_]; [controls_ addObject:order_];
    inMeter_ = [[FXMeter alloc] initWithFrame:NSMakeRect(354, 30, 190, 11)]; inMeter_.tint = uiColor(0xA9BCC8); [self addSubview:inMeter_];
    outMeter_ = [[FXMeter alloc] initWithFrame:NSMakeRect(354, 54, 190, 11)]; outMeter_.tint = uiColor(0x6FD6A8); [self addSubview:outMeter_];

    // ---- panels
    for (uint32_t b = 0; b < BlockCount; ++b) [self buildPanel:b];

    // ---- chips
    __weak FXView* weak = self;
    for (uint32_t b = 0; b < BlockCount; ++b) {
        chips_[b] = [[FXChip alloc] initWithFrame:NSMakeRect(kMargin, kChipY, kChipW, kChipH)];
        chips_[b].host = self; chips_[b].block = b; chips_[b].selected = b == shown_;
        chips_[b].onSelect = ^(uint32_t block) { [weak showBlock:block]; };
        [self addSubview:chips_[b]];
    }
    [self refresh];
    return self;
}

- (void)buildPanel:(uint32_t)block {
    FXBlockPanel* p = [[FXBlockPanel alloc] initWithFrame:NSMakeRect(kMargin, kPanelY, kPanelW, kPanelH)];
    p.block = block; p.hidden = block != shown_;
    panels_[block] = p;
    [self addSubview:p];
    NSColor* tint = uiColor(blockTint(block));
    NSMutableArray* names = [NSMutableArray array];
    for (uint32_t f = 0; f < flavorCount; ++f) [names addObject:ns(flavorNames[block][f]).uppercaseString];
    NSString* name = ns(blockNames[block]);
    flavor_[block] = [[FXPicker alloc] initWithFrame:NSMakeRect(0, 4, 3*140, 30)];
    flavor_[block].host = self; flavor_[block].param = blockParam(block, Flavor); flavor_[block].titles = names; flavor_[block].tint = tint;
    flavor_[block].spoken = [NSString stringWithFormat:@"%@ flavor", name];
    flavor_[block].toolTip = [NSString stringWithFormat:@"Which kind of %@ this block does.", name.lowercaseString];
    [p addSubview:flavor_[block]]; [controls_ addObject:flavor_[block]];
    caption_[block] = [self label:@"" frame:NSMakeRect(3*140+18, 11, kPanelW-3*140-18-(block == DelayBlock ? 150 : 0), 16) size:11 weight:NSFontWeightRegular color:uiColor(kDim)];
    [p addSubview:caption_[block]];

    FXGraph* g = nil;
    switch (block) {
    case FilterBlock: g = [FXFilterGraph alloc]; break;
    case DriveBlock: g = [FXDriveGraph alloc]; break;
    case ModBlock: g = [FXModGraph alloc]; break;
    case DelayBlock: g = [FXDelayGraph alloc]; break;
    case ReverbBlock: g = [FXReverbGraph alloc]; break;
    default: g = [FXWidthGraph alloc]; break;
    }
    g = [g initWithFrame:NSMakeRect(0, kGraphY, kPanelW, kGraphH)];
    g.host = self; g.block = block;
    [p addSubview:g]; [graphs_ addObject:g];

    const BlockKey keys[4] {P1, P2, P3, BlockMix};
    const CGFloat gap = (kPanelW-4*kKnobW)/5;
    for (int k = 0; k < 4; ++k) {
        FXKnob* knob = [[FXKnob alloc] initWithFrame:NSMakeRect(gap+static_cast<CGFloat>(k)*(kKnobW+gap), kKnobY, kKnobW, kKnobH)];
        knob.host = self; knob.param = blockParam(block, keys[k]); knob.tint = tint;
        [p addSubview:knob]; [controls_ addObject:knob];
        knobs_[block][k] = knob;
    }
    if (block == FilterBlock) {
        // Two states do not make a knob: a two-segment picker sits where the third one would, under the same kind of label.
        knobs_[block][2].hidden = YES;
        slopeLabel_ = [self label:@"SLOPE · DB/OCT" frame:NSMakeRect(gap+2*(kKnobW+gap), kKnobY+3, kKnobW, 13) size:9 weight:NSFontWeightSemibold color:uiColor(kDim)];
        slopeLabel_.alignment = NSTextAlignmentCenter;
        [p addSubview:slopeLabel_];
        slope_ = [[FXPicker alloc] initWithFrame:NSMakeRect(gap+2*(kKnobW+gap)+8, kKnobY+39, kKnobW-16, 26)];
        slope_.host = self; slope_.param = blockParam(block, P3); slope_.titles = @[@"12", @"24"]; slope_.tint = tint;
        slope_.spoken = @"Filter slope in decibels per octave";
        slope_.toolTip = @"How steeply the filter cuts: 12 or 24 dB per octave. 24 dB is sharper, and rings more with resonance.";
        [p addSubview:slope_]; [controls_ addObject:slope_];
    }
    if (block == DelayBlock) {
        NSMutableArray* syncNamesList = [NSMutableArray array];
        for (uint32_t i = 0; i < syncCount; ++i) [syncNamesList addObject:ns(syncNames[i])];
        sync_ = [[FXMenuField alloc] initWithFrame:NSMakeRect(kPanelW-130, 8, 130, 24)];
        sync_.host = self; sync_.param = DelaySync; sync_.titles = syncNamesList; sync_.tint = tint; sync_.label = @"SYNC";
        sync_.spoken = @"Delay tempo sync";
        sync_.toolTip = @"Free: the Time knob sets the delay. Or lock it to a note value at the host's tempo (T is a triplet, a dot is half again as long).";
        [p addSubview:sync_]; [controls_ addObject:sync_];
    }
}

- (void)showBlock:(uint32_t)block {
    shown_ = block;
    for (uint32_t b = 0; b < BlockCount; ++b) { panels_[b].hidden = b != block; chips_[b].selected = b == block; }
    [self refresh];
}

// What a block's knobs are called, and what they do, depends on its flavor; a delay's Time knob steps aside when it is synced.
- (void)syncPanel:(uint32_t)block {
    const BOOL on = [self valueFor:blockParam(block, On)] >= .5;
    const uint32_t flavor = [self flavorOf:blockParam(block, Flavor)];
    NSColor* tint = on ? uiColor(blockTint(block)) : uiColor(kFaint);
    if (flavor != shownFlavor_[block] || on != shownOn_[block]) {
        shownFlavor_[block] = flavor; shownOn_[block] = on;
        const FlavorInfo info = flavorInfo(block, flavor);
        caption_[block].stringValue = ns(info.caption);
        caption_[block].textColor = on ? uiColor(kDim) : uiColor(kFaint);
        for (int k = 0; k < 3; ++k) {
            knobs_[block][k].hidden = info.knob[k] == nullptr || (block == FilterBlock && k == 2);
            if (info.knob[k]) {
                knobs_[block][k].title = ns(info.knob[k]);
                knobs_[block][k].detail = [NSString stringWithFormat:@"%s: %s", info.knob[k], info.caption];
            }
        }
        knobs_[block][3].title = @"MIX";
        knobs_[block][3].detail = block == ModBlock || block == FilterBlock || block == DriveBlock || block == WidthBlock
            ? @"MIX: how much of the effect is crossfaded in against the signal going into this block."
            : @"MIX: how loud the effect is, added to the signal going into this block, which stays at full level.";
        for (int k = 0; k < 4; ++k) knobs_[block][k].tint = tint;
        flavor_[block].tint = on ? uiColor(blockTint(block)) : uiColor(kDim);
        if (block == FilterBlock) { slope_.tint = tint; slopeLabel_.textColor = on ? uiColor(kDim) : uiColor(kFaint); }
        if (block == DelayBlock) sync_.tint = tint;
    }
    if (block == DelayBlock) {
        const NSInteger division = static_cast<NSInteger>([self valueFor:DelaySync]);
        FXKnob* time = knobs_[block][0];
        // Synced, the Time knob is not in charge: it shows what the note value comes to at the tempo.
        if (division > 0 && division < static_cast<NSInteger>(syncCount)) {
            const double ms = DelayFx::effectiveMs([self valueFor:blockParam(DelayBlock, P1)], static_cast<uint32_t>(division), [self tempo]);
            time.title = [NSString stringWithFormat:@"TIME · %s", syncNames[static_cast<size_t>(division)]];
            time.overrideText = ms >= 1000 ? [NSString stringWithFormat:@"%.2f s", ms*.001] : [NSString stringWithFormat:@"%.0f ms", ms];
            time.dimmed = YES;
            time.detail = @"Time while Sync is Free. With a note value chosen, the delay follows the host's tempo and this shows the time it comes to.";
        } else {
            time.title = ns(flavorInfo(block, flavor).knob[0]);
            time.overrideText = nil; time.dimmed = NO;
        }
    }
}

// ---- refresh
- (void)layoutChips {
    // The chips follow the signal path: the filter moves to after the drive when its position switch says so.
    const BOOL after = [self valueFor:FilterOrder] >= .5;
    const uint32_t order[BlockCount] {after ? DriveBlock : FilterBlock, after ? FilterBlock : DriveBlock, ModBlock, DelayBlock, ReverbBlock, WidthBlock};
    const CGFloat gap = (kPanelW-BlockCount*kChipW)/(BlockCount-1);
    for (uint32_t i = 0; i < BlockCount; ++i) {
        const NSRect frame = NSMakeRect(kMargin+static_cast<CGFloat>(i)*(kChipW+gap), kChipY, kChipW, kChipH);
        if (!NSEqualRects(chips_[order[i]].frame, frame)) chips_[order[i]].frame = frame;
    }
}
- (void)refresh {
    if (cb_.status) cb_.status(cb_.context, &status_);
    [self layoutChips];
    for (uint32_t b = 0; b < BlockCount; ++b) [self syncPanel:b];
    for (NSView* c in controls_) {
        NSView* v = c; BOOL visible = YES;
        while (v && v != self) { if (v.hidden) { visible = NO; break; } v = v.superview; }
        if (visible) [(id)c sync];
    }
    for (uint32_t b = 0; b < BlockCount; ++b) [chips_[b] sync];
    for (FXGraph* g in graphs_) { if (!g.superview.hidden) [g sync]; }
    inMeter_.value = static_cast<float>(std::clamp((20*std::log10(std::max<double>(status_.inputPeak, 1e-4))+60)/60, 0., 1.));
    outMeter_.value = static_cast<float>(std::clamp((20*std::log10(std::max<double>(status_.outputPeak, 1e-4))+60)/60, 0., 1.));
    outMeter_.tint = status_.outputPeak > .98f ? uiColor(kRed) : uiColor(0x6FD6A8);
}
- (void)start {
    if (timer_) return;
    __weak FXView* weak = self;
    timer_ = [NSTimer timerWithTimeInterval:1./30 repeats:YES block:^(NSTimer*) { [weak refresh]; }];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
}
- (void)stop { [timer_ invalidate]; timer_ = nil; }
- (void)dealloc { [timer_ invalidate]; }

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [uiColor(kBackground) setFill]; NSRectFill(self.bounds);
    uiDrawText(@"FX BLOCK", NSMakeRect(kMargin, 14, 300, 30), 24, uiColor(kText), NSFontWeightHeavy, 4, NSTextAlignmentLeft);
    uiDrawText(@"SWISS-ARMY EFFECTS BOX", NSMakeRect(kMargin+1, 46, 280, 12), 8.5, uiColor(kFaint), NSFontWeightSemibold, 1.6, NSTextAlignmentLeft);
    uiDrawText(@"IN", NSMakeRect(326, 28, 24, 12), 8, uiColor(kDim), NSFontWeightBold, 1.2, NSTextAlignmentLeft);
    uiDrawText(@"OUT", NSMakeRect(326, 52, 28, 12), 8, uiColor(kDim), NSFontWeightBold, 1.2, NSTextAlignmentLeft);
    // the arrows between the chips
    const CGFloat gap = (kPanelW-BlockCount*kChipW)/(BlockCount-1);
    for (int i = 0; i < static_cast<int>(BlockCount)-1; ++i) uiDrawText(@"›", NSMakeRect(kMargin+(i+1)*kChipW+i*gap, kChipY+14, gap, 28), 22, uiColor(kFaint), NSFontWeightLight, 0, NSTextAlignmentCenter);
    uiDrawText([NSString stringWithFormat:@"v1.0 · %u parameters", exposedCount], NSMakeRect(kMargin, Editor::height-24, 400, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 1.2, NSTextAlignmentLeft);
}
@end

namespace fxblock {
class CocoaEditor final : public Editor {
public:
    explicit CocoaEditor(EditorCallbacks callbacks) { view_ = [[FXView alloc] initWithCallbacks:callbacks]; }
    ~CocoaEditor() override { [view_ stop]; [view_ removeFromSuperview]; view_ = nil; }
    bool setParent(void* parent) override {
        auto* host = (__bridge NSView*)parent;
        if (!host || !view_) return false;
        [view_ removeFromSuperview]; [host addSubview:view_]; return true;
    }
    void show(bool visible) override { view_.hidden = !visible; if (visible) [view_ start]; else [view_ stop]; }
    void* nativeView() const override { return (__bridge void*)view_; }
private:
    FXView* __strong view_;
};
Editor* Editor::create(EditorCallbacks callbacks) { return new CocoaEditor(callbacks); }
} // namespace fxblock
