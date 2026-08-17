# Visual MediaPlayer

A lightweight native Windows media library and player focused on local video, images, and immersive VR playback.

Visual MediaPlayer is built with **C++**, **Win32**, **Direct3D 11**, and **Windows Media Foundation**. It runs as a standalone Windows application with no Python, browser, local server, or FFmpeg dependency required at runtime.

## Download

For normal use, download the latest:

**`VisualMediaPlayer.exe`**

from the repository's **Releases** page.

No installation is required. Run the EXE directly.

> Windows SmartScreen may show a warning for unsigned builds. This is expected until the application is digitally code-signed.

## Features

- Local video and image library
- Folder-based browsing
- Fast thumbnail and preview generation
- Search limited to the currently opened folder
- Video information screen with timeline preview images
- Clickable secondary previews that start playback at that timestamp
- Native Direct3D 11 video rendering
- VR180 and 360° playback
- Automatic stereo VR detection
- One-eye rendering for stereo VR media
- 180° front-only VR mode with optional 360° backside toggle
- Mouse-controlled VR viewing
- Timeline seeking with press/drag timestamp indication
- Volume slider with press/drag percentage indication
- Auto Next playback
- Fullscreen playback
- Image slideshow with a 3-second interval
- Slideshow can start from the currently opened image
- Adjustable library and secondary-preview sizes with `Ctrl + Mouse Wheel`
- Windows **Open with Visual MediaPlayer** support
- Media opened from Explorer can launch directly into playback
- Remembers the selected library folder

## Privacy

Visual MediaPlayer is designed for local media.

Generated thumbnails and preview images are stored inside the selected media folder under:

```text
.visualmediaplayer-cache
```

The application does not intentionally store generated media previews in its settings folder.

Application settings such as the selected library path are stored separately in the user's local Windows application-data folder.

Original media files are not modified.

## Requirements

- 64-bit Windows 10 or Windows 11
- Direct3D 11 capable graphics hardware
- Media format support available through Windows Media Foundation / installed Windows codecs

## Using Visual MediaPlayer

### Library

Open Visual MediaPlayer and choose a media folder.

Use:

- **Videos / Images** to switch media type
- **Choose folder** to select another library
- **Rescan** to refresh the current folder
- **Ctrl + Mouse Wheel** to resize library cards
- Start typing to search the current folder
- **Esc** to close the search
- Leaving the current folder automatically cancels its search

### Video playback

Open a video from the library and select **Play video**.

Player controls include:

- Back
- VR 180° / 360° toggle when applicable
- Timeline
- Play / Pause
- Volume
- Auto Next
- Fullscreen

Every newly opened video starts at **30% volume**.

### VR playback

Stereo-packed VR media is displayed using a single eye.

By default, detected stereo VR starts in **180° mode**:

- front 180° contains the video
- rear 180° is black

Use the **180° / 360°** button to enable or disable the backside view.

Mouse dragging controls the VR viewing direction.

### Images

Images can be opened individually from the library.

Use the slideshow button to begin a slideshow. When started from an opened image, the slideshow begins with that image and continues through the remaining images in the current folder.

The slideshow changes image every **3 seconds**.

## Open media from Windows Explorer

Visual MediaPlayer can be selected through Windows **Open with**.

After running the application once:

1. Right-click a supported media file.
2. Choose **Open with**.
3. Select **Visual MediaPlayer**.
4. Choose **Always** if you want it to become the default for that file type.

Double-clicking an associated video will then open Visual MediaPlayer and start playback directly.

## Building from source

Visual MediaPlayer is currently built with Microsoft Visual Studio and the MSVC C++ toolchain.

Recommended project layout:

```text
VisualMediaPlayer/
├── Build.bat
└── Source/
    ├── app.manifest
    ├── res/
    │   ├── resource.h
    │   ├── VisualMediaPlayer.ico
    │   └── VisualMediaPlayer.rc
    ├── src/
    │   └── main.cpp
    ├── VisualMediaPlayer.sln
    └── VisualMediaPlayer.vcxproj
```

Build configuration:

- Platform: **x64**
- Configuration: **Release**
- C++ standard: **C++17**
- Visual Studio Platform Toolset: **v145**

After compiling, distribute only:

```text
VisualMediaPlayer.exe
```

The source files and build tools are not required to run the finished application.

## Technology

- C++17
- Win32
- Direct3D 11
- Windows Media Foundation
- GDI+
- Windows Shell APIs

## License

A software license should be added before treating the repository as an open-source project.

See the repository `LICENSE` file once a license has been selected.

---

**Visual MediaPlayer**
