#include "league_auto_accept/config_manager.h"
#include <fstream>
#include <stdexcept>
#include <shlobj.h>
#include <windows.h>

namespace league_auto_accept {

// AppConfig implementation
bool AppConfig::IsValid() const {
    try {
        ValidateAndThrow();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void AppConfig::ValidateAndThrow() const {
    if (!ConfigManager::ValidatePollingInterval(polling_interval)) {
        throw std::invalid_argument("polling_interval must be between 100-5000ms");
    }
    if (!ConfigManager::ValidateUIScaleFactor(ui_scale_factor)) {
        throw std::invalid_argument("ui_scale_factor must be between 0.5-3.0");
    }
    if (!ConfigManager::ValidateTemplateMatchThreshold(template_match_threshold)) {
        throw std::invalid_argument("template_match_threshold must be between 0.6-0.95");
    }
    if (!ConfigManager::ValidateHotkey(emergency_hotkey)) {
        throw std::invalid_argument("invalid emergency_hotkey format");
    }
    if (lcu_timeout < 1000 || lcu_timeout > 10000) {
        throw std::invalid_argument("lcu_timeout must be between 1000-10000ms");
    }
}

nlohmann::json AppConfig::ToJson() const {
    return {
        {"auto_accept_enabled", auto_accept_enabled},
        {"detection_method", GetDetectionMethodString()},
        {"polling_interval", polling_interval},
        {"lcu_timeout", lcu_timeout},
        {"ui_scale_factor", ui_scale_factor},
        {"template_match_threshold", template_match_threshold},
        {"enable_notifications", enable_notifications},
        {"enable_sound", enable_sound},
        {"emergency_hotkey", emergency_hotkey},
        {"startup_enabled", startup_enabled},
        {"log_level", GetLogLevelString()}
    };
}

void AppConfig::FromJson(const nlohmann::json& json) {
    auto_accept_enabled = json.value("auto_accept_enabled", false);
    polling_interval = json.value("polling_interval", 500);
    lcu_timeout = json.value("lcu_timeout", 5000);
    ui_scale_factor = json.value("ui_scale_factor", 1.0);
    template_match_threshold = json.value("template_match_threshold", 0.8);
    enable_notifications = json.value("enable_notifications", true);
    enable_sound = json.value("enable_sound", false);
    emergency_hotkey = json.value("emergency_hotkey", std::string("F9"));
    startup_enabled = json.value("startup_enabled", false);

    // Enum conversions with validation
    std::string method_str = json.value("detection_method", std::string("hybrid"));
    detection_method = StringToDetectionMethod(method_str);

    std::string level_str = json.value("log_level", std::string("info"));
    log_level = StringToLogLevel(level_str);

    ValidateAndThrow();
}

std::string AppConfig::GetDetectionMethodString() const {
    return DetectionMethodToString(detection_method);
}

std::string AppConfig::GetLogLevelString() const {
    return LogLevelToString(log_level);
}

// ConfigManager implementation
ConfigManager::ConfigManager()
    : current_config_(GetDefaultConfig())
    , file_watching_enabled_(false)
    , file_watch_handle_(nullptr)
    , stop_file_watching_(false) {
    InitializeConfigPath();
}

ConfigManager::ConfigManager(const std::filesystem::path& config_file_path)
    : current_config_(GetDefaultConfig())
    , config_file_path_(config_file_path)
    , file_watching_enabled_(false)
    , file_watch_handle_(nullptr)
    , stop_file_watching_(false) {
    EnsureConfigDirectoryExists();
}

ConfigManager::~ConfigManager() {
    StopFileWatching();
}

AppConfig ConfigManager::GetConfig() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_;
}

void ConfigManager::SetConfig(const AppConfig& config) {
    config.ValidateAndThrow();

    AppConfig old_config;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        old_config = current_config_;
        current_config_ = config;
    }

    NotifyConfigChange(old_config);
}

void ConfigManager::UpdateConfig(std::function<void(AppConfig&)> updater) {
    AppConfig old_config;
    AppConfig new_config;

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        old_config = current_config_;
        new_config = current_config_;
    }

    updater(new_config);
    new_config.ValidateAndThrow();

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        current_config_ = new_config;
    }

    NotifyConfigChange(old_config);
}

bool ConfigManager::GetAutoAcceptEnabled() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_.auto_accept_enabled;
}

void ConfigManager::SetAutoAcceptEnabled(bool enabled) {
    UpdateConfig([enabled](AppConfig& config) {
        config.auto_accept_enabled = enabled;
    });
}

DetectionMethod ConfigManager::GetDetectionMethod() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_.detection_method;
}

void ConfigManager::SetDetectionMethod(DetectionMethod method) {
    UpdateConfig([method](AppConfig& config) {
        config.detection_method = method;
    });
}

int ConfigManager::GetPollingInterval() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_.polling_interval;
}

void ConfigManager::SetPollingInterval(int interval_ms) {
    if (!ValidatePollingInterval(interval_ms)) {
        throw std::invalid_argument("Invalid polling interval");
    }

    UpdateConfig([interval_ms](AppConfig& config) {
        config.polling_interval = interval_ms;
    });
}

bool ConfigManager::LoadFromFile() {
    if (!ConfigFileExists()) {
        CreateDefaultConfig();
        return SaveToFile();
    }

    try {
        std::ifstream file(config_file_path_);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json;
        file >> json;

        AppConfig new_config;
        new_config.FromJson(json);

        SetConfig(new_config);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ConfigManager::SaveToFile() {
    try {
        EnsureConfigDirectoryExists();

        std::ofstream file(config_file_path_);
        if (!file.is_open()) {
            return false;
        }

        AppConfig config = GetConfig();
        file << config.ToJson().dump(4);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Validation methods
bool ConfigManager::ValidatePollingInterval(int interval_ms) {
    return interval_ms >= 100 && interval_ms <= 5000;
}

bool ConfigManager::ValidateUIScaleFactor(double factor) {
    return factor >= 0.5 && factor <= 3.0;
}

bool ConfigManager::ValidateTemplateMatchThreshold(double threshold) {
    return threshold >= 0.6 && threshold <= 0.95;
}

bool ConfigManager::ValidateDetectionMethod(const std::string& method_str) {
    return method_str == "primary_lcu" || method_str == "fallback_ui" || method_str == "hybrid";
}

bool ConfigManager::ValidateLogLevel(const std::string& level_str) {
    return level_str == "debug" || level_str == "info" ||
           level_str == "warning" || level_str == "error";
}

bool ConfigManager::ValidateHotkey(const std::string& hotkey) {
    // Simple validation - Windows virtual key names
    return hotkey == "F9" || hotkey == "F10" || hotkey == "F11" || hotkey == "F12" ||
           hotkey == "CTRL+F9" || hotkey == "ALT+F9" || hotkey == "SHIFT+F9";
}

AppConfig ConfigManager::GetDefaultConfig() {
    return AppConfig{}; // Uses default member initializers
}

std::filesystem::path ConfigManager::GetDefaultConfigPath() {
    return GetConfigDirectory() / "config.json";
}

void ConfigManager::InitializeConfigPath() {
    config_file_path_ = GetDefaultConfigPath();
    EnsureConfigDirectoryExists();
}

void ConfigManager::EnsureConfigDirectoryExists() {
    auto dir = config_file_path_.parent_path();
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}

std::filesystem::path ConfigManager::GetConfigDirectory() {
    return GetAppDataPath() / "League-Auto-Accept";
}

std::filesystem::path ConfigManager::GetAppDataPath() {
    wchar_t* path = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path) == S_OK) {
        std::filesystem::path result(path);
        CoTaskMemFree(path);
        return result;
    }
    return std::filesystem::temp_directory_path();
}

// Utility functions
std::string DetectionMethodToString(DetectionMethod method) {
    switch (method) {
    case DetectionMethod::PRIMARY_LCU: return "primary_lcu";
    case DetectionMethod::FALLBACK_UI: return "fallback_ui";
    case DetectionMethod::HYBRID: return "hybrid";
    default: return "hybrid";
    }
}

DetectionMethod StringToDetectionMethod(const std::string& method_str) {
    if (method_str == "primary_lcu") return DetectionMethod::PRIMARY_LCU;
    if (method_str == "fallback_ui") return DetectionMethod::FALLBACK_UI;
    if (method_str == "hybrid") return DetectionMethod::HYBRID;
    throw std::invalid_argument("Unknown detection method: " + method_str);
}

std::string LogLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG: return "debug";
    case LogLevel::INFO: return "info";
    case LogLevel::WARNING: return "warning";
    case LogLevel::ERROR_LEVEL: return "error";
    default: return "info";
    }
}

LogLevel StringToLogLevel(const std::string& level_str) {
    if (level_str == "debug") return LogLevel::DEBUG;
    if (level_str == "info") return LogLevel::INFO;
    if (level_str == "warning") return LogLevel::WARNING;
    if (level_str == "error") return LogLevel::ERROR_LEVEL;
    throw std::invalid_argument("Unknown log level: " + level_str);
}

} // namespace league_auto_accept