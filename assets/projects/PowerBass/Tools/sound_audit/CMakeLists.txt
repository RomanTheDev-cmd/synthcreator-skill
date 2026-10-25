# sound_audit_host: hosts an installed AU or VST3 the way a DAW does and renders scripted MIDI through it, for the
# independent sound audit (audit.py). Not part of the plug-in build.
#   cmake -S Tools/sound_audit -B <dir> -DCMAKE_BUILD_TYPE=Release && cmake --build <dir>
cmake_minimum_required(VERSION 3.22)
project(SoundAuditHost VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../third_party/JUCE" CACHE PATH "JUCE source")
set(JUCE_PATCH_DIR "${JUCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/patch_juce.cmake")
add_subdirectory("${JUCE_DIR}" JUCE)
juce_add_console_app(sound_audit_host PRODUCT_NAME "sound_audit_host")
target_sources(sound_audit_host PRIVATE main.cpp)
target_compile_definitions(sound_audit_host PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_LADSPA=0
                           JUCE_PLUGINHOST_LV2=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(sound_audit_host PRIVATE juce::juce_audio_processors juce::juce_audio_utils
                      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
