#include "league_auto_accept/utils/process_monitor.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>

namespace league_auto_accept {
namespace utils {

ProcessMonitor::ProcessMonitor()
    : check_interval_(std::chrono::milliseconds(1000))
    , monitoring_(false)
    , should_stop_(false)
    , last_process_state_(false) {
}

ProcessMonitor::~ProcessMonitor() {
    StopMonitoring();
}

bool ProcessMonitor::StartMonitoring(const std::string& process_name, std::chrono::milliseconds check_interval) {
    if (monitoring_) return false;

    target_process_name_ = process_name;
    check_interval_ = check_interval;
    should_stop_ = false;

    // Initial state check
    ProcessInfo info;
    last_process_state_ = FindProcess(target_process_name_, info);
    if (last_process_state_) {
        last_known_info_ = info;
    }

    monitoring_ = true;
    monitor_thread_ = std::thread([this]() { MonitoringLoop(); });

    return true;
}

void ProcessMonitor::StopMonitoring() {
    if (!monitoring_) return;

    should_stop_ = true;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    monitoring_ = false;
}

bool ProcessMonitor::IsMonitoring() const {
    return monitoring_;
}

bool ProcessMonitor::FindProcess(const std::string& process_name, ProcessInfo& info) {
    std::vector<DWORD> process_ids;
    if (!EnumerateProcesses(process_ids)) {
        return false;
    }

    for (DWORD pid : process_ids) {
        std::string name, path;
        if (GetProcessModuleInfo(pid, name, path)) {
            if (_stricmp(name.c_str(), process_name.c_str()) == 0) {
                info.process_id = pid;
                info.process_name = name;
                info.executable_path = path;
                info.main_window = FindMainWindow(pid);
                info.is_running = true;
                info.detected_time = std::chrono::steady_clock::now();
                return true;
            }
        }
    }

    return false;
}

std::vector<ProcessInfo> ProcessMonitor::FindAllProcesses(const std::string& process_name) {
    std::vector<ProcessInfo> results;
    std::vector<DWORD> process_ids;

    if (!EnumerateProcesses(process_ids)) {
        return results;
    }

    for (DWORD pid : process_ids) {
        std::string name, path;
        if (GetProcessModuleInfo(pid, name, path)) {
            if (_stricmp(name.c_str(), process_name.c_str()) == 0) {
                ProcessInfo info;
                info.process_id = pid;
                info.process_name = name;
                info.executable_path = path;
                info.main_window = FindMainWindow(pid);
                info.is_running = true;
                info.detected_time = std::chrono::steady_clock::now();
                results.push_back(info);
            }
        }
    }

    return results;
}

bool ProcessMonitor::IsProcessRunning(DWORD process_id) {
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
    if (!process) return false;

    DWORD exit_code;
    bool running = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(process);

    return running;
}

bool ProcessMonitor::IsProcessRunning(const std::string& process_name) {
    ProcessInfo info;
    return FindProcess(process_name, info);
}

ProcessInfo ProcessMonitor::GetLeagueClientInfo() {
    ProcessInfo info;
    FindProcess("LeagueClient.exe", info);
    return info;
}

bool ProcessMonitor::IsLeagueClientRunning() {
    return IsProcessRunning("LeagueClient.exe");
}

DWORD ProcessMonitor::GetLeagueClientProcessId() {
    ProcessInfo info = GetLeagueClientInfo();
    return info.is_running ? info.process_id : 0;
}

HWND ProcessMonitor::GetLeagueClientWindow() {
    ProcessInfo info = GetLeagueClientInfo();
    return info.main_window;
}

std::string ProcessMonitor::GetProcessName(DWORD process_id) {
    std::string name, path;
    GetProcessModuleInfo(process_id, name, path);
    return name;
}

std::string ProcessMonitor::GetProcessPath(DWORD process_id) {
    std::string name, path;
    GetProcessModuleInfo(process_id, name, path);
    return path;
}

HWND ProcessMonitor::GetMainWindow(DWORD process_id) {
    return FindMainWindow(process_id);
}

bool ProcessMonitor::GetProcessInfo(DWORD process_id, ProcessInfo& info) {
    if (!IsProcessRunning(process_id)) {
        info.is_running = false;
        return false;
    }

    std::string name, path;
    if (GetProcessModuleInfo(process_id, name, path)) {
        info.process_id = process_id;
        info.process_name = name;
        info.executable_path = path;
        info.main_window = FindMainWindow(process_id);
        info.is_running = true;
        info.detected_time = std::chrono::steady_clock::now();
        return true;
    }

    return false;
}

void ProcessMonitor::SetProcessEventCallback(ProcessEventCallback callback) {
    process_callback_ = callback;
}

void ProcessMonitor::SetCheckInterval(std::chrono::milliseconds interval) {
    check_interval_ = interval;
}

std::chrono::milliseconds ProcessMonitor::GetCheckInterval() const {
    return check_interval_;
}

std::vector<DWORD> ProcessMonitor::GetAllProcessIds() {
    std::vector<DWORD> process_ids;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return process_ids;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &pe32)) {
        do {
            process_ids.push_back(pe32.th32ProcessID);
        } while (Process32Next(snapshot, &pe32));
    }

    CloseHandle(snapshot);
    return process_ids;
}

std::vector<std::string> ProcessMonitor::GetAllProcessNames() {
    std::vector<std::string> names;
    auto process_ids = GetAllProcessIds();

    for (DWORD pid : process_ids) {
        std::string name = ProcessMonitor().GetProcessName(pid);
        if (!name.empty()) {
            names.push_back(name);
        }
    }

    return names;
}

bool ProcessMonitor::TerminateProcessSafely(DWORD process_id) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
    if (!process) return false;

    bool success = TerminateProcess(process, 0) != FALSE;
    CloseHandle(process);

    return success;
}

bool ProcessMonitor::WaitForProcessExit(DWORD process_id, std::chrono::milliseconds timeout) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
    if (!process) return true; // Process doesn't exist

    DWORD wait_result = WaitForSingleObject(process, static_cast<DWORD>(timeout.count()));
    CloseHandle(process);

    return wait_result == WAIT_OBJECT_0;
}

void ProcessMonitor::MonitoringLoop() {
    while (!should_stop_) {
        CheckProcessState();
        std::this_thread::sleep_for(check_interval_);
    }
}

void ProcessMonitor::CheckProcessState() {
    ProcessInfo current_info;
    bool current_state = FindProcess(target_process_name_, current_info);

    if (current_state != last_process_state_) {
        // State changed
        if (current_state) {
            // Process started
            last_known_info_ = current_info;
            NotifyProcessEvent(current_info, true);
        } else {
            // Process stopped
            last_known_info_.is_running = false;
            NotifyProcessEvent(last_known_info_, false);
        }

        last_process_state_ = current_state;
    } else if (current_state) {
        // Process still running, update info
        last_known_info_ = current_info;
    }
}

void ProcessMonitor::NotifyProcessEvent(const ProcessInfo& info, bool started) {
    if (process_callback_) {
        process_callback_(info, started);
    }
}

bool ProcessMonitor::EnumerateProcesses(std::vector<DWORD>& process_ids) {
    process_ids = GetAllProcessIds();
    return !process_ids.empty();
}

HWND ProcessMonitor::FindMainWindow(DWORD process_id) {
    WindowEnumData data;
    data.target_process_id = process_id;
    data.result_window = nullptr;

    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.result_window;
}

bool ProcessMonitor::GetProcessModuleInfo(DWORD process_id, std::string& name, std::string& path) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32 me32;
    me32.dwSize = sizeof(MODULEENTRY32);

    bool success = false;
    if (Module32First(snapshot, &me32)) {
        name = me32.szModule;
        path = me32.szExePath;
        success = true;
    }

    CloseHandle(snapshot);
    return success;
}

BOOL CALLBACK ProcessMonitor::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowEnumData* data = reinterpret_cast<WindowEnumData*>(lParam);

    DWORD window_process_id;
    GetWindowThreadProcessId(hwnd, &window_process_id);

    if (window_process_id == data->target_process_id) {
        // Check if this is a visible main window
        if (IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
            data->result_window = hwnd;
            return FALSE; // Stop enumeration
        }
    }

    return TRUE; // Continue enumeration
}

} // namespace utils
} // namespace league_auto_accept