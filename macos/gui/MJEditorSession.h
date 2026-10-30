#import <AppKit/AppKit.h>
#include "vterm.h"
#include "../gui_protocol.h"

@class MJEditorSession;
@protocol MJEditorSessionDelegate <NSObject>
- (void)editorContentChanged:(MJEditorSession *)session;
- (void)editor:(MJEditorSession *)session stateChanged:(NSDictionary *)state;
- (void)editor:(MJEditorSession *)session exited:(int)status;
@optional
- (void)editor:(MJEditorSession *)session damagedRect:(VTermRect)rect;
@end

@interface MJEditorSession : NSObject
@property(nonatomic, weak) id<MJEditorSessionDelegate> delegate;
@property(nonatomic, readonly) VTerm *terminal;
@property(nonatomic, readonly) VTermScreen *screen;
@property(nonatomic, readonly) NSDictionary *state;
@property(nonatomic, readonly) pid_t processID;
@property(nonatomic, readonly) BOOL running;
@property(nonatomic, readonly) BOOL cursorVisible;
@property(nonatomic, readonly) VTermPos cursor;
@property(nonatomic, readonly) NSColor *textForeground;
@property(nonatomic, readonly) NSColor *textBackground;
@property(nonatomic, readonly) NSColor *cursorColor;
- (instancetype)initWithExecutable:(NSString *)executable file:(NSString *)path error:(NSError **)error;
- (void)setFontSize:(CGFloat)size lineSpacing:(CGFloat)spacing;
- (void)resizeRows:(int)rows columns:(int)columns;
- (void)sendData:(NSData *)data;
- (void)sendText:(NSString *)text;
- (void)pasteText:(NSString *)text;
- (void)sendKey:(VTermKey)key modifiers:(VTermModifier)modifiers;
- (void)request:(int)operation path:(NSString *)path completion:(void (^)(NSDictionary *))completion;
- (NSString *)screenText;
@end
