#include "league_auto_accept/application.h"
#include <iostream>
#include <exception>
#include <windows.h>

// Global application instance for signal handling
static league_auto_accept::Application* g_application = nullptr;

// Console control handler for graceful shutdown
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_application) {
            std::cout << "\nShutting down gracefully..." << std::endl;
            g_application->Shutdown();
        }
        return TRUE;
    default:
        return FALSE;
    }
}

// Windows message handler for hotkeys and system messages
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_HOTKEY:
        // Handle hotkey presses
        if (g_application) {
            switch (wParam) {
            case 9001: // Emergency disable
                g_application->DisableAutoAccept();
                break;
            case 9002: // Toggle auto-accept
                g_application->ToggleAutoAccept();
                break;
            }
        }
        return 0;

    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        if (g_application) {
            g_application->Shutdown();
        }
        return 0;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int main(int argc, char* argv[]) {
    int exit_code = 0;

    try {
        // Create application instance
        league_auto_accept::Application app;
        g_application = &app;

        // Set up console control handler for graceful shutdown
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

        // Initialize application
        if (!app.Initialize(argc, argv)) {
            std::cerr << "Failed to initialize application" << std::endl;
            if (app.HasErrors()) {
                std::cerr << "Error: " << app.GetLastError() << std::endl;
            }
            return 1;
        }

        // Run application
        exit_code = app.Run();

        // Cleanup
        app.Shutdown();

    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        exit_code = 1;
    } catch (...) {
        std::cerr << "Unknown exception occurred" << std::endl;
        exit_code = 1;
    }

    // Cleanup global reference
    g_application = nullptr;

    return exit_code;
}

// Windows application entry point (for non-console builds)
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Parse command line into argc/argv format
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (!wargv) {
        return 1;
    }

    // Convert wide strings to narrow strings
    std::vector<std::string> args;
    std::vector<char*> argv;

    for (int i = 0; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            std::string arg(size - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &arg[0], size, nullptr, nullptr);
            args.push_back(arg);
            argv.push_back(&args.back()[0]);
        }
    }

    LocalFree(wargv);

    // Call main function
    return main(static_cast<int>(argv.size()), argv.data());
}
#endif