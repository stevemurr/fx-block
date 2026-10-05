#import <QuartzCore/QuartzCore.h>
#include "Ui.h"
#include <algorithm>
#include <cmath>

using namespace fxblock;

// ---------------------------------------------------------------- palette & text
NSColor* uiColor(unsigned rgb, CGFloat alpha) {
    return [NSColor colorWithSRGBRed:((rgb>>16)&255)/255. green:((rgb>>8)&255)/255. blue:(rgb&255)/255. alpha:alpha];
}
const unsigned kBackground = 0x0C1012, kPanel = 0x131A1D, kPanelHi = 0x182125, kLine = 0x25323A, kTrack = 0x212C32;
const unsigned kText = 0xE9EEEA, kDim = 0x82938B, kFaint = 0x56665F, kRed = 0xFF5F5F;
unsigned blockTint(uint32_t block) {
    static const unsigned tints[BlockCount] {0x7CC4E8, 0xFF8A7A, 0x6FD6A8, 0xF0C96B, 0xB59CF0, 0xE88AC6};
    return tints[block % BlockCount];
}
void uiDrawText(NSString* string, NSRect rect, CGFloat size, NSColor* tint, NSFontWeight weight, CGFloat kern, NSTextAlignment alignment) {
    auto* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = alignment; style.lineBreakMode = NSLineBreakByClipping;
    [string drawInRect:rect withAttributes:@{
        NSFontAttributeName: [NSFont systemFontOfSize:size weight:weight],
        NSForegroundColorAttributeName: tint, NSKernAttributeName: @(kern), NSParagraphStyleAttributeName: style}];
}
void uiFillRect(NSRect r, NSColor* color) { [color setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }
void uiFillPanel(NSRect r, CGFloat radius, NSColor* fill, NSColor* stroke) {
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

@implementation FXFlipped
- (BOOL)isFlipped { return YES; }
@end

// ---------------------------------------------------------------- knob
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
    field_.textColor = uiColor(kText); field_.backgroundColor = NSColor.clearColor;
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
    field_.font = [NSFont monospacedDigitSystemFontOfSize:compact ? 10 : 12 weight:NSFontWeightMedium];
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
    const double value = fromNormalized(_param, cont_);
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
    uiDrawText(_title, NSMakeRect(0, b.size.height-(_compact ? 13 : 15), b.size.width, 12), _compact ? 8.5 : 10, uiColor(kDim),
               NSFontWeightSemibold, _compact ? 1 : 1.6, NSTextAlignmentCenter);
    const NSPoint c = NSMakePoint(b.size.width/2, b.size.height-(_compact ? 38 : 52));
    auto* track = [NSBezierPath bezierPath];
    [track appendBezierPathWithArcWithCenter:c radius:radius startAngle:225 endAngle:-45 clockwise:YES];
    track.lineWidth = line; track.lineCapStyle = NSLineCapStyleRound;
    [uiColor(kTrack) setStroke]; [track stroke];
    // A parameter that runs either side of zero (a gain) grows its arc from the middle.
    const auto info = paramInfo(_param);
    const BOOL bipolar = info.min < 0 && info.max > 0 && !info.log;
    const double from = bipolar ? (0-info.min)/(info.max-info.min) : 0;
    if (std::abs(pos_-from) > .004) {
        auto* arc = [NSBezierPath bezierPath];
        [arc appendBezierPathWithArcWithCenter:c radius:radius startAngle:225-270*from endAngle:225-270*pos_ clockwise:pos_ > from];
        arc.lineWidth = line; arc.lineCapStyle = NSLineCapStyleRound;
        [NSGraphicsContext saveGraphicsState];
        auto* glow = [[NSShadow alloc] init];
        glow.shadowColor = [_tint colorWithAlphaComponent:.5]; glow.shadowBlurRadius = _compact ? 6 : 9; glow.shadowOffset = NSZeroSize;
        [glow set]; [_tint setStroke]; [arc stroke];
        [NSGraphicsContext restoreGraphicsState];
    }
    const CGFloat body = radius-(_compact ? 5.5 : 8);
    auto* disc = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x-body, c.y-body, 2*body, 2*body)];
    [[[NSGradient alloc] initWithStartingColor:uiColor(0x2B363C) endingColor:uiColor(0x12181B)] drawInBezierPath:disc angle:-90];
    [uiColor(0x3A4851) setStroke]; disc.lineWidth = 1; [disc stroke];
    const double angle = (225-270*pos_)*M_PI/180;
    auto* tick = [NSBezierPath bezierPath];
    [tick moveToPoint:NSMakePoint(c.x+std::cos(angle)*(_compact ? 3 : 5), c.y+std::sin(angle)*(_compact ? 3 : 5))];
    [tick lineToPoint:NSMakePoint(c.x+std::cos(angle)*(body-(_compact ? 2 : 3)), c.y+std::sin(angle)*(body-(_compact ? 2 : 3)))];
    tick.lineWidth = _compact ? 2 : 2.5; tick.lineCapStyle = NSLineCapStyleRound;
    [uiColor(0xF4F1EA) setStroke]; [tick stroke];
}
@end

// ---------------------------------------------------------------- toggle
@implementation FXToggle
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setOn:(BOOL)on { if (_on != on) { _on = on; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (void)sync { if (_param < ParamCount) self.on = [_host valueFor:_param] >= .5; }
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
    [(_on ? [_tint colorWithAlphaComponent:.2] : uiColor(kBackground, .6)) setFill]; [path fill];
    [NSGraphicsContext restoreGraphicsState];
    [(_on ? _tint : uiColor(kLine)) setStroke]; path.lineWidth = 1; [path stroke];
    const CGFloat size = _big ? 13 : 9, height = size+4;
    uiDrawText(_title, NSMakeRect(r.origin.x, r.origin.y+(r.size.height-height)/2+(_big ? 0 : .5), r.size.width, height), size,
               _on ? _tint : uiColor(kDim), NSFontWeightBold, _big ? 2.4 : 1.2, NSTextAlignmentCenter);
}
@end

// ---------------------------------------------------------------- picker
@implementation FXPicker
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setSelected:(NSInteger)selected { if (_selected != selected) { _selected = selected; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (void)setTitles:(NSArray<NSString*>*)titles { _titles = titles; self.needsDisplay = YES; }
- (void)sync { if (_param < ParamCount) self.selected = static_cast<NSInteger>([_host valueFor:_param]); }
- (void)choose:(NSInteger)index {
    index = std::clamp<NSInteger>(index, 0, static_cast<NSInteger>(_titles.count)-1);
    if (index == _selected) return;
    if (_param < ParamCount) [_host commit:_param value:static_cast<double>(index)];
    self.selected = index;
    if (_onChange) _onChange(index);
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
    uiFillPanel(b, b.size.height/2, uiColor(kBackground, .6), uiColor(kLine));
    const CGFloat width = b.size.width/static_cast<CGFloat>(_titles.count);
    for (NSUInteger i = 0; i < _titles.count; ++i) {
        const NSRect cell = NSMakeRect(static_cast<CGFloat>(i)*width, 0, width, b.size.height);
        const BOOL chosen = static_cast<NSInteger>(i) == _selected;
        if (chosen) {
            [NSGraphicsContext saveGraphicsState];
            auto* glow = [[NSShadow alloc] init];
            glow.shadowColor = [_tint colorWithAlphaComponent:.35]; glow.shadowBlurRadius = 8; glow.shadowOffset = NSZeroSize;
            [glow set];
            uiFillPanel(NSInsetRect(cell, 2, 2), (b.size.height-4)/2, [_tint colorWithAlphaComponent:.2], _tint);
            [NSGraphicsContext restoreGraphicsState];
        }
        const CGFloat size = 9.5;
        uiDrawText(_titles[i], NSMakeRect(cell.origin.x, (b.size.height-size-3)/2+.5, cell.size.width, size+4), size,
                   chosen ? _tint : uiColor(kDim), NSFontWeightBold, 1.1, NSTextAlignmentCenter);
    }
}
@end

// ---------------------------------------------------------------- menu
@implementation FXMenuField
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)setSelected:(NSInteger)selected { if (_selected != selected) { _selected = selected; self.needsDisplay = YES; } }
- (void)setTint:(NSColor*)tint { if (![_tint isEqual:tint]) { _tint = tint; self.needsDisplay = YES; } }
- (void)sync { if (_param < ParamCount) self.selected = static_cast<NSInteger>([_host valueFor:_param]); }
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
- (void)scrollWheel:(NSEvent*)event {
    if (std::abs(event.scrollingDeltaY) < (event.hasPreciseScrollingDeltas ? 3 : .5)) return;
    const NSInteger next = std::clamp<NSInteger>(_selected+(event.scrollingDeltaY > 0 ? -1 : 1), 0, static_cast<NSInteger>(_titles.count)-1);
    if (next != _selected) { [_host commit:_param value:static_cast<double>(next)]; self.selected = next; }
}
- (BOOL)isAccessibilityElement { return YES; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityPopUpButtonRole; }
- (NSString*)accessibilityLabel { return _spoken ? _spoken : _label; }
- (id)accessibilityValue { return _titles.count ? _titles[static_cast<NSUInteger>(std::clamp<NSInteger>(_selected, 0, static_cast<NSInteger>(_titles.count)-1))] : @""; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    uiFillPanel(b, b.size.height/2, [_tint colorWithAlphaComponent:.12], [_tint colorWithAlphaComponent:.7]);
    NSString* value = _titles.count ? _titles[static_cast<NSUInteger>(std::clamp<NSInteger>(_selected, 0, static_cast<NSInteger>(_titles.count)-1))].uppercaseString : @"";
    NSString* text = _label.length ? [NSString stringWithFormat:@"%@  %@", _label, value] : value;
    uiDrawText(text, NSMakeRect(10, (b.size.height-13)/2+.5, b.size.width-26, 13), 9.5, _tint, NSFontWeightBold, 1, NSTextAlignmentLeft);
    auto* chevron = [NSBezierPath bezierPath];
    const CGFloat cx = b.size.width-13, cy = b.size.height/2;
    [chevron moveToPoint:NSMakePoint(cx-3.5, cy-1.5)]; [chevron lineToPoint:NSMakePoint(cx, cy+2)]; [chevron lineToPoint:NSMakePoint(cx+3.5, cy-1.5)];
    chevron.lineWidth = 1.6; chevron.lineCapStyle = NSLineCapStyleRound; chevron.lineJoinStyle = NSLineJoinStyleRound;
    [_tint setStroke]; [chevron stroke];
}
@end

// ---------------------------------------------------------------- meter
@implementation FXMeter
- (BOOL)isFlipped { return YES; }
- (void)setValue:(float)value { if (value != _value) { _value = value; self.needsDisplay = YES; } }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    uiFillPanel(b, 3, uiColor(kBackground, .8), uiColor(kLine));
    const NSRect inner = NSInsetRect(b, 2, 2);
    NSColor* tint = _tint ? _tint : uiColor(0xA9BCC8);
    const CGFloat length = _vertical ? inner.size.height : inner.size.width;
    // A bar from `from` to `to` along the meter's length, 0 at the start (the top of a vertical one).
    auto bar = [&](CGFloat from, CGFloat to) {
        const CGFloat a = std::min(from, to), size = std::abs(to-from);
        return _vertical ? NSMakeRect(inner.origin.x, inner.origin.y+a, inner.size.width, size) : NSMakeRect(inner.origin.x+a, inner.origin.y, size, inner.size.height);
    };
    const CGFloat clamped = static_cast<CGFloat>(std::clamp(_value, _bipolar ? -1.f : 0.f, 1.f));
    if (_bipolar) {
        const CGFloat mid = length/2, end = _vertical ? mid-clamped*mid : mid+clamped*mid;      // up is positive on a vertical meter
        if (std::abs(clamped) > .01) uiFillRect(bar(mid, end), [tint colorWithAlphaComponent:.9]);
        uiFillRect(bar(mid-.5, mid+.5), uiColor(kFaint));
    } else {
        const CGFloat size = clamped*length;
        if (size > .5) uiFillRect(_vertical ? bar(length-size, length) : bar(0, size), [tint colorWithAlphaComponent:.9]);
    }
}
@end
