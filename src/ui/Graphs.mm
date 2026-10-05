#include "Ui.h"
#include "../Blocks.h"
#include <algorithm>
#include <cmath>

using namespace fxblock;

namespace {
struct Plot {
    NSRect r;
    double minX = 20, maxX = 20000, minY = -18, maxY = 18;
    bool logX = true, logY = false;
    CGFloat x(double v) const {
        const double t = logX ? std::log(v/minX)/std::log(maxX/minX) : (v-minX)/(maxX-minX);
        return r.origin.x+r.size.width*static_cast<CGFloat>(t);
    }
    CGFloat y(double v) const {
        const double t = logY ? std::log(v/minY)/std::log(maxY/minY) : (v-minY)/(maxY-minY);
        return r.origin.y+r.size.height*static_cast<CGFloat>(1-t);
    }
};
NSRect plotRect(NSRect bounds) { return NSMakeRect(bounds.origin.x+44, bounds.origin.y+30, bounds.size.width-62, bounds.size.height-58); }

// Frequency (log) against dB, the grid and its labels.
void drawResponseGrid(const Plot& p, double dbStep) {
    uiFillPanel(p.r, 4, uiColor(kBackground, .75), nil);
    auto* grid = [NSBezierPath bezierPath];
    for (double d = std::ceil(p.minY/dbStep)*dbStep; d <= p.maxY+1e-9; d += dbStep) { [grid moveToPoint:NSMakePoint(p.r.origin.x, p.y(d))]; [grid lineToPoint:NSMakePoint(NSMaxX(p.r), p.y(d))]; }
    for (double f : {30., 40., 50., 60., 70., 80., 90., 100., 200., 300., 400., 500., 600., 700., 800., 900., 1000., 2000., 3000., 4000., 5000., 6000., 7000., 8000., 9000., 10000.}) {
        [grid moveToPoint:NSMakePoint(p.x(f), p.r.origin.y)]; [grid lineToPoint:NSMakePoint(p.x(f), NSMaxY(p.r))];
    }
    grid.lineWidth = .5; [uiColor(kLine, .55) setStroke]; [grid stroke];
    if (p.minY < 0 && p.maxY > 0) {
        auto* zero = [NSBezierPath bezierPath];
        [zero moveToPoint:NSMakePoint(p.r.origin.x, p.y(0))]; [zero lineToPoint:NSMakePoint(NSMaxX(p.r), p.y(0))];
        zero.lineWidth = 1; [uiColor(kFaint) setStroke]; [zero stroke];
    }
    for (double d = std::ceil(p.minY/dbStep)*dbStep; d <= p.maxY+1e-9; d += dbStep)
        uiDrawText([NSString stringWithFormat:@"%+.0f", d], NSMakeRect(p.r.origin.x-38, p.y(d)-6, 32, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 0, NSTextAlignmentRight);
    for (double f : {100., 1000., 10000.})
        uiDrawText(f >= 1000 ? [NSString stringWithFormat:@"%.0fk", f/1000] : [NSString stringWithFormat:@"%.0f", f], NSMakeRect(p.x(f)-20, NSMaxY(p.r)+4, 40, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 0, NSTextAlignmentCenter);
}
void drawCaption(NSString* text, NSRect bounds) {
    uiDrawText(text.uppercaseString, NSMakeRect(bounds.origin.x+16, bounds.origin.y+9, bounds.size.width-32, 11), 8.5, uiColor(kFaint), NSFontWeightBold, .9, NSTextAlignmentLeft);
}
NSBezierPath* polyline(const std::vector<NSPoint>& points) {
    auto* path = [NSBezierPath bezierPath];
    for (size_t i = 0; i < points.size(); ++i) { if (i == 0) [path moveToPoint:points[i]]; else [path lineToPoint:points[i]]; }
    path.lineJoinStyle = NSLineJoinStyleRound;
    return path;
}
std::vector<double> logFrequencies(int n = 260) {
    std::vector<double> f(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) f[static_cast<size_t>(i)] = 20*std::pow(1000., i/static_cast<double>(n-1));
    return f;
}
NSString* hzText(double hz) { return hz >= 1000 ? [NSString stringWithFormat:@"%.2f kHz", hz*.001] : [NSString stringWithFormat:@"%.0f Hz", hz]; }
NSString* msText(double ms) { return ms >= 1000 ? [NSString stringWithFormat:@"%.2f s", ms*.001] : (ms < 10 ? [NSString stringWithFormat:@"%.2f ms", ms] : [NSString stringWithFormat:@"%.0f ms", ms]); }
}

// ---------------------------------------------------------------- base
@implementation FXGraph {
    Values values_;
    Status status_;
}
- (BOOL)isFlipped { return YES; }
- (void)sync { values_ = [self.host allValues]; status_ = [self.host status]; self.needsDisplay = YES; }
- (const Values&)values { return values_; }
- (const Status&)status { return status_; }
- (double)value:(BlockKey)key { return values_[blockParam(self.block, key)]; }
- (uint32_t)flavor { return static_cast<uint32_t>(std::clamp(values_[blockParam(self.block, Flavor)], 0., flavorCount-1.)); }
- (BOOL)on { return values_[blockParam(self.block, On)] >= .5; }
- (NSColor*)tint { return [uiColor(blockTint(self.block)) colorWithAlphaComponent:[self on] ? 1 : .45]; }
- (NSColor*)soft:(CGFloat)alpha { return [uiColor(blockTint(self.block)) colorWithAlphaComponent:alpha*([self on] ? 1 : .5)]; }
@end

@interface FXGraph (Reading)
- (const Values&)values;
- (const Status&)status;
- (double)value:(BlockKey)key;
- (uint32_t)flavor;
- (BOOL)on;
- (NSColor*)tint;
- (NSColor*)soft:(CGFloat)alpha;
@end

// ---------------------------------------------------------------- filter: the response
@implementation FXFilterGraph
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    uiFillPanel(self.bounds, 12, uiColor(kPanel), uiColor(kLine));
    Plot p; p.r = plotRect(self.bounds); p.minY = -42; p.maxY = 30;
    drawResponseGrid(p, 12);
    const double cutoff = [self value:P1], resonance = [self value:P2]*.01;
    const bool steep = [self value:P3] >= .5;
    const uint32_t flavor = [self flavor];
    const auto freqs = logFrequencies();
    std::vector<NSPoint> line;
    for (double f : freqs) line.push_back(NSMakePoint(p.x(f), p.y(std::clamp(FilterFx::responseDb(flavor, cutoff, resonance, steep, f, 48000), p.minY-6, p.maxY+6))));
    [NSGraphicsContext saveGraphicsState];
    [NSBezierPath clipRect:p.r];
    auto* fill = polyline(line);
    [fill lineToPoint:NSMakePoint(p.x(freqs.back()), NSMaxY(p.r))]; [fill lineToPoint:NSMakePoint(p.x(freqs.front()), NSMaxY(p.r))]; [fill closePath];
    [[self soft:.16] setFill]; [fill fill];
    auto* stroke = polyline(line);
    stroke.lineWidth = 2.4; [[self tint] setStroke]; [stroke stroke];
    // the cutoff
    auto* mark = [NSBezierPath bezierPath];
    [mark moveToPoint:NSMakePoint(p.x(cutoff), p.r.origin.y)]; [mark lineToPoint:NSMakePoint(p.x(cutoff), NSMaxY(p.r))];
    const CGFloat dash[2] {4, 4}; [mark setLineDash:dash count:2 phase:0];
    mark.lineWidth = 1; [[self soft:.55] setStroke]; [mark stroke];
    [NSGraphicsContext restoreGraphicsState];
    uiDrawText(hzText(cutoff), NSMakeRect(std::min(p.x(cutoff)+6, NSMaxX(p.r)-80), p.r.origin.y+6, 80, 12), 9, [self tint], NSFontWeightBold, .6, NSTextAlignmentLeft);
    drawCaption([NSString stringWithFormat:@"Frequency response · %s · %s", flavorNames[FilterBlock][flavor], steep ? "24 dB/oct" : "12 dB/oct"], self.bounds);
}
@end

// ---------------------------------------------------------------- drive: the curve and one cycle through it
@implementation FXDriveGraph
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    uiFillPanel(self.bounds, 12, uiColor(kPanel), uiColor(kLine));
    const uint32_t flavor = [self flavor];
    const double drive = [self value:P1], tone = [self value:P2];
    const CGFloat side = self.bounds.size.height-58;
    NSRect square = NSMakeRect(44, 30, side, side);
    NSRect wave = NSMakeRect(44+side+54, 30, self.bounds.size.width-(44+side+54)-18, side);
    const double extent = 1.15;
    auto X = [&](double v) { return square.origin.x+square.size.width*static_cast<CGFloat>((v+extent)/(2*extent)); };
    auto Y = [&](double v) { return square.origin.y+square.size.height*static_cast<CGFloat>(1-(v+extent)/(2*extent)); };
    // What the block does to a value in -1..1 (a sine of full amplitude into the clipper, scaled to fill the plot), and its
    // staircase for the crusher.
    const double gain = DriveFx::inputGain(flavor, drive), c0 = flavor < 2 ? DriveFx::clip(flavor, 0) : 0;
    const double bits = crushBits(drive), steps = std::exp2(bits-1), divisor = crushDivisor(tone);
    double norm = 1;
    if (flavor < 2) norm = std::max({std::abs(DriveFx::clip(flavor, gain)-c0), std::abs(DriveFx::clip(flavor, -gain)-c0), 1e-9});
    auto shape = [&](double x) { return flavor < 2 ? (DriveFx::clip(flavor, gain*x)-c0)/norm : std::round(x*steps)/steps; };
    // ---- the transfer curve
    uiFillPanel(square, 4, uiColor(kBackground, .75), nil);
    auto* grid = [NSBezierPath bezierPath];
    for (double v : {-1., -.5, 0., .5, 1.}) { [grid moveToPoint:NSMakePoint(X(v), square.origin.y)]; [grid lineToPoint:NSMakePoint(X(v), NSMaxY(square))]; [grid moveToPoint:NSMakePoint(square.origin.x, Y(v))]; [grid lineToPoint:NSMakePoint(NSMaxX(square), Y(v))]; }
    grid.lineWidth = .5; [uiColor(kLine, .6) setStroke]; [grid stroke];
    auto* unity = [NSBezierPath bezierPath];
    [unity moveToPoint:NSMakePoint(X(-1), Y(-1))]; [unity lineToPoint:NSMakePoint(X(1), Y(1))];
    unity.lineWidth = 1; [uiColor(kFaint) setStroke]; [unity stroke];
    std::vector<NSPoint> curve;
    for (double x = -1; x <= 1+1e-9; x += .004) curve.push_back(NSMakePoint(X(x), Y(shape(x))));
    auto* c = polyline(curve);
    c.lineWidth = 2.4; [[self tint] setStroke]; [c stroke];
    // ---- one cycle, in and out (two cycles for the crusher, so its steps and holds show)
    uiFillPanel(wave, 4, uiColor(kBackground, .75), nil);
    const double cycles = 2;
    auto WX = [&](double t) { return wave.origin.x+wave.size.width*static_cast<CGFloat>(t/cycles); };
    auto WY = [&](double v) { return wave.origin.y+wave.size.height*static_cast<CGFloat>(1-(v+extent)/(2*extent)); };
    auto* mid = [NSBezierPath bezierPath];
    [mid moveToPoint:NSMakePoint(wave.origin.x, WY(0))]; [mid lineToPoint:NSMakePoint(NSMaxX(wave), WY(0))];
    mid.lineWidth = .5; [uiColor(kFaint) setStroke]; [mid stroke];
    std::vector<NSPoint> in, out;
    const int perCycle = 64;
    double held = 0;
    for (int i = 0; i <= static_cast<int>(cycles*perCycle*8); ++i) {
        const double t = i/(8.*perCycle), s = std::sin(2*fxblock::pi*t);
        in.push_back(NSMakePoint(WX(t), WY(s)));
        double y;
        if (flavor < 2) y = shape(s);
        else { const double sampleIndex = std::floor(t*perCycle/divisor)*divisor/perCycle; held = std::sin(2*fxblock::pi*sampleIndex); y = std::round(held*steps)/steps; }
        out.push_back(NSMakePoint(WX(t), WY(y)));
    }
    auto* a = polyline(in); a.lineWidth = 1.2; [uiColor(kDim, .7) setStroke]; [a stroke];
    auto* b = polyline(out); b.lineWidth = 2.2; [[self tint] setStroke]; [b stroke];
    NSString* what = flavor < 2 ? [NSString stringWithFormat:@"+%.0f dB into the clip", 20*std::log10(gain)] : [NSString stringWithFormat:@"%.1f bits · holds each sample %.1f frames", bits, divisor];
    uiDrawText(@"TRANSFER CURVE", NSMakeRect(44, 9, 300, 11), 8.5, uiColor(kFaint), NSFontWeightBold, .9, NSTextAlignmentLeft);
    uiDrawText([NSString stringWithFormat:@"A SINE IN (DIM) AND OUT · %@", what].uppercaseString, NSMakeRect(wave.origin.x, 9, wave.size.width, 11), 8.5, uiColor(kFaint), NSFontWeightBold, .9, NSTextAlignmentLeft);
}
@end

// ---------------------------------------------------------------- modulation: the sweep over two cycles
@implementation FXModGraph
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    uiFillPanel(self.bounds, 12, uiColor(kPanel), uiColor(kLine));
    const uint32_t flavor = [self flavor];
    const double rate = [self value:P1], depth = [self value:P2]*.01, feedback = [self value:P3]*.01;
    Plot p; p.r = plotRect(self.bounds);
    p.logX = false; p.minX = 0; p.maxX = 2/rate;
    if (flavor == 2) { p.logY = true; p.minY = 100; p.maxY = 10000; }
    else { p.minY = 0; p.maxY = flavor == 0 ? 25 : 12; }
    uiFillPanel(p.r, 4, uiColor(kBackground, .75), nil);
    auto* grid = [NSBezierPath bezierPath];
    const std::vector<double> ticks = flavor == 2 ? std::vector<double> {100, 1000, 10000} : (flavor == 0 ? std::vector<double> {0, 5, 10, 15, 20, 25} : std::vector<double> {0, 2, 4, 6, 8, 10});
    for (double v : ticks) { [grid moveToPoint:NSMakePoint(p.r.origin.x, p.y(std::max(v, p.logY ? 100. : 0.)))]; [grid lineToPoint:NSMakePoint(NSMaxX(p.r), p.y(std::max(v, p.logY ? 100. : 0.)))]; }
    for (int c = 0; c <= 2; ++c) { [grid moveToPoint:NSMakePoint(p.x(c/rate), p.r.origin.y)]; [grid lineToPoint:NSMakePoint(p.x(c/rate), NSMaxY(p.r))]; }
    grid.lineWidth = .5; [uiColor(kLine, .55) setStroke]; [grid stroke];
    for (double v : ticks) uiDrawText(flavor == 2 ? (v >= 1000 ? [NSString stringWithFormat:@"%.0fk", v/1000] : [NSString stringWithFormat:@"%.0f", v]) : [NSString stringWithFormat:@"%.0f", v], NSMakeRect(p.r.origin.x-38, p.y(std::max(v, p.logY ? 100. : 0.))-6, 32, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 0, NSTextAlignmentRight);
    uiDrawText([NSString stringWithFormat:@"%.2f s", 1/rate], NSMakeRect(p.x(1/rate)-30, NSMaxY(p.r)+4, 60, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 0, NSTextAlignmentCenter);
    [NSGraphicsContext saveGraphicsState];
    [NSBezierPath clipRect:p.r];
    for (int tap = 0; tap < (flavor == 0 ? 2 : 1); ++tap) {
        std::vector<NSPoint> line;
        for (int i = 0; i <= 400; ++i) { const double phase = 2*i/400.; line.push_back(NSMakePoint(p.x(phase/rate), p.y(ModulationFx::sweep(flavor, depth, phase, tap)))); }
        auto* path = polyline(line);
        path.lineWidth = 2.2; [[[self tint] colorWithAlphaComponent:tap ? .55 : 1] setStroke]; [path stroke];
    }
    [NSGraphicsContext restoreGraphicsState];
    // What the loop does: the feedback it is allowed in this flavor.
    const double loop = flavor == 0 ? .4*feedback : flavor == 1 ? .88*feedback : .8*feedback;
    NSString* what = flavor == 0 ? @"the delay of two taps, in ms" : flavor == 1 ? @"the delay of the sweep, in ms" : @"the center frequency of the notches, in Hz";
    drawCaption([NSString stringWithFormat:@"%s · %@ · %.2f Hz · feedback loop gain %.0f%%", flavorNames[ModBlock][flavor], what, rate, loop*100], self.bounds);
}
@end

// ---------------------------------------------------------------- delay: the repeats
@implementation FXDelayGraph
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    uiFillPanel(self.bounds, 12, uiColor(kPanel), uiColor(kLine));
    const uint32_t flavor = [self flavor];
    const double knob = [self value:P1], fb = [self value:P2]*.01, mix = [self value:BlockMix]*.01;
    const uint32_t sync = static_cast<uint32_t>([self values][DelaySync]);
    const double ms = DelayFx::effectiveMs(knob, sync, [self.host tempo]);
    // The repeats: level n is the mix times feedback^n, until it is 60 dB down (the tone and the saturation reshape them).
    std::vector<double> levels;
    const double mixDb = 20*std::log10(std::max(mix, 1e-4));
    for (int n = 1; n <= 24; ++n) {
        const double l = mixDb+(fb < .01 ? (n == 1 ? 0. : -999.) : (n-1)*20*std::log10(fb));
        if (l < -60) break;
        levels.push_back(l);
    }
    if (levels.empty()) levels.push_back(-60);
    const double span = std::min(ms*(static_cast<double>(levels.size())+.8), 12000.), end = std::max(span, ms*3.2);
    const CGFloat laneHeight = (self.bounds.size.height-74)/2;
    for (int lane = 0; lane < 2; ++lane) {
        Plot p; p.logX = false; p.minX = 0; p.maxX = end; p.minY = -60; p.maxY = 0;
        p.r = NSMakeRect(44, 30+lane*(laneHeight+14), self.bounds.size.width-62, laneHeight);
        uiFillPanel(p.r, 4, uiColor(kBackground, .75), nil);
        auto* grid = [NSBezierPath bezierPath];
        for (double d : {-20., -40.}) { [grid moveToPoint:NSMakePoint(p.r.origin.x, p.y(d))]; [grid lineToPoint:NSMakePoint(NSMaxX(p.r), p.y(d))]; }
        grid.lineWidth = .5; [uiColor(kLine, .55) setStroke]; [grid stroke];
        uiDrawText(lane == 0 ? @"L" : @"R", NSMakeRect(p.r.origin.x-24, p.r.origin.y+laneHeight/2-6, 16, 12), 9, uiColor(kDim), NSFontWeightBold, 0, NSTextAlignmentRight);
        // the dry sound at 0
        auto* dry = [NSBezierPath bezierPath];
        [dry moveToPoint:NSMakePoint(p.x(0), p.y(-60))]; [dry lineToPoint:NSMakePoint(p.x(0), p.y(0))];
        dry.lineWidth = 3; [uiColor(kDim, .8) setStroke]; [dry stroke];
        for (size_t n = 0; n < levels.size(); ++n) {
            const bool onThisSide = flavor == 1 ? (n % 2 == lane) : true;                           // ping-pong: the first echo left, the second right
            if (!onThisSide) continue;
            const double t = ms*(static_cast<double>(n)+1);
            if (t > end) break;
            const CGFloat x = p.x(t), top = p.y(std::max(levels[n], -60.));
            auto* bar = [NSBezierPath bezierPath];
            [bar moveToPoint:NSMakePoint(x, p.y(-60))]; [bar lineToPoint:NSMakePoint(x, top)];
            bar.lineWidth = 3; bar.lineCapStyle = NSLineCapStyleRound; [[self tint] setStroke]; [bar stroke];
            if (flavor == 2) {                                                                         // a tape's time drifts
                auto* wobble = [NSBezierPath bezierPath];
                [wobble moveToPoint:NSMakePoint(x-5, top)]; [wobble lineToPoint:NSMakePoint(x+5, top)];
                wobble.lineWidth = 1.2; [[self soft:.7] setStroke]; [wobble stroke];
            }
            if (n < 6) uiDrawText([NSString stringWithFormat:@"%.0f", levels[n]], NSMakeRect(x-18, top-14, 36, 11), 8, uiColor(kDim), NSFontWeightMedium, 0, NSTextAlignmentCenter);
        }
    }
    NSString* timeText = sync > 0 ? [NSString stringWithFormat:@"%s at %.0f BPM = %@", syncNames[sync], [self.host tempo], msText(ms)] : msText(ms);
    drawCaption([NSString stringWithFormat:@"%s · every %@ · repeats in dB, each %.0f%% of the last", flavorNames[DelayBlock][flavor], timeText, fb*100], self.bounds);
}
@end

// ---------------------------------------------------------------- reverb: the decay
@implementation FXReverbGraph
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    uiFillPanel(self.bounds, 12, uiColor(kPanel), uiColor(kLine));
    const uint32_t flavor = [self flavor];
    const double rt60 = reverbDecay(flavor, [self value:P1]), pre = [self value:P3]*.001;
    Plot p; p.r = plotRect(self.bounds); p.logX = false; p.minX = 0; p.maxX = std::max(.5, pre+rt60*1.15); p.minY = -70; p.maxY = 0;
    uiFillPanel(p.r, 4, uiColor(kBackground, .75), nil);
    auto* grid = [NSBezierPath bezierPath];
    for (double d : {-10., -20., -30., -40., -50., -60.}) { [grid moveToPoint:NSMakePoint(p.r.origin.x, p.y(d))]; [grid lineToPoint:NSMakePoint(NSMaxX(p.r), p.y(d))]; }
    grid.lineWidth = .5; [uiColor(kLine, .55) setStroke]; [grid stroke];
    for (double d : {0., -20., -40., -60.}) uiDrawText([NSString stringWithFormat:@"%.0f", d], NSMakeRect(p.r.origin.x-38, p.y(d)-6, 32, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 0, NSTextAlignmentRight);
    const double step = p.maxX > 8 ? 2 : p.maxX > 3 ? 1 : p.maxX > 1.2 ? .5 : .25;
    for (double t = 0; t <= p.maxX; t += step) uiDrawText([NSString stringWithFormat:@"%.2g s", t], NSMakeRect(p.x(t)-24, NSMaxY(p.r)+4, 48, 12), 8.5, uiColor(kFaint), NSFontWeightMedium, 0, NSTextAlignmentCenter);
    [NSGraphicsContext saveGraphicsState];
    [NSBezierPath clipRect:p.r];
    if (pre > 0) { uiFillRect(NSMakeRect(p.x(0), p.r.origin.y, p.x(pre)-p.x(0), p.r.size.height), [uiColor(kLine) colorWithAlphaComponent:.35]); uiDrawText([NSString stringWithFormat:@"PRE-DELAY %.0f ms", pre*1000], NSMakeRect(p.x(0)+6, p.r.origin.y+22, 130, 11), 8, uiColor(kDim), NSFontWeightBold, .6, NSTextAlignmentLeft); }
    // The tail falls 60 dB over its RT60 from the end of the pre-delay: a straight line in dB.
    auto* fill = [NSBezierPath bezierPath];
    [fill moveToPoint:NSMakePoint(p.x(pre), p.y(0))]; [fill lineToPoint:NSMakePoint(p.x(pre+rt60*70./60), p.y(-70))];
    [fill lineToPoint:NSMakePoint(p.x(pre), p.y(-70))]; [fill closePath];
    [[self soft:.16] setFill]; [fill fill];
    auto* line = [NSBezierPath bezierPath];
    [line moveToPoint:NSMakePoint(p.x(0), p.y(0))]; [line lineToPoint:NSMakePoint(p.x(pre), p.y(0))]; [line lineToPoint:NSMakePoint(p.x(pre+rt60*70./60), p.y(-70))];
    line.lineWidth = 2.4; [[self tint] setStroke]; [line stroke];
    [NSGraphicsContext restoreGraphicsState];
    auto* mark = [NSBezierPath bezierPath];
    [mark moveToPoint:NSMakePoint(p.x(pre+rt60), p.y(-60)-8)]; [mark lineToPoint:NSMakePoint(p.x(pre+rt60), p.y(-60)+8)];
    mark.lineWidth = 2; [[self tint] setStroke]; [mark stroke];
    uiDrawText([NSString stringWithFormat:@"RT60 %.2f s", rt60], NSMakeRect(std::min(p.x(pre+rt60)-50, NSMaxX(p.r)-110), p.y(-60)-26, 110, 12), 9.5, [self tint], NSFontWeightBold, .6, NSTextAlignmentCenter);
    drawCaption([NSString stringWithFormat:@"%s · the tail's decay, in dB from the start of the tail · RT60 is the time to fall 60 dB", flavorNames[ReverbBlock][flavor]], self.bounds);
}
@end

// ---------------------------------------------------------------- width: the image, and the side by frequency
@implementation FXWidthGraph
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    uiFillPanel(self.bounds, 12, uiColor(kPanel), uiColor(kLine));
    const uint32_t flavor = [self flavor];
    const double amount = [self value:P1], bass = [self value:P2];
    const CGFloat side = self.bounds.size.height-58;
    NSRect image = NSMakeRect(44, 30, side, side);
    uiFillPanel(image, 4, uiColor(kBackground, .75), nil);
    const NSPoint c = NSMakePoint(NSMidX(image), NSMidY(image));
    auto* axes = [NSBezierPath bezierPath];
    [axes moveToPoint:NSMakePoint(image.origin.x, c.y)]; [axes lineToPoint:NSMakePoint(NSMaxX(image), c.y)];
    [axes moveToPoint:NSMakePoint(c.x, image.origin.y)]; [axes lineToPoint:NSMakePoint(c.x, NSMaxY(image))];
    axes.lineWidth = .5; [uiColor(kLine, .6) setStroke]; [axes stroke];
    uiDrawText(@"M", NSMakeRect(c.x+5, image.origin.y+4, 14, 12), 8.5, uiColor(kFaint), NSFontWeightBold, 0, NSTextAlignmentLeft);
    uiDrawText(@"S", NSMakeRect(NSMaxX(image)-18, c.y+4, 14, 12), 8.5, uiColor(kFaint), NSFontWeightBold, 0, NSTextAlignmentRight);
    // A schematic of where a centered source's energy sits: along the middle (mono) at width 0, widening to a circle at
    // 100% and an ellipse beyond; a Haas delay is drawn as the two ears' timing.
    [NSGraphicsContext saveGraphicsState];
    [NSBezierPath clipRect:image];
    if (flavor == 1) {
        const double ms = haasMs(amount);
        NSRect lane = NSMakeRect(image.origin.x+14, c.y-34, image.size.width-28, 22), lane2 = NSMakeRect(image.origin.x+14, c.y+12, image.size.width-28, 22);
        const CGFloat shift = static_cast<CGFloat>(ms/30.)*(lane.size.width-24);
        uiFillRect(lane, [uiColor(kLine) colorWithAlphaComponent:.4]); uiFillRect(lane2, [uiColor(kLine) colorWithAlphaComponent:.4]);
        uiFillRect(NSMakeRect(lane.origin.x+6, lane.origin.y+3, 14, 16), [self tint]);
        uiFillRect(NSMakeRect(lane2.origin.x+6+shift, lane2.origin.y+3, 14, 16), [self tint]);
        uiDrawText(@"LEFT", NSMakeRect(lane.origin.x, lane.origin.y-14, 60, 11), 8, uiColor(kDim), NSFontWeightBold, .6, NSTextAlignmentLeft);
        uiDrawText([NSString stringWithFormat:@"RIGHT +%.1f ms", ms], NSMakeRect(lane2.origin.x, lane2.origin.y+24, 140, 11), 8, uiColor(kDim), NSFontWeightBold, .6, NSTextAlignmentLeft);
    } else {
        const double w = flavor == 0 ? widthFactor(amount) : amount/100.;                     // the side's share against the mid's
        const CGFloat mid = side*.22, wide = mid*std::max<CGFloat>(static_cast<CGFloat>(w), .015);   // the mid never changes; the side is its share
        auto* shape = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x-wide, c.y-mid, 2*wide, 2*mid)];
        [[self soft:.22] setFill]; [shape fill];
        shape.lineWidth = 2.2; [[self tint] setStroke]; [shape stroke];
    }
    [NSGraphicsContext restoreGraphicsState];
    // ---- the side signal against frequency: Mono Below keeps the lows in the middle
    NSRect area = NSMakeRect(44+side+54, 30, self.bounds.size.width-(44+side+54)-18, side);
    Plot p; p.r = area; p.minY = -42; p.maxY = 6;
    drawResponseGrid(p, 12);
    std::vector<NSPoint> line;
    for (double f : logFrequencies()) {
        double db = 0;
        if (bass >= 20) { const double x = std::tan(fxblock::pi*f/48000)/std::tan(fxblock::pi*std::min(bass, 400.)/48000); db = 20*std::log10(std::max(x*x/std::sqrt(1+x*x*x*x), 1e-6)); }
        line.push_back(NSMakePoint(p.x(f), p.y(std::clamp(db, p.minY-4, p.maxY+4))));
    }
    [NSGraphicsContext saveGraphicsState];
    [NSBezierPath clipRect:p.r];
    auto* path = polyline(line);
    path.lineWidth = 2.4; [[self tint] setStroke]; [path stroke];
    [NSGraphicsContext restoreGraphicsState];
    if (bass < 20) uiDrawText(@"MONO BELOW IS OFF", NSMakeRect(p.r.origin.x+8, p.r.origin.y+22, 200, 11), 8.5, uiColor(kFaint), NSFontWeightBold, .8, NSTextAlignmentLeft);
    uiDrawText(@"IMAGE · MID UP, SIDE ACROSS", NSMakeRect(44, 9, 300, 11), 8.5, uiColor(kFaint), NSFontWeightBold, .9, NSTextAlignmentLeft);
    uiDrawText(@"SIDE LEVEL BY FREQUENCY · MONO BELOW", NSMakeRect(area.origin.x, 9, 360, 11), 8.5, uiColor(kFaint), NSFontWeightBold, .9, NSTextAlignmentLeft);
}
@end
