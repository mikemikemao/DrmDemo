package com.hikvision.jni;

import android.view.Surface;

public class MyTest {
    static {
        System.loadLibrary("learn-drm");
    }

    /**
     * description startPreview
     * param surface
     * @return
     */
    public native void native_helloWorld();
}
