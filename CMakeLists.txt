cmake_minimum_required(VERSION 3.22)

project(Roadie VERSION 0.1.0)

# NeuralAmpModelerCore requires C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/JUCE)

juce_add_gui_app(Roadie
    PRODUCT_NAME "Roadie")

set(NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore")
file(GLOB NAM_SOURCES
    "${NAM_DIR}/NAM/*.cpp"
    "${NAM_DIR}/NAM/*/*.cpp")

# Known Eigen warning tripped up by NAM's own -Wall/-Wextra; matches NAM's own workaround.
set_source_files_properties(${NAM_DIR}/NAM/dsp.cpp ${NAM_DIR}/NAM/conv1d.cpp
    PROPERTIES COMPILE_FLAGS "-Wno-error")

target_sources(Roadie PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AmpEngine.cpp
    Source/RoadieLookAndFeel.cpp
    Source/Looper.cpp
    Source/Metronome.cpp
    ${NAM_SOURCES})

target_include_directories(Roadie PRIVATE
    ${NAM_DIR}
    ${NAM_DIR}/Dependencies/eigen
    ${NAM_DIR}/Dependencies/nlohmann)

target_compile_definitions(Roadie PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    NAM_SAMPLE_FLOAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Roadie,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Roadie,JUCE_VERSION>")

target_link_libraries(Roadie
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
# LTO is intentionally skipped: with NAM's Eigen-heavy sources it drives link
# times into the tens of minutes for a marginal runtime gain. -O3 from the
# Release config flags above is what actually fixes real-time performance.
