#include "league_auto_accept/models/lcu_connection_info.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <shlobj.h>
#include <windows.h>
#include <tlhelp32.h>

namespace league_auto_accept {
namespace models {

LCUConnectionInfo::LCUConnectionInfo()
    : port_(DEFAULT_LCU_PORT)
    , process_id_(0)
    , connection_state_(LCUConnectionState::DISCONNECTED)
    , lockfile_path_(GetDefaultLockfilePath())
    , last_successful_request_(std::chrono::steady_clock::now())
    , connection_errors_(0) {
}

bool LCUConnectionInfo::IsConnectionRecent(std::chrono::seconds max_age) const {
    if (!IsConnected()) return false;

    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_successful_request_);
    return age <= max_age;
}

bool LCUConnectionInfo::DiscoverFromLockfile() {
    return DiscoverFromLockfile(lockfile_path_);
}

bool LCUConnectionInfo::DiscoverFromLockfile(const std::filesystem::path& lockfile_path) {
    lockfile_path_ = lockfile_path;

    if (!std::filesystem::exists(lockfile_path)) {
        SetConnectionState(LCUConnectionState::DISCONNECTED);
        return false;
    }

    try {
        std::ifstream file(lockfile_path);
        if (!file.is_open()) {
            SetConnectionState(LCUConnectionState::ERROR_STATE);
            return false;
        }

        std::string content;
        std::getline(file, content);

        if (!ValidateLockfileFormat(content)) {
            SetConnectionState(LCUConnectionState::ERROR_STATE);
            return false;
        }

        SetConnectionState(LCUConnectionState::CONNECTING);

        if (ParseLockfileContent(content)) {
            // Verify the process is still running
            if (IsLeagueProcessRunning(process_id_)) {
                ConstructBaseURL();
                SetConnectionState(LCUConnectionState::CONNECTED);
                ClearConnectionErrors();
                return true;
            }
        }

        SetConnectionState(LCUConnectionState::ERROR_STATE);
        return false;
    } catch (const std::exception&) {
        SetConnectionState(LCUConnectionState::ERROR_STATE);
        return false;
    }
}

void LCUConnectionInfo::SetConnectionState(LCUConnectionState state) {
    connection_state_ = state;

    if (state == LCUConnectionState::DISCONNECTED || state == LCUConnectionState::ERROR_STATE) {
        // Clear connection details when disconnected
        base_url_.clear();
        auth_token_.clear();
        process_id_ = 0;
        port_ = DEFAULT_LCU_PORT;
    }
}

void LCUConnectionInfo::UpdateLastSuccessfulRequest() {
    last_successful_request_ = std::chrono::steady_clock::now();
    ClearConnectionErrors();
}

void LCUConnectionInfo::IncrementConnectionErrors() {
    connection_errors_++;

    // After multiple consecutive errors, consider connection lost
    if (connection_errors_ >= 5) {
        SetConnectionState(LCUConnectionState::ERROR_STATE);
    }
}

void LCUConnectionInfo::ClearConnectionErrors() {
    connection_errors_ = 0;
}

void LCUConnectionInfo::Reset() {
    base_url_.clear();
    auth_token_.clear();
    process_id_ = 0;
    port_ = DEFAULT_LCU_PORT;
    connection_state_ = LCUConnectionState::DISCONNECTED;
    lockfile_path_ = GetDefaultLockfilePath();
    last_successful_request_ = std::chrono::steady_clock::now();
    connection_errors_ = 0;
}

bool LCUConnectionInfo::IsValid() const {
    return ValidatePort() && ValidateAuthToken() && ValidateProcessId();
}

bool LCUConnectionInfo::ValidatePort() const {
    return port_ >= MIN_PORT && port_ <= MAX_PORT;
}

bool LCUConnectionInfo::ValidateAuthToken() const {
    // Auth token should be non-empty when connected
    if (IsConnected()) {
        return !auth_token_.empty() && auth_token_.length() > 10; // Minimum reasonable length
    }
    return true; // OK to be empty when not connected
}

bool LCUConnectionInfo::ValidateProcessId() const {
    // Process ID should be valid when connected
    if (IsConnected()) {
        return process_id_ > 0 && IsLeagueProcessRunning(process_id_);
    }
    return true; // OK to be 0 when not connected
}

std::string LCUConnectionInfo::GetConnectionStateString() const {
    return LCUConnectionStateToString(connection_state_);
}

std::string LCUConnectionInfo::GetFullAPIURL(const std::string& endpoint) const {
    if (base_url_.empty()) {
        throw std::runtime_error("No LCU connection established");
    }

    // Ensure endpoint starts with /
    std::string clean_endpoint = endpoint;
    if (!clean_endpoint.empty() && clean_endpoint[0] != '/') {
        clean_endpoint = "/" + clean_endpoint;
    }

    return base_url_ + clean_endpoint;
}

std::filesystem::path LCUConnectionInfo::GetDefaultLockfilePath() {
    wchar_t* local_appdata = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_appdata) == S_OK) {
        std::filesystem::path path(local_appdata);
        CoTaskMemFree(local_appdata);
        return path / "Riot Games" / "Riot Client" / "Config" / "lockfile";
    }

    // Fallback
    return std::filesystem::temp_directory_path() / "lockfile";
}

bool LCUConnectionInfo::IsLeagueProcessRunning(DWORD process_id) {
    if (process_id == 0) return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
    if (process == NULL) return false;

    DWORD exit_code;
    bool is_running = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(process);

    return is_running;
}

DWORD LCUConnectionInfo::FindLeagueProcessId() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &pe32)) {
        do {
            if (wcscmp(pe32.szExeFile, L"LeagueClient.exe") == 0) {
                CloseHandle(snapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &pe32));
    }

    CloseHandle(snapshot);
    return 0;
}

bool LCUConnectionInfo::ParseLockfileContent(const std::string& content) {
    // Lockfile format: LeagueClient:PID:PORT:PASSWORD:PROTOCOL
    std::istringstream iss(content);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(iss, token, ':')) {
        tokens.push_back(token);
    }

    if (tokens.size() != 5) return false;

    try {
        // tokens[0] should be "LeagueClient"
        if (tokens[0] != "LeagueClient") return false;

        // Parse process ID
        process_id_ = static_cast<DWORD>(std::stoul(tokens[1]));

        // Parse port
        port_ = std::stoi(tokens[2]);

        // Auth token (password)
        auth_token_ = tokens[3];

        // Protocol should be "https"
        if (tokens[4] != "https") return false;

        return ValidatePort() && !auth_token_.empty() && process_id_ > 0;
    } catch (const std::exception&) {
        return false;
    }
}

void LCUConnectionInfo::ConstructBaseURL() {
    base_url_ = "https://127.0.0.1:" + std::to_string(port_);
}

bool LCUConnectionInfo::ValidateLockfileFormat(const std::string& content) const {
    // Basic format check: should have 4 colons (5 parts)
    return std::count(content.begin(), content.end(), ':') == 4 &&
           content.find("LeagueClient") == 0;
}

// Utility functions
std::string LCUConnectionStateToString(LCUConnectionState state) {
    switch (state) {
    case LCUConnectionState::DISCONNECTED: return "DISCONNECTED";
    case LCUConnectionState::CONNECTING: return "CONNECTING";
    case LCUConnectionState::CONNECTED: return "CONNECTED";
    case LCUConnectionState::ERROR_STATE: return "ERROR";
    default: return "UNKNOWN";
    }
}

LCUConnectionState StringToLCUConnectionState(const std::string& state_str) {
    if (state_str == "DISCONNECTED") return LCUConnectionState::DISCONNECTED;
    if (state_str == "CONNECTING") return LCUConnectionState::CONNECTING;
    if (state_str == "CONNECTED") return LCUConnectionState::CONNECTED;
    if (state_str == "ERROR") return LCUConnectionState::ERROR_STATE;

    throw std::invalid_argument("Unknown LCU connection state: " + state_str);
}

} // namespace models
} // namespace league_auto_accept