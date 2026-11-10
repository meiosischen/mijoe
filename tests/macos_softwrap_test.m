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
        NSString *original=[@"0123456789" stringByPaddingToLength:10000 withString:@"0123456789" startingAtIndex:0];
        [original writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];
        check(s!=nil,@"native wrapped editor starts");
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,720,400)];view.session=s;
        check([request(s,MIJOE_GUI_STATUS,nil)[@"softwrap"] boolValue],@"soft wrap defaults on");
        [s sendText:@"\x14R12\r"];
        for(NSNumber *width in @[@256,@900,@420,@1100]) {
            [s sendText:@"gg0"];request(s,MIJOE_GUI_STATUS,nil);
            [view setFrameSize:NSMakeSize(width.intValue,400)];
            NSDictionary *state=request(s,MIJOE_GUI_STATUS,nil);
            int rows,cols;vterm_get_size(s.terminal,&rows,&cols);
            check([state[@"wrapWidth"] intValue]==cols,@"dragging window uses current viewport independent of Right Margin");
            check(waitFor(^BOOL{return cell(s,2,0).chars[0]=='0'+cols%10;}),@"resized second screen row starts at correct file character");
            check(![state[@"changed"] boolValue],@"reflow keeps buffer clean");
        }
        [s sendText:@"gg$"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return s.cursor.row>=1 && s.cursor.col>=0;}),@"long line end stays visible");
        [s sendText:@":set nowrap\r"];
        check(![request(s,MIJOE_GUI_STATUS,nil)[@"softwrap"] boolValue],@"explicit nowrap remains available");
        check([[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:original],@"display wrapping preserves file bytes");
        NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[dir stringByAppendingPathComponent:@"softwrap.png"] atomically:YES];
        check([request(s,MIJOE_GUI_QUIT,nil)[@"ok"] boolValue],@"clean file closes without save prompt");
        NSData *json=[NSJSONSerialization dataWithJSONObject:checks options:NSJSONWritingPrettyPrinted error:NULL];
        [json writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
