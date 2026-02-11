# WavSlicerChiptune

Audio slicer and Furnace Tracker `.fur` file generator for chiptune sample production. Slices audio files into timed segments based on BPM, then generates ready-to-use Furnace modules with one instrument per sample.

## Features

- Slices audio into equal segments based on BPM, rows per beat, and pattern length
- Supports DEC and HEX file naming modes
- Generates binary Furnace `.fur` files (v228, Generic PCM DAC)
- Each sample gets its own instrument (persists through pause, unlike drum kits)
- Tkinter GUI with slicer and fur generator tabs
- Native Win32 GUI for the slicer (Windows)
- Compatible with Windows and Linux

## Prerequisites

- **ffmpeg** and **ffprobe** on PATH (used by slicer for audio splitting)
- **zlib** development headers (for fur_gen compilation)
- **Python 3** with tkinter (for GUI)

## Build

### Linux
```sh
gcc source/slicer.c -o slicer -lm
gcc source/fur_gen.c -o fur_gen -lm -lz
```

### Windows (MSYS2/MinGW)
```sh
gcc source/slicer.c -o slicer.exe -lm
gcc source/fur_gen.c -o fur_gen.exe -lm -lz
gcc source/slicerGUI_win32.c -o slicerGUI_win32.exe -lcomctl32 -mwindows -fgnu89-inline
```

### PyInstaller Bundle
```sh
pyinstaller WavSlicerChiptune.spec
```

## Usage

### Slicer
```sh
./slicer <file> <bpm> <rows_per_beat> <pattern_rows> <DEC|HEX> <output_folder> <slice_prefix>
```
Example:
```sh
./slicer mysong.wav 139 4 128 DEC output/ slice
```

### Fur Generator
```sh
./fur_gen <input_dir> <bpm> <speed> <pattern_length> <output.fur>
```
Example:
```sh
./fur_gen output/ 139 4 128 mysong.fur
```

### GUI
```sh
python slicer_gui.py
```
Requires compiled `slicer` and `fur_gen` binaries in the same directory.

## License

Distributed under the Unlicense.
