cmake_minimum_required(VERSION 3.22)

project(Roadie VERSION 0.1.0)

add_subdirectory(libs/JUCE)

juce_add_gui_app(Roadie
    PRODUCT_NAME "Roadie")

target_sources(Roadie PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp)

target_compile_definitions(Roadie PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Roadie,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Roadie,JUCE_VERSION>")

target_link_libraries(Roadie
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
