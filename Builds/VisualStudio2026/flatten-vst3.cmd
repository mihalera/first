@echo off
setlocal

set "outputDir=%~1"
if not "%outputDir:~-1%"=="\\" set "outputDir=%outputDir%\\"
set "bundle=%outputDir%first.vst3"
set "binary=%bundle%\Contents\x86_64-win\first.vst3"
set "temporary=%outputDir%first-flat-vst3.tmp"

if not exist "%binary%" exit /b 0

copy /Y "%binary%" "%temporary%" >nul
if errorlevel 1 exit /b 1

rmdir /S /Q "%bundle%"
if errorlevel 1 exit /b 1

move /Y "%temporary%" "%bundle%" >nul
if errorlevel 1 exit /b 1

exit /b 0
