#import "MJPreferences.h"
@implementation MJPreferences
+ (NSString *)path {
    NSDictionary *env=NSProcessInfo.processInfo.environment;
    NSString *root=env[@"MIJOE_CONFIG_HOME"];
    if(!root.length)root=[(env[@"HOME"] ?: NSHomeDirectory()) stringByAppendingPathComponent:@".mj"];
    return [root stringByAppendingPathComponent:@"macos.json"];
}
+ (NSDictionary *)typography {
    NSData *data=[NSData dataWithContentsOfFile:self.path];
    id json=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    if([json isKindOfClass:NSDictionary.class]) {
        NSMutableDictionary *values=[json mutableCopy];
        id size=values[@"EditorFontSize"],spacing=values[@"EditorLineSpacing"];
        if(![size isKindOfClass:NSNumber.class] || !([size doubleValue]>=9 && [size doubleValue]<=32))values[@"EditorFontSize"]=@13;
        if(![spacing isKindOfClass:NSNumber.class] || !([spacing doubleValue]>=-64 && [spacing doubleValue]<=32))values[@"EditorLineSpacing"]=@(-6);
        return values;
    }
    NSMutableDictionary *legacy=[@{@"EditorFontSize":@13,@"EditorLineSpacing":@(-6)} mutableCopy];
    /* Read the previous app preferences once; all subsequent writes use ~/.mj. */
    for(NSString *key in @[@"EditorFontSize",@"EditorLineSpacing"]) {
        id value=[NSUserDefaults.standardUserDefaults objectForKey:key];
        if([value isKindOfClass:NSNumber.class])legacy[key]=value;
    }
    if(!data)[self write:legacy];return legacy;
}
+ (void)write:(NSDictionary *)values {
    NSString *path=self.path;
    [NSFileManager.defaultManager createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:NULL];
    NSData *data=[NSJSONSerialization dataWithJSONObject:values options:NSJSONWritingPrettyPrinted error:NULL];
    [data writeToFile:path options:NSDataWritingAtomic error:NULL];
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:path error:NULL];
}
+ (void)saveFontSize:(CGFloat)size lineSpacing:(CGFloat)spacing {
    NSMutableDictionary *values=[[self typography] mutableCopy];
    values[@"EditorFontSize"]=@(size);values[@"EditorLineSpacing"]=@(spacing);
    [self write:values];
}
@end
