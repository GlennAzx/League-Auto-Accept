#include "league_auto_accept/models/gameflow_state.h"
#include <stdexcept>

namespace league_auto_accept {
namespace models {

GameflowState::GameflowState()
    : phase_(GameflowPhase::NONE)
    , ready_check_active_(false)
    , ready_check_duration_(0)
    , ready_check_remaining_(0)
    , last_detection_time_(std::chrono::steady_clock::now())
    , detection_source_(DetectionSource::LCU_API) {
}

void GameflowState::SetPhase(GameflowPhase phase) {
    if (!CanTransitionTo(phase)) {
        throw std::invalid_argument("Invalid phase transition from " + 
            GetPhaseString() + " to " + GameflowPhaseToString(phase));
    }
    phase_ = phase;
    UpdateDetectionTime();
    
    // Reset ready check state when leaving READY_CHECK phase
    if (phase != GameflowPhase::READY_CHECK) {
        ready_check_active_ = false;
        ready_check_duration_ = 0;
        ready_check_remaining_ = 0;
    }
}

void GameflowState::SetReadyCheckActive(bool active) {
    ready_check_active_ = active;
    UpdateDetectionTime();
    
    // If activating ready check, ensure we're in correct phase
    if (active && phase_ != GameflowPhase::READY_CHECK) {
        TransitionTo(GameflowPhase::READY_CHECK);
    }
}

void GameflowState::SetReadyCheckDuration(int duration) {
    if (duration < 0) {
        throw std::invalid_argument("Ready check duration cannot be negative");
    }
    ready_check_duration_ = duration;
}

void GameflowState::SetReadyCheckRemaining(int remaining) {
    if (remaining < 0) {
        throw std::invalid_argument("Ready check remaining time cannot be negative");
    }
    if (remaining > ready_check_duration_) {
        throw std::invalid_argument("Ready check remaining time cannot exceed duration");
    }
    ready_check_remaining_ = remaining;
}

void GameflowState::SetDetectionSource(DetectionSource source) {
    detection_source_ = source;
    UpdateDetectionTime();
}

void GameflowState::UpdateDetectionTime() {
    last_detection_time_ = std::chrono::steady_clock::now();
}

bool GameflowState::IsStateRecent(std::chrono::seconds max_age) const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_detection_time_);
    return age <= max_age;
}

void GameflowState::Reset() {
    phase_ = GameflowPhase::NONE;
    ready_check_active_ = false;
    ready_check_duration_ = 0;
    ready_check_remaining_ = 0;
    detection_source_ = DetectionSource::LCU_API;
    UpdateDetectionTime();
}

bool GameflowState::CanTransitionTo(GameflowPhase new_phase) const {
    return IsValidTransition(phase_, new_phase);
}

void GameflowState::TransitionTo(GameflowPhase new_phase) {
    if (CanTransitionTo(new_phase)) {
        SetPhase(new_phase);
    } else {
        throw std::runtime_error("Invalid state transition from " + 
            GetPhaseString() + " to " + GameflowPhaseToString(new_phase));
    }
}

std::string GameflowState::GetPhaseString() const {
    return GameflowPhaseToString(phase_);
}

std::string GameflowState::GetDetectionSourceString() const {
    return DetectionSourceToString(detection_source_);
}

bool GameflowState::IsValid() const {
    try {
        ValidateReadyCheckTiming();
        return IsStateRecent();
    } catch (const std::exception&) {
        return false;
    }
}

bool GameflowState::IsValidTransition(GameflowPhase from, GameflowPhase to) const {
    // Allow any transition to/from NONE (represents unknown state)
    if (from == GameflowPhase::NONE || to == GameflowPhase::NONE) {
        return true;
    }
    
    // Define valid transitions based on League client workflow
    switch (from) {
    case GameflowPhase::LOBBY:
        return to == GameflowPhase::MATCHMAKING || to == GameflowPhase::IN_GAME;
        
    case GameflowPhase::MATCHMAKING:
        return to == GameflowPhase::READY_CHECK || to == GameflowPhase::LOBBY;
        
    case GameflowPhase::READY_CHECK:
        return to == GameflowPhase::CHAMPION_SELECT || to == GameflowPhase::LOBBY;
        
    case GameflowPhase::CHAMPION_SELECT:
        return to == GameflowPhase::IN_GAME || to == GameflowPhase::LOBBY;
        
    case GameflowPhase::IN_GAME:
        return to == GameflowPhase::LOBBY;
        
    default:
        return false;
    }
}

void GameflowState::ValidateReadyCheckTiming() const {
    if (ready_check_remaining_ > ready_check_duration_) {
        throw std::runtime_error("Ready check remaining time exceeds duration");
    }
    
    if (ready_check_active_ && ready_check_remaining_ == 0 && ready_check_duration_ > 0) {
        throw std::runtime_error("Ready check is active but no time remaining");
    }
}

// Utility functions
std::string GameflowPhaseToString(GameflowPhase phase) {
    switch (phase) {
    case GameflowPhase::NONE: return "None";
    case GameflowPhase::LOBBY: return "Lobby";
    case GameflowPhase::MATCHMAKING: return "Matchmaking";
    case GameflowPhase::READY_CHECK: return "ReadyCheck";
    case GameflowPhase::CHAMPION_SELECT: return "ChampSelect";
    case GameflowPhase::IN_GAME: return "InGame";
    default: return "Unknown";
    }
}

GameflowPhase StringToGameflowPhase(const std::string& phase_str) {
    if (phase_str == "None") return GameflowPhase::NONE;
    if (phase_str == "Lobby") return GameflowPhase::LOBBY;
    if (phase_str == "Matchmaking") return GameflowPhase::MATCHMAKING;
    if (phase_str == "ReadyCheck") return GameflowPhase::READY_CHECK;
    if (phase_str == "ChampSelect") return GameflowPhase::CHAMPION_SELECT;
    if (phase_str == "InGame") return GameflowPhase::IN_GAME;
    
    throw std::invalid_argument("Unknown gameflow phase: " + phase_str);
}

std::string DetectionSourceToString(DetectionSource source) {
    switch (source) {
    case DetectionSource::LCU_API: return "LCU_API";
    case DetectionSource::UI_AUTOMATION: return "UI_AUTOMATION";
    default: return "Unknown";
    }
}

DetectionSource StringToDetectionSource(const std::string& source_str) {
    if (source_str == "LCU_API") return DetectionSource::LCU_API;
    if (source_str == "UI_AUTOMATION") return DetectionSource::UI_AUTOMATION;
    
    throw std::invalid_argument("Unknown detection source: " + source_str);
}

} // namespace models
} // namespace league_auto_accept