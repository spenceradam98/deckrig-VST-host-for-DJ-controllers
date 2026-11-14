cmake_minimum_required(VERSION 3.22)

# macOS 11 (Big Sur) on Intel is the minimum; universal binary (x86_64 + arm64).
# No -march flags anywhere: scalar C++ only, runs on any x86-64 (no AVX2 needed).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()

project(DeckRig VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout with -DJUCE_DIR=/path/to/JUCE, otherwise fetch it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Brand placeholders: change once the brand name is decided.
set(BRAND_NAME "YourBrand")
set(PRODUCT_NAME "DeckRig")

set(DECKRIG_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_VST=0
    DECKRIG_BRAND_NAME="${BRAND_NAME}"
    DECKRIG_PRODUCT_NAME="${PRODUCT_NAME}"
    DECKRIG_VERSION="${PROJECT_VERSION}"
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1)

set(DECKRIG_SOURCES
    Source/core/Mapping.cpp
    Source/core/TrackAnalysis.cpp
    Source/audio/TrackPlayer.cpp
    Source/audio/Deck.cpp
    Source/audio/HostEngine.cpp
    Source/hosting/PluginChain.cpp
    Source/ui/Widgets.cpp
    Source/ui/DeckWidgets.cpp
    Source/ui/Panels.cpp
    Source/ui/MainComponent.cpp)

juce_add_binary_data(DeckRigData SOURCES profiles/hercules-djcontrol-mix-ultra.json)

juce_add_gui_app(DeckRig
    PRODUCT_NAME "${PRODUCT_NAME}"
    COMPANY_NAME "${BRAND_NAME}"
    BUNDLE_ID com.yourbrand.deckrig
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DeckRig uses audio inputs to run live sound through your plugins.")

target_sources(DeckRig PRIVATE Source/Main.cpp ${DECKRIG_SOURCES})
target_include_directories(DeckRig PRIVATE Source)
target_compile_definitions(DeckRig PRIVATE ${DECKRIG_DEFS})
target_link_libraries(DeckRig
    PRIVATE
        DeckRigData
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Tests: mapping model and mixer maths (juce_core only, headless) ----
option(DECKRIG_BUILD_TESTS "Build unit tests" ON)
if(DECKRIG_BUILD_TESTS)
    juce_add_console_app(DeckRigTests PRODUCT_NAME "DeckRigTests")
    target_sources(DeckRigTests PRIVATE tests/Tests.cpp Source/core/Mapping.cpp Source/core/TrackAnalysis.cpp Source/audio/TrackPlayer.cpp)
    target_include_directories(DeckRigTests PRIVATE Source)
    target_compile_definitions(DeckRigTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(DeckRigTests PRIVATE DeckRigData juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME deckrig COMMAND DeckRigTests)
endif()

# ---- Screenshot renderer (developer tool; run under xvfb-run on Linux) ----
option(DECKRIG_BUILD_TOOLS "Build screenshot tool" OFF)
if(DECKRIG_BUILD_TOOLS)
    juce_add_console_app(DeckRigSnapshots PRODUCT_NAME "DeckRigSnapshots")
    target_sources(DeckRigSnapshots PRIVATE tools/Snapshots.cpp ${DECKRIG_SOURCES})
    target_include_directories(DeckRigSnapshots PRIVATE Source)
    target_compile_definitions(DeckRigSnapshots PRIVATE ${DECKRIG_DEFS})
    target_link_libraries(DeckRigSnapshots PRIVATE DeckRigData juce::juce_audio_utils juce::juce_audio_processors
        PUBLIC juce::juce_recommended_config_flags)
endif()
