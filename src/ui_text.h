#pragma once

struct UiText {
    const wchar_t* spanish;
    const wchar_t* english;
};

namespace ui {

constexpr UiText language{L"Idioma", L"Language"};
constexpr UiText inputDevice{L"Dispositivo de entrada", L"Input device"};
constexpr UiText refresh{L"Actualizar", L"Refresh"};
constexpr UiText momentaryHotkey{L"Hotkey momentánea", L"Momentary hotkey"};
constexpr UiText changeHotkey{L"Cambiar hotkey", L"Change hotkey"};
constexpr UiText pressKey{L"Presiona una tecla...", L"Press a key..."};
constexpr UiText rocksmithMode{L"Modo Rocksmith: bloquear la hotkey elegida",
                               L"Rocksmith Mode: block the selected hotkey"};
constexpr UiText muteHint{L"Mantén la tecla para silenciar. Suéltala para restaurar el audio.",
                          L"Hold the key to mute. Release it to restore audio."};
constexpr UiText support{L"Apoyar en Ko-fi", L"Support on Ko-fi"};
constexpr UiText hide{L"Ocultar en la bandeja", L"Hide to tray"};
constexpr UiText open{L"Abrir", L"Open"};
constexpr UiText exit{L"Salir", L"Exit"};

constexpr UiText starting{L"Iniciando...", L"Starting..."};
constexpr UiText unknownMute{L"Error: se desconoce el estado de silencio anterior.",
                             L"Error: the previous mute state is unknown."};
constexpr UiText restored{L"Listo. Se restauró el estado de silencio anterior.",
                           L"Ready. Previous mute state restored."};
constexpr UiText restoreFailed{L"Error al restaurar el audio. Reconecta el dispositivo.",
                                L"Couldn't restore audio. Reconnect the device to retry."};
constexpr UiText muted{L"ENTRADA SILENCIADA", L"INPUT MUTED"};
constexpr UiText muteFailed{L"No se pudo silenciar. Reconecta el dispositivo o pulsa Actualizar.",
                             L"Couldn't mute. Reconnect the device or click Refresh."};
constexpr UiText selectDevice{L"Conecta o selecciona un dispositivo de entrada.",
                               L"Connect or select an input device."};
constexpr UiText openDeviceFailed{L"No se pudo abrir el dispositivo. Pulsa Actualizar.",
                                   L"Couldn't open the device. Click Refresh."};
constexpr UiText readyWithLevel{L"Listo. Nivel: %d%%. Mantén la hotkey para silenciar.",
                                 L"Ready. Level: %d%%. Hold the hotkey to mute."};
constexpr UiText ready{L"Listo. Mantén la hotkey para silenciar.",
                        L"Ready. Hold the hotkey to mute."};
constexpr UiText audioUnavailable{L"Windows Audio no está disponible.",
                                    L"Windows Audio is unavailable."};
constexpr UiText enumerateFailed{L"No se pudieron enumerar los dispositivos de entrada.",
                                  L"Couldn't list the input devices."};
constexpr UiText noInputDevice{L"No hay dispositivos de entrada activos.",
                                L"No active input devices."};
constexpr UiText releaseHotkey{L"Suelta primero la hotkey actual.",
                               L"Release the current hotkey first."};
constexpr UiText waitingForKey{L"Esperando una tecla. Pulsa Escape para cancelar.",
                                L"Waiting for a key. Press Escape to cancel."};
constexpr UiText hotkeyChanged{L"Hotkey cambiada. Mantenla presionada para silenciar.",
                                L"Hotkey changed. Hold it to mute."};
constexpr UiText captureCanceled{L"Cambio cancelado. Killswitch listo.",
                                  L"Change canceled. Killswitch ready."};
constexpr UiText captureCanceledNoDevice{L"Cambio cancelado. Selecciona un dispositivo de entrada.",
                                          L"Change canceled. Select an input device."};
constexpr UiText rocksmithModeOn{L"Modo Rocksmith activo. La hotkey se bloquea en el juego.",
                                  L"Rocksmith Mode on. The hotkey is blocked in-game."};
constexpr UiText globalModeOn{L"Modo global activo. La hotkey sigue disponible para otras apps.",
                               L"Global mode on. Other apps still receive the hotkey."};
constexpr UiText physicalKey{L"Pulsa otra tecla física.", L"Press a different physical key."};
constexpr UiText reconnected{L"Listo. Dispositivo reconectado automáticamente.",
                              L"Ready. Input device reconnected automatically."};
constexpr UiText cutTimeout{L"Silencio liberado automáticamente tras 30 segundos.",
                             L"Mute released automatically after 30 seconds."};

constexpr UiText comError{L"No se pudo iniciar Windows Audio (COM).",
                          L"Couldn't initialize Windows Audio (COM)."};
constexpr UiText windowError{L"No se pudo crear la ventana.", L"Couldn't create the window."};
constexpr UiText hookError{L"No se pudo activar la hotkey global.",
                            L"Couldn't enable the global hotkey."};

}  // namespace ui
