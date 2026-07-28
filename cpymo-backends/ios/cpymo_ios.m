#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>
#include <SDL.h>

extern SDL_Window *window;

#ifdef ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY
static NSString *last_announcement;
static AVSpeechSynthesizer *speech_synthesizer;
static SDL_atomic_t pending_input_action;

void cpymo_ios_accessibility_announce(const char *text);
void cpymo_ios_accessibility_play_sound(int sound_type);
void cpymo_ios_accessibility_vibrate(int milliseconds);

enum {
    CPYMO_IOS_ACCESSIBILITY_UP = 1,
    CPYMO_IOS_ACCESSIBILITY_DOWN,
    CPYMO_IOS_ACCESSIBILITY_LEFT,
    CPYMO_IOS_ACCESSIBILITY_RIGHT,
    CPYMO_IOS_ACCESSIBILITY_OK,
    CPYMO_IOS_ACCESSIBILITY_CANCEL,
    CPYMO_IOS_ACCESSIBILITY_SKIP,
    CPYMO_IOS_ACCESSIBILITY_SKIP_HOLD_START,
    CPYMO_IOS_ACCESSIBILITY_SKIP_HOLD_END
};

@interface CPyMOAccessibilityTwoDoublePressRecognizer : UIGestureRecognizer
@property(nonatomic) NSUInteger phase;
@property(nonatomic) CFTimeInterval firstTapTime;
@end

@implementation CPyMOAccessibilityTwoDoublePressRecognizer
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (event.allTouches.count != 2) { self.state = UIGestureRecognizerStateFailed; return; }
    CFTimeInterval now = CACurrentMediaTime();
    if (self.phase == 0) {
        self.phase = 1;
        self.firstTapTime = now;
    } else if (self.phase == 2 && now - self.firstTapTime <= 0.4) {
        self.phase = 3;
        self.state = UIGestureRecognizerStateBegan;
    } else {
        self.state = UIGestureRecognizerStateFailed;
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (self.phase == 1) self.phase = 2;
    else if (self.phase == 3) self.state = UIGestureRecognizerStateEnded;
    else self.state = UIGestureRecognizerStateFailed;
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    self.state = self.phase == 3 ? UIGestureRecognizerStateCancelled : UIGestureRecognizerStateFailed;
}

- (void)reset
{
    self.phase = 0;
    self.firstTapTime = 0;
}
@end

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
    cpymo_ios_accessibility_vibrate(10);
    cpymo_ios_send_input(action);
}

- (void)activate:(UITapGestureRecognizer *)recognizer
{
    cpymo_ios_accessibility_play_sound(3);
    cpymo_ios_accessibility_vibrate(10);
    cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_OK);
}

- (void)scan:(UITapGestureRecognizer *)recognizer
{
    CGPoint point = [recognizer locationInView:recognizer.view];
    SDL_WarpMouseInWindow(window, (int)point.x, (int)point.y);
}

- (void)cancel:(UILongPressGestureRecognizer *)recognizer
{
    if (recognizer.state == UIGestureRecognizerStateBegan) {
        cpymo_ios_accessibility_play_sound(2);
        cpymo_ios_accessibility_vibrate(50);
        cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_CANCEL);
    }
}

- (void)skip:(UITapGestureRecognizer *)recognizer
{
    cpymo_ios_accessibility_play_sound(3);
    cpymo_ios_accessibility_vibrate(10);
    cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_SKIP);
}

- (void)twoFingerCancel:(UISwipeGestureRecognizer *)recognizer
{
    cpymo_ios_accessibility_play_sound(2);
    cpymo_ios_accessibility_vibrate(50);
    cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_CANCEL);
}

- (void)twoDoublePress:(CPyMOAccessibilityTwoDoublePressRecognizer *)recognizer
{
    if (recognizer.state == UIGestureRecognizerStateBegan) {
        cpymo_ios_accessibility_vibrate(20);
        cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_SKIP_HOLD_START);
    } else {
        cpymo_ios_accessibility_vibrate(20);
        cpymo_ios_send_input(CPYMO_IOS_ACCESSIBILITY_SKIP_HOLD_END);
    }
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
    cpymo_ios_accessibility_vibrate(10);
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

    UITapGestureRecognizer *scan = [[UITapGestureRecognizer alloc] initWithTarget:target action:@selector(scan:)];
    scan.delegate = gesture_delegate;
    scan.cancelsTouchesInView = YES;
    [scan requireGestureRecognizerToFail:activate];
    [view addGestureRecognizer:scan];

    UITapGestureRecognizer *skip = [[UITapGestureRecognizer alloc] initWithTarget:target action:@selector(skip:)];
    skip.numberOfTouchesRequired = 2;
    skip.numberOfTapsRequired = 2;
    skip.delegate = gesture_delegate;
    skip.cancelsTouchesInView = YES;

    CPyMOAccessibilityTwoDoublePressRecognizer *two_double_press = [[CPyMOAccessibilityTwoDoublePressRecognizer alloc] initWithTarget:target action:@selector(twoDoublePress:)];
    two_double_press.delegate = gesture_delegate;
    two_double_press.cancelsTouchesInView = YES;
    [skip requireGestureRecognizerToFail:two_double_press];
    [view addGestureRecognizer:two_double_press];
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

    UISwipeGestureRecognizer *two_finger_cancel = [[UISwipeGestureRecognizer alloc] initWithTarget:target action:@selector(twoFingerCancel:)];
    two_finger_cancel.numberOfTouchesRequired = 2;
    two_finger_cancel.direction = UISwipeGestureRecognizerDirectionDown;
    two_finger_cancel.delegate = gesture_delegate;
    two_finger_cancel.cancelsTouchesInView = YES;
    [view addGestureRecognizer:two_finger_cancel];
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

void cpymo_ios_accessibility_vibrate(int milliseconds) {
    (void)milliseconds;
    AudioServicesPlaySystemSound(kSystemSoundID_Vibrate);
}
#endif
