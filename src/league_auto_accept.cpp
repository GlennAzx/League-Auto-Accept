#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>

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

class LeagueAutoAccept {
private:
    std::string lcu_token;
    int lcu_port = 0;
    std::atomic<bool> running{false};
    std::atomic<bool> auto_accept_enabled{false};
    std::atomic<bool> emergency_stop{false};

    // Configuration
    struct Config {
        bool auto_accept_enabled = true;
        int polling_interval_ms = 250;
        int lcu_timeout_ms = 5000;
        std::string emergency_hotkey = "F9";
        bool enable_notifications = true;
        bool startup_enabled = false;
    } config;

    NOTIFYICONDATAA nid = {0};
    HWND hidden_window = nullptr;

public:
    bool Initialize() {
        std::cout << "League Auto-Accept v1.0" << std::endl;
        std::cout << "======================" << std::endl;

        // Load configuration
        LoadConfiguration();

        // Create hidden window for message handling
        CreateHiddenWindow();

        // Setup system tray
        SetupSystemTray();

        // Register emergency hotkey
        RegisterEmergencyHotkey();

        return true;
    }

    bool LoadConfiguration() {
        std::cout << "[>] Loading configuration..." << std::endl;

        std::ifstream file("config.json");
        if (!file.is_open()) {
            std::cout << "[!] Config file not found, using defaults" << std::endl;
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
                std::string value = line.substr(pos, end - pos);
                // Remove whitespace and quotes
                value.erase(0, value.find_first_not_of(" \t\""));
                value.erase(value.find_last_not_of(" \t\",") + 1);
                config.polling_interval_ms = std::stoi(value);
            }
        }
        file.close();

        auto_accept_enabled = config.auto_accept_enabled;
        std::cout << "[+] Configuration loaded" << std::endl;
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
            file << "  \"startup_enabled\": " << (config.startup_enabled ? "true" : "false") << "\n";
            file << "}\n";
            file.close();
            std::cout << "[+] Configuration saved" << std::endl;
        }
    }

    bool ReadLCULockfile() {
        char* appdata = getenv("LOCALAPPDATA");
        if (!appdata) return false;

        std::string lockfile_path = std::string(appdata) + "\\Riot Games\\Riot Client\\Config\\lockfile";
        std::ifstream file(lockfile_path);
        if (!file.is_open()) return false;

        std::string content;
        std::getline(file, content);
        file.close();

        std::vector<std::string> parts;
        std::stringstream ss(content);
        std::string item;
        while (std::getline(ss, item, ':')) {
            parts.push_back(item);
        }

        if (parts.size() >= 4) {
            lcu_port = std::stoi(parts[2]);
            lcu_token = parts[3];
            return true;
        }
        return false;
    }

    std::string MakeLCURequest(const std::wstring& endpoint, const std::wstring& method = L"GET", const std::string& body = "") {
        if (lcu_port == 0 || lcu_token.empty()) {
            if (!ReadLCULockfile()) return "";
        }

        HINTERNET hSession = WinHttpOpen(
            L"League Auto-Accept/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);

        if (!hSession) return "";

        HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", lcu_port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "";
        }

        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect, method.c_str(), endpoint.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        // Set SSL options (ignore certificate errors for self-signed cert)
        DWORD flags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                      SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                      SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

        // Set authorization header
        std::string auth_string = "riot:" + lcu_token;
        std::string encoded_auth = base64_encode(auth_string);
        std::string auth_header = "Authorization: Basic " + encoded_auth;
        std::wstring wauth_header(auth_header.begin(), auth_header.end());

        WinHttpAddRequestHeaders(hRequest, wauth_header.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);

        // Send request
        BOOL result;
        if (body.empty()) {
            result = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        } else {
            result = WinHttpSendRequest(hRequest, L"Content-Type: application/json\r\n", -1,
                (LPVOID)body.c_str(), body.length(), body.length(), 0);
        }

        std::string response;
        if (result && WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            do {
                DWORD dwDownloaded = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;

                if (dwSize > 0) {
                    std::vector<char> buffer(dwSize + 1);
                    if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                        buffer[dwDownloaded] = 0;
                        response += buffer.data();
                    }
                }
            } while (dwSize > 0);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        return response;
    }

    std::string GetGameflowPhase() {
        std::string response = MakeLCURequest(L"/lol-gameflow/v1/gameflow-phase");
        if (!response.empty()) {
            // Remove quotes from JSON string response
            if (response.front() == '"' && response.back() == '"') {
                response = response.substr(1, response.length() - 2);
            }
        }
        return response;
    }

    bool AcceptReadyCheck() {
        std::string response = MakeLCURequest(L"/lol-matchmaking/v1/ready-check/accept", L"POST");
        return !response.empty();
    }

    void CreateHiddenWindow() {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "LeagueAutoAcceptClass";
        RegisterClassA(&wc);

        hidden_window = CreateWindowA("LeagueAutoAcceptClass", "League Auto-Accept",
            0, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), this);
    }

    void SetupSystemTray() {
        nid.cbSize = sizeof(NOTIFYICONDATAA);
        nid.hWnd = hidden_window;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        nid.uCallbackMessage = WM_USER + 1;
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        strcpy_s(nid.szTip, sizeof(nid.szTip), "League Auto-Accept");

        Shell_NotifyIconA(NIM_ADD, &nid);
        UpdateTrayIcon();
    }

    void UpdateTrayIcon() {
        if (auto_accept_enabled) {
            strcpy_s(nid.szTip, sizeof(nid.szTip), "League Auto-Accept: ENABLED");
        } else {
            strcpy_s(nid.szTip, sizeof(nid.szTip), "League Auto-Accept: DISABLED");
        }
        Shell_NotifyIconA(NIM_MODIFY, &nid);
    }

    void RegisterEmergencyHotkey() {
        RegisterHotKey(hidden_window, 1, 0, VK_F9); // F9 emergency disable
        std::cout << "[+] Emergency hotkey registered (F9)" << std::endl;
    }

    void ShowNotification(const std::string& message) {
        if (!config.enable_notifications) return;

        nid.uFlags |= NIF_INFO;
        strcpy_s(nid.szInfo, sizeof(nid.szInfo), message.c_str());
        strcpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle), "League Auto-Accept");
        nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconA(NIM_MODIFY, &nid);
        nid.uFlags &= ~NIF_INFO; // Remove info flag
    }

    void MainLoop() {
        std::cout << "[+] Starting main loop..." << std::endl;
        std::cout << "[>] Auto-accept: " << (auto_accept_enabled ? "ENABLED" : "DISABLED") << std::endl;
        std::cout << "[>] Press F9 for emergency disable" << std::endl;
        std::cout << "[>] Check system tray for status" << std::endl;

        running = true;
        auto last_phase = std::string();
        int consecutive_errors = 0;

        while (running && !emergency_stop) {
            try {
                if (auto_accept_enabled) {
                    std::string current_phase = GetGameflowPhase();

                    if (!current_phase.empty()) {
                        consecutive_errors = 0;

                        if (current_phase != last_phase) {
                            std::cout << "[>] Phase: " << current_phase << std::endl;
                            last_phase = current_phase;
                        }

                        if (current_phase == "ReadyCheck") {
                            std::cout << "[!] READY CHECK DETECTED - Auto-accepting..." << std::endl;

                            if (AcceptReadyCheck()) {
                                std::cout << "[+] Ready check accepted successfully!" << std::endl;
                                ShowNotification("Ready check accepted!");
                            } else {
                                std::cout << "[!] Failed to accept ready check" << std::endl;
                                ShowNotification("Failed to accept ready check");
                            }
                        }
                    } else {
                        consecutive_errors++;
                        if (consecutive_errors == 5) {
                            std::cout << "[!] Cannot connect to League client" << std::endl;
                        }
                    }
                }

                // Process Windows messages (for hotkeys and tray)
                MSG msg;
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        running = false;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(config.polling_interval_ms));

            } catch (...) {
                std::cout << "[X] Exception in main loop" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }

        if (emergency_stop) {
            std::cout << "[!] EMERGENCY STOP activated!" << std::endl;
            ShowNotification("Emergency stop activated!");
        }

        std::cout << "[+] Main loop stopped" << std::endl;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        LeagueAutoAccept* app = nullptr;

        if (uMsg == WM_CREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = reinterpret_cast<LeagueAutoAccept*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<LeagueAutoAccept*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        switch (uMsg) {
            case WM_HOTKEY:
                if (wParam == 1) { // F9 emergency disable
                    if (app) {
                        app->emergency_stop = true;
                        app->auto_accept_enabled = false;
                        app->UpdateTrayIcon();
                    }
                }
                break;

            case WM_USER + 1: // Tray icon message
                if (lParam == WM_RBUTTONUP) {
                    if (app) app->ShowTrayMenu();
                } else if (lParam == WM_LBUTTONUP) {
                    if (app) {
                        app->auto_accept_enabled = !app->auto_accept_enabled;
                        app->UpdateTrayIcon();
                        if (app->auto_accept_enabled) {
                            std::cout << "[+] Auto-accept ENABLED" << std::endl;
                        } else {
                            std::cout << "[!] Auto-accept DISABLED" << std::endl;
                        }
                    }
                }
                break;

            case WM_DESTROY:
                PostQuitMessage(0);
                break;

            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }

    void ShowTrayMenu() {
        POINT pt;
        GetCursorPos(&pt);

        HMENU hMenu = CreatePopupMenu();
        InsertMenuA(hMenu, 0, MF_BYPOSITION | (auto_accept_enabled ? MF_CHECKED : MF_UNCHECKED), 1, "Auto-Accept");
        InsertMenuA(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        InsertMenuA(hMenu, 2, MF_BYPOSITION, 2, "Exit");

        SetForegroundWindow(hidden_window);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hidden_window, NULL);

        switch (cmd) {
            case 1: // Toggle auto-accept
                auto_accept_enabled = !auto_accept_enabled;
                UpdateTrayIcon();
                break;
            case 2: // Exit
                running = false;
                PostMessage(hidden_window, WM_QUIT, 0, 0);
                break;
        }

        DestroyMenu(hMenu);
    }

    void Shutdown() {
        std::cout << "[>] Shutting down..." << std::endl;

        running = false;

        // Unregister hotkey
        UnregisterHotKey(hidden_window, 1);

        // Remove system tray icon
        Shell_NotifyIconA(NIM_DELETE, &nid);

        // Save configuration
        config.auto_accept_enabled = auto_accept_enabled;
        SaveConfiguration();

        std::cout << "[+] Shutdown complete" << std::endl;
    }
};

int main() {
    std::cout << "Starting League Auto-Accept..." << std::endl;

    LeagueAutoAccept app;

    if (!app.Initialize()) {
        std::cout << "[X] Failed to initialize application" << std::endl;
        return 1;
    }

    // Run the main loop
    app.MainLoop();

    // Cleanup
    app.Shutdown();

    return 0;
}