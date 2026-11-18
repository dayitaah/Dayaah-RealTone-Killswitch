@echo off
setlocal

where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: MinGW-w64 g++ was not found on PATH.
  exit /b 1
)

where windres >nul 2>nul
if errorlevel 1 (
  echo ERROR: MinGW-w64 windres was not found on PATH.
  exit /b 1
)

if not exist build mkdir build

pushd src
windres app.rc -O coff -o ..\build\app.res
if errorlevel 1 goto :failed

g++ -std=c++17 -O2 -s -Wall -Wextra -municode -mwindows ^
  -static -static-libgcc -static-libstdc++ ^
  main.cpp ..\build\app.res -o ..\Dayaah_RealTone_Killswitch.exe ^
  -lole32 -luuid -lcomctl32 -lshell32 -lpropsys -ldwmapi -luxtheme
if errorlevel 1 goto :failed

popd
echo Built Dayaah_RealTone_Killswitch.exe
exit /b 0

:failed
popd
echo Build failed.
exit /b 1

