package com.mgsnative.wear;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * WearMGSActivity - Metal Gear Solid 1 on Wear OS
 *
 * Implements:
 * 1. Screen WakeLock for stealth sessions and Codec cutscenes.
 * 2. Rotary Crown navigation for Codec frequency tuning.
 * 3. Wrist Gyroscope + Arm Tilt Aiming in Toggleable First-Person Mode (Triangle).
 * 4. Minimalist Icon-Only HUD Overlay (CODEC, □, R2, ✕, △, L2) + 3-tap 2-finger START.
 */
public class WearMGSActivity extends SDLActivity implements SensorEventListener {

    private static final String TAG = "WearMGSActivity";

    private WearMGSTouchOverlay touchOverlay;
    private SensorManager sensorManager;
    private Sensor gyroscope;
    private Sensor accelerometer;

    // Rotary Crown tracking
    private float rotaryAccum = 0f;
    private static final float ROTARY_STEP = 1.0f;

    // Gyroscope & Accelerometer tracking for First-Person Aim
    private float lastGyroX = 0f, lastGyroY = 0f, lastGyroZ = 0f;
    private float gravX = 0f, gravY = 3.5f, gravZ = 8.8f;
    private boolean gravInit = false;
    private float baseGx = 0f, baseGy = 3.5f, baseGz = 8.8f;
    private float smoothAimX = 0f, smoothAimY = 0f;
    private volatile boolean mLibsLoaded = false;
    private android.os.PowerManager.WakeLock wakeLock;

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "mgsnative"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mLibsLoaded = true;

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK | android.os.PowerManager.ON_AFTER_RELEASE, "WearMGS:GameLock");
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true);
            setTurnScreenOn(true);
        }

        sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager != null) {
            gyroscope = sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
            accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        }

        setupOverlay();

        // Lock system gestures (prevent swipe-to-minimize from the edges)
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
            getWindow().getDecorView().post(() -> {
                android.view.View decor = getWindow().getDecorView();
                java.util.List<android.graphics.Rect> exclusionRects = java.util.Collections.singletonList(
                        new android.graphics.Rect(0, 0, decor.getWidth(), decor.getHeight())
                );
                decor.setSystemGestureExclusionRects(exclusionRects);
            });
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK) {
            // Pulse START or ignore back button so it doesn't minimize the game
            if (touchOverlay != null) {
                touchOverlay.pulseButton(WearMGSTouchOverlay.BTN_MASK_START, 200);
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        if (keyCode == android.view.KeyEvent.KEYCODE_BACK) {
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    private void setupOverlay() {
        touchOverlay = new WearMGSTouchOverlay(this);
        ViewGroup.LayoutParams params = new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
        );
        addContentView(touchOverlay, params);
    }

    /**
     * Called when the user toggles First-Person Mode (Triangle) ON or OFF.
     * Calibrates current arm/wrist posture as the center aiming point.
     */
    public void notifyFpsModeToggled(boolean enabled) {
        if (enabled && gravInit) {
            baseGx = gravX;
            baseGy = gravY;
            baseGz = gravZ;
            smoothAimX = 0f;
            smoothAimY = 0f;
        } else if (!enabled) {
            smoothAimX = 0f;
            smoothAimY = 0f;
            if (touchOverlay != null) {
                touchOverlay.setGyroAim(false, 0, 0);
            }
            try {
                WearMGSTouchOverlay.nativeSetFirstPersonLook(0f, 0f);
            } catch (Throwable ignored) {}
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire(60 * 60 * 1000L);
        }
        if (sensorManager != null) {
            if (gyroscope != null) {
                sensorManager.registerListener(this, gyroscope, SensorManager.SENSOR_DELAY_GAME);
            }
            if (accelerometer != null) {
                sensorManager.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_GAME);
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        if (sensorManager != null) {
            sensorManager.unregisterListener(this);
        }
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_SCROLL &&
                event.isFromSource(InputDevice.SOURCE_ROTARY_ENCODER)) {

            float axisVal = -event.getAxisValue(MotionEvent.AXIS_SCROLL);
            rotaryAccum += axisVal;

            if (Math.abs(rotaryAccum) >= ROTARY_STEP) {
                int direction = rotaryAccum > 0 ? 1 : -1;
                rotaryAccum = 0f;
                handleRotaryStep(direction);
            }
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    private void handleRotaryStep(int direction) {
        if (touchOverlay != null) {
            // Tune Codec frequency or cycle menu items with rotary crown
            touchOverlay.pulseButton(direction > 0 ? WearMGSTouchOverlay.BTN_MASK_RIGHT : WearMGSTouchOverlay.BTN_MASK_LEFT, 90);
        }
        Log.d(TAG, "Rotary Step: " + direction);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (!mLibsLoaded || touchOverlay == null) return;

        // If NOT in FPS mode, sensors must NEVER touch character movement!
        if (!touchOverlay.isFpsModeToggled()) {
            smoothAimX = 0f;
            smoothAimY = 0f;
            return;
        }

        if (event.sensor.getType() == Sensor.TYPE_GYROSCOPE) {
            lastGyroX = event.values[0];
            lastGyroY = event.values[1];
            lastGyroZ = event.values[2];

            try {
                // Invert horizontal yaw aiming per user request so wrist up turning body turns view correctly
                WearMGSTouchOverlay.nativeSetFirstPersonLook(-lastGyroX * 0.06f, -lastGyroZ * 0.06f);
            } catch (Throwable ignored) {}
        } else if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
            float ax = event.values[0];
            float ay = event.values[1];
            float az = event.values[2];

            if (!gravInit) {
                gravX = ax;
                gravY = ay;
                gravZ = az;
                baseGx = ax;
                baseGy = ay;
                baseGz = az;
                gravInit = true;
            } else {
                final float alpha = 0.75f;
                gravX = alpha * gravX + (1f - alpha) * ax;
                gravY = alpha * gravY + (1f - alpha) * ay;
                gravZ = alpha * gravZ + (1f - alpha) * az;
            }

            // Arm/wrist tilt and rotation aiming (horizontal inverted per user feedback)
            float relX = ((gravX - baseGx) * 0.35f - lastGyroZ * 0.45f);
            float relY = -((gravY - baseGy) * 0.35f + lastGyroX * 0.45f);

            smoothAimX = 0.65f * smoothAimX + 0.35f * relX;
            smoothAimY = 0.65f * smoothAimY + 0.35f * relY;

            int lx = Math.max(-127, Math.min(127, Math.round(smoothAimX * 127f)));
            int ly = Math.max(-127, Math.min(127, Math.round(smoothAimY * 127f)));

            // Deadzone so holding arm steady doesn't drift
            if (Math.abs(lx) < 18) lx = 0;
            if (Math.abs(ly) < 18) ly = 0;

            touchOverlay.setGyroAim(lx != 0 || ly != 0, lx, ly);
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // No-op
    }
}
