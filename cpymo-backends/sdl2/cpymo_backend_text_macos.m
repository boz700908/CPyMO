#import <AppKit/AppKit.h>

/* announce 与 stop 必须共用同一个合成器实例。
 * 以前两个 block 各自写 static 局部变量，作用域互不相干，
 * 于是 stop 拿到的永远是 nil，语音停不下来。 */
static NSSpeechSynthesizer *cpymo_macos_speaker = nil;

static NSSpeechSynthesizer *cpymo_macos_shared_speaker(void)
{
    if (cpymo_macos_speaker == nil)
        cpymo_macos_speaker = [[NSSpeechSynthesizer alloc] init];

    return cpymo_macos_speaker;
}

void cpymo_macos_accessibility_announce(const char *text)
{
    if (text == NULL || text[0] == '\0') return;

    NSString *announcement = [NSString stringWithUTF8String:text];
    if (announcement.length == 0) return;

    dispatch_async(dispatch_get_main_queue(), ^{
        NSSpeechSynthesizer *speaker = cpymo_macos_shared_speaker();
        [speaker stopSpeaking];
        [speaker startSpeakingString:announcement];
    });
}

void cpymo_macos_accessibility_stop(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (cpymo_macos_speaker != nil)
            [cpymo_macos_speaker stopSpeaking];
    });
}
