cmake_minimum_required(VERSION 3.22)

project(Roadie VERSION 0.2.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

# NeuralAmpModelerCore requires C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)

add_subdirectory(libs/JUCE)

# Without the usage descriptions, macOS denies the guitar input (silence or a
# TCC kill on first access) and the camera.
juce_add_gui_app(Roadie
    PRODUCT_NAME "Roadie"
    COMPANY_NAME "Roadie"
    BUNDLE_ID "app.roadie.Roadie"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/docs/logo/roadie-icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Roadie a besoin de l'entrée audio pour capter ta guitare, et du micro de la caméra pour la synchroniser."
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "Roadie a besoin de la caméra pour filmer tes prises."
    # Lets an iPhone (Continuity Camera) show up as a webcam.
    PLIST_TO_MERGE "<plist><dict><key>NSCameraUseContinuityCameraDeviceType</key><true/></dict></plist>")

set(NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore")
file(GLOB NAM_SOURCES
    "${NAM_DIR}/NAM/*.cpp"
    "${NAM_DIR}/NAM/*/*.cpp")

# NAM's sources compiled once, shared by the app and the engine tests (its
# own CMakeLists doesn't define a reusable target). OBJECT, not STATIC: each
# architecture (WaveNet, LSTM, Slimmable…) registers itself from a static
# initialiser that nothing references, so a static archive would let the
# linker drop them — "No config parser registered for architecture".
add_library(RoadieNAM OBJECT ${NAM_SOURCES})
target_include_directories(RoadieNAM PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/Dependencies/eigen
    ${NAM_DIR}/Dependencies/nlohmann)
# Buffers are float on the JUCE side; without this NAM uses double.
target_compile_definitions(RoadieNAM PUBLIC NAM_SAMPLE_FLOAT=1)

target_sources(Roadie PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Session.cpp
    Source/AudioEngine.cpp
    Source/AmpEngine.cpp
    Source/ToneStack.cpp
    Source/NoiseGate.cpp
    Source/Looper.cpp
    Source/Metronome.cpp
    Source/TakeRecorder.cpp
    Source/SyncAnalysis.cpp
    Source/SongAnalysis.cpp
    Source/Tuner.cpp
    Source/TimeStretch.cpp
    Source/Project.cpp
    Source/CaptureLibrary.cpp
    Source/Theme.cpp
    Source/RoadieLookAndFeel.cpp
    Source/UI/Icons.cpp
    Source/UI/Widgets.cpp
    Source/UI/TopBar.cpp
    Source/UI/LibraryPanel.cpp
    Source/UI/Timeline.cpp
    Source/UI/Inspector.cpp
    Source/UI/Monitor.cpp
    Source/UI/StagePanel.cpp
    Source/UI/ExportSheet.cpp)

# Camera capture and clip export talk to AVFoundation directly (see
# CameraCapture.h for why not juce::CameraDevice). Other platforms get stubs
# that report the camera as unavailable.
if(APPLE)
    set(ROADIE_MAC_SOURCES Source/CameraCapture_mac.mm Source/ClipExporter_mac.mm Source/VideoPreview_mac.mm)
    target_sources(Roadie PRIVATE ${ROADIE_MAC_SOURCES})
    set_source_files_properties(${ROADIE_MAC_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(Roadie PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework CoreText")
else()
    target_sources(Roadie PRIVATE Source/Platform_stub.cpp)
endif()

# macOS silently denies the camera (and may deny the microphone) to an
# unsigned app without even asking, and Intel linkers don't sign. Ad-hoc
# signing is enough to get the permission prompts. Each ad-hoc build is a new
# identity for macOS, which may ask again after a rebuild: set
# ROADIE_CODESIGN_IDENTITY to an "Apple Development" certificate to avoid it.
# yt-dlp (universal standalone binary) is embedded in the bundle so the
# YouTube import needs nothing installed. The version is pinned and checked
# against the SHA-256 published with the release: to update it, change both
# (SHA2-256SUMS on https://github.com/yt-dlp/yt-dlp/releases, line yt-dlp_macos).
# YouTube changes often: a stale yt-dlp is the first thing to break the import.
if(APPLE)
    set(ROADIE_YTDLP_VERSION "2026.10.26")
    set(ROADIE_YTDLP_SHA256 "0f922c15ccc0b83eefa1c8bc9104e434318c4bf16e408e1ee93b7745a6efe0aa")
    set(ROADIE_YTDLP "${CMAKE_CURRENT_SOURCE_DIR}/libs/yt-dlp/yt-dlp")
    if(EXISTS "${ROADIE_YTDLP}")
        file(SHA256 "${ROADIE_YTDLP}" ytdlp_actual)
        if(NOT ytdlp_actual STREQUAL ROADIE_YTDLP_SHA256)
            message(STATUS "yt-dlp does not match the pinned version ${ROADIE_YTDLP_VERSION}: downloading it again")
            file(REMOVE "${ROADIE_YTDLP}")
        endif()
    endif()
    if(NOT EXISTS "${ROADIE_YTDLP}")
        message(STATUS "Downloading yt-dlp ${ROADIE_YTDLP_VERSION}…")
        file(DOWNLOAD "https://github.com/yt-dlp/yt-dlp/releases/download/${ROADIE_YTDLP_VERSION}/yt-dlp_macos" "${ROADIE_YTDLP}"
             EXPECTED_HASH SHA256=${ROADIE_YTDLP_SHA256} STATUS ytdlp_status)
        list(GET ytdlp_status 0 ytdlp_code)
        if(NOT ytdlp_code EQUAL 0)
            file(REMOVE "${ROADIE_YTDLP}")
            message(FATAL_ERROR "Could not download yt-dlp ${ROADIE_YTDLP_VERSION}: ${ytdlp_status}")
        endif()
        file(CHMOD "${ROADIE_YTDLP}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
    add_custom_command(TARGET Roadie POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:Roadie>/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${ROADIE_YTDLP}" "$<TARGET_BUNDLE_CONTENT_DIR:Roadie>/Resources/yt-dlp"
        VERBATIM)
endif()

if(APPLE)
    set(ROADIE_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for Roadie.app (- = ad-hoc)")
    add_custom_command(TARGET Roadie POST_BUILD
        COMMAND codesign --force --deep --sign "${ROADIE_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:Roadie>"
        COMMENT "Signing Roadie.app (${ROADIE_CODESIGN_IDENTITY})"
        VERBATIM)
endif()

target_include_directories(Roadie PRIVATE Source)

target_compile_definitions(Roadie PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Roadie,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Roadie,JUCE_VERSION>")

target_link_libraries(Roadie
    PRIVATE
        RoadieNAM
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
# LTO is intentionally skipped: with NAM's Eigen-heavy sources it drives link
# times into the tens of minutes for a marginal runtime gain. -O3 from the
# Release config flags above is what actually fixes real-time performance.

# Logic tests for the JUCE-free parts (looper, tone stack, metronome, sync
# analysis): seconds to build, run with `ctest --test-dir build`.
enable_testing()
add_executable(RoadieLooperTests Tests/LooperTests.cpp Source/Looper.cpp)
target_include_directories(RoadieLooperTests PRIVATE Source)
add_test(NAME looper COMMAND RoadieLooperTests)

add_executable(RoadieDspTests Tests/DspTests.cpp Source/TimeStretch.cpp Source/Tuner.cpp Source/NoiseGate.cpp Source/ToneStack.cpp Source/SyncAnalysis.cpp Source/SongAnalysis.cpp Source/Metronome.cpp)
target_include_directories(RoadieDspTests PRIVATE Source)
add_test(NAME dsp COMMAND RoadieDspTests)

# Engine integration tests: the real-time chain through a fake audio device,
# a real NAM capture, takes, calibration capture, project files and (macOS)
# the MP4 export from a synthetic camera movie.
juce_add_console_app(RoadieEngineTests PRODUCT_NAME "RoadieEngineTests")
target_sources(RoadieEngineTests PRIVATE
    Tests/EngineTests.cpp
    Source/Tuner.cpp
    Source/AudioEngine.cpp
    Source/AmpEngine.cpp
    Source/ToneStack.cpp
    Source/NoiseGate.cpp
    Source/Looper.cpp
    Source/Metronome.cpp
    Source/TakeRecorder.cpp
    Source/SyncAnalysis.cpp
    Source/SongAnalysis.cpp
    Source/Project.cpp)
if(APPLE)
    set(ROADIE_TEST_MAC_SOURCES Source/ClipExporter_mac.mm Tests/TestMovie_mac.mm)
    target_sources(RoadieEngineTests PRIVATE ${ROADIE_TEST_MAC_SOURCES})
    set_source_files_properties(${ROADIE_TEST_MAC_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(RoadieEngineTests PRIVATE
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework QuartzCore" "-framework CoreText")
else()
    target_sources(RoadieEngineTests PRIVATE Source/Platform_stub.cpp)
endif()
target_include_directories(RoadieEngineTests PRIVATE Source Tests)
target_compile_definitions(RoadieEngineTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    ROADIE_TEST_NAM="${NAM_DIR}/example_models/wavenet.nam")
target_link_libraries(RoadieEngineTests PRIVATE
    RoadieNAM
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
add_test(NAME engine COMMAND RoadieEngineTests)

foreach(test_target RoadieLooperTests RoadieDspTests)
    # Tests rely on assert(): keep it on whatever the build type.
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
endforeach()

# CPU benchmark of a capture at a given buffer size (see README): reuses the
# NAM objects already built for the app.
add_executable(RoadieNamBench Tools/nam_bench.cpp)
target_link_libraries(RoadieNamBench PRIVATE RoadieNAM)
