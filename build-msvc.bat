@echo off
setlocal

where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: Run this from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)

where rc >nul 2>nul
if errorlevel 1 (
  echo ERROR: Windows SDK Resource Compiler ^(rc.exe^) was not found.
  exit /b 1
)

if not exist build mkdir build

pushd src
rc /nologo /fo ..\build\app.res app.rc
if errorlevel 1 goto :failed

cl /nologo /std:c++17 /O2 /MT /EHsc /DUNICODE /D_UNICODE ^
  /Fo..\build\ /Fe:..\Dayaah_RealTone_Killswitch.exe ^
  main.cpp ..\build\app.res ^
  /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF ^
  ole32.lib uuid.lib comctl32.lib shell32.lib propsys.lib user32.lib gdi32.lib
if errorlevel 1 goto :failed

popd
echo Built Dayaah_RealTone_Killswitch.exe
exit /b 0

:failed
popd
echo Build failed.
exit /b 1

