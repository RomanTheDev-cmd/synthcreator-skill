# Fixes to JUCE 8.0.12 (commit 29396c22) that upstream does not have. They are applied to the JUCE sources before
# use, to third_party/JUCE and to a fetched copy alike; running this again changes nothing.
#   include()d with JUCE_PATCH_DIR set, or: cmake -P patch_juce.cmake [JUCE dir, default: the current directory,
#   which is the source directory when FetchContent runs it as PATCH_COMMAND]
#
# 1. Windows: VBlankThread::run() looked at its exit flag only after IDXGIOutput::WaitForVBlank() succeeded. Where
#    that call keeps failing (Wine 11 has it as a stub, so Linux users through Wine/yabridge; outputs without a
#    vertical blank) closing the plug-in window waited for the thread for ever. Found 2026-10-23 with
#    Tools/host_check under Wine: both plug-ins hung in ~VBlankThread when the editor was deleted.
if(NOT JUCE_PATCH_DIR)
  if(CMAKE_ARGV3)
    set(JUCE_PATCH_DIR "${CMAKE_ARGV3}")
  else()
    set(JUCE_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
set(_f "${JUCE_PATCH_DIR}/modules/juce_gui_basics/native/juce_VBlank_windows.cpp")
file(READ "${_f}" _text)
string(FIND "${_text}" "patch_juce.cmake" _done)
if(_done EQUAL -1)
  # file(READ) drops the CRs of CRLF lines (CMake 4.4); the file keeps its own line endings when written back.
  file(SIZE "${_f}" _bytes)
  string(LENGTH "${_text}" _chars)
  set(_n "\n")
  set(_old "            else${_n}            {${_n}                sleep (1);${_n}            }${_n}")
  set(_new "            else${_n}            {${_n}                // WaitForVBlank() failing for good must not keep the thread alive (patch_juce.cmake)${_n}                if ((state.load() & flagExit) != 0)${_n}                    return;${_n}${_n}                sleep (1);${_n}            }${_n}")
  string(FIND "${_text}" "${_old}" _at)
  if(_at EQUAL -1)
    message(FATAL_ERROR "patch_juce.cmake: VBlankThread::run() is not as expected in ${_f}; check the fix by hand")
  endif()
  string(REPLACE "${_old}" "${_new}" _text "${_text}")
  if(NOT _bytes EQUAL _chars)
    string(REPLACE "\n" "\r\n" _text "${_text}")
  endif()
  file(WRITE "${_f}" "${_text}")
  message(STATUS "JUCE patched: VBlankThread exits when WaitForVBlank() keeps failing")
endif()
