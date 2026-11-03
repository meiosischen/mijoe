/* Offscreen renderer checks: only sessions and views owned by this test. */
#import "MJEditorView.h"
static NSMutableArray *checks;
static void check(BOOL ok,NSString *label) {
    if(!ok){fprintf(stderr,"FAIL %s\n",label.UTF8String);exit(1);}
    [checks addObject:label];printf("PASS %s\n",label.UTF8String);fflush(stdout);
}
static BOOL waitFor(BOOL (^predicate)(void)) {
    NSDate *until=[NSDate dateWithTimeIntervalSinceNow:8];
    while(!predicate() && until.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
    return predicate();
}
static NSDictionary *request(MJEditorSession *s,int action,NSString *path) {
    __block NSDictionary *result=nil;
    [s request:action path:path completion:^(NSDictionary *r){result=r;}];
    check(waitFor(^BOOL{return result!=nil;}),@"editor responds without stalling");return result;
}
static VTermScreenCell cell(MJEditorSession *s,int row,int col) {
    VTermScreenCell c;vterm_screen_get_cell(s.screen,(VTermPos){row,col},&c);return c;
}
static NSString *ascii(MJEditorSession *s,int row,int length) {
    NSMutableString *text=[NSMutableString string];
    for(int x=0;x<length;++x){VTermScreenCell c=cell(s,row,x);[text appendFormat:@"%C",(unichar)(c.chars[0] ?: ' ')];}
    return text;
}
static BOOL bg(MJEditorSession *s,int row,int col,int rgb) {
    VTermScreenCell c=cell(s,row,col);vterm_screen_convert_color_to_rgb(s.screen,&c.bg);
    if(c.attrs.reverse){vterm_screen_convert_color_to_rgb(s.screen,&c.fg);c.bg=c.fg;}
    return ((c.bg.rgb.red<<16)|(c.bg.rgb.green<<8)|c.bg.rgb.blue)==rgb;
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        if(argc!=4)return 2;
        [NSApplication sharedApplication];checks=[NSMutableArray array];
        NSString *dir=@(argv[2]),*file=[dir stringByAppendingPathComponent:@"wrap.txt"];
        NSString *original=@"abcdefghijklmnopqrstuvwxXYZ\nnext\n";
        [original writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];
        check(s!=nil,@"native wrapped editor starts");
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,720,400)];view.session=s;
        check(![request(s,MIJOE_GUI_STATUS,nil)[@"softwrap"] boolValue],@"soft wrap defaults off");
        check(waitFor(^BOOL{return [ascii(s,1,27) isEqual:@"abcdefghijklmnopqrstuvwxXYZ"];}),@"disabled wrapping preserves the original single-row display");
        [s sendText:@"\x14R12\r:set wrap\r"];
        check(waitFor(^BOOL{return [ascii(s,1,12) isEqual:@"abcdefghijkl"] && [ascii(s,2,12) isEqual:@"mnopqrstuvwx"] && [ascii(s,3,3) isEqual:@"XYZ"] && [ascii(s,4,4) isEqual:@"next"];}),@"existing logical line paints three complete display rows without dropping characters");
        [s sendText:@"gg012l"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return s.cursor.row==2 && s.cursor.col==0;}),@"cursor crosses a soft boundary to the next display row");
        [s sendText:@"j"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return s.cursor.row==4;}),@"Vim j retains logical-line motion");
        [s sendText:@"gg09lv5l"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return bg(s,1,9,0xe8e8e8) && bg(s,2,2,0xe8e8e8);}),@"Visual selection spans soft rows using the normal selection colors");
        check([request(s,MIJOE_GUI_COPY,nil)[@"text"] isEqual:@"jklmno"],@"copy uses file bytes without inserting display newlines");
        [s sendText:@"\x1b/mnop\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return bg(s,2,0,0xe8e8e8) && bg(s,2,3,0xe8e8e8);}),@"search matching retains highlighting in wrapped rows");
        [s sendText:@":noh\rgg0\x14R8\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [ascii(s,2,8) isEqual:@"ijklmnop"];}),@"changing Right Margin immediately reflows existing content");
        [s sendText:@":set nowrap\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [ascii(s,1,27) isEqual:@"abcdefghijklmnopqrstuvwxXYZ"];}),@"turning soft wrap off restores the original layout");
        check(![request(s,MIJOE_GUI_STATUS,nil)[@"changed"] boolValue] && [[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:original],@"display controls never dirty or rewrite the file");
        NSString *source=[dir stringByAppendingPathComponent:@"syntax.c"];
        [@"char *s = \"abcdefghijklmnop\";\n" writeToFile:source atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [s sendText:[NSString stringWithFormat:@":e %@\r\x14R12\r:set wrap\r",source]];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{
            VTermScreenCell a=cell(s,1,11),b=cell(s,2,0);
            vterm_screen_convert_color_to_rgb(s.screen,&a.fg);vterm_screen_convert_color_to_rgb(s.screen,&b.fg);
            return a.chars[0]=='a' && b.chars[0]=='b' && a.fg.rgb.red==0xfa && a.fg.rgb.green==0xff && a.fg.rgb.blue==0x8a &&
                   b.fg.rgb.red==a.fg.rgb.red && b.fg.rgb.green==a.fg.rgb.green && b.fg.rgb.blue==a.fg.rgb.blue;
        }),@"syntax highlighting remains correct across soft boundaries");
        NSString *unicode=[dir stringByAppendingPathComponent:@"unicode.txt"];
        [@"abcdefghijk中文Z\n123456789\tXYZ\ne\u0301abcdefghij中文\n" writeToFile:unicode atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        /* This fixture deliberately spans a tab across column 12; keep its width explicit. */
        [s sendText:[NSString stringWithFormat:@":e %@\r\024D8\r\x14R12\r:set wrap\r",unicode]];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return cell(s,2,0).chars[0]==0x4e2d && cell(s,2,0).width==2 && cell(s,2,2).chars[0]==0x6587;}),@"wide characters move intact to the next row rather than showing clipping markers");
        check(waitFor(^BOOL{return [ascii(s,4,7) isEqual:@"    XYZ"];}),@"a tab crossing the margin preserves its remaining display cells");
        check(cell(s,5,0).chars[0]=='e' && cell(s,5,0).chars[1]==0x301,@"combining marks remain attached to their base character");
        [s sendText:@"gg0\x14N"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return s.cursor.col>0 && cell(s,2,0).chars[0]==' ';}),@"continuation rows leave the line-number gutter blank");
        [s sendText:@"\x14N"];request(s,MIJOE_GUI_STATUS,nil);
        NSString *longfile=[dir stringByAppendingPathComponent:@"long.txt"];
        [[@"0123456789" stringByPaddingToLength:10000 withString:@"0123456789" startingAtIndex:0] writeToFile:longfile atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [s sendText:[NSString stringWithFormat:@":e %@\r:set wrap\rgg$",longfile]];request(s,MIJOE_GUI_STATUS,nil);
        int rows,cols;vterm_get_size(s.terminal,&rows,&cols);
        check(waitFor(^BOOL{return s.cursor.row>=1 && s.cursor.row<rows && s.cursor.col>=0 && s.cursor.col<cols;}),@"end of an over-screen logical line scrolls into view");
        long long before=[request(s,MIJOE_GUI_STATUS,nil)[@"fileOffset"] longLongValue];
        [s sendKey:VTERM_KEY_PAGEUP modifiers:0];
        check([request(s,MIJOE_GUI_STATUS,nil)[@"fileOffset"] longLongValue]<before,@"PageUp moves through display rows inside a single file line");
        [s sendText:@"gg0\x14R100\r"];request(s,MIJOE_GUI_STATUS,nil);
        [view setFrameSize:NSMakeSize(256,400)];request(s,MIJOE_GUI_STATUS,nil);
        vterm_get_size(s.terminal,&rows,&cols);
        check([s.state[@"wrapWidth"] intValue]==cols,@"soft wrap width is limited by the resized text viewport");
        check(waitFor(^BOOL{return [ascii(s,1,10) isEqual:@"0123456789"];}),@"resized viewport repaints from the correct file position");
        NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[dir stringByAppendingPathComponent:@"softwrap.png"] atomically:YES];
        check([request(s,MIJOE_GUI_QUIT,nil)[@"ok"] boolValue],@"clean wrapped document closes without a save prompt");
        check(waitFor(^BOOL{return !s.running;}),@"test child exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];
        [report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
