/* Offscreen unit/integration test of the app's own input client and session.
 * Does not synthesize OS events or operate any application window. */
#import "MJEditorView.h"

/* AppKit may discard needsDisplay for a view without a visible window.
 * Record invalidation requests while exercising the offscreen view. */
@interface MJTestEditorView : MJEditorView
@property(nonatomic) BOOL refreshRequested;
@end
@implementation MJTestEditorView
- (void)setNeedsDisplay:(BOOL)value { if (value) self.refreshRequested=YES; [super setNeedsDisplay:value]; }
@end

static NSMutableArray *results;
static void check(BOOL condition, NSString *name) {
    if (!condition) { fprintf(stderr,"FAIL: %s\n",name.UTF8String); exit(1); }
    [results addObject:name]; printf("PASS %s\n",name.UTF8String); fflush(stdout);
}
static BOOL waitFor(BOOL (^predicate)(void)) {
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:8];
    while (!predicate() && deadline.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    return predicate();
}
static NSDictionary *request(MJEditorSession *session, int action, NSString *path) {
    __block NSDictionary *response=nil;
    [session request:action path:path completion:^(NSDictionary *value) { response=value; }];
    if (!waitFor(^BOOL { return response!=nil; })) { fprintf(stderr,"FAIL missing response\n"); exit(1); }
    return response;
}
static int coloredCount(MJEditorSession *session, NSString *text, int foreground, int background) {
    int count=0;
    int rows,columns; vterm_get_size(session.terminal,&rows,&columns);
    for (int row=0;row<rows;++row) for (int col=0;col+text.length<=columns;++col) {
        BOOL matches=YES;
        for (NSUInteger n=0;n<text.length;++n) {
            VTermScreenCell cell;
            vterm_screen_get_cell(session.screen,(VTermPos){row,col+(int)n},&cell);
            vterm_screen_convert_color_to_rgb(session.screen,&cell.fg);
            vterm_screen_convert_color_to_rgb(session.screen,&cell.bg);
            if (cell.attrs.reverse) { VTermColor swap=cell.fg; cell.fg=cell.bg; cell.bg=swap; }
            int fg=(cell.fg.rgb.red<<16)|(cell.fg.rgb.green<<8)|cell.fg.rgb.blue;
            int bg=(cell.bg.rgb.red<<16)|(cell.bg.rgb.green<<8)|cell.bg.rgb.blue;
            if (cell.chars[0]!=[text characterAtIndex:n] || fg!=foreground || bg!=background) { matches=NO; break; }
        }
        if (matches) ++count;
    }
    return count;
}
static BOOL coloredText(MJEditorSession *session, NSString *text, int foreground, int background) {
    return coloredCount(session,text,foreground,background)>0;
}

/* Owned test payloads, not synthesized operating-system events. */
@interface MJScrollPayload : NSObject
@property CGFloat delta;
@property BOOL precise;
@property NSEventPhase phase, momentumPhase;
@end
@implementation MJScrollPayload
- (CGFloat)scrollingDeltaY { return self.delta; }
- (BOOL)hasPreciseScrollingDeltas { return self.precise; }
@end
@interface MJScrollProbe : NSObject
@property(nonatomic,strong) NSMutableArray<NSString *> *events;
@end
@implementation MJScrollProbe
- (instancetype)init { if((self=[super init]))_events=[NSMutableArray array];return self; }
- (void)resizeRows:(int)rows columns:(int)cols {}
- (void)setFontSize:(CGFloat)size lineSpacing:(CGFloat)spacing {}
- (void)request:(int)op path:(NSString *)path completion:(void (^)(NSDictionary *))completion { [self.events addObject:path ?: @""];if(completion)completion(@{@"ok":@YES}); }
- (void)sendKey:(VTermKey)key modifiers:(VTermModifier)mods { [self.events addObject:@"key"]; }
@end
@interface MJDamageProbe : NSObject <MJEditorSessionDelegate>
@property(nonatomic,strong) NSMutableArray<NSValue *> *rects;
@property NSUInteger full, acknowledgments;
@end
@implementation MJDamageProbe
- (instancetype)init { if((self=[super init]))_rects=[NSMutableArray array];return self; }
- (void)editorContentChanged:(MJEditorSession *)s { self.full++; }
- (void)editor:(MJEditorSession *)s damagedRect:(VTermRect)rect { [self.rects addObject:[NSValue valueWithBytes:&rect objCType:@encode(VTermRect)]]; }
- (void)editor:(MJEditorSession *)s stateChanged:(NSDictionary *)state { if([state[@"id"] unsignedIntValue])self.acknowledgments++; }
- (void)editor:(MJEditorSession *)s exited:(int)status {}
@end
@interface MJEditorSession (FrameProtocolTests)
- (void)consumeOutput:(const char *)bytes length:(NSUInteger)length;
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc!=4) return 2;
        [NSApplication sharedApplication];
        results=[NSMutableArray array];
        NSString *directory=[NSString stringWithUTF8String:argv[2]];
        NSString *file=[directory stringByAppendingPathComponent:@"view-input.txt"];
        [@"base\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        NSError *error;
        MJEditorSession *session=[[MJEditorSession alloc] initWithExecutable:[NSString stringWithUTF8String:argv[1]] file:file error:&error];
        check(session!=nil,@"native session starts its own editor");
        check([request(session,MIJOE_GUI_STATUS,nil)[@"ok"] boolValue],@"native session receives editor status");
        MJTestEditorView *view=[[MJTestEditorView alloc] initWithFrame:NSMakeRect(0,0,720,400)]; view.session=session;
        check(view.fontSize==13 && view.lineSpacing==-6,@"new view defaults to 13 point font and minus six pixel leading");
        int initialRows,initialCols,adjustedRows,adjustedCols;
        vterm_get_size(session.terminal,&initialRows,&initialCols);
        NSEvent *(^functionKey)(unsigned short,unichar)=^NSEvent *(unsigned short code,unichar character) {
            NSString *text=[NSString stringWithFormat:@"%C",character];
            return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagFunction timestamp:0 windowNumber:0 context:nil characters:text charactersIgnoringModifiers:text isARepeat:NO keyCode:code];
        };
        view.refreshRequested=NO; [view keyDown:functionKey(122,NSF1FunctionKey)];
        vterm_get_size(session.terminal,&adjustedRows,&adjustedCols);
        check(view.fontSize==14 && view.refreshRequested && adjustedCols<initialCols,@"F1 enlarges font and refreshes terminal geometry");
        check(waitFor(^BOOL { return [session.screenText containsString:@"FS 14 H -6"]; }),@"status reflects F1 enlarges font and refreshes terminal geometry");
        view.refreshRequested=NO; [view keyDown:functionKey(120,NSF2FunctionKey)];
        vterm_get_size(session.terminal,&adjustedRows,&adjustedCols);
        check(view.fontSize==13 && view.refreshRequested && adjustedCols==initialCols && adjustedRows==initialRows,@"F2 restores font and refreshes terminal geometry");
        check(waitFor(^BOOL { return [session.screenText containsString:@"FS 13 H -6"]; }),@"status reflects F2 restores font and refreshes terminal geometry");
        view.refreshRequested=NO; [view keyDown:functionKey(99,NSF3FunctionKey)];
        vterm_get_size(session.terminal,&adjustedRows,&adjustedCols);
        check(view.lineSpacing==-5 && view.refreshRequested && adjustedRows<initialRows,@"F3 increases leading and refreshes terminal rows");
        check(waitFor(^BOOL { return [session.screenText containsString:@"FS 13 H -5"]; }),@"status reflects F3 increases leading and refreshes terminal rows");
        view.refreshRequested=NO; [view keyDown:functionKey(118,NSF4FunctionKey)];
        vterm_get_size(session.terminal,&adjustedRows,&adjustedCols);
        check(view.lineSpacing==-6 && view.refreshRequested && adjustedRows==initialRows,@"F4 restores leading and refreshes terminal rows");
        check(waitFor(^BOOL { return [session.screenText containsString:@"FS 13 H -6"]; }),@"status reflects F4 restores leading and refreshes terminal rows");
        check(![request(session,MIJOE_GUI_STATUS,nil)[@"changed"] boolValue],@"function keys do not insert bytes or modify the document");
        [view insertText:@"i" replacementRange:NSMakeRange(NSNotFound,0)];
        check([request(session,MIJOE_GUI_STATUS,nil)[@"mode"] isEqual:@"I"],@"input client forwards Insert mode key");
        [view setMarkedText:@"zhongwen" selectedRange:NSMakeRange(8,0) replacementRange:NSMakeRange(NSNotFound,0)];
        check(view.hasMarkedText && view.markedRange.length==8 && view.selectedRange.location==8,@"input method marked text and selection are retained");
        check(![request(session,MIJOE_GUI_STATUS,nil)[@"changed"] boolValue],@"composition does not modify the editor before commit");
        [view keyDown:functionKey(99,NSF3FunctionKey)]; [view keyDown:functionKey(118,NSF4FunctionKey)];
        check(view.hasMarkedText && view.markedRange.length==8,@"leading shortcuts preserve active input method composition");
        NSString *committed=@"中文🙂e\u0301";
        [view insertText:committed replacementRange:NSMakeRange(0,8)];
        NSDictionary *saved=request(session,MIJOE_GUI_SAVE,nil);
        NSString *expected=[committed stringByAppendingString:@"base\n"];
        check([saved[@"ok"] boolValue] && !view.hasMarkedText && [[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:expected],@"Chinese emoji and combining input commit once and save as UTF-8");
        [view setMarkedText:@"未提交" selectedRange:NSMakeRange(3,0) replacementRange:NSMakeRange(NSNotFound,0)];
        [view unmarkText];
        request(session,MIJOE_GUI_SAVE,nil);
        check([[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:expected],@"discarding composition leaves saved text intact");
        [session sendText:@"\x1b"];
        request(session,MIJOE_GUI_STATUS,nil);
        int oldRows,oldColumns,newRows,newColumns;
        vterm_get_size(session.terminal,&oldRows,&oldColumns);
        [view setFrameSize:NSMakeSize(950,650)];
        request(session,MIJOE_GUI_STATUS,nil);
        vterm_get_size(session.terminal,&newRows,&newColumns);
        check(newRows>oldRows && newColumns>oldColumns,@"view resize updates terminal dimensions");
        check([session.screenText containsString:@"中文"],@"native screen model retains Unicode display data after resize");
        check([request(session,MIJOE_GUI_QUIT,nil)[@"ok"] boolValue] && waitFor(^BOOL { return !session.running; }),@"clean session exits and reaps its child process");
        file=[directory stringByAppendingPathComponent:@"synhill.c"];
        [@"int answer = 42; // synhill comment\nchar *message = \"hello synhill\";\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        session=[[MJEditorSession alloc] initWithExecutable:[NSString stringWithUTF8String:argv[1]] file:file error:&error];
        NSDictionary *theme=request(session,MIJOE_GUI_STATUS,nil);
        check([theme[@"scheme"] isEqual:@"teasp"] && [theme[@"textBackground"] intValue]==0x222222 && [theme[@"textForeground"] intValue]==0xe8e8d3 && [theme[@"cursorBackground"] intValue]==0x00cc66,@"native editor defaults to teasp with exact background and caret palette");
        check(waitFor(^BOOL { return coloredText(session,@"synhill comment",0x888888,0x222222); }),@"native comment RGB matches teaspoon Comment");
        check(coloredText(session,@"int",0xffb964,0x222222),@"native keyword RGB matches teaspoon Type");
        check(coloredText(session,@"answer",0xefd14e,0x222222),@"native identifier RGB matches teaspoon Identifier");
        check(coloredText(session,@"42",0xff6666,0x222222),@"native numeric RGB matches teaspoon Constant");
        check(waitFor(^BOOL { return coloredText(session,@"hello synhill",0xfaff8a,0x222222); }),@"native string RGB matches teaspoon String");
        [session sendText:@":colors gruvbox\r"];
        check(waitFor(^BOOL { return [session.state[@"scheme"] isEqual:@"gruvbox"] && [session.state[@"textBackground"] intValue]==0x282828; }),@"switching theme also updates native canvas palette");
        [session sendText:@":colors synhill\r"];
        check(waitFor(^BOOL { return [session.state[@"scheme"] isEqual:@"synhill"] && coloredText(session,@"synhill comment",0xd75f5f,0); }),@"switching back to synhill restores exact RGB");
        NSString *logfile=[directory stringByAppendingPathComponent:@"levels.log"];
        [@"WARN ERROR WARNING XERROR ERROR_code warn error **\n" writeToFile:logfile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[logfile stringByAppendingString:@"\r:colors teasp\r"]]];
        check(waitFor(^BOOL { return coloredText(session,@"WARN",0xff0066,0x222222); }),@"whole-word WARN uses teaspoon Todo pink");
        check(waitFor(^BOOL { return coloredText(session,@"ERROR",0xe8e8d3,0xff0066); }),@"whole-word ERROR uses teaspoon ErrorMsg background");
        check(coloredText(session,@"WARNING XERROR ERROR_code warn error",0xe8e8d3,0x222222),@"longer identifiers and lowercase words retain normal text colors");
        check(coloredText(session,@"**",0xffb454,0x222222),@"log asterisks use highlighted orange");
        check(waitFor(^BOOL { return coloredText(session,@"R",0x222222,0xe8e8d3); }),@"status bar uses high contrast dark text on a light background");
        [session sendText:@"iX\x1b"];
        check(waitFor(^BOOL { return coloredText(session,@"*",0x000000,0x00cc66); }),@"modified status asterisk uses the solid green cursor highlight");
        [session sendText:@"u"];
        check(waitFor(^BOOL { return ![session.state[@"changed"] boolValue]; }),@"undo clears the modified status marker");
        NSString *searchFile=[directory stringByAppendingPathComponent:@"search.txt"];
        [@"alpha foo middle foo end\nfoo 中文 foo\n" writeToFile:searchFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[searchFile stringByAppendingString:@"\r/foo\r"]]];
        check(waitFor(^BOOL { return coloredText(session,@"alpha ",0xe8e8d3,0x222222) && coloredText(session,@"foo",0x222222,0xe8e8e8); }),@"slash search highlights matches with pale white background");
        check(waitFor(^BOOL { return coloredCount(session,@"foo",0x222222,0xe8e8e8)==4; }),@"all four visible search matches are highlighted");
        check(waitFor(^BOOL { return session.cursorVisible; }),@"cursor remains visible after slash search");
        view.session=session;
        NSBitmapImageRep *preview=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:preview];
        [[preview representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[directory stringByAppendingPathComponent:@"search-cursor.png"] atomically:YES];
        int greenPixels=0;
        for (int y=0;y<preview.pixelsHigh;++y) for (int x=0;x<preview.pixelsWide;++x) {
            NSColor *pixel=[[preview colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
            if (pixel.redComponent<0.01 && fabs(pixel.greenComponent-0.8)<0.01 && fabs(pixel.blueComponent-0.4)<0.01) ++greenPixels;
        }
        check(greenPixels>20,@"native rendering paints a solid green cursor with readable character");

        [session sendText:@"nN"];
        check(waitFor(^BOOL { return coloredText(session,@"foo",0x222222,0xe8e8e8); }),@"n and N retain search highlighting");
        [session sendText:@":noh\r"];
        check(waitFor(^BOOL { return coloredText(session,@"alpha foo middle foo end",0xe8e8d3,0x222222); }),@"noh clears match highlighting");
        [session sendText:@"n"];
        check(waitFor(^BOOL { return coloredText(session,@"foo",0x222222,0xe8e8e8); }),@"repeat search restores match highlighting after noh");
        [session sendText:@"/中文\r"];
        check(waitFor(^BOOL { return coloredText(session,@"中",0x222222,0xe8e8e8); }),@"UTF-8 search highlights the full-width match");
        [session sendText:@":noh\rgg0vll"];
        check(waitFor(^BOOL { return coloredText(session,@"alp",0x222222,0xe8e8e8); }),@"Visual selection uses pale white rather than red");
        [session sendText:@"\x1b"];
        NSString *starFile=[directory stringByAppendingPathComponent:@"star.txt"];
        [@"foo foobar foo\n" writeToFile:starFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[starFile stringByAppendingString:@"\rgg0*"]]];
        check(waitFor(^BOOL { return coloredCount(session,@"foo",0x222222,0xe8e8e8)==2 && coloredText(session,@"foobar",0xe8e8d3,0x222222); }),@"Normal star highlights complete words and skips substring matches");
        NSString *grepFile=[directory stringByAppendingPathComponent:@"grep.txt"];
        NSMutableString *grepData=[NSMutableString string];
        for (int n=1;n<=80;++n) [grepData appendFormat:@"foo item %d 中文\n",n];
        [grepData writeToFile:grepFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[grepFile stringByAppendingString:@"\r:grep foo"]]];
        VTermColor cyan; vterm_state_get_palette_color(vterm_obtain_state(session.terminal),14,&cyan);
        vterm_screen_convert_color_to_rgb(session.screen,&cyan);
        int cyanRGB=(cyan.rgb.red<<16)|(cyan.rgb.green<<8)|cyan.rgb.blue;
        check(waitFor(^BOOL { return coloredText(session,@":grep foo",cyanRGB,0x151515); }),@"command line has bright cyan foreground");
        [session sendText:@"\r"];
        check(waitFor(^BOOL { return [session.state[@"grepActive"] boolValue] && [session.state[@"grepCount"] intValue]==80; }),@"grep opens modal list with all eighty matching lines");
        check(waitFor(^BOOL { return !session.cursorVisible; }),@"grep hides the text caret while list selection remains visible");
        for (int poll=0;poll<8;++poll) request(session,MIJOE_GUI_STATUS,nil);
        check(!session.cursorVisible,@"grep caret stays hidden through repeated idle status refreshes");
        check(waitFor(^BOOL { return coloredText(session,@"1: foo item 1",0x222222,0xe8e8e8); }),@"grep selected line has pale white background and line number");
        check([session.screenText containsString:@"foo item 1"] && [session.screenText containsString:@"Enter: keep preview"],@"grep overlay retains document around centered results");
        int grepRows,grepColumns; vterm_get_size(session.terminal,&grepRows,&grepColumns);
        int grepWidth=grepColumns*3/4,grepHeight=grepRows*2/3;
        int grepX=(grepColumns-grepWidth)/2,grepY=(grepRows-grepHeight)/2;
        BOOL aligned=YES;
        for (int row=grepY+1;row<grepY+grepHeight-1;++row) {
            VTermScreenCell left,right;
            vterm_screen_get_cell(session.screen,(VTermPos){row,grepX},&left);
            vterm_screen_get_cell(session.screen,(VTermPos){row,grepX+grepWidth-1},&right);
            if (left.chars[0]!='|' || right.chars[0]!='|') { aligned=NO; fprintf(stderr,"border mismatch at row %d: %u / %u\n",row,left.chars[0],right.chars[0]); }
        }
        check(aligned,@"grep modal borders align with full-width Chinese previews");
        NSBitmapImageRep *grepImage=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:grepImage];
        [[grepImage representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[directory stringByAppendingPathComponent:@"grep-modal.png"] atomically:YES];
        NSEvent *controlN=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagControl timestamp:0 windowNumber:0 context:nil characters:@"\x0e" charactersIgnoringModifiers:@"n" isARepeat:NO keyCode:45];
        [view keyDown:controlN];
        check(waitFor(^BOOL { return [session.state[@"grepSelected"] intValue]==1; }),@"native Ctrl N advances grep result without editing document");
        [session sendText:@"G"];
        check(waitFor(^BOOL { return [session.state[@"grepSelected"] intValue]==79 && [session.screenText containsString:@"80: foo item 80"]; }),@"grep list scrolls to its final result");
        [session sendText:@"\x1b"];
        check(waitFor(^BOOL { return ![session.state[@"grepActive"] boolValue] && [session.state[@"fileOffset"] longLongValue]==0; }),@"Escape dismisses grep and restores original document cursor");
        check(waitFor(^BOOL { return session.cursorVisible; }),@"closing grep restores the document caret");
        [session sendText:@"gg0v2l:grep \x12" @"0"];
        check(waitFor(^BOOL { return [session.state[@"command"] isEqual:@"grep foo"]; }),@"Visual selection inserts into command line through Ctrl R zero");
        [session sendText:@"\r"];
        check(waitFor(^BOOL { return [session.state[@"grepActive"] boolValue] && [session.state[@"grepCount"] intValue]==80; }),@"register-pasted grep finds all matching lines");
        [session sendText:@"\x1bgg0\x0b\x02" @"3l\x0b\x0b:grep \x12" @"0"];
        check(waitFor(^BOOL { return [session.state[@"command"] isEqual:@"grep foo"]; }),@"JOE Ctrl K marked selection inserts through Ctrl R zero");
        [session sendText:@"\x1b"];

        [session sendText:@"\""];
        check(waitFor(^BOOL { return [session.screenText containsString:@"register?"]; }),@"quote prefix visibly asks for register name");
        [session sendText:@"a"];
        check(waitFor(^BOOL { return [session.screenText containsString:@"register selected"]; }),@"register name selection echoes its pending operation");
        [session sendText:@"\x1b:reg 0\r"];
        check(waitFor(^BOOL { return [session.screenText containsString:@"MiJoe registers"] && [session.screenText containsString:@"foo"]; }),@"register viewer shows copied selection without modifying text");
        [session sendText:@"q"];
        NSString *macroFile=[directory stringByAppendingPathComponent:@"macro.txt"];
        [@"abc\n" writeToFile:macroFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[macroFile stringByAppendingString:@"\r"]]];
        request(session,MIJOE_GUI_STATUS,nil);
        [view keyDown:functionKey(96,NSF5FunctionKey)];
        check(waitFor(^BOOL { return [session.state[@"macroRecording"] boolValue] && [session.screenText containsString:@"REC"]; }),@"native F5 starts recording with persistent REC status");
        [session sendText:@"iZ\x1bl"];
        request(session,MIJOE_GUI_STATUS,nil);
        [view keyDown:functionKey(96,NSF5FunctionKey)];
        check(waitFor(^BOOL { return ![session.state[@"macroRecording"] boolValue] && [session.state[@"macroSteps"] intValue]==4; }),@"native F5 stops and saves four editing keys");
        [view keyDown:functionKey(97,NSF6FunctionKey)];
        check(waitFor(^BOOL { return ![session.state[@"macroPlaying"] boolValue] && [session.screenText containsString:@"ZZabc"]; }),@"native F6 replays recorded editing once");
        [session sendText:@"uu:macro\r"];
        check(waitFor(^BOOL { return [session.screenText containsString:@"MiJoe macro"] && [session.screenText containsString:@"<Esc>"]; }),@"macro viewer displays readable recorded keys after undo");
        [session sendText:@"q"];
        /* Same ft command, split across two native inputs and a status round trip. */
        NSString *findFile=[directory stringByAppendingPathComponent:@"find-t.txt"];
        [@"abctdef t end\n" writeToFile:findFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[findFile stringByAppendingString:@"\rgg0f"]]];
        request(session,MIJOE_GUI_STATUS,nil);[session sendText:@"t"];
        check(waitFor(^BOOL{return [session.state[@"fileOffset"] intValue]==3 && session.cursor.col==3;}),@"native Normal ft reaches the next t on the same line");
        [session sendText:@";"];
        check(waitFor(^BOOL{return [session.state[@"fileOffset"] intValue]==8 && session.cursor.col==8;}),@"native repeat find reaches the following t");
        NSString *objectFile=[directory stringByAppendingPathComponent:@"text-objects.txt"];
        [@"one (alpha beta) \"gamma delta\" tail\n" writeToFile:objectFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[objectFile stringByAppendingString:@"\rgg02lviw"]]];
        check(waitFor(^BOOL{return [session.state[@"mode"] isEqual:@"V"] && session.cursor.col==2;}),@"native viw selects the complete word from its middle");
        [session sendText:@"y:\x12" "0"];
        check(waitFor(^BOOL{return [session.state[@"command"] isEqual:@"one"];}),@"native text object yank fills the command register");
        [session sendText:@"\x1b" "f(di(:w\r"];
        check(waitFor(^BOOL{return [[NSString stringWithContentsOfFile:objectFile encoding:NSUTF8StringEncoding error:NULL] isEqual:@"one () \"gamma delta\" tail\n"];}),@"native inner block delete preserves both delimiters");
        NSString *pagingFile=[directory stringByAppendingPathComponent:@"paging.txt"];
        NSMutableString *pagingText=[NSMutableString string];
        for(int i=1;i<=300;++i)[pagingText appendFormat:@"line %03d payload\n",i];
        [pagingText writeToFile:pagingFile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [session sendText:[@":e " stringByAppendingString:[pagingFile stringByAppendingString:@"\r150G0"]]];
        request(session,MIJOE_GUI_STATUS,nil);
        NSEvent *controlU=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagControl timestamp:0 windowNumber:0 context:nil characters:@"\x15" charactersIgnoringModifiers:@"u" isARepeat:NO keyCode:32];
        NSEvent *shiftControlU=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:(NSEventModifierFlagControl|NSEventModifierFlagShift) timestamp:0 windowNumber:0 context:nil characters:@"\x15" charactersIgnoringModifiers:@"U" isARepeat:NO keyCode:32];
        for(NSString *enter in @[@"",@"i",@"v"]){
            [session sendText:[@"\x1b" "150G0" stringByAppendingString:enter]];request(session,MIJOE_GUI_STATUS,nil);
            [view keyDown:controlU];long long up=[request(session,MIJOE_GUI_STATUS,nil)[@"fileOffset"] longLongValue];
            [session sendText:[@"\x1b" "150G0" stringByAppendingString:enter]];request(session,MIJOE_GUI_STATUS,nil);
            [view keyDown:functionKey(116,NSPageUpFunctionKey)];
            check([request(session,MIJOE_GUI_STATUS,nil)[@"fileOffset"] longLongValue]==up && up<149*17,@"native Ctrl U matches full PageUp in each editing mode");
            [session sendText:[@"\x1b" "150G0" stringByAppendingString:enter]];request(session,MIJOE_GUI_STATUS,nil);
            [view keyDown:shiftControlU];long long down=[request(session,MIJOE_GUI_STATUS,nil)[@"fileOffset"] longLongValue];
            [session sendText:[@"\x1b" "150G0" stringByAppendingString:enter]];request(session,MIJOE_GUI_STATUS,nil);
            [view keyDown:functionKey(121,NSPageDownFunctionKey)];
            check([request(session,MIJOE_GUI_STATUS,nil)[@"fileOffset"] longLongValue]==down && down>149*17,@"native Ctrl Shift U matches full PageDown in each editing mode");
        }
        [session sendText:@"\x1b"];
        [session sendText:[@":e " stringByAppendingString:[grepFile stringByAppendingString:@"\rgg0"]]];
        request(session,MIJOE_GUI_STATUS,nil);
        MJDamageProbe *damageProbe=[MJDamageProbe new];session.delegate=damageProbe;
        NSDate *quietUntil=[NSDate dateWithTimeIntervalSinceNow:0.03];
        while(quietUntil.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:quietUntil];
        [damageProbe.rects removeAllObjects];damageProbe.full=0;
        [session sendText:@"l"];request(session,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return damageProbe.rects.count>0;}),@"cursor movement delivers bounded terminal damage rectangles");
        int area=0,dr,dc;vterm_get_size(session.terminal,&dr,&dc);
        for(NSValue *value in damageProbe.rects){VTermRect rect;[value getValue:&rect size:sizeof(rect)];area+=(rect.end_row-rect.start_row)*(rect.end_col-rect.start_col);}
        check(!damageProbe.full && area<dr*dc/4,@"cursor movement avoids full-editor invalidation");
        [damageProbe.rects removeAllObjects];damageProbe.acknowledgments=0;
        [session request:MIJOE_GUI_SCROLL path:@"-1" completion:nil];
        check(waitFor(^BOOL{return damageProbe.rects.count>0;}),@"fire-and-forget scrolling still updates the terminal");
        quietUntil=[NSDate dateWithTimeIntervalSinceNow:0.03];
        while(quietUntil.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:quietUntil];
        check(!damageProbe.acknowledgments,@"scrolling without completion avoids redundant action acknowledgments");
        MJScrollProbe *scrollProbe=[MJScrollProbe new];
        MJEditorView *scrollView=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,720,400)];scrollView.session=(MJEditorSession *)scrollProbe;
        MJScrollPayload *wheel=[MJScrollPayload new];wheel.delta=-1;
        for(int n=0;n<100;n++)[scrollView scrollWheel:(NSEvent *)wheel];
        check(waitFor(^BOOL {int sum=0;for(NSString *value in scrollProbe.events)sum+=value.intValue;return sum==-100;}),@"coalesced wheel scrolling preserves all requested lines");
        check(scrollProbe.events.count==5,@"one hundred wheel events become five bounded scroll requests");
        MJScrollProbe *preciseProbe=[MJScrollProbe new];
        MJEditorView *preciseView=[[MJEditorView alloc]initWithFrame:scrollView.bounds];preciseView.session=(MJEditorSession *)preciseProbe;
        NSFont *scrollFont=[NSFont fontWithName:@"Menlo-Regular" size:preciseView.fontSize];
        CGFloat cellHeight=MAX(6,ceil(scrollFont.ascender-scrollFont.descender+scrollFont.leading)+preciseView.lineSpacing);
        wheel.precise=YES;wheel.delta=-cellHeight/4;
        for(int n=0;n<2;n++)[preciseView scrollWheel:(NSEvent *)wheel];
        wheel.delta=0;wheel.phase=NSEventPhaseEnded;[preciseView scrollWheel:(NSEvent *)wheel];
        wheel.phase=NSEventPhaseNone;wheel.delta=-cellHeight/4;
        for(int n=0;n<2;n++)[preciseView scrollWheel:(NSEvent *)wheel];
        check(!preciseProbe.events.count,@"subline trackpad deltas are not amplified or carried into another gesture");
        for(int n=0;n<2;n++)[preciseView scrollWheel:(NSEvent *)wheel];
        check(waitFor(^BOOL{return preciseProbe.events.count>0;}),@"trackpad fractions accumulate into a whole scroll line");
        check(preciseProbe.events.count==1 && [preciseProbe.events[0] intValue]==-1,@"trackpad input coalesces without losing its direction");
        MJScrollProbe *orderedProbe=[MJScrollProbe new];MJEditorView *orderedView=[[MJEditorView alloc]initWithFrame:scrollView.bounds];orderedView.session=(MJEditorSession *)orderedProbe;
        wheel.precise=NO;wheel.delta=-1;
        for(int n=0;n<10;n++)[orderedView scrollWheel:(NSEvent *)wheel];
        [orderedView keyDown:functionKey(123,NSLeftArrowFunctionKey)];
        check(orderedProbe.events.count==2 && [orderedProbe.events[0] intValue]==-10 && [orderedProbe.events[1] isEqual:@"key"],@"pending scroll is flushed before a later keyboard movement");
        const char *part1="\033[?202",*part2="6h\033[5;5HFRAME_ATOMIC_TEST",*part3="\033[?2026",*part4="l";
        [session consumeOutput:part1 length:strlen(part1)];[session consumeOutput:part2 length:strlen(part2)];
        check(![session.screenText containsString:@"FRAME_ATOMIC_TEST"],@"partial native repaint frames never change the displayed terminal model");
        [session consumeOutput:part3 length:strlen(part3)];
        check(![session.screenText containsString:@"FRAME_ATOMIC_TEST"],@"split end markers cannot prematurely publish a repaint");
        [session consumeOutput:part4 length:1];
        check([session.screenText containsString:@"FRAME_ATOMIC_TEST"],@"a complete native repaint frame is committed atomically");
        [session consumeOutput:"Z" length:1];
        check([session.screenText containsString:@"FRAME_ATOMIC_TESTZ"],@"unframed terminal output remains compatible");
        check(![request(session,MIJOE_GUI_STATUS,nil)[@"changed"] boolValue] && [request(session,MIJOE_GUI_QUIT,nil)[@"ok"] boolValue] && waitFor(^BOOL { return !session.running; }),@"theme switching preserves the document and exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(results.count),@"checks":results,@"real_ime_candidates_tested":@NO} options:NSJSONWritingPrettyPrinted error:NULL];
        [report writeToFile:[NSString stringWithUTF8String:argv[3]] atomically:YES];
    }
    return 0;
}
