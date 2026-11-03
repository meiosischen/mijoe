/* Owns an offscreen AppKit editor: rendered confirmation and undo behavior. */
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
static NSDictionary *request(MJEditorSession *s,int action) {
    __block NSDictionary *result=nil;
    [s request:action path:nil completion:^(NSDictionary *r){result=r;}];
    check(waitFor(^BOOL{return result!=nil;}),@"native control request responds");return result;
}
static BOOL selected(MJEditorSession *s,int col) {
    VTermScreenCell c;vterm_screen_get_cell(s.screen,(VTermPos){1,col},&c);
    vterm_screen_convert_color_to_rgb(s.screen,&c.bg);
    return c.bg.rgb.red==232 && c.bg.rgb.green==232 && c.bg.rgb.blue==232;
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        if(argc!=4)return 2;
        [NSApplication sharedApplication];checks=[NSMutableArray array];
        NSString *file=[@(argv[2]) stringByAppendingPathComponent:@"source.txt"];
        [@"foo foo foo\nfoo\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];
        check(s!=nil,@"substitute native session starts");
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,800,420)];view.session=s;
        request(s,MIJOE_GUI_STATUS);[s sendText:@":%s/foo/bar/gc\r"];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"Replace?"];}),@"confirmation prompt renders");
        check(waitFor(^BOOL{return selected(s,0) && selected(s,1) && selected(s,2) && !selected(s,3);}),@"only the current replacement match has a pale selection background");
        check(![request(s,MIJOE_GUI_SAVE)[@"ok"] boolValue],@"native save is blocked during confirmation");
        [s sendText:@"yna"];request(s,MIJOE_GUI_STATUS);
        check(waitFor(^BOOL{return [s.screenText containsString:@"bar foo bar"];}),@"confirmation choices update rendered text");
        check([request(s,MIJOE_GUI_SAVE)[@"ok"] boolValue],@"native save resumes after confirmation");
        check([[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:@"bar foo bar\nbar\n"],@"saved native replacement bytes are correct");
        [s sendText:@"u"];request(s,MIJOE_GUI_SAVE);
        check([[NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:NULL] isEqual:@"foo foo foo\nfoo\n"],@"one undo restores the complete confirmed command");
        [s sendText:@":s/foo/bar/c\r"];request(s,MIJOE_GUI_STATUS);[s sendText:@"\x03"];request(s,MIJOE_GUI_STATUS);
        check([request(s,MIJOE_GUI_SAVE)[@"ok"] boolValue],@"Ctrl+C cancels confirmation without closing the document");
        check([request(s,MIJOE_GUI_QUIT)[@"ok"] boolValue] && waitFor(^BOOL{return !s.running;}),@"owned native child exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];
        [report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
