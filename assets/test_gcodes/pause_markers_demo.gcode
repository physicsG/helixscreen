; HelixScreen fixture: scheduled-pause markers (prestonbrown/helixscreen#1509).
; A minimal but realistic sliced file — M73 P/R progress lines throughout, two
; ;LAYER_CHANGE-delimited layers, and TWO scheduled pauses the progress
; indicator must mark: an M600 filament change at roughly a third of the file
; and a PAUSE macro call at roughly two thirds. Used by live --test runs
; (--gcode-file assets/test_gcodes/pause_markers_demo.gcode); the scan itself
; is pinned by tests/unit/test_gcode_pause_scan.cpp.
; estimated printing time (normal mode) = 4 minutes
; filament used [mm] = 1200
M73 P0 R4
M140 S60
M104 S210
G21
G90
M82
G28
G1 Z0.2 F600
;LAYER_CHANGE
G1 X10 Y10 E1 F1800
G1 X50 Y10 E2
M73 P20 R3.2
G1 X50 Y50 E3
;LAYER_CHANGE
G1 Z0.4 F600
G1 X10 Y50 E4
M73 P45 R2.2
G1 X10 Y10 E5
M600
G1 X30 Y30 E6
;LAYER_CHANGE
G1 Z0.6 F600
G1 X50 Y50 E7
M73 P70 R1.2
G1 X10 Y10 E8
PAUSE
;@pause
G1 X30 Y30 E9
M73 P100 R0
M104 S0
M140 S0
G28
M84
