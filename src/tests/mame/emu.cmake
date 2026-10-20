# The emulated board against MAME: runs after mame_capture and replays its
# scripted run from the same primed CMOS. Checked (test_emu):
#   - instruction traces of MAME's frames TRACE_FROM-TRACE_TO, taken here:
#     the main CPU's cycle for cycle, the sound CPU's instruction for instruction
#   - at every captured frame, RAM and the picture (see test_emu.c)
#   cmake -DMAME=... -DROMPATH=... -DWORKDIR=... -DROM=... -DPROBE=trace_probe.lua -DCHECK=test_emu -P emu.cmake
foreach(v MAME ROMPATH WORKDIR ROM PROBE CHECK)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "emu.cmake: ${v} not set")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/script.cmake")
foreach(f ${capture_frames})
  if(NOT EXISTS "${WORKDIR}/capture/frame_${f}.txt" OR NOT EXISTS "${WORKDIR}/capture/${f}.png")
    message(FATAL_ERROR "${WORKDIR}/capture: frame ${f} missing: run mame_capture first")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/mame_common.cmake")

# a minute into the game: flying, shooting, a smart bomb and a hyperspace jump
set(trace_from 1600)
set(trace_to 1700)
set(traces "${WORKDIR}/trace")
file(REMOVE_RECURSE "${traces}")
file(MAKE_DIRECTORY "${traces}")
set(ENV{PROBE_OUT} "${traces}")
set(ENV{TRACE_FROM} ${trace_from})
set(ENV{TRACE_TO} ${trace_to})
set(ENV{PROBE_SCRIPT} "${capture_script}")
execute_process(COMMAND "${MAME}" ${mame_args} -debug -debugger none -seconds_to_run 300 -autoboot_script "${PROBE}"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0 OR NOT EXISTS "${traces}/main.tr" OR NOT EXISTS "${traces}/sound.tr")
  message(FATAL_ERROR "MAME exited with ${rc} and no traces")
endif()

execute_process(COMMAND "${CHECK}" "${ROM}" "${primed}/defender/nvram" "${capture_script}"
                  "${WORKDIR}/capture" "${traces}" ${trace_from} ${trace_to} ${capture_frames}
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the emulated board differs from MAME")
endif()
