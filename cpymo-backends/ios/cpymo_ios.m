#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>
#include <SDL_atomic.h>

#ifdef ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY
static NSString *last_announcement;
static AVSpeechSynthesizer *speech_synthesizer;
static SDL_atomic_t pending_input_action;

void cpymo_ios_accessibility_announce(const char *text);
void cpymo_ios_accessibility_play_sound(int sound_type);

enum {
    CPYMO_IOS_ACCESSIBILITY_UP = 1,
    CPYMO_IOS_ACCESSIBILITY_DOWN,
    CPYMO_IOS_ACCESSIBILITY_LEFT,
    CPYMO_IOS_ACCESSIBILITY_RIGHT,
    CPYMO_IOS_ACCESSIBILITY_OK,
    CPYMO_IOS_ACCESSIBILITY_CANCEL,
    CPYMO_IOS_ACCESSIBILITY_SKIP
};

@interface CPyMOAccessibilityGestureDelegate : NSObject <UIGestureRecognizerDelegate>
@end

@implementation CPyMOAccessibilityGestureDelegate
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer
{
    return NO;
}
@end

static CPyMOAccessibilityGestureDelegate *gesture_delegate;

static void cpymo_ios_send_input(int action)
{
    SDL_AtomicSet(&pending_input_action, action);
}

@interface CPyMOAccessibilityGestures : NSObject
@end

@implementation CPyMOAccessibilityGestures
- (void)swipe:(UISwipeGestureRecognizer *)recognizer
{
    int action = 0;
    switch (recognizer.direction) {
    case UISwipeGestureRecognizerDirectionUp: action = CPYMO_IOS_ACCESSIBILITY_UP; break;
    case UISwipeGestureRecognizerDirectionDown: action = CPYMO_IOS_ACCESSIBILITY_DOWN; break;
    case UISwipeGestureRecognizerDirectionLeft: action = CPYMO_IOS_ACCESSIBILITY_LEFT; break;
    case UISwipeGestureRecognizerDirectionRight: action = CPYMO_IOS_ACCESSIBILITY_RIGHT; break;
    default: return;
    }
    cpymo_ios_accessibility_play_sound(3);
    cpymo_ios_send_input(action);
}

- (void)activate:(UITapGestureRecognizer *)recognizer
{
    cpymo_ios_accessibility_play_sound(3);
    cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_OK);
}

- (void)cancel:(UILongPressGestureRecognizer *)recognizer
{
    if (recognizer.state == UIGestureRecognizerStateBegan) {
        cpymo_ios_accessibility_play_sound(2);
        cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_CANCEL);
    }
}

- (void)skip:(UITapGestureRecognizer *)recognizer
{
    cpymo_ios_accessibility_play_sound(3);
    cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_SKIP);
}

- (void)copy:(UISwipeGestureRecognizer *)recognizer
{
    if (last_announcement == nil) return;
    UIPasteboard *pasteboard = [UIPasteboard generalPasteboard];
    if (recognizer.direction == UISwipeGestureRecognizerDirectionLeft) {
        pasteboard.string = last_announcement;
        cpymo_ios_accessibility_announce("已复制");
    } else {
        NSString *prefix = pasteboard.string.length ? [pasteboard.string stringByAppendingString:@"\n"] : @"";
        pasteboard.string = [prefix stringByAppendingString:last_announcement];
        cpymo_ios_accessibility_announce("已追加复制");
    }
    cpymo_ios_accessibility_play_sound(3);
}
@end

static void cpymo_ios_install_accessibility_gestures(void)
{
    UIWindow *window = UIApplication.sharedApplication.keyWindow;
    if (window == nil) window = UIApplication.sharedApplication.windows.firstObject;
    UIView *view = window.rootViewController.view;
    if (view == nil || view.tag == 0x4350594d) return;
    view.tag = 0x4350594d;

    gesture_delegate = [CPyMOAccessibilityGestureDelegate new];
    CPyMOAccessibilityGestures *target = [CPyMOAccessibilityGestures new];
    objc_setAssociatedObject(view, @selector(cpymo_ios_install_accessibility_gestures), target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    for (NSNumber *direction_number in @[@(UISwipeGestureRecognizerDirectionUp), @(UISwipeGestureRecognizerDirectionDown), @(UISwipeGestureRecognizerDirectionLeft), @(UISwipeGestureRecognizerDirectionRight)]) {
        UISwipeGestureRecognizer *swipe = [[UISwipeGestureRecognizer alloc] initWithTarget:target action:@selector(swipe:)];
        swipe.direction = direction_number.unsignedIntegerValue;
        swipe.delegate = gesture_delegate;
        swipe.cancelsTouchesInView = YES;
        [view addGestureRecognizer:swipe];
    }

    UITapGestureRecognizer *activate = [[UITapGestureRecognizer alloc] initWithTarget:target action:@selector(activate:)];
    activate.numberOfTapsRequired = 2;
    activate.delegate = gesture_delegate;
    activate.cancelsTouchesInView = YES;
    [view addGestureRecognizer:activate];

    UITapGestureRecognizer *skip = [[UITapGestureRecognizer alloc] initWithTarget:target action:@selector(skip:)];
    skip.numberOfTouchesRequired = 2;
    skip.numberOfTapsRequired = 2;
    skip.delegate = gesture_delegate;
    skip.cancelsTouchesInView = YES;
    [view addGestureRecognizer:skip];

    UILongPressGestureRecognizer *cancel = [[UILongPressGestureRecognizer alloc] initWithTarget:target action:@selector(cancel:)];
    cancel.minimumPressDuration = 0.5;
    cancel.delegate = gesture_delegate;
    cancel.cancelsTouchesInView = YES;
    [view addGestureRecognizer:cancel];

    for (NSNumber *direction_number in @[@(UISwipeGestureRecognizerDirectionLeft), @(UISwipeGestureRecognizerDirectionRight)]) {
        UISwipeGestureRecognizer *copy = [[UISwipeGestureRecognizer alloc] initWithTarget:target action:@selector(copy:)];
        copy.numberOfTouchesRequired = 2;
        copy.direction = direction_number.unsignedIntegerValue;
        copy.delegate = gesture_delegate;
        copy.cancelsTouchesInView = YES;
        [view addGestureRecognizer:copy];
    }
}
#endif

const char* get_ios_directory() {
    NSString *homeDir = NSHomeDirectory();
    NSLog(@"homeDir=%@", homeDir);
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *docDir = [paths objectAtIndex:0];
    return [docDir UTF8String];
}

#ifdef ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY
void cpymo_ios_accessibility_announce(const char *text) {
    if (text == NULL || text[0] == '\0') return;

    NSString *announcement = [NSString stringWithUTF8String:text];
    if (announcement == nil || announcement.length == 0) return;

    last_announcement = announcement;
    void (^post_announcement)(void) = ^{
        cpymo_ios_install_accessibility_gestures();
        if (UIAccessibilityIsVoiceOverRunning()) {
            UIAccessibilityPostNotification(UIAccessibilityAnnouncementNotification, announcement);
        } else {
            if (speech_synthesizer == nil)
                speech_synthesizer = [[AVSpeechSynthesizer alloc] init];
            [speech_synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            AVSpeechUtterance *utterance = [AVSpeechUtterance speechUtteranceWithString:announcement];
            [speech_synthesizer speakUtterance:utterance];
        }
    };

    if ([NSThread isMainThread]) post_announcement();
    else dispatch_async(dispatch_get_main_queue(), post_announcement);
}

int cpymo_ios_accessibility_take_input_action(void)
{
    return SDL_AtomicSet(&pending_input_action, 0);
}

void cpymo_ios_accessibility_play_sound(int sound_type) {
    SystemSoundID sound = sound_type == 1 ? 1104 : (sound_type == 2 ? 1155 : 1103);
    AudioServicesPlaySystemSound(sound);
}
#endif
