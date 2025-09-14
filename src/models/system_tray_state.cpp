#include "league_auto_accept/models/system_tray_state.h"
#include <stdexcept>
#include <algorithm>

namespace league_auto_accept {
namespace models {

SystemTrayState::SystemTrayState()
    : icon_state_(SystemTrayIconState::DISABLED)
    , tooltip_text_(GenerateDefaultTooltip())
    , last_user_interaction_(std::chrono::steady_clock::now()) {
    InitializeDefaultMenu();
}

SystemTrayIconState SystemTrayState::GetIconState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return icon_state_;
}

void SystemTrayState::SetIconState(SystemTrayIconState state) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        icon_state_ = state;

        // Update tooltip based on new state
        tooltip_text_ = GenerateDefaultTooltip();
    }
}

std::string SystemTrayState::GetIconStateString() const {
    return SystemTrayIconStateToString(GetIconState());
}

std::string SystemTrayState::GetTooltipText() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return tooltip_text_;
}

void SystemTrayState::SetTooltipText(const std::string& text) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    tooltip_text_ = text;
}

void SystemTrayState::UpdateTooltipWithStatus(bool auto_accept_enabled, bool league_connected,
                                             int matches_accepted, double success_rate) {
    std::string status_text = "League Auto-Accept\n";

    if (auto_accept_enabled) {
        if (league_connected) {
            status_text += "Status: Active (monitoring matches)\n";
        } else {
            status_text += "Status: Monitoring (waiting for League client)\n";
        }
    } else {
        status_text += "Status: Disabled\n";
    }

    if (matches_accepted > 0) {
        status_text += "Matches accepted: " + std::to_string(matches_accepted) + "\n";
        status_text += "Success rate: " + std::to_string(static_cast<int>(success_rate * 100)) + "%";
    }

    SetTooltipText(status_text);
}

std::vector<SystemTrayMenuItem> SystemTrayState::GetMenuItems() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return menu_items_;
}

void SystemTrayState::SetMenuItems(const std::vector<SystemTrayMenuItem>& items) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        menu_items_ = items;
    }

    ValidateMenuItems();
}

void SystemTrayState::UpdateMenuItem(const std::string& item_id, const std::string& new_text,
                                   bool enabled, bool checked) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto it = std::find_if(menu_items_.begin(), menu_items_.end(),
        [&item_id](const SystemTrayMenuItem& item) {
            return item.id == item_id;
        });

    if (it != menu_items_.end()) {
        it->text = new_text;
        it->enabled = enabled;
        it->checked = checked;
    } else {
        throw std::invalid_argument("Menu item not found: " + item_id);
    }
}

bool SystemTrayState::HasMenuItem(const std::string& item_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    return std::any_of(menu_items_.begin(), menu_items_.end(),
        [&item_id](const SystemTrayMenuItem& item) {
            return item.id == item_id;
        });
}

SystemTrayMenuItem SystemTrayState::GetMenuItem(const std::string& item_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    auto it = std::find_if(menu_items_.begin(), menu_items_.end(),
        [&item_id](const SystemTrayMenuItem& item) {
            return item.id == item_id;
        });

    if (it != menu_items_.end()) {
        return *it;
    }

    throw std::invalid_argument("Menu item not found: " + item_id);
}

void SystemTrayState::CreateStandardMenu(bool auto_accept_enabled) {
    std::vector<SystemTrayMenuItem> items;

    // Toggle auto-accept menu item
    std::string toggle_text = auto_accept_enabled ? "Disable Auto-Accept" : "Enable Auto-Accept";
    items.emplace_back(MENU_TOGGLE_AUTO_ACCEPT, toggle_text);

    // Separator
    items.emplace_back(MENU_SEPARATOR, "", true, false, true);

    // Settings
    items.emplace_back(MENU_SETTINGS, "Settings");

    // Statistics
    items.emplace_back(MENU_STATISTICS, "Show Statistics");

    // Separator
    items.emplace_back(MENU_SEPARATOR, "", true, false, true);

    // Exit
    items.emplace_back(MENU_EXIT, "Exit");

    SetMenuItems(items);
}

void SystemTrayState::UpdateAutoAcceptMenuItem(bool enabled) {
    std::string new_text = enabled ? "Disable Auto-Accept" : "Enable Auto-Accept";
    UpdateMenuItem(MENU_TOGGLE_AUTO_ACCEPT, new_text);
}

void SystemTrayState::QueueNotification(const std::string& title, const std::string& message, int duration_ms) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    notification_queue_.emplace(title, message, duration_ms);
}

bool SystemTrayState::HasPendingNotifications() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return !notification_queue_.empty();
}

SystemNotification SystemTrayState::GetNextNotification() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (notification_queue_.empty()) {
        throw std::runtime_error("No pending notifications");
    }

    SystemNotification notification = notification_queue_.front();
    notification_queue_.pop();
    return notification;
}

void SystemTrayState::MarkNotificationShown(const SystemNotification& notification) {
    // For future use - could track shown notifications for history
    // Currently just used to mark that notification was processed
}

void SystemTrayState::ClearNotificationQueue() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::queue<SystemNotification> empty_queue;
    notification_queue_.swap(empty_queue);
}

std::chrono::steady_clock::time_point SystemTrayState::GetLastUserInteraction() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_user_interaction_;
}

void SystemTrayState::UpdateLastUserInteraction() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_user_interaction_ = std::chrono::steady_clock::now();
}

bool SystemTrayState::HasRecentUserInteraction(std::chrono::seconds max_age) const {
    auto now = std::chrono::steady_clock::now();
    auto last_interaction = GetLastUserInteraction();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_interaction);
    return age <= max_age;
}

bool SystemTrayState::IsValid() const {
    try {
        ValidateMenuItems();

        // Check that we have basic required menu items
        return HasMenuItem(MENU_TOGGLE_AUTO_ACCEPT) && HasMenuItem(MENU_EXIT);
    } catch (const std::exception&) {
        return false;
    }
}

void SystemTrayState::Reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    icon_state_ = SystemTrayIconState::DISABLED;
    tooltip_text_ = GenerateDefaultTooltip();
    last_user_interaction_ = std::chrono::steady_clock::now();

    // Clear notifications
    std::queue<SystemNotification> empty_queue;
    notification_queue_.swap(empty_queue);

    // Reset to default menu
    menu_items_.clear();
    InitializeDefaultMenu();
}

std::string SystemTrayState::GetStatusSummary() const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    std::string summary = "Icon: " + GetIconStateString() + ", ";
    summary += "Menu items: " + std::to_string(menu_items_.size()) + ", ";
    summary += "Pending notifications: " + std::to_string(notification_queue_.size());

    return summary;
}

int SystemTrayState::GetNotificationQueueSize() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return static_cast<int>(notification_queue_.size());
}

void SystemTrayState::InitializeDefaultMenu() {
    // This is called from constructor, so no lock needed
    CreateStandardMenu(false); // Start with auto-accept disabled
}

std::string SystemTrayState::GenerateDefaultTooltip() const {
    // This is called from constructor and SetIconState, so state_mutex_ already locked
    switch (icon_state_) {
    case SystemTrayIconState::DISABLED:
        return "League Auto-Accept\nStatus: Disabled\nRight-click to enable";
    case SystemTrayIconState::MONITORING:
        return "League Auto-Accept\nStatus: Monitoring\nWaiting for League client";
    case SystemTrayIconState::ACTIVE:
        return "League Auto-Accept\nStatus: Active\nMonitoring for matches";
    case SystemTrayIconState::ERROR_STATE:
        return "League Auto-Accept\nStatus: Error\nCheck connection or settings";
    default:
        return "League Auto-Accept";
    }
}

void SystemTrayState::ValidateMenuItems() const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // Check for duplicate IDs
    std::vector<std::string> ids;
    for (const auto& item : menu_items_) {
        if (!item.separator && !item.id.empty()) {
            if (std::find(ids.begin(), ids.end(), item.id) != ids.end()) {
                throw std::invalid_argument("Duplicate menu item ID: " + item.id);
            }
            ids.push_back(item.id);
        }
    }

    // Check for required items
    if (!HasMenuItem(MENU_TOGGLE_AUTO_ACCEPT)) {
        throw std::runtime_error("Missing required menu item: toggle auto-accept");
    }
    if (!HasMenuItem(MENU_EXIT)) {
        throw std::runtime_error("Missing required menu item: exit");
    }
}

// Utility functions
std::string SystemTrayIconStateToString(SystemTrayIconState state) {
    switch (state) {
    case SystemTrayIconState::DISABLED: return "DISABLED";
    case SystemTrayIconState::MONITORING: return "MONITORING";
    case SystemTrayIconState::ACTIVE: return "ACTIVE";
    case SystemTrayIconState::ERROR_STATE: return "ERROR";
    default: return "UNKNOWN";
    }
}

SystemTrayIconState StringToSystemTrayIconState(const std::string& state_str) {
    if (state_str == "DISABLED") return SystemTrayIconState::DISABLED;
    if (state_str == "MONITORING") return SystemTrayIconState::MONITORING;
    if (state_str == "ACTIVE") return SystemTrayIconState::ACTIVE;
    if (state_str == "ERROR") return SystemTrayIconState::ERROR_STATE;

    throw std::invalid_argument("Unknown system tray icon state: " + state_str);
}

} // namespace models
} // namespace league_auto_accept