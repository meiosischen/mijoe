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
        NSString *dir=@(argv[2]),*file=[dir stringByAppendingPathComponent:@"external.txt"];
        [@"old text\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,900,420)];view.session=s;
        request(s,MIJOE_GUI_STATUS);
        [@"new text\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        check(waitFor(^BOOL{return [s.screenText containsString:@"Reload current file?"];}),@"idle native window prompts without a keyboard event");
        [s sendText:@"y"];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"new text"] && ![s.screenText containsString:@"Reload current file?"];}),@"yes reloads external contents in the same native window");
        [s sendText:@"iLOCAL\x1b"];request(s,MIJOE_GUI_STATUS);
        [@"third text\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        check(waitFor(^BOOL{return [s.screenText containsString:@"DISCARD unsaved changes?"];}),@"dirty buffer receives an explicit discard warning");
        [s sendText:@"n"];NSDictionary *r=request(s,MIJOE_GUI_STATUS);
        check([r[@"changed"] boolValue] && [s.screenText containsString:@"LOCALnew text"],@"no preserves native buffer edits");
        check(![request(s,MIJOE_GUI_SAVE)[@"ok"] boolValue],@"native save still refuses to overwrite the declined external version");
        [s sendText:@"\x0b`y"];r=request(s,MIJOE_GUI_STATUS);
        check(![r[@"changed"] boolValue],@"manual reload after declining still supports dirty confirmation");
        [s sendText:@":set-hl third"];request(s,MIJOE_GUI_STATUS);
        [@"fourth text\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        NSDate *until=[NSDate dateWithTimeIntervalSinceNow:1.6];
        while(until.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
        check(![s.screenText containsString:@"Reload current file?"],@"native command bar is not interrupted");
        [s sendText:@"\x1b"];
        check(waitFor(^BOOL{return [s.screenText containsString:@"Reload current file?"];}),@"deferred notice appears after leaving command mode");
        [s sendText:@"y"];request(s,MIJOE_GUI_STATUS);
        check([s.screenText containsString:@"fourth text"],@"deferred refresh displays the latest source");
        request(s,MIJOE_GUI_DISCARD);check(waitFor(^BOOL{return !s.running;}),@"owned native session exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];[report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
