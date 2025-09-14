#include "league_auto_accept/models/performance_metrics.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <stdexcept>

namespace league_auto_accept {
namespace models {

PerformanceMetrics::PerformanceMetrics()
    : detection_latency_ms_(0)
    , acceptance_latency_ms_(0)
    , detection_count_(0)
    , total_detection_latency_(0)
    , acceptance_count_(0)
    , total_acceptance_latency_(0)
    , total_matches_detected_(0)
    , total_matches_accepted_(0)
    , memory_usage_mb_(0.0)
    , cpu_usage_percent_(0.0)
    , last_error_time_(std::chrono::system_clock::now())
    , consecutive_errors_(0)
    , total_errors_(0)
    , start_time_(std::chrono::steady_clock::now()) {
    
    // Initialize with current system metrics
    UpdateMemoryUsage();
    UpdateCPUUsage();
}

int PerformanceMetrics::GetDetectionLatencyMs() const {
    return detection_latency_ms_.load();
}

int PerformanceMetrics::GetAcceptanceLatencyMs() const {
    return acceptance_latency_ms_.load();
}

int PerformanceMetrics::GetTotalMatchesDetected() const {
    return total_matches_detected_.load();
}

int PerformanceMetrics::GetTotalMatchesAccepted() const {
    return total_matches_accepted_.load();
}

double PerformanceMetrics::GetSuccessRate() const {
    return CalculateSuccessRate();
}

double PerformanceMetrics::GetMemoryUsageMB() const {
    return memory_usage_mb_.load();
}

double PerformanceMetrics::GetCPUUsagePercent() const {
    return cpu_usage_percent_.load();
}

double PerformanceMetrics::GetUptimeHours() const {
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::hours>(now - start_time_);
    return uptime.count() + 
           (std::chrono::duration_cast<std::chrono::minutes>(now - start_time_).count() % 60) / 60.0;
}

std::string PerformanceMetrics::GetLastErrorMessage() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_message_;
}

std::chrono::system_clock::time_point PerformanceMetrics::GetLastErrorTime() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_time_;
}

int PerformanceMetrics::GetConsecutiveErrors() const {
    return consecutive_errors_.load();
}

int PerformanceMetrics::GetTotalErrors() const {
    return total_errors_.load();
}

void PerformanceMetrics::RecordDetectionLatency(std::chrono::milliseconds latency) {
    int latency_ms = static_cast<int>(latency.count());
    detection_latency_ms_.store(latency_ms);
    detection_count_.fetch_add(1);
    total_detection_latency_.fetch_add(latency_ms);
}

void PerformanceMetrics::RecordAcceptanceLatency(std::chrono::milliseconds latency) {
    int latency_ms = static_cast<int>(latency.count());
    acceptance_latency_ms_.store(latency_ms);
    acceptance_count_.fetch_add(1);
    total_acceptance_latency_.fetch_add(latency_ms);
}

void PerformanceMetrics::RecordMatchDetected() {
    total_matches_detected_.fetch_add(1);
}

void PerformanceMetrics::RecordMatchAccepted() {
    total_matches_accepted_.fetch_add(1);
    ClearConsecutiveErrors(); // Clear error count on successful acceptance
}

void PerformanceMetrics::RecordError(const std::string& error_message) {
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_message_ = error_message;
        last_error_time_ = std::chrono::system_clock::now();
    }
    consecutive_errors_.fetch_add(1);
    total_errors_.fetch_add(1);
}

void PerformanceMetrics::ClearConsecutiveErrors() {
    consecutive_errors_.store(0);
}

void PerformanceMetrics::UpdateMemoryUsage() {
    memory_usage_mb_.store(GetCurrentMemoryUsageMB());
}

void PerformanceMetrics::UpdateCPUUsage() {
    cpu_usage_percent_.store(GetCurrentCPUUsagePercent());
}

bool PerformanceMetrics::MeetsPerformanceTargets() const {
    return MeetsDetectionTarget() && 
           MeetsAcceptanceTarget() && 
           MeetsMemoryTarget() && 
           MeetsCPUTarget() && 
           MeetsSuccessRateTarget();
}

bool PerformanceMetrics::MeetsDetectionTarget() const {
    int latency = detection_latency_ms_.load();
    return latency > 0 && latency < DETECTION_TARGET_MS;
}

bool PerformanceMetrics::MeetsAcceptanceTarget() const {
    int latency = acceptance_latency_ms_.load();
    return latency > 0 && latency < ACCEPTANCE_TARGET_MS;
}

bool PerformanceMetrics::MeetsMemoryTarget() const {
    double memory = memory_usage_mb_.load();
    return memory > 0.0 && memory < MEMORY_TARGET_MB;
}

bool PerformanceMetrics::MeetsCPUTarget() const {
    double cpu = cpu_usage_percent_.load();
    return cpu >= 0.0 && cpu < CPU_TARGET_PERCENT;
}

bool PerformanceMetrics::MeetsSuccessRateTarget() const {
    double rate = CalculateSuccessRate();
    return rate >= SUCCESS_RATE_TARGET;
}

double PerformanceMetrics::GetAverageDetectionLatency() const {
    int count = detection_count_.load();
    if (count == 0) return 0.0;
    
    int total = total_detection_latency_.load();
    return static_cast<double>(total) / static_cast<double>(count);
}

double PerformanceMetrics::GetAverageAcceptanceLatency() const {
    int count = acceptance_count_.load();
    if (count == 0) return 0.0;
    
    int total = total_acceptance_latency_.load();
    return static_cast<double>(total) / static_cast<double>(count);
}

int PerformanceMetrics::GetDetectionCount() const {
    return detection_count_.load();
}

int PerformanceMetrics::GetAcceptanceCount() const {
    return acceptance_count_.load();
}

void PerformanceMetrics::Reset() {
    ResetCounters();
    ResetLatencyStats();
    
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_message_.clear();
        last_error_time_ = std::chrono::system_clock::now();
    }
    
    consecutive_errors_.store(0);
    total_errors_.store(0);
    start_time_ = std::chrono::steady_clock::now();
    
    UpdateMemoryUsage();
    UpdateCPUUsage();
}

void PerformanceMetrics::ResetCounters() {
    total_matches_detected_.store(0);
    total_matches_accepted_.store(0);
}

void PerformanceMetrics::ResetLatencyStats() {
    detection_latency_ms_.store(0);
    acceptance_latency_ms_.store(0);
    detection_count_.store(0);
    total_detection_latency_.store(0);
    acceptance_count_.store(0);
    total_acceptance_latency_.store(0);
}

double PerformanceMetrics::CalculateSuccessRate() const {
    int detected = total_matches_detected_.load();
    int accepted = total_matches_accepted_.load();
    
    if (detected == 0) return 0.0;
    
    return static_cast<double>(accepted) / static_cast<double>(detected);
}

double PerformanceMetrics::GetCurrentMemoryUsageMB() const {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), 
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), 
                           sizeof(pmc))) {
        // Convert bytes to MB
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return 0.0;
}

double PerformanceMetrics::GetCurrentCPUUsagePercent() const {
    // Windows CPU usage calculation is complex and requires multiple samples
    // For now, return a simplified implementation
    // TODO: Implement proper CPU usage monitoring with performance counters
    static FILETIME last_idle_time, last_kernel_time, last_user_time;
    static bool first_call = true;
    
    FILETIME idle_time, kernel_time, user_time;
    
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        return 0.0;
    }
    
    if (first_call) {
        last_idle_time = idle_time;
        last_kernel_time = kernel_time;
        last_user_time = user_time;
        first_call = false;
        return 0.0; // No previous measurement
    }
    
    // Calculate time differences
    auto FileTimeToULL = [](const FILETIME& ft) -> unsigned long long {
        return static_cast<unsigned long long>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
    };
    
    unsigned long long idle_diff = FileTimeToULL(idle_time) - FileTimeToULL(last_idle_time);
    unsigned long long kernel_diff = FileTimeToULL(kernel_time) - FileTimeToULL(last_kernel_time);
    unsigned long long user_diff = FileTimeToULL(user_time) - FileTimeToULL(last_user_time);
    
    unsigned long long total_diff = kernel_diff + user_diff;
    
    if (total_diff == 0) return 0.0;
    
    double cpu_usage = 100.0 * (1.0 - static_cast<double>(idle_diff) / static_cast<double>(total_diff));
    
    // Update last measurements
    last_idle_time = idle_time;
    last_kernel_time = kernel_time;
    last_user_time = user_time;
    
    return std::max(0.0, std::min(100.0, cpu_usage));
}

} // namespace models
} // namespace league_auto_accept