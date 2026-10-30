/* Offscreen benchmark of our own renderer; never drives an application window. */
#import "MJEditorView.h"
#include <time.h>
@interface MJEditorView (BenchmarkDamage)
- (void)invalidateTerminalRect:(VTermRect)rect;
@end
@interface BenchView : MJEditorView
@property NSRect pendingRect;
@property NSUInteger invalidations;
@end
@implementation BenchView
- (void)setNeedsDisplay:(BOOL)value { if(value) { self.pendingRect=self.bounds; self.invalidations++; } [super setNeedsDisplay:value]; }
- (void)setNeedsDisplayInRect:(NSRect)rect { self.pendingRect=NSIsEmptyRect(self.pendingRect) ? rect : NSUnionRect(self.pendingRect,rect); self.invalidations++; [super setNeedsDisplayInRect:rect]; }
@end
@interface BenchDelegate : NSObject <MJEditorSessionDelegate>
@property BenchView *view;
@property NSUInteger content,stateCount;
@end
@implementation BenchDelegate
- (void)editorContentChanged:(MJEditorSession *)s { self.content++; self.view.needsDisplay=YES; }
- (void)editor:(MJEditorSession *)s damagedRect:(VTermRect)rect { self.content++; [self.view invalidateTerminalRect:rect]; }
- (void)editor:(MJEditorSession *)s stateChanged:(NSDictionary *)state { self.stateCount++; }
- (void)editor:(MJEditorSession *)s exited:(int)code {}
@end
/* Frozen pre-optimization drawing loop, for same-screen A/B measurement only. */
@interface LegacyBenchView : MJEditorView { NSFont *_legacyFont,*_legacyBold;CGFloat _legacyWidth; }
@end
@implementation LegacyBenchView
- (NSColor *)legacyColor:(VTermColor)c {
    vterm_screen_convert_color_to_rgb(self.session.screen,&c);
    return [NSColor colorWithSRGBRed:c.rgb.red/255.0 green:c.rgb.green/255.0 blue:c.rgb.blue/255.0 alpha:1];
}
- (void)drawRect:(NSRect)dirty {
    [self.session.textBackground setFill];NSRectFill(dirty);
    if (!_legacyFont || _legacyFont.pointSize!=self.fontSize) {
        _legacyFont=[NSFont fontWithName:@"Menlo-Regular" size:self.fontSize];
        _legacyBold=[[NSFontManager sharedFontManager]convertFont:_legacyFont toHaveTrait:NSBoldFontMask];
        _legacyWidth=ceil([@"M" sizeWithAttributes:@{NSFontAttributeName:_legacyFont}].width);
    }
    NSFont *font=_legacyFont,*bold=_legacyBold;CGFloat cw=_legacyWidth;
    CGFloat ch=MAX(6,ceil(font.ascender-font.descender+font.leading)+self.lineSpacing);
    int rows,cols;vterm_get_size(self.session.terminal,&rows,&cols);
    int overlap=(int)ceil((font.ascender-font.descender+font.leading)/ch);
    int first=MAX(0,(int)floor((NSMinY(dirty)-8)/ch)-overlap);
    int last=MIN(rows,(int)ceil((NSMaxY(dirty)-8)/ch)+overlap);
    for(int pass=0;pass<2;pass++)for(int row=first;row<last;row++)for(int col=0;col<cols;col++) {
        VTermScreenCell cell;if(!vterm_screen_get_cell(self.session.screen,(VTermPos){row,col},&cell))continue;
        if(cell.chars[0]==0xffffffff || !cell.width)continue;
        NSColor *fg=[self legacyColor:cell.fg],*bg=[self legacyColor:cell.bg];
        if(cell.attrs.reverse){NSColor *swap=fg;fg=bg;bg=swap;}
        NSRect rectangle=NSMakeRect(8+col*cw,8+row*ch,MAX(1,cell.width)*cw,ch);
        if(!pass){[bg setFill];NSRectFill(rectangle);continue;}
        if(!cell.chars[0])continue;
        NSUInteger count=0;while(count<VTERM_MAX_CHARS_PER_CELL && cell.chars[count])count++;
        NSString *text=[[NSString alloc]initWithBytes:cell.chars length:count*4 encoding:NSUTF32LittleEndianStringEncoding];if(!text)continue;
        NSMutableDictionary *attrs=[@{NSFontAttributeName:cell.attrs.bold ? bold : font,NSForegroundColorAttributeName:fg}mutableCopy];
        if(cell.attrs.underline)attrs[NSUnderlineStyleAttributeName]=@(NSUnderlineStyleSingle);
        if(cell.attrs.strike)attrs[NSStrikethroughStyleAttributeName]=@(NSUnderlineStyleSingle);
        [NSGraphicsContext saveGraphicsState];NSRectClip(NSMakeRect(rectangle.origin.x,0,rectangle.size.width,self.bounds.size.height));
        [text drawAtPoint:NSMakePoint(rectangle.origin.x,rectangle.origin.y+(ch-[text sizeWithAttributes:attrs].height)/2)withAttributes:attrs];
        [NSGraphicsContext restoreGraphicsState];
    }
    if(self.session.cursorVisible) {
        VTermPos p=self.session.cursor;VTermScreenCell cell;
        BOOL have=vterm_screen_get_cell(self.session.screen,p,&cell);
        NSRect rect=NSMakeRect(8+p.col*cw,8+p.row*ch,cw*(have ? MAX(1,cell.width) : 1),ch);
        [self.session.cursorColor setFill];NSRectFill(rect);
    }
}
@end

static BOOL waitFor(BOOL (^predicate)(void)) {
 NSDate *limit=[NSDate dateWithTimeIntervalSinceNow:8];
 while(!predicate() && limit.timeIntervalSinceNow>0) [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.002]];
 return predicate();
}
static NSDictionary *request(MJEditorSession *s,int op,NSString *path) {
 __block NSDictionary *result=nil;[s request:op path:path completion:^(NSDictionary *r){result=r;}];
 if(!waitFor(^BOOL{return result!=nil;})) exit(2);return result;
}
static double paint(MJEditorView *view, NSRect rect) {
 double start=CFAbsoluteTimeGetCurrent();
 @autoreleasepool {
  [view setNeedsDisplayInRect:rect];
  NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:rect];
  [view cacheDisplayInRect:rect toBitmapImageRep:image];
 }
 return (CFAbsoluteTimeGetCurrent()-start)*1000;
}
int main(int argc,const char **argv) {
 @autoreleasepool {
  if(argc!=4)return 2;[NSApplication sharedApplication];
  NSString *directory=[NSString stringWithUTF8String:argv[2]],*file=[directory stringByAppendingPathComponent:@"bench.txt"];
  NSMutableString *contents=[NSMutableString string];
  for(int i=0;i<20000;i++)[contents appendFormat:@"%06d WARN sample ASCII text with columns and 中文 glyphs for scrolling.\n",i];
  [contents writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
  MJEditorSession *session=[[MJEditorSession alloc]initWithExecutable:[NSString stringWithUTF8String:argv[1]] file:file error:NULL];
  BenchView *view=[[BenchView alloc]initWithFrame:NSMakeRect(0,0,1040,740)];view.fontSize=14;view.lineSpacing=-5;view.session=session;
  BenchDelegate *delegate=[BenchDelegate new];delegate.view=view;session.delegate=delegate;
  request(session,MIJOE_GUI_STATUS,nil);
  int gridRows,gridCols;vterm_get_size(session.terminal,&gridRows,&gridCols);
  NSString *lastLine=[NSString stringWithFormat:@"%06d",gridRows-5];
  if(!waitFor(^BOOL{return [session.screenText containsString:lastLine];}))exit(3);
  NSDate *settled=[NSDate dateWithTimeIntervalSinceNow:0.15];while(settled.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:settled];
  paint(view,view.bounds);
  LegacyBenchView *legacy=[[LegacyBenchView alloc]initWithFrame:view.bounds];legacy.fontSize=14;legacy.lineSpacing=-5;legacy.session=session;
  request(session,MIJOE_GUI_STATUS,nil);paint(legacy,legacy.bounds);
  double full=0,legacyFull=0;for(int i=0;i<20;i++) { full+=paint(view,view.bounds);legacyFull+=paint(legacy,legacy.bounds); }
  NSUInteger callbacks=delegate.content;double cursor=0,area=0;
  for(int i=0;i<24;i++) {
   view.pendingRect=NSZeroRect;[session sendText:i%2 ? @"h" : @"l"];request(session,MIJOE_GUI_STATUS,nil);
   NSDate *until=[NSDate dateWithTimeIntervalSinceNow:0.003];while(until.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:until];
   NSRect dirty=NSIsEmptyRect(view.pendingRect) ? view.bounds : NSIntersectionRect(view.pendingRect,view.bounds);
   area+=dirty.size.width*dirty.size.height/(view.bounds.size.width*view.bounds.size.height);
   cursor+=paint(view,dirty);
  }
  NSDictionary *report=@{@"legacyReplicaFullDrawMeanMs":@(legacyFull/20),@"fullDrawMeanMs":@(full/20),@"cursorDrawMeanMs":@(cursor/24),@"cursorDirtyAreaFraction":@(area/24),@"cursorCallbacks":@(delegate.content-callbacks),@"stateResponses":@(delegate.stateCount),@"note":@"Offscreen CPU draw cost, not display FPS or OS input latency"};
  NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];[json writeToFile:[NSString stringWithUTF8String:argv[3]] atomically:YES];puts([[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
  request(session,MIJOE_GUI_DISCARD,nil);waitFor(^BOOL{return !session.running;});
 }
 return 0;
}
