package com.mcsmes.mibox;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;

import java.util.Arrays;

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
//
//  Every lifecycle callback also writes a line through Diag, which is dormant
//  unless the app was started with `--ez miboxDiag true`. That trace is the only
//  way to see how far a start gets on a device we cannot attach a debugger to.
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
        Diag.enable (this, getIntent());
        Diag.note ("MainActivity.onCreate begin");

        super.onCreate (savedInstanceState);
        initEdgeToEdge();

        Diag.note ("MainActivity.onCreate end, edge-to-edge applied");
    }

    @Override
    protected void onStart()
    {
        Diag.note ("MainActivity.onStart");
        super.onStart();
    }

    @Override
    protected void onResume()
    {
        Diag.note ("MainActivity.onResume");
        super.onResume();
    }

    @Override
    protected void onPause()
    {
        Diag.note ("MainActivity.onPause");
        super.onPause();
    }

    @Override
    protected void onStop()
    {
        Diag.note ("MainActivity.onStop");
        super.onStop();
    }

    @Override
    protected void onDestroy()
    {
        Diag.note ("MainActivity.onDestroy");
        super.onDestroy();
    }

    @Override
    protected void onNewIntent (Intent intent)
    {
        Diag.note ("MainActivity.onNewIntent");
        super.onNewIntent (intent);
        setIntent (intent);
    }

    @Override
    public void onRequestPermissionsResult (int requestCode, String[] permissions, int[] results)
    {
        // Logged before the call through to super, because the dispatch to the
        // fragment JUCE uses to hear the answer is what super does - if anything
        // in that path throws, the request and its result are already written.
        Diag.note ("onRequestPermissionsResult code=" + requestCode
                   + " permissions=" + Arrays.toString (permissions)
                   + " results=" + Arrays.toString (results));

        super.onRequestPermissionsResult (requestCode, permissions, results);
    }
}
