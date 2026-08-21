# Visual MediaPlayer

**Visual MediaPlayer** is a native Windows application for browsing and playing local **videos, images, and VR media**.

It is built around a visual thumbnail Library, a separate Info view, and a hardware-accelerated Player. The application does not modify the original media files.

**Supported platform:** 64-bit Windows 10 and Windows 11.

---

## Main Features

- Visual thumbnail Library for videos and images
- Folder navigation and in-Library search
- Favorite, VR, 4K, 5K, and 8K filtering
- Video Info view with timeline previews
- Image Info view with zoom and pan
- Hardware-accelerated video playback with Direct3D 11
- GPU-backed Library rendering with a GDI fallback
- Flat-video zoom and free repositioning
- VR180 and 360° playback with mouse-look and FOV control
- Native Size mode for flat videos and images
- Auto Next for videos
- Image slideshow
- Fullscreen Library, Info, and Player modes
- Previous/next navigation that respects the current search result set
- Resolution and VR badges
- Windows **Open with Visual MediaPlayer** integration
- Automatic handling of unavailable library drives/folders
- Portable and Installer builds
- Original media files are never changed

---

# Quick Start

1. Start **Visual MediaPlayer**.
2. Click the **Folder** button and select a media folder.
3. Choose **Videos** or **Images**.
4. Click a media card to open its Info view.
5. For a video, click **Play** or press `Space`.
6. For images, press `Space` to start or stop the slideshow.

The selected library is remembered between launches.

You can also open supported files from File Explorer with **Open with Visual MediaPlayer**. Opening another file reuses the existing application window. Opening multiple media files together creates a temporary mini-library for those files.

---

# Controls

## Global

| Input | Action |
|---|---|
| `F11` | Toggle fullscreen |
| `Esc` | Context-sensitive Back / reset |
| Drag title bar | Move the application window |

`Esc` handles the current view first. For example, it can reset video zoom before leaving Player, or close search before leaving the Library.

---

## Library

| Input | Action |
|---|---|
| Type | Search the current folder |
| `Ctrl + F` | Toggle Favorite for the media under the pointer |
| `Ctrl + A` | Select all search text |
| `Backspace` | Delete search characters |
| `Enter` | Open the first search result |
| `Esc` | Close search, go up one folder, or leave fullscreen at the Library root |
| Mouse Wheel | Scroll the Library |
| `Ctrl + Mouse Wheel` | Resize Library cards |
| Click media | Open Info view |

When media is opened from a search, previous/next navigation stays inside that filtered result set. Returning to the Library preserves the search and highlights the item you came from.

The Library also preserves its scroll position when entering and leaving Info/Player views.

---

## Video Info

| Input | Action |
|---|---|
| `Space` | Play video |
| `Left Arrow` | Previous video |
| `Right Arrow` | Next video |
| Mouse Wheel | Scroll the Info view |
| `Ctrl + Mouse Wheel` | Resize timeline preview cards |
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
| Mouse Wheel | Zoom around the mouse position |
| Left-click + drag while zoomed | Pan image |
| First `Esc` while zoomed | Reset image to fit-to-window |
| Next `Esc` | Return to Library |
| `F11` | Toggle fullscreen |

Free image zoom and pan are disabled while **Native Size** is enabled.

---

# Normal Video Player

| Input | Action |
|---|---|
| `Space` | Play / Pause |
| `Left Arrow` | Seek backward 30 seconds |
| `Right Arrow` | Seek forward 30 seconds |
| Mouse Wheel | Zoom around the mouse position |
| Left-click + drag on the video | Reposition / pan the video |
| First `Esc` after zoom/pan | Reset to centered fit-to-window |
| Next `Esc` | Return to Info |
| `F11` | Toggle fullscreen |

Use the large side-arrow controls to move to the previous or next video.

### Flat-video zoom and positioning

Flat video can be zoomed from **0.25× to 8×** relative to its fitted size.

Panning is not limited to zoomed-in video:

- When zoomed **above fit**, drag to inspect cropped parts of the video.
- When zoomed **below fit**, drag to position the smaller video within the available black surround.
- At fit size, movement is possible only on an axis that has unused letterbox/pillarbox space.
- A drag can start **only when the mouse-down occurs on actual rendered video pixels**.
- Dragging the black area surrounding the video does nothing.
- Once a valid drag starts, mouse capture keeps the drag continuous until the button is released.
- `Esc` resets both zoom and position to centered fit-to-window.

Free flat-video zoom and positioning are disabled while **Native Size** is enabled.

---

# VR Player

| Input | Action |
|---|---|
| Left-click + drag | Look around |
| Mouse Wheel | Change field of view |
| `Space` | Play / Pause |
| `Left Arrow` | Seek backward 30 seconds |
| `Right Arrow` | Seek forward 30 seconds |
| `Esc` | Return to Info |
| `F11` | Toggle fullscreen |
| **180° / 360°** control | Change VR projection mode |

VR playback uses a separate mouse-look/FOV system. Flat-video zoom, pan, and Native Size do not apply to VR playback.

Supported/detected VR layouts include:

- VR180
- 360° video
- Side-by-side stereo (`SBS` / `LR`)
- Top/bottom stereo (`TB` / `OU`)
- Mono panoramic video

### VR filename detection

VR classification is based on an explicit **`VR` filename marker**. The normal naming form is:

```text
name VR.mp4
```

Numbered variants such as `name VR (1).mp4` remain in the same VR name family when the Library sorts them.

Explicit 180° forms such as `VR180`, `180VR`, `VR 180`, and `180 VR` are also recognized. Stereo-layout markers such as `SBS`, `LR`, `TB`, and `OU` describe the packing/layout once the file has been identified as VR.

The old `360` filename suffix is **not** a VR-detection marker anymore. For example:

```text
name 360.mp4   -> treated as a normal video
name VR.mp4    -> treated as VR
```

This only changes **automatic filename detection**. The Player still supports both **180° and 360° projection modes**, and the 180° / 360° control continues to work normally. A `.360` file extension can still be recognized as a video container, but the filename must also contain a VR marker if it should automatically enter the VR path.

---

# Player Window Movement and Resizing

The video controls are separate overlay windows attached to the main player window.

During a window move:

- the control popup follows the main window continuously;
- previous/next overlays follow the window as well;
- the popup no longer remains at the old screen position and jump to the player after the move ends.

During interactive window resizing, the current video surface is kept stable while the frame is being dragged. The video child/swap-chain resize is deferred until the resize operation finishes. This reduces resize flicker and avoids repeatedly rebuilding the backbuffer for every intermediate mouse movement.

---

# Search

Search applies to the folder currently displayed in the Library. Start typing to search.

Favorited media also match the searchable term `favorite`, so text such as `fav` can locate favorites while ordinary filename matches continue to work.

Special filters:

| Filter | Matches |
|---|---|
| `VR` | VR videos |
| `4K` | 4K, 5K, and 8K videos |
| `5K` | 5K and 8K videos |
| `8K` | 8K videos |

Filters can be combined with normal text, for example:

```text
holiday 4k
concert vr
vacation 8k
```

Resolution metadata is cached after it has been determined for an unchanged file.

---

# Native Size

Native Size is available for:

- flat/non-VR videos;
- images.

Native Size means **1 source pixel = 1 screen pixel**.

## Windowed mode

When Native Size is enabled:

- the application is resized for the media's true native dimensions;
- the window cannot be resized below those native dimensions;
- the window may be enlarged beyond native size;
- enlarging the window does not upscale the media;
- media larger than the monitor is not silently reduced to fit the monitor.

## Fullscreen

The application remains fullscreen, but the media itself remains at native pixel size and is centered.

Turning Native Size off returns to normal fitted rendering. Leaving the media session resets Native Size for the next session.

Native Size disables free flat-video/image zoom and pan while it is active.

---

# Auto Next, Slideshow, and Volume

## Auto Next

Auto Next starts the next video when the current video finishes.

If Auto Next is enabled after a video has already reached its end, the next video starts immediately.

Auto Next is reset to **Off** when leaving Player.

## Volume

A new video playback session starts at **30% volume**.

When Auto Next is active, a manually changed volume carries into the next automatically played video. Leaving Player resets the next new session to 30%.

## Image Slideshow

Press `Space` in Image Info to start or stop the slideshow. The slideshow advances through images in the current folder and resets when leaving the image session.

---

# Library Rendering and Cache

Visual MediaPlayer generates reusable browsing data such as:

- Library thumbnails;
- Info banners;
- timeline previews;
- small metadata files.

Generated cache data is stored under:

```text
.visualmediaplayer-cache
```

The original media files are not modified.

## Rendering path

The Library uses a hardware **Direct2D** render target for normal windowed and fullscreen rendering. Library thumbnails are copied to temporary GPU bitmap resources as required. If hardware Library rendering cannot be created, the application retains a GDI fallback path.

The video Player itself uses **Direct3D 11 / DXGI**.

## Cache hierarchy

Library media follows this general path:

```text
disk thumbnail cache
        ↓
decoded RAM thumbnail cache
        ↓
temporary GPU thumbnail resources
```

GPU resources are working resources only; they are not written to disk.

The decoded RAM cache has a nominal soft budget around **640 MiB** and is trimmed under process-memory pressure. GPU thumbnail residency is also bounded and disposable.

When video playback starts, deep Library cache data can be reduced while recently useful Library content is retained so returning to the Library does not require a completely cold reload.

High-resolution and VR playback can use substantial **dedicated/shared GPU memory** for decoder surfaces and render resources. File size on disk is not equivalent to RAM usage: a large video is streamed rather than loaded completely into memory.

---

# Load Everything

**Load Everything** is available from the selected Library root.

It pre-generates missing preview/cache data for the Library so later browsing requires less on-demand work. Existing healthy cache files are reused instead of regenerated.

---

# Folder and Drive Availability

If the selected Library becomes unavailable, Visual MediaPlayer unloads inaccessible Library/session data and shows:

> **This folder is unavailable.**

For drive-letter libraries, the application monitors whether the backing drive is still mounted. If the saved Library later becomes available again, Visual MediaPlayer can reopen it automatically.

This is useful for removable drives and mounted encrypted volumes.

---

# Supported Media and Codecs

Visual MediaPlayer recognizes many common and legacy media file extensions. Recognition of an extension does **not** guarantee that Windows has a decoder for the codec stored inside the file.

If a recognized file cannot be decoded, the application shows:

> **This media is unsupported.**

## Video

Common recognized video extensions include:

`MP4`, `M4V`, `MKV`, `WEBM`, `AVI`, `MOV`, `WMV`, `ASF`, `MPG`, `MPEG`, `TS`, `M2TS`, `VOB`, `OGV`, `FLV`, `F4V`, `3GP`, `RM`, `RMVB`, `MXF`, and related formats.

Video playback uses **Windows Media Foundation**. Actual codec support therefore depends on the codecs available to Media Foundation on the Windows installation.

## Images and RAW files

Common recognized image extensions include:

`JPG`, `JPEG`, `PNG`, `APNG`, `BMP`, `GIF`, `TIFF`, `WEBP`, `HEIC`, `HEIF`, `AVIF`, `JXL`, `JPEG 2000`, `TGA`, `DDS`, `PSD`, `EXR`, `HDR`, `SVG`, and many camera RAW extensions.

Recognized camera RAW extensions include formats such as:

`DNG`, `CR2`, `CR3`, `NEF`, `NRW`, `ARW`, `RAF`, `ORF`, `RW2`, `PEF`, `X3F`, and others.

Images use **GDI+** with a **Windows Imaging Component (WIC)** fallback. RAW support therefore depends on an appropriate Windows/WIC RAW decoder being installed. For example, Nikon `.NEF` files are recognized by Visual MediaPlayer, but decoding still requires Windows to provide a compatible RAW codec.

---

# Fullscreen

Fullscreen is available in Library, Info, and Player.

- `F11` toggles fullscreen.
- `Esc` first performs the current view's Back/reset action.
- Player → Info → Library can remain fullscreen throughout navigation.
- At the top-level Library, when no search/folder navigation action remains, `Esc` exits fullscreen.

---

# Windows Explorer Integration

The Installer build registers supported media types for **Open with Visual MediaPlayer**.

From File Explorer:

1. Right-click a supported media file.
2. Choose **Open with**.
3. Select **Visual MediaPlayer**.

---

# Installer and Portable Builds

## Installer

The Installer build installs Visual MediaPlayer under Program Files and can create/register:

- `VisualMediaPlayer.exe`;
- a Start Menu shortcut;
- Windows **Open with** registration;
- an Installed Apps entry;
- `Uninstall.exe`.

The uninstaller removes application registration, settings, and the Start Menu entries it owns. Cache removal is optional.

In this source branch, the running uninstaller cannot delete itself immediately. `Uninstall.exe` and the now-empty installation directory are therefore scheduled for deletion by Windows at the next restart.

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
- Direct2D
- GDI+
- Windows Imaging Component (WIC)
- Compatible Windows codecs for the media being opened

No account, browser, cloud service, or internet connection is required for normal local playback.

---

# Building From Source

Visual MediaPlayer is a native **C++17 x64 Windows application**.

## Portable build

Run:

```text
Portable\Build.bat
```

## Installer build

Run:

```text
Installer\BuildInstaller.bat
```

Recommended environment:

- Visual Studio / MSVC
- Windows SDK
- C++17
- x64 Release configuration

---

# Main Technologies

- C++17
- Win32 API
- Direct3D 11 / DXGI
- Direct2D
- Windows Media Foundation
- GDI+
- Windows Imaging Component (WIC)
- Windows Shell APIs

---

**Visual MediaPlayer**
