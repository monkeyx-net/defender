# The main CPU's ROM map in MAME against `defender --dump-rom`.
#   cmake -DMAME=... -DROMPATH=<dir holding defender.zip> -DGAME=defender -DROM=<set>
#         -DWORKDIR=dir -DPROBE=rom_map_probe.lua -P rom_map.cmake
foreach(v MAME ROMPATH GAME ROM WORKDIR PROBE)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "rom_map.cmake: ${v} not set")
  endif()
endforeach()

file(MAKE_DIRECTORY "${WORKDIR}")
set(mame_out "${WORKDIR}/rom_map_mame.txt")
set(ours "${WORKDIR}/rom_map_ours.txt")
file(REMOVE "${mame_out}" "${ours}")
set(ENV{PROBE_OUT} "${mame_out}")
execute_process(
  COMMAND "${MAME}" defender -rompath "${ROMPATH}"
    -nvram_directory "${WORKDIR}/nvram" -cfg_directory "${WORKDIR}/cfg" -snapshot_directory "${WORKDIR}/snap"
    -video none -sound none -nothrottle -noplugin data -skip_gameinfo -seconds_to_run 60
    -autoboot_script "${PROBE}"
  RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0 OR NOT EXISTS "${mame_out}")
  message(FATAL_ERROR "MAME exited with ${rc} and no probe output")
endif()

execute_process(COMMAND "${GAME}" --dump-rom "${ROM}" C000 FFFF OUTPUT_FILE "${ours}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "defender --dump-rom exited with ${rc}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${ours}" "${mame_out}" RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
  message(FATAL_ERROR "ROM map differs from MAME's: ${ours} vs ${mame_out}")
endif()
message(STATUS "ROM map matches MAME (12 KB fixed, 7 banks of 4 KB)")
