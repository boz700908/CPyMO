package xyz.xydm.cpymo;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.SoundPool;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.speech.tts.TextToSpeech;
import android.util.Log;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.util.HashMap;

public class VisualHelper {
    public static final int SOUND_ENTER = 1;
    public static final int SOUND_MENU = 2;
    public static final int SOUND_SELECT = 3;

    private static final String TAG = "VisualHelper";

    private static TextToSpeech mTextToSpeech;
    private static Vibrator mVibrator;
    private static SoundPool mSoundPool;

    private static HashMap<Integer, Integer> mSoundMap = new HashMap<>();

    public static native void nativeSetupJNI();

    public static void setup(Context context) {
        // Set up text speech
        if (!Config.needAccessibility()) return;

        mTextToSpeech = new TextToSpeech(context, status -> {
            if (status == TextToSpeech.SUCCESS)
                mTextToSpeech.speak("请先关闭系统读屏，然后上下滑动选择游戏", TextToSpeech.QUEUE_FLUSH, null);
            else
                Toast.makeText(context, "TTS初始化失败", Toast.LENGTH_SHORT).show();
        });

        // Set up vibrator
        mVibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);

        // Set up sound pool
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
            AudioAttributes attrs = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build();
            mSoundPool = new SoundPool.Builder()
                    .setMaxStreams(2)
                    .setAudioAttributes(attrs)
                    .build();

            mSoundMap.put(SOUND_ENTER, mSoundPool.load(context, R.raw.enter, 1));
            mSoundMap.put(SOUND_MENU, mSoundPool.load(context, R.raw.menu, 1));
            mSoundMap.put(SOUND_SELECT, mSoundPool.load(context, R.raw.select, 1));
        } else {
            mSoundPool = new SoundPool(2, AudioManager.STREAM_MUSIC, 0);
            mSoundMap.put(SOUND_ENTER, mSoundPool.load(context, R.raw.enter, 1));
            mSoundMap.put(SOUND_MENU, mSoundPool.load(context, R.raw.menu, 1));
            mSoundMap.put(SOUND_SELECT, mSoundPool.load(context, R.raw.select, 1));
        }

        nativeSetupJNI();
    }

    /**
     * This method is called by SDL using JNI.
     */
    public static boolean textToSpeech(String text) {
        if (mTextToSpeech == null)
            return false;
        mTextToSpeech.speak(text, TextToSpeech.QUEUE_FLUSH, null);
        return true;
    }

    public static boolean textToSpeechWithoutCopy(String text) {
        if (mTextToSpeech == null)
            return false;
        mTextToSpeech.speak(text, TextToSpeech.QUEUE_FLUSH, null);
        return true;
    }

    public static void copySpeechText(String speechText, boolean append) {
        if (speechText == null || speechText.isEmpty()) return;
        String text = SDLActivity.clipboardGetText();
        SDLActivity.clipboardSetText(append && text != null && !text.isEmpty()
                ? text + "\n" + speechText : speechText);
        playAccessibilityFeedback(SOUND_SELECT);
        textToSpeechWithoutCopy(append ? "已追加复制" : "已复制");
    }

    public static void vibrate(long milliseconds) {
        // Vibrate for 500 milliseconds
        if (mVibrator == null || !mVibrator.hasVibrator()) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            mVibrator.vibrate(VibrationEffect.createOneShot(milliseconds, VibrationEffect.DEFAULT_AMPLITUDE));
        } else {
            //deprecated in API 26
            mVibrator.vibrate(milliseconds);
        }
    }

    public static void playSound(int sound_type) {
        Integer soundID = mSoundMap.get(sound_type);
        if (mSoundPool == null || soundID == null) {
            Log.e(TAG, "VisualHelper.playSound(): unknown sound type");
            return;
        }
        mSoundPool.play(soundID, 1, 1, 0, 0, 1);
    }

    public static void playAccessibilityFeedback(int soundType) {
        playSound(soundType);
        vibrate(soundType == SOUND_MENU ? 50 : 10);
    }
}
