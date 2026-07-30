package xyz.xydm.cpymo;

public class Config {
    public static final int ACCESSIBILITY_UP = 1;
    public static final int ACCESSIBILITY_DOWN = 2;
    public static final int ACCESSIBILITY_LEFT = 3;
    public static final int ACCESSIBILITY_RIGHT = 4;
    public static final int ACCESSIBILITY_OK = 5;
    public static final int ACCESSIBILITY_CANCEL = 6;
    public static final int ACCESSIBILITY_SKIP = 7;
    public static final int ACCESSIBILITY_SKIP_HOLD_START = 8;
    public static final int ACCESSIBILITY_SKIP_HOLD_END = 9;
    public static final int ACCESSIBILITY_COPY = 10;
    public static final int ACCESSIBILITY_APPEND_COPY = 11;

    public static native boolean nativeNeedAccessibility();
    public static native void nativeInputDeviceChanged();
    public static native void nativeAccessibilityAction(int action);

    public static boolean needAccessibility() {
        return nativeNeedAccessibility();
    }
}
