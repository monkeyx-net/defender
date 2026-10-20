# The scripted run the MAME tests capture and test_emu replays (inputs in
# capture_probe.lua's PROBE_SCRIPT format): a coin, a 1-player start, then
# a minute of play cycling through every control.
set(capture_script "1200:coin 1215: 1300:start 1315:")
set(moves "thrust,fire" "up,thrust" "fire" "down,fire" "reverse" "thrust,up" "fire,down" "bomb" "thrust" "hyper"
    "thrust,fire" "up")
set(f 1400)
set(k 0)
while(f LESS 5000)
  math(EXPR m "${k} % 12")
  list(GET moves ${m} move)
  # hold each move a while (the smart bomb and hyperspace briefly), then let go
  if(move STREQUAL "bomb" OR move STREQUAL "hyper" OR move STREQUAL "reverse")
    set(hold 4)
  else()
    math(EXPR hold "20 + (${k} * 7) % 25")
  endif()
  math(EXPR off "${f} + ${hold}")
  string(APPEND capture_script " ${f}:${move} ${off}:")
  math(EXPR f "${off} + 6")
  math(EXPR k "${k} + 1")
endwhile()
# frames dumped and snapshotted; static_frames are those with nothing moving
# during scan-out (the attract mode's text pages)
set(capture_frames 700 1000 1500 1650 1800 2100 2400 2700 3000 3600 4200 4800)
set(static_frames 700 1000)
