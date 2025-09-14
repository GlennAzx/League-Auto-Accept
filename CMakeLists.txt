cmake_minimum_required(VERSION 3.20)
project(LeagueAutoAcceptGUI VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set static runtime linking for Windows
if(WIN32 AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Source files
set(SOURCES
    src/league_auto_accept_gui.cpp
)

# Create executable
add_executable(LeagueAutoAcceptGUI_Fixed ${SOURCES})

# Windows specific settings
if(WIN32)
    # Windows subsystem for GUI application (no console)
    set_target_properties(LeagueAutoAcceptGUI_Fixed PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "League_Auto_Accept"
    )

    # Link Windows libraries
    target_link_libraries(LeagueAutoAcceptGUI_Fixed PRIVATE
        ws2_32
        winhttp
        user32
        gdi32
        shell32
        advapi32
        comctl32
        kernel32
    )
endif()

# Compiler optimizations for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        # MSVC optimizations
        target_compile_options(LeagueAutoAcceptGUI_Fixed PRIVATE
            /O2          # Optimize for speed
            /GL          # Whole program optimization
            /DNDEBUG     # Disable debug assertions
        )

        # Linker optimizations
        set_target_properties(LeagueAutoAcceptGUI_Fixed PROPERTIES
            LINK_FLAGS "/LTCG /OPT:REF /OPT:ICF"
        )

        # Strip symbols for smaller executable
        set_target_properties(LeagueAutoAcceptGUI_Fixed PROPERTIES
            LINK_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF"
        )
    else()
        # GCC/Clang optimizations
        target_compile_options(LeagueAutoAcceptGUI_Fixed PRIVATE
            -O3
            -DNDEBUG
            -flto
        )
        target_link_options(LeagueAutoAcceptGUI_Fixed PRIVATE
            -flto
            -s  # Strip symbols
        )
    endif()
endif()

# Add version information
target_compile_definitions(LeagueAutoAcceptGUI_Fixed PRIVATE
    APP_VERSION_MAJOR=1
    APP_VERSION_MINOR=0
    APP_VERSION_PATCH=0
    APP_NAME="League Auto-Accept GUI"
)

# Set output directory
set_target_properties(LeagueAutoAcceptGUI_Fixed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# Create build info
message(STATUS "Building ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Target: GUI Application with System Tray")