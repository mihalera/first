/*
  The one call from Java into the diagnostic trace.

  On Android the file belongs to Diag.java: it is created by the launcher, in the
  app's own external files directory, before JUCE has initialised. So Java tells
  the native half where it is - once, by this exported JNI method - and from then
  on both halves append to the same file.

  Raw JNI rather than JUCE's helpers on purpose: they are internal to the modules
  and reach the context from inside JUCE's own translation units, which a file
  compiled into the app cannot. An exported symbol needs neither, and the library
  is already loaded by the time this can be called (com.rmsl.juce.Java's static
  initialiser runs from JuceApp.onCreate, before any activity).
*/
#if JUCE_ANDROID

#include "DiagCore.h"

#include <jni.h>

extern "C" __attribute__ ((visibility ("default"))) JNIEXPORT void JNICALL
Java_com_mcsmes_mibox_Diag_setLogPath (JNIEnv* env, jclass, jstring path)
{
    if (env == nullptr || path == nullptr)
        return;

    const auto* characters = env->GetStringUTFChars (path, nullptr);

    if (characters == nullptr)
        return;

    j37diag::setEnabled (true);
    j37diag::setLogPath (characters);
    j37diag::beginRun ("android launcher");

    env->ReleaseStringUTFChars (path, characters);
}

#endif
