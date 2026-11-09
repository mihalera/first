#include "Platform.h"

#include "DiagCore.h"

#include <juce_core/juce_core.h>

namespace j37diag
{
#if JUCE_ANDROID
std::string diagnosticsFilePath()
{
    // The launcher owns the path and has already passed it in - see
    // Source/Diagnostics/AndroidBridge.cpp - so there is nothing to resolve here.
    return logPath();
}

void beginRunForThisPlatform (const char* where)
{
    // Java is the only half that can see the switch on this platform: it reads the
    // launch extra and, when it is on, calls setLogPath(), which is what enables
    // the native lines below. Until that call this does nothing at all.
    if (! enabled())
        return;

    beginRun (where);

    const auto microphoneRequired =
        juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio);
    const auto microphoneGranted =
        juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio);

    std::string text = "android: native startup, mic permission required=";
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
