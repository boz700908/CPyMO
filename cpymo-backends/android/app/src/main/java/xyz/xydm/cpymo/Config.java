package xyz.xydm.cpymo;

public class Config {
    public static native boolean nativeNeedAccessibility();
    public static native void nativeInputDeviceChanged();

    public static boolean needAccessibility() {
        return nativeNeedAccessibility();
    }
}
