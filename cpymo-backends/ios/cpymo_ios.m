#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

const char* get_ios_directory() {
    NSString *homeDir = NSHomeDirectory();
    NSLog(@"homeDir=%@", homeDir);
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *docDir = [paths objectAtIndex:0];
    return [docDir UTF8String];
}

void cpymo_ios_accessibility_announce(const char *text) {
    if (text == NULL || text[0] == '\0') return;

    NSString *announcement = [NSString stringWithUTF8String:text];
    if (announcement == nil || announcement.length == 0) return;

    void (^post_announcement)(void) = ^{
        UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, announcement);
    };

    if ([NSThread isMainThread]) post_announcement();
    else dispatch_async(dispatch_get_main_queue(), post_announcement);
}
