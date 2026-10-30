#import "MJEditorView.h"

@interface MJRenderGlyph : NSObject
@property(nonatomic,strong) NSString *text;
@property(nonatomic,strong) NSDictionary *attributes;
@property(nonatomic) NSSize size;
@end
@implementation MJRenderGlyph
@end

static const CGFloat padding = 8;
@interface MJEditorView () {
    NSFont *_font, *_boldFont;
    CGFloat _cellWidth, _cellHeight;
    NSString *_marked;
    NSRange _markedSelection;
    NSCache<NSData *,MJRenderGlyph *> *_glyphCache;
    NSMutableDictionary<NSNumber *,NSColor *> *_colorCache;
    CGFloat _scrollRemainder;
    NSInteger _pendingScroll;
    BOOL _scrollScheduled;

}
@end

@implementation MJEditorView
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        _glyphCache=[NSCache new];_glyphCache.countLimit=4096;_glyphCache.totalCostLimit=8*1024*1024;
        _colorCache=[NSMutableDictionary dictionary];
        _lineSpacing = -6;
        self.fontSize = 13;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        [self setAccessibilityElement:YES];
        [self setAccessibilityRole:NSAccessibilityTextAreaRole];
        [self setAccessibilityLabel:@"MiJoe 编辑区"];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { self.needsDisplay = YES; return YES; }
- (BOOL)resignFirstResponder { self.needsDisplay = YES; return YES; }
- (void)setFontSize:(CGFloat)size {
    [_glyphCache removeAllObjects];
    _fontSize = MAX(9, MIN(size, 32));
    _font = [NSFont fontWithName:@"Menlo-Regular" size:_fontSize] ?: [NSFont monospacedSystemFontOfSize:_fontSize weight:NSFontWeightRegular];
    _boldFont = [[NSFontManager sharedFontManager] convertFont:_font toHaveTrait:NSBoldFontMask];
    _cellWidth = ceil([@"M" sizeWithAttributes:@{NSFontAttributeName:_font}].width);
    _lineSpacing = MAX(6 - ceil(_font.ascender - _font.descender + _font.leading), MIN(_lineSpacing, 32));
    _cellHeight = MAX(6, ceil(_font.ascender - _font.descender + _font.leading) + _lineSpacing);
    [self resizeEditor]; self.needsDisplay = YES;
}
- (void)setLineSpacing:(CGFloat)spacing {
    CGFloat natural = ceil(_font.ascender - _font.descender + _font.leading);
    _lineSpacing = MAX(6 - natural, MIN(spacing, 32));
    _cellHeight = MAX(6, natural + _lineSpacing);
    [self resizeEditor]; self.needsDisplay = YES;
    if (self.hasMarkedText) [self.inputContext invalidateCharacterCoordinates];
}
- (void)setSession:(MJEditorSession *)session { _session = session; [self resizeEditor]; }
- (void)setFrameSize:(NSSize)newSize { [super setFrameSize:newSize]; [self resizeEditor]; }
- (void)resizeEditor {
    if (_session && _cellWidth > 0 && _cellHeight > 0)
        [_session resizeRows:(int)floor((self.bounds.size.height - padding * 2) / _cellHeight)
                    columns:(int)floor((self.bounds.size.width - padding * 2) / _cellWidth)];
    [_session setFontSize:self.fontSize lineSpacing:self.lineSpacing];
}
- (NSColor *)color:(VTermColor)color {
    vterm_screen_convert_color_to_rgb(self.session.screen,&color);
    NSNumber *key=@(((uint32_t)color.rgb.red<<16)|((uint32_t)color.rgb.green<<8)|color.rgb.blue);
    NSColor *cached=_colorCache[key];
    if (!cached) {
        cached=[NSColor colorWithSRGBRed:color.rgb.red/255.0 green:color.rgb.green/255.0 blue:color.rgb.blue/255.0 alpha:1];
        if (_colorCache.count>4096) [_colorCache removeAllObjects];
        _colorCache[key]=cached;
    }
    return cached;
}
- (NSRect)cursorRect {
    VTermPos cursor = self.session.cursor;
    return NSMakeRect(padding + cursor.col * _cellWidth, padding + cursor.row * _cellHeight, _cellWidth, _cellHeight);
}
- (void)invalidateTerminalRect:(VTermRect)rect {
    if (_cellWidth<=0 || _cellHeight<=0) { self.needsDisplay=YES; return; }
    NSRect pixels=NSMakeRect(padding+rect.start_col*_cellWidth,padding+rect.start_row*_cellHeight,
                             (rect.end_col-rect.start_col)*_cellWidth,(rect.end_row-rect.start_row)*_cellHeight);
    /* Negative leading and wide characters can overlap adjacent grid cells. */
    CGFloat halo=ceil(_font.ascender-_font.descender+_font.leading);
    pixels=NSIntersectionRect(NSInsetRect(pixels,-_cellWidth,-halo),self.bounds);
    if (!NSIsEmptyRect(pixels)) [self setNeedsDisplayInRect:pixels];
}
- (MJRenderGlyph *)glyphForCell:(VTermScreenCell)cell color:(NSColor *)color cursor:(BOOL)cursor {
    struct { uint32_t chars[VTERM_MAX_CHARS_PER_CELL],rgb; unsigned char bold,underline,strike; } key;
    memset(&key,0,sizeof(key)); memcpy(key.chars,cell.chars,sizeof(key.chars));
    key.rgb=cursor ? 0 : ((uint32_t)lround(color.redComponent*255)<<16)|((uint32_t)lround(color.greenComponent*255)<<8)|(uint32_t)lround(color.blueComponent*255);
    key.bold=cell.attrs.bold;key.underline=cursor ? 0 : cell.attrs.underline;key.strike=cursor ? 0 : cell.attrs.strike;
    NSData *identifier=[NSData dataWithBytes:&key length:sizeof(key)];
    MJRenderGlyph *glyph=[_glyphCache objectForKey:identifier];
    if (glyph) return glyph;
    NSUInteger count=0;while(count<VTERM_MAX_CHARS_PER_CELL && cell.chars[count])++count;
    NSString *text=[[NSString alloc]initWithBytes:cell.chars length:count*4 encoding:NSUTF32LittleEndianStringEncoding];
    if (!text) return nil;
    NSMutableDictionary *attributes=[@{NSFontAttributeName:cell.attrs.bold ? _boldFont : _font,NSForegroundColorAttributeName:color} mutableCopy];
    if (key.underline) attributes[NSUnderlineStyleAttributeName]=@(NSUnderlineStyleSingle);
    if (key.strike) attributes[NSStrikethroughStyleAttributeName]=@(NSUnderlineStyleSingle);
    glyph=[MJRenderGlyph new];glyph.text=text;glyph.attributes=attributes;
    glyph.size=[text sizeWithAttributes:attributes];
    [_glyphCache setObject:glyph forKey:identifier cost:512+text.length*8];
    return glyph;
}
- (void)drawRect:(NSRect)dirty {
    [(self.session.textBackground ?: NSColor.blackColor) setFill]; NSRectFill(dirty);
    if (!self.session) return;
    int rows,columns;vterm_get_size(self.session.terminal,&rows,&columns);
    BOOL fullPaint=NSContainsRect(dirty,self.bounds);
    int overlap=(int)ceil((_font.ascender-_font.descender+_font.leading)/_cellHeight);
    int first=MAX(0,(int)floor((NSMinY(dirty)-padding)/_cellHeight)-overlap);
    int last=MIN(rows,(int)ceil((NSMaxY(dirty)-padding)/_cellHeight)+overlap);
    int firstCol=MAX(0,(int)floor((NSMinX(dirty)-padding)/_cellWidth)-1);
    int lastCol=MIN(columns,(int)ceil((NSMaxX(dirty)-padding)/_cellWidth)+1);
    for (int pass=0;pass<2;++pass) for (int row=first;row<last;++row) {
        NSRect rowRect=NSMakeRect(0,padding+row*_cellHeight,self.bounds.size.width,_cellHeight);
        if (!fullPaint && ![self needsToDrawRect:NSInsetRect(rowRect,0,-overlap*_cellHeight)]) continue;
        for (int col=firstCol;col<lastCol;++col) {
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(self.session.screen,(VTermPos){row,col},&cell)) continue;
            if (cell.chars[0]==0xffffffff || cell.width==0) continue;
            NSRect rectangle=NSMakeRect(padding+col*_cellWidth,padding+row*_cellHeight,MAX(1,cell.width)*_cellWidth,_cellHeight);
            if (!fullPaint && ![self needsToDrawRect:NSInsetRect(rectangle,0,-overlap*_cellHeight)]) continue;
            VTermColor foreground=cell.attrs.reverse ? cell.bg : cell.fg;
            VTermColor background=cell.attrs.reverse ? cell.fg : cell.bg;
            if (!pass) { [[self color:background] setFill]; NSRectFill(rectangle); continue; }
            if (!cell.chars[0] || (cell.chars[0]==' ' && !cell.attrs.underline && !cell.attrs.strike)) continue;
            MJRenderGlyph *glyph=[self glyphForCell:cell color:[self color:foreground] cursor:NO];
            if (!glyph) continue;
            [NSGraphicsContext saveGraphicsState];
            NSRectClip(NSMakeRect(rectangle.origin.x,0,rectangle.size.width,self.bounds.size.height));
            [glyph.text drawAtPoint:NSMakePoint(rectangle.origin.x,rectangle.origin.y+(_cellHeight-glyph.size.height)/2) withAttributes:glyph.attributes];
            [NSGraphicsContext restoreGraphicsState];
        }
    }
    NSRect cursor=[self cursorRect];
    if (_marked.length) {
        NSDictionary *attributes=@{NSFontAttributeName:_font,NSForegroundColorAttributeName:self.session.textForeground,
                                  NSBackgroundColorAttributeName:self.session.textBackground,NSUnderlineStyleAttributeName:@(NSUnderlineStyleSingle)};
        NSSize size=[_marked sizeWithAttributes:attributes];
        NSRect box=NSMakeRect(cursor.origin.x,cursor.origin.y,MAX(_cellWidth,size.width+6),_cellHeight);
        [self.session.textBackground setFill];NSRectFill(box);
        [_marked drawAtPoint:NSMakePoint(box.origin.x+2,box.origin.y+(_cellHeight-size.height)/2) withAttributes:attributes];
    } else if (self.session.cursorVisible) {
        VTermScreenCell cell;BOOL have=vterm_screen_get_cell(self.session.screen,self.session.cursor,&cell);
        if (have) cursor.size.width*=MAX(1,cell.width);
        [self.session.cursorColor setFill];NSRectFill(cursor);
        if (have && cell.chars[0] && cell.chars[0]!=0xffffffff) {
            MJRenderGlyph *glyph=[self glyphForCell:cell color:NSColor.blackColor cursor:YES];
            [NSGraphicsContext saveGraphicsState];NSRectClip(NSMakeRect(cursor.origin.x,0,cursor.size.width,self.bounds.size.height));
            [glyph.text drawAtPoint:NSMakePoint(cursor.origin.x,cursor.origin.y+(_cellHeight-glyph.size.height)/2) withAttributes:glyph.attributes];
            [NSGraphicsContext restoreGraphicsState];
        }
    }
}
- (void)flushScrollBeforeInput {
    _scrollRemainder=0;
    while (_pendingScroll) {
        NSInteger amount=MAX(-20,MIN((NSInteger)20,_pendingScroll));_pendingScroll-=amount;
        [self.session request:MIJOE_GUI_SCROLL path:[NSString stringWithFormat:@"%ld",(long)amount] completion:nil];
    }
}
- (void)keyDown:(NSEvent *)event {
    [self flushScrollBeforeInput];
    if (!(event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagShift))) {
        BOOL adjusted = YES;
        switch (event.keyCode) {
            case 122: self.fontSize += 1; break; /* F1 */
            case 120: self.fontSize -= 1; break; /* F2 */
            case 99: self.lineSpacing += 1; break; /* F3, one pixel */
            case 118: self.lineSpacing -= 1; break; /* F4 */
            default: adjusted = NO; break;
        }
        if (adjusted) {
            [NSUserDefaults.standardUserDefaults setDouble:self.fontSize forKey:@"EditorFontSize"];
            [NSUserDefaults.standardUserDefaults setDouble:self.lineSpacing forKey:@"EditorLineSpacing"];
            if (self.hasMarkedText) [self.inputContext invalidateCharacterCoordinates];
            return;
        }
    }
    if (event.modifierFlags & NSEventModifierFlagCommand) { NSBeep(); return; }
    if ([self hasMarkedText]) { [self interpretKeyEvents:@[event]]; return; }
    if (event.modifierFlags & NSEventModifierFlagControl) {
        NSString *text = event.charactersIgnoringModifiers;
        if (text.length) {
            unichar c = [text characterAtIndex:0];
            if ((c=='u' || c=='U') && (event.modifierFlags & NSEventModifierFlagShift) && !(event.modifierFlags & NSEventModifierFlagOption)) {
                [self.session sendKey:VTERM_KEY_PAGEDOWN modifiers:VTERM_MOD_NONE]; return;
            }
            if (c < 128) { uint8_t key = c == '?' ? 127 : c & 31; [self.session sendData:[NSData dataWithBytes:&key length:1]]; return; }
        }
    }
    VTermKey key = VTERM_KEY_NONE;
    switch (event.keyCode) {
        case 96:key=VTERM_KEY_FUNCTION(5);break; case 97:key=VTERM_KEY_FUNCTION(6);break;
        case 123:key=VTERM_KEY_LEFT;break; case 124:key=VTERM_KEY_RIGHT;break;
        case 125:key=VTERM_KEY_DOWN;break; case 126:key=VTERM_KEY_UP;break;
        case 115:key=VTERM_KEY_HOME;break; case 119:key=VTERM_KEY_END;break;
        case 116:key=VTERM_KEY_PAGEUP;break; case 121:key=VTERM_KEY_PAGEDOWN;break;
        case 117:key=VTERM_KEY_DEL;break; case 51:key=VTERM_KEY_BACKSPACE;break;
        case 53:key=VTERM_KEY_ESCAPE;break; case 48:key=VTERM_KEY_TAB;break;
        case 36:case 76:key=VTERM_KEY_ENTER;break;
    }
    if (key != VTERM_KEY_NONE) {
        [self.session sendKey:key modifiers:((event.modifierFlags & NSEventModifierFlagShift) && key==VTERM_KEY_TAB) ? VTERM_MOD_SHIFT : VTERM_MOD_NONE];
    } else [self interpretKeyEvents:@[event]];
}
- (void)insertText:(id)value replacementRange:(NSRange)range {
    [self flushScrollBeforeInput];
    NSString *text = [value isKindOfClass:NSAttributedString.class] ? [value string] : value;
    BOOL hadMarked=_marked.length>0; _marked=nil;
    [self.session sendText:text]; if (hadMarked) [self invalidateTerminalRect:(VTermRect){self.session.cursor.row,self.session.cursor.row+1,0,1000}];
}
/* NSResponder and accessibility text services may use the older selector. */
- (void)insertText:(id)value {
    [self insertText:value replacementRange:NSMakeRange(NSNotFound,0)];
}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)replacement {
    _marked = [[value isKindOfClass:NSAttributedString.class] ? [value string] : value copy];
    _markedSelection = selected; [self invalidateTerminalRect:(VTermRect){self.session.cursor.row,self.session.cursor.row+1,0,1000}];
}
- (void)unmarkText { _marked=nil; [self invalidateTerminalRect:(VTermRect){self.session.cursor.row,self.session.cursor.row+1,0,1000}]; }
- (BOOL)hasMarkedText { return _marked.length != 0; }
- (NSRange)markedRange { return [self hasMarkedText] ? NSMakeRange(0,_marked.length) : NSMakeRange(NSNotFound,0); }
- (NSRange)selectedRange { return [self hasMarkedText] ? _markedSelection : NSMakeRange(0,0); }
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[NSUnderlineStyleAttributeName,NSForegroundColorAttributeName,NSBackgroundColorAttributeName]; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (!_marked.length || range.location >= _marked.length) { if(actual)*actual=NSMakeRange(NSNotFound,0); return nil; }
    NSRange intersection = NSIntersectionRange(range,NSMakeRange(0,_marked.length));
    if(actual)*actual=intersection;
    return [[NSAttributedString alloc] initWithString:[_marked substringWithRange:intersection]];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return NSNotFound; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (actual) *actual = [self markedRange];
    return [self.window convertRectToScreen:[self convertRect:[self cursorRect] toView:nil]];
}
- (void)doCommandBySelector:(SEL)selector {
    NSDictionary *keys = @{@"insertNewline:":@(VTERM_KEY_ENTER),@"insertTab:":@(VTERM_KEY_TAB),
                          @"deleteBackward:":@(VTERM_KEY_BACKSPACE),@"deleteForward:":@(VTERM_KEY_DEL),
                          @"moveLeft:":@(VTERM_KEY_LEFT),@"moveRight:":@(VTERM_KEY_RIGHT),
                          @"moveUp:":@(VTERM_KEY_UP),@"moveDown:":@(VTERM_KEY_DOWN),
                          @"cancelOperation:":@(VTERM_KEY_ESCAPE)};
    NSNumber *key = keys[NSStringFromSelector(selector)];
    if (key) [self.session sendKey:key.intValue modifiers:VTERM_MOD_NONE];
}
- (void)paste:(id)sender {
    NSString *text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
    if (text) { [self.inputContext discardMarkedText]; [self unmarkText]; [self.session pasteText:text]; }
}
- (void)performEditorAction:(int)action {
    __weak typeof(self) weakSelf = self;
    [self.session request:action path:nil completion:^(NSDictionary *result) {
        MJEditorView *self = weakSelf;
        if (!self) return;
        if (![result[@"ok"] boolValue]) {
            NSAlert *alert = [NSAlert new]; alert.messageText=@"操作未完成"; alert.informativeText=result[@"error"] ?: @"";
            [alert beginSheetModalForWindow:self.window completionHandler:nil];
        } else if (result[@"text"]) {
            [NSPasteboard.generalPasteboard clearContents];
            [NSPasteboard.generalPasteboard setString:result[@"text"] forType:NSPasteboardTypeString];
        }
    }];
}
- (void)copy:(id)sender { [self performEditorAction:MIJOE_GUI_COPY]; }
- (void)cut:(id)sender { [self performEditorAction:MIJOE_GUI_CUT]; }
- (void)undo:(id)sender { [self performEditorAction:MIJOE_GUI_UNDO]; }
- (void)redo:(id)sender { [self performEditorAction:MIJOE_GUI_REDO]; }
- (void)selectAll:(id)sender { [self performEditorAction:MIJOE_GUI_SELECT_ALL]; }
- (BOOL)validateMenuItem:(NSMenuItem *)item { return self.session.running; }
- (void)mouseAt:(NSEvent *)event button:(int)button pressed:(BOOL)pressed {
    [self flushScrollBeforeInput];
    if (self.window.firstResponder!=self) [self.window makeFirstResponder:self];
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    int row = MAX(0,(int)floor((point.y-padding)/_cellHeight));
    int column = MAX(0,(int)floor((point.x-padding)/_cellWidth));
    int kind = button ? (pressed ? 1 : 3) : 2;
    [self.session request:MIJOE_GUI_MOUSE path:[NSString stringWithFormat:@"%d;%d;%d",kind,column,row] completion:nil];
}
- (void)mouseDown:(NSEvent *)event { [self mouseAt:event button:1 pressed:YES]; }
- (void)mouseDragged:(NSEvent *)event { [self mouseAt:event button:0 pressed:YES]; }
- (void)mouseUp:(NSEvent *)event { [self mouseAt:event button:1 pressed:NO]; }
- (void)flushScrollInput {
    _scrollScheduled=NO;
    NSInteger amount=MAX(-20,MIN((NSInteger)20,_pendingScroll));
    _pendingScroll-=amount;
    if (amount) [self.session request:MIJOE_GUI_SCROLL path:[NSString stringWithFormat:@"%ld",(long)amount] completion:nil];
    if (_pendingScroll) [self scheduleScrollInput];
}
- (void)scheduleScrollInput {
    if (_scrollScheduled) return;
    _scrollScheduled=YES;
    __weak typeof(self) weakSelf=self;
    NSInteger rate=60;
    if (@available(macOS 12.0,*)) { NSInteger available=(self.window.screen ?: NSScreen.mainScreen).maximumFramesPerSecond; if (available>0) rate=MAX(30,MIN(120,available)); }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/rate),dispatch_get_main_queue(),^{ [weakSelf flushScrollInput]; });
}
- (void)scrollWheel:(NSEvent *)event {
    BOOL finished=(event.phase & (NSEventPhaseEnded|NSEventPhaseCancelled)) || (event.momentumPhase & (NSEventPhaseEnded|NSEventPhaseCancelled));
    if (event.phase & NSEventPhaseBegan) _scrollRemainder=0;
    CGFloat delta=event.scrollingDeltaY;
    if (fabs(delta)>=0.001) {
        /* Trackpad deltas are pixels; mouse wheel deltas already represent lines. */
        _scrollRemainder+=event.hasPreciseScrollingDeltas ? delta/_cellHeight : delta;
        NSInteger lines=(NSInteger)_scrollRemainder;
        if (lines) { _scrollRemainder-=lines;_pendingScroll+=lines;[self scheduleScrollInput]; }
    }
    if (finished) _scrollRemainder=0;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray *urls = [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if (!urls.count) return NO;
    [NSApp.delegate application:NSApp openFiles:[urls valueForKey:@"path"]];
    return YES;
}
- (NSString *)accessibilityValue { return [self.session screenText]; }
@end
