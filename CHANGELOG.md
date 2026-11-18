# Changelog

All notable changes to Dayaah RealTone Killswitch are documented here.

## [1.3.3] - 2026-11-18

### Added

- Spanish and English UI with a saved language selector that updates the
  window, tray menu, status messages, and errors.
- Dark interface styled after Dayaah Capture with the version visible in the
  window and tray tooltip.

## [1.3.2] - 2026-11-10

### Fixed

- A disconnected endpoint is reopened automatically every three seconds after
  failure, so the hotkey recovers without pressing **Actualizar**.
- Clicking a button or checkbox no longer leaves keyboard focus on that
  control, preventing Space from activating it again in the app window.
- Upgrades from v1.2.0 preserve AltGr when the old INI has no explicit
  `HotkeyVK`; genuinely new installations still default to Space.

## [1.3.1] - 2026-11-10

### Fixed

- The failsafe no longer relies on `GetAsyncKeyState` for a swallowed hotkey.
- Space and Enter are fully consumed while assigning a new hotkey, preventing
  the focused capture button from reopening itself.
- Restoring Mute now checks the Windows Audio result, reopens the selected
  endpoint once, and retries periodically instead of reporting false success.
- A queued DOWN is ignored after the hook has already observed its UP.
- Once an endpoint is reported unavailable, the selected hotkey passes
  through until the endpoint can be opened again.

### Added

- Dedicated keyboard-hook thread with atomic shared state.
- Recovery marker for restoring the pre-cut Mute state after a forced exit.
- Thirty-second watchdog and stale-key-up recovery for blocked hotkeys.

### Clarified

- Suppression always follows the currently selected hotkey. Space is not
  hardcoded and works normally as soon as another hotkey is selected.

## [1.3.0] - 2026-11-08

### Added

- Rocksmith Mode, enabled by default and persisted in the INI file.
- Foreground-process verification for `Rocksmith2014.exe`.
- Hotkey suppression inside Rocksmith, allowing Space to work as a killswitch
  without opening Riff Repeater.
- Focus-loss protection that restores audio immediately while still consuming
  the pending key-up event.

### Changed

- Space is the default hotkey for new installations. Existing saved hotkeys are
  preserved.
- Keyboard-hook audio work is dispatched to the window message loop so the
  low-level hook can return promptly.

## [1.2.0] - 2026-11-01

### Added

- Clickable GitHub and Ko-fi buttons with embedded icons.
- Visual-style manifest and public-release metadata.
- Visual Studio, MinGW-w64, GitHub Actions, and tagged-release builds.

### Preserved

- Momentary endpoint Mute while the selected hotkey is held.
- Exact restoration of the previous Mute state on release.
- Input/calibration level remains untouched.

## [1.1.0] - 2026-11-01

### Changed

- Replaced minimum input-volume control with real Windows endpoint Mute.
- The calibrated Real Tone Cable level is no longer modified.

## [1.0.0] - 2026-10-31

### Added

- First working prototype.
- Automatic Real Tone Cable detection and manual device selector.
- Configurable global keyboard hotkey.
- Notification-area operation and lost-key-up failsafe.

