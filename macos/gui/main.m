#import "MJEditorView.h"
#include <signal.h>

@class MJAppDelegate;
@interface MJDocumentWindow : NSWindowController <NSWindowDelegate, MJEditorSessionDelegate>
@property(nonatomic, strong) MJEditorSession *session;
@property(nonatomic, strong) MJEditorView *editor;
@property(nonatomic, weak) MJAppDelegate *appOwner;
@property(nonatomic) BOOL allowClose, closing;
@property(nonatomic, copy) void (^closeCompletion)(BOOL);
- (instancetype)initWithFile:(NSString *)file owner:(MJAppDelegate *)owner;
- (void)requestClose:(void (^)(BOOL))completion;
- (void)saveAs:(BOOL)saveAs completion:(void (^)(BOOL))completion;
- (void)perform:(int)operation;
@property(nonatomic) BOOL largePromptVisible;
@property(nonatomic) NSDictionary *previousState;
@end

@interface MJAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSMutableArray<MJDocumentWindow *> *documents;
@property(nonatomic, strong) NSMutableArray<NSString *> *openingFiles;
@property(nonatomic) BOOL launched, terminating;
- (void)openFile:(NSString *)file;
- (void)documentClosed:(MJDocumentWindow *)document;
@end

static void showMessage(NSWindow *window, NSString *title, NSString *message) {
    NSAlert *alert = [NSAlert new];
    alert.messageText = title; alert.informativeText = message ?: @"";
    [alert addButtonWithTitle:@"好"];
    if (window.visible) [alert beginSheetModalForWindow:window completionHandler:nil];
    else [alert runModal];
}

@implementation MJDocumentWindow
- (instancetype)initWithFile:(NSString *)file owner:(MJAppDelegate *)owner {
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1040,740)
                                                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                    backing:NSBackingStoreBuffered defer:NO];
    if (!(self = [super initWithWindow:window])) return nil;
    self.appOwner = owner; window.delegate = self; window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(500,300); window.title = @"MiJoe";
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    [window center];
    self.editor = [[MJEditorView alloc] initWithFrame:window.contentView.bounds];
    CGFloat size = [NSUserDefaults.standardUserDefaults doubleForKey:@"EditorFontSize"];
    if (size) self.editor.fontSize = size;
    if ([NSUserDefaults.standardUserDefaults objectForKey:@"EditorLineSpacing"])
        self.editor.lineSpacing = [NSUserDefaults.standardUserDefaults doubleForKey:@"EditorLineSpacing"];
    [window.contentView addSubview:self.editor];
    NSString *executable = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"editor/bin/joe"];
    NSError *error;
    self.session = [[MJEditorSession alloc] initWithExecutable:executable file:file error:&error];
    if (!self.session) { showMessage(nil,@"无法启动 MiJoe",error.localizedDescription); return nil; }
    self.session.delegate = self; self.editor.session = self.session;
    [window makeFirstResponder:self.editor];
    [window makeKeyAndOrderFront:nil];
    return self;
}
- (void)editorContentChanged:(MJEditorSession *)session { self.editor.needsDisplay = YES; }
- (void)editor:(MJEditorSession *)session damagedRect:(VTermRect)rect { [self.editor invalidateTerminalRect:rect]; }
- (void)editor:(MJEditorSession *)session stateChanged:(NSDictionary *)state {
    NSString *path = state[@"file"];
    NSDictionary *previous=self.previousState;
    if (![path isEqual:previous[@"file"]] || ![state[@"lazy"] isEqual:previous[@"lazy"]]) {
        self.window.title=path.length ? [path.lastPathComponent stringByAppendingString:@" — MiJoe"] : @"未命名 — MiJoe";
        self.window.representedURL=path.length ? [NSURL fileURLWithPath:path] : nil;
        if ([state[@"lazy"] boolValue]) self.window.title=[self.window.title stringByAppendingString:@"（只读·懒加载）"];
    }
    if (![state[@"changed"] isEqual:previous[@"changed"]]) self.window.documentEdited=[state[@"changed"] boolValue];
    if (![state[@"textForeground"] isEqual:previous[@"textForeground"]] ||
        ![state[@"textBackground"] isEqual:previous[@"textBackground"]] ||
        ![state[@"cursorBackground"] isEqual:previous[@"cursorBackground"]]) self.editor.needsDisplay=YES;
    self.previousState=state;
    if ([state[@"largePending"] boolValue] && !self.largePromptVisible && !self.closing) {
        self.largePromptVisible = YES;
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"如何打开大文件？";
        NSString *size = [NSByteCountFormatter stringFromByteCount:[state[@"fileBytes"] longLongValue] countStyle:NSByteCountFormatterCountStyleFile];
        alert.informativeText = [NSString stringWithFormat:@"这是一个 %@%@文件。建议使用只读懒加载，按需读取附近内容；支持按字面文本搜索；跳转行号和搜索可能扫描文件，可按 Esc 取消。正常打开会读取整个文件。",size,[state[@"binary"] boolValue] ? @"二进制" : @"文本"];
        [alert addButtonWithTitle:@"只读懒加载"];
        [alert addButtonWithTitle:@"正常打开"];
        [alert addButtonWithTitle:@"取消"];
        alert.buttons[1].enabled = [state[@"normalAllowed"] boolValue];
        __weak typeof(self) weakSelf = self;
        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
            MJDocumentWindow *document = weakSelf;
            if (!document || !document.session.running) return;
            NSString *choice = response == NSAlertFirstButtonReturn ? @"lazy" : response == NSAlertSecondButtonReturn ? @"normal" : @"cancel";
            [document.session request:MIJOE_GUI_LARGE_CHOICE path:choice completion:^(NSDictionary *result) {
                document.largePromptVisible = NO;
                if (![result[@"ok"] boolValue]) showMessage(document.window,@"文件未打开",result[@"error"]);
            }];
        }];
    }
}
- (void)editor:(MJEditorSession *)session exited:(int)status {
    if (status && !self.closing) showMessage(self.window,@"编辑器已退出",[NSString stringWithFormat:@"退出状态：%d",status]);
    self.allowClose = YES;
    [self.window close];
}
- (void)perform:(int)operation {
    __weak typeof(self) weakSelf = self;
    [self.session request:operation path:nil completion:^(NSDictionary *response) {
        MJDocumentWindow *self = weakSelf;
        if (!self) return;
        if (![response[@"ok"] boolValue]) { showMessage(self.window,@"操作未完成",response[@"error"]); return; }
        NSString *text = response[@"text"];
        if (text) { [NSPasteboard.generalPasteboard clearContents]; [NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString]; }
    }];
}
- (void)saveAs:(BOOL)saveAs completion:(void (^)(BOOL))completion {
    if (self.editor.hasMarkedText) {
        showMessage(self.window,@"请先完成输入",@"请确认或取消输入法中的组合文字，然后保存。");
        if (completion) completion(NO);
        return;
    }
    NSString *current = self.session.state[@"file"];
    __weak typeof(self) weakSelf = self;
    void (^save)(NSString *) = ^(NSString *path) {
        MJDocumentWindow *self = weakSelf;
        if (!self) return;
        [self.session request:path ? MIJOE_GUI_SAVE_AS : MIJOE_GUI_SAVE path:path completion:^(NSDictionary *response) {
            BOOL ok = [response[@"ok"] boolValue];
            if (!ok) showMessage(self.window,@"文件未保存",response[@"error"]);
            if (completion) completion(ok);
        }];
    };
    if (!saveAs && current.length) { save(nil); return; }
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.title = saveAs ? @"另存为" : @"保存文件";
    panel.nameFieldStringValue = current.length ? current.lastPathComponent : @"未命名.txt";
    panel.canCreateDirectories = YES;
    if (current.length) panel.directoryURL = [NSURL fileURLWithPath:current.stringByDeletingLastPathComponent isDirectory:YES];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response == NSModalResponseOK) save(panel.URL.path);
        else if (completion) completion(NO);
    }];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    if (self.allowClose) return YES;
    [self requestClose:nil]; return NO;
}
- (void)finishClose:(BOOL)closed {
    self.closing = NO;
    void (^completion)(BOOL) = self.closeCompletion; self.closeCompletion = nil;
    if (completion) completion(closed);
}
- (void)quitDiscarding:(BOOL)discard {
    __weak typeof(self) weakSelf = self;
    [self.session request:discard ? MIJOE_GUI_DISCARD : MIJOE_GUI_QUIT path:nil completion:^(NSDictionary *response) {
        MJDocumentWindow *self = weakSelf;
        if (!self) return;
        if (![response[@"ok"] boolValue]) { showMessage(self.window,@"窗口未关闭",response[@"error"]); [self finishClose:NO]; return; }
        self.allowClose = YES; [self.window close]; [self finishClose:YES];
    }];
}
- (void)requestClose:(void (^)(BOOL))completion {
    if (self.editor.hasMarkedText) {
        showMessage(self.window,@"请先完成输入",@"请确认或取消输入法中的组合文字，然后关闭窗口。");
        if (completion) completion(NO);
        return;
    }
    if (self.closing) { if (completion) completion(NO); return; }
    if (!self.session.running) { self.allowClose = YES; [self.window close]; if(completion)completion(YES); return; }
    self.closing = YES; self.closeCompletion = completion;
    __weak typeof(self) weakSelf = self;
    [self.session request:MIJOE_GUI_STATUS path:nil completion:^(NSDictionary *state) {
        MJDocumentWindow *self = weakSelf;
        if (!self) return;
        if (![state[@"ok"] boolValue]) { [self finishClose:NO]; return; }
        NSInteger dirty = [state[@"dirtyBuffers"] integerValue];
        if (!dirty) { [self quitDiscarding:NO]; return; }
        NSAlert *alert = [NSAlert new];
        alert.messageText = @"要保存更改吗？";
        alert.informativeText = [NSString stringWithFormat:@"此窗口有 %ld 个未保存的缓冲区。保存会写入当前缓冲区；放弃更改会关闭此窗口并丢弃其中所有未保存的修改。",(long)dirty];
        [alert addButtonWithTitle:@"保存"]; [alert addButtonWithTitle:@"放弃更改"]; [alert addButtonWithTitle:@"取消"];
        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
            if (response == NSAlertSecondButtonReturn) [self quitDiscarding:YES];
            else if (response == NSAlertFirstButtonReturn) [self saveAs:NO completion:^(BOOL ok) {
                if (!ok) { [self finishClose:NO]; return; }
                if ([self.session.state[@"dirtyBuffers"] integerValue]) {
                    showMessage(self.window,@"还有未保存的缓冲区",@"当前缓冲区已保存。请用“缓冲区”菜单切换并保存其余缓冲区，然后关闭窗口。");
                    [self finishClose:NO];
                } else [self quitDiscarding:NO];
            }];
            else [self finishClose:NO];
        }];
    }];
}
- (void)windowWillClose:(NSNotification *)notification { [self.appOwner documentClosed:self]; }
@end

@implementation MJAppDelegate
- (instancetype)init { if ((self=[super init])) { _documents=[NSMutableArray array]; _openingFiles=[NSMutableArray array]; } return self; }
- (MJDocumentWindow *)currentDocument {
    for (MJDocumentWindow *document in self.documents) if (document.window == NSApp.keyWindow || document.window == NSApp.mainWindow) return document;
    return self.documents.lastObject;
}
- (NSMenuItem *)item:(NSString *)title action:(SEL)action key:(NSString *)key menu:(NSMenu *)menu {
    NSMenuItem *item = [menu addItemWithTitle:title action:action keyEquivalent:key];
    item.target = self; return item;
}
- (void)applicationWillFinishLaunching:(NSNotification *)notification {
    NSMenu *bar = [NSMenu new]; NSApp.mainMenu = bar;
    NSMenu *application = [NSMenu new];
    NSMenuItem *appItem = [bar addItemWithTitle:@"MiJoe" action:nil keyEquivalent:@""]; appItem.submenu = application;
    [self item:@"关于 MiJoe" action:@selector(about:) key:@"" menu:application];
    [application addItem:NSMenuItem.separatorItem];
    NSMenuItem *hide = [application addItemWithTitle:@"隐藏 MiJoe" action:@selector(hide:) keyEquivalent:@"h"]; hide.target=NSApp;
    NSMenuItem *hideOthers = [application addItemWithTitle:@"隐藏其他" action:@selector(hideOtherApplications:) keyEquivalent:@"h"]; hideOthers.target=NSApp; hideOthers.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagOption;
    [application addItem:NSMenuItem.separatorItem];
    NSMenuItem *quit = [application addItemWithTitle:@"退出 MiJoe" action:@selector(terminate:) keyEquivalent:@"q"]; quit.target=NSApp;
    NSMenu *file = [NSMenu new]; [bar addItemWithTitle:@"文件" action:nil keyEquivalent:@""].submenu=file;
    [self item:@"新建窗口" action:@selector(newDocument:) key:@"n" menu:file];
    [self item:@"打开…" action:@selector(openDocument:) key:@"o" menu:file];
    [file addItem:NSMenuItem.separatorItem];
    [self item:@"保存" action:@selector(saveDocument:) key:@"s" menu:file];
    NSMenuItem *saveAs = [self item:@"另存为…" action:@selector(saveDocumentAs:) key:@"s" menu:file]; saveAs.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;
    [file addItem:NSMenuItem.separatorItem];
    [self item:@"关闭窗口" action:@selector(closeDocument:) key:@"w" menu:file];
    NSMenu *edit = [NSMenu new]; [bar addItemWithTitle:@"编辑" action:nil keyEquivalent:@""].submenu=edit;
    [self item:@"撤销" action:@selector(undo:) key:@"z" menu:edit];
    NSMenuItem *redo = [self item:@"重做" action:@selector(redo:) key:@"z" menu:edit]; redo.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;
    [edit addItem:NSMenuItem.separatorItem];
    [self item:@"剪切" action:@selector(cut:) key:@"x" menu:edit];
    [self item:@"复制" action:@selector(copy:) key:@"c" menu:edit];
    [self item:@"粘贴" action:@selector(paste:) key:@"v" menu:edit];
    [self item:@"全选" action:@selector(selectAll:) key:@"a" menu:edit];
    for (NSMenuItem *item in edit.itemArray) if (!item.separatorItem) item.target=nil;
    NSMenu *view = [NSMenu new]; [bar addItemWithTitle:@"显示" action:nil keyEquivalent:@""].submenu=view;
    [self item:@"放大字体" action:@selector(zoomIn:) key:@"+" menu:view];
    [self item:@"缩小字体" action:@selector(zoomOut:) key:@"-" menu:view];
    [self item:@"默认字体大小" action:@selector(resetZoom:) key:@"0" menu:view];
    [view addItem:NSMenuItem.separatorItem];
    [self item:@"主题与选项…" action:@selector(editorOptions:) key:@"" menu:view];
    NSMenu *buffers = [NSMenu new]; [bar addItemWithTitle:@"缓冲区" action:nil keyEquivalent:@""].submenu=buffers;
    [self item:@"下一个缓冲区" action:@selector(nextBuffer:) key:@"" menu:buffers];
    [self item:@"上一个缓冲区" action:@selector(previousBuffer:) key:@"" menu:buffers];
    NSMenu *windows = [NSMenu new]; [bar addItemWithTitle:@"窗口" action:nil keyEquivalent:@""].submenu=windows; NSApp.windowsMenu=windows;
    NSMenuItem *minimize=[windows addItemWithTitle:@"最小化" action:@selector(performMiniaturize:) keyEquivalent:@"m"]; minimize.target=nil;
    NSMenuItem *full=[windows addItemWithTitle:@"进入全屏" action:@selector(toggleFullScreen:) keyEquivalent:@"f"]; full.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagControl; full.target=nil;
    NSMenu *help = [NSMenu new]; [bar addItemWithTitle:@"帮助" action:nil keyEquivalent:@""].submenu=help; NSApp.helpMenu=help;
    [self item:@"支持的编辑命令" action:@selector(editorHelp:) key:@"?" menu:help];
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    self.launched = YES;
    for (NSString *file in self.openingFiles) [self openFile:file];
    [self.openingFiles removeAllObjects];
    if (!self.documents.count) [self openFile:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
- (void)application:(NSApplication *)application openFiles:(NSArray<NSString *> *)filenames {
    if (!self.launched) [self.openingFiles addObjectsFromArray:filenames];
    else for (NSString *file in filenames) [self openFile:file];
    [application replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
    if (!visible) [self openFile:nil]; return YES;
}
- (void)openFile:(NSString *)file {
    if (file.length) {
        if (!file.isAbsolutePath) file = [NSFileManager.defaultManager.currentDirectoryPath stringByAppendingPathComponent:file];
        file = file.stringByStandardizingPath;
    }
    MJDocumentWindow *document = [[MJDocumentWindow alloc] initWithFile:file owner:self];
    if (document) [self.documents addObject:document];
}
- (void)documentClosed:(MJDocumentWindow *)document { [self.documents removeObject:document]; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return NO; }
- (void)closeForTermination {
    MJDocumentWindow *document = self.documents.lastObject;
    if (!document) { self.terminating=NO; [NSApp replyToApplicationShouldTerminate:YES]; return; }
    [document requestClose:^(BOOL closed) {
        if (closed) [self closeForTermination];
        else { self.terminating=NO; [NSApp replyToApplicationShouldTerminate:NO]; }
    }];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    if (!self.documents.count) return NSTerminateNow;
    if (self.terminating) return NSTerminateCancel;
    self.terminating=YES;
    dispatch_async(dispatch_get_main_queue(), ^{ [self closeForTermination]; });
    return NSTerminateLater;
}
- (void)newDocument:(id)sender { [self openFile:nil]; }
- (void)openDocument:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel]; panel.allowsMultipleSelection=YES; panel.canChooseDirectories=NO;
    void (^finished)(NSModalResponse)=^(NSModalResponse response) { if (response==NSModalResponseOK) for (NSURL *url in panel.URLs) [self openFile:url.path]; };
    if (NSApp.keyWindow) [panel beginSheetModalForWindow:NSApp.keyWindow completionHandler:finished];
    else [panel beginWithCompletionHandler:finished];
}
- (void)saveDocument:(id)sender { [[self currentDocument] saveAs:NO completion:nil]; }
- (void)saveDocumentAs:(id)sender { [[self currentDocument] saveAs:YES completion:nil]; }
- (void)closeDocument:(id)sender { [[self currentDocument] requestClose:nil]; }
- (void)undo:(id)sender { [[self currentDocument] perform:MIJOE_GUI_UNDO]; }
- (void)redo:(id)sender { [[self currentDocument] perform:MIJOE_GUI_REDO]; }
- (void)cut:(id)sender { [[self currentDocument] perform:MIJOE_GUI_CUT]; }
- (void)copy:(id)sender { [[self currentDocument] perform:MIJOE_GUI_COPY]; }
- (void)paste:(id)sender { [[[self currentDocument] editor] paste:sender]; }
- (void)selectAll:(id)sender { [[self currentDocument] perform:MIJOE_GUI_SELECT_ALL]; }
- (void)nextBuffer:(id)sender { [[self currentDocument] perform:MIJOE_GUI_NEXT_BUFFER]; }
- (void)previousBuffer:(id)sender { [[self currentDocument] perform:MIJOE_GUI_PREV_BUFFER]; }
- (void)editorHelp:(id)sender { [[self currentDocument] perform:MIJOE_GUI_HELP]; }
- (void)editorOptions:(id)sender { uint8_t key=20; [[[self currentDocument] session] sendData:[NSData dataWithBytes:&key length:1]]; }
- (void)setFontSize:(CGFloat)size { [self currentDocument].editor.fontSize=size; [NSUserDefaults.standardUserDefaults setDouble:[self currentDocument].editor.fontSize forKey:@"EditorFontSize"]; }
- (void)zoomIn:(id)sender { [self setFontSize:[self currentDocument].editor.fontSize+1]; }
- (void)zoomOut:(id)sender { [self setFontSize:[self currentDocument].editor.fontSize-1]; }
- (void)resetZoom:(id)sender { [self setFontSize:13]; }
- (void)about:(id)sender { [NSApp orderFrontStandardAboutPanelWithOptions:@{NSAboutPanelOptionApplicationName:@"MiJoe",NSAboutPanelOptionApplicationVersion:@"1.0",NSAboutPanelOptionCredits:[[NSAttributedString alloc] initWithString:@"JOE editing engine · Native AppKit frontend\nlibvterm © Paul Evans (MIT)"]}]; }
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    NSArray *global=@[NSStringFromSelector(@selector(newDocument:)),NSStringFromSelector(@selector(openDocument:)),NSStringFromSelector(@selector(about:))];
    MJDocumentWindow *current = [self currentDocument];
    return [global containsObject:NSStringFromSelector(item.action)] || (current.session.running && !current.window.attachedSheet);
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        signal(SIGPIPE,SIG_IGN);
        NSApplication *application=[NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        MJAppDelegate *delegate=[MJAppDelegate new]; application.delegate=delegate;
        for (int n=1;n<argc;++n) {
            NSString *argument=[NSString stringWithUTF8String:argv[n]];
            if (![argument hasPrefix:@"-"]) [delegate.openingFiles addObject:argument];
        }
        [application run];
    }
    return 0;
}
