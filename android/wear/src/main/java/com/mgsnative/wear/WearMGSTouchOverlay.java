package com.mgsnative.wear;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.MotionEvent;
import android.view.View;

import java.util.HashMap;
import java.util.Map;

/**
 * Metal Gear Solid 1 - Custom Wear OS Touch Overlay
 *
 * Minimalist PlayStation HUD:
 * - Top: ONLY "CODEC" text (Select)
 * - Upper-Left: ONLY "L" icon (Item menu / equip - L2)
 * - Upper-Right: ONLY "R" icon (Weapon menu / equip - R2)
 * - Action buttons on right edge:
 *     □ (Square): Weapon aim / Shoot / Choke
 *     ○ (Circle): Punch combo / Choke / Confirm / Actions
 *     ✕ (Cross): Crouch / Crawl / Cancel
 * - Bottom: △ (Triangle) — TOGGLE for 1st-Person Mode (Gyroscope aim + Shoot with □)
 * - Entire left area: Floating Virtual Analog Stick (locked against Wear OS swipe gestures)
 * - 3 taps with 2 fingers = START
 */
public class WearMGSTouchOverlay extends View {

    public static boolean SHOW_CONTROLS_FOR_PRINT = false;

    // JNI Native Input Hooks
    public static native void nativeSetTouchButtons(int buttons);
    public static native void nativeSetWearAnalog(boolean active, int lx, int ly);
    public static native void nativeSetFirstPersonLook(float deltaPitch, float deltaYaw);
    public static native boolean nativeIsFirstPersonActive();
    public static native boolean nativeIsInCodec();
    public static native int nativeCycleScreenMode();

    // PS1 Bitmasks (Active HIGH in Java; converted to Active LOW in native C)
    public static final int BTN_MASK_SELECT   = 0x0001; // Codec
    public static final int BTN_MASK_L3       = 0x0002;
    public static final int BTN_MASK_R3       = 0x0004;
    public static final int BTN_MASK_START    = 0x0008; // Start / Pause
    public static final int BTN_MASK_UP       = 0x0010;
    public static final int BTN_MASK_RIGHT    = 0x0020;
    public static final int BTN_MASK_DOWN     = 0x0040;
    public static final int BTN_MASK_LEFT     = 0x0080;
    public static final int BTN_MASK_L2       = 0x0100; // Items Menu
    public static final int BTN_MASK_R2       = 0x0200; // Weapons Menu
    public static final int BTN_MASK_L1       = 0x0400;
    public static final int BTN_MASK_R1       = 0x0800;
    public static final int BTN_MASK_TRIANGLE = 0x1000; // 1st Person View (Toggle)
    public static final int BTN_MASK_CIRCLE   = 0x2000; // Punch / Choke / Confirm
    public static final int BTN_MASK_CROSS    = 0x4000; // Crouch / Crawl
    public static final int BTN_MASK_SQUARE   = 0x8000; // Shoot / Weapon Aim

    // Authentic Metal Gear Solid & PlayStation Color Palette
    public static final int COLOR_TACTICAL_GREEN = Color.rgb(72, 219, 137);
    public static final int COLOR_CODEC_AMBER    = Color.rgb(255, 179, 0);
    public static final int COLOR_WEAPON_RED     = Color.rgb(255, 75, 75);
    public static final int COLOR_ITEM_BLUE      = Color.rgb(64, 158, 255);
    public static final int COLOR_CROSS          = Color.rgb(91, 134, 229);
    public static final int COLOR_SQUARE         = Color.rgb(243, 104, 224);
    public static final int COLOR_CIRCLE         = Color.rgb(255, 71, 87);
    public static final int COLOR_TRIANGLE       = Color.rgb(46, 213, 115);

    // Paints
    private final Paint paintArc = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintSymbol = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintIconBg = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintPrintVignette = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint paintAnalog = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final RectF ovalBounds = new RectF();
    private final Path trianglePath = new Path();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Vibrator vibrator;

    private int touchButtons = 0;
    private int pulseButtons = 0;
    private boolean fpsModeToggled = false;
    private final Map<Integer, Integer> pointerButtons = new HashMap<>();

    // Analog stick tracking (left half of screen)
    private boolean analogActive = false;
    private float analogOriginX, analogOriginY;
    private float analogCurrentX, analogCurrentY;
    private int analogPointerId = -1;

    // Gyro aiming analog values when in FPS mode
    private boolean gyroAimActive = false;
    private int gyroAimLX = 0;
    private int gyroAimLY = 0;

    // 2-finger multi-tap detection (3 taps with 2 fingers = START)
    private int twoFingerTapCount = 0;
    private long lastTwoFingerTapTime = 0;
    private boolean twoFingerDownActive = false;
    private long twoFingerDownTime = 0;
    private static final long TWO_FINGER_TAP_MAX_HOLD_MS = 350;
    private static final long TWO_FINGER_MULTI_TAP_WINDOW_MS = 650;
    private long lastCodecTapTime = 0;

    public WearMGSTouchOverlay(Context context) {
        super(context);
        init();
    }

    private void init() {
        setWillNotDraw(false);
        vibrator = (Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);

        paintArc.setStyle(Paint.Style.STROKE);
        paintArc.setStrokeCap(Paint.Cap.ROUND);

        paintText.setTextAlign(Paint.Align.CENTER);
        paintText.setFakeBoldText(true);
        paintText.setShadowLayer(5f, 0f, 2f, Color.argb(200, 0, 0, 0));

        paintSymbol.setStyle(Paint.Style.STROKE);
        paintSymbol.setStrokeWidth(4.5f);
        paintSymbol.setStrokeCap(Paint.Cap.ROUND);
        paintSymbol.setStrokeJoin(Paint.Join.ROUND);
        paintSymbol.setShadowLayer(5f, 0f, 2f, Color.argb(200, 0, 0, 0));

        paintIconBg.setStyle(Paint.Style.FILL);
        paintIconBg.setColor(Color.argb(195, 10, 14, 18));

        paintAnalog.setStyle(Paint.Style.FILL);
    }

    public boolean isFpsModeToggled() {
        return fpsModeToggled;
    }

    public void setGyroAim(boolean active, int lx, int ly) {
        if (!fpsModeToggled) {
            this.gyroAimActive = false;
            this.gyroAimLX = 0;
            this.gyroAimLY = 0;
            if (!analogActive) {
                updateAnalogOutput(false, 0, 0);
            }
            return;
        }
        this.gyroAimActive = active;
        this.gyroAimLX = lx;
        this.gyroAimLY = ly;
        if (!analogActive) {
            updateAnalogOutput(active, lx, ly);
        }
    }

    private void performHaptic(int effectType) {
        if (vibrator != null && vibrator.hasVibrator()) {
            try {
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
                    vibrator.vibrate(VibrationEffect.createPredefined(effectType));
                } else {
                    vibrator.vibrate(18);
                }
            } catch (Exception ignored) {}
        }
    }

    public void pulseButton(final int mask, long durationMs) {
        pulseButtons |= mask;
        syncNativeButtons();
        invalidate();
        handler.postDelayed(() -> {
            pulseButtons &= ~mask;
            syncNativeButtons();
            invalidate();
        }, durationMs);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float margin = 6f;
        ovalBounds.set(margin, margin, w - margin, h - margin);

        RadialGradient vignette = new RadialGradient(
                w / 2f, h / 2f, Math.min(w, h) / 2f,
                new int[]{Color.argb(130, 10, 15, 12), Color.argb(220, 5, 8, 7)},
                new float[]{0.45f, 1.0f},
                Shader.TileMode.CLAMP
        );
        paintPrintVignette.setShader(vignette);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // Prevent Wear OS swipe-to-dismiss gesture from taking over touch events
        if (getParent() != null) {
            getParent().requestDisallowInterceptTouchEvent(true);
        }

        int action = event.getActionMasked();
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        int pointerCount = event.getPointerCount();
        long now = System.currentTimeMillis();

        float w = getWidth();
        float h = getHeight();
        float cx = w / 2f;
        float cy = h / 2f;
        float r = Math.min(cx, cy);

        // Detect 2-finger taps (3 taps with 2 fingers = START)
        if (action == MotionEvent.ACTION_POINTER_DOWN && pointerCount == 2) {
            twoFingerDownActive = true;
            twoFingerDownTime = now;
            pointerButtons.clear();
            if (analogActive) {
                analogActive = false;
                analogPointerId = -1;
                updateAnalogOutput(gyroAimActive, gyroAimLX, gyroAimLY);
            }
            rebuildTouchButtons();
            invalidate();
            return true;
        }

        if (twoFingerDownActive) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                twoFingerDownActive = false;
                if (now - twoFingerDownTime <= TWO_FINGER_TAP_MAX_HOLD_MS) {
                    if (now - lastTwoFingerTapTime > TWO_FINGER_MULTI_TAP_WINDOW_MS) {
                        twoFingerTapCount = 0;
                    }
                    twoFingerTapCount++;
                    lastTwoFingerTapTime = now;

                    if (twoFingerTapCount == 3) {
                        twoFingerTapCount = 0;
                        performHaptic(VibrationEffect.EFFECT_HEAVY_CLICK);
                        pulseButton(BTN_MASK_START, 220);
                    }
                }
            }
            return true;
        }

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = event.getX(actionIndex);
                float y = event.getY(actionIndex);
                handleTouchDown(pointerId, x, y, cx, cy, r);
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < pointerCount; i++) {
                    int pId = event.getPointerId(i);
                    float x = event.getX(i);
                    float y = event.getY(i);
                    handleTouchMove(pId, x, y, cx, cy, r);
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                handleTouchUp(pointerId);
                break;
            }
        }

        invalidate();
        return true;
    }

    private void handleTouchDown(int pointerId, float x, float y, float cx, float cy, float r) {
        float hitThreshold = r * 0.23f;

        // Button positions
        float codecX = cx,                   codecY = cy - r * 0.82f;
        float lItemX = cx - r * 0.62f,       lItemY = cy - r * 0.58f;
        float rWeapX = cx + r * 0.62f,       rWeapY = cy - r * 0.58f;
        float sqX    = cx + r * 0.78f,       sqY    = cy - r * 0.20f;
        float cirX   = cx + r * 0.80f,       cirY   = cy + r * 0.18f;
        float crossX = cx + r * 0.58f,       crossY = cy + r * 0.58f;
        float triX   = cx,                   triY   = cy + r * 0.80f;

        // 1. Top: CODEC (Select) - Double tap toggles Screen Mode (Big Round [Default] / 1:1 / 4:3)
        if (Math.hypot(x - codecX, y - codecY) <= hitThreshold || (y < cy - r * 0.65f && Math.abs(x - cx) < r * 0.40f)) {
            long now = System.currentTimeMillis();
            if (now - lastCodecTapTime < 380) {
                lastCodecTapTime = 0;
                try {
                    nativeCycleScreenMode();
                    performHaptic(VibrationEffect.EFFECT_HEAVY_CLICK);
                } catch (Throwable ignored) {}
                return;
            }
            lastCodecTapTime = now;
            pointerButtons.put(pointerId, BTN_MASK_SELECT);
            rebuildTouchButtons();
            performHaptic(VibrationEffect.EFFECT_CLICK);
            return;
        }

        // 2. Upper-Left: L (Items Menu)
        if (Math.hypot(x - lItemX, y - lItemY) <= hitThreshold) {
            pointerButtons.put(pointerId, BTN_MASK_L2);
            rebuildTouchButtons();
            performHaptic(VibrationEffect.EFFECT_TICK);
            return;
        }

        // 3. Upper-Right: R (Weapons Menu)
        if (Math.hypot(x - rWeapX, y - rWeapY) <= hitThreshold) {
            pointerButtons.put(pointerId, BTN_MASK_R2);
            rebuildTouchButtons();
            performHaptic(VibrationEffect.EFFECT_TICK);
            return;
        }

        // 4. Action: Square (□) - Shoot / Aim Weapon
        if (Math.hypot(x - sqX, y - sqY) <= hitThreshold) {
            pointerButtons.put(pointerId, BTN_MASK_SQUARE);
            rebuildTouchButtons();
            performHaptic(VibrationEffect.EFFECT_CLICK);
            return;
        }

        // 5. Action: Circle (○) - Punch / Choke / Confirm
        if (Math.hypot(x - cirX, y - cirY) <= hitThreshold) {
            pointerButtons.put(pointerId, BTN_MASK_CIRCLE);
            rebuildTouchButtons();
            performHaptic(VibrationEffect.EFFECT_CLICK);
            return;
        }

        // 6. Action: Cross (✕) - Crouch / Crawl / Cancel
        if (Math.hypot(x - crossX, y - crossY) <= hitThreshold) {
            pointerButtons.put(pointerId, BTN_MASK_CROSS);
            rebuildTouchButtons();
            performHaptic(VibrationEffect.EFFECT_CLICK);
            return;
        }

        // 7. Bottom: Triangle (△) - Toggle First-Person Mode
        if (Math.hypot(x - triX, y - triY) <= hitThreshold || (y > cy + r * 0.65f && Math.abs(x - cx) < r * 0.42f)) {
            fpsModeToggled = !fpsModeToggled;
            if (!fpsModeToggled) {
                gyroAimActive = false;
                gyroAimLX = 0;
                gyroAimLY = 0;
                if (!analogActive) {
                    updateAnalogOutput(false, 0, 0);
                }
            }
            if (getContext() instanceof WearMGSActivity) {
                ((WearMGSActivity) getContext()).notifyFpsModeToggled(fpsModeToggled);
            }
            syncNativeButtons();
            performHaptic(VibrationEffect.EFFECT_HEAVY_CLICK);
            return;
        }

        // 8. Anywhere else on the left side: Dynamic Floating Virtual Joystick
        if (x < cx + r * 0.05f && !analogActive) {
            analogActive = true;
            analogPointerId = pointerId;
            analogOriginX = x;
            analogOriginY = y;
            analogCurrentX = x;
            analogCurrentY = y;
            updateAnalogOutput(true, 0, 0);
        }
    }

    private void handleTouchMove(int pointerId, float x, float y, float cx, float cy, float r) {
        if (pointerId == analogPointerId && analogActive) {
            analogCurrentX = x;
            analogCurrentY = y;
            float adx = analogCurrentX - analogOriginX;
            float ady = analogCurrentY - analogOriginY;
            float dist = (float) Math.hypot(adx, ady);
            float maxRadius = 0.35f * r;

            if (dist > maxRadius) {
                adx = (adx / dist) * maxRadius;
                ady = (ady / dist) * maxRadius;
                analogCurrentX = analogOriginX + adx;
                analogCurrentY = analogOriginY + ady;
            }

            int lx = Math.round((adx / maxRadius) * 127f);
            int ly = Math.round((ady / maxRadius) * 127f);
            updateAnalogOutput(true, lx, ly);
        }
    }

    private void handleTouchUp(int pointerId) {
        if (pointerId == analogPointerId) {
            analogActive = false;
            analogPointerId = -1;
            if (fpsModeToggled && gyroAimActive) {
                updateAnalogOutput(true, gyroAimLX, gyroAimLY);
            } else {
                updateAnalogOutput(false, 0, 0);
            }
        }

        if (pointerButtons.remove(pointerId) != null) {
            rebuildTouchButtons();
        }
    }

    private void rebuildTouchButtons() {
        int m = 0;
        for (int b : pointerButtons.values()) {
            m |= b;
        }
        touchButtons = m;
        syncNativeButtons();
    }

    private void syncNativeButtons() {
        int combined = touchButtons | pulseButtons;
        if (fpsModeToggled) {
            // When in FPS Mode: keep Triangle held for 1st-person camera,
            // but if Square (Shoot) is pressed, temporarily drop Triangle so weapon fires forward!
            if ((combined & BTN_MASK_SQUARE) == 0) {
                combined |= BTN_MASK_TRIANGLE;
            }
        }
        try {
            nativeSetTouchButtons(combined);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    private void updateAnalogOutput(boolean active, int lx, int ly) {
        try {
            nativeSetWearAnalog(active, lx, ly);
        } catch (UnsatisfiedLinkError ignored) {}
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth();
        float h = getHeight();
        float cx = w / 2f;
        float cy = h / 2f;
        float r = Math.min(cx, cy);

        if (SHOW_CONTROLS_FOR_PRINT) {
            canvas.drawRect(0, 0, w, h, paintPrintVignette);
        }

        int active = touchButtons | pulseButtons;

        // 1. Top: ONLY "CODEC"
        boolean codecPressed = (active & BTN_MASK_SELECT) != 0;
        drawTopCodecHUD(canvas, cx, cy, r, codecPressed);

        // 2. Upper-Left: ONLY "L" (Item menu) & Upper-Right: ONLY "R" (Weapon menu)
        drawInventoryBadges(canvas, cx, cy, r, active);

        // 3. Right Action Buttons: Square (□), Circle (○), Cross (✕)
        drawActionButtons(canvas, cx, cy, r, active);

        // 4. Bottom: Triangle (△) (Toggleable FPS mode)
        drawFirstPersonHUD(canvas, cx, cy, r, fpsModeToggled);

        // 5. Floating Analog Stick (left side)
        if (analogActive || SHOW_CONTROLS_FOR_PRINT) {
            drawAnalogHUD(canvas, cx, cy, r);
        }
    }

    private void drawTopCodecHUD(Canvas canvas, float cx, float cy, float r, boolean pressed) {
        paintText.setColor(pressed ? Color.WHITE : COLOR_CODEC_AMBER);
        paintText.setTextSize(r * 0.105f);
        canvas.drawText("CODEC", cx, cy - r * 0.81f, paintText);
    }

    private void drawInventoryBadges(Canvas canvas, float cx, float cy, float r, int active) {
        boolean lPressed = (active & BTN_MASK_L2) != 0;
        boolean rPressed = (active & BTN_MASK_R2) != 0;

        // L (Item Menu) - Upper-Left: Pure transparent, just the colored letter L
        float lX = cx - r * 0.62f;
        float lY = cy - r * 0.58f;
        paintText.setColor(lPressed ? Color.WHITE : COLOR_ITEM_BLUE);
        paintText.setTextSize(r * 0.120f);
        Paint.FontMetrics fm = paintText.getFontMetrics();
        canvas.drawText("L", lX, lY - (fm.ascent + fm.descent) * 0.5f, paintText);

        // R (Weapon Menu) - Upper-Right: Pure transparent, just the colored letter R (spaced away from Square)
        float rX = cx + r * 0.62f;
        float rY = cy - r * 0.58f;
        paintText.setColor(rPressed ? Color.WHITE : COLOR_WEAPON_RED);
        canvas.drawText("R", rX, rY - (fm.ascent + fm.descent) * 0.5f, paintText);
    }

    private void drawActionButtons(Canvas canvas, float cx, float cy, float r, int active) {
        boolean sqPressed = (active & BTN_MASK_SQUARE) != 0;
        boolean cirPressed = (active & BTN_MASK_CIRCLE) != 0;
        boolean crPressed = (active & BTN_MASK_CROSS) != 0;
        float symSize = r * 0.054f;

        // 1. Square (□) - Shoot / Weapon Aim: Pure transparent, colored square
        float sqX = cx + r * 0.78f;
        float sqY = cy - r * 0.20f;
        paintSymbol.setColor(sqPressed ? Color.WHITE : COLOR_SQUARE);
        paintSymbol.setStrokeWidth(sqPressed ? 6.5f : 4.5f);
        canvas.drawRect(sqX - symSize, sqY - symSize, sqX + symSize, sqY + symSize, paintSymbol);

        // 2. Circle (○) - Punch / Kick / Choke / Confirm: Pure transparent, colored circle
        float cirX = cx + r * 0.80f;
        float cirY = cy + r * 0.18f;
        paintSymbol.setColor(cirPressed ? Color.WHITE : COLOR_CIRCLE);
        paintSymbol.setStrokeWidth(cirPressed ? 6.5f : 4.5f);
        canvas.drawCircle(cirX, cirY, symSize * 1.15f, paintSymbol);

        // 3. Cross (✕) - Crouch / Crawl / Cancel: Pure transparent, colored cross
        float crX = cx + r * 0.58f;
        float crY = cy + r * 0.58f;
        paintSymbol.setColor(crPressed ? Color.WHITE : COLOR_CROSS);
        paintSymbol.setStrokeWidth(crPressed ? 6.5f : 4.5f);
        canvas.drawLine(crX - symSize, crY - symSize, crX + symSize, crY + symSize, paintSymbol);
        canvas.drawLine(crX + symSize, crY - symSize, crX - symSize, crY + symSize, paintSymbol);
    }

    private void drawFirstPersonHUD(Canvas canvas, float cx, float cy, float r, boolean toggledOn) {
        float triX = cx;
        float triY = cy + r * 0.80f;
        float symSize = r * 0.058f;

        paintSymbol.setColor(toggledOn ? Color.WHITE : COLOR_TRIANGLE);
        paintSymbol.setStrokeWidth(toggledOn ? 6.5f : 4.5f);
        trianglePath.reset();
        trianglePath.moveTo(triX, triY - symSize * 1.1f);
        trianglePath.lineTo(triX + symSize, triY + symSize * 0.85f);
        trianglePath.lineTo(triX - symSize, triY + symSize * 0.85f);
        trianglePath.close();
        canvas.drawPath(trianglePath, paintSymbol);
    }

    private void drawAnalogHUD(Canvas canvas, float cx, float cy, float r) {
        float ox = analogActive ? analogOriginX : (cx - r * 0.45f);
        float oy = analogActive ? analogOriginY : (cy + r * 0.15f);
        float curX = analogActive ? analogCurrentX : ox;
        float curY = analogActive ? analogCurrentY : oy;

        paintAnalog.setColor(Color.argb(35, 72, 219, 137));
        canvas.drawCircle(ox, oy, r * 0.32f, paintAnalog);

        paintArc.setColor(Color.argb(70, 72, 219, 137));
        paintArc.setStrokeWidth(2f);
        canvas.drawCircle(ox, oy, r * 0.18f, paintArc);

        paintAnalog.setColor(Color.argb(analogActive ? 220 : 120, 72, 219, 137));
        canvas.drawCircle(curX, curY, r * 0.11f, paintAnalog);
    }
}
