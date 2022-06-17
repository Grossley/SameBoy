#import <Cocoa/Cocoa.h>
#include <Core/gb.h>
#include <Misc/wide_gb.h>
#import <JoyKit/JoyKit.h>
#import "GBOSDView.h"
@class Document;

typedef enum {
    GB_FRAME_BLENDING_MODE_DISABLED,
    GB_FRAME_BLENDING_MODE_SIMPLE,
    GB_FRAME_BLENDING_MODE_ACCURATE,
    GB_FRAME_BLENDING_MODE_ACCURATE_EVEN = GB_FRAME_BLENDING_MODE_ACCURATE,
    GB_FRAME_BLENDING_MODE_ACCURATE_ODD,
} GB_frame_blending_mode_t;

@interface GBView : NSView<JOYListener>
- (void) flip;
- (uint32_t *) pixels;    // whole screen buffer
- (uint32_t *) bg_pixels; // BG-only buffer
@property (nonatomic, weak) IBOutlet Document *document;
@property (nonatomic) GB_gameboy_t *gb;
@property (nonatomic) wide_gb *wgb;
@property (nonatomic) GB_frame_blending_mode_t frameBlendingMode;
@property (nonatomic) bool widescreenEnabled;
@property (nonatomic, getter=isMouseHidingEnabled) bool mouseHidingEnabled;
@property (nonatomic) bool isRewinding;
@property (nonatomic, strong) NSView *internalView;
@property (weak) GBOSDView *osdView;
- (void) createInternalView;

// Output composited buffers
- (CGContextRef)currentBuffer;
- (CGContextRef)previousBuffer;

// The rectangle in which the actual emulated screen is displayed (in view cordinates)
- (NSRect) viewport;

- (void)screenSizeChanged;
- (void)setRumble: (double)amp;
- (NSImage *)renderToImage;
@end
