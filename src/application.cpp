#include "league_auto_accept/application.h"
#include <iostream>
#include <sstream>

namespace league_auto_accept {

Application::Application()
    : current_state_(ApplicationState::INITIALIZING)
    , running_(false)
    , auto_accept_enabled_(false)
    , should_stop_(false)
    , detection_start_time_(std::chrono::steady_clock::now())
    , last_activity_time_(std::chrono::steady_clock::now())
    , debug_mode_(false)
    , console_mode_(false) {
}

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(int argc, char* argv[]) {
    try {
        // Process command line arguments first
        if (!ProcessCommandLineArgs(argc, argv)) {
            return false;
        }

        // Initialize logging
        utils::Logger::Instance().Initialize(APPLICATION_NAME,
            debug_mode_ ? utils::LogLevel::DEBUG : utils::LogLevel::INFO);

        LOG_INFO("Starting {} v{}", APPLICATION_NAME, APPLICATION_VERSION);

        // Initialize performance metrics
        performance_metrics_ = std::make_shared<models::PerformanceMetrics>();

        // Initialize configuration manager
        config_manager_ = std::make_shared<ConfigManager>();
        if (config_file_override_.empty()) {
            if (!config_manager_->LoadFromFile()) {
                LOG_WARNING("Failed to load configuration, using defaults");
                config_manager_->CreateDefaultConfig();
            }
        } else {
            LOG_INFO("Using configuration file: {}", config_file_override_);
            // Would load from override path in full implementation
        }

        // Initialize core components
        if (!InitializeComponents()) {
            HandleError("Failed to initialize core components", true);
            return false;
        }

        // Setup event handlers
        SetupEventHandlers();

        // Setup hotkeys
        SetupHotkeys();

        SetState(ApplicationState::IDLE);
        LOG_INFO("Application initialized successfully");
        return true;

    } catch (const std::exception& e) {
        HandleError("Initialization failed: " + std::string(e.what()), true);
        return false;
    }
}

int Application::Run() {
    if (current_state_ == ApplicationState::ERROR_STATE) {
        return 1;
    }

    running_ = true;
    LOG_INFO("Application starting main loop");

    try {
        // Start main loop in separate thread
        main_loop_thread_ = std::thread([this]() { MainLoop(); });

        // Start detection loop in separate thread
        detection_thread_ = std::thread([this]() { DetectionLoop(); });

        // Process Windows messages in main thread
        while (running_ && !should_stop_) {
            if (!ProcessWindowsMessages()) {
                Sleep(std::chrono::milliseconds(10));
            }
        }

        // Wait for threads to complete
        if (main_loop_thread_.joinable()) {
            main_loop_thread_.join();
        }
        if (detection_thread_.joinable()) {
            detection_thread_.join();
        }

        LOG_INFO("Application main loop completed");
        return 0;

    } catch (const std::exception& e) {
        HandleError("Runtime error: " + std::string(e.what()), true);
        return 1;
    }
}

void Application::Shutdown() {
    if (!running_) return;

    SetState(ApplicationState::SHUTTING_DOWN);
    LOG_INFO("Shutting down application");

    should_stop_ = true;
    running_ = false;

    // Stop monitoring
    if (process_monitor_) {
        process_monitor_->StopMonitoring();
    }

    // Cleanup Windows resources
    UnregisterAllHotkeys();

    // Cleanup components
    CleanupComponents();

    // Flush logs
    utils::Logger::Instance().Shutdown();
}

bool Application::IsRunning() const {
    return running_;
}

std::shared_ptr<ConfigManager> Application::GetConfigManager() const {
    return config_manager_;
}

AppConfig Application::GetCurrentConfig() const {
    return config_manager_->GetConfig();
}

bool Application::EnableAutoAccept() {
    if (!auto_accept_enabled_) {
        auto_accept_enabled_ = true;
        config_manager_->SetAutoAcceptEnabled(true);

        SetState(ApplicationState::MONITORING);
        UpdateSystemTrayState();

        LOG_USER_ACTION("Enable Auto-Accept", "");
        return true;
    }
    return false;
}

bool Application::DisableAutoAccept() {
    if (auto_accept_enabled_) {
        auto_accept_enabled_ = false;
        config_manager_->SetAutoAcceptEnabled(false);

        SetState(ApplicationState::IDLE);
        UpdateSystemTrayState();

        LOG_USER_ACTION("Disable Auto-Accept", "");
        return true;
    }
    return false;
}

bool Application::IsAutoAcceptEnabled() const {
    return auto_accept_enabled_;
}

bool Application::ToggleAutoAccept() {
    if (auto_accept_enabled_) {
        return DisableAutoAccept();
    } else {
        return EnableAutoAccept();
    }
}

ApplicationState Application::GetState() const {
    return current_state_;
}

std::string Application::GetStateString() const {
    return ApplicationStateToString(current_state_);
}

bool Application::IsMonitoring() const {
    return current_state_ == ApplicationState::MONITORING ||
           current_state_ == ApplicationState::READY_CHECK_DETECTED ||
           current_state_ == ApplicationState::ACCEPTING;
}

std::shared_ptr<models::PerformanceMetrics> Application::GetMetrics() const {
    return performance_metrics_;
}

void Application::UpdatePerformanceMetrics() {
    if (performance_metrics_) {
        performance_metrics_->UpdateMemoryUsage();
        performance_metrics_->UpdateCPUUsage();
    }
}

std::string Application::GetLastError() const {
    return last_error_;
}

bool Application::HasErrors() const {
    return !last_error_.empty();
}

void Application::ClearErrors() {
    last_error_.clear();
}

bool Application::ProcessCommandLineArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            ShowHelp();
            return false;
        } else if (arg == "--version" || arg == "-v") {
            ShowVersion();
            return false;
        } else if (arg == "--debug" || arg == "-d") {
            debug_mode_ = true;
        } else if (arg == "--console" || arg == "-c") {
            console_mode_ = true;
        } else if (arg == "--config" && i + 1 < argc) {
            config_file_override_ = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            ShowHelp();
            return false;
        }
    }

    return true;
}

void Application::ShowHelp() const {
    std::cout << APPLICATION_NAME << " v" << APPLICATION_VERSION << std::endl;
    std::cout << "Automatically accepts League of Legends match invitations" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: League-Auto-Accept [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help       Show this help message" << std::endl;
    std::cout << "  -v, --version    Show version information" << std::endl;
    std::cout << "  -d, --debug      Enable debug mode" << std::endl;
    std::cout << "  -c, --console    Run in console mode" << std::endl;
    std::cout << "  --config FILE    Use specified configuration file" << std::endl;
}

void Application::ShowVersion() const {
    std::cout << APPLICATION_NAME << " v" << APPLICATION_VERSION << std::endl;
    std::cout << "Built with C++20, OpenCV, and cpp-httplib" << std::endl;
}

bool Application::InitializeComponents() {
    try {
        // Initialize LCU client
        lcu_client_ = std::make_shared<LCUClient>(performance_metrics_);
        if (!lcu_client_->Initialize()) {
            LOG_WARNING("LCU client initialization failed - UI fallback will be used");
        }

        // Initialize UI automation
        ui_automation_ = std::make_shared<UIAutomation>(performance_metrics_);
        if (!ui_automation_->Initialize()) {
            LOG_ERROR("UI automation initialization failed");
            return false;
        }

        // Initialize system tray
        system_tray_ = std::make_shared<SystemTray>(performance_metrics_);
        if (!system_tray_->Initialize(GetModuleHandle(nullptr), APPLICATION_NAME)) {
            LOG_ERROR("System tray initialization failed");
            return false;
        }

        if (!system_tray_->CreateTrayIcon()) {
            LOG_ERROR("Failed to create system tray icon");
            return false;
        }

        // Initialize process monitor
        process_monitor_ = std::make_shared<utils::ProcessMonitor>();

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Component initialization failed: {}", e.what());
        return false;
    }
}

void Application::SetupEventHandlers() {
    // Configuration change handler
    config_manager_->SetConfigChangeCallback(
        [this](const AppConfig& new_config, const AppConfig& old_config) {
            OnConfigChanged(new_config, old_config);
        });

    // LCU connection state handler
    lcu_client_->SetConnectionStateCallback(
        [this](bool connected, const std::string& error) {
            OnLCUConnectionStateChanged(connected, error);
        });

    // Process monitor handler
    process_monitor_->SetProcessEventCallback(
        [this](const utils::ProcessInfo& info, bool started) {
            OnProcessStateChanged(info, started);
        });

    // System tray event handler
    system_tray_->SetEventCallback(
        [this](const SystemTrayEventData& event_data) {
            OnSystemTrayEvent(event_data);
        });
}

void Application::SetupHotkeys() {
    AppConfig config = config_manager_->GetConfig();

    // Emergency disable hotkey (F9 by default)
    RegisterHotkey(HotkeyAction::EMERGENCY_DISABLE, VK_F9, 0);

    // Toggle auto-accept hotkey (Ctrl+Shift+F9)
    RegisterHotkey(HotkeyAction::TOGGLE_AUTO_ACCEPT, VK_F9, MOD_CONTROL | MOD_SHIFT);

    LOG_DEBUG("Hotkeys registered successfully");
}

void Application::MainLoop() {
    LOG_DEBUG("Main loop started");

    while (!should_stop_) {
        try {
            // Update performance metrics periodically
            UpdatePerformanceMetrics();

            // Update system tray status
            UpdatePerformanceDisplay();

            // Check for configuration changes
            if (config_manager_->IsFileWatchingEnabled()) {
                // File watching would trigger config reload automatically
            }

            Sleep(std::chrono::milliseconds(DEFAULT_MAIN_LOOP_INTERVAL_MS));

        } catch (const std::exception& e) {
            LOG_ERROR("Main loop error: {}", e.what());
            Sleep(std::chrono::milliseconds(1000)); // Back off on error
        }
    }

    LOG_DEBUG("Main loop completed");
}

void Application::DetectionLoop() {
    LOG_DEBUG("Detection loop started");

    // Start process monitoring
    process_monitor_->StartMonitoring("LeagueClient.exe");

    while (!should_stop_) {
        try {
            if (auto_accept_enabled_ && IsMonitoring()) {
                if (CheckForReadyCheck()) {
                    if (PerformAcceptance()) {
                        // Success - continue monitoring
                        SetState(ApplicationState::MONITORING);
                    } else {
                        // Failed to accept - retry next cycle
                        LOG_WARNING("Failed to accept ready check, will retry");
                    }
                }
            }

            Sleep(std::chrono::milliseconds(DEFAULT_DETECTION_INTERVAL_MS));

        } catch (const std::exception& e) {
            LOG_ERROR("Detection loop error: {}", e.what());
            Sleep(std::chrono::milliseconds(1000)); // Back off on error
        }
    }

    LOG_DEBUG("Detection loop completed");
}

void Application::SetState(ApplicationState new_state) {
    ApplicationState old_state = current_state_.load();
    if (old_state != new_state) {
        current_state_ = new_state;
        HandleStateChange(old_state, new_state);
    }
}

void Application::HandleStateChange(ApplicationState old_state, ApplicationState new_state) {
    LOG_DEBUG("State change: {} -> {}", ApplicationStateToString(old_state), ApplicationStateToString(new_state));

    // Update system tray icon
    UpdateSystemTrayState();

    // Notify callback
    NotifyStateChange(old_state, new_state);
}

bool Application::CheckForReadyCheck() {
    detection_start_time_ = std::chrono::steady_clock::now();

    // Try LCU API first
    if (lcu_client_->IsConnected()) {
        if (lcu_client_->IsReadyCheckActive()) {
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - detection_start_time_);

            SetState(ApplicationState::READY_CHECK_DETECTED);
            HandleReadyCheckDetected("LCU_API", latency);
            return true;
        }
    }

    // Fallback to UI detection
    auto ui_result = ui_automation_->FindAcceptButton();
    if (ui_result.found) {
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - detection_start_time_);

        SetState(ApplicationState::READY_CHECK_DETECTED);
        HandleReadyCheckDetected("UI_AUTOMATION", latency);
        return true;
    }

    return false;
}

bool Application::PerformAcceptance() {
    SetState(ApplicationState::ACCEPTING);

    // Try LCU acceptance first
    if (TryLCUAcceptance()) {
        auto total_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - detection_start_time_);
        HandleMatchAccepted(true, total_latency);
        return true;
    }

    // Fallback to UI acceptance
    if (TryUIAcceptance()) {
        auto total_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - detection_start_time_);
        HandleMatchAccepted(true, total_latency);
        return true;
    }

    // Both methods failed
    auto total_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - detection_start_time_);
    HandleMatchAccepted(false, total_latency);
    return false;
}

bool Application::TryLCUAcceptance() {
    if (lcu_client_->IsConnected()) {
        return lcu_client_->AcceptCurrentReadyCheck();
    }
    return false;
}

bool Application::TryUIAcceptance() {
    auto result = ui_automation_->ClickAcceptButton();
    return result.success;
}

void Application::UpdateSystemTrayState() {
    if (!system_tray_) return;

    models::SystemTrayIconState icon_state;

    switch (current_state_.load()) {
    case ApplicationState::IDLE:
        icon_state = models::SystemTrayIconState::DISABLED;
        break;
    case ApplicationState::MONITORING:
        icon_state = models::SystemTrayIconState::ACTIVE;
        break;
    case ApplicationState::ERROR_STATE:
        icon_state = models::SystemTrayIconState::ERROR_STATE;
        break;
    default:
        icon_state = models::SystemTrayIconState::MONITORING;
        break;
    }

    system_tray_->UpdateIcon(icon_state);
}

void Application::OnSystemTrayEvent(const SystemTrayEventData& event_data) {
    switch (event_data.event_type) {
    case SystemTrayEvent::MENU_ITEM_SELECTED:
        if (event_data.menu_item_id == "toggle_auto_accept") {
            ToggleAutoAccept();
        } else if (event_data.menu_item_id == "exit") {
            should_stop_ = true;
        }
        break;
    case SystemTrayEvent::DOUBLE_CLICK:
        ToggleAutoAccept();
        break;
    default:
        break;
    }
}

void Application::HandleReadyCheckDetected(const std::string& method, std::chrono::milliseconds latency) {
    LOG_DETECTION_METRICS(static_cast<int>(latency.count()), true, method);

    if (match_detected_callback_) {
        match_detected_callback_(method, latency);
    }
}

void Application::HandleMatchAccepted(bool success, std::chrono::milliseconds total_latency) {
    LOG_ACCEPTANCE_METRICS(static_cast<int>(total_latency.count()), success, success ? "" : "Acceptance failed");

    if (performance_metrics_) {
        if (success) {
            performance_metrics_->RecordMatchAccepted();
        }
    }

    if (match_accepted_callback_) {
        match_accepted_callback_(success, total_latency);
    }

    // Show notification
    if (system_tray_) {
        std::string title = success ? "Match Accepted" : "Acceptance Failed";
        std::string message = success ?
            "Ready check accepted in " + std::to_string(total_latency.count()) + "ms" :
            "Failed to accept ready check";
        system_tray_->ShowNotification(title, message);
    }
}

void Application::Sleep(std::chrono::milliseconds duration) {
    std::this_thread::sleep_for(duration);
}

void Application::HandleError(const std::string& error, bool critical) {
    RecordError(error);

    if (critical) {
        SetState(ApplicationState::ERROR_STATE);
        LOG_CRITICAL("{}", error);
    } else {
        LOG_ERROR("{}", error);
    }
}

bool Application::RegisterHotkey(HotkeyAction action, UINT vk_code, UINT modifiers) {
    UINT hotkey_id = static_cast<UINT>(action) + 9000;

    if (RegisterHotKey(nullptr, hotkey_id, modifiers, vk_code)) {
        registered_hotkeys_.emplace_back(action, hotkey_id);
        return true;
    }

    return false;
}

std::string ApplicationStateToString(ApplicationState state) {
    switch (state) {
    case ApplicationState::INITIALIZING: return "Initializing";
    case ApplicationState::IDLE: return "Idle";
    case ApplicationState::MONITORING: return "Monitoring";
    case ApplicationState::READY_CHECK_DETECTED: return "Ready Check Detected";
    case ApplicationState::ACCEPTING: return "Accepting";
    case ApplicationState::ERROR_STATE: return "Error";
    case ApplicationState::SHUTTING_DOWN: return "Shutting Down";
    default: return "Unknown";
    }
}

} // namespace league_auto_accept