# favia (alpha)

favia is a Fast Video App for Linux based on ffmpeg and SDL.


## Features & limitations

* Play video files with hardware decoding (VAAPI/Linux); limited range of supported formats, no subtitles


## Install

```sh
cd /tmp
wget https://github.com/stsaz/favia/releases/download/v0.1-alpha1/favia-0.1-alpha1-linux-amd64.tar.zst
mkdir -p ~/bin
cd ~/bin
tar xf favia-*-linux-amd64.tar.zst
ln -s favia-0/favia favia
```


## Usage Examples

```sh
# Play 4 video files in parallel
favia -zoom 50 -mute -parallel 4 *.mp4
```


## Control Keys

| Key | Action |
| --- | --- |
| `q`          | Quit |
| `n/p`        | Next/Previous input file |
| `Space`      | Pause/Resume |
| `[Ctrl+]Left/Right` | Seek |
| `Shift+Del`  | Move source file to Trash |
| Video:       | |
| `+/- or Ctrl+MouseWheel` | Zoom |
| `f`          | Fullscreen |
| `Ctrl+(+/-)` | Add/remove window |
| `Tab`        | Cycle through windows |
| Audio:       | |
| `Up/Down or MouseWheel` | Volume |
| `m`          | Mute |
| `a`          | Switch audio track |


## Build for Linux

```sh
mkdir -p favia-src
cd favia-src
git clone https://github.com/stsaz/ffbase
git clone https://github.com/stsaz/ffsys
git clone https://github.com/stsaz/ffaudio
git clone https://github.com/stsaz/favia
cd favia
bash xbuild-debianBW.sh
# ./_linux-amd64/favia-0/favia file.mp4
```
