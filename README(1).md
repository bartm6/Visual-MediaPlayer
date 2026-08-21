# Visual MediaPlayer

**Visual MediaPlayer** is a native 64-bit Windows media library and player for local **videos, images, camera RAW files, and VR media**.

It is designed for fast visual browsing, simple keyboard and mouse navigation, native-resolution viewing, high-resolution/VR playback, and a compact interface without unnecessary media-center features.

**Platform:** Windows 10 / Windows 11, x64

---

## Highlights

- Browse local video and image folders visually
- Hardware-accelerated Library renderer in both windowed and fullscreen modes
- Fast thumbnail-based Library with disk, RAM, and temporary GPU caching
- Video Info view with timeline previews
- Search within the current folder
- Search filters for **VR**, **4K**, **5K**, and **8K**
- Previous/next navigation that respects active search results
- Favorite media and search for favorites
- Normal video and image zoom with mouse-position anchoring
- Click-drag panning while zoomed
- Strict **Native Size** viewing for normal videos and images
- **VR180 / 360°** playback with mouse-look and FOV zoom
- Auto Next for videos
- Image slideshow
- Fullscreen Library, Info, and Player views
- Resolution and VR badges
- Broad video, image, and camera RAW extension recognition
- In-app notices for unavailable folders and unsupported media
- Automatic reopening of a saved library when it becomes available again
- Windows **Open with Visual MediaPlayer** integration
- Portable and Installer builds
- Original media files are never modified

---

# Quick Start

1. Start **Visual MediaPlayer**.
2. Click the **Folder** button and choose your media library.
3. Switch between **Videos** and **Images** as needed.
4. Click a media card to open its Info view.
5. For video, click **Play** or press `Space`.
6. For images, press `Space` to start or stop the slideshow.

The selected library is remembered between launches.

You can also open media directly from File Explorer with **Open with Visual MediaPlayer**. Visual MediaPlayer uses a single application window: opening another file replaces the current external-open session instead of starting another copy. Opening several media files together creates a temporary mini-library containing those files. Existing thumbnails and timeline cache are reused from their normal locations when possible.

---

# Controls

## Global

| Input | Action |
|---|---|
| `F11` | Toggle fullscreen |
| `Esc` | Context-sensitive Back / reset |
| Drag title bar | Move the app window |

`Esc` performs the most useful action for the current view. It does not leave fullscreen if there is another navigation or reset action to perform first.

## Library

| Input | Action |
|---|---|
| Type normally | Search the current folder |
| `Ctrl + F` | Toggle Favorite for the media under the mouse |
| `Ctrl + A` | Select all search text |
| `Backspace` | Delete search characters |
| `Enter` | Open the first search result |
| `Esc` | Close search, go up a folder, or leave fullscreen at the Library root |
| Mouse Wheel | Scroll |
| `Ctrl + Mouse Wheel` | Resize media cards |
| Click media | Open Info view |

When media is opened from a search, previous/next navigation remains inside that search result set. Returning to the Library keeps the current Library position/search context and highlights the media you came from.

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

Favorited media also carry the searchable word `favorite`, so searches such as `fav` can find favorites while normal filename matches remain visible too. `Ctrl + F` toggles Favorite for the hovered Library item or the current Info item.

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

When enabled, Visual MediaPlayer displays the media at **1 source pixel = 1 screen pixel**.

## Windowed Native Size

The application window resizes around the media and remains centered on the active monitor where possible.

While Native Size is active:

- The window cannot be resized below the media's true native pixel dimensions.
- The window may be enlarged beyond native size.
- Enlarging the window does **not** upscale the media; the media remains 1:1 and centered.
- Media larger than the monitor is not silently reduced simply to fit the display.

The Native Size minimum is enforced through the Windows window-sizing limits rather than by allowing the window to shrink and correcting it afterwards.

## Fullscreen Native Size

The application remains fullscreen while the media itself stays at native pixel size and is centered on screen.

Turning Native Size off returns the application to its normal sizing behavior. Leaving the media session also resets Native Size for the next use.

If Auto Next or slideshow is active, Native Size is reapplied to each new normal video or image.

---

# Auto Next, Slideshow & Volume

## Auto Next

Auto Next plays the next video when the current one finishes.

If a video has already ended and Auto Next is then enabled, the player advances immediately.

Auto Next is reset to **Off** when leaving the Player.

## Volume

A newly opened video playback session starts at **30% volume**.

If you change the volume while Auto Next is active, that volume is carried into the next automatically played video. Leaving the Player resets the next new playback session to 30%.

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

High-resolution VR playback can use substantial **dedicated or shared GPU memory**. This is normal: decoded video surfaces and VR rendering resources are working playback resources and do not need to mirror the media file's size in system RAM.

---

# Library Rendering & Scrolling

The Library uses the same hardware-accelerated rendering path at normal window sizes and in fullscreen.

The primary Library renderer uses **Direct2D + DirectWrite** hardware rendering for cards, thumbnails, text, badges, hover effects, navigation controls, search UI, notices, and the scrollbar. The legacy GDI Library painter is retained as an emergency fallback if the hardware renderer cannot be created or must be recreated.

Library scrolling does not shift old rendered pixels with `ScrollWindowEx`. Scrolling updates the scroll offset, redraws cards at their new positions, and presents the new frame through the hardware renderer. This keeps windowed and fullscreen Library behavior consistent.

---

# Cache & Memory Behavior

Visual MediaPlayer generates thumbnails, Info banners, timeline previews, and small metadata files so future browsing is faster.

Generated media cache data is stored below:

```text
.visualmediaplayer-cache
```

Original media files are never modified.

The Library uses a layered cache:

```text
disk cache
    ↓
decoded RAM thumbnail cache
    ↓
temporary visible/nearby GPU thumbnail copies
```

GPU-specific resources are temporary and are **never written to disk**.

## During Library browsing

The decoded thumbnail cache can grow as more media is browsed. This is intentional: recently decoded thumbnails are retained so returning to them is fast.

The normal decoded Library thumbnail budget can reach approximately **640 MiB**, but it dynamically contracts as total process memory rises. Normal process memory is targeted around **1 GiB**, with progressively more aggressive cache trimming above that level.

Prefetch distance also decreases under memory pressure.

## When video playback starts

Playback has priority over reconstructable Library cache memory.

When entering the Player, Visual MediaPlayer:

- Stops/cancels unnecessary background thumbnail and preview work.
- Releases the Library GPU rendering target and temporary GPU thumbnail residency.
- Reduces the deep CPU Library thumbnail cache to a smaller playback working set.
- Preserves the thumbnails for the Library viewport you left plus approximately **two surrounding rows** and the selected media.
- Trims expendable preview/detail cache data.

This means system RAM can **drop when a large 8K/VR video begins playing**. A demanding video may trigger cache eviction while its decoded surfaces are being allocated on the GPU.

When you return to the Library, the same scroll/search/selection context is retained. The visible Library is already partially warm, and normal Library prefetch then continues from that position.

## High-resolution playback memory

The app does **not** treat active decoder surfaces, current video textures, Media Foundation buffers, or VR rendering resources as disposable thumbnail cache.

If an 8K/VR video genuinely needs more memory, playback is allowed to use it. The app first sheds reconstructable Library/preview state rather than forcing the active decoder under an arbitrary hard process-memory limit.

Internal cache-pressure thresholds become increasingly aggressive around roughly 1.0–1.45 GiB of process memory, but those thresholds govern reconstructable cache state, not the video's required active working set.

---

# Load Everything

**Load Everything** can be used from the selected library root to pre-generate missing preview/cache data for the library.

Existing healthy cache data is reused. The feature is intended to prepare a library in advance so later browsing needs less source-media decoding.

---

# Folder Availability

If the selected library becomes unavailable, Visual MediaPlayer unloads the inaccessible library contents and shows:

> **This folder is unavailable.**

The normal empty-library prompt remains available so another folder can be selected.

If the saved library becomes available again, Visual MediaPlayer automatically reopens it.

---

# Media Formats

Visual MediaPlayer recognizes a broad range of media extensions. Actual decoding still depends on the codecs and imaging components installed in Windows.

## Video

Common recognized video formats include:

`MP4`, `M4V`, `MKV`, `MK3D`, `WEBM`, `AVI`, `DIVX`, `MOV`, `WMV`, `ASF`, `MPG`, `MPEG`, `TS`, `M2TS`, `VOB`, `OGV`, `FLV`, `F4V`, `3GP`, `RM`, `RMVB`, `MXF`, `H.264`, `H.265/HEVC`, `VP8`, `VP9`, `AV1`, `R3D`, `BRAW`, `INSV`, and many related or legacy extensions.

Video playback uses **Windows Media Foundation** together with the app's Direct3D 11 video presentation path.

## Images

Common recognized image formats include:

`JPG`, `JPEG`, `PNG`, `APNG`, `BMP`, `GIF`, `TIFF`, `WEBP`, `HEIC`, `HEIF`, `AVIF`, `JXL`, `JPEG 2000`, `TGA`, `DDS`, `PSD`, `EXR`, `HDR`, `SVG`, and others.

Images are decoded through **GDI+** with a **Windows Imaging Component (WIC)** fallback.

## Camera RAW / Nikon NEF

Recognized RAW extensions include formats such as:

`DNG`, `CR2`, `CR3`, `CRW`, **`NEF`**, `NRW`, `ARW`, `SRF`, `SR2`, `RAF`, `ORF`, `RW2`, `PEF`, `X3F`, `3FR`, `FFF`, `IIQ`, `ERF`, `MEF`, `MOS`, `MRW`, `KDC`, `DCR`, `RAW`, `SRW`, and others.

`.NEF` files therefore appear as images in the Library and Visual MediaPlayer will attempt to thumbnail/open them.

RAW decoding depends on a compatible Windows **WIC RAW codec** being installed for that camera/file format. Recognition of an extension does not guarantee that every RAW variant can be decoded on every Windows installation.

Large RAW photographs can require significant temporary and decoded RAM because the image must be expanded from its compressed camera format into a pixel bitmap for display.

---

# Unsupported Media

A recognized extension does not guarantee that every codec or file variant can be decoded on every Windows installation.

If a recognized file cannot be decoded, the app shows:

> **This media is unsupported.**

The notice is non-blocking and disappears automatically.

---

# Fullscreen

Fullscreen is available in the Library, Info view, and Player.

- `F11` toggles fullscreen.
- `Esc` first performs the current view's Back/reset action.
- Returning Player → Info → Library can remain fullscreen throughout.
- At the top Library, when there is nothing else for `Esc` to close or navigate, `Esc` leaves fullscreen.

Library thumbnails already present in RAM remain reusable across view/fullscreen transitions. When the Library becomes active, visible thumbnails can also be restored immediately from healthy private disk-cache files before normal asynchronous prefetch continues.

---

# Window Resizing During Playback

During an interactive window-frame drag, Visual MediaPlayer keeps presenting the existing completed video surface instead of repeatedly tearing down and resizing the DXGI backbuffer on every intermediate `WM_SIZE`.

The final media layout/backbuffer resize is applied when the interactive resize finishes. This reduces resize flicker while keeping playback visible during the drag.

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

- Install Visual MediaPlayer under `C:\Program Files\Visual MediaPlayer`
- Create the Start Menu shortcut
- Register Open With support
- Add the application to Windows Installed Apps
- Install an uninstaller

### Uninstall behavior

The current uninstaller stages a temporary helper in `%TEMP%`, waits for the installed uninstaller process to exit, and then removes the owned installation tree. This allows the Program Files directory to be removed immediately instead of leaving the running uninstaller inside the directory it is trying to delete.

Uninstall removes:

- `C:\Program Files\Visual MediaPlayer`
- Current and legacy Visual MediaPlayer Start Menu entries from machine-wide and per-user Programs locations
- Installed Apps/uninstall registration
- Open With/application registration
- Visual MediaPlayer application settings

If Windows unexpectedly keeps a file or directory locked, delayed deletion on reboot is retained only as a last-resort fallback.

Generated `.visualmediaplayer-cache` folders can be removed optionally during uninstall. Cache cleanup is separate from deleting original media; original media is never removed by the uninstaller.

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
- Direct2D / DirectWrite
- GDI+
- Windows Imaging Component (WIC)
- Compatible Windows codecs for the media being played
- Compatible WIC imaging/RAW codec for image formats not decoded natively by the installed Windows imaging stack

No account, browser, cloud service, or internet connection is required for normal local playback.

---

# Building From Source

Visual MediaPlayer is a native **C++17 x64** Windows application.

## Portable

Run:

```text
Portable\Build.bat
```

## Installer

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

# Technology

- C++17
- Win32 API
- Direct3D 11 / DXGI for video presentation
- Direct2D for the hardware-accelerated Library
- DirectWrite for Library text
- Windows Media Foundation
- GDI+
- Windows Imaging Component (WIC)
- Windows Shell APIs

---

# Data & Privacy

Visual MediaPlayer is designed for local media playback.

- Original media files are not modified.
- Generated thumbnail/preview data remains local.
- No account is required.
- No cloud service is required for normal playback.
- No internet connection is required for normal local operation.

---

**Visual MediaPlayer**
