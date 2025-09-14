#include "league_auto_accept/system_tray.h"
#include "league_auto_accept/models/performance_metrics.h"
#include <stdexcept>
#include <algorithm>

namespace league_auto_accept {

SystemTray::SystemTray()
    : SystemTray(nullptr) {
}

SystemTray::SystemTray(std::shared_ptr<models::PerformanceMetrics> metrics)
    : hinstance_(nullptr)
    , message_window_(nullptr)
    , context_menu_(nullptr)
    , tray_state_(std::make_shared<models::SystemTrayState>())
    , performance_metrics_(metrics)
    , message_loop_running_(false)
    , should_stop_message_loop_(false)
    , icon_disabled_(nullptr)
    , icon_monitoring_(nullptr)
    , icon_active_(nullptr)
    , icon_error_(nullptr)
    , current_icon_(nullptr)
    , initialized_(false)
    , icon_created_(false) {

    ZeroMemory(&notify_icon_data_, sizeof(NOTIFYICONDATAW));
}

SystemTray::~SystemTray() {
    StopMessageLoop();
    DestroyTrayIcon();
    CleanupResources();
}

bool SystemTray::Initialize(HINSTANCE hInstance, const std::string& app_name) {
    if (initialized_) return true;

    hinstance_ = hInstance;

    try {
        // Register window class for message handling
        if (!RegisterWindowClass(hInstance, std::string(reinterpret_cast<const char*>(WINDOW_CLASS_NAME)))) {
            return false;
        }

        // Create message-only window
        message_window_ = CreateMessageWindow(hInstance, std::string(reinterpret_cast<const char*>(WINDOW_CLASS_NAME)), DefaultWindowProc);
        if (!message_window_) {
            return false;
        }

        // Load icon resources
        if (!LoadIconResources()) {
            return false;
        }

        // Initialize notify icon data structure
        if (!InitializeNotifyIconData()) {
            return false;
        }

        // Set initial state
        tray_state_->SetIconState(models::SystemTrayIconState::DISABLED);
        tray_state_->CreateStandardMenu(false);

        initialized_ = true;
        return true;

    } catch (const std::exception&) {
        CleanupResources();
        return false;
    }
}

bool SystemTray::CreateTrayIcon() {
    if (!initialized_ || icon_created_) return false;

    notify_icon_data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID;
    notify_icon_data_.uCallbackMessage = WM_TRAYICON;

    // Set initial icon
    current_icon_ = GetIconForState(tray_state_->GetIconState());
    notify_icon_data_.hIcon = current_icon_;

    // Set initial tooltip
    std::string tooltip = tray_state_->GetTooltipText();
    std::wstring wtooltip = StringToWString(tooltip);
    wcsncpy_s(notify_icon_data_.szTip, wtooltip.c_str(), _TRUNCATE);

    bool success = Shell_NotifyIconW(NIM_ADD, &notify_icon_data_) != FALSE;
    if (success) {
        icon_created_ = true;

        // Set modern behavior
        notify_icon_data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &notify_icon_data_);
    }

    return success;
}

void SystemTray::DestroyTrayIcon() {
    if (icon_created_) {
        Shell_NotifyIconW(NIM_DELETE, &notify_icon_data_);
        icon_created_ = false;
    }
}

bool SystemTray::IsInitialized() const {
    return initialized_;
}

bool SystemTray::UpdateIcon(models::SystemTrayIconState state) {
    if (!icon_created_) return false;

    tray_state_->SetIconState(state);

    HICON new_icon = GetIconForState(state);
    if (new_icon != current_icon_) {
        current_icon_ = new_icon;
        notify_icon_data_.hIcon = current_icon_;
        notify_icon_data_.uFlags = NIF_ICON;

        return Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_) != FALSE;
    }

    return true;
}

bool SystemTray::SetTooltip(const std::string& tooltip) {
    if (!icon_created_) return false;

    tray_state_->SetTooltipText(tooltip);

    std::wstring wtooltip = StringToWString(tooltip);
    wcsncpy_s(notify_icon_data_.szTip, wtooltip.c_str(), _TRUNCATE);

    notify_icon_data_.uFlags = NIF_TIP;
    return Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_) != FALSE;
}

std::string SystemTray::GetTooltip() const {
    return tray_state_->GetTooltipText();
}

void SystemTray::UpdateTooltipWithStatus(bool auto_accept_enabled, bool league_connected,
                                        int matches_accepted, double success_rate) {
    tray_state_->UpdateTooltipWithStatus(auto_accept_enabled, league_connected, matches_accepted, success_rate);
    SetTooltip(tray_state_->GetTooltipText());
}

bool SystemTray::CreateContextMenu() {
    if (context_menu_) {
        DestroyMenu(context_menu_);
    }

    context_menu_ = CreatePopupMenu();
    if (!context_menu_) return false;

    CreateStandardMenu();
    return true;
}

void SystemTray::ShowContextMenu(int x, int y) {
    if (!context_menu_) {
        CreateContextMenu();
    }

    // Update menu state before showing
    UpdateContextMenu();

    // Required for proper menu behavior
    SetForegroundWindow(message_window_);

    TrackPopupMenu(context_menu_,
                   TPM_RIGHTBUTTON | TPM_NONOTIFY,
                   x, y, 0, message_window_, nullptr);

    // Clean up
    PostMessage(message_window_, WM_NULL, 0, 0);
}

bool SystemTray::ShowNotification(const std::string& title, const std::string& message, int duration_ms) {
    tray_state_->QueueNotification(title, message, duration_ms);
    ProcessNotificationQueue();
    return true;
}

bool SystemTray::ShowBalloonTip(const std::string& title, const std::string& message, DWORD icon_type) {
    return ShowBalloonNotification(title, message, icon_type, 3000);
}

void SystemTray::ProcessNotificationQueue() {
    if (!tray_state_->HasPendingNotifications()) return;

    auto notification = tray_state_->GetNextNotification();
    ShowBalloonNotification(notification.title, notification.message, NIIF_INFO, notification.duration_ms);
    tray_state_->MarkNotificationShown(notification);
}

std::shared_ptr<models::SystemTrayState> SystemTray::GetState() const {
    return tray_state_;
}

void SystemTray::SetEventCallback(EventCallback callback) {
    event_callback_ = callback;
}

bool SystemTray::ProcessMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (hwnd != message_window_) return false;

    switch (message) {
    case WM_TRAYICON:
        HandleTrayIconMessage(wParam, lParam);
        return true;

    case WM_COMMAND:
        HandleMenuCommand(wParam);
        return true;

    case WM_TASKBARCREATED:
        return HandleTaskbarCreated();

    default:
        return false;
    }
}

void SystemTray::StartMessageLoop() {
    if (message_loop_running_) return;

    should_stop_message_loop_ = false;
    message_loop_running_ = true;

    message_thread_ = std::thread([this]() {
        MSG msg;
        while (!should_stop_message_loop_) {
            BOOL result = GetMessage(&msg, nullptr, 0, 0);

            if (result == -1) break; // Error
            if (result == 0) break;  // WM_QUIT

            if (!ProcessMessage(msg.hwnd, msg.message, msg.wParam, msg.lParam)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        message_loop_running_ = false;
    });
}

void SystemTray::StopMessageLoop() {
    if (!message_loop_running_) return;

    should_stop_message_loop_ = true;

    if (message_thread_.joinable()) {
        PostThreadMessage(GetThreadId(message_thread_.native_handle()), WM_QUIT, 0, 0);
        message_thread_.join();
    }
}

bool SystemTray::IsMessageLoopRunning() const {
    return message_loop_running_;
}

// Static helper methods
bool SystemTray::RegisterWindowClass(HINSTANCE hInstance, const std::string& class_name) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = DefaultWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    return RegisterClassExW(&wc) != 0;
}

HWND SystemTray::CreateMessageWindow(HINSTANCE hInstance, const std::string& class_name, WNDPROC window_proc) {
    return CreateWindowExW(0, WINDOW_CLASS_NAME, L"", 0, 0, 0, 0, 0,
                          HWND_MESSAGE, nullptr, hInstance, nullptr);
}

LRESULT CALLBACK SystemTray::DefaultWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // This would be set up to route to the SystemTray instance
    // For simplicity, using default handling
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool SystemTray::HandleTaskbarCreated() {
    // Recreate tray icon after explorer restart
    if (icon_created_) {
        DestroyTrayIcon();
        return CreateTrayIcon();
    }
    return true;
}

// Private helper methods
bool SystemTray::InitializeNotifyIconData() {
    notify_icon_data_.cbSize = sizeof(NOTIFYICONDATAW);
    notify_icon_data_.hWnd = message_window_;
    notify_icon_data_.uID = TRAY_ICON_ID;
    notify_icon_data_.uVersion = NOTIFYICON_VERSION_4;

    // Generate a unique GUID for this application
    static const GUID tray_guid = {
        0x5CA81ADA, 0xA481, 0x4BA8,
        {0x8B, 0x70, 0x80, 0x3f, 0x48, 0x67, 0xA1, 0x68}
    };
    notify_icon_data_.guidItem = tray_guid;
    notify_icon_data_.uFlags |= NIF_GUID;

    return true;
}

bool SystemTray::LoadIconResources() {
    // Load default system icons (in full implementation, these would be custom resources)
    icon_disabled_ = LoadIcon(nullptr, IDI_APPLICATION);
    icon_monitoring_ = LoadIcon(nullptr, IDI_INFORMATION);
    icon_active_ = LoadIcon(nullptr, IDI_SHIELD);
    icon_error_ = LoadIcon(nullptr, IDI_ERROR);

    return icon_disabled_ && icon_monitoring_ && icon_active_ && icon_error_;
}

void SystemTray::CleanupResources() {
    if (context_menu_) {
        DestroyMenu(context_menu_);
        context_menu_ = nullptr;
    }

    if (message_window_) {
        DestroyWindow(message_window_);
        message_window_ = nullptr;
    }

    // Don't destroy system icons
    current_icon_ = nullptr;
}

void SystemTray::CreateStandardMenu() {
    if (!context_menu_) return;

    auto menu_items = tray_state_->GetMenuItems();
    UINT menu_id = MENU_ID_BASE;

    for (const auto& item : menu_items) {
        if (item.separator) {
            AppendMenuW(context_menu_, MF_SEPARATOR, 0, nullptr);
        } else {
            UINT flags = MF_STRING;
            if (!item.enabled) flags |= MF_GRAYED;
            if (item.checked) flags |= MF_CHECKED;

            std::wstring wtext = StringToWString(item.text);
            AppendMenuW(context_menu_, flags, menu_id++, wtext.c_str());
        }
    }
}

void SystemTray::HandleTrayIconMessage(WPARAM wParam, LPARAM lParam) {
    if (wParam != TRAY_ICON_ID) return;

    SystemTrayEventData event_data;
    GetCursorPos(&event_data.cursor_position);

    switch (lParam) {
    case WM_LBUTTONDOWN:
        event_data.event_type = SystemTrayEvent::LEFT_CLICK;
        break;

    case WM_RBUTTONDOWN:
    case WM_CONTEXTMENU:
        event_data.event_type = SystemTrayEvent::RIGHT_CLICK;
        ShowContextMenu(event_data.cursor_position.x, event_data.cursor_position.y);
        break;

    case WM_LBUTTONDBLCLK:
        event_data.event_type = SystemTrayEvent::DOUBLE_CLICK;
        break;

    case NIN_BALLOONUSERCLICK:
        event_data.event_type = SystemTrayEvent::NOTIFICATION_CLICKED;
        break;

    default:
        return;
    }

    DispatchEvent(event_data);
    UpdateLastUserInteraction();
}

void SystemTray::HandleMenuCommand(WPARAM wParam) {
    UINT menu_id = LOWORD(wParam);
    if (menu_id < MENU_ID_BASE) return;

    // Map menu ID back to menu item
    auto menu_items = tray_state_->GetMenuItems();
    size_t item_index = menu_id - MENU_ID_BASE;

    if (item_index < menu_items.size()) {
        SystemTrayEventData event_data(SystemTrayEvent::MENU_ITEM_SELECTED);
        event_data.menu_item_id = menu_items[item_index].id;
        DispatchEvent(event_data);
    }

    UpdateLastUserInteraction();
}

void SystemTray::DispatchEvent(const SystemTrayEventData& event_data) {
    if (event_callback_) {
        event_callback_(event_data);
    }
}

void SystemTray::UpdateLastUserInteraction() {
    tray_state_->UpdateLastUserInteraction();
}

HICON SystemTray::GetIconForState(models::SystemTrayIconState state) const {
    switch (state) {
    case models::SystemTrayIconState::DISABLED: return icon_disabled_;
    case models::SystemTrayIconState::MONITORING: return icon_monitoring_;
    case models::SystemTrayIconState::ACTIVE: return icon_active_;
    case models::SystemTrayIconState::ERROR_STATE: return icon_error_;
    default: return icon_disabled_;
    }
}

bool SystemTray::ShowBalloonNotification(const std::string& title, const std::string& message,
                                        DWORD icon_type, int timeout) {
    if (!icon_created_) return false;

    std::wstring wtitle = StringToWString(title);
    std::wstring wmessage = StringToWString(message);

    wcsncpy_s(notify_icon_data_.szInfoTitle, wtitle.c_str(), _TRUNCATE);
    wcsncpy_s(notify_icon_data_.szInfo, wmessage.c_str(), _TRUNCATE);
    notify_icon_data_.dwInfoFlags = icon_type;
    notify_icon_data_.uTimeout = timeout;
    notify_icon_data_.uFlags = NIF_INFO;

    return Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_) != FALSE;
}

std::wstring SystemTray::StringToWString(const std::string& str) const {
    if (str.empty()) return std::wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

std::string SystemTray::WStringToString(const std::wstring& wstr) const {
    if (wstr.empty()) return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, nullptr, nullptr);
    return str;
}

} // namespace league_auto_accept