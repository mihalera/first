package com.mcsmes.mibox;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

// ============================================================================
//  The startup trace the app can turn on for itself.
//
//  The report this exists for - "it does not open without the microphone
//  permission" - needs the order of events from process start to the first
//  frame, which is exactly what nobody can see from a black screen. So the
//  launcher writes them down when it is started with an extra:
//
//      adb shell am start -n com.mcsmes.mibox/.MainActivity --ez miboxDiag true
//
//  Everything lands in mibox-start.log in the app's own external files
//  directory, and the absolute path is printed to logcat under the tag MiBoXDiag
//  so it can be found without guessing:
//
//      adb pull /sdcard/Android/data/com.mcsmes.mibox/files/mibox-start.log
//
//  The native half appends to the same file: setLogPath() below is the one call
//  Java makes into it, and it is also what turns the native lines on. A launch
//  without the extra calls nothing and writes nothing.
// ============================================================================
public final class Diag
{
    private static final String TAG = "MiBoXDiag";
    private static final String LOG_NAME = "mibox-start.log";

    /** Implemented in Source/Diagnostics/AndroidBridge.cpp. */
    private static native void setLogPath (String path);

    private static final Object lock = new Object();
    private static final long startedAt = System.currentTimeMillis();

    private static File logFile;
    private static boolean enabled;

    private Diag() {}

    /** Reads the launch extra, prepares the file and installs the crash handler.
        Returns whether the trace is on. */
    public static boolean enable (Activity activity, Intent intent)
    {
        final boolean requested = intent != null && intent.getBooleanExtra ("miboxDiag", false);

        try
        {
            final File directory = activity.getExternalFilesDir (null);

            if (directory == null)
            {
                if (requested)
                    Log.w (TAG, "no external files directory; diagnostics cannot be written");

                return false;
            }

            if (! requested)
                return false;

            synchronized (lock)
            {
                logFile = new File (directory, LOG_NAME);
                enabled = true;
            }

            try
            {
                // The library is already loaded: com.rmsl.juce.Java's static
                // initialiser runs from JuceApp.onCreate, before this activity.
                setLogPath (logFile.getAbsolutePath());
            }
            catch (Throwable error)
            {
                // No native half is still a Java half: the lifecycle lines are
                // what says whether the window was ever reached.
                Log.w (TAG, "the native trace could not be attached", error);
            }

            installCrashHandler();

            note ("==============================");
            note ("device: " + Build.MANUFACTURER + " " + Build.MODEL
                  + ", Android " + Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")");
            note ("log: " + logFile.getAbsolutePath());
            Log.i (TAG, "diagnostics on, writing " + logFile.getAbsolutePath());

            return true;
        }
        catch (Throwable error)
        {
            // Diagnosing the app must never be the reason it fails to start.
            Log.w (TAG, "could not prepare the diagnostic file", error);
            return false;
        }
    }

    public static boolean isEnabled()
    {
        synchronized (lock)
        {
            return enabled;
        }
    }

    /** One timestamped line. Also mirrored to logcat, so `adb logcat` shows the
        same sequence when pulling the file is inconvenient. */
    public static void note (String text)
    {
        if (text == null)
            return;

        final long elapsed = System.currentTimeMillis() - startedAt;
        final String line = String.format (Locale.US, "%s +%dms %s", stamp(), elapsed, text);

        Log.i (TAG, line);

        synchronized (lock)
        {
            if (! enabled || logFile == null)
                return;

            append (logFile, line);
        }
    }

    private static void installCrashHandler()
    {
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler (new Thread.UncaughtExceptionHandler()
        {
            @Override
            public void uncaughtException (Thread thread, Throwable error)
            {
                try
                {
                    final StringWriter trace = new StringWriter();
                    error.printStackTrace (new PrintWriter (trace));
                    note ("UNCAUGHT on " + thread.getName() + ": " + trace);
                }
                catch (Throwable ignored)
                {
                }

                if (previous != null)
                    previous.uncaughtException (thread, error);
            }
        });
    }

    private static String stamp()
    {
        return new SimpleDateFormat ("HH:mm:ss.SSS", Locale.US).format (new Date());
    }

    private static void append (File file, String line)
    {
        try (FileOutputStream out = new FileOutputStream (file, true))
        {
            out.write ((line + "\n").getBytes ("UTF-8"));
        }
        catch (IOException error)
        {
            Log.w (TAG, "could not append to " + file.getAbsolutePath(), error);
        }
    }

}
