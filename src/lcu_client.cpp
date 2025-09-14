#include "league_auto_accept/lcu_client.h"
#include "league_auto_accept/models/performance_metrics.h"
#include <thread>
#include <regex>

namespace league_auto_accept {

LCUClient::LCUClient()
    : LCUClient(nullptr) {
}

LCUClient::LCUClient(std::shared_ptr<models::PerformanceMetrics> metrics)
    : performance_metrics_(metrics)
    , connection_timeout_(std::chrono::milliseconds(DEFAULT_TIMEOUT_MS))
    , max_retries_(DEFAULT_MAX_RETRIES)
    , retry_delay_(std::chrono::milliseconds(DEFAULT_RETRY_DELAY_MS))
    , auto_reconnect_enabled_(true)
    , successful_requests_(0)
    , failed_requests_(0)
    , total_request_time_(0) {
}

LCUClient::~LCUClient() {
    Disconnect();
}

bool LCUClient::Initialize() {
    return connection_info_.DiscoverFromLockfile();
}

bool LCUClient::Connect() {
    if (!Initialize()) {
        UpdateConnectionState(false, "Failed to discover LCU connection info");
        return false;
    }

    if (!connection_info_.IsValid()) {
        UpdateConnectionState(false, "Invalid LCU connection information");
        return false;
    }

    try {
        SetupHTTPClient();

        // Test the connection
        if (TestConnection()) {
            connection_info_.SetConnectionState(models::LCUConnectionState::CONNECTED);
            UpdateConnectionState(true);
            return true;
        } else {
            UpdateConnectionState(false, "Connection test failed");
            return false;
        }
    } catch (const std::exception& e) {
        UpdateConnectionState(false, "Connection error: " + std::string(e.what()));
        return false;
    }
}

bool LCUClient::Reconnect() {
    Disconnect();
    return Connect();
}

void LCUClient::Disconnect() {
    http_client_.reset();
    connection_info_.SetConnectionState(models::LCUConnectionState::DISCONNECTED);
    UpdateConnectionState(false);
}

bool LCUClient::IsConnected() const {
    return connection_info_.IsConnected() && http_client_ != nullptr;
}

bool LCUClient::TestConnection() {
    if (!http_client_) return false;

    auto response = GetGameflowPhase();
    return response.IsSuccess() || response.result == LCURequestResult::NOT_FOUND;
}

models::LCUConnectionInfo LCUClient::GetConnectionInfo() const {
    return connection_info_;
}

void LCUClient::SetConnectionTimeout(std::chrono::milliseconds timeout) {
    connection_timeout_ = timeout;
    if (http_client_) {
        http_client_->set_read_timeout(timeout.count() / 1000, (timeout.count() % 1000) * 1000);
    }
}

std::chrono::milliseconds LCUClient::GetConnectionTimeout() const {
    return connection_timeout_;
}

LCUResponse LCUClient::GetGameflowPhase() {
    return MakeRequestWithRetry("GET", GAMEFLOW_ENDPOINT);
}

LCUResponse LCUClient::GetReadyCheckStatus() {
    return MakeRequestWithRetry("GET", READY_CHECK_ENDPOINT);
}

LCUResponse LCUClient::AcceptReadyCheck() {
    return MakeRequestWithRetry("POST", READY_CHECK_ACCEPT_ENDPOINT);
}

LCUResponse LCUClient::DeclineReadyCheck() {
    return MakeRequestWithRetry("POST", READY_CHECK_DECLINE_ENDPOINT);
}

std::future<LCUResponse> LCUClient::GetGameflowPhaseAsync() {
    return std::async(std::launch::async, [this]() {
        return GetGameflowPhase();
    });
}

std::future<LCUResponse> LCUClient::GetReadyCheckStatusAsync() {
    return std::async(std::launch::async, [this]() {
        return GetReadyCheckStatus();
    });
}

std::future<LCUResponse> LCUClient::AcceptReadyCheckAsync() {
    return std::async(std::launch::async, [this]() {
        return AcceptReadyCheck();
    });
}

std::future<LCUResponse> LCUClient::DeclineReadyCheckAsync() {
    return std::async(std::launch::async, [this]() {
        return DeclineReadyCheck();
    });
}

models::GameflowPhase LCUClient::GetCurrentGameflowPhase() {
    auto response = GetGameflowPhase();
    if (response.IsSuccess()) {
        try {
            std::string phase_str = ParseGameflowPhaseFromResponse(response.body);
            return models::StringToGameflowPhase(phase_str);
        } catch (const std::exception&) {
            return models::GameflowPhase::NONE;
        }
    }
    return models::GameflowPhase::NONE;
}

ReadyCheckStatus LCUClient::GetCurrentReadyCheckStatus() {
    auto response = GetReadyCheckStatus();
    if (response.IsSuccess()) {
        return ParseReadyCheckFromResponse(response.body);
    }
    return ReadyCheckStatus{};
}

bool LCUClient::IsReadyCheckActive() {
    auto status = GetCurrentReadyCheckStatus();
    return status.IsActive() && status.HasTimeRemaining();
}

bool LCUClient::AcceptCurrentReadyCheck() {
    auto response = AcceptReadyCheck();
    return response.IsSuccess();
}

void LCUClient::SetPerformanceMetrics(std::shared_ptr<models::PerformanceMetrics> metrics) {
    performance_metrics_ = metrics;
}

double LCUClient::GetAverageRequestLatency() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (successful_requests_ == 0) return 0.0;
    return static_cast<double>(total_request_time_.count()) / static_cast<double>(successful_requests_);
}

int LCUClient::GetSuccessfulRequestCount() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return successful_requests_;
}

int LCUClient::GetFailedRequestCount() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return failed_requests_;
}

void LCUClient::SetRetryPolicy(int max_retries, std::chrono::milliseconds retry_delay) {
    max_retries_ = max_retries;
    retry_delay_ = retry_delay;
}

void LCUClient::EnableAutoReconnect(bool enabled) {
    auto_reconnect_enabled_ = enabled;
}

bool LCUClient::IsAutoReconnectEnabled() const {
    return auto_reconnect_enabled_;
}

bool LCUClient::IsLCUAvailable() {
    models::LCUConnectionInfo temp_connection;
    return temp_connection.DiscoverFromLockfile() && temp_connection.IsValid();
}

std::vector<int> LCUClient::FindLCUPorts() {
    std::vector<int> ports;

    // Try to find from lockfile first
    models::LCUConnectionInfo connection;
    if (connection.DiscoverFromLockfile()) {
        ports.push_back(connection.GetPort());
    }

    // Add common LCU port range as fallback
    for (int port = 2999; port <= 3010; ++port) {
        if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
            ports.push_back(port);
        }
    }

    return ports;
}

std::string LCUClient::ParseGameflowPhaseFromResponse(const std::string& response_body) {
    try {
        nlohmann::json json = nlohmann::json::parse(response_body);
        if (json.is_string()) {
            return json.get<std::string>();
        }
    } catch (const std::exception&) {
        // Fall through to error case
    }
    throw std::invalid_argument("Invalid gameflow phase response format");
}

ReadyCheckStatus LCUClient::ParseReadyCheckFromResponse(const std::string& response_body) {
    ReadyCheckStatus status;

    try {
        nlohmann::json json = nlohmann::json::parse(response_body);

        if (json.contains("declinerIds") && json["declinerIds"].is_array()) {
            for (const auto& id : json["declinerIds"]) {
                if (id.is_number_integer()) {
                    status.decliner_ids.push_back(id.get<int>());
                }
            }
        }

        status.dodge_warning = json.value("dodgeWarning", std::string("None"));
        status.player_response = json.value("playerResponse", std::string("None"));
        status.state = json.value("state", std::string("Invalid"));
        status.suppress_ux = json.value("suppressUx", false);
        status.timer = json.value("timer", 0.0);

    } catch (const std::exception&) {
        // Return default-constructed status on parse error
    }

    return status;
}

LCUResponse LCUClient::MakeRequest(const std::string& method, const std::string& endpoint,
                                  const std::string& body, const std::string& content_type) {
    if (!IsConnected()) {
        return LCUResponse(LCURequestResult::CONNECTION_ERROR);
    }

    auto start_time = std::chrono::steady_clock::now();

    std::shared_ptr<httplib::Response> response;

    if (method == "GET") {
        response = http_client_->Get(endpoint.c_str());
    } else if (method == "POST") {
        response = http_client_->Post(endpoint.c_str(), body, content_type.c_str());
    } else {
        LCUResponse error_response(LCURequestResult::UNKNOWN_ERROR);
        error_response.error_message = "Unsupported HTTP method: " + method;
        return error_response;
    }

    return ProcessHTTPResponse(response, start_time);
}

LCUResponse LCUClient::MakeRequestWithRetry(const std::string& method, const std::string& endpoint,
                                           const std::string& body, const std::string& content_type) {
    LCUResponse last_response;

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        last_response = MakeRequest(method, endpoint, body, content_type);

        if (last_response.IsSuccess()) {
            connection_info_.UpdateLastSuccessfulRequest();
            RecordRequestMetrics(last_response);
            return last_response;
        }

        // Check if we should retry
        if (attempt < max_retries_) {
            if (last_response.result == LCURequestResult::CONNECTION_ERROR && auto_reconnect_enabled_) {
                // Try to reconnect
                if (Reconnect()) {
                    continue; // Retry immediately after successful reconnection
                }
            }

            // Wait before retry
            std::this_thread::sleep_for(retry_delay_);
        }
    }

    // All retries failed
    connection_info_.IncrementConnectionErrors();
    RecordRequestMetrics(last_response);
    return last_response;
}

void LCUClient::SetupHTTPClient() {
    http_client_ = std::make_unique<httplib::SSLClient>("127.0.0.1", connection_info_.GetPort());

    // Configure timeouts
    auto timeout_sec = connection_timeout_.count() / 1000;
    auto timeout_usec = (connection_timeout_.count() % 1000) * 1000;
    http_client_->set_read_timeout(timeout_sec, timeout_usec);
    http_client_->set_write_timeout(timeout_sec, timeout_usec);

    // Configure SSL
    ConfigureSSLSettings();

    // Set authentication
    http_client_->set_basic_auth("riot", connection_info_.GetAuthToken());
}

void LCUClient::ConfigureSSLSettings() {
    if (http_client_) {
        // LCU uses self-signed certificates, so we need to disable verification
        http_client_->enable_server_certificate_verification(false);
    }
}

void LCUClient::UpdateConnectionState(bool connected, const std::string& error) {
    if (connection_callback_) {
        connection_callback_(connected, error);
    }
}

void LCUClient::RecordRequestMetrics(const LCUResponse& response) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (response.IsSuccess()) {
            successful_requests_++;
            total_request_time_ += response.latency;
        } else {
            failed_requests_++;
        }
    }

    if (performance_metrics_) {
        if (response.IsSuccess()) {
            performance_metrics_->RecordDetectionLatency(response.latency);
        } else {
            performance_metrics_->RecordError("LCU request failed: " + response.error_message);
        }
    }
}

LCUResponse LCUClient::ProcessHTTPResponse(std::shared_ptr<httplib::Response> response,
                                          std::chrono::steady_clock::time_point start_time) {
    auto end_time = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    LCUResponse result;
    result.latency = latency;

    if (!response) {
        result.result = LCURequestResult::CONNECTION_ERROR;
        result.error_message = "No response from LCU";
        return result;
    }

    result.status_code = response->status;
    result.body = response->body;
    result.result = MapHTTPStatusToResult(response->status);

    if (!result.IsSuccess()) {
        result.error_message = "HTTP " + std::to_string(response->status) + ": " + response->reason;
    }

    return result;
}

LCURequestResult LCUClient::MapHTTPStatusToResult(int status_code) {
    switch (status_code) {
    case 200:
    case 204:
        return LCURequestResult::SUCCESS;
    case 401:
    case 403:
        return LCURequestResult::AUTHENTICATION_ERROR;
    case 404:
        return LCURequestResult::NOT_FOUND;
    case 408:
        return LCURequestResult::TIMEOUT;
    default:
        if (status_code >= 500) {
            return LCURequestResult::CONNECTION_ERROR;
        }
        return LCURequestResult::UNKNOWN_ERROR;
    }
}

std::string LCUClient::GetAuthHeader() const {
    return "Basic " + httplib::detail::base64_encode("riot:" + connection_info_.GetAuthToken());
}

bool LCUClient::ValidateGameflowResponse(const nlohmann::json& json) const {
    return json.is_string() && !json.get<std::string>().empty();
}

bool LCUClient::ValidateReadyCheckResponse(const nlohmann::json& json) const {
    return json.is_object() &&
           json.contains("state") &&
           json.contains("timer") &&
           json["state"].is_string() &&
           json["timer"].is_number();
}

} // namespace league_auto_accept