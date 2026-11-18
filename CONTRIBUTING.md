# Contributing

Bug reports and focused pull requests are welcome in English or Spanish.

## Before opening an issue

1. Confirm you are using the latest release.
2. Close every older instance from the notification area.
3. Include your Windows version, Rocksmith version, cable/device name, chosen
   hotkey, and exact reproduction steps.

## Building

Use `build-msvc.bat` from an x64 Native Tools Command Prompt for Visual Studio,
or `build-mingw.bat` with MinGW-w64 on `PATH`.

## Pull requests

- Keep the application native, portable, local, and dependency-light.
- Do not add telemetry, advertising, accounts, background downloads, or an
  installer.
- Preserve the selected endpoint's previous Mute state.
- Never change the user's calibrated input level.
- Test press, hold, release, repeated presses, app exit while held, and the
  notification-area lifecycle.
- Describe user-visible changes in `CHANGELOG.md`.

By contributing, you agree that your contribution may be distributed under
the repository's MIT License.

