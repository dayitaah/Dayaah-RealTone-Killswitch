#define NOMINMAX
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <windowsx.h>
#include <initguid.h>
#include <commctrl.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

#include "resource.h"

namespace {

constexpr wchar_t kClassName[] = L"DayaahRealToneKillswitchWindow";
constexpr wchar_t kWindowTitle[] = L"Dayaah RealTone Killswitch";
constexpr wchar_t kMutexName[] = L"Local\\DayaahRealToneKillswitch_1_0";

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_CAPTURED_KEY = WM_APP + 2;
constexpr UINT WM_SHOW_APP = WM_APP + 3;
constexpr UINT WM_HOTKEY_DOWN = WM_APP + 4;
constexpr UINT WM_HOTKEY_UP = WM_APP + 5;

constexpr UINT_PTR TIMER_FAILSAFE = 1;
constexpr UINT_PTR TIMER_CAPTURE_CTRL = 2;
constexpr UINT_PTR TIMER_ENDPOINT_RETRY = 3;
constexpr UINT TRAY_ID = 1;
constexpr UINT FAILSAFE_INTERVAL_MS = 50;
constexpr UINT ENDPOINT_RETRY_INTERVAL_MS = 3000;
constexpr ULONGLONG MAX_SUPPRESSED_HOLD_MS = 30000;
constexpr ULONGLONG RESTORE_RETRY_MS = 1000;

enum ControlId : int {
    IDC_DEVICE_COMBO = 1001,
    IDC_REFRESH = 1002,
    IDC_HOTKEY_VALUE = 1003,
    IDC_CAPTURE_HOTKEY = 1004,
    IDC_STATUS = 1005,
    IDC_HIDE = 1006,
    IDC_EXIT = 1007,
    IDC_GITHUB = 1008,
    IDC_KOFI = 1009,
    IDC_ROCKSMITH_MODE = 1010,
};

struct AudioDevice {
    std::wstring id;
    std::wstring name;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_deviceCombo = nullptr;
HWND g_hotkeyValue = nullptr;
HWND g_captureButton = nullptr;
HWND g_rocksmithModeCheck = nullptr;
HWND g_status = nullptr;
HFONT g_font = nullptr;
HANDLE g_mutex = nullptr;
HANDLE g_keyboardThread = nullptr;
HANDLE g_keyboardReadyEvent = nullptr;
DWORD g_keyboardThreadId = 0;
std::atomic<HHOOK> g_keyboardHook{nullptr};
std::atomic<HWND> g_blockedRocksmithWindow{nullptr};
HIMAGELIST g_githubImageList = nullptr;
HIMAGELIST g_kofiImageList = nullptr;

IMMDeviceEnumerator* g_enumerator = nullptr;
IAudioEndpointVolume* g_endpointVolume = nullptr;
std::vector<AudioDevice> g_devices;

std::wstring g_iniPath;
std::wstring g_selectedDeviceId;
std::atomic<DWORD> g_hotkeyVk{VK_SPACE};
std::atomic_bool g_captureMode{false};
bool g_pendingLeftCtrlCapture = false;
bool g_cutActive = false;
bool g_restoreValid = false;
BOOL g_restoreMute = FALSE;
std::atomic_bool g_rocksmithMode{true};
std::atomic_bool g_hotkeyHeld{false};
std::atomic_bool g_suppressHotkeyRelease{false};
std::atomic_bool g_ignoreHotkeyUntilRelease{false};
std::atomic_bool g_endpointReady{false};
std::atomic<DWORD> g_captureSwallowVk{0};
std::atomic<ULONGLONG> g_hotkeyStartTick{0};
ULONGLONG g_lastRestoreAttemptTick = 0;

NOTIFYICONDATAW g_tray{};

template <typename T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool LooksLikeRealTone(const std::wstring& name) {
    const std::wstring lower = Lower(name);
    return lower.find(L"rocksmith") != std::wstring::npos ||
           lower.find(L"real tone") != std::wstring::npos ||
           lower.find(L"realtone") != std::wstring::npos ||
           lower.find(L"usb guitar adapter") != std::wstring::npos;
}

std::wstring ConfigPath() {
    wchar_t appData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) {
        std::wstring directory = std::wstring(appData) + L"\\Dayaah";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\RealToneKillswitch.ini";
    }
    return L"RealToneKillswitch.ini";
}

void LoadConfig() {
    g_iniPath = ConfigPath();

    const DWORD attributes = GetFileAttributesW(g_iniPath.c_str());
    const bool existingConfig =
        attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;

    wchar_t hotkeyText[32]{};
    GetPrivateProfileStringW(L"Settings", L"HotkeyVK", L"", hotkeyText,
                             static_cast<DWORD>(std::size(hotkeyText)), g_iniPath.c_str());

    // v1.2.0 used AltGr by default but only wrote HotkeyVK after a manual change.
    // Preserve that default for an existing INI with no key. A genuinely new
    // installation starts with Space, then persists the decision immediately.
    DWORD hotkey = existingConfig ? VK_RMENU : VK_SPACE;
    if (hotkeyText[0]) {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(hotkeyText, &end, 10);
        if (end != hotkeyText && *end == L'\0' && parsed > 0 && parsed <= 0xFF) {
            hotkey = static_cast<DWORD>(parsed);
        }
    }
    g_hotkeyVk.store(hotkey);

    if (!hotkeyText[0]) {
        wchar_t value[32]{};
        wsprintfW(value, L"%lu", static_cast<unsigned long>(hotkey));
        WritePrivateProfileStringW(L"Settings", L"HotkeyVK", value, g_iniPath.c_str());
    }

    g_rocksmithMode.store(
        GetPrivateProfileIntW(L"Settings", L"RocksmithMode", 1, g_iniPath.c_str()) != 0);

    wchar_t deviceId[1024]{};
    GetPrivateProfileStringW(L"Settings", L"DeviceId", L"", deviceId,
                             static_cast<DWORD>(std::size(deviceId)), g_iniPath.c_str());
    g_selectedDeviceId = deviceId;
}

void SaveHotkey() {
    wchar_t value[32]{};
    wsprintfW(value, L"%lu", static_cast<unsigned long>(g_hotkeyVk.load()));
    WritePrivateProfileStringW(L"Settings", L"HotkeyVK", value, g_iniPath.c_str());
}

void SaveDevice() {
    WritePrivateProfileStringW(L"Settings", L"DeviceId", g_selectedDeviceId.c_str(),
                               g_iniPath.c_str());
}

void SaveRocksmithMode() {
    WritePrivateProfileStringW(L"Settings", L"RocksmithMode",
                               g_rocksmithMode.load() ? L"1" : L"0", g_iniPath.c_str());
}

bool IsRocksmithWindow(HWND window) {
    if (!window) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return false;
    }

    wchar_t path[1024]{};
    DWORD pathLength = static_cast<DWORD>(std::size(path));
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &pathLength) != FALSE;
    CloseHandle(process);
    if (!queried) {
        return false;
    }

    const wchar_t* filename = std::wcsrchr(path, L'\\');
    filename = filename ? filename + 1 : path;
    return _wcsicmp(filename, L"Rocksmith2014.exe") == 0;
}

bool IsExtendedKey(DWORD vk) {
    switch (vk) {
        case VK_RMENU:
        case VK_RCONTROL:
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
            return true;
        default:
            return false;
    }
}

std::wstring KeyName(DWORD vk) {
    switch (vk) {
        case VK_RMENU: return L"Alt derecho (AltGr)";
        case VK_LMENU: return L"Alt izquierdo";
        case VK_RCONTROL: return L"Ctrl derecho";
        case VK_LCONTROL: return L"Ctrl izquierdo";
        case VK_RSHIFT: return L"Shift derecho";
        case VK_LSHIFT: return L"Shift izquierdo";
        case VK_SPACE: return L"Barra espaciadora";
        case VK_CAPITAL: return L"Bloq Mayus";
        case VK_ESCAPE: return L"Escape";
        case VK_RETURN: return L"Enter";
        case VK_TAB: return L"Tab";
        case VK_BACK: return L"Retroceso";
        default: break;
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG keyData = static_cast<LONG>(scan << 16);
    if (IsExtendedKey(vk)) {
        keyData |= (1 << 24);
    }
    wchar_t name[128]{};
    if (GetKeyNameTextW(keyData, name, static_cast<int>(std::size(name))) > 0) {
        return name;
    }
    wchar_t fallback[32]{};
    wsprintfW(fallback, L"Tecla VK %lu", static_cast<unsigned long>(vk));
    return fallback;
}

void SetStatus(const std::wstring& text) {
    if (g_status) {
        SetWindowTextW(g_status, text.c_str());
        InvalidateRect(g_status, nullptr, TRUE);
    }
}

void RefreshHotkeyLabel() {
    if (g_hotkeyValue) {
        SetWindowTextW(g_hotkeyValue, KeyName(g_hotkeyVk.load()).c_str());
    }
}

void FlushConfig() {
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath.c_str());
}

void SaveRecoveryMarker(BOOL restoreMute) {
    WritePrivateProfileStringW(L"Recovery", L"DeviceId", g_selectedDeviceId.c_str(),
                               g_iniPath.c_str());
    WritePrivateProfileStringW(L"Recovery", L"RestoreMute",
                               restoreMute ? L"1" : L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Recovery", L"Pending", L"1", g_iniPath.c_str());
    FlushConfig();
}

void ClearRecoveryMarker() {
    WritePrivateProfileStringW(L"Recovery", nullptr, nullptr, g_iniPath.c_str());
    FlushConfig();
}

bool OpenEndpointById(const std::wstring& id) {
    g_endpointReady.store(false);
    SafeRelease(g_endpointVolume);
    if (!g_enumerator || id.empty()) {
        return false;
    }

    IMMDevice* device = nullptr;
    HRESULT result = g_enumerator->GetDevice(id.c_str(), &device);
    if (SUCCEEDED(result)) {
        result = device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&g_endpointVolume));
    }
    SafeRelease(device);

    if (FAILED(result) || !g_endpointVolume) {
        SafeRelease(g_endpointVolume);
        return false;
    }

    g_endpointReady.store(true);
    if (g_window) {
        KillTimer(g_window, TIMER_ENDPOINT_RETRY);
    }
    return true;
}

void ScheduleEndpointRetry() {
    if (g_window && !g_selectedDeviceId.empty()) {
        SetTimer(g_window, TIMER_ENDPOINT_RETRY, ENDPOINT_RETRY_INTERVAL_MS, nullptr);
    }
}

void RecoverPendingAudioState() {
    if (GetPrivateProfileIntW(L"Recovery", L"Pending", 0, g_iniPath.c_str()) == 0 ||
        !g_enumerator) {
        return;
    }

    wchar_t deviceId[1024]{};
    GetPrivateProfileStringW(L"Recovery", L"DeviceId", L"", deviceId,
                             static_cast<DWORD>(std::size(deviceId)), g_iniPath.c_str());
    if (!deviceId[0]) {
        ClearRecoveryMarker();
        return;
    }

    IMMDevice* device = nullptr;
    IAudioEndpointVolume* endpoint = nullptr;
    HRESULT result = g_enumerator->GetDevice(deviceId, &device);
    if (SUCCEEDED(result)) {
        result = device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&endpoint));
    }
    if (SUCCEEDED(result) && endpoint) {
        const BOOL restoreMute =
            GetPrivateProfileIntW(L"Recovery", L"RestoreMute", 0, g_iniPath.c_str()) != 0;
        result = endpoint->SetMute(restoreMute, &GUID_NULL);
    }
    SafeRelease(endpoint);
    SafeRelease(device);
    if (SUCCEEDED(result)) {
        ClearRecoveryMarker();
    }
}

bool RestoreAudioState() {
    if (!g_cutActive) {
        return true;
    }

    if (!g_restoreValid) {
        SetStatus(L"ERROR: no conozco el estado anterior del Mute.");
        return false;
    }

    HRESULT result = g_endpointVolume
                         ? g_endpointVolume->SetMute(g_restoreMute, &GUID_NULL)
                         : E_POINTER;
    if (FAILED(result) && OpenEndpointById(g_selectedDeviceId)) {
        result = g_endpointVolume->SetMute(g_restoreMute, &GUID_NULL);
    }

    if (SUCCEEDED(result)) {
        ClearRecoveryMarker();
        g_endpointReady.store(true);
        SetStatus(L"LISTO - estado de Mute restaurado.");
        g_cutActive = false;
        g_restoreValid = false;
        g_lastRestoreAttemptTick = 0;
        return true;
    }

    g_lastRestoreAttemptTick = GetTickCount64();
    g_endpointReady.store(false);
    SetStatus(L"ERROR: no pude restaurar el Mute. Reconecta el cable; reintentare.");
    return false;
}

bool EngageCut() {
    if (g_cutActive || g_captureMode.load()) {
        return g_cutActive;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        if ((!g_endpointVolume || !g_endpointReady.load()) &&
            !OpenEndpointById(g_selectedDeviceId)) {
            break;
        }

        BOOL currentMute = FALSE;
        HRESULT result = g_endpointVolume->GetMute(&currentMute);
        if (SUCCEEDED(result)) {
            result = g_endpointVolume->SetMute(TRUE, &GUID_NULL);
        }

        if (SUCCEEDED(result)) {
            g_restoreMute = currentMute;
            g_restoreValid = true;
            g_cutActive = true;
            SaveRecoveryMarker(currentMute);
            SetStatus(L"CORTE ACTIVO - MUTE");
            return true;
        }

        g_endpointReady.store(false);
        SafeRelease(g_endpointVolume);
    }

    g_restoreValid = false;
    SetStatus(L"No pude activar Mute. Reconecta el cable o pulsa Actualizar.");
    ScheduleEndpointRetry();
    return false;
}

bool ReleaseAudioEndpoint() {
    const bool restored = RestoreAudioState();
    g_endpointReady.store(false);
    SafeRelease(g_endpointVolume);
    return restored;
}

bool ActivateDevice(const std::wstring& id, bool persist) {
    if (!ReleaseAudioEndpoint() && g_cutActive) {
        return false;
    }
    if (!g_enumerator || id.empty()) {
        SetStatus(L"Conecta o selecciona el Rocksmith Real Tone Cable.");
        return false;
    }

    if (!OpenEndpointById(id)) {
        SetStatus(L"No pude abrir ese dispositivo. Pulsa Actualizar.");
        return false;
    }

    g_selectedDeviceId = id;
    if (persist) {
        SaveDevice();
    }

    float current = 0.0f;
    if (SUCCEEDED(g_endpointVolume->GetMasterVolumeLevelScalar(&current))) {
        const int percent = static_cast<int>(std::lround(current * 100.0f));
        SetStatus(L"LISTO - nivel actual " + std::to_wstring(percent) + L". Manten la hotkey para cortar.");
    } else {
        SetStatus(L"LISTO - manten la hotkey para cortar.");
    }
    return true;
}

void EnumerateDevices() {
    ReleaseAudioEndpoint();
    if (!g_cutActive) {
        RecoverPendingAudioState();
    }
    g_devices.clear();
    SendMessageW(g_deviceCombo, CB_RESETCONTENT, 0, 0);

    if (!g_enumerator) {
        SetStatus(L"Windows Audio no esta disponible.");
        return;
    }

    IMMDeviceCollection* collection = nullptr;
    if (FAILED(g_enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection)) ||
        !collection) {
        SetStatus(L"No pude enumerar los dispositivos de entrada.");
        return;
    }

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device) {
            continue;
        }

        LPWSTR rawId = nullptr;
        IPropertyStore* properties = nullptr;
        PROPVARIANT nameValue;
        PropVariantInit(&nameValue);

        std::wstring id;
        std::wstring name = L"Dispositivo de entrada";
        if (SUCCEEDED(device->GetId(&rawId)) && rawId) {
            id = rawId;
        }
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) && properties &&
            SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &nameValue)) &&
            nameValue.vt == VT_LPWSTR && nameValue.pwszVal) {
            name = nameValue.pwszVal;
        }

        if (!id.empty()) {
            g_devices.push_back({id, name});
            SendMessageW(g_deviceCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }

        PropVariantClear(&nameValue);
        if (rawId) CoTaskMemFree(rawId);
        SafeRelease(properties);
        SafeRelease(device);
    }
    SafeRelease(collection);

    int selected = -1;
    for (size_t i = 0; i < g_devices.size(); ++i) {
        if (!g_selectedDeviceId.empty() && g_devices[i].id == g_selectedDeviceId) {
            selected = static_cast<int>(i);
            break;
        }
    }
    if (selected < 0) {
        for (size_t i = 0; i < g_devices.size(); ++i) {
            if (LooksLikeRealTone(g_devices[i].name)) {
                selected = static_cast<int>(i);
                break;
            }
        }
    }

    if (selected >= 0) {
        SendMessageW(g_deviceCombo, CB_SETCURSEL, selected, 0);
        ActivateDevice(g_devices[static_cast<size_t>(selected)].id, true);
    } else if (g_devices.empty()) {
        SetStatus(L"No hay dispositivos de entrada activos.");
        ScheduleEndpointRetry();
    } else {
        SetStatus(L"Selecciona el Rocksmith Real Tone Cable en la lista.");
        ScheduleEndpointRetry();
    }
}

void BeginHotkeyCapture() {
    if (g_hotkeyHeld.load() || g_suppressHotkeyRelease.load()) {
        SetStatus(L"Suelta primero la hotkey actual.");
        return;
    }
    g_hotkeyHeld.store(false);
    g_suppressHotkeyRelease.store(false);
    g_ignoreHotkeyUntilRelease.store(false);
    g_blockedRocksmithWindow.store(nullptr);
    g_captureSwallowVk.store(0);
    if (!RestoreAudioState()) {
        SetTimer(g_window, TIMER_FAILSAFE, FAILSAFE_INTERVAL_MS, nullptr);
        return;
    }
    g_pendingLeftCtrlCapture = false;
    KillTimer(g_window, TIMER_CAPTURE_CTRL);
    g_captureMode.store(true);
    SetWindowTextW(g_captureButton, L"Presiona una tecla...");
    SetStatus(L"Esperando nueva hotkey (Escape cancela)...");
}

void FinishHotkeyCapture(DWORD vk) {
    KillTimer(g_window, TIMER_CAPTURE_CTRL);
    g_pendingLeftCtrlCapture = false;
    g_captureMode.store(false);
    SetWindowTextW(g_captureButton, L"Cambiar hotkey");
    if (vk != VK_ESCAPE) {
        g_hotkeyVk.store(vk);
        SaveHotkey();
        RefreshHotkeyLabel();
        SetStatus(L"Hotkey cambiada. Mantenla presionada para cortar.");
    } else {
        SetStatus(g_endpointVolume ? L"Cambio cancelado. Killswitch listo."
                                   : L"Cambio cancelado. Selecciona el RealTone Cable.");
    }
}

void ShowMainWindow() {
    ShowWindow(g_window, SW_SHOW);
    ShowWindow(g_window, SW_RESTORE);
    SetForegroundWindow(g_window);
}

void HideMainWindow() {
    ShowWindow(g_window, SW_HIDE);
}

void AddTrayIcon() {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = TRAY_ID;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAYICON;
    g_tray.hIcon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                 0, 0, LR_DEFAULTSIZE));
    if (!g_tray.hIcon) {
        g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    lstrcpynW(g_tray.szTip, L"Dayaah RealTone Killswitch", static_cast<int>(std::size(g_tray.szTip)));
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
}

void RemoveTrayIcon() {
    if (g_tray.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
    }
}

void ShowTrayMenu() {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Abrir");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"Salir");
    SetForegroundWindow(g_window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         point.x, point.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    PostMessageW(g_window, WM_NULL, 0, 0);
    if (command == 1) ShowMainWindow();
    if (command == 2) DestroyWindow(g_window);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        const DWORD captureSwallow = g_captureSwallowVk.load();
        if (captureSwallow && key->vkCode == captureSwallow) {
            if (up) {
                g_captureSwallowVk.store(0);
            }
            return 1;
        }

        if (g_captureMode.load() && down) {
            // Swallow both halves of the captured key. Otherwise Space or Enter can click
            // the still-focused "Cambiar hotkey" button and reopen capture immediately.
            g_captureSwallowVk.store(key->vkCode);
            if (!PostMessageW(g_window, WM_CAPTURED_KEY,
                              static_cast<WPARAM>(key->vkCode), 0)) {
                g_captureSwallowVk.store(0);
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }
            return 1;
        }

        const DWORD hotkey = g_hotkeyVk.load();
        if (key->vkCode != hotkey) {
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        if (g_ignoreHotkeyUntilRelease.load()) {
            const bool suppress = g_suppressHotkeyRelease.load();
            if (up) {
                g_ignoreHotkeyUntilRelease.store(false);
                g_hotkeyHeld.store(false);
                g_suppressHotkeyRelease.store(false);
                g_blockedRocksmithWindow.store(nullptr);
                PostMessageW(g_window, WM_HOTKEY_UP, 0, 0);
            }
            return suppress ? 1 : CallNextHookEx(nullptr, code, wParam, lParam);
        }

        if (down) {
            const bool rocksmithMode = g_rocksmithMode.load();
            bool suppress = g_suppressHotkeyRelease.load();

            if (!suppress && !g_endpointReady.load()) {
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }

            if (rocksmithMode && !suppress) {
                HWND foreground = GetForegroundWindow();
                if (!IsRocksmithWindow(foreground)) {
                    return CallNextHookEx(nullptr, code, wParam, lParam);
                }
                g_blockedRocksmithWindow.store(foreground);
                g_suppressHotkeyRelease.store(true);
                suppress = true;
            }

            bool expected = false;
            if (g_hotkeyHeld.compare_exchange_strong(expected, true)) {
                g_hotkeyStartTick.store(GetTickCount64());
                if (!PostMessageW(g_window, WM_HOTKEY_DOWN, 0, 0)) {
                    g_hotkeyHeld.store(false);
                    g_suppressHotkeyRelease.store(false);
                    g_blockedRocksmithWindow.store(nullptr);
                    return CallNextHookEx(nullptr, code, wParam, lParam);
                }
            }

            if (suppress) {
                return 1;
            }
        }

        if (up) {
            const bool suppress = g_suppressHotkeyRelease.exchange(false);
            const bool held = g_hotkeyHeld.exchange(false);
            g_ignoreHotkeyUntilRelease.store(false);
            g_blockedRocksmithWindow.store(nullptr);
            if (held || suppress) {
                PostMessageW(g_window, WM_HOTKEY_UP, 0, 0);
            }
            if (suppress) {
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI KeyboardThreadProc(void*) {
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, g_instance, 0);
    g_keyboardHook.store(hook);
    if (g_keyboardReadyEvent) {
        SetEvent(g_keyboardReadyEvent);
    }
    if (!hook) {
        return 1;
    }

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnhookWindowsHookEx(hook);
    g_keyboardHook.store(nullptr);
    return 0;
}

bool StartKeyboardHookThread() {
    g_keyboardReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_keyboardReadyEvent) {
        return false;
    }

    g_keyboardThread = CreateThread(nullptr, 0, KeyboardThreadProc, nullptr, 0,
                                    &g_keyboardThreadId);
    if (!g_keyboardThread) {
        CloseHandle(g_keyboardReadyEvent);
        g_keyboardReadyEvent = nullptr;
        return false;
    }

    const DWORD wait = WaitForSingleObject(g_keyboardReadyEvent, 5000);
    if (wait != WAIT_OBJECT_0 || !g_keyboardHook.load()) {
        PostThreadMessageW(g_keyboardThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_keyboardThread, 5000);
        CloseHandle(g_keyboardReadyEvent);
        g_keyboardReadyEvent = nullptr;
        CloseHandle(g_keyboardThread);
        g_keyboardThread = nullptr;
        g_keyboardThreadId = 0;
        return false;
    }
    CloseHandle(g_keyboardReadyEvent);
    g_keyboardReadyEvent = nullptr;
    return true;
}

void StopKeyboardHookThread() {
    if (!g_keyboardThread) {
        return;
    }
    PostThreadMessageW(g_keyboardThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_keyboardThread, 5000);
    CloseHandle(g_keyboardThread);
    g_keyboardThread = nullptr;
    g_keyboardThreadId = 0;
}

void ApplyFont(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

HWND MakeControl(const wchar_t* className, const wchar_t* text, DWORD style,
                 int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                    x, y, width, height, g_window,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    g_instance, nullptr);
    ApplyFont(control);
    return control;
}

HIMAGELIST AddButtonIcon(HWND button, int resourceId) {
    HICON icon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(resourceId),
                                               IMAGE_ICON, 20, 20, LR_DEFAULTCOLOR));
    if (!icon) return nullptr;

    HIMAGELIST images = ImageList_Create(20, 20, ILC_COLOR32 | ILC_MASK, 1, 1);
    if (!images) {
        DestroyIcon(icon);
        return nullptr;
    }
    ImageList_AddIcon(images, icon);
    DestroyIcon(icon);

    BUTTON_IMAGELIST imageList{};
    imageList.himl = images;
    imageList.margin = {8, 0, 8, 0};
    imageList.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;
    SendMessageW(button, BCM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(&imageList));
    return images;
}

void OpenLink(const wchar_t* url) {
    ShellExecuteW(g_window, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void CreateControls() {
    MakeControl(L"STATIC", L"Dispositivo de entrada", SS_LEFT, 20, 18, 300, 20, 0);
    g_deviceCombo = MakeControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                20, 40, 375, 240, IDC_DEVICE_COMBO);
    MakeControl(WC_BUTTONW, L"Actualizar", BS_PUSHBUTTON | WS_TABSTOP,
                405, 39, 88, 25, IDC_REFRESH);

    MakeControl(L"STATIC", L"Hotkey momentanea", SS_LEFT, 20, 84, 200, 20, 0);
    g_hotkeyValue = MakeControl(L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | WS_BORDER,
                                20, 106, 245, 29, IDC_HOTKEY_VALUE);
    g_captureButton = MakeControl(WC_BUTTONW, L"Cambiar hotkey", BS_PUSHBUTTON | WS_TABSTOP,
                                  277, 106, 216, 29, IDC_CAPTURE_HOTKEY);

    g_rocksmithModeCheck = MakeControl(
        WC_BUTTONW, L"Modo Rocksmith: bloquear la hotkey elegida",
        BS_AUTOCHECKBOX | WS_TABSTOP, 20, 145, 473, 24, IDC_ROCKSMITH_MODE);
    SendMessageW(g_rocksmithModeCheck, BM_SETCHECK,
                 g_rocksmithMode.load() ? BST_CHECKED : BST_UNCHECKED, 0);

    MakeControl(L"STATIC",
                L"Al mantenerla: Mute real. Al soltarla: restaura el estado anterior.",
                SS_LEFT, 20, 175, 475, 20, 0);

    g_status = MakeControl(L"STATIC", L"Iniciando...", SS_LEFT | SS_CENTERIMAGE | WS_BORDER,
                           20, 204, 473, 36, IDC_STATUS);

    HWND githubButton = MakeControl(WC_BUTTONW, L"GitHub", BS_PUSHBUTTON | WS_TABSTOP,
                                    20, 256, 235, 31, IDC_GITHUB);
    HWND kofiButton = MakeControl(WC_BUTTONW, L"Apoyar en Ko-fi", BS_PUSHBUTTON | WS_TABSTOP,
                                  267, 256, 226, 31, IDC_KOFI);
    g_githubImageList = AddButtonIcon(githubButton, IDI_GITHUB);
    g_kofiImageList = AddButtonIcon(kofiButton, IDI_KOFI);

    MakeControl(WC_BUTTONW, L"Ocultar a bandeja", BS_PUSHBUTTON | WS_TABSTOP,
                20, 300, 235, 30, IDC_HIDE);
    MakeControl(WC_BUTTONW, L"Salir", BS_PUSHBUTTON | WS_TABSTOP,
                267, 300, 226, 30, IDC_EXIT);

    RefreshHotkeyLabel();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_window = window;
            CreateControls();
            AddTrayIcon();
            EnumerateDevices();
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);
            // A mouse click normally leaves focus on the button or checkbox, so
            // Space would activate it again while this window is in front.
            if (notification == BN_CLICKED) {
                SetFocus(window);
            }
            if (id == IDC_REFRESH && notification == BN_CLICKED) {
                EnumerateDevices();
                return 0;
            }
            if (id == IDC_CAPTURE_HOTKEY && notification == BN_CLICKED) {
                BeginHotkeyCapture();
                return 0;
            }
            if (id == IDC_ROCKSMITH_MODE && notification == BN_CLICKED) {
                g_rocksmithMode.store(
                    SendMessageW(g_rocksmithModeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveRocksmithMode();
                // If this changes during a blocked press, release audio but keep consuming
                // repeat events until key-up. The selected key must never re-arm mid-press.
                if (g_suppressHotkeyRelease.load()) {
                    g_hotkeyHeld.store(false);
                    g_ignoreHotkeyUntilRelease.store(true);
                } else {
                    g_blockedRocksmithWindow.store(nullptr);
                }
                const bool restored = RestoreAudioState();
                if (restored) {
                    SetStatus(g_rocksmithMode.load()
                                  ? L"Modo Rocksmith activo: bloqueare la hotkey elegida."
                                  : L"Modo global activo: la hotkey no se bloquea.");
                } else {
                    SetTimer(window, TIMER_FAILSAFE, FAILSAFE_INTERVAL_MS, nullptr);
                }
                if (g_suppressHotkeyRelease.load()) {
                    SetTimer(window, TIMER_FAILSAFE, FAILSAFE_INTERVAL_MS, nullptr);
                }
                return 0;
            }
            if (id == IDC_HIDE && notification == BN_CLICKED) {
                HideMainWindow();
                return 0;
            }
            if (id == IDC_EXIT && notification == BN_CLICKED) {
                DestroyWindow(window);
                return 0;
            }
            if (id == IDC_GITHUB && notification == BN_CLICKED) {
                OpenLink(L"https://github.com/dayitaah/Dayaah-RealTone-Killswitch");
                return 0;
            }
            if (id == IDC_KOFI && notification == BN_CLICKED) {
                OpenLink(L"https://ko-fi.com/dayaah");
                return 0;
            }
            if (id == IDC_DEVICE_COMBO && notification == CBN_SELCHANGE) {
                const LRESULT index = SendMessageW(g_deviceCombo, CB_GETCURSEL, 0, 0);
                if (index >= 0 && static_cast<size_t>(index) < g_devices.size()) {
                    ActivateDevice(g_devices[static_cast<size_t>(index)].id, true);
                }
                return 0;
            }
            break;
        }

        case WM_CAPTURED_KEY:
            if (g_captureMode.load()) {
                const DWORD vk = static_cast<DWORD>(wParam);
                if (vk == VK_PACKET || vk == VK_PROCESSKEY) {
                    SetStatus(L"Pulsa una tecla fisica distinta.");
                    return 0;
                }
                // AltGr produces a synthetic Left Ctrl immediately before Right Alt.
                // Wait briefly so capturing AltGr records VK_RMENU, not VK_LCONTROL.
                if (vk == VK_LCONTROL) {
                    g_pendingLeftCtrlCapture = true;
                    SetTimer(window, TIMER_CAPTURE_CTRL, 80, nullptr);
                } else {
                    FinishHotkeyCapture(vk);
                }
            }
            return 0;

        case WM_HOTKEY_DOWN:
            // A tap can enqueue DOWN and UP while the UI is busy. Do not apply a late
            // DOWN after the hook has already observed the release.
            if (g_hotkeyHeld.load()) {
                if (EngageCut()) {
                    SetTimer(window, TIMER_FAILSAFE, FAILSAFE_INTERVAL_MS, nullptr);
                } else {
                    g_hotkeyHeld.store(false);
                    g_ignoreHotkeyUntilRelease.store(true);
                    if (g_suppressHotkeyRelease.load()) {
                        SetTimer(window, TIMER_FAILSAFE, FAILSAFE_INTERVAL_MS, nullptr);
                    }
                }
            }
            return 0;

        case WM_HOTKEY_UP:
            if (RestoreAudioState()) {
                KillTimer(window, TIMER_FAILSAFE);
            } else {
                SetTimer(window, TIMER_FAILSAFE, FAILSAFE_INTERVAL_MS, nullptr);
            }
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_CAPTURE_CTRL && g_captureMode.load() &&
                g_pendingLeftCtrlCapture) {
                FinishHotkeyCapture(VK_LCONTROL);
                return 0;
            }
            if (wParam == TIMER_ENDPOINT_RETRY) {
                if (g_endpointReady.load() || g_selectedDeviceId.empty()) {
                    KillTimer(window, TIMER_ENDPOINT_RETRY);
                } else if (!g_cutActive && OpenEndpointById(g_selectedDeviceId)) {
                    SetStatus(L"LISTO - cable reconectado automaticamente.");
                }
                return 0;
            }
            if (wParam == TIMER_FAILSAFE) {
                const ULONGLONG now = GetTickCount64();
                const bool suppress = g_suppressHotkeyRelease.load();
                const bool held = g_hotkeyHeld.load();

                if (suppress && held) {
                    const HWND blockedWindow = g_blockedRocksmithWindow.load();
                    const bool focusLost = blockedWindow &&
                                           GetForegroundWindow() != blockedWindow;
                    const bool heldTooLong =
                        now - g_hotkeyStartTick.load() >= MAX_SUPPRESSED_HOLD_MS;
                    if (focusLost || heldTooLong) {
                        // Do not consult GetAsyncKeyState for a swallowed key. The low-level
                        // hook is called before that state is updated. Release audio, but keep
                        // swallowing repeat events until the real key-up arrives.
                        g_hotkeyHeld.store(false);
                        g_ignoreHotkeyUntilRelease.store(true);
                        const bool restored = RestoreAudioState();
                        if (heldTooLong && restored) {
                            SetStatus(L"Failsafe: corte liberado tras 30 segundos.");
                        }
                    }
                } else if (!suppress && held &&
                           (GetAsyncKeyState(static_cast<int>(g_hotkeyVk.load())) & 0x8000) == 0) {
                    // Global mode does not swallow the key, so asynchronous state remains a
                    // useful fallback if its key-up message is lost.
                    g_hotkeyHeld.store(false);
                    RestoreAudioState();
                }

                if (g_cutActive && !g_hotkeyHeld.load() &&
                    (g_lastRestoreAttemptTick == 0 ||
                     now - g_lastRestoreAttemptTick >= RESTORE_RETRY_MS)) {
                    RestoreAudioState();
                }

                if (g_suppressHotkeyRelease.load() &&
                    g_ignoreHotkeyUntilRelease.load() &&
                    now - g_hotkeyStartTick.load() >= MAX_SUPPRESSED_HOLD_MS + 5000) {
                    // If a secure desktop or a privilege boundary swallowed the key-up,
                    // do not leave the selected key suppressed forever. Keep the ignore
                    // latch until the next observed key-up so an autorepeat cannot re-arm it.
                    g_suppressHotkeyRelease.store(false);
                    g_blockedRocksmithWindow.store(nullptr);
                }

                if (!g_cutActive && !g_hotkeyHeld.load() &&
                    !g_suppressHotkeyRelease.load()) {
                    KillTimer(window, TIMER_FAILSAFE);
                }
                return 0;
            }
            break;

        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == NIN_SELECT) {
                ShowMainWindow();
            } else if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
                ShowTrayMenu();
            }
            return 0;

        case WM_SHOW_APP:
            ShowMainWindow();
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                HideMainWindow();
                return 0;
            }
            break;

        case WM_CLOSE:
            HideMainWindow();
            return 0;

        case WM_QUERYENDSESSION:
            RestoreAudioState();
            return TRUE;

        case WM_DESTROY:
            StopKeyboardHookThread();
            KillTimer(window, TIMER_FAILSAFE);
            KillTimer(window, TIMER_CAPTURE_CTRL);
            KillTimer(window, TIMER_ENDPOINT_RETRY);
            RestoreAudioState();
            ReleaseAudioEndpoint();
            RemoveTrayIcon();
            if (g_githubImageList) {
                ImageList_Destroy(g_githubImageList);
                g_githubImageList = nullptr;
            }
            if (g_kofiImageList) {
                ImageList_Destroy(g_kofiImageList);
                g_kofiImageList = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            if (control == g_status) {
                SetBkMode(dc, TRANSPARENT);
                if (g_cutActive) {
                    SetTextColor(dc, RGB(190, 0, 60));
                } else if (!g_endpointVolume) {
                    SetTextColor(dc, RGB(160, 75, 0));
                } else {
                    SetTextColor(dc, RGB(0, 110, 55));
                }
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            }
            break;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    SetProcessDPIAware();

    g_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kClassName, nullptr);
        if (existing) PostMessageW(existing, WM_SHOW_APP, 0, 0);
        CloseHandle(g_mutex);
        return 0;
    }

    LoadConfig();
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        MessageBoxW(nullptr, L"No pude iniciar Windows Audio (COM).", kWindowTitle, MB_ICONERROR);
        return 1;
    }

    CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                     IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&g_enumerator));
    RecoverPendingAudioState();

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        g_font = CreateFontIndirectW(&metrics.lfMessageFont);
    } else {
        g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                                      IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                                        IMAGE_ICON, 16, 16, 0));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    RECT wanted{0, 0, 520, 350};
    AdjustWindowRectEx(&wanted, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                       FALSE, 0);
    const int width = wanted.right - wanted.left;
    const int height = wanted.bottom - wanted.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    g_window = CreateWindowExW(0, kClassName, kWindowTitle,
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        MessageBoxW(nullptr, L"No pude crear la ventana.", kWindowTitle, MB_ICONERROR);
        SafeRelease(g_enumerator);
        CoUninitialize();
        return 1;
    }

    if (!StartKeyboardHookThread()) {
        MessageBoxW(g_window, L"No pude activar la hotkey global.", kWindowTitle, MB_ICONWARNING);
    }

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    StopKeyboardHookThread();
    SafeRelease(g_endpointVolume);
    SafeRelease(g_enumerator);
    if (g_font && g_font != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(g_font);
    CoUninitialize();
    if (g_mutex) CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
