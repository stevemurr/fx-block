// Shared pieces of the editor: the palette, text drawing, the host protocol every control talks to, and the widgets.
#pragma once
#import <AppKit/AppKit.h>
#include "../Chain.h"
#include "../Parameters.h"

// ---------------------------------------------------------------- palette & text
NSColor* uiColor(unsigned rgb, CGFloat alpha = 1);
extern const unsigned kBackground, kPanel, kPanelHi, kLine, kTrack, kText, kDim, kFaint, kRed;
unsigned blockTint(uint32_t block);
void uiDrawText(NSString* string, NSRect rect, CGFloat size, NSColor* tint, NSFontWeight weight, CGFloat kern, NSTextAlignment alignment);
void uiFillPanel(NSRect r, CGFloat radius, NSColor* fill, NSColor* stroke);
// NSRectFill copies its color over what is there, ignoring alpha; this blends.
void uiFillRect(NSRect r, NSColor* color);

// ---------------------------------------------------------------- host protocol
@protocol FXHost <NSObject>
- (double)valueFor:(uint32_t)pid;
- (void)begin:(uint32_t)pid;
- (void)set:(uint32_t)pid value:(double)value;
- (void)end:(uint32_t)pid;
- (void)commit:(uint32_t)pid value:(double)value;           // begin + set + end
- (NSString*)textFor:(uint32_t)pid value:(double)value;     // in the units of the parameter's block's flavor
- (BOOL)parse:(NSString*)text for:(uint32_t)pid into:(double*)value;
- (fxblock::Status)status;
- (fxblock::Values)allValues;                                 // every parameter now, for the graphs
- (double)tempo;                                              // the host's, or 120 until it says
@end

// ---------------------------------------------------------------- widgets
// A plain container whose coordinates run from the top, like the rest of the editor's layout.
@interface FXFlipped : NSView
@end

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

@interface FXToggle : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property NSString* title;
@property (nonatomic) NSColor* tint;
@property BOOL big;
@property (nonatomic) BOOL on;
- (void)sync;
@end

@interface FXPicker : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property (nonatomic) NSArray<NSString*>* titles;
@property (nonatomic) NSColor* tint;
@property (nonatomic) NSInteger selected;
@property NSString* spoken;
@property (copy) void (^onChange)(NSInteger);       // for a picker that is not a parameter
- (void)sync;
@end

@interface FXMenuField : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t param;
@property (nonatomic) NSArray<NSString*>* titles;
@property (nonatomic) NSColor* tint;
@property (nonatomic) NSString* label;
@property (nonatomic) NSInteger selected;
@property NSString* spoken;
- (NSMenu*)buildMenu;
- (void)sync;
@end

// A level bar: horizontal or vertical, 0..1.
@interface FXMeter : NSView
@property (nonatomic) float value;
@property (nonatomic) BOOL vertical, bipolar;
@property (nonatomic) NSColor* tint;
@end

// ---------------------------------------------------------------- graphs (src/ui/Graphs.mm), one per block
@interface FXGraph : NSView
@property (weak) id<FXHost> host;
@property (nonatomic) uint32_t block;
- (void)sync;
@end
@interface FXFilterGraph : FXGraph
@end
@interface FXDriveGraph : FXGraph
@end
@interface FXModGraph : FXGraph
@end
@interface FXDelayGraph : FXGraph
@end
@interface FXReverbGraph : FXGraph
@end
@interface FXWidthGraph : FXGraph
@end
