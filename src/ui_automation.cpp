#include "league_auto_accept/ui_automation.h"
#include "league_auto_accept/models/performance_metrics.h"
#include <stdexcept>

namespace league_auto_accept {

// Static member initialization
double UIAutomation::system_dpi_scale_ = 1.0;
bool UIAutomation::dpi_scale_initialized_ = false;

UIAutomation::UIAutomation()
    : UIAutomation(nullptr) {
}

UIAutomation::UIAutomation(std::shared_ptr<models::PerformanceMetrics> metrics)
    : ui_scale_factor_(DEFAULT_UI_SCALE_FACTOR)
    , template_match_threshold_(DEFAULT_TEMPLATE_THRESHOLD)
    , use_search_region_(false)
    , performance_metrics_(metrics)
    , multi_scale_enabled_(false)
    , min_scale_(0.8)
    , max_scale_(1.2)
    , scale_step_(0.1)
    , last_detection_time_(0)
    , last_click_time_(0)
    , successful_detections_(0)
    , total_detections_(0) {

    if (!dpi_scale_initialized_) {
        InitializeDPIScale();
    }
}

UIAutomation::~UIAutomation() = default;

bool UIAutomation::Initialize() {
    try {
        // Load default accept button template
        if (!LoadAcceptButtonTemplateFromResource()) {
            return false;
        }

        // Test screen capture
        cv::Mat test_screen = CaptureScreen();
        if (test_screen.empty()) {
            return false;
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void UIAutomation::SetUIScaleFactor(double scale_factor) {
    if (scale_factor >= 0.5 && scale_factor <= 3.0) {
        ui_scale_factor_ = scale_factor;
    } else {
        throw std::invalid_argument("UI scale factor must be between 0.5 and 3.0");
    }
}

double UIAutomation::GetUIScaleFactor() const {
    return ui_scale_factor_;
}

void UIAutomation::SetTemplateMatchThreshold(double threshold) {
    if (threshold >= MIN_TEMPLATE_THRESHOLD && threshold <= MAX_TEMPLATE_THRESHOLD) {
        template_match_threshold_ = threshold;
    } else {
        throw std::invalid_argument("Template match threshold must be between 0.6 and 0.95");
    }
}

double UIAutomation::GetTemplateMatchThreshold() const {
    return template_match_threshold_;
}

bool UIAutomation::LoadAcceptButtonTemplate(const std::string& template_path) {
    return LoadTemplateFromFile(template_path);
}

bool UIAutomation::LoadAcceptButtonTemplateFromResource() {
    // For now, create a simple synthetic template
    // In full implementation, this would load from embedded resource
    accept_button_template_ = cv::Mat::zeros(DEFAULT_TEMPLATE_HEIGHT, DEFAULT_TEMPLATE_WIDTH, CV_8UC3);

    // Create a simple button-like template (rectangle with text area)
    cv::rectangle(accept_button_template_,
                 cv::Rect(10, 10, DEFAULT_TEMPLATE_WIDTH-20, DEFAULT_TEMPLATE_HEIGHT-20),
                 cv::Scalar(100, 150, 100), -1);
    cv::rectangle(accept_button_template_,
                 cv::Rect(10, 10, DEFAULT_TEMPLATE_WIDTH-20, DEFAULT_TEMPLATE_HEIGHT-20),
                 cv::Scalar(200, 255, 200), 2);

    return !accept_button_template_.empty();
}

bool UIAutomation::HasValidTemplate() const {
    return !accept_button_template_.empty() && IsValidTemplate(accept_button_template_);
}

cv::Mat UIAutomation::GetCurrentTemplate() const {
    return accept_button_template_.clone();
}

cv::Mat UIAutomation::CaptureScreen() {
    ScreenRegion screen_dim = GetPrimaryScreenDimensions();
    return CaptureScreenRegion(screen_dim);
}

cv::Mat UIAutomation::CaptureScreenRegion(const ScreenRegion& region) {
    try {
        return CaptureDesktop(); // Simplified implementation
    } catch (const std::exception&) {
        return cv::Mat();
    }
}

ScreenRegion UIAutomation::GetPrimaryScreenDimensions() {
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    return ScreenRegion(0, 0, width, height);
}

TemplateMatchResult UIAutomation::FindAcceptButton() {
    cv::Mat screen = CaptureScreen();
    if (screen.empty()) {
        return TemplateMatchResult();
    }
    return FindAcceptButton(screen);
}

TemplateMatchResult UIAutomation::FindAcceptButton(const cv::Mat& screen_image) {
    if (!HasValidTemplate()) {
        return TemplateMatchResult();
    }

    auto start_time = std::chrono::steady_clock::now();
    TemplateMatchResult result;

    try {
        ScreenRegion search_region = use_search_region_ ? search_region_ :
                                   ScreenRegion(0, 0, screen_image.cols, screen_image.rows);

        if (multi_scale_enabled_) {
            result = PerformMultiScaleMatching(screen_image, accept_button_template_, search_region);
        } else {
            result = PerformTemplateMatching(screen_image, accept_button_template_, search_region);
        }

        auto end_time = std::chrono::steady_clock::now();
        result.detection_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        result.search_region = search_region;

        RecordDetectionMetrics(result);

        if (detection_callback_) {
            detection_callback_(result);
        }

    } catch (const std::exception&) {
        result = TemplateMatchResult();
    }

    return result;
}

ClickResult UIAutomation::ClickAcceptButton() {
    TemplateMatchResult detection = FindAcceptButton();

    if (!detection.found) {
        ClickResult result;
        result.success = false;
        result.error_message = "Accept button not found";
        return result;
    }

    // Calculate center of detected button
    cv::Point center = detection.location;
    center.x += accept_button_template_.cols / 2;
    center.y += accept_button_template_.rows / 2;

    return ClickAtLocation(center);
}

ClickResult UIAutomation::ClickAtLocation(const cv::Point& location) {
    return ClickAtLocation(location.x, location.y);
}

ClickResult UIAutomation::ClickAtLocation(int x, int y) {
    auto start_time = std::chrono::steady_clock::now();

    ClickResult result;
    result.target_location = cv::Point(x, y);

    try {
        // Adjust for DPI scaling
        cv::Point adjusted = AdjustForDPI(cv::Point(x, y));

        bool success = SimulateMouseClick(adjusted.x, adjusted.y);

        if (success) {
            result.success = true;
            result.actual_location = adjusted;
        } else {
            result.success = false;
            result.error_message = "Failed to simulate mouse click";
        }

        auto end_time = std::chrono::steady_clock::now();
        result.click_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        RecordClickMetrics(result);

        if (click_callback_) {
            click_callback_(result);
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }

    return result;
}

bool UIAutomation::SimulateMouseClick(int x, int y, int button) {
    DWORD down_flag, up_flag;

    switch (button) {
    case 0: // Left button
        down_flag = MOUSEEVENTF_LEFTDOWN;
        up_flag = MOUSEEVENTF_LEFTUP;
        break;
    case 1: // Right button
        down_flag = MOUSEEVENTF_RIGHTDOWN;
        up_flag = MOUSEEVENTF_RIGHTUP;
        break;
    case 2: // Middle button
        down_flag = MOUSEEVENTF_MIDDLEDOWN;
        up_flag = MOUSEEVENTF_MIDDLEUP;
        break;
    default:
        return false;
    }

    // Move cursor to position
    if (!SetCursorPosition(x, y)) {
        return false;
    }

    // Simulate mouse down and up
    return SendMouseInput(x, y, down_flag) && SendMouseInput(x, y, up_flag);
}

double UIAutomation::GetSystemDPIScale() {
    if (!dpi_scale_initialized_) {
        InitializeDPIScale();
    }
    return system_dpi_scale_;
}

void UIAutomation::InitializeDPIScale() {
    system_dpi_scale_ = QuerySystemDPIScale();
    dpi_scale_initialized_ = true;
}

double UIAutomation::QuerySystemDPIScale() {
    HDC screen = GetDC(NULL);
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);

    // Standard DPI is 96
    return static_cast<double>(dpi) / 96.0;
}

cv::Point UIAutomation::AdjustForDPI(const cv::Point& point) {
    double scale = GetSystemDPIScale();
    return cv::Point(
        static_cast<int>(point.x * scale),
        static_cast<int>(point.y * scale)
    );
}

// Helper method implementations (simplified)
cv::Mat UIAutomation::CaptureDesktop() {
    // Simplified implementation - would use Windows GDI+ in full version
    ScreenRegion screen = GetPrimaryScreenDimensions();
    return cv::Mat::zeros(screen.height, screen.width, CV_8UC3);
}

TemplateMatchResult UIAutomation::PerformTemplateMatching(const cv::Mat& screen, const cv::Mat& template_img,
                                                         const ScreenRegion& search_region) {
    TemplateMatchResult result;

    try {
        cv::Mat search_area = screen;
        if (search_region.IsValid() &&
            search_region.x + search_region.width <= screen.cols &&
            search_region.y + search_region.height <= screen.rows) {
            search_area = screen(search_region.ToCVRect());
        }

        cv::Mat match_result;
        cv::matchTemplate(search_area, template_img, match_result, cv::TM_CCOEFF_NORMED);

        double min_val, max_val;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(match_result, &min_val, &max_val, &min_loc, &max_loc);

        if (max_val >= template_match_threshold_) {
            result.found = true;
            result.confidence = max_val;
            result.location = max_loc;

            // Adjust location if we searched in a sub-region
            if (search_region.IsValid()) {
                result.location.x += search_region.x;
                result.location.y += search_region.y;
            }
        }

    } catch (const std::exception&) {
        result.found = false;
    }

    return result;
}

TemplateMatchResult UIAutomation::PerformMultiScaleMatching(const cv::Mat& screen, const cv::Mat& template_img,
                                                           const ScreenRegion& search_region) {
    TemplateMatchResult best_result;

    for (double scale = min_scale_; scale <= max_scale_; scale += scale_step_) {
        cv::Mat scaled_template = ScaleTemplate(template_img, scale);
        TemplateMatchResult result = PerformTemplateMatching(screen, scaled_template, search_region);

        if (result.found && result.confidence > best_result.confidence) {
            best_result = result;
        }
    }

    return best_result;
}

cv::Mat UIAutomation::ScaleTemplate(const cv::Mat& template_img, double scale) {
    cv::Mat scaled;
    cv::Size new_size(
        static_cast<int>(template_img.cols * scale),
        static_cast<int>(template_img.rows * scale)
    );
    cv::resize(template_img, scaled, new_size);
    return scaled;
}

bool UIAutomation::LoadTemplateFromFile(const std::string& file_path) {
    try {
        accept_button_template_ = cv::imread(file_path, cv::IMREAD_COLOR);
        return !accept_button_template_.empty();
    } catch (const std::exception&) {
        return false;
    }
}

bool UIAutomation::SendMouseInput(int x, int y, DWORD flags) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = x;
    input.mi.dy = y;
    input.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE;

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

bool UIAutomation::SetCursorPosition(int x, int y) {
    return SetCursorPos(x, y) != 0;
}

void UIAutomation::RecordDetectionMetrics(const TemplateMatchResult& result) {
    total_detections_++;
    if (result.found) {
        successful_detections_++;
    }

    last_detection_time_ = result.detection_time;

    if (performance_metrics_) {
        performance_metrics_->RecordDetectionLatency(result.detection_time);
    }
}

void UIAutomation::RecordClickMetrics(const ClickResult& result) {
    last_click_time_ = result.click_time;

    if (performance_metrics_ && result.success) {
        performance_metrics_->RecordAcceptanceLatency(result.click_time);
    }
}

bool UIAutomation::IsValidTemplate(const cv::Mat& template_img) const {
    return !template_img.empty() &&
           template_img.cols > 10 &&
           template_img.rows > 10 &&
           template_img.type() == CV_8UC3;
}

double UIAutomation::GetDetectionAccuracy() const {
    if (total_detections_ == 0) return 0.0;
    return static_cast<double>(successful_detections_) / static_cast<double>(total_detections_);
}

} // namespace league_auto_accept