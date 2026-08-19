# Visual MediaPlayer

**Visual MediaPlayer** is a lightweight native Windows media library and player for local **videos, images, and VR media**.

It is built in **C++17** with **Win32**, **Direct3D 11**, **Windows Media Foundation**, **GDI+**, and **Windows Imaging Component (WIC)**. The interface is designed around fast folder browsing, large visual previews, keyboard navigation, native-resolution viewing, and minimal UI clutter.

**Current version: 12.5.5**  
**Platform: 64-bit Windows 10 / Windows 11**

---

## Highlights

- Local video and image library with folder navigation
- Fast thumbnail browsing with a bounded in-memory thumbnail cache
- Video Info screen with a generated hero banner and timeline previews
- Click a timeline image to start playback from that point
- Search inside the current folder simply by typing
- Search filters for **VR**, **4K**, **5K**, and **8K** media
- Search-result-aware previous/next navigation
- Native Direct3D 11 video rendering
- **VR180 / 360°** playback with stereo-layout detection
- Side-by-side and top/bottom VR support
- Mouse-look and FOV zoom for VR
- Cursor-anchored zoom and click-drag panning for normal videos and images
- **Native Size** mode for non-VR videos and images
- Auto Next for videos
- Image slideshow with smooth transitions
- Fullscreen support in Library, Info, and Player views
- Resolution badges and VR badges on Library cards and Info banners
- Broad video/image extension recognition
- Non-blocking in-app notices for unavailable folders and unsupported media
- Automatically reloads a saved library when its drive becomes available again
- Windows **Open with Visual MediaPlayer** integration
- Portable and Installer builds
- Original media files are never modified

---

# Quick Start

1. Start **Visual MediaPlayer**.
2. Click the **Folder** button and choose the root of your media library.
3. Use the **Videos / Images** toggle to change media category.
4. Open folders normally and click a media card to open its Info screen.
5. For video, click **Play** or press `Space`.
6. For images, use `Space` to start/stop the slideshow from the currently opened image.

The selected library root is remembered between launches.

If no library is currently loaded, the Library displays:

> **Choose a folder to load videos and images.**

---

# Application Views

Visual MediaPlayer has three main views.

## 1. Library

The Library shows folders and the media contained directly in the current folder.

Features include:

- Folder navigation
- Videos / Images category toggle
- Media count for the current folder
- Instant text search
- VR and resolution badges
- Adjustable media-card size
- Fullscreen Library browsing
- Image slideshow shortcut
- Root-level Folder, Refresh, and Load Everything controls

### Library layout

On a 4K-class fullscreen display, the Library initially lays out **8 media cards per row**. This is only the starting size—you can still resize cards with `Ctrl + Mouse Wheel` while fullscreen.

Windowed layouts remain freely scalable as well.

### Library thumbnail memory

Visual MediaPlayer keeps a soft target of approximately **300 Library thumbnails in RAM**. Currently visible cards and nearby prefetched rows are protected from eviction so scrolling remains responsive.

Thumbnails are loaded/generated only when needed rather than continuously walking the entire library while idle.

---

## 2. Info

Clicking a media card opens its Info screen.

### Video Info

The Video Info screen contains:

- Large video banner
- VR badge when applicable
- 4K / 5K / 8K badge when applicable
- Secondary timeline preview images
- Timestamp labels
- Previous / next media arrows
- **Play** button
- Fullscreen control

Click any timeline preview to begin playback at that approximate timestamp.

### Timeline layout

- **Windowed:** defaults to **7 timeline images per row**
- **Fullscreen:** defaults to **10 timeline images per row**
- `Ctrl + Mouse Wheel` changes timeline-card size

### Image Info

The Image Info screen provides:

- Large contained image view
- Previous / next image navigation
- Slideshow control
- Native Size control
- Fullscreen control
- Cursor-anchored image zoom
- Click-drag panning while zoomed

---

## 3. Player

The video Player contains:

- Back
- Play / Pause
- 30-second skip backward
- 30-second skip forward
- Seek timeline
- Current time / duration
- Volume
- Auto Next
- Native Size for non-VR video
- Fullscreen
- VR 180° / 360° toggle when playing VR media
- Previous / next video buttons at the sides of the player

The control layout automatically rearranges on narrow windows so buttons do not overlap.

Video rendering continues while the application window is being moved or resized.

---

# Keyboard & Mouse Reference

## Global / Common

| Input | Action |
|---|---|
| `F11` | Toggle fullscreen |
| `Esc` | Context-sensitive Back / reset action; see the sections below |
| Drag the title bar | Move the app window; active video/image rendering continues |

Fullscreen is preserved when `Esc` has a more specific action to perform. At the top Library with nothing else to close, `Esc` exits fullscreen.

---

## Library Controls

| Input | Action |
|---|---|
| Type normally | Start/search within the current folder |
| `Space` while typing/searching | Inserts a normal space in the search |
| `Ctrl + F` | Open/focus search; selects the existing query when present |
| `Ctrl + A` | Select all current search text |
| `Backspace` | Delete search characters |
| `Enter` | Open the first matching search result |
| `Esc` while search is open | Clear and close search |
| `Esc` in a subfolder | Go up one folder |
| `Esc` at Library root while fullscreen | Exit fullscreen |
| Mouse Wheel | Scroll Library |
| `Ctrl + Mouse Wheel` | Resize Library cards |
| Drag scrollbar thumb | Scroll Library directly |
| `F11` | Toggle fullscreen |

### Returning from search

When you open a media item from search:

- Info-screen Left/Right navigation stays inside that search result set.
- Returning to Library keeps the search active.
- The item you returned from is centered and receives the animated focus/breathing highlight.

---

## Video Info Controls

| Input | Action |
|---|---|
| `Space` | Play the selected video |
| `Left Arrow` | Previous video in the current navigation set |
| `Right Arrow` | Next video in the current navigation set |
| Mouse Wheel | Scroll the Info page |
| `Ctrl + Mouse Wheel` | Resize secondary timeline previews |
| Click timeline image | Start playback from that timestamp |
| `Esc` | Return to Library while preserving fullscreen |
| `F11` | Toggle fullscreen |

If the video was opened from a search result, Left/Right stays within those searched results.

---

## Image Info Controls

| Input | Action |
|---|---|
| `Space` | Start / stop slideshow from the current image |
| `Left Arrow` | Previous image |
| `Right Arrow` | Next image |
| `Ctrl + Mouse Wheel` over image | Zoom toward/away from the mouse position |
| Left-click + drag while zoomed | Pan the zoomed image |
| First `Esc` while zoomed | Reset to normal fit-to-window image |
| Next `Esc` | Return to Library |
| `F11` | Toggle fullscreen |

Free zoom/pan is disabled while **Native Size** is active.

---

## Normal Video Player Controls

| Input | Action |
|---|---|
| `Space` | Play / Pause |
| `Left Arrow` | Skip backward **30 seconds** |
| `Right Arrow` | Skip forward **30 seconds** |
| Mouse Wheel | Zoom toward/away from the mouse position |
| Left-click + drag while zoomed | Pan the zoomed video |
| First `Esc` while zoomed | Reset video to normal fit-to-window |
| Next `Esc` | Return to Info |
| `F11` | Toggle fullscreen |

The ±30-second keyboard seek does **not** force the player controls to pop open.

Use the large side-arrow buttons for previous/next **video** navigation. Keyboard Left/Right remains dedicated to ±30-second seeking during playback.

Free zoom/pan is disabled while **Native Size** is active.

---

## VR Player Controls

| Input | Action |
|---|---|
| Left-click + drag | Look around the VR scene |
| Mouse Wheel | Change VR field of view |
| `Space` | Play / Pause |
| `Left Arrow` | Skip backward 30 seconds |
| `Right Arrow` | Skip forward 30 seconds |
| `F11` | Toggle fullscreen |
| `Esc` | Return to Info |
| **180° / 360° button** | Toggle VR projection mode |

VR keeps its own independent mouse-look and FOV system. The normal flat-video zoom/pan system does not alter VR behavior.

The **Native Size** control is intentionally hidden for VR videos.

---

# Search

Search is local to the folder currently being viewed.

You do not need to click a search box first—simply start typing in the Library.

Search supports normal text plus special media filters.

## Search Filters

| Search token | Matches |
|---|---|
| `VR` | VR videos |
| `4K` | 4K, 5K, and 8K videos |
| `5K` | 5K and 8K videos |
| `8K` | 8K videos |

Filters can be combined with normal text.

Examples:

```text
holiday 4k
concert vr
vacation 8k
```

A resolution search may need to determine a video's resolution the first time it is encountered. That resolution is then stored as tiny cached metadata so future launches do not need to probe the same unchanged file again.

---

# VR Media

Visual MediaPlayer supports VR media including:

- VR180
- 360° video
- Side-by-side stereo (SBS / LR)
- Top/bottom stereo (TB / OU)
- Mono panoramic VR
- Automatic stereo-layout detection for ambiguous files
- Single-eye rendering of stereo-packed sources

## VR filename detection

VR detection recognizes common markers such as:

- `VR`
- `VR180`
- `180VR`
- filenames ending in `360`
- filenames ending in forms such as `360 (2)`
- `SBS`, `LR`, `TB`, `OU` stereo-layout markers

Stereo-packed VR defaults to the front-facing **180°** view. Use the VR projection button to switch between **180°** and **360°** while playing.

For stereo-packed material viewed in 360° mode, the rear hemisphere is handled without stretching the front 180° source across the entire sphere.

---

# Native Size

Native Size is available for:

- Normal/non-VR videos in the Player
- Images in the Info view

When enabled, Visual MediaPlayer attempts to show the source at **1 media pixel = 1 screen pixel**.

## Windowed Native Size

The window is resized and centered so its actual client/render area matches the media's native dimensions whenever the monitor can physically accommodate them.

If the source is larger than the monitor, it is reduced only enough to fit.

## Fullscreen Native Size

The app remains fullscreen, but the media itself is rendered at its native pixel dimensions and centered instead of automatically enlarging to fill the display.

## Standard window size

When Native Size is turned off or a media session is exited, the normal window returns to **50% of the active monitor's width and 50% of its height**, centered on that monitor.

Examples:

| Monitor | Standard window |
|---|---|
| 3840×2160 | 1920×1080 |
| 2560×1440 | 1280×720 |
| 1920×1080 | 960×540 |

If fullscreen is active, returning from Native Size keeps the app fullscreen rather than changing the window size.

## Native Size + Auto Next / Slideshow

- Auto Next reapplies Native Size for each new normal video.
- If a VR video occurs between normal videos, Native Size is temporarily suspended for that VR video and resumes on the next normal video.
- Image slideshow reapplies Native Size for each new image.
- Leaving the video/image session resets Native Size for the next use.

---

# Auto Next & Volume

## Auto Next

Auto Next plays the next video in the current folder when playback finishes.

If the video has already reached the end while Auto Next is off, enabling Auto Next immediately advances to the next available video.

When leaving the Player, Auto Next is reset to **Off** for the next playback session.

## Volume behavior

A new playback session starts at **30% volume**.

If you change the volume while **Auto Next** is enabled, that adjusted volume is carried into the next automatically played video.

Leaving the Player resets the next playback session to **30%**.

---

# Image Slideshow

The slideshow operates on images in the current folder.

- Starts from the currently opened image when launched from Info
- Can be started with `Space`
- Advances every **3 seconds**
- Uses a smooth transition between images
- Native Size is reapplied per image when enabled
- Stops when leaving the image session
- Slideshow state is reset for the next use

If the slideshow is enabled while already on the final image, it immediately wraps to the first image rather than appearing to do nothing.

---

# Resolution & VR Badges

Video cards can display:

- **VR** icon
- **4K** icon
- **5K** icon
- **8K** icon

The same applicable badges are also shown in the upper-right corner of the Video Info banner.

Resolution is based on the source video's native frame dimensions. Nearby cinema/VR dimensions can be classified into the closest supported 4K/5K/8K class rather than requiring one exact consumer-TV dimension.

Resolution metadata is cached per source version. If the source file changes, its cache identity changes and the metadata is determined again.

---

# Load Everything

**Load Everything** is available at the selected library root.

It pre-generates missing media data for the library, including the applicable:

- Library banners/thumbnails
- Info banners
- Video timeline previews
- Video resolution metadata

A progress popup appears in the upper-right while the operation is running.

Existing healthy cache data is reused instead of regenerated unnecessarily.

This control is intentionally restricted to the actual selected Library root rather than subfolders.

---

# Folder, Refresh & Library Availability

The main maintenance controls are shown at the selected Library root:

- **Load Everything**
- **Refresh**
- **Choose Folder**

If the saved library drive becomes unavailable, Visual MediaPlayer clears the unavailable Library contents from the interface and shows a compact pulsing in-app notice:

> **This folder is unavailable.**

The empty Library continues to show:

> **Choose a folder to load videos and images.**

When the saved library becomes available again, Visual MediaPlayer automatically reopens/rescans it.

No confirmation dialog is required.

---

# Unsupported Media

Visual MediaPlayer recognizes a broad range of media extensions, but the actual ability to decode a particular file also depends on the file's codec/encoding and the media components available on the Windows system.

If a recognized file cannot be decoded, Visual MediaPlayer shows the same compact non-blocking in-app notification style:

> **This media is unsupported.**

The message appears in the upper-right, pulses for approximately five seconds, and then disappears automatically.

Background thumbnail scanning does not spam error dialogs for files that cannot be decoded.

---

# Recognized Media Extensions

Recognition means the file can appear in the Visual MediaPlayer library and the application will attempt to open it. It does **not** guarantee that every possible codec stored inside every container can be decoded on every Windows installation.

## Video extensions

<details>
<summary>Show recognized video extensions</summary>

```text
.mp4   .m4v   .mkv   .mk3d  .webm  .avi   .divx  .mov
.qt    .wmv   .asf   .mpg   .mpeg  .mpe   .mpv   .mpv2
.m1v   .m2v   .m2p   .ts    .m2t   .mts   .m2ts  .tp
.trp   .vob   .vro   .ogv   .ogm   .flv   .f4v   .f4p
.3gp   .3g2   .3gp2  .3gpp  .rm    .rmvb  .rv    .mxf
.gxf   .dv    .dif   .dvr-ms .wtv  .mod   .tod   .amv
.ivf   .y4m   .nut   .nsv   .roq   .smk   .bik   .bk2
.mjpeg .mjpg  .mjp   .h264  .264   .avc   .h265  .265
.hevc  .vp8   .vp9   .av1   .r3d   .braw  .ari   .cine
.crm   .insv  .lrv   .360   .evo   .mj2
```

</details>

Video playback uses **Windows Media Foundation**. A listed extension may still contain a codec that is unavailable on the current system; in that case the in-app unsupported-media notice is shown.

## Image extensions

<details>
<summary>Show recognized image extensions</summary>

```text
.jpg   .jpeg  .jpe   .jfif  .jif   .jfi   .png   .apng
.bmp   .dib   .gif   .tif   .tiff  .webp  .heic  .heif
.hif   .avif  .avifs .jxl   .jp2   .j2k   .j2c   .jpf
.jpx   .jpm   .jxr   .wdp   .hdp   .tga   .targa .icb
.vda   .vst   .dds   .pcx   .ico   .cur   .mng   .psd
.psb   .exr   .hdr   .rgbe  .pic   .pfm   .pnm   .ppm
.pgm   .pbm   .pam   .qoi   .sgi   .rgb   .rgba  .bw
.ras   .sun   .xbm   .xpm   .svg   .svgz  .dng   .cr2
.cr3   .crw   .nef   .nrw   .arw   .srf   .sr2   .raf
.orf   .rw2   .rwl   .pef   .x3f   .3fr   .fff   .iiq
.erf   .mef   .mos   .mrw   .kdc   .dcr   .raw   .srw
.bay   .cap   .eip   .mdc   .rwz
```

</details>

Images are decoded using **GDI+** with a **Windows Imaging Component (WIC)** fallback where available.

Some specialized, RAW, vector, HDR, or newer formats require a compatible Windows imaging codec/provider.

---

# Cache

Visual MediaPlayer creates its generated media cache beside the corresponding media folders under:

```text
.visualmediaplayer-cache
```

The cache can contain generated items such as:

- Library thumbnails/banners
- Native Info banners
- Timeline images
- Timeline completion/duration data
- Tiny resolution metadata files

Cache entries are tied to the source media identity, including information such as file size and modification time. Changing/replacing a source therefore produces a new cache identity rather than blindly trusting stale data.

Original media files are never modified.

The application also avoids keeping source media open unnecessarily once generated images have been copied into RAM. Active video playback is the primary operation that needs to retain continuous access to the source video.

---

# Filename Display & Sorting

Visual MediaPlayer uses natural media-oriented sorting rather than simple string order.

Numeric suffixes are treated numerically, so for example:

```text
Movie (1)
Movie (2)
Movie (10)
Movie (100)
```

sort in the expected order.

Paired/ampersand names and `360` suffixes are grouped consistently so related media stays together.

For images whose filename begins with a generated dimension prefix such as:

```text
2000x1333 56dd215e4c7c204bf682908b59578f1a.jpg
```

the leading `2000x1333` portion is hidden from the displayed/search title. The actual filename on disk is not changed.

---

# Fullscreen Behavior

Fullscreen is available from Library, Info, and Player.

- `F11` toggles fullscreen globally.
- `Esc` does **not** automatically exit fullscreen when another meaningful action is available.
- Player `Esc` returns to Info while fullscreen remains active.
- Info `Esc` returns to Library while fullscreen remains active.
- Library subfolder `Esc` navigates upward while fullscreen remains active.
- At the top Library, when no other action remains, `Esc` exits fullscreen.

This lets you navigate through Library → Info → Player and back without unnecessarily dropping out of fullscreen.

---

# Windows Explorer / Open With

The Installer build registers Visual MediaPlayer for supported media extensions so it can be selected from Windows Explorer.

Typical use:

1. Right-click a media file.
2. Choose **Open with**.
3. Select **Visual MediaPlayer**.
4. Choose **Always** if you want Windows to remember the association.

Videos opened directly from Explorer can enter playback directly.

---

# Installation Options

## Installer

Build/output:

```text
VisualMediaPlayerSetup.exe
```

The installer can:

- Install Visual MediaPlayer
- Create a Start Menu shortcut
- Register **Open with Visual MediaPlayer** associations
- Add Visual MediaPlayer to Installed Apps
- Install an uninstaller

The uninstaller removes the installed application, shortcuts, registration, and application settings. It also provides an optional cache-cleanup choice rather than deleting generated cache data without permission.

## Portable

Build/output:

```text
VisualMediaPlayer.exe
```

No installation is required. Run the executable directly.

> Windows SmartScreen may warn about unsigned locally built executables.

---

# Settings

Application settings are stored under:

```text
%LOCALAPPDATA%\VisualMediaPlayer\
```

The app remembers core preferences such as the selected library root and global Videos/Images category.

Playback-session states such as Auto Next, slideshow, Native Size, and adjusted playback volume are intentionally reset when their corresponding media session is exited.

---

# Requirements

- 64-bit Windows 10 or Windows 11
- Direct3D 11-capable graphics hardware
- Windows Media Foundation
- GDI+ / Windows Imaging Component
- Compatible system media codecs for the specific formats being played

No account, browser, web server, or cloud service is required for normal local playback.

---

# Building From Source

Visual MediaPlayer is built as a native **x64 Release** C++ application.

Recommended environment:

- Visual Studio 2026 / compatible MSVC installation
- MSVC Platform Toolset **v145**
- Windows SDK
- C++17
- x64 target
- Release configuration

## Portable build

From:

```text
Portable\
```

run:

```text
Build.bat
```

The resulting application is:

```text
VisualMediaPlayer.exe
```

## Installer build

From:

```text
Installer\
```

run:

```text
BuildInstaller.bat
```

The resulting installer is:

```text
VisualMediaPlayerSetup.exe
```

---

# Project Layout

```text
Visual-MediaPlayer-1.0\
├─ Portable\
│  ├─ Build.bat
│  └─ Source\
│     ├─ VisualMediaPlayer.sln
│     ├─ VisualMediaPlayer.vcxproj
│     ├─ src\
│     └─ res\
├─ Installer\
│  ├─ BuildInstaller.bat
│  ├─ Source\
│  │  ├─ VisualMediaPlayer.sln
│  │  ├─ VisualMediaPlayer.vcxproj
│  │  ├─ src\
│  │  └─ res\
│  └─ Installer\
│     ├─ VisualMediaPlayerSetup.vcxproj
│     └─ src\
└─ README.md
```

---

# Technology

- **C++17**
- **Win32 API**
- **Direct3D 11**
- **DXGI**
- **Windows Media Foundation**
- **GDI+**
- **Windows Imaging Component (WIC)**
- **Windows Shell APIs**
- Native Windows installer/uninstaller

---

# Design Principles

Visual MediaPlayer is intentionally focused on local visual media:

- Keep the interface compact and predictable.
- Keep media navigation fast even with large folders.
- Prefer background generation over blocking the UI.
- Cache expensive preview work without modifying originals.
- Keep VR behavior isolated from normal flat-media controls.
- Make keyboard and mouse actions consistent with the current view.
- Fail gracefully when a file cannot be decoded or a saved library is temporarily unavailable.

---

**Visual MediaPlayer 12.5.5**
