# host_check: loads a VST3 plug-in through the VST3 interface, as a DAW does, and checks it (instance,
# audio from MIDI notes, state round trip, editor). Built for Windows with cmake/windows-clang-msvc.cmake
# and run under Wine to check the shipped Windows builds (there is no Windows machine here).
#   cmake -S Tools/host_check -B <dir> [-DCMAKE_TOOLCHAIN_FILE=.../windows-clang-msvc.cmake] -DJUCE_DIR=<third_party/JUCE>
cmake_minimum_required(VERSION 3.22)
project(HostCheck VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../third_party/JUCE" CACHE PATH "JUCE source")
set(JUCE_PATCH_DIR "${JUCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/patch_juce.cmake")  # the host's own windows close too
add_subdirectory("${JUCE_DIR}" JUCE)
juce_add_console_app(host_check PRODUCT_NAME "host_check")
target_sources(host_check PRIVATE main.cpp)
target_compile_definitions(host_check PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0
                           JUCE_PLUGINHOST_LV2=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(host_check PRIVATE juce::juce_audio_processors juce::juce_gui_basics
                      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
