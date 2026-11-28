# favia

![](res/favia.svg)

favia is a fast portable video player (and image viewer) with multi-window capabilities for Linux & Windows based on ffmpeg and SDL.

*Note: beta version*


## Features & limitations

* Play video files with hardware decoding (VAAPI/CUDA on Linux, D3D11 on Windows); limited range of supported formats, no subtitles
* View images (jpg, png)
* View multiple files in separate windows


## Install

### Linux

Install the package from [Releases](https://github.com/stsaz/favia/releases):

```sh
# Debian/Ubuntu
sudo dpkg -i favia_*.deb

# Fedora/RHEL
sudo rpm -i favia-*.rpm
```

Portable package:

```sh
cd /tmp
wget https://github.com/stsaz/favia/releases/download/.../favia-...-linux-amd64.tar.zst
mkdir -p ~/bin
cd ~/bin
tar xf favia-*-linux-amd64.tar.zst
ln -s favia-0/favia favia
```

### Windows

TODO


## Usage Examples

```sh
# Play all files from current directory
favia .

# Play 4 video files in parallel
favia -zoom 50 -mute -parallel 4 *.mp4
```


## Control Keys

| Key | Action |
| --- | --- |
| `q`          | Quit |
| File ops:    | |
| `n/p`        | Next/Previous input file |
| `PgDn/PgUp`  | Next/Previous input file (by 10) |
| `Home/End`   | First/Last input file |
| `Shift+Del`  | Move source file to Trash |
| `F[1-3]`     | Move source file to a directory |
| Video:       | |
| `Space`      | Pause/Resume |
| `[Ctrl+/Ctrl+Shift+]Left/Right` | Seek |
| `+/- or Ctrl+MouseWheel` | Zoom |
| `f`          | Fullscreen |
| Audio:       | |
| `Up/Down or MouseWheel` | Volume |
| `m`          | Mute |
| `a`          | Switch audio track |
| Windows:     | |
| `Ctrl+(+/-)` | Add/remove window |
| `Tab`        | Cycle through windows |


## Build

```sh
mkdir -p favia-src
cd favia-src
git clone https://github.com/stsaz/ffbase
git clone https://github.com/stsaz/ffsys
git clone https://github.com/stsaz/ffaudio
git clone https://github.com/stsaz/favia
cd favia
```

Linux:

```sh
bash xbuild.sh
# ./_linux-amd64/favia-0/favia file.mp4
```

For Windows:

```sh
...
OS=windows bash xbuild.sh
```

## External Libraries

favia uses these third-party libraries:
[FFmpeg](https://github.com/FFmpeg/FFmpeg),
[SDL](https://github.com/libsdl-org/SDL).

Additionally:
* [ffbase](https://github.com/stsaz/ffbase) — base algorithms
* [ffsys](https://github.com/stsaz/ffsys) — cross-platform system interface
* [ffaudio](https://github.com/stsaz/ffaudio) — cross-platform audio I/O


## Bug report

If you encounter a bug, please report it via [favia GitHub Issues](https://github.com/stsaz/favia/issues).
When filing a bug report, please provide as much information as possible.

Debug output may help — add `-Debug` when executing:

```sh
favia -Debug file.mp4
```


## Architecture

	favia -> Core       -------> libffmpeg
	(exe)    (libfavia) -> vo -> libSDL
	                    -> ao -> libpulse

## License

favia is licensed under BSD-2.
Please review the licenses of all third-party libraries before any commercial use.
