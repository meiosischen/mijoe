/* Command-bar provenance, read-only paging and persistence in real AppKit sessions. */
#import "MJEditorView.h"
static NSMutableArray *checks;
static BOOL waitFor(BOOL (^p)(void)) {
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:12];
    while(!p() && end.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
    return p();
}
static void check(BOOL ok,NSString *label) {
    if(!ok){fprintf(stderr,"FAIL %s\n",label.UTF8String);exit(1);}
    [checks addObject:label];printf("PASS %s\n",label.UTF8String);fflush(stdout);
}
static NSDictionary *request(MJEditorSession *s,int action) {
    __block NSDictionary *r=nil;[s request:action path:nil completion:^(NSDictionary *v){r=v;}];
    check(waitFor(^BOOL{return r!=nil;}),@"native control request responds");return r;
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        if(argc!=4)return 2;[NSApplication sharedApplication];checks=[NSMutableArray array];
        NSString *dir=@(argv[2]),*file=[dir stringByAppendingPathComponent:@"source.txt"];
        [@"abcdef\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,900,420)];view.session=s;
        request(s,MIJOE_GUI_STATUS);
        [s sendText:@":set-hl \"gogogo 123\"\r/abc\r?def\r"];request(s,MIJOE_GUI_STATUS);
        [s sendKey:VTERM_KEY_FUNCTION(5) modifiers:0];[s sendText:@":set-hl macro_word\r"];request(s,MIJOE_GUI_STATUS);
        [s sendKey:VTERM_KEY_FUNCTION(5) modifiers:0];[s sendKey:VTERM_KEY_FUNCTION(6) modifiers:0];request(s,MIJOE_GUI_STATUS);
        [s sendText:@":history\r"];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"MiJoe :history"] && [s.screenText containsString:@"Command-bar history: 5 / 200"];}),@"history shows only the five submitted command-bar inputs");
        check([s.screenText containsString:@"INPUT"] && [s.screenText containsString:@"REC"] && [s.screenText containsString:@"PLAY"],@"history distinguishes input, macro recording and macro playback");
        check([s.screenText containsString:@":set-hl \"gogogo 123\""] && [s.screenText containsString:@"/abc"] && [s.screenText containsString:@"?def"],@"quoted command and both search prefixes render literally");
        [s sendText:@"\x03"];request(s,MIJOE_GUI_STATUS);
        NSMutableString *batch=[NSMutableString string];for(int n=0;n<60;++n)[batch appendFormat:@":noop-%03d\r",n];
        [s sendText:batch];[s sendText:@":set-hl \"中文 短语\"\r"];NSDictionary *before=request(s,MIJOE_GUI_STATUS);
        NSString *path=[dir stringByAppendingPathComponent:@".mj/.history"];NSData *saved=[NSData dataWithContentsOfFile:path];
        [s sendText:@":history\r"];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"中文 短语"] && [s.screenText containsString:@":noop-059"];}),@"latest entries appear first, including Unicode text");
        NSString *first=s.screenText;[s sendKey:VTERM_KEY_PAGEDOWN modifiers:0];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return ![s.screenText isEqual:first] && ![s.screenText containsString:@":noop-059"];}),@"PageDown advances the history viewport");
        [s sendKey:VTERM_KEY_PAGEUP modifiers:0];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@":noop-059"];}),@"PageUp restores the newest history entries");
        [view setFrameSize:NSMakeSize(520,300)];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"中文 短语"];}),@"history reflows on a narrower native view");
        NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[dir stringByAppendingPathComponent:@"history.png"] atomically:YES];
        [s sendText:@"iBADdd\x03"];NSDictionary *after=request(s,MIJOE_GUI_STATUS);
        check(![after[@"changed"] boolValue] && [after[@"fileOffset"] isEqual:before[@"fileOffset"]],@"typing in history and cancelling preserves the document and cursor");
        check([[NSData dataWithContentsOfFile:path] isEqual:saved],@"queries, paging and cancellation do not add command-history rows");
        request(s,MIJOE_GUI_DISCARD);check(waitFor(^BOOL{return !s.running;}),@"first owned session exits cleanly");
        s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];view.session=s;
        check([request(s,MIJOE_GUI_STATUS)[@"temporaryHighlights"] intValue]==0,@"persisted set-hl text does not restore temporary highlights");
        [s sendText:@":history\r"];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"中文 短语"];}),@"the next editor process can print the previous command history");
        [s sendText:@"q"];request(s,MIJOE_GUI_STATUS);
        check([[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:@"abcdef\n"],@"history never changes source-file bytes");
        request(s,MIJOE_GUI_DISCARD);check(waitFor(^BOOL{return !s.running;}),@"second owned session exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];[report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
