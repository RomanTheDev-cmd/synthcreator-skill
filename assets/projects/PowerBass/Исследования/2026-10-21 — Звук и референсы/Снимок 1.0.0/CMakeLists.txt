cmake_minimum_required(VERSION 3.22)

project(PowerBass VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")

option(POWERBASS_COPY_AFTER_BUILD "Install plug-ins into the user plug-in folders after build" OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
  add_subdirectory(third_party/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                            GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
  FetchContent_MakeAvailable(JUCE)
endif()

# Cross-compiling for Windows: the manifest helper needs an emulator (Wine) to run here.
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  set(POWER_VST3_AUTO_MANIFEST FALSE)
else()
  set(POWER_VST3_AUTO_MANIFEST TRUE)
endif()

set(POWERBASS_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND POWERBASS_FORMATS AU)
endif()

juce_add_plugin(PowerBass
  COMPANY_NAME "MyDarknessDays"
  COMPANY_WEBSITE ""
  BUNDLE_ID "com.mydarknessdays.powerbass"
  PLUGIN_MANUFACTURER_CODE Mddy
  PLUGIN_CODE Pwbs
  PRODUCT_NAME "PowerBass"
  FORMATS ${POWERBASS_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_AUTO_MANIFEST ${POWER_VST3_AUTO_MANIFEST}
  COPY_PLUGIN_AFTER_BUILD ${POWERBASS_COPY_AFTER_BUILD})

set(POWERBASS_CORE_SOURCES
  Source/Params.cpp
  Source/Presets.cpp
)

target_sources(PowerBass PRIVATE
  ${POWERBASS_CORE_SOURCES}
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(PowerBass PRIVATE Source)
target_compile_definitions(PowerBass PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(PowerBass
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless DSP/preset tests: the same engine code, no plug-in wrapper.
juce_add_console_app(powerbass_tests PRODUCT_NAME "PowerBass tests")
target_sources(powerbass_tests PRIVATE ${POWERBASS_CORE_SOURCES} Tests/PowerBassTests.cpp)
target_include_directories(powerbass_tests PRIVATE Source)
target_compile_definitions(powerbass_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(powerbass_tests
  PRIVATE juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME powerbass_tests COMMAND powerbass_tests)
