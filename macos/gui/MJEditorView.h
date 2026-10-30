#import "MJEditorSession.h"

@interface MJEditorView : NSView <NSTextInputClient>
@property(nonatomic, strong) MJEditorSession *session;
@property(nonatomic) CGFloat fontSize;
@property(nonatomic) CGFloat lineSpacing;
- (void)resizeEditor;
- (void)invalidateTerminalRect:(VTermRect)rect;
- (void)paste:(id)sender;
- (void)insertText:(id)value;
@end
