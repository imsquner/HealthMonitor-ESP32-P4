# Heart Test

This folder contains simulator modules for the teammate heart-rate algorithm.

Files:

- `heartrate.c` / `heartrate.h`: the same heart-rate event algorithm used by the teammate demo.
- `heart_signal_simulator.cpp` / `heart_signal_simulator.h`: synthetic MAX30105-like IR waveform generator.

There is intentionally no `.ino` file here. The project entry point should stay in the original
`sketch_apr26a.ino`; include these files from the main sketch when the simulator is needed.

Current integration:

- `ui_heart_test.cpp`: starts/stops simulated collection from `ui_Button1`.
- `heart_test_modules.cpp`: pulls these simulator sources into the Arduino sketch build.
- `sketch_apr26a.ino`: initializes the UI bridge and polls it from `loop()`.

Serial output flow:

- `0-3s`: no finger, IR stays below the detection threshold.
- `3-18s`: finger present, simulated target is 72 BPM.
- `18-30s`: target changes to 96 BPM.
- `30-42s`: target changes to 58 BPM.
- `42-47s`: finger removed.
- `47s+`: finger present again, target is 84 BPM.

The simulator does not generate SpO2. It only exercises the existing IR-based heart-rate path.
