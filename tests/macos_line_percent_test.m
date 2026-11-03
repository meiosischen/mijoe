/* Rendered logical-line progress remains independent of byte/display offsets. */
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
    check(waitFor(^BOOL{return result!=nil;}),@"native status request responds");return result;
}
static NSString *header(MJEditorSession *s) {
    int rows,cols;vterm_get_size(s.terminal,&rows,&cols);NSMutableString *text=[NSMutableString string];
    for(int x=0;x<cols;++x){VTermScreenCell c;vterm_screen_get_cell(s.screen,(VTermPos){0,x},&c);uint32_t ch=c.chars[0];[text appendFormat:@"%C",(unichar)(ch && ch<128 ? ch : ' ')];}return text;
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        if(argc!=4)return 2;
        [NSApplication sharedApplication];checks=[NSMutableArray array];
        NSString *file=[@(argv[2]) stringByAppendingPathComponent:@"source.txt"];
        NSString *longLine=[@"x" stringByPaddingToLength:2000 withString:@"x" startingAtIndex:0];
        [[longLine stringByAppendingString:@"\na\nb\nc\n"] writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:file error:NULL];
        check(s!=nil,@"line percentage native session starts");
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,800,420)];view.session=s;
        request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [header(s) containsString:@" 25% FS "];}),@"right status shows 25 percent on line one of four, despite its long byte length");
        [s sendText:@"j"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [header(s) containsString:@" 50% FS "];}),@"line movement updates the rendered percentage");
        [s sendText:@"G"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [header(s) containsString:@" 100% FS "];}),@"last real text line shows 100 percent with a final newline");
        [s sendText:@"gg\x14R12\r:set wrap\r"];request(s,MIJOE_GUI_STATUS,nil);
        [s sendKey:VTERM_KEY_PAGEDOWN modifiers:0];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [header(s) containsString:@" 25% FS "];}),@"paging through soft-wrapped rows retains logical-line progress");
        [view setFrameSize:NSMakeSize(500,320)];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [header(s) containsString:@" 25% FS "];}),@"percentage remains visible after native window resize");
        [s sendText:@":enew\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return [header(s) containsString:@" 100% FS "];}),@"empty buffer shows a defined 100 percent without division by zero");
        check([request(s,MIJOE_GUI_QUIT,nil)[@"ok"] boolValue] && waitFor(^BOOL{return !s.running;}),@"owned native child exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];
        [report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
