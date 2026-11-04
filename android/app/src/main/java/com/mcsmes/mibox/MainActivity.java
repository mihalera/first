package com.mcsmes.mibox;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;

// ============================================================================
//  The APK's launcher.
//
//  JUCE's com.rmsl.juce.JuceActivity is deliberately NOT used. Its onResume()
//  and onNewIntent() call the native methods appOnResume/appNewIntent, but
//  JUCE compiles their implementations only inside
//  `#ifdef JUCE_PUSH_NOTIFICATIONS_ACTIVITY` (juce_Windowing_android.cpp).
//  This app has no push notifications, so libjuce_jni.so has neither method:
//  the APK installed and then died on the first resume with
//  `UnsatisfiedLinkError: No implementation found for void
//  com.rmsl.juce.JuceActivity.appOnResume()`.
//
//  A plain Activity is all JUCE's window layer needs. It resolves the launcher
//  class from the manifest (JuceActivityWatcher), attaches its
//  ComponentPeerView to this activity, and drives the audio engine from the
//  Application. The only behaviour worth keeping from JuceActivity is the
//  edge-to-edge setup below, which is pure platform code.
// ============================================================================
public class MainActivity extends Activity
{
    @SuppressWarnings ("deprecation")
    private void initEdgeToEdge()
    {
        if (Build.VERSION.SDK_INT < 35)
        {
            View decorView = getWindow().getDecorView();

            final int flags = Build.VERSION.SDK_INT < 30
                    ? (  View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                       | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                       | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
                    : 0;

            decorView.setSystemUiVisibility (decorView.getSystemUiVisibility() | flags);
        }

        if (Build.VERSION.SDK_INT >= 30)
            getWindow().setDecorFitsSystemWindows (false);

        if (Build.VERSION.SDK_INT >= 29)
        {
            if (Build.VERSION.SDK_INT < 35)
                getWindow().setStatusBarContrastEnforced (false);

            getWindow().setNavigationBarContrastEnforced (false);
        }
    }

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        initEdgeToEdge();
        super.onCreate (savedInstanceState);
    }
}
