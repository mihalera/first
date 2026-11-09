# Diagnostics

The two reports this exists for — “the Android app never opens without the
microphone permission” and “the DAW hangs when the project closes” — need evidence
from a machine we cannot attach a debugger to, so the plugin can be told, without
being rebuilt, to write down what it did; on the desktop that is
`%APPDATA%\MiBoX\diagnostics\shutdown.log` on Windows (`~/Library/Application
Support/MiBoX/diagnostics/` on macOS, `~/.config/MiBoX/diagnostics/` on Linux),
which holds the startup and shutdown phase trace and is written only after
`setx MIBOX_DIAG 1` (or `MIBOX_DIAG=1` in the environment of the process that
loads the plugin, so the DAW has to be restarted) — and if the teardown stops for
more than two seconds, `shutdown.log.stacks.txt` beside it carries the last phase
reached plus the stacks of the message, GL and audio threads, captured on Windows
with DbgHelp (other platforms list the thread ids and say so), never forcing or
aborting anything so the stuck process stays as stuck as it was; on Android the
switch is the launch extra `adb shell am start -n com.mcsmes.mibox/.MainActivity
--ez miboxDiag true`, and everything from process start to the first painted frame
— lifecycle callbacks, the permission dialog's request and result, any uncaught
Java exception and the native startup steps — lands in `mibox-start.log` in the
app's own external files directory (`/sdcard/Android/data/com.mcsmes.mibox/files/`,
pulled with `adb pull`), whose absolute path is printed to logcat under the tag
`MiBoXDiag`; a launch without the extra deletes the marker file that switched the
native half on (`mibox-diag.on`, in the same directory) so an ordinary start
leaves no trace behind, and with the switch off every call site is one cached flag
check and no file, no thread and no timing change; `tests/diagnostics/run.sh`
compiles `Source/Diagnostics/DiagCore.cpp` and `ShutdownWatchdog.cpp` as they ship
and runs the three states — off, on-and-finished, on-and-stalled — asserting the
files that are promised appear and that the stalled teardown produces a phase
trace naming the step it stopped at.
