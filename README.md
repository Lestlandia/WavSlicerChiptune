# WavSlicerChiptune

WavSlicerChiptune is an audio slicer that uses ffprobe and ffmpeg to split audio files into segments. These slices can be used as samples in Furnace Tracker chiptune projects.

## Features
- Splits audio into equal slices
- Supports DEC and HEX naming modes
- Compatible with Windows and Linux

## Build & Usage

### Linux
Compile with:
```sh
gcc source/slicer.c -o slicer -lm
```
Run with:
```sh
./slicer <FILENAME> <BPM> <rows_per_beat> <pattern_rows> <naming_mode>
```
Example: 
```sh
./slicer mysong.wav 120 4 64 DEC|HEX
```

### Windows
Compile using your configured tasks. Run slicer.exe with the appropriate parameters.

## License
Distributed under the Unlicense.

