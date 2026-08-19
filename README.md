# Visual MediaPlayer

**Visual MediaPlayer** is a native Windows media library and player for local **videos, images, and VR media**.

It is designed for fast visual browsing, simple keyboard and mouse navigation, native-resolution viewing, and a compact interface without unnecessary media-center features.

**Platform:** 64-bit Windows 10 / Windows 11

---

## Features

- Browse local video and image folders visually
- Fast thumbnail-based library
- Video Info view with timeline previews
- Search within the current folder
- Search filters for **VR**, **4K**, **5K**, and **8K**
- Previous/next navigation that respects active search results
- Normal video and image zoom with mouse-position anchoring
- Click-drag panning while zoomed
- **Native Size** viewing for normal videos and images
- **VR180 / 360°** playback with mouse-look and FOV zoom
- Auto Next for videos
- Image slideshow
- Fullscreen Library, Info, and Player views
- Resolution and VR badges
- Broad video and image format recognition
- In-app notices for unavailable folders and unsupported media
- Automatic reopening of a saved library when it becomes available again
- Windows **Open with Visual MediaPlayer** integration
- Portable and Installer builds
- Original media files are never modified

---

## Quick Start

1. Start **Visual MediaPlayer**.
2. Click the **Folder** button and choose your media library.
3. Switch between **Videos** and **Images** as needed.
4. Click a media card to open its Info view.
5. For video, click **Play** or press `Space`.
6. For images, press `Space` to start or stop the slideshow.

The selected library is remembered between launches.

---

# Controls

## Global

| Input | Action |
|---|---|
| `F11` | Toggle fullscreen |
| `Esc` | Context-sensitive Back / reset |
| Drag title bar | Move the app window |

`Esc` performs the most useful action for the current view. It does not leave fullscreen if there is another navigation or reset action to perform first.

---

## Library

| Input | Action |
|---|---|
| Type normally | Search the current folder |
| `Ctrl + F` | Open/focus search |
| `Ctrl + A` | Select all search text |
| `Backspace` | Delete search characters |
| `Enter` | Open the first search result |
| `Esc` | Close search, go up a folder, or leave fullscreen at the Library root |
| Mouse Wheel | Scroll |
| `Ctrl + Mouse Wheel` | Resize media cards |
| Click media | Open Info view |

When media is opened from a search, previous/next navigation remains inside that search result set. Returning to the Library keeps the search active and highlights the media you came from.

---

## Video Info

| Input | Action |
|---|---|
| `Space` | Play video |
| `Left Arrow` | Previous video |
| `Right Arrow` | Next video |
| Mouse Wheel | Scroll the Info view |
| `Ctrl + Mouse Wheel` | Resize timeline previews |
| Click timeline preview | Start playback from that point |
| `Esc` | Return to Library |
| `F11` | Toggle fullscreen |

---

## Image Info

| Input | Action |
|---|---|
| `Space` | Start / stop slideshow |
| `Left Arrow` | Previous image |
| `Right Arrow` | Next image |
| Mouse Wheel | Zoom toward/away from the mouse position |
| Left-click + drag while zoomed | Pan image |
| First `Esc` while zoomed | Reset to fit-to-window |
| Next `Esc` | Return to Library |
| `F11` | Toggle fullscreen |

Free zoom and panning are disabled while **Native Size** is active.

---

## Normal Video Player

| Input | Action |
|---|---|
| `Space` | Play / Pause |
| `Left Arrow` | Skip backward 30 seconds |
| `Right Arrow` | Skip forward 30 seconds |
| Mouse Wheel | Zoom toward/away from the mouse position |
| Left-click + drag while zoomed | Pan video |
| First `Esc` while zoomed | Reset to fit-to-window |
| Next `Esc` | Return to Info |
| `F11` | Toggle fullscreen |

Use the large side-arrow buttons for previous/next **video** navigation.

The 30-second keyboard seek does not force the player controls to appear.

---

## VR Player

| Input | Action |
|---|---|
| Left-click + drag | Look around |
| Mouse Wheel | Change field of view |
| `Space` | Play / Pause |
| `Left Arrow` | Skip backward 30 seconds |
| `Right Arrow` | Skip forward 30 seconds |
| `Esc` | Return to Info |
| `F11` | Toggle fullscreen |
| **180° / 360°** button | Change VR projection mode |

VR uses its own mouse-look and zoom system. Normal flat-media zoom and Native Size do not affect VR playback.

---

# Search

Search works inside the folder currently being viewed. Simply start typing in the Library.

Special filters can be used on their own or together with normal text:

| Filter | Matches |
|---|---|
| `VR` | VR videos |
| `4K` | 4K, 5K, and 8K videos |
| `5K` | 5K and 8K videos |
| `8K` | 8K videos |

Examples:

```text
holiday 4k
concert vr
vacation 8k
```

Resolution information is cached after it has been determined, so unchanged files do not need to be checked again on every launch.

---

# Native Size

Native Size is available for:

- Normal/non-VR videos
- Images

When enabled, the app attempts to display the media at **1 source pixel = 1 screen pixel**.

### Windowed

The application window resizes and stays centered on the active monitor. If the media is larger than the display, it is reduced only enough to fit.

### Fullscreen

The app remains fullscreen, while the media itself stays at its native pixel size and is centered on screen.

Turning Native Size off returns the application to its normal window size. Leaving the media session also resets Native Size for the next use.

If Auto Next or slideshow is active, Native Size is reapplied to each new normal video or image.

---

# Auto Next, Slideshow & Volume

## Auto Next

Auto Next plays the next video when the current one finishes.

If a video has already ended and Auto Next is then enabled, the player advances immediately.

Auto Next is reset to **Off** when leaving the Player.

## Volume

A new video playback session starts at **30% volume**.

If you change the volume while Auto Next is active, that volume is carried into the next automatically played video. Leaving the Player resets the next session to 30%.

## Image Slideshow

The slideshow advances through images in the current folder and can be started or stopped with `Space`.

Slideshow state is reset when leaving the image session.

---

# VR Media

Visual MediaPlayer supports common VR layouts including:

- VR180
- 360° video
- Side-by-side stereo (SBS / LR)
- Top/bottom stereo (TB / OU)
- Mono panoramic VR

Common filename markers such as `VR`, `VR180`, `180VR`, `360`, `SBS`, `LR`, `TB`, and `OU` are used to help identify VR media and stereo layout.

Stereo-packed VR opens in the normal 180° view. Use the player control to switch between 180° and 360°.

---

# Library Cache & Load Everything

Visual MediaPlayer generates thumbnails, Info banners, timeline previews, and small metadata files to make future browsing faster.

Generated data is stored in:

```text
.visualmediaplayer-cache
```

Original media files are never modified.

**Load Everything** can be used from the selected library root to pre-generate missing preview data for the library. Existing healthy cache data is reused.

---

# Folder Availability

If the selected library becomes unavailable, Visual MediaPlayer unloads the inaccessible library contents and shows:

> **This folder is unavailable.**

The normal empty-library prompt remains available so another folder can be selected.

If the saved library becomes available again, Visual MediaPlayer automatically reopens it.

---

# Unsupported Media

Visual MediaPlayer recognizes a broad range of media file extensions, but actual decoding depends on the codecs and imaging components available on the Windows system.

If a recognized file cannot be decoded, the app shows:

> **This media is unsupported.**

The notice is non-blocking and disappears automatically.

---

# Media Formats

Common recognized video formats include:

`MP4`, `M4V`, `MKV`, `WEBM`, `AVI`, `MOV`, `WMV`, `ASF`, `MPG`, `MPEG`, `TS`, `M2TS`, `VOB`, `OGV`, `FLV`, `F4V`, `3GP`, `RM`, `RMVB`, `MXF`, and many related/legacy formats.

Common recognized image formats include:

`JPG`, `JPEG`, `PNG`, `APNG`, `BMP`, `GIF`, `TIFF`, `WEBP`, `HEIC`, `HEIF`, `AVIF`, `JXL`, `JPEG 2000`, `TGA`, `DDS`, `PSD`, `EXR`, `HDR`, `SVG`, and many camera RAW formats.

Video playback uses **Windows Media Foundation**. Images use **GDI+** with a **Windows Imaging Component (WIC)** fallback.

A recognized extension does not guarantee that every codec stored inside that file can be decoded on every Windows installation.

---

# Fullscreen

Fullscreen is available in the Library, Info view, and Player.

- `F11` toggles fullscreen.
- `Esc` first performs the current view's Back/reset action.
- Returning Player → Info → Library can therefore remain fullscreen throughout.
- At the top Library, when there is nothing else for `Esc` to close or navigate, `Esc` leaves fullscreen.

---

# Windows Explorer Integration

The Installer build registers Visual MediaPlayer with Windows so supported media can be opened through **Open with**.

1. Right-click a media file.
2. Choose **Open with**.
3. Select **Visual MediaPlayer**.

---

# Installer & Portable Builds

## Installer

The Installer build can:

- Install Visual MediaPlayer
- Create a Start Menu shortcut
- Register Open With support
- Add the app to Installed Apps
- Install an uninstaller

The uninstaller removes the application, shortcuts, registration, and app settings. Generated media cache cleanup remains optional.

## Portable

The Portable build requires no installation. Run:

```text
VisualMediaPlayer.exe
```

---

# Requirements

- 64-bit Windows 10 or Windows 11
- Direct3D 11-capable graphics hardware
- Windows Media Foundation
- GDI+ / Windows Imaging Component
- Compatible Windows codecs for the media being played

No account, browser, cloud service, or internet connection is required for normal local playback.

---

# Building From Source

Visual MediaPlayer is a native **C++17 x64** Windows application.

### Portable

Run:

```text
Portable\Build.bat
```

### Installer

Run:

```text
Installer\BuildInstaller.bat
```

Recommended build environment:

- Visual Studio / MSVC
- Windows SDK
- C++17
- x64 Release configuration

---

## Technology

- C++17
- Win32 API
- Direct3D 11 / DXGI
- Windows Media Foundation
- GDI+
- Windows Imaging Component (WIC)
- Windows Shell APIs

---

**Visual MediaPlayer**
