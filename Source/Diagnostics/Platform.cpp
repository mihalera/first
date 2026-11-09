#include "Platform.h"

#include "DiagCore.h"

#include <juce_core/juce_core.h>

namespace j37diag
{
#if JUCE_ANDROID
namespace
{
    /** The app's own external files directory, asked of the context JUCE already
        holds. Same place Diag.java writes to, computed the same way, so the two
        halves of the startup trace end up in one file without either telling the
        other where it is. */
    juce::File externalFilesDirectory()
    {
        auto* env = juce::getEnv();

        if (env == nullptr)
            return {};

        auto context = juce::getAppContext();

        if (context.get() == nullptr)
            return {};

        auto* contextClass = env->GetObjectClass (context.get());
        auto* getDirectory = env->GetMethodID (contextClass, "getExternalFilesDir",
                                               "(Ljava/lang/String;)Ljava/io/File;");

        if (getDirectory == nullptr)
        {
            env->ExceptionClear();
            return {};
        }

        auto directory = juce::LocalRef<jobject> (
            env->CallObjectMethod (context.get(), getDirectory, static_cast<jstring> (nullptr)));

        if (directory.get() == nullptr)
        {
            env->ExceptionClear();
            return {};
        }

        auto* fileClass = env->GetObjectClass (directory.get());
        auto* getPath = env->GetMethodID (fileClass, "getAbsolutePath", "()Ljava/lang/String;");

        if (getPath == nullptr)
        {
            env->ExceptionClear();
            return {};
        }

        auto path = juce::LocalRef<jstring> (
            static_cast<jstring> (env->CallObjectMethod (directory.get(), getPath)));

        if (path.get() == nullptr)
        {
            env->ExceptionClear();
            return {};
        }

        const auto* characters = env->GetStringUTFChars (path.get(), nullptr);

        if (characters == nullptr)
            return {};

        const juce::String text (juce::CharPointer_UTF8 (characters));
        env->ReleaseStringUTFChars (path.get(), characters);
        return juce::File (text);
    }
} // namespace

std::string diagnosticsFilePath()
{
    return externalFilesDirectory().getChildFile ("mibox-start.log").getFullPathName().toStdString();
}

void beginRunForThisPlatform (const char* where)
{
    const auto directory = externalFilesDirectory();

    // The marker is the switch Java can turn on without the app being rebuilt: it
    // is created when the launcher sees `--ez miboxDiag true` and removed when it
    // is launched without it. The environment variable still works, for a build
    // whose Java half is older than this file.
    const auto markerOn = directory != juce::File()
                          && directory.getChildFile ("mibox-diag.on").existsAsFile();

    if (! markerOn && ! enabled())
        return;

    setEnabled (true);
    setLogPath (diagnosticsFilePath());
    beginRun (where);

    const auto microphoneRequired =
        juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio);
    const auto microphoneGranted =
        juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio);

    std::string text = "android: diagnostics file ";
    text += diagnosticsFilePath();
    text += ", mic permission required=";
    text += microphoneRequired ? "yes" : "no";
    text += " granted=";
    text += microphoneGranted ? "yes" : "no";
    note (text);
}
#else
std::string diagnosticsFilePath()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("MiBoX")
        .getChildFile ("diagnostics")
        .getChildFile ("shutdown.log")
        .getFullPathName()
        .toStdString();
}

void beginRunForThisPlatform (const char* where)
{
    if (! enabled())
        return;

    const juce::File file { juce::String (diagnosticsFilePath()) };
    file.getParentDirectory().createDirectory();

    setLogPath (diagnosticsFilePath());
    beginRun (where);
}
#endif
} // namespace j37diag
