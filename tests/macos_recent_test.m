/* Real offscreen modal rendering and profile preferences; owns all test sessions. */
#import "MJEditorView.h"
#import "MJPreferences.h"
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
    check(waitFor(^BOOL{return result!=nil;}),@"editor responds to native control requests");return result;
}
static BOOL character(MJEditorSession *s,int row,int col,uint32_t character) {
    VTermScreenCell cell;vterm_screen_get_cell(s.screen,(VTermPos){row,col},&cell);return cell.chars[0]==character;
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        if(argc!=4)return 2;
        [NSApplication sharedApplication];checks=[NSMutableArray array];
        NSString *dir=@(argv[2]),*profile=[dir stringByAppendingPathComponent:@".mj"];
        NSString *first=[dir stringByAppendingPathComponent:@"source.txt"];
        [@"original\n" writeToFile:first atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:first error:NULL];
        check(s!=nil,@"recent-file native session starts");
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,720,400)];view.session=s;
        request(s,MIJOE_GUI_STATUS,nil);
        NSMutableArray *files=[NSMutableArray array];
        for(int n=0;n<25;++n){
            NSString *name=[NSString stringWithFormat:@"%02d-中文-%@-END.txt",n,[@"x" stringByPaddingToLength:80 withString:@"x" startingAtIndex:0]];
            NSString *file=[dir stringByAppendingPathComponent:name];[files addObject:file];
            [@"payload\n" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL];
            [s sendText:[NSString stringWithFormat:@":e %@\r",file]];request(s,MIJOE_GUI_STATUS,nil);
        }
        [s sendText:@"\x14O:enew\r:recent\r"];NSDictionary *state=request(s,MIJOE_GUI_STATUS,nil);
        check([state[@"recentActive"] boolValue] && [state[@"recentCount"] intValue]==26,@"recent modal contains all files including the startup document");
        check([state[@"recentRows"] intValue]>[state[@"recentCount"] intValue],@"long Unicode paths consume multiple display rows");
        check(waitFor(^BOOL{return [s.screenText containsString:@"[RO TEXT]"];}),@"the latest entry shows its saved read-only text mode");
        int rows,cols;vterm_get_size(s.terminal,&rows,&cols);
        int width=cols*3/4,height=rows*2/3,x=(cols-width)/2,y=(rows-height)/2;
        check(waitFor(^BOOL{return character(s,y+1,x+width-1,'#');}),@"overflowing recent-file viewport paints a scrollbar thumb");
        check(!s.cursorVisible,@"modal hides the document caret");
        [s sendKey:VTERM_KEY_PAGEDOWN modifiers:0];state=request(s,MIJOE_GUI_STATUS,nil);
        check([state[@"recentTop"] intValue]>0 && [state[@"recentSelected"] intValue]>0,@"PageDown advances the wrapped viewport and its selected item");
        [s sendKey:VTERM_KEY_PAGEUP modifiers:0];state=request(s,MIJOE_GUI_STATUS,nil);
        check([state[@"recentTop"] intValue]==0 && [state[@"recentSelected"] intValue]==0,@"PageUp returns to the newest entry");
        check(![request(s,MIJOE_GUI_SAVE,nil)[@"ok"] boolValue],@"the recent modal blocks background native editing commands");
        [s sendText:@"\r"];state=request(s,MIJOE_GUI_STATUS,nil);
        check(![state[@"recentActive"] boolValue] && [state[@"file"] isEqual:files.lastObject] && [state[@"readonly"] boolValue],@"Enter reopens the entire Unicode path with its saved read-only mode");
        [NSFileManager.defaultManager removeItemAtPath:files.lastObject error:NULL];
        [s sendText:@":recent\r\r"];state=request(s,MIJOE_GUI_STATUS,nil);
        check([state[@"recentActive"] boolValue] && [state[@"recentCount"] intValue]==25,@"missing file is removed from persistent history without closing the chooser");
        check(waitFor(^BOOL{return [s.screenText containsString:@"no longer exists"]; }),@"missing-file deletion displays a visible error");
        [view setFrameSize:NSMakeSize(520,300)];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return character(s,2,20,0) || character(s,2,20,' ');}),@"resizing the modal clears the previous popup footprint");
        NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];[view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[dir stringByAppendingPathComponent:@"recent.png"] atomically:YES];
        [s sendText:@"\x03"];state=request(s,MIJOE_GUI_STATUS,nil);
        check(![state[@"recentActive"] boolValue] && ![state[@"changed"] boolValue],@"Ctrl+C dismisses the modal without editing a file");
        check(waitFor(^BOOL{return s.cursorVisible;}),@"closing the recent modal restores the green document caret");
        [MJPreferences saveFontSize:17 lineSpacing:-4];
        NSData *data=[NSData dataWithContentsOfFile:[profile stringByAppendingPathComponent:@"macos.json"]];
        NSDictionary *prefs=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        check([prefs[@"EditorFontSize"] intValue]==17 && [prefs[@"EditorLineSpacing"] intValue]==-4,@"native typography persists inside ~/.mj/macos.json");
        [@"{\"EditorFontSize\":[],\"EditorLineSpacing\":\"bad\"}" writeToFile:[profile stringByAppendingPathComponent:@"macos.json"] atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        prefs=MJPreferences.typography;
        check([prefs[@"EditorFontSize"] intValue]==13 && [prefs[@"EditorLineSpacing"] intValue]==-6,@"malformed preference values fall back safely");
        check([[NSString stringWithContentsOfFile:[profile stringByAppendingPathComponent:@"macos.json"] encoding:NSUTF8StringEncoding error:NULL] containsString:@"[]"],@"reading malformed preference values preserves the original personal file");
        check([request(s,MIJOE_GUI_QUIT,nil)[@"ok"] boolValue] && waitFor(^BOOL{return !s.running;}),@"recent-file test child exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];
        [report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
