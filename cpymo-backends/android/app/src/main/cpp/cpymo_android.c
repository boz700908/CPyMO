#include <cpymo_prelude.h>
#include "include/cpymo_android.h"

#include <jni.h>
#include <SDL.h>
#include <android/log.h>

static jclass mVisualHelperClass;
static jmethodID midTextToSpeech;
static jmethodID midPlaySound;
static jclass mControllerManagerClass;
static jmethodID midPollInputDevices;
static SDL_atomic_t input_device_changed;

JNIEXPORT jboolean JNICALL
Java_xyz_xydm_cpymo_Config_nativeNeedAccessibility(JNIEnv *env, jclass clazz)
{
#if defined ENABLE_TEXT_EXTRACT && defined ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT void JNICALL
Java_xyz_xydm_cpymo_Config_nativeInputDeviceChanged(JNIEnv *env, jclass clazz)
{
    SDL_AtomicSet(&input_device_changed, 1);
}

JNIEXPORT void JNICALL
Java_xyz_xydm_cpymo_VisualHelper_nativeSetupJNI(JNIEnv *env, jclass clazz)
{
    mVisualHelperClass = (jclass)((*env)->NewGlobalRef(env, clazz));
    midTextToSpeech = (*env)->GetStaticMethodID(env, clazz, "textToSpeech", "(Ljava/lang/String;)Z");
    midPlaySound = (*env)->GetStaticMethodID(env, clazz, "playSound", "(I)V");
}

void cpymo_android_text_to_speech(const char* text)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jstring jtext = (*env)->NewStringUTF(env, text);
    (*env)->CallStaticBooleanMethod(env, mVisualHelperClass, midTextToSpeech, jtext);
    (*env)->DeleteLocalRef(env, jtext);
}

void cpymo_android_play_sound(int sound_type)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    (*env)->CallStaticVoidMethod(env, mVisualHelperClass, midPlaySound, sound_type);
}

void cpymo_android_refresh_input_devices(void)
{
    if (!SDL_AtomicSet(&input_device_changed, 0)) return;

    JNIEnv *env = SDL_AndroidGetJNIEnv();
    if (env == NULL) return;

    if (mControllerManagerClass == NULL) {
        jclass controller_manager = (*env)->FindClass(env, "org/libsdl/app/SDLControllerManager");
        if (controller_manager == NULL) {
            (*env)->ExceptionClear(env);
            return;
        }
        mControllerManagerClass = (jclass)(*env)->NewGlobalRef(env, controller_manager);
        (*env)->DeleteLocalRef(env, controller_manager);
        if (mControllerManagerClass == NULL) return;

        midPollInputDevices = (*env)->GetStaticMethodID(env, mControllerManagerClass,
                "pollInputDevices", "()V");
        if (midPollInputDevices == NULL) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteGlobalRef(env, mControllerManagerClass);
            mControllerManagerClass = NULL;
            return;
        }
    }

    (*env)->CallStaticVoidMethod(env, mControllerManagerClass, midPollInputDevices);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

