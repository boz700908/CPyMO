#import <AppKit/AppKit.h>

void cpymo_macos_accessibility_announce(const char *text)
{
    if (text == NULL || text[0] == '\0') return;

    NSString *announcement = [NSString stringWithUTF8String:text];
    if (announcement.length == 0) return;

    dispatch_async(dispatch_get_main_queue(), ^{
        static NSSpeechSynthesizer *speaker;
        if (speaker == nil) speaker = [[NSSpeechSynthesizer alloc] init];
        [speaker stopSpeaking];
        [speaker startSpeakingString:announcement];
    });
}
