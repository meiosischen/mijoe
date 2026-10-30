#import "MJEditorSession.h"
#include <util.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>

@interface MJEditorSession () {
@public
    VTerm *_terminal;
    VTermScreen *_screen;
    VTermPos _cursor;
    BOOL _cursorVisible, _running;
    int _master, _requests, _responses;
    pid_t _processID;
    dispatch_source_t _readSource, _writeSource, _responseSource, _processSource;
    NSMutableData *_input, *_responseBuffer;
    NSMutableDictionary<NSNumber *, void (^)(NSDictionary *)> *_pending;
    uint32_t _nextID;
    int _rows, _columns;
    NSString *_typography;
    NSMutableData *_outputBuffer, *_frameBuffer;
    BOOL _inFrame, _publishedVisible, _batchingInput;
    VTermPos _publishedCursor;
    BOOL _dirtyRows[500];
    int _dirtyStart[500], _dirtyEnd[500];
    NSUInteger _damageCount;

}
@property(nonatomic, readwrite) NSDictionary *state;
- (void)flushInput;
- (void)scheduleInput;
- (void)finishEditorOutput;
- (void)readEditor;
- (void)readResponses;
- (void)addDamage:(VTermRect)rect;
- (void)publishDamage;
- (void)consumeOutput:(const char *)bytes length:(NSUInteger)length;

@end

static int damaged(VTermRect rect, void *user) {
    MJEditorSession *s = (__bridge MJEditorSession *)user;
    [s addDamage:rect];
    return 1;
}
static int movedCursor(VTermPos position, VTermPos previous, int visible, void *user) {
    MJEditorSession *s = (__bridge MJEditorSession *)user;
    s->_cursor = position; s->_cursorVisible = visible;
    return 1;
}
static int setProperty(VTermProp property, VTermValue *value, void *user) {
    MJEditorSession *s = (__bridge MJEditorSession *)user;
    if (property == VTERM_PROP_CURSORVISIBLE) s->_cursorVisible = value->boolean;
    if (property==VTERM_PROP_REVERSE || property==VTERM_PROP_ALTSCREEN)
        [s addDamage:(VTermRect){0,s->_rows,0,s->_columns}];
    return 1;
}
static void outputBytes(const char *bytes, size_t length, void *user) {
    [(__bridge MJEditorSession *)user sendData:[NSData dataWithBytes:bytes length:length]];
}
static const VTermScreenCallbacks callbacks = {
    .damage = damaged, .movecursor = movedCursor, .settermprop = setProperty
};

@implementation MJEditorSession
- (instancetype)initWithExecutable:(NSString *)executable file:(NSString *)path error:(NSError **)error {
    if (!(self = [super init])) return nil;
    _master = _requests = _responses = -1;
    _rows = 36; _columns = 112;
    _input = [NSMutableData data]; _responseBuffer = [NSMutableData data];
    _outputBuffer=[NSMutableData data]; _frameBuffer=[NSMutableData data];
    _pending = [NSMutableDictionary dictionary];
    _state = @{ @"file": path ?: @"", @"changed": @NO, @"dirtyBuffers": @0, @"mode": @"N" };
    _terminal = vterm_new(_rows, _columns);
    if (!_terminal) return nil;
    vterm_set_utf8(_terminal, 1);
    vterm_output_set_callback(_terminal, outputBytes, (__bridge void *)self);
    _screen = vterm_obtain_screen(_terminal);
    vterm_screen_enable_altscreen(_screen, 1);
    vterm_screen_set_callbacks(_screen, &callbacks, (__bridge void *)self);
    vterm_screen_set_damage_merge(_screen, VTERM_DAMAGE_CELL);
    VTermColor fg, bg;
    vterm_color_rgb(&fg, 175, 175, 175); vterm_color_rgb(&bg, 0, 0, 0);
    vterm_state_set_default_colors(vterm_obtain_state(_terminal), &fg, &bg);
    vterm_screen_reset(_screen, 1);

    int requestPipe[2] = {-1,-1}, responsePipe[2] = {-1,-1};
    if (pipe(requestPipe) || pipe(responsePipe)) {
        int failure = errno;
        for (int n = 0; n < 2; ++n) { if (requestPipe[n] >= 0) close(requestPipe[n]); if (responsePipe[n] >= 0) close(responsePipe[n]); }
        if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:failure userInfo:nil];
        return nil;
    }
    for (int n = 0; n < 2; ++n) { fcntl(requestPipe[n], F_SETFD, FD_CLOEXEC); fcntl(responsePipe[n], F_SETFD, FD_CLOEXEC); }
    NSArray *arguments = @[executable, @"-nonotice", @"--joe_state", @"-mouse", path ?: @""];
    NSMutableDictionary *environment = [[[NSProcessInfo processInfo] environment] mutableCopy];
    environment[@"TERM"] = @"xterm-256color";
    environment[@"COLORTERM"] = @"truecolor";
    environment[@"LANG"] = @"en_US.UTF-8";
    environment[@"LC_ALL"] = @"en_US.UTF-8";
    environment[@"MIJOE_GUI_REQUEST_FD"] = @(requestPipe[0]).stringValue;
    environment[@"MIJOE_GUI_RESPONSE_FD"] = @(responsePipe[1]).stringValue;
    char **argv = calloc(arguments.count + 1, sizeof(char *));
    char **envp = calloc(environment.count + 1, sizeof(char *));
    NSUInteger n = 0;
    for (NSString *argument in arguments) argv[n++] = strdup(argument.UTF8String);
    n = 0;
    for (NSString *key in environment) envp[n++] = strdup([[NSString stringWithFormat:@"%@=%@", key, environment[key]] UTF8String]);
    char *directory = strdup((path.length ? path.stringByDeletingLastPathComponent : NSHomeDirectory()).fileSystemRepresentation);
    struct winsize size = { (unsigned short)_rows, (unsigned short)_columns, 0, 0 };
    _processID = forkpty(&_master, NULL, NULL, &size);
    if (_processID == 0) {
        /* AppKit is multithreaded: use only async-signal-safe calls before exec. */
        close(requestPipe[1]); close(responsePipe[0]);
        fcntl(requestPipe[0], F_SETFD, 0); fcntl(responsePipe[1], F_SETFD, 0);
        chdir(directory);
        execve(argv[0], argv, envp);
        _exit(127);
    }
    int failure = errno;
    for (n = 0; argv[n]; ++n) free(argv[n]); free(argv);
    for (n = 0; envp[n]; ++n) free(envp[n]); free(envp); free(directory);
    close(requestPipe[0]); close(responsePipe[1]);
    if (_processID < 0) {
        close(requestPipe[1]); close(responsePipe[0]);
        if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:failure userInfo:nil];
        return nil;
    }
    _requests = requestPipe[1]; _responses = responsePipe[0]; _running = YES;
    fcntl(_master, F_SETFL, O_NONBLOCK); fcntl(_master, F_SETFD, FD_CLOEXEC);
    fcntl(_responses, F_SETFL, O_NONBLOCK);
    __weak typeof(self) weakSelf = self;
    _readSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, _master, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_readSource, ^{ [weakSelf readEditor]; });
    dispatch_resume(_readSource);
    _responseSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, _responses, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_responseSource, ^{ [weakSelf readResponses]; });
    dispatch_resume(_responseSource);
    _processSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, _processID, DISPATCH_PROC_EXIT, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_processSource, ^{
        /* Keep the session alive until its own child has been reaped. */
        MJEditorSession *s = self;
        [s readEditor]; [s finishEditorOutput]; [s readResponses];
        int status = 0; waitpid(s->_processID, &status, 0); s->_running = NO;
        if (s->_readSource) { dispatch_source_cancel(s->_readSource); s->_readSource = nil; }
        if (s->_responseSource) { dispatch_source_cancel(s->_responseSource); s->_responseSource = nil; }
        if (s->_writeSource) { dispatch_source_cancel(s->_writeSource); s->_writeSource = nil; }
        dispatch_source_cancel(s->_processSource); s->_processSource = nil;
        for (void (^completion)(NSDictionary *) in s->_pending.allValues)
            completion(@{ @"ok": @NO, @"error": @"编辑器已退出。" });
        [s->_pending removeAllObjects];
        [s.delegate editor:s exited:WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)];
    });
    dispatch_resume(_processSource);
    return self;
}
- (void)dealloc {
    if (_readSource) dispatch_source_cancel(_readSource);
    if (_responseSource) dispatch_source_cancel(_responseSource);
    if (_writeSource) dispatch_source_cancel(_writeSource);
    if (_processSource) dispatch_source_cancel(_processSource);
    if (_master >= 0) close(_master);
    if (_requests >= 0) close(_requests);
    if (_responses >= 0) close(_responses);
    if (_terminal) vterm_free(_terminal);
}
- (VTerm *)terminal { return _terminal; }
- (VTermScreen *)screen { return _screen; }
- (VTermPos)cursor { return _cursor; }
- (BOOL)cursorVisible { return _cursorVisible; }
- (BOOL)running { return _running; }
- (pid_t)processID { return _processID; }
- (NSColor *)themeColor:(NSString *)key fallback:(int)fallback {
    NSNumber *value = self.state[key];
    int rgb = value && value.intValue >= 0 ? value.intValue : fallback;
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0
                             green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1];
}
- (NSColor *)textForeground { return [self themeColor:@"textForeground" fallback:0xafafaf]; }
- (NSColor *)textBackground { return [self themeColor:@"textBackground" fallback:0x000000]; }
- (NSColor *)cursorColor { return [self themeColor:@"cursorBackground" fallback:0xbebebe]; }
- (void)addDamage:(VTermRect)rect {
    rect.start_row=MAX(0,rect.start_row); rect.end_row=MIN(_rows,rect.end_row);
    rect.start_col=MAX(0,rect.start_col); rect.end_col=MIN(_columns,rect.end_col);
    if (rect.start_row>=rect.end_row || rect.start_col>=rect.end_col) return;
    for (int row=rect.start_row;row<rect.end_row;row++) {
        if (!_dirtyRows[row]) { _dirtyRows[row]=YES;_dirtyStart[row]=rect.start_col;_dirtyEnd[row]=rect.end_col;_damageCount++; }
        else { _dirtyStart[row]=MIN(_dirtyStart[row],rect.start_col);_dirtyEnd[row]=MAX(_dirtyEnd[row],rect.end_col); }
    }
}
- (void)publishDamage {
    if (_publishedVisible!=_cursorVisible || _publishedCursor.row!=_cursor.row || _publishedCursor.col!=_cursor.col) {
        if (_publishedVisible) [self addDamage:(VTermRect){_publishedCursor.row,_publishedCursor.row+1,_publishedCursor.col,_publishedCursor.col+2}];
        if (_cursorVisible) [self addDamage:(VTermRect){_cursor.row,_cursor.row+1,_cursor.col,_cursor.col+2}];
    }
    _publishedCursor=_cursor; _publishedVisible=_cursorVisible;
    if (_damageCount) {
        if ([self.delegate respondsToSelector:@selector(editor:damagedRect:)]) {
            for (int row=0;row<_rows;) {
                if (!_dirtyRows[row]) { row++;continue; }
                int end=row+1;
                while (end<_rows && _dirtyRows[end] && _dirtyStart[row]==_dirtyStart[end] && _dirtyEnd[row]==_dirtyEnd[end]) end++;
                [self.delegate editor:self damagedRect:(VTermRect){row,end,_dirtyStart[row],_dirtyEnd[row]}];row=end;
            }
        } else [self.delegate editorContentChanged:self];
    }
    _damageCount=0; memset(_dirtyRows,0,sizeof(_dirtyRows));
}
- (void)consumeOutput:(const char *)bytes length:(NSUInteger)length {
    static const char beginBytes[]="\033[?2026h", endBytes[]="\033[?2026l";
    NSData *begin=[NSData dataWithBytes:beginBytes length:8],*end=[NSData dataWithBytes:endBytes length:8];
    [_outputBuffer appendBytes:bytes length:length];
    while (_outputBuffer.length) {
        NSRange full=NSMakeRange(0,_outputBuffer.length);
        NSRange a=[_outputBuffer rangeOfData:begin options:0 range:full],b=[_outputBuffer rangeOfData:end options:0 range:full];
        NSRange marker=a.location<b.location ? a : b;
        BOOL starting=a.location!=NSNotFound && a.location==marker.location;
        NSUInteger count=marker.location;
        if (count==NSNotFound) {
            count=_outputBuffer.length;
            const char *data=_outputBuffer.bytes;
            for (NSUInteger tail=MIN((NSUInteger)7,count);tail>0;--tail) {
                if (!memcmp(data+count-tail,beginBytes,tail)) { count-=tail; break; }
            }
        }
        if (count) {
            if (_inFrame) [_frameBuffer appendBytes:_outputBuffer.bytes length:count];
            else vterm_input_write(_terminal,_outputBuffer.bytes,count);
        }
        if (marker.location==NSNotFound) {
            [_outputBuffer replaceBytesInRange:NSMakeRange(0,count) withBytes:NULL length:0];
            break;
        }
        [_outputBuffer replaceBytesInRange:NSMakeRange(0,count+8) withBytes:NULL length:0];
        if (starting) _inFrame=YES;
        else if (_inFrame) {
            vterm_input_write(_terminal,_frameBuffer.bytes,_frameBuffer.length);
            [_frameBuffer setLength:0]; _inFrame=NO;
        }
    }
    /* Bound memory even if a damaged child never sends its closing marker. */
    if (_frameBuffer.length>64*1024*1024) {
        vterm_input_write(_terminal,_frameBuffer.bytes,_frameBuffer.length);[_frameBuffer setLength:0];_inFrame=NO;
    }
}
- (void)readEditor {
    char bytes[32768]; ssize_t count; NSUInteger total=0;
    /* Yield back to AppKit under sustained output instead of starving input. */
    while (total<1024*1024 && (count=read(_master,bytes,sizeof(bytes)))>0) {
        total+=(NSUInteger)count; [self consumeOutput:bytes length:(NSUInteger)count];
    }
    vterm_screen_flush_damage(_screen); [self publishDamage];
}
- (void)finishEditorOutput {
    char bytes[32768];ssize_t count;
    while ((count=read(_master,bytes,sizeof(bytes)))>0) [self consumeOutput:bytes length:(NSUInteger)count];
    if (_inFrame) { [_frameBuffer appendData:_outputBuffer]; vterm_input_write(_terminal,_frameBuffer.bytes,_frameBuffer.length); }
    else if (_outputBuffer.length) vterm_input_write(_terminal,_outputBuffer.bytes,_outputBuffer.length);
    [_frameBuffer setLength:0];[_outputBuffer setLength:0];_inFrame=NO;
    vterm_screen_flush_damage(_screen);[self publishDamage];
}
- (void)readResponses {
    char bytes[16384]; ssize_t count;
    while ((count = read(_responses, bytes, sizeof(bytes))) > 0) [_responseBuffer appendBytes:bytes length:count];
    for (;;) {
        NSRange newline = [_responseBuffer rangeOfData:[NSData dataWithBytes:"\n" length:1] options:0 range:NSMakeRange(0, _responseBuffer.length)];
        if (newline.location == NSNotFound) break;
        NSData *line = [_responseBuffer subdataWithRange:NSMakeRange(0, newline.location)];
        [_responseBuffer replaceBytesInRange:NSMakeRange(0, newline.location + 1) withBytes:NULL length:0];
        NSDictionary *message = [NSJSONSerialization JSONObjectWithData:line options:0 error:NULL];
        if (![message isKindOfClass:NSDictionary.class]) continue;
        self.state = message;
        [self.delegate editor:self stateChanged:message];
        NSNumber *identifier = message[@"id"];
        void (^completion)(NSDictionary *) = _pending[identifier];
        if (completion) { [_pending removeObjectForKey:identifier]; completion(message); }
    }
}
- (void)flushInput {
    while (_input.length && _running) {
        ssize_t count = write(_master, _input.bytes, _input.length);
        if (count > 0) [_input replaceBytesInRange:NSMakeRange(0, count) withBytes:NULL length:0];
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    if (!_input.length && _writeSource) { dispatch_source_cancel(_writeSource); _writeSource = nil; }
}
- (void)scheduleInput {
    [self flushInput];
    if (_input.length && !_writeSource) {
        __weak typeof(self) weakSelf=self;
        _writeSource=dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,_master,0,dispatch_get_main_queue());
        dispatch_source_set_event_handler(_writeSource,^{ [weakSelf flushInput]; });dispatch_resume(_writeSource);
    }
}
- (void)sendData:(NSData *)data {
    if (!_running || !data.length) return;
    [_input appendData:data];if (!_batchingInput) [self scheduleInput];
}
- (void)sendText:(NSString *)text {
    BOOL nested=_batchingInput;_batchingInput=YES;
    NSData *utf32=[text dataUsingEncoding:NSUTF32LittleEndianStringEncoding];const uint32_t *characters=utf32.bytes;
    for (NSUInteger n=0;n<utf32.length/4;++n) vterm_keyboard_unichar(_terminal,characters[n],VTERM_MOD_NONE);
    _batchingInput=nested;if (!nested) [self scheduleInput];
}
- (void)pasteText:(NSString *)text {
    BOOL nested=_batchingInput;_batchingInput=YES;
    vterm_keyboard_start_paste(_terminal);[self sendText:text];vterm_keyboard_end_paste(_terminal);
    _batchingInput=nested;if (!nested) [self scheduleInput];
}
- (void)sendKey:(VTermKey)key modifiers:(VTermModifier)modifiers {
    BOOL nested=_batchingInput;_batchingInput=YES;vterm_keyboard_key(_terminal,key,modifiers);
    _batchingInput=nested;if (!nested) [self scheduleInput];
}
- (void)setFontSize:(CGFloat)size lineSpacing:(CGFloat)spacing {
    NSString *value = [NSString stringWithFormat:@"%.3g %.3g", size, spacing];
    if (!_running || [_typography isEqual:value]) return;
    _typography = value;
    [self request:MIJOE_GUI_TYPOGRAPHY path:value completion:nil];
}
- (void)resizeRows:(int)rows columns:(int)columns {
    rows = MAX(6, MIN(rows, 500)); columns = MAX(20, MIN(columns, 1000));
    if (rows == _rows && columns == _columns) return;
    _damageCount=0;memset(_dirtyRows,0,sizeof(_dirtyRows));
    _rows = rows; _columns = columns;
    vterm_set_size(_terminal, rows, columns);
    struct winsize size = { (unsigned short)rows, (unsigned short)columns, 0, 0 };
    ioctl(_master, TIOCSWINSZ, &size);
    if (_running) kill(_processID, SIGWINCH);
    vterm_screen_flush_damage(_screen); [self publishDamage];
}
- (void)request:(int)operation path:(NSString *)path completion:(void (^)(NSDictionary *))completion {
    NSData *name = [(path ?: @"") dataUsingEncoding:NSUTF8StringEncoding];
    if (!_running || name.length >= 1000) {
        if (completion) completion(@{ @"ok": @NO, @"error": _running ? @"文件路径过长。" : @"编辑器已退出。" });
        return;
    }
    BOOL quiet=!completion && (operation==MIJOE_GUI_SCROLL || operation==MIJOE_GUI_MOUSE || operation==MIJOE_GUI_TYPOGRAPHY);
    NSNumber *identifier = quiet ? @0 : @(++_nextID);
    if (completion) _pending[identifier] = [completion copy];
    uint32_t header[] = {htonl((uint32_t)name.length), htonl(identifier.unsignedIntValue), htonl(operation)};
    NSMutableData *packet = [NSMutableData dataWithBytes:header length:sizeof(header)]; [packet appendData:name];
    if (write(_requests, packet.bytes, packet.length) != (ssize_t)packet.length) {
        [_pending removeObjectForKey:identifier];
        if (completion) completion(@{ @"ok": @NO, @"error": @"无法向编辑器发送操作。" });
        return;
    }
    [self sendText:[NSString stringWithFormat:@"%C%C", (unichar)0xdbff, (unichar)0xdffd]];
    if (!completion) return;
    __weak typeof(self) weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        MJEditorSession *s = weakSelf;
        if (!s) return;
        void (^pending)(NSDictionary *) = s->_pending[identifier];
        if (pending) { [s->_pending removeObjectForKey:identifier]; pending(@{ @"ok": @NO, @"error": @"编辑器未响应，请先结束粘贴或关闭编辑器中的菜单。" }); }
    });
}
- (NSString *)screenText {
    size_t capacity = (size_t)_rows * _columns * 24 + 1;
    char *text = calloc(capacity, 1);
    size_t length = vterm_screen_get_text(_screen, text, capacity - 1, (VTermRect){0, _rows, 0, _columns});
    NSString *result = [[NSString alloc] initWithBytes:text length:length encoding:NSUTF8StringEncoding];
    free(text); return result ?: @"";
}
@end
