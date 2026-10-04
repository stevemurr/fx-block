#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "Editor.h"
#include "Parameters.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace fxblock;

// ---------------------------------------------------------------- palette & text
static NSColor* color(unsigned rgb, CGFloat alpha = 1) {
    return [NSColor colorWithSRGBRed:((rgb>>16)&255)/255. green:((rgb>>8)&255)/255. blue:(rgb&255)/255. alpha:alpha];
}
static const unsigned kBackground = 0x0C1012, kPanel = 0x131A1D, kLine = 0x25323A, kTrack = 0x212C32;
static const unsigned kText = 0xE9EEEA, kDim = 0x82938B, kFaint = 0x56665F;
static unsigned blockTint(uint32_t block) {
    static const unsigned tints[BlockCount] {0x7CC4E8, 0xFF8A7A, 0x6FD6A8, 0xF0C96B, 0xB59CF0, 0xE88AC6};
    return tints[block%BlockCount];
}

static void drawText(NSString* string, NSRect rect, CGFloat size, NSColor* tint, NSFontWeight weight,
                     CGFloat kern, NSTextAlignment alignment) {
    auto* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = alignment; style.lineBreakMode = NSLineBreakByClipping;
    [string drawInRect:rect withAttributes:@{
        NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
        NSForegroundColorAttributeName: tint, NSKernAttributeName: @(kern), NSParagraphStyleAttributeName: style}];
}
static void fillPanel(NSRect r, CGFloat radius, NSColor* fill, NSColor* stroke) {
    auto* path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, .5, .5) xRadius:radius yRadius:radius];
    [fill setFill]; [path fill];
    if (stroke) { [stroke setStroke]; path.lineWidth = 1; [path stroke]; }
}

// A borderless field draws its text from the top; this centres it vertically.
@interface FXCenteredCell : NSTextFieldCell
@end
@implementation FXCenteredCell
- (NSRect)drawingRectForBounds:(NSRect)rect {
    NSRect r = [super drawingRectForBounds:rect];
    const CGFloat height = [self cellSizeForBounds:rect].height;
    if (height < r.size.height) { r.origin.y += (r.size.height-height)/2; r.size.height = height; }
    return r;
}
- (NSRect)titleRectForBounds:(NSRect)rect { return [self drawingRectForBounds:rect]; }
@end

// ---------------------------------------------------------------- host protocol
@protocol FXHost <NSObject>
- (double)valueFor:(uint32_t)pid;
- (void)begin:(uint32_t)pid;
- (void)set:(uint32_t)pid value:(double)value;
- (void)end:(uint32_t)pid;
- (void)commit:(uint32_t)pid value:(double)value; // begin + set + end
- (NSString*)textFor:(uint32_t)pid value:(double)value;   // in the units of the parameter's block's flavor
- (BOOL)parse:(NSString*)text for:(uint32_t)pid into:(double*)value;
- (double)tempo;                                           // the host's, or 120 until it says
@end

// ---------------------------------------------------------------- knob
@interface FXKnob : NSView <NSTextFieldDelegate>
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property (nonatomic) NSString* title;
@property (nonatomic) NSColor* tint;
@property (nonatomic) BOOL compact;
@property (nonatomic) NSString* detail;
@property (nonatomic) NSString* overrideText;     // shown instead of the value (a synced delay's time)
@property (nonatomic) BOOL dimmed;                // drawn faint: set, but something else is in charge
- (void)sync;
@end
@implementation FXKnob {
    double pos_, cont_, lastY_;
    BOOL interacting_;
    NSTextField* field_;
}
- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    field_ = [[NSTextField alloc] initWithFrame:NSZeroRect];
    field_.cell = [[FXCenteredCell alloc] initTextCell:@""];
    field_.editable = YES; field_.selectable = YES;
    field_.textColor = color(kText); field_.backgroundColor = NSColor.clearColor;
    field_.drawsBackground = NO; field_.bordered = NO; field_.bezeled = NO;
    field_.alignment = NSTextAlignmentCenter; field_.focusRingType = NSFocusRingTypeNone;
    field_.delegate = self; field_.target = self; field_.action = @selector(typed:);
    [self addSubview:field_];
    [self setCompact:NO];
    return self;
}
- (void)setCompact:(BOOL)compact {
    _compact = compact;
    const NSRect b = self.bounds;
    field_.font = [NSFont monospacedDigitSystemFontOfSize:compact ? 10.5 : 12 weight:NSFontWeightMedium];
    field_.frame = compact ? NSMakeRect(0, 2, b.size.width, 16) : NSMakeRect(4, 10, b.size.width-8, 20);
    self.needsDisplay = YES;
}
- (void)setTitle:(NSString*)title { if (![_title isEqualToString:title]) { _title = title; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setParam:(uint32_t)param { if (_param != param) { _param = param; cont_ = -1; } }
- (void)sync {
    const double value = [_host valueFor:_param];
    const double pos = toNormalized(_param, value);
    if (!interacting_ && cont_ < 0) cont_ = pos;
    NSString* text = _overrideText ? _overrideText : [_host textFor:_param value:value];
    self.alphaValue = _dimmed ? .45 : 1;
    if (pos != pos_ || ![field_.stringValue isEqualToString:text]) {
        pos_ = pos;
        if (!field_.currentEditor) field_.stringValue = text;
        self.needsDisplay = YES;
    }
    NSString* how = @"Drag, scroll, or type a value. Shift-drag for fine control; double-click resets.";
    NSString* hint = _detail.length ? [NSString stringWithFormat:@"%@ %@", _detail, how] : [NSString stringWithFormat:@"%@. %@", _title, how];
    if (![self.toolTip isEqualToString:hint]) self.toolTip = field_.toolTip = hint;
    [field_ setAccessibilityLabel:[NSString stringWithFormat:@"%@ value", _title]];
}
- (void)applyPosition:(double)p {
    cont_ = std::clamp(p, 0., 1.);
    double value = fromNormalized(_param, cont_);
    if (paramInfo(_param).stepped) value = std::round(value);        // flips halfway, not only at the very end
    if (value != [_host valueFor:_param]) [_host set:_param value:value];
    [self sync];
}
- (void)mouseDown:(NSEvent*)event {
    if (event.clickCount >= 2) { [_host commit:_param value:paramInfo(_param).initial]; [self sync]; return; }
    [self.window makeFirstResponder:nil];
    interacting_ = YES; lastY_ = event.locationInWindow.y;
    if (cont_ < 0) cont_ = pos_;
    [_host begin:_param];
}
- (void)mouseDragged:(NSEvent*)event {
    if (!interacting_) return;
    const double y = event.locationInWindow.y;
    const double perPixel = (event.modifierFlags & NSEventModifierFlagShift) ? 1./1000 : 1./190;
    [self applyPosition:cont_+(y-lastY_)*perPixel];
    lastY_ = y;
}
- (void)mouseUp:(NSEvent*)event {
    (void)event;
    if (!interacting_) return;
    interacting_ = NO; [_host end:_param]; [self sync];
}
- (void)scrollWheel:(NSEvent*)event {
    const double delta = event.scrollingDeltaY*(event.hasPreciseScrollingDeltas ? .004 : .02);
    if (delta == 0) return;
    if (cont_ < 0) cont_ = pos_;
    [_host begin:_param]; [self applyPosition:cont_+delta]; [_host end:_param];
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilitySliderRole; }
- (NSString*)accessibilityLabel { return _title; }
- (id)accessibilityValue { return [_host textFor:_param value:[_host valueFor:_param]]; }
- (BOOL)accessibilityPerformIncrement { cont_ = pos_; [_host begin:_param]; [self applyPosition:pos_+.05]; [_host end:_param]; return YES; }
- (BOOL)accessibilityPerformDecrement { cont_ = pos_; [_host begin:_param]; [self applyPosition:pos_-.05]; [_host end:_param]; return YES; }
- (void)typed:(NSTextField*)sender {
    double value;
    if ([_host parse:sender.stringValue for:_param into:&value]) [_host commit:_param value:value];
    [self.window makeFirstResponder:nil]; [self sync];
}
- (void)controlTextDidEndEditing:(NSNotification*)notification {
    if ([notification.userInfo[@"NSTextMovement"] integerValue] != NSReturnTextMovement) [self typed:field_];
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    const CGFloat radius = _compact ? 18 : 29, line = _compact ? 3.2 : 4;
    drawText(_title, NSMakeRect(0, b.size.height-(_compact ? 13 : 15), b.size.width, 12), _compact ? 8.5 : 10, color(kDim),
             NSFontWeightSemibold, _compact ? 1.2 : 1.6, NSTextAlignmentCenter);
    const NSPoint c = NSMakePoint(b.size.width/2, b.size.height-(_compact ? 38 : 52));
    auto* track = [NSBezierPath bezierPath];
    [track appendBezierPathWithArcWithCenter:c radius:radius startAngle:225 endAngle:-45 clockwise:YES];
    track.lineWidth = line; track.lineCapStyle = NSLineCapStyleRound;
    [color(kTrack) setStroke]; [track stroke];
    if (pos_ > .004) {
        auto* arc = [NSBezierPath bezierPath];
        [arc appendBezierPathWithArcWithCenter:c radius:radius startAngle:225 endAngle:225-270*pos_ clockwise:YES];
        arc.lineWidth = line; arc.lineCapStyle = NSLineCapStyleRound;
        [NSGraphicsContext saveGraphicsState];
        auto* glow = [[NSShadow alloc] init];
        glow.shadowColor = [_tint colorWithAlphaComponent:.5]; glow.shadowBlurRadius = _compact ? 6 : 9; glow.shadowOffset = NSZeroSize;
        [glow set]; [_tint setStroke]; [arc stroke];
        [NSGraphicsContext restoreGraphicsState];
    }
    const CGFloat body = radius-(_compact ? 5.5 : 8);
    auto* disc = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x-body, c.y-body, 2*body, 2*body)];
    [[[NSGradient alloc] initWithStartingColor:color(0x2B363C) endingColor:color(0x12181B)] drawInBezierPath:disc angle:-90];
    [color(0x3A4851) setStroke]; disc.lineWidth = 1; [disc stroke];
    const double angle = (225-270*pos_)*M_PI/180;
    auto* tick = [NSBezierPath bezierPath];
    [tick moveToPoint:NSMakePoint(c.x+std::cos(angle)*(_compact ? 3 : 5), c.y+std::sin(angle)*(_compact ? 3 : 5))];
    [tick lineToPoint:NSMakePoint(c.x+std::cos(angle)*(body-(_compact ? 2 : 3)), c.y+std::sin(angle)*(body-(_compact ? 2 : 3)))];
    tick.lineWidth = _compact ? 2 : 2.5; tick.lineCapStyle = NSLineCapStyleRound;
    [color(0xF4F1EA) setStroke]; [tick stroke];
}
@end

// ---------------------------------------------------------------- toggle
@interface FXToggle : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property NSString* title;
@property (nonatomic) NSColor* tint;
@property (nonatomic) BOOL big;
@property (nonatomic) BOOL on;
@end
@implementation FXToggle
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setOn:(BOOL)on { if (_on != on) { _on = on; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (void)mouseDown:(NSEvent*)event { (void)event; [self.window makeFirstResponder:nil]; [_host commit:_param value:_on ? 0 : 1]; self.on = !_on; }
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityCheckBoxRole; }
- (NSString*)accessibilityLabel { return _title; }
- (id)accessibilityValue { return @(_on); }
- (BOOL)accessibilityPerformPress { [_host commit:_param value:_on ? 0 : 1]; self.on = !_on; return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect r = NSInsetRect(self.bounds, 3, 3);
    const CGFloat radius = _big ? 10 : r.size.height/2;
    auto* path = [NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius];
    [NSGraphicsContext saveGraphicsState];
    if (_on) {
        auto* glow = [[NSShadow alloc] init];
        glow.shadowColor = [_tint colorWithAlphaComponent:_big ? .5 : .4]; glow.shadowBlurRadius = _big ? 14 : 8; glow.shadowOffset = NSZeroSize;
        [glow set];
    }
    [(_on ? [_tint colorWithAlphaComponent:.2] : color(kBackground, .6)) setFill]; [path fill];
    [NSGraphicsContext restoreGraphicsState];
    [(_on ? _tint : color(kLine)) setStroke]; path.lineWidth = 1; [path stroke];
    const CGFloat size = _big ? 13 : 9, height = size+4;
    drawText(_title, NSMakeRect(r.origin.x, r.origin.y+(r.size.height-height)/2+(_big ? 0 : .5), r.size.width, height), size,
             _on ? _tint : color(kDim), NSFontWeightBold, _big ? 2.4 : 1.2, NSTextAlignmentCenter);
}
@end

// ---------------------------------------------------------------- picker: a row of segments, one chosen
@interface FXPicker : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property (nonatomic) NSArray<NSString*>* titles;
@property (nonatomic) NSColor* tint;
@property (nonatomic) NSInteger selected;
@property NSString* spoken;
@end
@implementation FXPicker
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setSelected:(NSInteger)selected { if (_selected != selected) { _selected = selected; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (void)setTitles:(NSArray<NSString*>*)titles { _titles = titles; self.needsDisplay = YES; }
- (void)choose:(NSInteger)index {
    index = std::clamp<NSInteger>(index, 0, static_cast<NSInteger>(_titles.count)-1);
    if (index == _selected) return;
    [_host commit:_param value:static_cast<double>(index)];
    self.selected = index;
}
- (void)mouseDown:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    [self.window makeFirstResponder:nil];
    [self choose:static_cast<NSInteger>(p.x/(self.bounds.size.width/static_cast<CGFloat>(_titles.count)))];
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityRadioGroupRole; }
- (NSString*)accessibilityLabel { return _spoken ? _spoken : @"Choice"; }
- (id)accessibilityValue { return _titles.count ? _titles[static_cast<NSUInteger>(std::clamp<NSInteger>(_selected, 0, static_cast<NSInteger>(_titles.count)-1))] : @""; }
- (BOOL)accessibilityPerformIncrement { [self choose:_selected+1]; return YES; }
- (BOOL)accessibilityPerformDecrement { [self choose:_selected-1]; return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    fillPanel(b, b.size.height/2, color(kBackground, .6), color(kLine));
    const CGFloat width = b.size.width/static_cast<CGFloat>(_titles.count);
    for (NSUInteger i = 0; i < _titles.count; ++i) {
        const NSRect cell = NSMakeRect(static_cast<CGFloat>(i)*width, 0, width, b.size.height);
        const BOOL chosen = static_cast<NSInteger>(i) == _selected;
        if (chosen) {
            [NSGraphicsContext saveGraphicsState];
            auto* glow = [[NSShadow alloc] init];
            glow.shadowColor = [_tint colorWithAlphaComponent:.35]; glow.shadowBlurRadius = 8; glow.shadowOffset = NSZeroSize;
            [glow set];
            fillPanel(NSInsetRect(cell, 2, 2), (b.size.height-4)/2, [_tint colorWithAlphaComponent:.2], _tint);
            [NSGraphicsContext restoreGraphicsState];
        }
        const CGFloat size = 9.5;
        drawText(_titles[i], NSMakeRect(cell.origin.x, (b.size.height-size-3)/2+.5, cell.size.width, size+4), size,
                 chosen ? _tint : color(kDim), NSFontWeightBold, 1.1, NSTextAlignmentCenter);
    }
}
@end

// ---------------------------------------------------------------- menu: one of many choices, in a pop-up
@interface FXMenuField : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property (nonatomic) NSArray<NSString*>* titles;
@property (nonatomic) NSColor* tint;
@property (nonatomic) NSString* label;
@property (nonatomic) NSInteger selected;
@property NSString* spoken;
- (NSMenu*)buildMenu;
@end
@implementation FXMenuField
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setSelected:(NSInteger)selected { if (_selected != selected) { _selected = selected; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (NSMenu*)buildMenu {
    auto* menu = [[NSMenu alloc] initWithTitle:@""];
    for (NSUInteger i = 0; i < _titles.count; ++i) {
        auto* item = [[NSMenuItem alloc] initWithTitle:_titles[i] action:@selector(chosen:) keyEquivalent:@""];
        item.target = self; item.tag = static_cast<NSInteger>(i);
        item.state = static_cast<NSInteger>(i) == _selected ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    return menu;
}
- (void)chosen:(NSMenuItem*)item {
    [_host commit:_param value:static_cast<double>(item.tag)];
    self.selected = item.tag;
}
- (void)mouseDown:(NSEvent*)event {
    (void)event;
    [self.window makeFirstResponder:nil];
    NSMenu* menu = [self buildMenu];
    [menu popUpMenuPositioningItem:menu.itemArray[static_cast<NSUInteger>(std::clamp<NSInteger>(_selected, 0, static_cast<NSInteger>(_titles.count)-1))]
                        atLocation:NSMakePoint(0, self.bounds.size.height) inView:self];
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityPopUpButtonRole; }
- (NSString*)accessibilityLabel { return _spoken ? _spoken : _label; }
- (id)accessibilityValue { return _titles.count ? _titles[static_cast<NSUInteger>(std::clamp<NSInteger>(_selected, 0, static_cast<NSInteger>(_titles.count)-1))] : @""; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    const BOOL active = _selected != 0;
    fillPanel(b, b.size.height/2, active ? [_tint colorWithAlphaComponent:.16] : color(kBackground, .6), active ? _tint : color(kLine));
    NSString* text = [NSString stringWithFormat:@"%@ %@", _label, _titles.count ? _titles[static_cast<NSUInteger>(std::clamp<NSInteger>(_selected, 0, static_cast<NSInteger>(_titles.count)-1))].uppercaseString : @""];
    drawText(text, NSMakeRect(10, (b.size.height-13)/2+.5, b.size.width-26, 13), 9.5, active ? _tint : color(kDim), NSFontWeightBold, 1.0, NSTextAlignmentLeft);
    auto* chevron = [NSBezierPath bezierPath];
    const CGFloat cx = b.size.width-13, cy = b.size.height/2;
    [chevron moveToPoint:NSMakePoint(cx-3.5, cy-1.5)]; [chevron lineToPoint:NSMakePoint(cx, cy+2)]; [chevron lineToPoint:NSMakePoint(cx+3.5, cy-1.5)];
    chevron.lineWidth = 1.6; chevron.lineCapStyle = NSLineCapStyleRound; chevron.lineJoinStyle = NSLineJoinStyleRound;
    [(active ? _tint : color(kDim)) setStroke]; [chevron stroke];
}
@end

// ---------------------------------------------------------------- block panel
static const CGFloat kMargin = 24, kHeaderH = 92, kPanelW = 448, kPanelH = 186, kPanelGap = 16, kRowGap = 14, kKnobW = 100, kKnobH = 108, kKnobY = 68;

@interface FXPanel : NSView
@property (nonatomic) uint32_t block;
@property (weak) id<FXHost> host;
- (instancetype)initWithBlock:(uint32_t)block host:(id<FXHost>)host frame:(NSRect)frame;
- (void)sync;
@end
@implementation FXPanel {
    FXToggle* on_;
    FXPicker* flavor_;
    FXKnob* knobs_[4];                  // P1, P2, P3, Mix
    FXPicker* slope_;                   // the filter's 12/24 dB choice stands in for its third knob
    FXMenuField* sync_;                 // the delay's tempo sync
    uint32_t shownFlavor_;
    BOOL shownOn_;
}
- (BOOL)isFlipped { return YES; }
- (instancetype)initWithBlock:(uint32_t)block host:(id<FXHost>)host frame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _block = block; _host = host; shownFlavor_ = 99;
    NSColor* tint = color(blockTint(block));
    NSString* name = [NSString stringWithUTF8String:blockNames[block]];
    on_ = [[FXToggle alloc] initWithFrame:NSMakeRect(14, 9, 60, 32)];
    on_.host = host; on_.param = blockParam(block, On); on_.title = @"ON"; on_.tint = tint;
    on_.toolTip = [NSString stringWithFormat:@"Turns the %@ block on or off.", name.lowercaseString];
    [self addSubview:on_];
    NSMutableArray* names = [NSMutableArray array];
    for (uint32_t f = 0; f < flavorCount; ++f) [names addObject:[[NSString stringWithUTF8String:flavorNames[block][f]] uppercaseString]];
    flavor_ = [[FXPicker alloc] initWithFrame:NSMakeRect(kPanelW-14-3*82, 10, 3*82, 30)];
    flavor_.host = host; flavor_.param = blockParam(block, Flavor); flavor_.titles = names; flavor_.tint = tint;
    flavor_.spoken = [NSString stringWithFormat:@"%@ flavor", name];
    flavor_.toolTip = [NSString stringWithFormat:@"Which kind of %@ this block does.", name.lowercaseString];
    [self addSubview:flavor_];
    const BlockKey keys[4] {P1, P2, P3, BlockMix};
    const CGFloat gap = (kPanelW-4*kKnobW)/5;
    for (int k = 0; k < 4; ++k) {
        knobs_[k] = [[FXKnob alloc] initWithFrame:NSMakeRect(gap+static_cast<CGFloat>(k)*(kKnobW+gap), kKnobY, kKnobW, kKnobH)];
        knobs_[k].host = host; knobs_[k].param = blockParam(block, keys[k]); knobs_[k].tint = tint;
        [self addSubview:knobs_[k]];
    }
    if (block == FilterBlock) {
        // Two states do not make a knob: a two-segment picker sits where the third one would, under the same kind of label.
        knobs_[2].hidden = YES;
        slope_ = [[FXPicker alloc] initWithFrame:NSMakeRect(gap+2*(kKnobW+gap)+8, kKnobY+39, kKnobW-16, 26)];
        slope_.host = host; slope_.param = blockParam(block, P3); slope_.titles = @[@"12", @"24"]; slope_.tint = tint;
        slope_.spoken = @"Filter slope in decibels per octave";
        slope_.toolTip = @"How steeply the filter cuts: 12 or 24 dB per octave. 24 dB is sharper, and rings more with resonance.";
        [self addSubview:slope_];
    }
    if (block == DelayBlock) {
        NSMutableArray* names = [NSMutableArray array];
        for (uint32_t i = 0; i < syncCount; ++i) [names addObject:[NSString stringWithUTF8String:syncNames[i]]];
        sync_ = [[FXMenuField alloc] initWithFrame:NSMakeRect(kPanelW-14-112, 40, 112, 22)];
        sync_.host = host; sync_.param = DelaySync; sync_.titles = names; sync_.tint = tint; sync_.label = @"SYNC";
        sync_.spoken = @"Delay tempo sync";
        sync_.toolTip = @"Free: the Time knob sets the delay. Or lock it to a note value at the host's tempo (T is a triplet, a dot is half again as long).";
        [self addSubview:sync_];
    }
    return self;
}
- (void)sync {
    const BOOL on = [_host valueFor:blockParam(_block, On)] >= .5;
    const uint32_t flavor = static_cast<uint32_t>(std::clamp([_host valueFor:blockParam(_block, Flavor)], 0., flavorCount-1.));
    NSColor* tint = on ? color(blockTint(_block)) : color(kFaint);
    on_.on = on; flavor_.selected = flavor;
    flavor_.tint = on ? color(blockTint(_block)) : color(kDim);
    on_.tint = color(blockTint(_block));
    if (flavor != shownFlavor_ || on != shownOn_) {
        shownFlavor_ = flavor; shownOn_ = on;
        const FlavorInfo info = flavorInfo(_block, flavor);
        for (int k = 0; k < 3; ++k) {
            knobs_[k].hidden = info.knob[k] == nullptr || (_block == FilterBlock && k == 2);
            if (info.knob[k]) {
                knobs_[k].title = [NSString stringWithUTF8String:info.knob[k]];
                knobs_[k].detail = [NSString stringWithFormat:@"%s: %s", info.knob[k], info.caption];
            }
        }
        knobs_[3].title = @"MIX";
        knobs_[3].detail = ModBlock == _block || _block == FilterBlock || _block == DriveBlock || _block == WidthBlock
            ? @"MIX: how much of the effect is crossfaded in against the signal going into this block."
            : @"MIX: how loud the effect is, added to the signal going into this block, which stays at full level.";
        for (int k = 0; k < 4; ++k) knobs_[k].tint = tint;
        self.needsDisplay = YES;
    }
    for (int k = 0; k < 4; ++k) if (!knobs_[k].hidden) [knobs_[k] sync];
    if (sync_) {
        const NSInteger division = static_cast<NSInteger>([_host valueFor:DelaySync]);
        sync_.selected = division; sync_.tint = tint;
        // Synced, the Time knob is not in charge: it shows what the note value comes to at the tempo.
        FXKnob* time = knobs_[0];
        if (division > 0 && division < static_cast<NSInteger>(syncCount)) {
            const double ms = std::clamp(syncBeats[static_cast<size_t>(division)]*60000./[_host tempo], 20., 4000.);
            time.title = [NSString stringWithFormat:@"TIME · %s", syncNames[static_cast<size_t>(division)]];
            time.overrideText = ms >= 1000 ? [NSString stringWithFormat:@"%.2f s", ms*.001] : [NSString stringWithFormat:@"%.0f ms", ms];
            time.dimmed = YES;
            time.detail = @"Time while Sync is Free. With a note value chosen, the delay follows the host's tempo and this shows the time it comes to.";
        } else {
            time.title = [NSString stringWithUTF8String:flavorInfo(_block, shownFlavor_ < flavorCount ? shownFlavor_ : 0).knob[0]];
            time.overrideText = nil; time.dimmed = NO;
        }
        [time sync];
    }
    if (slope_) { slope_.selected = [_host valueFor:blockParam(_block, P3)] >= .5 ? 1 : 0; slope_.tint = tint; }
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const BOOL on = shownOn_;
    NSColor* tint = color(blockTint(_block));
    fillPanel(self.bounds, 14, color(kPanel), on ? [tint colorWithAlphaComponent:.55] : color(kLine));
    drawText([[NSString stringWithUTF8String:blockNames[_block]] uppercaseString], NSMakeRect(86, 17, 110, 16), 11,
             on ? tint : color(kDim), NSFontWeightHeavy, 1.6, NSTextAlignmentLeft);
    if (slope_) {
        const CGFloat gap = (kPanelW-4*kKnobW)/5;
        drawText(@"SLOPE · DB/OCT", NSMakeRect(gap+2*(kKnobW+gap), kKnobY+3, kKnobW, 12), 10, color(on ? kDim : kFaint), NSFontWeightSemibold, 1.0, NSTextAlignmentCenter);
    }
    if (shownFlavor_ < flavorCount)
        drawText([NSString stringWithUTF8String:flavorInfo(_block, shownFlavor_).caption], NSMakeRect(16, 46, kPanelW-32-(sync_ ? 124 : 0), 14), 10.5,
                 color(on ? kDim : kFaint), NSFontWeightRegular, 0, NSTextAlignmentLeft);
}
@end

// ---------------------------------------------------------------- the editor view
@interface FXView : NSView <FXHost> {
    EditorCallbacks cb_;
    FXPanel* panels_[BlockCount];
    FXKnob* input_; FXKnob* output_; FXKnob* mix_;
    FXToggle* bypass_;
    FXToggle* autoGain_;
    FXPicker* order_;
    NSTimer* timer_;
    Status status_;
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
- (double)valueFor:(uint32_t)pid { return cb_.get ? cb_.get(cb_.context, pid) : paramInfo(pid).initial; }
- (void)begin:(uint32_t)pid { if (cb_.edit) cb_.edit(cb_.context, pid, [self valueFor:pid], 1); }
- (void)set:(uint32_t)pid value:(double)value { if (cb_.edit) cb_.edit(cb_.context, pid, value, 0); }
- (void)end:(uint32_t)pid { if (cb_.edit) cb_.edit(cb_.context, pid, [self valueFor:pid], 2); }
- (void)commit:(uint32_t)pid value:(double)value { [self begin:pid]; [self set:pid value:value]; [self end:pid]; }
- (uint32_t)flavorOf:(uint32_t)pid {
    return isBlockParam(pid) ? static_cast<uint32_t>(std::clamp([self valueFor:blockParam(blockOf(pid), Flavor)], 0., flavorCount-1.)) : 0;
}
- (NSString*)textFor:(uint32_t)pid value:(double)value {
    char text[64];
    formatValue(text, sizeof(text), pid, value, [self flavorOf:pid]);
    return [NSString stringWithUTF8String:text];
}
- (BOOL)parse:(NSString*)text for:(uint32_t)pid into:(double*)value {
    return parseValue(pid, text.UTF8String, [self flavorOf:pid], *value);
}
- (double)tempo { return status_.tempo > 0 ? status_.tempo : 120; }

- (instancetype)initWithCallbacks:(EditorCallbacks)callbacks {
    self = [super initWithFrame:NSMakeRect(0, 0, Editor::width, Editor::height)];
    if (!self) return nil;
    cb_ = callbacks;
    self.wantsLayer = YES;
    for (uint32_t b = 0; b < BlockCount; ++b) {
        const NSRect frame = NSMakeRect(kMargin+static_cast<CGFloat>(b%2)*(kPanelW+kPanelGap), kHeaderH+kRowGap+static_cast<CGFloat>(b/2)*(kPanelH+kRowGap), kPanelW, kPanelH);
        panels_[b] = [[FXPanel alloc] initWithBlock:b host:self frame:frame];
        [self addSubview:panels_[b]];
    }
    NSColor* steel = color(0xA9BCC8);
    auto makeKnob = ^FXKnob*(uint32_t pid, NSString* title, NSString* detail, CGFloat x) {
        FXKnob* knob = [[FXKnob alloc] initWithFrame:NSMakeRect(x, 8, 72, 76)];
        knob.compact = YES; knob.host = self; knob.param = pid; knob.title = title; knob.tint = steel; knob.detail = detail;
        [self addSubview:knob];
        return knob;
    };
    input_ = makeKnob(Input, @"INPUT", @"Level into the chain. The dry signal that Mix blends back in is not touched.", 556);
    output_ = makeKnob(Output, @"OUTPUT", @"Final level of the whole box.", 640);
    mix_ = makeKnob(Mix, @"MIX", @"The whole chain against the dry signal. 0% is dry, 100% is all effect.", 724);
    auto* autoGain = [[FXToggle alloc] initWithFrame:NSMakeRect(816, 54, 120, 30)];
    autoGain.host = self; autoGain.param = AutoGain; autoGain.title = @"AUTO GAIN"; autoGain.tint = color(0x6FD6A8);
    autoGain.toolTip = @"Matches the loudness of the effected signal to the dry, so wet and dry compare at the same level. Learns over about half a second, and holds its gain when the input stops.";
    [self addSubview:autoGain];
    autoGain_ = autoGain;
    bypass_ = [[FXToggle alloc] initWithFrame:NSMakeRect(816, 12, 120, 40)];
    bypass_.host = self; bypass_.param = Bypass; bypass_.title = @"BYPASS"; bypass_.big = YES; bypass_.tint = color(0xFF5F5F);
    bypass_.toolTip = @"Takes the dry signal around everything, smoothly.";
    [self addSubview:bypass_];
    order_ = [[FXPicker alloc] initWithFrame:NSMakeRect(kMargin, 60, 260, 24)];
    order_.host = self; order_.param = FilterOrder; order_.titles = @[@"FILTER → DRIVE", @"DRIVE → FILTER"]; order_.tint = color(blockTint(FilterBlock));
    order_.spoken = @"Filter position";
    order_.toolTip = @"Where the filter sits in the chain: before the drive, or after it to tame the fizz the drive makes.";
    [self addSubview:order_];
    [self refresh];
    return self;
}

- (void)refresh {
    if (cb_.status) cb_.status(cb_.context, &status_);
    for (uint32_t b = 0; b < BlockCount; ++b) [panels_[b] sync];
    [input_ sync]; [output_ sync]; [mix_ sync];
    bypass_.on = [self valueFor:Bypass] >= .5;
    autoGain_.on = [self valueFor:AutoGain] >= .5;
    order_.selected = [self valueFor:FilterOrder] >= .5 ? 1 : 0;
    [self setNeedsDisplayInRect:NSMakeRect(480, 0, 76, kHeaderH)];
}
- (void)start {
    if (timer_) return;
    __weak FXView* weak = self;
    timer_ = [NSTimer timerWithTimeInterval:1./30 repeats:YES block:^(NSTimer*) { [weak refresh]; }];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
}
- (void)stop { [timer_ invalidate]; timer_ = nil; }
- (void)dealloc { [timer_ invalidate]; }

static void drawMeter(NSRect r, float peak, NSColor* tint) {
    fillPanel(r, r.size.height/2, color(kBackground, .8), color(kLine));
    const double dbs = 20*std::log10(std::max<double>(peak, 1e-4)), fill = std::clamp((dbs+60)/60, 0., 1.);
    if (fill > .01) {
        NSRect bar = NSInsetRect(r, 2, 2); bar.size.width *= static_cast<CGFloat>(fill);
        NSColor* c = dbs > -1 ? color(0xFF5F5F) : tint;
        fillPanel(bar, bar.size.height/2, [c colorWithAlphaComponent:.85], nil);
    }
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [color(kBackground) setFill]; NSRectFill(self.bounds);
    drawText(@"FX BLOCK", NSMakeRect(kMargin, 16, 300, 30), 24, color(kText), NSFontWeightHeavy, 4, NSTextAlignmentLeft);
    drawText(@"SWISS-ARMY EFFECTS BOX", NSMakeRect(kMargin+1, 46, 240, 12), 8.5, color(kFaint), NSFontWeightSemibold, 1.6, NSTextAlignmentLeft);
    // levels, in and out, in the gap beside the knobs
    drawText(@"IN", NSMakeRect(300, 28, 20, 10), 8, color(kDim), NSFontWeightBold, 1.2, NSTextAlignmentLeft);
    drawText(@"OUT", NSMakeRect(300, 50, 24, 10), 8, color(kDim), NSFontWeightBold, 1.2, NSTextAlignmentLeft);
    drawMeter(NSMakeRect(326, 26, 200, 12), status_.inputPeak, color(0xA9BCC8));
    drawMeter(NSMakeRect(326, 48, 200, 12), status_.outputPeak, color(0x6FD6A8));
    drawText([NSString stringWithFormat:@"v1.0 · %u parameters", exposedCount], NSMakeRect(kMargin, Editor::height-24, 400, 12), 8.5,
             color(kFaint), NSFontWeightMedium, 1.2, NSTextAlignmentLeft);
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
