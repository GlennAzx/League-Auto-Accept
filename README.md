# League Auto-Accept

A lightweight C++ desktop application for automatically accepting League of Legends match invitations using Riot's official LCU API. Features system tray integration, hotkey support, and <200ms response times with minimal resource usage.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-lightgrey.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)

## Features

- **🚀 LCU API Integration**: Uses Riot's official League Client Update API for reliable ready check detection
- **⚡ High Performance**: <200ms acceptance latency, <30MB memory usage
- **🎯 System Tray Integration**: Runs minimized in system tray with custom icon and context menu controls
- **⌨️ Hotkey Support**: F9 emergency disable and other customizable hotkeys
- **⚙️ Configurable**: JSON configuration with real-time updates and validation
- **🔒 Secure & Compliant**: Uses only official APIs, no memory injection or ToS violations
- **📊 Activity Logging**: Real-time logging with connection status and acceptance tracking

## Quick Start

### Download and Run

1. Download the latest `League_Auto_Accept.exe` from [Releases](https://github.com/yourusername/League-Auto-Accept/releases)
2. Place the executable in any directory of your choice
3. Run `League_Auto_Accept.exe` (no admin privileges required)
4. The application will minimize to system tray - right-click the tray icon to access settings
5. Start League of Legends and the application will automatically detect and accept ready checks

### Requirements

- **Operating System**: Windows 10/11 (64-bit)
- **League of Legends**: Any current version with LCU API support
- **Dependencies**: None (statically compiled executable)

## Performance Specifications

| Metric | Performance |
|--------|-------------|
| **Detection Latency** | 50-100ms |
| **Acceptance Latency** | 120-200ms total |
| **Memory Usage** | 15-30MB working set |
| **CPU Usage (Idle)** | <2% CPU usage |
| **Executable Size** | ~3MB single file |

## Configuration

The application automatically creates a `config.json` file on first run. You can customize the behavior:

```json
{
  "auto_accept_enabled": true,
  "polling_interval": 1000,
  "hotkey": "F9",
  "minimize_to_tray": true,
  "start_minimized": false,
  "log_level": "info"
}
```

### Configuration Options

- **auto_accept_enabled**: Enable/disable automatic ready check acceptance
- **polling_interval**: How often to check for ready checks (milliseconds, 500-2000 recommended)
- **hotkey**: Emergency disable key (F9 default, F1-F12 supported)
- **minimize_to_tray**: Whether minimize button sends to tray (true) or taskbar (false)
- **start_minimized**: Start application minimized to system tray
- **log_level**: Logging verbosity (error, warn, info, debug)

## How It Works

### LCU API Integration

The application integrates with Riot's League Client Update (LCU) API to detect and accept ready checks:

1. **Client Detection**: Automatically finds League client by reading the LCU lockfile
2. **Authentication**: Uses lockfile credentials for secure HTTPS communication
3. **Phase Monitoring**: Polls `/lol-gameflow/v1/gameflow-phase` endpoint to detect ready checks
4. **Auto-Acceptance**: Sends POST request to `/lol-matchmaking/v1/ready-check/accept`

### Key Features

- **⚡ Fast Response**: Typically accepts within 100-200ms of ready check appearance
- **🔒 Secure**: Uses only official Riot APIs with proper authentication
- **🎯 Reliable**: Direct API communication is more reliable than screen automation
- **📊 Logging**: Tracks connection status, phase changes, and acceptance events

### System Integration

- **System Tray**: Runs minimized with right-click context menu
- **Hotkey Support**: F9 emergency disable (configurable)
- **Auto-Start**: Can be configured to start with Windows (optional)
- **Graceful Shutdown**: Proper cleanup when League client closes

## Building from Source

### Prerequisites

- **Compiler**: Visual Studio 2019+ with C++ Desktop Development workload
- **Build System**: CMake 3.20 or later
- **Platform**: Windows 10/11 (64-bit)

### Build Process

```bash
# Clone repository
git clone https://github.com/yourusername/League-Auto-Accept.git
cd League-Auto-Accept

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# Build project
cmake --build . --config Release

# Output executable will be in build/bin/League_Auto_Accept.exe
```

### Dependencies

The application uses only Windows system libraries (no external dependencies):

- **WinHTTP**: For HTTPS requests to LCU API
- **Win32 API**: For GUI, system tray, and hotkey support
- **Windows Resource Compiler**: For embedding custom application icon
- **Standard Libraries**: C++17 STL for core functionality

All dependencies are statically linked into a single executable with embedded resources.

## Project Structure

```
League-Auto-Accept/
├── src/
│   └── league_auto_accept_gui.cpp  # Main application source
├── resources/
│   ├── app_icon.ico                # Custom application icon
│   ├── resources.rc                # Windows resource file
│   └── simple_icon.py              # Icon generator script
├── CMakeLists.txt                  # Build configuration
├── README.md                       # This file
└── config.json                     # Runtime configuration (auto-generated)
```

### Code Overview

The application is built as a single-file C++ program with the following key components:

- **GUI Management**: Windows API for main window and controls
- **System Tray**: NOTIFYICONDATA for tray integration with context menus and custom icon
- **LCU Client**: WinHTTP-based client for League API communication
- **Threading**: Dedicated worker thread for network operations
- **Configuration**: JSON-based config with runtime validation
- **Resources**: Windows Resource Compiler for embedding custom application icon
- **Logging**: Real-time activity logging with GUI display

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes to `src/league_auto_accept_gui.cpp`
4. Test the build: `cmake --build build --config Release`
5. Test functionality with League client
6. Commit changes (`git commit -m 'Add amazing feature'`)
7. Push to branch (`git push origin feature/amazing-feature`)
8. Create a Pull Request

### Development Guidelines

- **Code Style**: Follow existing code formatting and naming conventions
- **Testing**: Test with actual League client to ensure functionality
- **Performance**: Maintain <30MB memory usage and fast response times
- **Compatibility**: Ensure changes work with current League client versions

## Troubleshooting

### Common Issues

**"Could not connect to League client"**
- Ensure League of Legends client is running
- Check that LCU API is accessible (restart League if needed)
- Verify no firewall is blocking localhost connections

**"Application not responding"**
- Check Windows Task Manager for hung processes
- Restart the application
- Try running as administrator if issues persist

**Ready checks not being accepted**
- Verify auto-accept is enabled (check system tray menu)
- Confirm League client is in matchmaking/lobby phase
- Check activity log for connection status

**High resource usage**
- Increase `polling_interval` to 1500-2000ms in config.json
- Close other applications that might conflict
- Restart both League client and auto-accept application

### Configuration Issues

**Config file problems**
- Delete config.json to reset to defaults
- Ensure JSON syntax is valid (use a JSON validator)
- Check file permissions in application directory

**Hotkey not working**
- Try different function keys (F1-F12)
- Ensure no other applications are using the same hotkey
- Restart application after changing hotkey in config

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Disclaimer

This software is provided "as is" without warranty. Use at your own risk. The developers are not responsible for any account actions taken by Riot Games.

**Important**: This application uses only official Riot Games LCU API endpoints and does not:
- Read or modify game memory
- Inject code into game processes
- Automate gameplay beyond accepting ready checks
- Violate Riot Games Terms of Service

The application is designed to comply with Riot's developer policies by using only sanctioned API methods.

## Support

- 🐛 [Report Issues](https://github.com/yourusername/League-Auto-Accept/issues)
- 💡 [Feature Requests](https://github.com/yourusername/League-Auto-Accept/discussions)
- ⭐ Star this repository if it helps you!

---

**Made with ❤️ for the League of Legends community**