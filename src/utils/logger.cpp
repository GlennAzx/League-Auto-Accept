#include "league_auto_accept/utils/logger.h"
#include <shlobj.h>
#include <stdexcept>

namespace league_auto_accept {
namespace utils {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Initialize(const std::string& app_name, LogLevel level, const std::filesystem::path& log_dir) {
    try {
        current_level_ = level;
        console_enabled_ = true;
        file_enabled_ = true;

        // Set log directory
        if (log_dir.empty()) {
            wchar_t* appdata_path = nullptr;
            if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata_path) == S_OK) {
                log_directory_ = std::filesystem::path(appdata_path) / app_name / "logs";
                CoTaskMemFree(appdata_path);
            } else {
                log_directory_ = std::filesystem::temp_directory_path() / app_name / "logs";
            }
        } else {
            log_directory_ = log_dir;
        }

        if (!CreateLogDirectory()) {
            last_error_ = "Failed to create log directory: " + log_directory_.string();
            return false;
        }

        // Setup loggers
        SetupMainLogger();
        SetupMetricsLogger();
        SetupPerformanceLogger();

        // Set initial log level
        SetLevel(level);

        initialized_ = true;
        Info("Logger initialized successfully - Directory: {}", log_directory_.string());
        return true;

    } catch (const std::exception& e) {
        last_error_ = "Logger initialization failed: " + std::string(e.what());
        return false;
    }
}

void Logger::LogDetectionMetrics(int latency_ms, bool success, const std::string& method) {
    if (metrics_logger_) {
        metrics_logger_->info("DETECTION,{},{},{}", latency_ms, success ? "SUCCESS" : "FAILED", method);
    }
}

void Logger::LogAcceptanceMetrics(int latency_ms, bool success, const std::string& error) {
    if (metrics_logger_) {
        if (success) {
            metrics_logger_->info("ACCEPTANCE,{},SUCCESS,", latency_ms);
        } else {
            metrics_logger_->info("ACCEPTANCE,{},FAILED,{}", latency_ms, error);
        }
    }
}

void Logger::LogSystemMetrics(double memory_mb, double cpu_percent) {
    if (performance_logger_) {
        performance_logger_->info("SYSTEM,{:.2f},{:.2f}", memory_mb, cpu_percent);
    }
}

void Logger::LogConnectionEvent(bool connected, const std::string& endpoint, const std::string& error) {
    if (main_logger_) {
        if (connected) {
            Info("Connected to {}", endpoint);
        } else {
            Error("Connection failed to {} - {}", endpoint, error);
        }
    }
}

void Logger::LogUserAction(const std::string& action, const std::string& details) {
    if (main_logger_) {
        if (details.empty()) {
            Info("User action: {}", action);
        } else {
            Info("User action: {} - {}", action, details);
        }
    }
}

void Logger::SetLevel(LogLevel level) {
    current_level_ = level;
    auto spdlog_level = ConvertLogLevel(level);

    if (main_logger_) main_logger_->set_level(spdlog_level);
    if (metrics_logger_) metrics_logger_->set_level(spdlog_level);
    if (performance_logger_) performance_logger_->set_level(spdlog_level);
}

LogLevel Logger::GetLevel() const {
    return current_level_;
}

void Logger::SetPattern(const std::string& pattern) {
    if (main_logger_) main_logger_->set_pattern(pattern);
}

void Logger::EnableConsoleLogging(bool enable) {
    console_enabled_ = enable;
    // Would need to recreate loggers to change sinks in full implementation
    Info("Console logging {}", enable ? "enabled" : "disabled");
}

void Logger::EnableFileLogging(bool enable) {
    file_enabled_ = enable;
    // Would need to recreate loggers to change sinks in full implementation
    Info("File logging {}", enable ? "enabled" : "disabled");
}

void Logger::SetLogDirectory(const std::filesystem::path& dir) {
    log_directory_ = dir;
    CreateLogDirectory();
}

std::filesystem::path Logger::GetLogDirectory() const {
    return log_directory_;
}

void Logger::SetMaxFileSize(size_t size_mb) {
    // Would be implemented in rotating file sink configuration
    Info("Max file size set to {} MB", size_mb);
}

void Logger::SetMaxFiles(int count) {
    // Would be implemented in rotating file sink configuration
    Info("Max file count set to {}", count);
}

void Logger::FlushLogs() {
    if (main_logger_) main_logger_->flush();
    if (metrics_logger_) metrics_logger_->flush();
    if (performance_logger_) performance_logger_->flush();
}

bool Logger::HasErrors() const {
    return !last_error_.empty();
}

std::string Logger::GetLastError() const {
    return last_error_;
}

void Logger::ClearErrors() {
    last_error_.clear();
}

void Logger::Shutdown() {
    if (initialized_) {
        Info("Shutting down logger");
        FlushLogs();

        main_logger_.reset();
        metrics_logger_.reset();
        performance_logger_.reset();

        spdlog::shutdown();
        initialized_ = false;
    }
}

bool Logger::CreateLogDirectory() {
    try {
        if (!std::filesystem::exists(log_directory_)) {
            std::filesystem::create_directories(log_directory_);
        }
        return std::filesystem::exists(log_directory_);
    } catch (const std::exception& e) {
        last_error_ = "Failed to create log directory: " + std::string(e.what());
        return false;
    }
}

void Logger::SetupMainLogger() {
    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Console sink
        if (console_enabled_) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern(DEFAULT_PATTERN);
            sinks.push_back(console_sink);
        }

        // File sink
        if (file_enabled_) {
            auto file_path = log_directory_ / "app.log";
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file_path.string(), DEFAULT_MAX_FILE_SIZE_MB * 1024 * 1024, DEFAULT_MAX_FILES);
            file_sink->set_pattern(DEFAULT_PATTERN);
            sinks.push_back(file_sink);
        }

        main_logger_ = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        main_logger_->set_level(ConvertLogLevel(current_level_));
        spdlog::register_logger(main_logger_);

    } catch (const std::exception& e) {
        last_error_ = "Failed to setup main logger: " + std::string(e.what());
    }
}

void Logger::SetupMetricsLogger() {
    try {
        if (!file_enabled_) return;

        auto file_path = log_directory_ / "metrics.log";
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file_path.string(), DEFAULT_MAX_FILE_SIZE_MB * 1024 * 1024, DEFAULT_MAX_FILES);
        file_sink->set_pattern(METRICS_PATTERN);

        metrics_logger_ = std::make_shared<spdlog::logger>("metrics", file_sink);
        metrics_logger_->set_level(spdlog::level::info); // Always log metrics
        spdlog::register_logger(metrics_logger_);

    } catch (const std::exception& e) {
        last_error_ = "Failed to setup metrics logger: " + std::string(e.what());
    }
}

void Logger::SetupPerformanceLogger() {
    try {
        if (!file_enabled_) return;

        auto file_path = log_directory_ / "performance.log";
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file_path.string(), DEFAULT_MAX_FILE_SIZE_MB * 1024 * 1024, DEFAULT_MAX_FILES);
        file_sink->set_pattern(METRICS_PATTERN);

        performance_logger_ = std::make_shared<spdlog::logger>("performance", file_sink);
        performance_logger_->set_level(spdlog::level::info); // Always log performance
        spdlog::register_logger(performance_logger_);

    } catch (const std::exception& e) {
        last_error_ = "Failed to setup performance logger: " + std::string(e.what());
    }
}

spdlog::level::level_enum Logger::ConvertLogLevel(LogLevel level) const {
    switch (level) {
    case LogLevel::TRACE: return spdlog::level::trace;
    case LogLevel::DEBUG: return spdlog::level::debug;
    case LogLevel::INFO: return spdlog::level::info;
    case LogLevel::WARNING: return spdlog::level::warn;
    case LogLevel::ERROR_LEVEL: return spdlog::level::err;
    case LogLevel::CRITICAL: return spdlog::level::critical;
    case LogLevel::OFF: return spdlog::level::off;
    default: return spdlog::level::info;
    }
}

LogLevel Logger::ConvertSpdlogLevel(spdlog::level::level_enum level) const {
    switch (level) {
    case spdlog::level::trace: return LogLevel::TRACE;
    case spdlog::level::debug: return LogLevel::DEBUG;
    case spdlog::level::info: return LogLevel::INFO;
    case spdlog::level::warn: return LogLevel::WARNING;
    case spdlog::level::err: return LogLevel::ERROR_LEVEL;
    case spdlog::level::critical: return LogLevel::CRITICAL;
    case spdlog::level::off: return LogLevel::OFF;
    default: return LogLevel::INFO;
    }
}

} // namespace utils
} // namespace league_auto_accept