#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

// Window dimensions and IDs
#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 360
#define IDC_STATUS_TEXT 1001
#define IDC_ENABLE_CHECKBOX 1002
#define IDC_START_BUTTON 1003
#define IDC_STOP_BUTTON 1004
#define IDC_CONFIG_BUTTON 1005
#define IDC_LOG_LISTBOX 1006
#define IDC_MINIMIZE_BUTTON 1007
#define IDM_SHOW_WINDOW 2001
#define IDM_TOGGLE_ACCEPT 2002
#define IDM_EXIT 2003
#define IDM_COPY_LOGS 2004
#define IDM_SELECT_ALL_LOGS 2005
#define WM_TRAY_CALLBACK WM_USER + 1
#define WM_ADD_LOG WM_USER + 2

// Base64 encoding for authentication
std::string base64_encode(const std::string& input) {
    static const char encoding_table[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
        'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
        'w', 'x', 'y', 'z', '0', '1', '2', '3',
        '4', '5', '6', '7', '8', '9', '+', '/'
    };

    std::string encoded;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(encoding_table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) encoded.push_back(encoding_table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4) encoded.push_back('=');
    return encoded;
}

class LeagueAutoAcceptGUI {
private:
    HWND main_window;
    HWND status_text;
    HWND enable_checkbox;
    HWND start_button;
    HWND stop_button;
    HWND config_button;
    HWND log_listbox;
    HWND minimize_button;

    NOTIFYICONDATAA nid = {0};
    HICON app_icon;

    std::string lcu_token;
    int lcu_port = 0;
    std::atomic<bool> running{false};
    std::atomic<bool> auto_accept_enabled{false};
    std::atomic<bool> emergency_stop{false};
    std::thread worker_thread;

    // Cache working endpoint to avoid repeated testing
    std::string working_gameflow_endpoint = "";
    bool endpoints_tested = false;
    bool connection_logged = false;  // Track if we've logged connection for this session

    // Configuration
    struct Config {
        bool auto_accept_enabled = true;
        int polling_interval_ms = 250;
        int lcu_timeout_ms = 5000;
        std::string emergency_hotkey = "F9";
        bool enable_notifications = true;
        bool startup_enabled = false;
        bool minimize_to_tray = true;
        bool show_notifications = true;
    } config;

public:
    static LeagueAutoAcceptGUI* instance;

    LeagueAutoAcceptGUI() : main_window(nullptr), app_icon(nullptr) {
        instance = this;
    }

    ~LeagueAutoAcceptGUI() {
        Shutdown();
        instance = nullptr;
    }

    bool Initialize() {
        // Load configuration
        LoadConfiguration();

        // Initialize COM controls
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);

        // Create main window
        if (!CreateMainWindow()) {
            return false;
        }

        // Setup system tray
        SetupSystemTray();

        // Register emergency hotkey
        RegisterEmergencyHotkey();

        // Start with auto-accept based on config
        auto_accept_enabled = config.auto_accept_enabled;
        UpdateUI();

        return true;
    }

    bool CreateMainWindow() {
        const char* CLASS_NAME = "LeagueAutoAcceptGUI";

        WNDCLASSA wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        app_icon = wc.hIcon;

        if (!RegisterClassA(&wc)) {
            return false;
        }

        main_window = CreateWindowExA(
            0,
            CLASS_NAME,
            "League Auto-Accept v1.0",
            WS_OVERLAPPEDWINDOW,  // Makes window resizable with maximize button
            CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
            nullptr, nullptr, GetModuleHandle(nullptr), this
        );

        if (!main_window) {
            return false;
        }

        CreateControls();
        ShowWindow(main_window, SW_SHOW);
        UpdateWindow(main_window);

        return true;
    }

    void CreateControls() {
        HFONT default_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // Status text at top
        status_text = CreateWindowA("STATIC", "Ready - League client not detected",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            10, 10, 450, 20,
            main_window, (HMENU)IDC_STATUS_TEXT, GetModuleHandle(nullptr), nullptr);
        SendMessage(status_text, WM_SETFONT, (WPARAM)default_font, TRUE);

        // Enable/Disable checkbox
        enable_checkbox = CreateWindowA("BUTTON", "Enable Auto-Accept",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            10, 40, 150, 25,
            main_window, (HMENU)IDC_ENABLE_CHECKBOX, GetModuleHandle(nullptr), nullptr);
        SendMessage(enable_checkbox, WM_SETFONT, (WPARAM)default_font, TRUE);

        // Start button
        start_button = CreateWindowA("BUTTON", "Start Monitoring",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            10, 75, 100, 30,
            main_window, (HMENU)IDC_START_BUTTON, GetModuleHandle(nullptr), nullptr);
        SendMessage(start_button, WM_SETFONT, (WPARAM)default_font, TRUE);

        // Stop button
        stop_button = CreateWindowA("BUTTON", "Stop",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            120, 75, 80, 30,
            main_window, (HMENU)IDC_STOP_BUTTON, GetModuleHandle(nullptr), nullptr);
        SendMessage(stop_button, WM_SETFONT, (WPARAM)default_font, TRUE);
        EnableWindow(stop_button, FALSE);

        // Configuration button
        config_button = CreateWindowA("BUTTON", "Settings",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            210, 75, 80, 30,
            main_window, (HMENU)IDC_CONFIG_BUTTON, GetModuleHandle(nullptr), nullptr);
        SendMessage(config_button, WM_SETFONT, (WPARAM)default_font, TRUE);

        // Minimize to tray button
        minimize_button = CreateWindowA("BUTTON", "Minimize to Tray",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            300, 75, 120, 30,
            main_window, (HMENU)IDC_MINIMIZE_BUTTON, GetModuleHandle(nullptr), nullptr);
        SendMessage(minimize_button, WM_SETFONT, (WPARAM)default_font, TRUE);

        // Log activity area with label
        CreateWindowA("STATIC", "Activity Log (drag to select, Ctrl+A to select all, Ctrl+C to copy):",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            10, 120, 450, 20,
            main_window, nullptr, GetModuleHandle(nullptr), nullptr);

        // Replace listbox with multiline edit control for better text selection
        log_listbox = CreateWindowA("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            10, 140, 450, 180,
            main_window, (HMENU)IDC_LOG_LISTBOX, GetModuleHandle(nullptr), nullptr);
        SendMessage(log_listbox, WM_SETFONT, (WPARAM)default_font, TRUE);

        // Set a fixed-width font for better log readability
        HFONT mono_font = CreateFontA(
            14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        if (mono_font) {
            SendMessage(log_listbox, WM_SETFONT, (WPARAM)mono_font, TRUE);
        }

        // Initialize checkbox state
        SendMessage(enable_checkbox, BM_SETCHECK,
                    auto_accept_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    void AddLogMessage(const std::string& message) {
        // Get current time
        SYSTEMTIME st;
        GetLocalTime(&st);

        char timestamp[32];
        sprintf_s(timestamp, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

        std::string full_message = timestamp + message + "\r\n";

        // Get current text length
        int current_length = GetWindowTextLengthA(log_listbox);

        // Limit log size to prevent memory issues (keep last 50KB of text)
        if (current_length > 50000) {
            // Get all text
            char* all_text = new char[current_length + 1];
            GetWindowTextA(log_listbox, all_text, current_length + 1);

            // Keep only the last portion
            std::string text_str(all_text);
            delete[] all_text;

            // Find a good breaking point (after a newline)
            size_t break_pos = text_str.find("\r\n", text_str.length() / 2);
            if (break_pos != std::string::npos) {
                text_str = text_str.substr(break_pos + 2);
            }

            SetWindowTextA(log_listbox, text_str.c_str());
            current_length = text_str.length();
        }

        // Move cursor to end and append new message
        SendMessage(log_listbox, EM_SETSEL, current_length, current_length);
        SendMessageA(log_listbox, EM_REPLACESEL, FALSE, (LPARAM)full_message.c_str());

        // Auto-scroll to bottom
        SendMessage(log_listbox, EM_SCROLLCARET, 0, 0);
    }

    void CopyLogsToClipboard() {
        // Select all text first
        SendMessage(log_listbox, EM_SETSEL, 0, -1);
        // Copy to clipboard using standard Windows shortcut
        SendMessage(log_listbox, WM_COPY, 0, 0);
        // Deselect
        int text_len = GetWindowTextLengthA(log_listbox);
        SendMessage(log_listbox, EM_SETSEL, text_len, text_len);

        AddLogMessage("All logs copied to clipboard");
    }

    void SelectAllLogs() {
        // Select all text in the edit control
        SendMessage(log_listbox, EM_SETSEL, 0, -1);
        SetFocus(log_listbox);
    }

    void ShowLogContextMenu(int x, int y) {
        HMENU menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING, IDM_COPY_LOGS, "Copy All Logs");
        AppendMenuA(menu, MF_STRING, IDM_SELECT_ALL_LOGS, "Select All");

        SetForegroundWindow(main_window);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, main_window, nullptr);
        DestroyMenu(menu);
    }

    void ResizeControls() {
        RECT client_rect;
        GetClientRect(main_window, &client_rect);
        int width = client_rect.right - client_rect.left;
        int height = client_rect.bottom - client_rect.top;

        // Resize status text to fit window width
        MoveWindow(status_text, 10, 10, width - 20, 20, TRUE);

        // Keep buttons in their original positions (they don't need to resize)

        // Resize the activity log label
        MoveWindow(GetWindow(log_listbox, GW_HWNDPREV), 10, 120, width - 20, 20, TRUE);

        // Resize the log area to fill most of the window
        int log_top = 140;
        int log_height = height - log_top - 10; // Leave 10px margin at bottom
        if (log_height < 100) log_height = 100; // Minimum height

        MoveWindow(log_listbox, 10, log_top, width - 20, log_height, TRUE);
    }

    void UpdateUI() {
        // Update status text
        std::string status = running ?
            (auto_accept_enabled ? "Running - Auto-accept ENABLED" : "Running - Auto-accept DISABLED") :
            "Stopped - Click Start to begin monitoring";

        SetWindowTextA(status_text, status.c_str());

        // Update button states
        EnableWindow(start_button, !running);
        EnableWindow(stop_button, running);

        // Update checkbox
        SendMessage(enable_checkbox, BM_SETCHECK,
                    auto_accept_enabled ? BST_CHECKED : BST_UNCHECKED, 0);

        // Update system tray tooltip
        UpdateTrayIcon();
    }

    void SetupSystemTray() {
        nid.cbSize = sizeof(NOTIFYICONDATAA);
        nid.hWnd = main_window;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        nid.uCallbackMessage = WM_TRAY_CALLBACK;
        nid.hIcon = app_icon;
        strcpy_s(nid.szTip, sizeof(nid.szTip), "League Auto-Accept");

        Shell_NotifyIconA(NIM_ADD, &nid);
        UpdateTrayIcon();
    }

    void UpdateTrayIcon() {
        std::string tip = "League Auto-Accept - ";
        if (running) {
            tip += auto_accept_enabled ? "ENABLED" : "DISABLED";
        } else {
            tip += "STOPPED";
        }

        strcpy_s(nid.szTip, sizeof(nid.szTip), tip.c_str());
        Shell_NotifyIconA(NIM_MODIFY, &nid);
    }

    void ShowTrayMenu() {
        POINT cursor;
        GetCursorPos(&cursor);

        HMENU menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING, IDM_SHOW_WINDOW, "Show Window");
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_STRING, IDM_TOGGLE_ACCEPT,
                    auto_accept_enabled ? "Disable Auto-Accept" : "Enable Auto-Accept");
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_STRING, IDM_EXIT, "Exit");

        SetForegroundWindow(main_window);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, main_window, nullptr);
        DestroyMenu(menu);
    }

    void StartMonitoring() {
        if (running) return;

        running = true;
        worker_thread = std::thread([this]() { WorkerLoop(); });

        AddLogMessage("Started monitoring League client");
        UpdateUI();
    }

    void StopMonitoring() {
        if (!running) return;

        AddLogMessage("Stopping monitoring...");
        running = false;
        UpdateUI();

        // Detach the worker thread immediately to avoid blocking
        // The worker thread will exit on its own when it sees running = false
        if (worker_thread.joinable()) {
            AddLogMessage("Detaching worker thread to avoid UI freeze");
            worker_thread.detach();
        }

        AddLogMessage("Stopped monitoring");
    }

    void WorkerLoop() {
        AddLogMessage("Worker thread started");

        std::string last_phase = "";
        bool lcu_connected = false;
        int connection_attempts = 0;
        bool api_endpoints_tested = false;
        std::chrono::steady_clock::time_point client_connect_time;
        int api_test_delay_seconds = 3; // Wait 3 seconds after connection before testing APIs
        bool ready_check_handled = false; // Track if we already handled current ready check

        while (running && !emergency_stop) {
            try {
                // Try to read LCU lockfile
                if (ReadLCULockfile()) {
                    if (!lcu_connected) {
                        lcu_connected = true;
                        connection_attempts = 0;
                        api_endpoints_tested = false;
                        client_connect_time = std::chrono::steady_clock::now();
                    }

                    // Wait for client initialization
                    if (lcu_connected && !api_endpoints_tested) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - client_connect_time
                        ).count();

                        if (elapsed >= api_test_delay_seconds) {
                            api_endpoints_tested = true;
                            working_gameflow_endpoint.clear(); // Clear cache to force retesting
                        }
                    }

                    // Get current game phase
                    std::string current_phase = GetGameflowPhase();

                    if (!current_phase.empty()) {
                        // Only log important phase changes
                        if (current_phase != last_phase) {
                            if (current_phase == "ReadyCheck" || current_phase == "Matchmaking" ||
                                current_phase == "ChampSelect" || current_phase == "InGame") {
                                AddLogMessage("Game phase: " + current_phase);
                            }
                            last_phase = current_phase;

                            // Reset ready check handling when phase changes
                            if (current_phase != "ReadyCheck") {
                                ready_check_handled = false;
                            }
                        }

                        // Check for ready check - multiple detection methods
                        bool ready_check_detected = false;
                        std::string detection_method = "";

                        if (current_phase == "ReadyCheck") {
                            ready_check_detected = true;
                            detection_method = "gameflow phase";
                        }
                        else if (current_phase.empty() || current_phase == "None") {
                            // Phase detection failed - try alternative methods
                            if (CheckForReadyCheckAlternatives()) {
                                ready_check_detected = true;
                                detection_method = "alternative endpoint scanning";
                            }
                        }

                        if (ready_check_detected && !ready_check_handled) {
                            ready_check_handled = true; // Mark as handled to prevent repeated attempts

                            if (auto_accept_enabled) {
                                AddLogMessage("READY CHECK DETECTED via " + detection_method + " - Auto-accepting...");
                                if (AcceptReadyCheck()) {
                                    AddLogMessage("Ready check accepted successfully!");
                                    if (config.show_notifications) {
                                        ShowNotification("Ready check accepted!");
                                    }
                                } else {
                                    AddLogMessage("Failed to accept ready check - all API methods failed");
                                }
                            } else {
                                AddLogMessage("Ready check detected via " + detection_method + " but auto-accept is DISABLED");
                            }
                        }
                    } else {
                        // API call failed
                        if (lcu_connected) {
                            AddLogMessage("Failed to get game phase - LCU API error");
                        }
                    }
                } else {
                    // Could not read lockfile
                    if (lcu_connected) {
                        AddLogMessage("Lost connection to League client");
                        lcu_connected = false;
                        last_phase = "";
                    } else {
                        connection_attempts++;
                        if (connection_attempts == 1) {
                            AddLogMessage("Waiting for League client to start...");
                        } else if (connection_attempts % 40 == 0) {  // Every 10 seconds (40 * 250ms)
                            AddLogMessage("Still waiting for League client (make sure it's running)");
                        }
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(config.polling_interval_ms));
            } catch (const std::exception& e) {
                AddLogMessage("Exception in worker loop: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            } catch (...) {
                AddLogMessage("Unknown exception in worker loop");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        if (emergency_stop) {
            AddLogMessage("EMERGENCY STOP activated!");
        }

        AddLogMessage("Worker thread stopped");
    }

    // LCU API methods (same as original implementation)
    bool ReadLCULockfile() {
        // Try multiple potential League client lockfile locations
        std::vector<std::string> potential_paths = {
            std::string(getenv("LOCALAPPDATA")) + "\\Riot Games\\League of Legends\\lockfile",
            "C:\\Riot Games\\League of Legends\\lockfile",
            std::string(getenv("PROGRAMFILES")) + "\\Riot Games\\League of Legends\\lockfile",
            std::string(getenv("PROGRAMFILES(X86)")) + "\\Riot Games\\League of Legends\\lockfile",
            std::string(getenv("USERPROFILE")) + "\\AppData\\Local\\Riot Games\\League of Legends\\lockfile",
            std::string(getenv("LOCALAPPDATA")) + "\\Riot Games\\Riot Client\\Config\\lockfile" // Riot Client as fallback
        };

        for (const std::string& lockfile_path : potential_paths) {
            std::ifstream file(lockfile_path);
            if (!file.is_open()) {
                continue;
            }

            std::string line;
            if (std::getline(file, line)) {
                std::vector<std::string> parts;
                std::stringstream ss(line);
                std::string part;

                while (std::getline(ss, part, ':')) {
                    parts.push_back(part);
                }

                if (parts.size() >= 5) {
                    lcu_port = std::stoi(parts[2]);
                    lcu_token = parts[3];

                    // Check if this is League client (not Riot Client)
                    if (parts[0].find("League") != std::string::npos) {
                        if (!connection_logged) {
                            AddLogMessage("Connected to League client (port: " + std::to_string(lcu_port) + ")");
                            connection_logged = true;
                        }
                        return true;
                    } else if (parts[0].find("Riot Client") != std::string::npos) {
                        file.close();
                        continue; // Try next path - we want League client, not Riot client
                    } else {
                        if (!connection_logged) {
                            AddLogMessage("Connected to " + parts[0] + " (port: " + std::to_string(lcu_port) + ")");
                            connection_logged = true;
                        }
                        return true;
                    }
                }
            }
            file.close();
        }

        AddLogMessage("❌ Could not find League of Legends client lockfile");
        return false;
    }

    std::string MakeLCURequest(const std::string& endpoint, const std::string& method = "GET") {
        std::wstring host = L"127.0.0.1";
        DWORD flags = WINHTTP_FLAG_SECURE;

        // Silent connection - no debug spam

        HINTERNET session = WinHttpOpen(L"LeagueAutoAccept/1.0",
                                       WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            AddLogMessage("API Error: Failed to create HTTP session");
            return "";
        }

        HINTERNET connect = WinHttpConnect(session, host.c_str(), lcu_port, 0);
        if (!connect) {
            AddLogMessage("API Error: Failed to connect to 127.0.0.1:" + std::to_string(lcu_port));
            WinHttpCloseHandle(session);
            return "";
        }

        std::wstring wendpoint(endpoint.begin(), endpoint.end());
        std::wstring wmethod(method.begin(), method.end());

        HINTERNET request = WinHttpOpenRequest(connect, wmethod.c_str(), wendpoint.c_str(),
                                              nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) {
            AddLogMessage("API Error: Failed to create HTTP request for " + endpoint);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return "";
        }

        // Skip SSL certificate verification
        DWORD ssl_flags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_UNKNOWN_CA;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &ssl_flags, sizeof(ssl_flags))) {
            AddLogMessage("API Warning: Failed to set SSL ignore flags");
        }

        // Add authorization header
        std::string auth = "riot:" + lcu_token;
        std::string encoded_auth = base64_encode(auth);
        std::string auth_header = "Authorization: Basic " + encoded_auth;
        std::wstring wauth_header(auth_header.begin(), auth_header.end());

        if (!WinHttpAddRequestHeaders(request, wauth_header.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD)) {
            // Silent - auth header failure is rare
        }

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return "";
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return "";
        }

        // Check HTTP status code silently
        DWORD status_code = 0;
        DWORD status_code_size = sizeof(status_code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX);

        std::string result;
        DWORD bytes_available = 0;

        do {
            if (!WinHttpQueryDataAvailable(request, &bytes_available)) {
                AddLogMessage("API Error: Failed to query data availability");
                break;
            }

            if (bytes_available > 0) {
                std::vector<char> buffer(bytes_available + 1);
                DWORD bytes_read = 0;

                if (WinHttpReadData(request, buffer.data(), bytes_available, &bytes_read)) {
                    buffer[bytes_read] = '\0';
                    result += buffer.data();
                } else {
                    AddLogMessage("API Error: Failed to read response data");
                    break;
                }
            }
        } while (bytes_available > 0);

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);

        if (result.empty()) {
            AddLogMessage("API Error: Empty response from " + endpoint);
        }

        return result;
    }

    std::string GetGameflowPhase() {
        // If we have a working endpoint cached, use it
        if (!working_gameflow_endpoint.empty()) {
            std::string response = MakeLCURequest(working_gameflow_endpoint);
            if (!response.empty() && response.find("errorCode") == std::string::npos) {
                return ParseGameflowResponse(response, working_gameflow_endpoint);
            } else {
                working_gameflow_endpoint = "";
            }
        }

        // Try the primary gameflow endpoint
        std::string response = MakeLCURequest("/lol-gameflow/v1/gameflow-phase");
        if (!response.empty() && response.find("errorCode") == std::string::npos) {
            working_gameflow_endpoint = "/lol-gameflow/v1/gameflow-phase";
            return ParseGameflowResponse(response, working_gameflow_endpoint);
        }

        return "";
    }

    std::string ParseGameflowResponse(const std::string& response, const std::string& endpoint) {
        if (endpoint == "/lol-gameflow/v1/gameflow-phase") {
            // Direct phase response - should be just "None", "Lobby", etc.
            size_t start = response.find("\"");
            if (start != std::string::npos) {
                start += 1;
                size_t end = response.find("\"", start);
                if (end != std::string::npos) {
                    return response.substr(start, end - start);
                }
            }
        }
        else if (endpoint == "/lol-gameflow/v1/session") {
            // Parse session object for phase
            size_t phase_pos = response.find("\"phase\":");
            if (phase_pos != std::string::npos) {
                size_t quote_start = response.find("\"", phase_pos + 8);
                if (quote_start != std::string::npos) {
                    size_t quote_end = response.find("\"", quote_start + 1);
                    if (quote_end != std::string::npos) {
                        return response.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }
            }
        }
        else if (endpoint == "/lol-lobby/v2/lobby") {
            // Lobby exists = we're in lobby
            if (response.find("\"gameConfig\"") != std::string::npos) {
                return "Lobby";
            }
        }
        else if (endpoint == "/lol-matchmaking/v1/search") {
            // Matchmaking active
            if (response.find("\"searchState\"") != std::string::npos &&
                response.find("\"Searching\"") != std::string::npos) {
                return "Matchmaking";
            }
        }
        else if (endpoint == "/lol-champ-select/v1/session") {
            // Champion select active
            if (response.find("\"phase\"") != std::string::npos) {
                return "ChampSelect";
            }
        }

        return "";
    }

    bool CheckForReadyCheckAlternatives() {
        // Test multiple ready check detection endpoints
        std::vector<std::pair<std::string, std::string>> ready_endpoints = {
            {"/lol-matchmaking/v1/ready-check", "ready check status"},
            {"/lol-matchmaking/v1/search", "matchmaking search state"},
            {"/lol-gameflow/v1/gameflow-phase", "gameflow phase check"},
            {"/lol-lobby/v2/ready-check", "lobby ready check"},
            {"/lol-gameflow/v1/session", "gameflow session check"}
        };

        for (const auto& endpoint_pair : ready_endpoints) {
            const std::string& endpoint = endpoint_pair.first;
            const std::string& description = endpoint_pair.second;

            std::string response = MakeLCURequest(endpoint);
            if (!response.empty() && response.find("errorCode") == std::string::npos) {
                AddLogMessage("Testing " + description + ":");

                // Check for ready check indicators
                if (response.find("\"state\":\"InProgress\"") != std::string::npos ||
                    response.find("\"readyCheck\"") != std::string::npos ||
                    response.find("\"ReadyCheck\"") != std::string::npos ||
                    response.find("\"playerResponse\":\"None\"") != std::string::npos) {

                    AddLogMessage("  ✓ Ready check detected via " + description);
                    AddLogMessage("  Response: " + response.substr(0, 150) + "...");
                    return true;
                }
            }
        }
        return false;
    }

    bool AcceptReadyCheck() {
        AddLogMessage("=== READY CHECK ACCEPTANCE ATTEMPT ===");

        // First try to get ready check status
        std::string status_response = MakeLCURequest("/lol-matchmaking/v1/ready-check");
        if (!status_response.empty() && status_response.find("errorCode") == std::string::npos) {
            AddLogMessage("Ready check status: " + status_response.substr(0, 100) + "...");

            // Check if already accepted
            if (status_response.find("\"playerResponse\":\"Accepted\"") != std::string::npos) {
                AddLogMessage("✓ Ready check already accepted - skipping");
                return true;
            }
        }

        // Try multiple acceptance endpoints (primary endpoint first)
        std::vector<std::string> accept_endpoints = {
            "/lol-matchmaking/v1/ready-check/accept",  // Primary endpoint per Riot docs
            "/lol-lobby/v2/ready-check/accept",
            "/lol-gameflow/v1/ready-check/accept"
        };

        for (const std::string& endpoint : accept_endpoints) {
            AddLogMessage("Trying acceptance via: " + endpoint);
            std::string response = MakeLCURequest(endpoint, "POST");

            if (response.empty()) {
                AddLogMessage("  → Empty response");
                continue;
            }

            // Check for success response
            if (response.find("errorCode") != std::string::npos) {
                // Extract error details
                size_t error_start = response.find("\"errorCode\":");
                if (error_start != std::string::npos) {
                    size_t quote_start = response.find("\"", error_start + 12);
                    if (quote_start != std::string::npos) {
                        size_t quote_end = response.find("\"", quote_start + 1);
                        if (quote_end != std::string::npos) {
                            std::string error_code = response.substr(quote_start + 1, quote_end - quote_start - 1);
                            AddLogMessage("  → Error: " + error_code);
                        }
                    }
                }
                continue;
            }

            AddLogMessage("  ✓ Success! Response: " + response.substr(0, 100) + "...");
            AddLogMessage("=== READY CHECK ACCEPTED SUCCESSFULLY ===");
            return true;
        }

        AddLogMessage("=== ALL ACCEPTANCE METHODS FAILED ===");
        return false;
    }

    void ShowNotification(const std::string& message) {
        nid.uFlags = NIF_INFO;
        strcpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle), "League Auto-Accept");
        strcpy_s(nid.szInfo, sizeof(nid.szInfo), message.c_str());
        nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconA(NIM_MODIFY, &nid);
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE; // Reset flags
    }

    void RegisterEmergencyHotkey() {
        RegisterHotKey(main_window, 1, 0, VK_F9);
        AddLogMessage("Emergency hotkey registered (F9)");
    }

    bool LoadConfiguration() {
        std::ifstream file("config.json");
        if (!file.is_open()) {
            SaveConfiguration();
            return true;
        }

        // Simple JSON parsing for basic config
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("\"auto_accept_enabled\"") != std::string::npos) {
                config.auto_accept_enabled = line.find("true") != std::string::npos;
            }
            else if (line.find("\"polling_interval_ms\"") != std::string::npos) {
                size_t pos = line.find(":") + 1;
                size_t end = line.find(",", pos);
                if (end == std::string::npos) end = line.find("}", pos);
                if (pos != std::string::npos && end != std::string::npos) {
                    std::string value = line.substr(pos, end - pos);
                    value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
                    config.polling_interval_ms = std::stoi(value);
                }
            }
        }

        return true;
    }

    void SaveConfiguration() {
        std::ofstream file("config.json");
        if (file.is_open()) {
            file << "{\n";
            file << "  \"auto_accept_enabled\": " << (config.auto_accept_enabled ? "true" : "false") << ",\n";
            file << "  \"polling_interval_ms\": " << config.polling_interval_ms << ",\n";
            file << "  \"lcu_timeout_ms\": " << config.lcu_timeout_ms << ",\n";
            file << "  \"emergency_hotkey\": \"" << config.emergency_hotkey << "\",\n";
            file << "  \"enable_notifications\": " << (config.enable_notifications ? "true" : "false") << ",\n";
            file << "  \"startup_enabled\": " << (config.startup_enabled ? "true" : "false") << ",\n";
            file << "  \"minimize_to_tray\": " << (config.minimize_to_tray ? "true" : "false") << ",\n";
            file << "  \"show_notifications\": " << (config.show_notifications ? "true" : "false") << "\n";
            file << "}\n";
        }
    }

    void ShowSettingsDialog() {
        MessageBoxA(main_window,
                   "Settings dialog will be implemented in next version.\n\n"
                   "Current settings:\n"
                   "- Polling interval: 250ms\n"
                   "- Emergency hotkey: F9\n"
                   "- Notifications: Enabled\n\n"
                   "Edit config.json file to modify settings.",
                   "Settings", MB_OK | MB_ICONINFORMATION);
    }

    void Shutdown() {
        emergency_stop = true;
        StopMonitoring();

        // Remove system tray icon
        Shell_NotifyIconA(NIM_DELETE, &nid);

        // Unregister hotkey
        UnregisterHotKey(main_window, 1);

        // Save configuration
        SaveConfiguration();
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        LeagueAutoAcceptGUI* app = nullptr;

        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            app = (LeagueAutoAcceptGUI*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        } else {
            app = (LeagueAutoAcceptGUI*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        if (app) {
            return app->HandleMessage(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case IDC_ENABLE_CHECKBOX:
                auto_accept_enabled = SendMessage(enable_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                config.auto_accept_enabled = auto_accept_enabled;
                UpdateUI();
                AddLogMessage(auto_accept_enabled ? "Auto-accept ENABLED" : "Auto-accept DISABLED");
                break;

            case IDC_START_BUTTON:
                StartMonitoring();
                break;

            case IDC_STOP_BUTTON:
                StopMonitoring();
                break;

            case IDC_CONFIG_BUTTON:
                ShowSettingsDialog();
                break;

            case IDC_MINIMIZE_BUTTON:
                ShowWindow(main_window, SW_HIDE);
                AddLogMessage("Minimized to system tray");
                break;

            case IDM_SHOW_WINDOW:
                ShowWindow(main_window, SW_SHOW);
                SetForegroundWindow(main_window);
                break;

            case IDM_TOGGLE_ACCEPT:
                auto_accept_enabled = !auto_accept_enabled;
                config.auto_accept_enabled = auto_accept_enabled;
                UpdateUI();
                AddLogMessage(auto_accept_enabled ? "Auto-accept ENABLED" : "Auto-accept DISABLED");
                break;

            case IDM_EXIT:
                PostQuitMessage(0);
                break;

            case IDM_COPY_LOGS:
                CopyLogsToClipboard();
                break;

            case IDM_SELECT_ALL_LOGS:
                SelectAllLogs();
                break;
            }
            break;

        case WM_TRAY_CALLBACK:
            if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (lParam == WM_LBUTTONUP) {
                auto_accept_enabled = !auto_accept_enabled;
                config.auto_accept_enabled = auto_accept_enabled;
                UpdateUI();
                AddLogMessage(auto_accept_enabled ? "Auto-accept ENABLED (tray click)" : "Auto-accept DISABLED (tray click)");
            }
            break;

        case WM_HOTKEY:
            if (wParam == 1) { // F9 emergency hotkey
                emergency_stop = true;
                auto_accept_enabled = false;
                StopMonitoring();
                AddLogMessage("EMERGENCY STOP activated! (F9 pressed)");
                UpdateUI();
                ShowNotification("Emergency stop activated!");
            }
            break;

        case WM_KEYDOWN:
            // The edit control handles Ctrl+A and Ctrl+C natively, but we can add our own handling if needed
            if (GetFocus() == log_listbox) {
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                    if (wParam == 'A') {
                        SelectAllLogs();
                        return 0;
                    }
                    // Let Ctrl+C be handled by the edit control naturally
                }
            }
            break;

        case WM_RBUTTONUP:
            // Check if right-click was on the log listbox
            POINT cursor;
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);

            RECT listbox_rect;
            GetWindowRect(log_listbox, &listbox_rect);
            ScreenToClient(hwnd, (POINT*)&listbox_rect.left);
            ScreenToClient(hwnd, (POINT*)&listbox_rect.right);

            if (PtInRect(&listbox_rect, cursor)) {
                GetCursorPos(&cursor);
                ShowLogContextMenu(cursor.x, cursor.y);
                return 0;
            }
            break;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                ResizeControls();
            }
            break;

        case WM_CLOSE:
            // Close button should always close the program
            // Only minimize to tray when minimize button is clicked
            PostQuitMessage(0);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void RunMessageLoop() {
        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

LeagueAutoAcceptGUI* LeagueAutoAcceptGUI::instance = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LeagueAutoAcceptGUI app;

    if (!app.Initialize()) {
        MessageBoxA(nullptr, "Failed to initialize application", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    app.AddLogMessage("League Auto-Accept GUI started");
    app.AddLogMessage("Press F9 for emergency disable");
    app.AddLogMessage("Click 'Start Monitoring' to begin");

    app.RunMessageLoop();

    return 0;
}