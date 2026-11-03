#import <Foundation/Foundation.h>
@interface MJPreferences : NSObject
+ (NSDictionary *)typography;
+ (void)saveFontSize:(CGFloat)size lineSpacing:(CGFloat)spacing;
@end
