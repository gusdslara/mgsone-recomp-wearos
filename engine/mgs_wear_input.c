#include "mgs_wear_input.h"
#include <string.h>

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#define LOG_TAG "MGSWearInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define JNI_FN(name) JNIEXPORT JNICALL __attribute__((visibility("default"))) name
#else
#define LOGI(...)
#define JNI_FN(name) name
#endif

// Internal state tracking
static uint16_t s_touchButtons = 0; // Bitmask of currently active buttons (active HIGH from Java)
static bool     s_wearAnalogActive = false;
static uint8_t  s_wearAnalogLX = 128;
static uint8_t  s_wearAnalogLY = 128;

static float    s_fppPitchDelta = 0.0f;
static float    s_fppYawDelta = 0.0f;
static bool     s_inFirstPerson = false;
static bool     s_inCodec = false;
static int      s_screenMode = 0; // 0 = Big Round (Default), 1 = 1:1 Square, 2 = Inscribed 4:3

void MGSWear_InitInput(void) {
    s_touchButtons = 0;
    s_wearAnalogActive = false;
    s_wearAnalogLX = 128;
    s_wearAnalogLY = 128;
    s_fppPitchDelta = 0.0f;
    s_fppYawDelta = 0.0f;
    s_inFirstPerson = false;
    s_inCodec = false;
}

void MGSWear_PollInput(MGSWearPadState *pad) {
    if (!pad) return;

    // Convert active-HIGH touch buttons to PS1 active-LOW bits:
    // When a bit in s_touchButtons is 1 (pressed), in PS1 hardware it is 0.
    uint16_t ps1Buttons = (uint16_t)(~s_touchButtons);

    // If analog stick is pushed, map direction to D-Pad or stick
    if (s_wearAnalogActive) {
        pad->left_stick_x = s_wearAnalogLX;
        pad->left_stick_y = s_wearAnalogLY;

        // D-Pad emulation for compatibility
        if (s_wearAnalogLX < 64)  ps1Buttons &= ~(0x0080); // LEFT
        if (s_wearAnalogLX > 192) ps1Buttons &= ~(0x0020); // RIGHT
        if (s_wearAnalogLY < 64)  ps1Buttons &= ~(0x0010); // UP
        if (s_wearAnalogLY > 192) ps1Buttons &= ~(0x0040); // DOWN
    } else {
        pad->left_stick_x = 128;
        pad->left_stick_y = 128;
    }

    pad->buttons = ps1Buttons;
    pad->right_stick_x = 128;
    pad->right_stick_y = 128;
}

bool MGSWear_IsFirstPersonMode(void) {
    // If Triangle is held or toggled, or game internal state is FPP
    return (s_touchButtons & 0x1000) != 0 || s_inFirstPerson;
}

bool MGSWear_IsInCodec(void) {
    return (s_touchButtons & 0x0001) != 0 || s_inCodec;
}

int MGSWear_GetScreenMode(void) {
    return s_screenMode;
}

void MGSWear_CycleScreenMode(void) {
    s_screenMode = (s_screenMode + 1) % 3;
    LOGI("Screen Mode switched to: %d (0=The Big One [Round 4:3], 1=4:3 Fit, 2=1:1 Square)", s_screenMode);
}

#ifdef __ANDROID__

JNIEXPORT void JNICALL __attribute__((visibility("default")))
Java_com_mgsnative_wear_WearMGSTouchOverlay_nativeSetTouchButtons(JNIEnv *env, jclass clazz, jint buttons) {
    (void)env;
    (void)clazz;
    s_touchButtons = (uint16_t)buttons;
}

JNIEXPORT void JNICALL __attribute__((visibility("default")))
Java_com_mgsnative_wear_WearMGSTouchOverlay_nativeSetWearAnalog(JNIEnv *env, jclass clazz, jboolean active, jint lx, jint ly) {
    (void)env;
    (void)clazz;
    s_wearAnalogActive = active ? true : false;
    // Map -128..127 to 0..255 PS1 stick range
    s_wearAnalogLX = (uint8_t)(lx + 128);
    s_wearAnalogLY = (uint8_t)(ly + 128);
}

JNIEXPORT void JNICALL __attribute__((visibility("default")))
Java_com_mgsnative_wear_WearMGSTouchOverlay_nativeSetFirstPersonLook(JNIEnv *env, jclass clazz, jfloat deltaPitch, jfloat deltaYaw) {
    (void)env;
    (void)clazz;
    s_fppPitchDelta = deltaPitch;
    s_fppYawDelta = deltaYaw;
}

JNIEXPORT jboolean JNICALL __attribute__((visibility("default")))
Java_com_mgsnative_wear_WearMGSTouchOverlay_nativeIsFirstPersonActive(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    return MGSWear_IsFirstPersonMode() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL __attribute__((visibility("default")))
Java_com_mgsnative_wear_WearMGSTouchOverlay_nativeIsInCodec(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    return MGSWear_IsInCodec() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL __attribute__((visibility("default")))
Java_com_mgsnative_wear_WearMGSTouchOverlay_nativeCycleScreenMode(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    MGSWear_CycleScreenMode();
    return (jint)MGSWear_GetScreenMode();
}

#endif
