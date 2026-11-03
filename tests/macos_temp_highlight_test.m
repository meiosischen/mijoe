/* Real AppKit/PTY cells: literal overlays, viewport bounds and buffer lifetime. */
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
static NSDictionary *request(MJEditorSession *s,int action,NSString *path) {
    __block NSDictionary *r=nil;[s request:action path:path completion:^(NSDictionary *v){r=v;}];
    check(waitFor(^BOOL{return r!=nil;}),@"native control request responds");return r;
}
static int rgb(VTermColor c,MJEditorSession *s) {
    vterm_screen_convert_color_to_rgb(s.screen,&c);return c.rgb.red<<16|c.rgb.green<<8|c.rgb.blue;
}
static int palette(MJEditorSession *s,int n) {
    VTermColor c;vterm_state_get_palette_color(vterm_obtain_state(s.terminal),n,&c);return rgb(c,s);
}
static int colored(MJEditorSession *s,NSString *text,int fg,int bg) {
    int rows,cols,count=0;vterm_get_size(s.terminal,&rows,&cols);
    for(int y=1;y<rows-1;++y)for(int x=0;x+(int)text.length<=cols;++x){
        BOOL found=YES;
        for(NSUInteger n=0;n<text.length;++n){
            VTermScreenCell c;vterm_screen_get_cell(s.screen,(VTermPos){y,x+(int)n},&c);
            if(c.chars[0]!=[text characterAtIndex:n] || rgb(c.fg,s)!=fg || rgb(c.bg,s)!=bg){found=NO;break;}
        }
        if(found)++count;
    }
    return count;
}
static BOOL temporary(MJEditorSession *s,NSString *text) {
    return colored(s,text,palette(s,0),palette(s,11))>0;
}
static BOOL cellBG(MJEditorSession *s,int row,int col,int bg) {
    VTermScreenCell c;vterm_screen_get_cell(s.screen,(VTermPos){row,col},&c);return rgb(c.bg,s)==bg;
}
static void openText(MJEditorSession *s,NSString *path,NSString *text) {
    [text writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    [s sendText:[NSString stringWithFormat:@":e %@\r",path]];
    check([request(s,MIJOE_GUI_STATUS,nil)[@"file"] isEqual:path],@"test file opens in its own buffer");
}
int main(int argc,const char **argv) {
    @autoreleasepool {
        if(argc!=4)return 2;[NSApplication sharedApplication];checks=[NSMutableArray array];
        NSString *dir=@(argv[2]),*source=[dir stringByAppendingPathComponent:@"source.txt"];
        NSString *original=@"gogogo 123 | gogogo X 123 | xgogogox | 1234\n";
        [original writeToFile:source atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        MJEditorSession *s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:source error:NULL];
        MJEditorView *view=[[MJEditorView alloc]initWithFrame:NSMakeRect(0,0,900,460)];view.session=s;
        request(s,MIJOE_GUI_STATUS,nil);[s sendText:@":set-hl \"gogogo 123\"\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"gogogo 123");}),@"quoted phrase includes its exact separating space in the yellow highlight");
        check(colored(s,@"gogogo X 123",0xe8e8d3,0x222222)==1,@"quoted phrase does not highlight separated words elsewhere");
        [s sendText:@":clear-hl\r:set-hl gogogo 123\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return colored(s,@"gogogo",palette(s,0),palette(s,11))==3 && colored(s,@"123",palette(s,0),palette(s,11))==3;}),@"unquoted terms highlight separately, including literal substrings");
        check(!cellBG(s,1,6,palette(s,11)),@"spaces between separate terms are not highlighted");
        [s sendText:@"gg0v2l"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return cellBG(s,1,0,0xe8e8e8) && cellBG(s,1,4,palette(s,11));}),@"Visual selection takes priority over a temporary match");
        [s sendText:@"\x1b/gogogo\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return colored(s,@"gogogo",0x222222,0xe8e8e8)==3;}),@"search matches take priority over the temporary color");
        [s sendText:@":noh\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return colored(s,@"gogogo",palette(s,0),palette(s,11))==3;}),@"nohlsearch leaves temporary matches active");
        [s sendText:@"gg0i!\x1b"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"gogogo");}),@"editing refreshes temporary match locations");
        [s sendText:@"u"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"gogogo") && ![s.state[@"changed"] boolValue];}),@"undo restores contents while preserving the temporary set");
        [s sendText:@":clear-hl\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return !temporary(s,@"gogogo");}),@"clear-hl removes the overlay without changing text");
        openText(s,[dir stringByAppendingPathComponent:@"line-numbers.txt"],@"first row\n\nnormal :1 ERROR\nfourth row\n");
        [s sendText:@":set-hl :1 :2 :4 ERROR\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"first row") && temporary(s,@"fourth row");}),@"line selectors highlight the first and fourth complete logical lines");
        check(cellBG(s,1,80,palette(s,11)) && cellBG(s,2,80,palette(s,11)) && cellBG(s,4,80,palette(s,11)),@"selected lines include blank lines and padding to the viewport edge");
        check(cellBG(s,3,0,0x222222) && temporary(s,@"ERROR"),@"literal terms still match independently on unselected lines");
        check(!cellBG(s,5,80,palette(s,11)),@"line highlighting does not bleed below the selected lines");
        [s sendText:@":clear-hl\r:set-hl \":1\"\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@":1") && !temporary(s,@"first row");}),@"quoted colon-number remains a literal substring");
        openText(s,[dir stringByAppendingPathComponent:@"line-wrap.txt"],@"first\nsecond\nabcdefghijk\nlast\n");
        [s sendText:@"\x14R8\r:set wrap\r:set-hl :3\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"abcdefgh") && temporary(s,@"ijk");}),@"all soft-wrapped segments of a selected logical line are highlighted");
        check(cellBG(s,3,80,palette(s,11)) && cellBG(s,4,80,palette(s,11)) && !cellBG(s,5,80,palette(s,11)),@"wrapped line padding is yellow without coloring the following logical line");
        [s sendText:@":set nowrap\r"];request(s,MIJOE_GUI_STATUS,nil);
        openText(s,[dir stringByAppendingPathComponent:@"line-eof.txt"],@"only");
        [s sendText:@":set-hl :1\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"only") && !cellBG(s,2,80,palette(s,11));}),@"unterminated last line does not highlight nonexistent rows below EOF");
        openText(s,[dir stringByAppendingPathComponent:@"line-empty-eof.txt"],@"only\n");
        [s sendText:@":set-hl :2\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return cellBG(s,2,80,palette(s,11)) && !cellBG(s,3,80,palette(s,11));}),@"the final empty logical line can be selected without coloring later screen rows");
        openText(s,[dir stringByAppendingPathComponent:@"literal.txt"],@"a.*[x]|(y)$ axxx\n");
        [s sendText:@":set-hl \"a.*[x]|(y)$\"\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"a.*[x]|(y)$");}),@"regex punctuation and pipe are literal text");
        openText(s,[dir stringByAppendingPathComponent:@"overlap.txt"],@"ababa aaaa\n");
        [s sendText:@":set-hl aba ba aa\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"ababa") && temporary(s,@"aaaa");}),@"overlapping literal matches form the complete union");
        openText(s,[dir stringByAppendingPathComponent:@"unicode.txt"],@"中文 word|中文 other\n");
        [s sendText:@":set-hl \"中文 word\"\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return cellBG(s,1,0,palette(s,11)) && cellBG(s,1,2,palette(s,11)) && cellBG(s,1,4,palette(s,11)) && cellBG(s,1,8,palette(s,11)) && !cellBG(s,1,9,palette(s,11));}),@"wide Unicode phrase highlights whole glyphs and its internal space");
        NSString *latin=[dir stringByAppendingPathComponent:@"latin.txt"];
        const unsigned char latinBytes[]={'c','a','f',0xe9,'?','\n'};
        [[NSData dataWithBytes:latinBytes length:sizeof(latinBytes)] writeToFile:latin atomically:YES];
        [s sendText:[NSString stringWithFormat:@":e %@\r",latin]];request(s,MIJOE_GUI_STATUS,nil);
        [s sendText:@"\024Eiso-8859-1\r:set-hl café\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"café");}),@"representable Latin-1 literals match exact encoded text");
        [s sendText:@":set-hl A中\r"];NSDictionary *encodingState=request(s,MIJOE_GUI_STATUS,nil);
        check([encodingState[@"temporaryHighlights"] intValue]==1 && !temporary(s,@"?"),@"unrepresentable Unicode is rejected without turning replacement characters into regex operators");
        NSString *longWord=[@"a" stringByPaddingToLength:200 withString:@"a" startingAtIndex:0];
        openText(s,[dir stringByAppendingPathComponent:@"long.txt"],[longWord stringByAppendingString:@"!\n"]);
        [s sendText:[NSString stringWithFormat:@":set-hl %@\r$",longWord]];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return cellBG(s,1,0,palette(s,11));}),@"a match beginning left of the horizontal viewport remains highlighted");
        openText(s,[dir stringByAppendingPathComponent:@"wrap.txt"],@"gogogo 123 END\n");
        [s sendText:@"\x14R8\r:set wrap\r:set-hl \"gogogo 123\"\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"gogogo") && cellBG(s,2,0,palette(s,11)) && cellBG(s,2,1,palette(s,11)) && !cellBG(s,2,2,palette(s,11));}),@"one literal remains highlighted across soft-wrapped display rows");
        NSString *large=[dir stringByAppendingPathComponent:@"large.log"];
        NSMutableData *data=[NSMutableData dataWithData:[@"ERROR marker\n" dataUsingEncoding:NSUTF8StringEncoding]];
        NSString *line=[[@"x" stringByPaddingToLength:1023 withString:@"x" startingAtIndex:0] stringByAppendingString:@"\n"];
        NSData *chunk=[line dataUsingEncoding:NSUTF8StringEncoding];for(int n=0;n<17000;++n)[data appendData:chunk];
        [data appendData:[@"ERROR end\n" dataUsingEncoding:NSUTF8StringEncoding]];[data writeToFile:large atomically:YES];
        [s sendText:[NSString stringWithFormat:@":e %@\r",large]];NSDictionary *before=request(s,MIJOE_GUI_STATUS,nil);
        check([before[@"fileBytes"] longLongValue]>16000000 && ![before[@"lazy"] boolValue],@"fixture triggers normal-open large-file syntax protection");
        [s sendText:@":set-hl ERROR\r"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"ERROR");}),@"temporary highlighting works above the automatic syntax cutoff");
        [s sendText:@"G"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return temporary(s,@"ERROR");}),@"temporary highlights work after jumping to the end of a large file");
        [s sendText:[NSString stringWithFormat:@":e %@\r",source]];request(s,MIJOE_GUI_STATUS,nil);
        check([[NSString stringWithContentsOfFile:source encoding:NSUTF8StringEncoding error:NULL] isEqual:original],@"all display commands preserve original source bytes");
        [s sendText:@":set-hl restart-sentinel\r"];
        check([request(s,MIJOE_GUI_STATUS,nil)[@"temporaryHighlights"] intValue]==1,@"an active temporary set exists immediately before process exit");
        request(s,MIJOE_GUI_DISCARD,nil);check(waitFor(^BOOL{return !s.running;}),@"first owned child exits cleanly");
        NSString *config=[dir stringByAppendingPathComponent:@".mj/config"];
        [@"-large_threshold 1\n" writeToFile:config atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        s=[[MJEditorSession alloc]initWithExecutable:@(argv[1]) file:source error:NULL];view.session=s;
        check([request(s,MIJOE_GUI_STATUS,nil)[@"temporaryHighlights"] intValue]==0,@"restarting an editor does not restore temporary terms");
        NSString *lazy=[dir stringByAppendingPathComponent:@"lazy.log"];
        [[@"ERROR payload\n" stringByPaddingToLength:3000000 withString:@"ERROR payload\n" startingAtIndex:0] writeToFile:lazy atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        [s sendText:[NSString stringWithFormat:@":e %@\r",lazy]];request(s,MIJOE_GUI_STATUS,nil);
        request(s,MIJOE_GUI_LARGE_CHOICE,@"lazy");before=request(s,MIJOE_GUI_STATUS,nil);
        [s sendText:@":set-hl ERROR\r"];NSDictionary *after=request(s,MIJOE_GUI_STATUS,nil);
        check([after[@"lazy"] boolValue] && [after[@"readonly"] boolValue] && [after[@"readBytes"] isEqual:before[@"readBytes"]],@"temporary highlights do not scan or load additional lazy-file data");
        check(waitFor(^BOOL{return temporary(s,@"ERROR");}),@"read-only lazy text displays literal matches");
        [s sendText:@":set-hl :1 :2\r"];after=request(s,MIJOE_GUI_STATUS,nil);
        check([after[@"readBytes"] isEqual:before[@"readBytes"]] && waitFor(^BOOL{return cellBG(s,1,80,palette(s,11)) && cellBG(s,2,80,palette(s,11)) && !cellBG(s,3,80,palette(s,11));}),@"known global line selectors paint lazy rows without reading additional data");
        [s sendText:@"G"];after=request(s,MIJOE_GUI_STATUS,nil);
        check([after[@"temporaryHighlights"] intValue]==3 && waitFor(^BOOL{return temporary(s,@"ERROR");}),@"lazy-region replacement preserves the open buffer's highlight set");
        check(waitFor(^BOOL{return !cellBG(s,1,80,palette(s,11));}),@"line 1 does not become a region-relative line after a lazy jump");
        [s sendText:@"gg"];request(s,MIJOE_GUI_STATUS,nil);
        check(waitFor(^BOOL{return cellBG(s,1,80,palette(s,11)) && cellBG(s,2,80,palette(s,11));}),@"returning to the beginning restores global line highlights");
        NSString *history=[NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:@".mj/.recent"] encoding:NSUTF8StringEncoding error:NULL];
        check([history containsString:source] && [history containsString:lazy] && ![history containsString:@"ERROR"] && ![history containsString:@"gogogo"] && ![history containsString:@"restart-sentinel"],@"recent-file paths persist without storing temporary highlight words");
        request(s,MIJOE_GUI_DISCARD,nil);check(waitFor(^BOOL{return !s.running;}),@"second owned child exits cleanly");
        NSData *report=[NSJSONSerialization dataWithJSONObject:@{@"passed":@(checks.count),@"checks":checks} options:NSJSONWritingPrettyPrinted error:NULL];[report writeToFile:@(argv[3]) atomically:YES];
    }
    return 0;
}
