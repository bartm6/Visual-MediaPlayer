# Visual MediaPlayer

A lightweight native Windows media library and player for **videos, images, and VR media**.

Built with **C++17**, **Win32**, **Direct3D 11**, and **Windows Media Foundation**.

**Current version: 12.5.5**

## Download

### Installer

Download:

```text
VisualMediaPlayerSetup.exe
```

The installer adds Visual MediaPlayer to Windows, creates a Start Menu shortcut, enables **Open with Visual MediaPlayer**, and includes an uninstaller.

### Portable

Download:

```text
VisualMediaPlayer.exe
```

No installation is required. Run the EXE directly.

> Windows SmartScreen may show a warning for unsigned builds.

## Features

* Local video and image library
* Folder-based browsing
* Fast thumbnails and cached previews
* Timeline preview images
* Click a timeline preview to start playback from that point
* Search within the currently opened folder
* Native Direct3D 11 video rendering
* VR180 and 360° playback
* Automatic stereo VR detection
* Mouse-controlled VR viewing
* Timeline seeking
* Volume control
* Auto Next
* Fullscreen playback
* Image slideshow
* Adjustable library and timeline preview sizes with `Ctrl + Mouse Wheel`
* Windows **Open with Visual MediaPlayer** support
* Opens media directly from Windows Explorer
* Remembers the selected library folder
* Custom filename sorting
* Background timeline generation with **Loading Timeline** status

## Video Controls

| Control          | Action                            |
| ---------------- | --------------------------------- |
| **Play / Pause** | Start or pause playback           |
| **Timeline**     | Click or drag to seek             |
| **Volume**       | Click or drag to change volume    |
| **Auto Next**    | Automatically play the next video |
| **180° / 360°**  | Switch VR viewing mode            |
| **Fullscreen**   | Enter fullscreen playback         |

Newly opened videos start at **30% volume**.

## Keyboard Controls

| Key           | Action                             |
| ------------- | ---------------------------------- |
| `Space`       | Play / Pause                       |
| `Left Arrow`  | Previous media                     |
| `Right Arrow` | Next media                         |
| `F11`         | Fullscreen                         |
| `Esc`         | Exit fullscreen, player, or search |

## VR Playback

Visual MediaPlayer supports:

* VR180
* 360° video
* Side-by-side stereo
* Top/bottom stereo
* Automatic stereo detection
* Single-eye rendering for stereo media

Detected stereo VR starts in **180° mode** by default.

Use the **180° / 360°** button to switch viewing modes.

Click and drag the video to look around.

## Images

Images can be opened directly from the library.

The slideshow:

* Starts from the currently opened image
* Continues through the current folder
* Changes image every **3 seconds**

## Search

Start typing while viewing a folder to search its contents.

* `Backspace` removes characters
* `Enter` opens the first result
* `Esc` closes the search

Changing folders automatically clears the current search.

## Open With Windows Explorer

Visual MediaPlayer can open supported media directly from Windows Explorer.

1. Right-click a media file.
2. Choose **Open with**.
3. Select **Visual MediaPlayer**.
4. Choose **Always** if you want it as the default application.

Videos open directly into playback.

## Cache & Privacy

Visual MediaPlayer is designed for **local media**.

Generated thumbnails and timeline previews are stored inside:

```text
.visualmediaplayer-cache
```

Application settings are stored in:

```text
%LOCALAPPDATA%\VisualMediaPlayer\
```

Original media files are **not modified**.

Visual MediaPlayer does not require:

* An account
* A browser
* A local server
* Python
* FFmpeg at runtime

## Requirements

* 64-bit Windows 10 or Windows 11
* Direct3D 11 capable graphics hardware
* Windows Media Foundation
* Compatible Windows codecs

## Building From Source

Build configuration:

* **x64**
* **Release**
* **C++17**
* **MSVC**
* **Visual Studio Platform Toolset v145**

### Portable

Run:

```text
Build.bat
```

Output:

```text
VisualMediaPlayer.exe
```

### Installer

Run:

```text
BuildInstaller.bat
```

Output:

```text
VisualMediaPlayerSetup.exe
```

## Technology

* C++17
* Win32
* Direct3D 11
* Windows Media Foundation
* GDI+
* Windows Shell APIs

---

**Visual MediaPlayer**
