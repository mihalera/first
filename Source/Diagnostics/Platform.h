/*
  Where the diagnostic files land, and how the switch is read on each platform.

  DiagCore knows how to write lines but not where a plugin should put them: that
  needs JUCE's special folders on the desktop and the app's own external storage
  directory on Android, which is also where Diag.java writes. This is the only
  file that knows either path, so the answer to "where do I look" is in one place.
*/
#pragma once

#include <string>

namespace j37diag
{
    /** Resolve the file for this platform, read the switch, and start the run.
        Safe to call more than once: only the first call writes the header.

        Desktop: on when MIBOX_DIAG is 1 (or y/t) in the environment.
        Android: on when Diag.java created the marker after being started with
        `--ez miboxDiag true`; the marker is what the native side can see. */
    void beginRunForThisPlatform (const char* where);

    /** The file the plugin writes to on this platform. Empty on Android before
        storage is available. */
    std::string diagnosticsFilePath();
} // namespace j37diag
