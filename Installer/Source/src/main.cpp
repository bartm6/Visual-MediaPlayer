#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <oleauto.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <shlobj.h>
#include <psapi.h>
#include "../res/resource.h"

#include <algorithm>
#include <cstring>
#include <climits>
#include <cwctype>
#include <iterator>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <string>
#include <utility>
#include <vector>
#include <cwchar>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdint>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "msimg32.lib")

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

static constexpr UINT WM_APP_MEDIA_EVENT = WM_APP + 1;
static constexpr UINT WM_APP_SEEK_COMMIT = WM_APP + 2;
static constexpr UINT WM_APP_PLAYER_READY = WM_APP + 3;
static constexpr UINT WM_APP_SELECTED_WORK_DONE = WM_APP + 4;
static constexpr UINT WM_APP_MEDIA_ERROR = WM_APP + 5;
static constexpr float PI_F = 3.14159265358979323846f;

static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}


static void TrimSortSpaces(std::wstring& s) {
    size_t first = 0;
    while (first < s.size() && iswspace(s[first])) ++first;
    size_t last = s.size();
    while (last > first && iswspace(s[last - 1])) --last;
    s = s.substr(first, last - first);
}

static bool ExtractTrailingSortNumber(std::wstring& text, std::wstring& digits) {
    TrimSortSpaces(text);
    if (text.empty() || text.back() != L')') return false;

    const size_t open = text.rfind(L" (");
    if (open == std::wstring::npos || open + 2 >= text.size() - 1) return false;

    const size_t digitsBegin = open + 2;
    const size_t digitsEnd = text.size() - 1;
    for (size_t i = digitsBegin; i < digitsEnd; ++i) {
        if (!iswdigit(text[i])) return false;
    }

    digits = text.substr(digitsBegin, digitsEnd - digitsBegin);
    text.erase(open);
    TrimSortSpaces(text);
    return true;
}

static int CompareSortNumbers(const std::wstring& a, const std::wstring& b) {
    size_t aFirst = 0, bFirst = 0;
    while (aFirst + 1 < a.size() && a[aFirst] == L'0') ++aFirst;
    while (bFirst + 1 < b.size() && b[bFirst] == L'0') ++bFirst;

    const size_t aLen = a.size() - aFirst;
    const size_t bLen = b.size() - bFirst;
    if (aLen != bLen) return aLen < bLen ? -1 : 1;

    const int valueCmp = a.compare(aFirst, aLen, b, bFirst, bLen);
    if (valueCmp != 0) return valueCmp < 0 ? -1 : 1;

    // Equal numeric values: keep the shorter spelling first, e.g. (1) before (01).
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

struct MediaNameSortKey {
    std::wstring primary;
    std::wstring secondary;
    std::wstring number;
    std::wstring fallback;
    int group = 0;
    bool hasNumber = false;
};

static MediaNameSortKey BuildMediaNameSortKey(const std::wstring& title) {
    MediaNameSortKey key;
    key.fallback = ToLower(title);

    std::wstring text = key.fallback;
    TrimSortSpaces(text);
    key.hasNumber = ExtractTrailingSortNumber(text, key.number);

    // VR files named "name VR" or "name VR (x)" always come last in
    // the matching name family. The optional x is still sorted numerically.
    if (text.size() >= 3 && text.compare(text.size() - 3, 3, L" vr") == 0) {
        key.primary = text.substr(0, text.size() - 3);
        TrimSortSpaces(key.primary);
        key.group = 3;
        return key;
    }

    // "name &" and "name & (x)" are their own groups before "name & name".
    if (text.size() >= 2 && text.compare(text.size() - 2, 2, L" &") == 0) {
        key.primary = text.substr(0, text.size() - 2);
        TrimSortSpaces(key.primary);
        key.group = 1;
        return key;
    }

    // "name & name" and "name & name (x)" follow the bare ampersand forms.
    const size_t amp = text.find(L" & ");
    if (amp != std::wstring::npos) {
        key.primary = text.substr(0, amp);
        key.secondary = text.substr(amp + 3);
        TrimSortSpaces(key.primary);
        TrimSortSpaces(key.secondary);
        key.group = 2;
        return key;
    }

    key.primary = text;
    key.group = 0;
    return key;
}



static std::wstring StripLeadingImageResolutionPrefix(std::wstring title) {
    // Some generated/downloaded image filenames begin with dimensions such as
    // "2000x1333 ". Keep the real file/path unchanged; only hide that leading
    // resolution token from the user-facing image title/search text.
    size_t pos = 0;
    const size_t n = title.size();
    while (pos < n && iswdigit(title[pos])) ++pos;
    if (pos == 0 || pos >= n || (title[pos] != L'x' && title[pos] != L'X')) return title;
    ++pos;
    const size_t heightBegin = pos;
    while (pos < n && iswdigit(title[pos])) ++pos;
    if (pos == heightBegin || pos >= n || !iswspace(title[pos])) return title;
    while (pos < n && iswspace(title[pos])) ++pos;
    if (pos >= n) return title;
    return title.substr(pos);
}

static std::wstring NormalizeMarkerText(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.size() + 2);
    bool lastWasSpace = true;
    for (wchar_t c : ToLower(input)) {
        if (iswalnum(c)) {
            out.push_back(c);
            lastWasSpace = false;
        } else if (!lastWasSpace) {
            out.push_back(L' ');
            lastWasSpace = true;
        }
    }
    if (!out.empty() && out.back() == L' ') out.pop_back();
    return L" " + out + L" ";
}

static bool HasMarker(const std::wstring& normalized, const wchar_t* marker) {
    return normalized.find(std::wstring(L" ") + marker + L" ") != std::wstring::npos;
}

static bool IsVideoExtension(const std::wstring& extRaw) {
    const std::wstring ext = ToLower(extRaw);
    // Broad container/elementary-stream/camera list. Recognition only means Visual
    // MediaPlayer will list/open the file; actual decoding still depends on Media
    // Foundation and codecs installed on this Windows system.
    static const wchar_t* kExts[] = {
        L".mp4", L".m4v", L".mkv", L".mk3d", L".webm", L".avi", L".divx", L".mov", L".qt", L".wmv", L".asf", L".mpg", L".mpeg", L".mpe", L".mpv", L".mpv2", L".m1v", L".m2v", L".m2p", L".ts", L".m2t", L".mts", L".m2ts", L".tp", L".trp", L".vob", L".vro", L".ogv", L".ogm", L".flv", L".f4v", L".f4p", L".3gp", L".3g2", L".3gp2", L".3gpp", L".rm", L".rmvb", L".rv", L".mxf", L".gxf", L".dv", L".dif", L".dvr-ms", L".wtv", L".mod", L".tod", L".amv", L".ivf", L".y4m", L".nut", L".nsv", L".roq", L".smk", L".bik", L".bk2", L".mjpeg", L".mjpg", L".mjp", L".h264", L".264", L".avc", L".h265", L".265", L".hevc", L".vp8", L".vp9", L".av1", L".r3d", L".braw", L".ari", L".cine", L".crm", L".insv", L".lrv", L".360", L".evo", L".mj2"
    };
    for (auto* e : kExts) if (ext == e) return true;
    return false;
}

static std::wstring HrText(HRESULT hr) {
    wchar_t buf[64]{};
    swprintf_s(buf, L"0x%08X", static_cast<unsigned>(hr));
    return buf;
}

static std::wstring PathToFileUrl(const std::wstring& path) {
    wchar_t out[32768]{};
    DWORD len = static_cast<DWORD>(std::size(out));
    if (SUCCEEDED(UrlCreateFromPathW(path.c_str(), out, &len, 0))) return out;
    return path;
}

struct VRInfo {
    bool vr = false;
    int layout = 0;      // 0 mono, 1 SBS, 2 top-bottom
    int projection = 0;  // 0 flat, 1 360, 2 180
    bool layoutExplicit = false; // only true when filename explicitly says SBS/LR/TB/OU
};

static VRInfo DetectVR(const std::wstring& file) {
    const std::wstring filename = fs::path(file).filename().wstring();
    const std::wstring markers = NormalizeMarkerText(filename);
    VRInfo v;

    // VR classification is filename-marker based. A standalone "VR" token marks
    // generic VR media; VR180/180VR remain explicit 180-degree markers.
    // The old "360" filename suffix is intentionally no longer a VR marker.
    const bool has180 = HasMarker(markers, L"vr180") || HasMarker(markers, L"180vr") ||
                        markers.find(L" vr 180 ") != std::wstring::npos ||
                        markers.find(L" 180 vr ") != std::wstring::npos;
    const bool hasVr = has180 || HasMarker(markers, L"vr");
    if (!hasVr) return v;

    v.vr = true;
    v.projection = has180 ? 2 : 1;

    const bool tb = HasMarker(markers, L"tb") || HasMarker(markers, L"ou") ||
                    markers.find(L" top bottom ") != std::wstring::npos ||
                    markers.find(L" over under ") != std::wstring::npos;
    const bool sbs = HasMarker(markers, L"sbs") || HasMarker(markers, L"lr") ||
                     markers.find(L" side by side ") != std::wstring::npos ||
                     markers.find(L" left right ") != std::wstring::npos;

    if (tb) { v.layout = 2; v.layoutExplicit = true; }
    else if (sbs) { v.layout = 1; v.layoutExplicit = true; }
    else v.layout = 0;

    // Explicit stereo packing is front-facing VR180 by default. Ambiguous files are
    // resolved once from decoded content later rather than by substring guesses.
    if (v.layout != 0) v.projection = 2;
    return v;
}

struct MediaItem {
    std::wstring path;
    std::wstring title;
    std::wstring cachePath;
    std::wstring uiCachePath;
    std::wstring searchText;
    VRInfo vr;
    bool isVideo = true;
    HBITMAP thumb = nullptr;
    int thumbW = 0;
    int thumbH = 0;
    // The GPU Library renderer keeps a device-dependent Direct2D copy of resident
    // thumbnails. It is recreated lazily if the render target changes.
    ComPtr<ID2D1Bitmap> libraryGpuThumb;
    HBITMAP libraryGpuThumbSource = nullptr;
    uint64_t libraryGpuGeneration = 0;
    // Details/Info uses the same hardware Direct2D target as Library, but keeps a
    // separate device-dependent copy because its hero image may be the full native banner.
    ComPtr<ID2D1Bitmap> detailsGpuThumb;
    HBITMAP detailsGpuThumbSource = nullptr;
    uint64_t detailsGpuGeneration = 0;
    bool thumbAttempted = false;
    bool thumbFromPrivateCache = false;
    ULONGLONG thumbLastUsed = 0;
    // Library thumbnail disk/decode requests are performed off the UI thread.
    // The epoch prevents stale work from a previous scroll position being applied.
    uint64_t thumbLoadRequestEpoch = 0;
    int thumbLoadRequestW = 0;
    int thumbLoadRequestH = 0;
    ULONGLONG thumbNextLoadAttempt = 0;
    HBITMAP detailThumb = nullptr;
    int detailThumbW = 0;
    int detailThumbH = 0;
    bool detailDecodeUnsupported = false;
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    bool resolutionProbeAttempted = false;
    bool resolutionMetadataQueued = false;
    bool favorite = false;
};

struct LibraryFolder {
    std::wstring path;
    std::wstring name;
};

struct PreviewFrame {
    int seconds = 0;
    std::wstring path;
    HBITMAP bitmap = nullptr;
    ULONGLONG lastUsed = 0;
    int loadFailures = 0;
    ULONGLONG nextLoadAttempt = 0;
    // Temporary GPU copy of the decoded preview. It is never written to disk and is
    // recreated lazily if the shared Direct2D render target is lost.
    ComPtr<ID2D1Bitmap> gpuBitmap;
    HBITMAP gpuBitmapSource = nullptr;
    uint64_t gpuGeneration = 0;
};

class MediaEngineNotify final : public IMFMediaEngineNotify {
public:
    explicit MediaEngineNotify(HWND hwnd) : hwnd_(hwnd) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --ref_;
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP EventNotify(DWORD meEvent, DWORD_PTR param1, DWORD param2) override {
        PostMessageW(hwnd_, WM_APP_MEDIA_EVENT, meEvent, static_cast<LPARAM>(param1));
        (void)param2;
        return S_OK;
    }
private:
    std::atomic<ULONG> ref_{1};
    HWND hwnd_{};
};

class NativePlayer {
public:
    ~NativePlayer() { Shutdown(); }

    HRESULT Initialize(HWND eventWindow, HWND videoWindow) {
        eventWindow_ = eventWindow;
        videoWindow_ = videoWindow;

        RECT rc{}; GetClientRect(videoWindow_, &rc);
        UINT width = std::max<LONG>(1L, rc.right - rc.left);
        UINT height = std::max<LONG>(1L, rc.bottom - rc.top);

        DXGI_SWAP_CHAIN_DESC sc{};
        sc.BufferCount = 2;
        sc.BufferDesc.Width = width;
        sc.BufferDesc.Height = height;
        sc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.OutputWindow = videoWindow_;
        sc.SampleDesc.Count = 1;
        sc.Windowed = TRUE;
        sc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requested, static_cast<UINT>(std::size(requested)), D3D11_SDK_VERSION,
            &sc, &swapChain_, &device_, &got, &context_);
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                requested + 1, static_cast<UINT>(std::size(requested) - 1), D3D11_SDK_VERSION,
                &sc, &swapChain_, &device_, &got, &context_);
        }
        if (FAILED(hr)) return hr;

        ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device_.As(&mt))) mt->SetMultithreadProtected(TRUE);

        hr = CreateBackbuffer();
        if (FAILED(hr)) return hr;
        hr = CreatePipeline();
        if (FAILED(hr)) return hr;

        hr = MFCreateDXGIDeviceManager(&dxgiResetToken_, &dxgiManager_);
        if (FAILED(hr)) return hr;
        hr = dxgiManager_->ResetDevice(device_.Get(), dxgiResetToken_);
        if (FAILED(hr)) return hr;

        return CreateMediaEngine();
    }

    HRESULT Open(const std::wstring& path, const VRInfo& vr, double startSeconds = -1.0) {
        if (!engine_) {
            const HRESULT recreateHr=CreateMediaEngine();
            if(FAILED(recreateHr)) return recreateHr;
        }
        const bool switchingMedia = !path_.empty() && path_ != path && videoSRV_ && eyeW_ && eyeH_;
        if (switchingMedia) {
            transitionTexture_ = videoTexture_;
            transitionSRV_ = videoSRV_;
            transitionState_ = CurrentSurfaceState();
            transitionWaitingForFrame_ = true;
            transitionActive_ = false;
            transitionStart_ = 0;
        } else if (path_.empty() || path_ == path) {
            ClearVideoTransition();
        }
        vrInfo_ = vr;
        // VR always opens in the standard front-only 180 mode.  The 360 state is
        // user-enabled only, so the button stays dark until the user turns 360 on.
        projectionOverride_ = vr.vr ? 2 : 0;
        layoutDetectionDone_ = vr.layoutExplicit;
        layoutDetectionPending_ = false;
        layoutDetectionAttempts_ = 0;
        layoutMonoVotes_ = 0;
        stereoProbeGpu_.Reset();
        stereoProbeStaging_.Reset();
        stereoProbePending_ = false;
        stereoProbeW_ = stereoProbeH_ = 0;
        pendingStartSeconds_ = startSeconds;
        yaw_ = 0.0f;
        pitch_ = 0.0f;
        fovRadians_ = 65.0f * PI_F / 180.0f;
        ResetFlatZoom();
        nativeW_ = nativeH_ = eyeW_ = eyeH_ = 0;
        videoTexture_.Reset();
        videoSRV_.Reset();
        autoPlayWhenReady_ = true;
        path_ = path;

        BSTR src = SysAllocString(PathToFileUrl(path).c_str());
        if (!src) return E_OUTOFMEMORY;
        HRESULT hr = engine_->SetSource(src);
        SysFreeString(src);
        if (FAILED(hr)) return hr;
        engine_->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC);
        return engine_->Load();
    }

    void CloseSource() {
        // Pause is not enough for removable/encrypted volumes: Media Foundation may
        // retain the underlying source handle. Shutdown only the media engine here so
        // the file is definitively released while the D3D device/swap chain stay alive.
        if (engine_) {
            engine_->Pause();
            engine_->Shutdown();
        }
        engine_.Reset();
        notify_.Reset();
        path_.clear();
        pendingStartSeconds_=-1.0;
        autoPlayWhenReady_=false;
        nativeW_=nativeH_=eyeW_=eyeH_=0;
        videoSRV_.Reset();
        videoTexture_.Reset();
        ClearVideoTransition();
        ResetFlatZoom();
    }

    void Shutdown() {
        if (engine_) engine_->Shutdown();
        engine_.Reset();
        notify_.Reset();
        dxgiManager_.Reset();
        videoSRV_.Reset();
        videoTexture_.Reset();
        ClearVideoTransition();
        blendState_.Reset();
        stereoProbeGpu_.Reset();
        stereoProbeStaging_.Reset();
        stereoProbePending_ = false;
        renderTarget_.Reset();
        swapChain_.Reset();
        context_.Reset();
        device_.Reset();
    }

    void HandleMediaEvent(DWORD ev) {
        if (!engine_) return;
        switch (ev) {
        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
        case MF_MEDIA_ENGINE_EVENT_CANPLAY:
        case MF_MEDIA_ENGINE_EVENT_FORMATCHANGE:
            EnsureVideoTexture();
            if (pendingStartSeconds_ >= 0.0) {
                const double duration = engine_->GetDuration();
                double target = pendingStartSeconds_;
                if (duration > 0.0) target = std::clamp(target, 0.0, duration);
                engine_->SetCurrentTime(target);
                pendingStartSeconds_ = -1.0;
            }
            if (autoPlayWhenReady_) {
                autoPlayWhenReady_ = false;
                engine_->Play();
            }
            PostMessageW(eventWindow_, WM_APP_PLAYER_READY, 0, 0);
            break;
        case MF_MEDIA_ENGINE_EVENT_ERROR:
            PostMessageW(eventWindow_, WM_APP_MEDIA_ERROR, 0, 0);
            break;
        default:
            break;
        }
    }

    void Render() {
        if (!context_ || !swapChain_ || !renderTarget_) return;
        if (engine_ && nativeW_ && nativeH_) {
            LONGLONG pts = 0;
            if (engine_->OnVideoStreamTick(&pts) == S_OK) {
                EnsureVideoTexture();

                // A 2:1 360 source is ambiguous: it can be a normal mono panorama or
                // two square stereo eyes packed side-by-side.  Inspect a tiny copy of
                // the first decoded frame so stereo VR is not accidentally rendered as
                // one double/mirrored panorama.  This runs only once per opened video.
                if (layoutDetectionPending_ && !layoutDetectionDone_) {
                    const int detected = DetectPackedStereoFromCurrentFrame();
                    if (detected == -2) {
                        // GPU readback is still pending. Do not stall playback and do not
                        // count this frame as a failed stereo-detection attempt.
                    } else if (detected == 1 || detected == 2) {
                        ++layoutDetectionAttempts_;
                        // A positive stereo match wins immediately. Never let a later
                        // frame undo the one-eye decision for this playback session.
                        vrInfo_.layout = detected;
                        vrInfo_.projection = 2;
                        layoutDetectionDone_ = true;
                        layoutDetectionPending_ = false;
                        yaw_ = 0.0f;
                        pitch_ = 0.0f;
                        EnsureVideoTexture();
                    } else {
                        ++layoutDetectionAttempts_;
                        if (detected == 0) ++layoutMonoVotes_;
                        // Do not classify an ambiguous VR video as mono from one frame.
                        // Stereo footage can contain cuts, fades and large disparity.
                        if (layoutDetectionAttempts_ >= 12 && layoutMonoVotes_ >= 4) {
                            vrInfo_.layout = 0;
                            layoutDetectionDone_ = true;
                            layoutDetectionPending_ = false;
                            EnsureVideoTexture();
                        } else if (layoutDetectionAttempts_ >= 24) {
                            // Final safety fallback for genuinely mono/inconclusive files.
                            vrInfo_.layout = 0;
                            layoutDetectionDone_ = true;
                            layoutDetectionPending_ = false;
                            EnsureVideoTexture();
                        }
                    }
                }

                if (videoTexture_) {
                    // Unpack the selected eye during Media Foundation's GPU blit.
                    // This keeps the VR shader working on the full per-eye resolution
                    // instead of uploading a double-wide/double-high stereo texture.
                    MFVideoNormalizedRect src{0.f, 0.f, 1.f, 1.f};
                    if (vrInfo_.layout == 1) src.right = 0.5f;       // SBS: left eye
                    else if (vrInfo_.layout == 2) src.bottom = 0.5f; // TB/OU: top eye
                    RECT dst{0, 0, static_cast<LONG>(eyeW_), static_cast<LONG>(eyeH_)};
                    MFARGB border{0,0,0,255};
                    const HRESULT transferHr=engine_->TransferVideoFrame(videoTexture_.Get(), &src, &dst, &border);
                    if (SUCCEEDED(transferHr) && transitionWaitingForFrame_ && transitionSRV_) {
                        transitionWaitingForFrame_ = false;
                        transitionActive_ = true;
                        transitionStart_ = GetTickCount64();
                    }
                }
            }
        }

        const float clear[4] = {0.008f, 0.010f, 0.014f, 1.0f};
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        context_->ClearRenderTargetView(renderTarget_.Get(), clear);

        if (transitionSRV_ && transitionWaitingForFrame_) {
            // Keep the outgoing frame visible while Media Foundation prepares the next
            // source. This removes the black/disappear gap, especially when switching
            // between flat and VR pipelines.
            DrawVideoSurface(transitionSRV_.Get(), transitionState_, 1.0f);
        } else if (transitionSRV_ && transitionActive_) {
            const ULONGLONG elapsed=GetTickCount64()-transitionStart_;
            const float raw=std::clamp(static_cast<float>(elapsed)/static_cast<float>(kMediaSwitchFadeMs),0.0f,1.0f);
            const float t=raw*raw*(3.0f-2.0f*raw); // smoothstep
            DrawVideoSurface(transitionSRV_.Get(), transitionState_, 1.0f);
            if(videoSRV_) DrawVideoSurface(videoSRV_.Get(), CurrentSurfaceState(), t);
            if(raw>=1.0f) ClearVideoTransition();
        } else if (videoSRV_) {
            DrawVideoSurface(videoSRV_.Get(), CurrentSurfaceState(), 1.0f);
        }

        swapChain_->Present(1, 0);
    }

    void Resize() {
        if (!swapChain_ || !context_) return;

        // A bound RTV keeps a reference to the swap-chain backbuffer. Unbind it
        // before ResizeBuffers or DXGI can reject the resize with INVALID_CALL.
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        renderTarget_.Reset();
        context_->Flush();

        RECT rc{}; GetClientRect(videoWindow_, &rc);
        const UINT w = static_cast<UINT>(std::max<LONG>(1L, rc.right - rc.left));
        const UINT h = static_cast<UINT>(std::max<LONG>(1L, rc.bottom - rc.top));
        if (SUCCEEDED(swapChain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
            CreateBackbuffer();
        }
    }

    void PlayPause() {
        if (!engine_) return;
        if (engine_->IsPaused()) engine_->Play(); else engine_->Pause();
    }
    void Play() { if (engine_) engine_->Play(); }
    void Pause() { if (engine_) engine_->Pause(); }
    bool IsPaused() const { return !engine_ || engine_->IsPaused(); }
    double CurrentTime() const { return engine_ ? engine_->GetCurrentTime() : 0.0; }
    double Duration() const { return engine_ ? engine_->GetDuration() : 0.0; }
    void Seek(double seconds) {
        if (!engine_) return;
        double d = Duration();
        if (d > 0.0) seconds = std::clamp(seconds, 0.0, d);
        engine_->SetCurrentTime(seconds);
    }
    void SetVolume(double v) { if (engine_) engine_->SetVolume(std::clamp(v, 0.0, 1.0)); }
    std::pair<UINT,UINT> NativeSize() const { return {nativeW_, nativeH_}; }
    std::pair<UINT,UINT> EyeSize() const { return {eyeW_, eyeH_}; }
    void SetNativePixelSizing(bool enabled) { nativePixelSizing_ = enabled; if(enabled) ResetFlatZoom(); }
    bool NativePixelSizing() const { return nativePixelSizing_; }
    bool FlatZoomActive() const {
        return !vrInfo_.vr && !nativePixelSizing_ &&
               (std::abs(flatZoom_-1.0f) > 0.001f ||
                std::abs(flatCenterU_-0.5f) > 0.001f ||
                std::abs(flatCenterV_-0.5f) > 0.001f);
    }
    void ResetFlatZoom() { flatZoom_=1.0f; flatCenterU_=0.5f; flatCenterV_=0.5f; }
    static float ClampFlatCenterAxis(float center, float fitScale, float zoom) {
        const float extent=std::max(0.0001f,fitScale*zoom);
        const float half=0.5f/extent;
        const float a=half, b=1.0f-half;
        return std::clamp(center,std::min(a,b),std::max(a,b));
    }
    void ClampFlatCenter(float sx, float sy, float zoom) {
        flatCenterU_=ClampFlatCenterAxis(flatCenterU_,sx,zoom);
        flatCenterV_=ClampFlatCenterAxis(flatCenterV_,sy,zoom);
    }
    bool FlatPointOverMedia(int x, int y) const {
        if(vrInfo_.vr || nativePixelSizing_ || !videoWindow_ || !eyeW_ || !eyeH_) return false;
        RECT rc{}; GetClientRect(videoWindow_,&rc);
        const float clientW=static_cast<float>(std::max<LONG>(1L,rc.right-rc.left));
        const float clientH=static_cast<float>(std::max<LONG>(1L,rc.bottom-rc.top));
        const float viewportAspect=clientW/clientH;
        const float sourceAspect=static_cast<float>(eyeW_)/static_cast<float>(eyeH_);
        float sx=1.0f,sy=1.0f;
        if(viewportAspect>sourceAspect) sx=sourceAspect/viewportAspect;
        else sy=viewportAspect/sourceAspect;
        sx=std::max(0.0001f,sx); sy=std::max(0.0001f,sy);
        const float zoom=std::clamp(flatZoom_,0.25f,8.0f);

        // Match the pixel shader exactly. These are the screen-space bounds where
        // source UVs remain inside [0,1]; everything outside is the black surround.
        const float left=clientW*(0.5f-flatCenterU_*sx*zoom);
        const float right=clientW*(0.5f+(1.0f-flatCenterU_)*sx*zoom);
        const float top=clientH*(0.5f-flatCenterV_*sy*zoom);
        const float bottom=clientH*(0.5f+(1.0f-flatCenterV_)*sy*zoom);
        const float px=static_cast<float>(x), py=static_cast<float>(y);
        return px>=std::max(0.0f,left) && px<std::min(clientW,right) &&
               py>=std::max(0.0f,top) && py<std::min(clientH,bottom);
    }
    void FlatWheelZoom(short delta, int x, int y) {
        if(vrInfo_.vr || nativePixelSizing_ || !videoWindow_ || !eyeW_ || !eyeH_ || delta==0) return;
        RECT rc{}; GetClientRect(videoWindow_,&rc);
        const float clientW=static_cast<float>(std::max<LONG>(1L,rc.right-rc.left));
        const float clientH=static_cast<float>(std::max<LONG>(1L,rc.bottom-rc.top));
        const float viewportAspect=clientW/clientH;
        const float sourceAspect=static_cast<float>(eyeW_)/static_cast<float>(eyeH_);
        float sx=1.0f,sy=1.0f;
        if(viewportAspect>sourceAspect) sx=sourceAspect/viewportAspect;
        else sy=viewportAspect/sourceAspect;
        sx=std::max(0.0001f,sx); sy=std::max(0.0001f,sy);

        const float oldZoom=std::clamp(flatZoom_,0.25f,8.0f);
        const float factor=std::pow(1.15f,static_cast<float>(delta)/120.0f);
        const float newZoom=std::clamp(oldZoom*factor,0.25f,8.0f);
        if(std::abs(newZoom-1.0f)<=0.001f){ ResetFlatZoom(); return; }

        const float clampedX=std::clamp(static_cast<float>(x),0.0f,clientW);
        const float clampedY=std::clamp(static_cast<float>(y),0.0f,clientH);
        const float px=(clampedX/clientW)*2.0f-1.0f;
        const float py=(clampedY/clientH)*2.0f-1.0f;
        const float baseU=(px/sx)*0.5f+0.5f;
        const float baseV=(py/sy)*0.5f+0.5f;
        const float sourceU=flatCenterU_+(baseU-0.5f)/oldZoom;
        const float sourceV=flatCenterV_+(baseV-0.5f)/oldZoom;
        float newCenterU=sourceU-(baseU-0.5f)/newZoom;
        float newCenterV=sourceV-(baseV-0.5f)/newZoom;

        flatZoom_=newZoom; flatCenterU_=newCenterU; flatCenterV_=newCenterV;
        // Above fit this prevents revealing black through a cropped axis. Below fit it
        // keeps the smaller media fully inside the client area while still allowing it
        // to be positioned anywhere within the available black surround.
        ClampFlatCenter(sx,sy,newZoom);
    }
    const VRInfo& VR() const { return vrInfo_; }
    int EffectiveProjection() const {
        return projectionOverride_ != 0 ? projectionOverride_ : vrInfo_.projection;
    }
    bool IsVr360Enabled() const { return vrInfo_.vr && EffectiveProjection() == 1; }
    bool IsMirroredBack360() const {
        // Stereo-packed VR content is fundamentally front-facing 180 data.
        // When the user enables 360, mirror the back hemisphere instead of
        // stretching the front 180 across the full sphere.
        return vrInfo_.vr && vrInfo_.layout != 0 && EffectiveProjection() == 1;
    }
    void ToggleVrBackside() {
        if (!vrInfo_.vr) return;

        // Keep the user's live 180/360 choice separate from automatic VR detection.
        // This prevents a later layout/projection probe from silently undoing the toggle.
        if (EffectiveProjection() == 1) {
            projectionOverride_ = 2;
            while (yaw_ > PI_F) yaw_ -= 2.0f * PI_F;
            while (yaw_ < -PI_F) yaw_ += 2.0f * PI_F;
            if (std::abs(yaw_) > (PI_F * 0.5f)) yaw_ = 0.0f;
        } else {
            projectionOverride_ = 1;
        }
    }

    void BeginDrag(int x, int y) {
        // Preserve VR mouse-look. Flat media can be repositioned at any wheel zoom,
        // including below fit-to-window, but a drag may start only on actual media
        // pixels. The black surround is deliberately not draggable.
        if (!vrInfo_.vr && !FlatPointOverMedia(x,y)) return;
        dragging_ = true; lastX_ = x; lastY_ = y; SetCapture(videoWindow_);
    }
    void Drag(int x, int y) {
        if (!dragging_) return;
        const int dx = x - lastX_, dy = y - lastY_;
        lastX_ = x; lastY_ = y;
        if (vrInfo_.vr) {
            // VR mouse-look: keep the approved horizontal direction, with gentler vertical movement.
            yaw_ -= dx * 0.0032f;
            // Vertical direction is intentionally opposite to the horizontal grab direction.
            pitch_ = std::clamp(pitch_ - dy * 0.0026f, -1.48f, 1.48f);
            return;
        }
        if (nativePixelSizing_ || !videoWindow_ || !eyeW_ || !eyeH_) return;
        RECT rc{}; GetClientRect(videoWindow_,&rc);
        const float clientW=static_cast<float>(std::max<LONG>(1L,rc.right-rc.left));
        const float clientH=static_cast<float>(std::max<LONG>(1L,rc.bottom-rc.top));
        const float viewportAspect=clientW/clientH;
        const float sourceAspect=static_cast<float>(eyeW_)/static_cast<float>(eyeH_);
        float sx=1.0f,sy=1.0f;
        if(viewportAspect>sourceAspect) sx=sourceAspect/viewportAspect;
        else sy=viewportAspect/sourceAspect;
        sx=std::max(0.0001f,sx); sy=std::max(0.0001f,sy);
        const float zoom=std::clamp(flatZoom_,0.25f,8.0f);
        // Grab-style panning: dragging the picture right/down moves the picture with
        // the pointer, which means sampling a little farther left/up in source space.
        flatCenterU_-=static_cast<float>(dx)/(clientW*sx*zoom);
        flatCenterV_-=static_cast<float>(dy)/(clientH*sy*zoom);
        ClampFlatCenter(sx,sy,zoom);
    }
    void EndDrag() {
        if (!dragging_) return;
        dragging_ = false;
        if (GetCapture() == videoWindow_) ReleaseCapture();
    }
    void CancelDrag() { dragging_ = false; }
    void Wheel(short delta) {
        if (!vrInfo_.vr) return;
        float deg = fovRadians_ * 180.f / PI_F;
        deg = std::clamp(deg - (delta / 120.f) * 5.f, 35.f, 110.f);
        fovRadians_ = deg * PI_F / 180.f;
    }

private:
    static constexpr ULONGLONG kMediaSwitchFadeMs = 220;
    struct SurfaceState {
        VRInfo vr{};
        int projectionOverride = 0;
        float yaw = 0.0f, pitch = 0.0f, fov = 65.0f * PI_F / 180.0f;
        UINT eyeW = 0, eyeH = 0;
        bool nativePixelSizing = false;
        float flatZoom = 1.0f, flatCenterU = 0.5f, flatCenterV = 0.5f;
    };
    struct Vertex { float x,y,u,v; };
    struct ShaderConstants {
        float yaw, pitch, fov, vrMode;
        float layout, projection, sourceAspect, viewportAspect;
        float mirrorBack, pad0, pad1, pad2;
    };

    SurfaceState CurrentSurfaceState() const {
        SurfaceState s;
        s.vr=vrInfo_; s.projectionOverride=projectionOverride_;
        s.yaw=yaw_; s.pitch=pitch_; s.fov=fovRadians_;
        s.eyeW=eyeW_; s.eyeH=eyeH_;
        s.nativePixelSizing=nativePixelSizing_;
        s.flatZoom=flatZoom_; s.flatCenterU=flatCenterU_; s.flatCenterV=flatCenterV_;
        return s;
    }

    void ClearVideoTransition() {
        transitionTexture_.Reset(); transitionSRV_.Reset();
        transitionWaitingForFrame_=false; transitionActive_=false; transitionStart_=0;
    }

    void DrawVideoSurface(ID3D11ShaderResourceView* srv,const SurfaceState& s,float opacity) {
        if(!srv || !context_ || !videoWindow_ || !s.eyeW || !s.eyeH) return;
        RECT rc{}; GetClientRect(videoWindow_,&rc);
        const float clientW=static_cast<float>(std::max<LONG>(1L,rc.right-rc.left));
        const float clientH=static_cast<float>(std::max<LONG>(1L,rc.bottom-rc.top));
        float renderW=clientW,renderH=clientH,renderX=0.0f,renderY=0.0f;
        if(s.nativePixelSizing && !s.vr.vr){
            const float sourceW=static_cast<float>(s.eyeW),sourceH=static_cast<float>(s.eyeH);
            // Native Size is literal 1:1 pixel mapping. Windowed mode is prevented from
            // becoming smaller than this surface; fullscreen may clip an oversized source
            // rather than silently scaling it below native dimensions.
            renderW=std::max(1.0f,sourceW);
            renderH=std::max(1.0f,sourceH);
            renderX=std::floor((clientW-renderW)*0.5f);
            renderY=std::floor((clientH-renderH)*0.5f);
        }
        D3D11_VIEWPORT vp{}; vp.TopLeftX=renderX;vp.TopLeftY=renderY;vp.Width=renderW;vp.Height=renderH;vp.MinDepth=0.f;vp.MaxDepth=1.f;
        context_->RSSetViewports(1,&vp);
        const int projection=s.projectionOverride!=0?s.projectionOverride:s.vr.projection;
        ShaderConstants c{};
        c.yaw=s.yaw;c.pitch=s.pitch;c.fov=s.fov;c.vrMode=s.vr.vr?1.0f:0.0f;
        c.layout=std::clamp(opacity,0.0f,1.0f);
        c.projection=static_cast<float>(projection);
        c.sourceAspect=s.eyeH?static_cast<float>(s.eyeW)/static_cast<float>(s.eyeH):1.0f;
        c.viewportAspect=vp.Height>0.f?vp.Width/vp.Height:1.0f;
        c.mirrorBack=(s.vr.vr && s.vr.layout!=0 && projection==1)?1.0f:0.0f;
        c.pad0=(!s.vr.vr && !s.nativePixelSizing)?s.flatZoom:1.0f;
        c.pad1=s.flatCenterU;c.pad2=s.flatCenterV;
        context_->UpdateSubresource(constantBuffer_.Get(),0,nullptr,&c,0,0);
        const float blendFactor[4]={0,0,0,0};
        context_->OMSetBlendState(blendState_.Get(),blendFactor,0xffffffffu);
        UINT stride=sizeof(Vertex),offset=0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->IASetVertexBuffers(0,1,vertexBuffer_.GetAddressOf(),&stride,&offset);
        context_->VSSetShader(vs_.Get(),nullptr,0);
        context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->PSSetShaderResources(0,1,&srv);
        context_->PSSetSamplers(0,1,sampler_.GetAddressOf());
        context_->PSSetConstantBuffers(0,1,constantBuffer_.GetAddressOf());
        context_->Draw(6,0);
        ID3D11ShaderResourceView* nullSrv=nullptr;
        context_->PSSetShaderResources(0,1,&nullSrv);
    }

    int DetectPackedStereoFromCurrentFrame() {
        if (!engine_ || !device_ || !context_ || !nativeW_ || !nativeH_) return -1;

        // The packed-stereo probe is intentionally asynchronous. A blocking Map() here
        // can stall the UI for high-resolution VR frames while the GPU finishes the copy.
        // We submit the tiny readback on one frame and inspect it on a later frame with
        // D3D11_MAP_FLAG_DO_NOT_WAIT. -2 means "not ready yet".
        const float aspect = static_cast<float>(nativeW_) / static_cast<float>(nativeH_);
        UINT probeW = 256u;
        UINT probeH = static_cast<UINT>(std::clamp<int>(static_cast<int>(std::lround(256.0f / std::max(0.01f, aspect))), 64, 256));
        if (aspect < 1.0f) {
            probeH = 256u;
            probeW = static_cast<UINT>(std::clamp<int>(static_cast<int>(std::lround(256.0f * aspect)), 64, 256));
        }
        probeW = std::max<UINT>(4u, probeW & ~1u);
        probeH = std::max<UINT>(4u, probeH & ~1u);

        if (stereoProbeW_ != probeW || stereoProbeH_ != probeH) {
            stereoProbeGpu_.Reset();
            stereoProbeStaging_.Reset();
            stereoProbePending_ = false;
            stereoProbeW_ = probeW;
            stereoProbeH_ = probeH;
        }

        if (!stereoProbeGpu_ || !stereoProbeStaging_) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = probeW;
            td.Height = probeH;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(device_->CreateTexture2D(&td, nullptr, &stereoProbeGpu_))) return -1;

            D3D11_TEXTURE2D_DESC sd = td;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(&sd, nullptr, &stereoProbeStaging_))) {
                stereoProbeGpu_.Reset();
                return -1;
            }
        }

        if (!stereoProbePending_) {
            MFVideoNormalizedRect src{0.f, 0.f, 1.f, 1.f};
            RECT dst{0, 0, static_cast<LONG>(probeW), static_cast<LONG>(probeH)};
            MFARGB border{0,0,0,255};
            if (FAILED(engine_->TransferVideoFrame(stereoProbeGpu_.Get(), &src, &dst, &border))) return -1;
            context_->CopyResource(stereoProbeStaging_.Get(), stereoProbeGpu_.Get());
            stereoProbePending_ = true;
            return -2;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT mapHr = context_->Map(stereoProbeStaging_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (mapHr == DXGI_ERROR_WAS_STILL_DRAWING) return -2;
        if (FAILED(mapHr)) {
            stereoProbePending_ = false;
            return -1;
        }

        auto luma = [](const BYTE* p) -> double {
            return 0.0722*p[0] + 0.7152*p[1] + 0.2126*p[2]; // BGRA
        };
        struct Metric { double mad=1e9,corr=-1.0,contrast=0.0; };
        auto measureLR = [&]() -> Metric {
            const UINT halfW=probeW/2u;
            const int maxShift=std::clamp(static_cast<int>(halfW/10u),3,24);
            Metric best{};
            constexpr int sxCount=20,syCount=12;
            for(int shift=-maxShift;shift<=maxShift;++shift){
                double a=0,b=0,aa=0,bb=0,ab=0,mad=0; int count=0;
                for(int gy=0;gy<syCount;++gy){
                    const UINT y=std::min<UINT>(probeH-1u,static_cast<UINT>((gy+0.5)*probeH/syCount));
                    for(int gx=0;gx<sxCount;++gx){
                        const UINT x=std::min<UINT>(halfW-1u,static_cast<UINT>((gx+0.5)*halfW/sxCount));
                        const int bx=std::clamp(static_cast<int>(x+halfW)+shift,static_cast<int>(halfW),static_cast<int>(probeW)-1);
                        const BYTE* pa=static_cast<const BYTE*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch+static_cast<size_t>(x)*4u;
                        const BYTE* pb=static_cast<const BYTE*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch+static_cast<size_t>(bx)*4u;
                        const double av=luma(pa),bv=luma(pb); a+=av;b+=bv;aa+=av*av;bb+=bv*bv;ab+=av*bv;mad+=std::abs(av-bv);++count;
                    }
                }
                if(count<24) continue;
                const double ma=a/count,mb=b/count,va=std::max(0.0,aa/count-ma*ma),vb=std::max(0.0,bb/count-mb*mb);
                const double denom=std::sqrt(va*vb),corr=denom>1e-6?(ab/count-ma*mb)/denom:-1.0;
                const Metric m{mad/count,corr,std::sqrt(std::max(0.0,(va+vb)*0.5))};
                if(m.corr>best.corr || (std::abs(m.corr-best.corr)<0.015 && m.mad<best.mad)) best=m;
            }
            return best;
        };
        auto measureTB = [&]() -> Metric {
            const UINT halfH=probeH/2u;
            const int maxShift=std::clamp(static_cast<int>(probeW/20u),3,24);
            Metric best{};
            constexpr int sxCount=20,syCount=12;
            for(int shift=-maxShift;shift<=maxShift;++shift){
                double a=0,b=0,aa=0,bb=0,ab=0,mad=0; int count=0;
                for(int gy=0;gy<syCount;++gy){
                    const UINT y=std::min<UINT>(halfH-1u,static_cast<UINT>((gy+0.5)*halfH/syCount));
                    for(int gx=0;gx<sxCount;++gx){
                        const UINT x=std::min<UINT>(probeW-1u,static_cast<UINT>((gx+0.5)*probeW/sxCount));
                        const int bx=std::clamp(static_cast<int>(x)+shift,0,static_cast<int>(probeW)-1);
                        const BYTE* pa=static_cast<const BYTE*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch+static_cast<size_t>(x)*4u;
                        const BYTE* pb=static_cast<const BYTE*>(mapped.pData)+static_cast<size_t>(y+halfH)*mapped.RowPitch+static_cast<size_t>(bx)*4u;
                        const double av=luma(pa),bv=luma(pb); a+=av;b+=bv;aa+=av*av;bb+=bv*bv;ab+=av*bv;mad+=std::abs(av-bv);++count;
                    }
                }
                if(count<24) continue;
                const double ma=a/count,mb=b/count,va=std::max(0.0,aa/count-ma*ma),vb=std::max(0.0,bb/count-mb*mb);
                const double denom=std::sqrt(va*vb),corr=denom>1e-6?(ab/count-ma*mb)/denom:-1.0;
                const Metric m{mad/count,corr,std::sqrt(std::max(0.0,(va+vb)*0.5))};
                if(m.corr>best.corr || (std::abs(m.corr-best.corr)<0.015 && m.mad<best.mad)) best=m;
            }
            return best;
        };
        auto likely=[](const Metric& m){
            if(m.contrast<5.0) return false;
            return (m.corr>=0.72&&m.mad<=72.0)||(m.corr>=0.58&&m.mad<=42.0);
        };

        const Metric lr=measureLR();
        const Metric tb=measureTB();
        context_->Unmap(stereoProbeStaging_.Get(), 0);
        stereoProbePending_ = false;
        if(likely(lr) && (!likely(tb) || lr.corr>tb.corr+0.05)) return 1;
        if(likely(tb) && (!likely(lr) || tb.corr>lr.corr+0.05)) return 2;
        if(lr.contrast<7.0 && tb.contrast<7.0) return -1; // fade/black/flat frame: retry on a later frame
        return 0;
    }

    HRESULT CreateMediaEngine() {
        ComPtr<IMFMediaEngineClassFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;
        notify_.Attach(new MediaEngineNotify(eventWindow_));

        ComPtr<IMFAttributes> attrs;
        hr = MFCreateAttributes(&attrs, 4);
        if (FAILED(hr)) return hr;
        attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify_.Get());
        attrs->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
        attrs->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, dxgiManager_.Get());

        hr = factory->CreateInstance(0, attrs.Get(), &engine_);
        if (FAILED(hr)) return hr;
        engine_->SetAutoPlay(FALSE);
        engine_->SetVolume(0.30);
        return S_OK;
    }

    HRESULT CreateBackbuffer() {
        ComPtr<ID3D11Texture2D> back;
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&back));
        if (FAILED(hr)) return hr;
        return device_->CreateRenderTargetView(back.Get(), nullptr, &renderTarget_);
    }

    HRESULT Compile(const char* src, const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
        ComPtr<ID3DBlob> errors;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
        HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, flags, 0, &blob, &errors);
        if (FAILED(hr) && errors) {
            std::string e(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            MessageBoxA(eventWindow_, e.c_str(), "Shader compile error", MB_ICONERROR);
        }
        return hr;
    }

    HRESULT CreatePipeline() {
        static const char* hlsl = R"HLSL(
Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);
cbuffer C : register(b0) {
    float yaw;
    float pitch;
    float fov;
    float vrMode;
    float layout;
    float projection;
    float sourceAspect;
    float viewportAspect;
    float mirrorBack;
    float pad0;
    float pad1;
    float pad2;
};
struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
PSIn VSMain(VSIn i) { PSIn o; o.pos=float4(i.pos,0,1); o.uv=i.uv; return o; }
float4 PSMain(PSIn i) : SV_TARGET {
    if (vrMode < 0.5) {
        float2 p = i.uv * 2.0 - 1.0;
        float sx = 1.0, sy = 1.0;
        if (viewportAspect > sourceAspect) sx = sourceAspect / viewportAspect;
        else sy = viewportAspect / sourceAspect;
        float2 baseUv = float2(p.x / sx, p.y / sy) * 0.5 + 0.5;
        float zoom = clamp(pad0, 0.25, 8.0);
        float2 uv = float2(pad1, pad2) + (baseUv - 0.5) / zoom;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return float4(0,0,0,layout);
        float4 sampled = tex0.Sample(samp0, uv);
        return float4(sampled.rgb, layout);
    }

    float2 ndc = i.uv * 2.0 - 1.0;
    float tanHalf = tan(fov * 0.5);
    float3 d = normalize(float3(ndc.x * viewportAspect * tanHalf, -ndc.y * tanHalf, 1.0));

    float cp = cos(pitch), sp = sin(pitch);
    d = float3(d.x, cp*d.y - sp*d.z, sp*d.y + cp*d.z);
    float cy = cos(yaw), syaw = sin(yaw);
    d = float3(cy*d.x + syaw*d.z, d.y, -syaw*d.x + cy*d.z);

    float lon = atan2(d.x, d.z);
    float lat = asin(clamp(d.y, -1.0, 1.0));
    float2 uv;
    if (projection > 1.5) {
        if (abs(lon) > 1.57079632679) return float4(0,0,0,layout);
        uv.x = lon / 3.14159265359 + 0.5;
    } else {
        if (mirrorBack > 0.5) {
            if (lon > 1.57079632679) lon = 3.14159265359 - lon;
            else if (lon < -1.57079632679) lon = -3.14159265359 - lon;
            uv.x = lon / 3.14159265359 + 0.5;
        } else {
            uv.x = frac(lon / 6.28318530718 + 0.5);
        }
    }
    uv.y = 0.5 - lat / 3.14159265359;

    float4 sampled = tex0.Sample(samp0, uv);
    return float4(sampled.rgb, layout);
}
)HLSL";
        ComPtr<ID3DBlob> vsBlob, psBlob;
        HRESULT hr = Compile(hlsl, "VSMain", "vs_4_0", vsBlob);
        if (FAILED(hr)) return hr;
        hr = Compile(hlsl, "PSMain", "ps_4_0", psBlob);
        if (FAILED(hr)) return hr;
        hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
        if (FAILED(hr)) return hr;
        hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
        if (FAILED(hr)) return hr;

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        hr = device_->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout_);
        if (FAILED(hr)) return hr;

        Vertex v[] = {
            {-1.f,-1.f,0.f,1.f}, {-1.f,1.f,0.f,0.f}, {1.f,1.f,1.f,0.f},
            {-1.f,-1.f,0.f,1.f}, {1.f,1.f,1.f,0.f}, {1.f,-1.f,1.f,1.f}
        };
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(v); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = v;
        hr = device_->CreateBuffer(&bd, &init, &vertexBuffer_);
        if (FAILED(hr)) return hr;

        D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = sizeof(ShaderConstants); cbd.Usage = D3D11_USAGE_DEFAULT; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device_->CreateBuffer(&cbd, nullptr, &constantBuffer_);
        if (FAILED(hr)) return hr;

        D3D11_SAMPLER_DESC sd{};
        // VR projection is highly non-linear, especially near the poles.
        // 16x anisotropic filtering preserves considerably more source detail
        // than the old bilinear-only sampler.
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.MaxAnisotropy = 16;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr=device_->CreateSamplerState(&sd, &sampler_);
        if(FAILED(hr)) return hr;

        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].BlendEnable=TRUE;
        blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        return device_->CreateBlendState(&blend,&blendState_);
    }

    HRESULT EnsureVideoTexture() {
        if (!engine_) return E_FAIL;
        DWORD w=0,h=0;
        HRESULT hr = engine_->GetNativeVideoSize(&w,&h);
        if (FAILED(hr) || !w || !h) return hr;

        // A filename ending in "360" or "360 (number)" identifies the projection,
        // not necessarily the stereo packing.  Obvious 4:1 SBS and ~1:1 TB sources
        // can be resolved from aspect ratio.  A ~2:1 360 source is ambiguous because
        // it can be either a mono panorama or two square stereo eyes side-by-side;
        // defer that case to a tiny first-frame content probe in Render().
        if (vrInfo_.vr && !vrInfo_.layoutExplicit && !layoutDetectionDone_) {
            const float aspect = static_cast<float>(w) / static_cast<float>(h);
            layoutDetectionPending_ = false;
            if (vrInfo_.projection == 1) {
                if (aspect >= 3.20f) {
                    vrInfo_.layout = 1;
                    vrInfo_.projection = 2;
                    layoutDetectionDone_ = true;
                } else if (aspect <= 1.20f) {
                    vrInfo_.layout = 2;
                    vrInfo_.projection = 2;
                    layoutDetectionDone_ = true;
                } else {
                    vrInfo_.layout = 0;
                    layoutDetectionPending_ = true;
                }
            } else if (vrInfo_.projection == 2) {
                if (aspect >= 1.70f && aspect < 3.20f) vrInfo_.layout = 1;
                else if (aspect <= 0.70f) vrInfo_.layout = 2;
                else vrInfo_.layout = 0;
                if (vrInfo_.layout != 0) vrInfo_.projection = 2;
                layoutDetectionDone_ = true;
            }
        }

        UINT newEyeW = static_cast<UINT>(w);
        UINT newEyeH = static_cast<UINT>(h);
        if (vrInfo_.layout == 1) newEyeW = std::max<UINT>(1u, static_cast<UINT>(w) / 2u);
        else if (vrInfo_.layout == 2) newEyeH = std::max<UINT>(1u, static_cast<UINT>(h) / 2u);

        if (videoTexture_ && w == nativeW_ && h == nativeH_ && newEyeW == eyeW_ && newEyeH == eyeH_) return S_OK;
        nativeW_ = static_cast<UINT>(w);
        nativeH_ = static_cast<UINT>(h);
        eyeW_ = newEyeW;
        eyeH_ = newEyeH;
        videoSRV_.Reset(); videoTexture_.Reset();

        D3D11_TEXTURE2D_DESC td{};
        td.Width = eyeW_; td.Height = eyeH_; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = device_->CreateTexture2D(&td, nullptr, &videoTexture_);
        if (FAILED(hr)) return hr;
        return device_->CreateShaderResourceView(videoTexture_.Get(), nullptr, &videoSRV_);
    }

    HWND eventWindow_{};
    HWND videoWindow_{};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> inputLayout_;
    ComPtr<ID3D11Buffer> vertexBuffer_;
    ComPtr<ID3D11Buffer> constantBuffer_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blendState_;
    ComPtr<ID3D11Texture2D> videoTexture_;
    ComPtr<ID3D11ShaderResourceView> videoSRV_;
    ComPtr<ID3D11Texture2D> transitionTexture_;
    ComPtr<ID3D11ShaderResourceView> transitionSRV_;
    SurfaceState transitionState_{};
    bool transitionWaitingForFrame_=false,transitionActive_=false;
    ULONGLONG transitionStart_=0;
    ComPtr<ID3D11Texture2D> stereoProbeGpu_;
    ComPtr<ID3D11Texture2D> stereoProbeStaging_;
    UINT stereoProbeW_ = 0, stereoProbeH_ = 0;
    bool stereoProbePending_ = false;
    ComPtr<IMFDXGIDeviceManager> dxgiManager_;
    UINT dxgiResetToken_{};
    ComPtr<IMFMediaEngineNotify> notify_;
    ComPtr<IMFMediaEngine> engine_;
    std::wstring path_;
    VRInfo vrInfo_{};
    double pendingStartSeconds_ = -1.0;
    UINT nativeW_ = 0, nativeH_ = 0;
    UINT eyeW_ = 0, eyeH_ = 0;
    bool autoPlayWhenReady_ = false;
    bool layoutDetectionDone_ = false;
    bool layoutDetectionPending_ = false;
    int layoutDetectionAttempts_ = 0;
    int layoutMonoVotes_ = 0;
    int projectionOverride_ = 0; // 0=automatic, 1=force 360, 2=force front-only 180
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    float yaw_ = 0.f, pitch_ = 0.f, fovRadians_ = 65.f * PI_F / 180.f;
    float flatZoom_ = 1.0f, flatCenterU_ = 0.5f, flatCenterV_ = 0.5f;
    bool nativePixelSizing_ = false;
};

class App {
public:
    static constexpr int kDefaultLibraryCardWidth = 340;
    static constexpr int kMinLibraryCardWidth = 200;
    static constexpr int kLibraryTitleHeight = 45;
    static constexpr int kLibraryGap = 16;
    static constexpr int kLibraryPad = 20;
    static constexpr int kLibraryScrollbarReserve = 18;

    static int FourAcrossLibraryMaxWidth(int clientWidth) {
        // Maximum zoom is always the largest card width that still fits four cards
        // horizontally in the current Library viewport.
        const int usable = std::max(1, clientWidth - kLibraryScrollbarReserve - kLibraryPad * 2 - kLibraryGap * 3);
        return std::max(kMinLibraryCardWidth, usable / 4);
    }

    static int EightAcrossFullscreenLibraryWidth(int clientWidth) {
        // On a 4K-class fullscreen viewport, fill the Library with exactly eight
        // cards across.  This uses the same margins/gaps as the normal grid.
        const int usable = std::max(1, clientWidth - kLibraryScrollbarReserve - kLibraryPad * 2 - kLibraryGap * 7);
        return std::max(kMinLibraryCardWidth, usable / 8);
    }

    bool UseEightAcrossFullscreenLibrary(int clientWidth) const {
        return fullscreen_ && clientWidth >= 3600;
    }

    void ApplyLibraryWidthForViewport(int clientWidth) {
        const int maxWidth = FourAcrossLibraryMaxWidth(clientWidth);
        if (UseEightAcrossFullscreenLibrary(clientWidth) && !fullscreenLibraryZoomOverridden_)
            libraryCardWidth_ = EightAcrossFullscreenLibraryWidth(clientWidth);
        else
            libraryCardWidth_ = std::clamp(libraryCardWidth_, kMinLibraryCardWidth, maxWidth);
    }

    int LibraryWheelPixelsPerNotch() const {
        // Use exactly the same wheel distance in windowed and fullscreen Library views.
        // Fullscreen may use a different card layout, but scrolling itself is identical.
        return 120;
    }

    int DetailsWheelPixelsPerNotch(int clientWidth) const {
        const int cardW=DetailsPreviewCardWidthForViewport(clientWidth);
        const int imageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int rowStride=imageH+24+12;
        const int defaultImageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(kDefaultPreviewCardWidth)*9.0/16.0)));
        const int defaultStride=defaultImageH+24+12;
        return std::max(72,static_cast<int>(std::lround(120.0*static_cast<double>(rowStride)/static_cast<double>(std::max(1,defaultStride)))));
    }

    static int ConsumeWheelPixels(short wheelDelta,int pixelsPerNotch,double& remainder) {
        remainder += static_cast<double>(wheelDelta)*static_cast<double>(pixelsPerNotch)/static_cast<double>(WHEEL_DELTA);
        const int pixels = remainder>=0.0 ? static_cast<int>(std::floor(remainder)) : static_cast<int>(std::ceil(remainder));
        remainder -= static_cast<double>(pixels);
        return pixels;
    }

    static int FourAcrossPreviewMaxWidth(int clientWidth) {
        // Same rule for Info secondary previews: maximum zoom still shows four
        // preview cards side-by-side in the current window.
        constexpr int sideMargins = 80;
        constexpr int previewGap = 12;
        constexpr int minPreviewWidth = 140;
        const int usable = std::max(1, clientWidth - sideMargins - previewGap * 3);
        return std::max(minPreviewWidth, usable / 4);
    }

    static int TenAcrossFullscreenPreviewWidth(int clientWidth) {
        constexpr int sideMargins = 80;
        constexpr int previewGap = 12;
        constexpr int minPreviewWidth = 140;
        const int usable = std::max(1, clientWidth - sideMargins - previewGap * 9);
        return std::max(minPreviewWidth, usable / 10);
    }

    static int SevenAcrossWindowedPreviewWidth(int clientWidth) {
        constexpr int sideMargins = 80;
        constexpr int previewGap = 12;
        constexpr int minPreviewWidth = 140;
        const int usable = std::max(1, clientWidth - sideMargins - previewGap * 6);
        return std::max(minPreviewWidth, usable / 7);
    }

    bool UseTenAcrossFullscreenPreviews(int clientWidth) const {
        return fullscreen_ && clientWidth >= 3600;
    }

    int DetailsPreviewCardWidthForViewport(int clientWidth) const {
        // Fullscreen and windowed layouts have sensible defaults (10 / 7 across),
        // but Ctrl+wheel may override either default for the current Info session.
        if (UseTenAcrossFullscreenPreviews(clientWidth) && !previewZoomOverridden_)
            return TenAcrossFullscreenPreviewWidth(clientWidth);
        if (!fullscreen_ && !previewZoomOverridden_)
            return SevenAcrossWindowedPreviewWidth(clientWidth);
        return previewCardWidth_;
    }

    enum class Mode { Library, Details, Player };
    enum class Category { Videos, Images };

    struct PrefetchedPreviewSet {
        std::vector<PreviewFrame> frames;
        double duration = 0.0;
    };

    struct DetailPrefetchJob {
        uint64_t generation = 0;
        Category category = Category::Videos;
        size_t index = 0;
        std::wstring mediaPath;
        std::wstring bannerPath;
        std::wstring previewDir;
        bool isVideo = false;
        bool loadBanner = false;
        bool loadPreviews = false;
    };

    struct DetailPrefetchResult {
        uint64_t generation = 0;
        Category category = Category::Videos;
        size_t index = 0;
        std::wstring mediaPath;
        HBITMAP banner = nullptr;
        int bannerW = 0;
        int bannerH = 0;
        bool hasPreviewSet = false;
        PrefetchedPreviewSet previewSet;
    };

    bool Initialize(HINSTANCE inst) {
        inst_ = inst;
        INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_BAR_CLASSES}; InitCommonControlsEx(&ic);
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) return false;
        comInitialized_=true;
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) return false;
        mfStarted_=true;

        Gdiplus::GdiplusStartupInput gdiplusInput;
        if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusInput, nullptr) != Gdiplus::Ok) return false;
        LoadUiIcons();

        WNDCLASSW wc{};
        wc.hInstance = inst_;
        wc.lpszClassName = L"VisualMediaPlayerMain";
        wc.lpfnWndProc = MainWndProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        wc.hbrBackground = CreateSolidBrush(RGB(13,15,20));
        RegisterClassW(&wc);

        WNDCLASSW vc{};
        vc.hInstance = inst_;
        vc.lpszClassName = L"VisualMediaPlayerVideo";
        vc.lpfnWndProc = VideoWndProc;
        vc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        vc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&vc);

        WNDCLASSW cc{};
        cc.hInstance = inst_;
        cc.lpszClassName = L"VisualMediaPlayerControls";
        cc.lpfnWndProc = ControlsWndProc;
        cc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cc.hbrBackground = CreateSolidBrush(RGB(16,18,24));
        RegisterClassW(&cc);

        WNDCLASSW ec{};
        ec.hInstance = inst_;
        ec.lpszClassName = L"VisualMediaPlayerEdgeArrow";
        ec.lpfnWndProc = EdgeArrowWndProc;
        ec.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        ec.hbrBackground = nullptr;
        RegisterClassW(&ec);

        // Standard window size is half of the monitor's fullscreen dimensions in
        // each axis. On a 3840x2160 display this is 1920x1080; other displays scale
        // proportionally instead of using a hard-coded QHD restore size.
        const int monitorW = std::max(1, GetSystemMetrics(SM_CXSCREEN));
        const int monitorH = std::max(1, GetSystemMetrics(SM_CYSCREEN));
        const int initialW = std::max(1, monitorW / 2);
        const int initialH = std::max(1, monitorH / 2);
        // Keep the geometric center of the window exactly on the geometric center
        // of the physical screen. Do not center in the work area, because a taskbar
        // would shift the window slightly away from the true screen center.
        const int initialX = (monitorW - initialW) / 2;
        const int initialY = (monitorH - initialH) / 2;
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Visual MediaPlayer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            initialX, initialY, initialW, initialH, nullptr, nullptr, inst_, this);
        if (!hwnd_) return false;
        HICON appIconBig = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        HICON appIconSmall = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (appIconBig) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIconBig));
        if (appIconSmall) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIconSmall));
        BOOL darkTitle = TRUE;
        DwmSetWindowAttribute(hwnd_, 20, &darkTitle, sizeof(darkTitle));
        ApplyMainWindowCornerPreference();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        StartLibraryThumbLoader();
        StartResolutionMetadataWorker();

        // Register this exact EXE path as an "Open with Visual MediaPlayer" handler.
        // Windows still lets the user choose whether it becomes the default app.
        RegisterOpenWith();

        LoadSettings();
        SetTimer(hwnd_,kLibraryAccessRetryTimerId,1000,nullptr);
        if (!folder_.empty()) {
            if (IsLibraryRootAccessible()) Scan();
            else { libraryUnavailableLatched_=true; libraryAccessFailCount_=3; }
        }
        return true;
    }

    int Run() {
        MSG msg{};
        while (true) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);

                // Media navigation shortcuts are application-level. Owned/player windows
                // can temporarily hold keyboard focus, so route these keys through the
                // main window instead of depending on the focused HWND.
                if (msg.message == WM_KEYDOWN && (mode_ == Mode::Library || mode_ == Mode::Details || mode_ == Mode::Player)) {
                    const WPARAM key = msg.wParam;
                    const bool routeSpace = (mode_ == Mode::Details || mode_ == Mode::Player) && key == VK_SPACE;
                    const bool routeMediaArrow = (mode_ == Mode::Details || mode_ == Mode::Player) && (key == VK_LEFT || key == VK_RIGHT);
                    if (routeSpace || routeMediaArrow) {
                        SendMessageW(hwnd_, WM_KEYDOWN, key, msg.lParam);
                        continue;
                    }
                }

                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            EnforceProcessMemoryBudget();
            if (mode_ == Mode::Player && player_) {
                player_->Render();
                UpdateSeekUi();
                UpdatePlayerControlVisibility();
            } else {
                WaitMessage();
            }
        }
    }

    bool OpenExternalMedia(const std::wstring& rawPath) {
        if(rawPath.empty()) return false;
        return OpenExternalMediaBatch(std::vector<std::wstring>{rawPath});
    }

    bool OpenExternalMediaBatch(const std::vector<std::wstring>& rawPaths) {
        std::vector<fs::path> mediaPaths;
        std::set<std::wstring> seen;
        mediaPaths.reserve(rawPaths.size());
        for(const auto& rawPath:rawPaths){
            if(rawPath.empty()) continue;
            std::error_code ec;
            fs::path mediaPath=fs::path(rawPath);
            if(mediaPath.is_relative()) mediaPath=fs::absolute(mediaPath,ec);
            if(ec){ ec.clear(); mediaPath=fs::path(rawPath); }
            mediaPath=mediaPath.lexically_normal();
            if(!fs::exists(mediaPath,ec) || ec){ ec.clear(); continue; }
            if(!fs::is_regular_file(mediaPath,ec) || ec){ ec.clear(); continue; }
            const std::wstring ext=mediaPath.extension().wstring();
            if(!IsVideoExtension(ext) && !IsImageExtension(ext)) continue;
            const std::wstring key=ToLower(mediaPath.wstring());
            if(seen.insert(key).second) mediaPaths.push_back(std::move(mediaPath));
        }
        if(mediaPaths.empty()) return false;

        const std::wstring firstPath=mediaPaths.front().wstring();
        const bool firstIsVideo=IsVideoExtension(mediaPaths.front().extension().wstring());

        // A new Explorer/Open-With request replaces the previous external session in the
        // existing application window.  Do not create an alternate cache namespace: every
        // item keeps using BuildCachePath/BuildUiCachePath/BuildPreviewDirectory, exactly
        // the same paths it would use when discovered by a normal folder scan.
        KillTimer(hwnd_,kResumeDetailsWorkersTimerId);
        StopImageSlideshow();
        autoNext_=false;
        DestroyPlayerFooterTransition();
        if(player_){ player_->Pause(); player_->CloseSource(); player_->SetNativePixelSizing(false); }
        nativeVideoSizing_=false; nativeImageSizing_=false;
        nativeSizingRestoreRectValid_=false; nativeImageSizingRestoreRectValid_=false;
        if(videoHwnd_) ShowWindow(videoHwnd_,SW_HIDE);
        if(controlsHwnd_) ShowWindow(controlsHwnd_,SW_HIDE);
        if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
        if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
        playerControlsVisible_=false; controlsFading_=false; controlsAlpha_=0;
        controlsHideDeadline_=0;
        volumeFraction_=0.30;

        StopPreviewWorker();
        ClearAllDetailInfoMemory();
        StopThumbnailWorker();
        ResetLibraryThumbLoadView();
        ResetResolutionMetadataWork();
        ClearThumbs(videos_); ClearThumbs(images_);
        videos_.clear(); images_.clear(); folders_.clear();
        filteredIndices_.clear(); filterDirty_=true;
        searchQuery_.clear(); searchVisible_=false; searchSelectAll_=false;
        detailsSearchNavigationActive_=false; detailsSearchNavigationIndices_.clear();
        folderViewStates_.clear();
        selected_=0; scrollY_=0; detailsScrollY_=0;
        ResetPreviewZoom(); ResetLibraryZoom(); ResetImageZoom();
        ClearMediaHoverImmediate();
        libraryReturnHighlightIndex_=static_cast<size_t>(-1); libraryReturnHighlightStart_=0; libraryReturnHighlightRect_=RECT{};

        externalMediaSession_=true;
        externalMediaPaths_.clear();
        externalMediaPaths_.reserve(mediaPaths.size());
        folder_=mediaPaths.front().parent_path().lexically_normal().wstring();
        currentFolder_=folder_;
        detailsOriginFolder_=folder_;
        libraryAccessFailCount_=0; libraryUnavailableLatched_=false; libraryAccessRetryNeedsRescan_=false;

        auto addItem=[this](const fs::path& path){
            const bool video=IsVideoExtension(path.extension().wstring());
            MediaItem item;
            item.path=path.lexically_normal().wstring();
            item.title=path.stem().wstring();
            item.isVideo=video;
            if(video) item.vr=DetectVR(item.path);
            else item.title=StripLeadingImageResolutionPrefix(std::move(item.title));
            item.cachePath=BuildCachePath(item.path);
            item.uiCachePath=BuildUiCachePath(item.path);
            item.favorite=ReadFavoriteMetadata(item.path);
            item.searchText=ToLower(item.title+L"\n"+item.path);
            if(video){
                UINT w=0,h=0;
                if(ReadResolutionMetadata(item.uiCachePath,w,h)){
                    item.sourceWidth=w; item.sourceHeight=h; item.resolutionProbeAttempted=true;
                }
                videos_.push_back(std::move(item));
            }else images_.push_back(std::move(item));
        };
        for(const auto& path:mediaPaths){ externalMediaPaths_.push_back(path.wstring()); addItem(path); }

        // Keep the mini-library deterministic and consistent with a normal scanned library.
        auto mediaSorter=[](const MediaItem& a,const MediaItem& b){
            const MediaNameSortKey ka=BuildMediaNameSortKey(a.title);
            const MediaNameSortKey kb=BuildMediaNameSortKey(b.title);
            if(ka.primary!=kb.primary) return ka.primary<kb.primary;
            if(ka.group!=kb.group) return ka.group<kb.group;
            if(ka.secondary!=kb.secondary) return ka.secondary<kb.secondary;
            if(ka.hasNumber!=kb.hasNumber) return !ka.hasNumber;
            if(ka.hasNumber){ const int cmp=CompareSortNumbers(ka.number,kb.number); if(cmp!=0) return cmp<0; }
            if(ka.fallback!=kb.fallback) return ka.fallback<kb.fallback;
            return ToLower(a.path)<ToLower(b.path);
        };
        std::sort(videos_.begin(),videos_.end(),mediaSorter);
        std::sort(images_.begin(),images_.end(),mediaSorter);

        category_=firstIsVideo?Category::Videos:Category::Images;
        auto& active=CurrentItems();
        const std::wstring targetKey=ToLower(fs::path(firstPath).lexically_normal().wstring());
        selected_=0;
        for(size_t i=0;i<active.size();++i){
            if(ToLower(fs::path(active[i].path).lexically_normal().wstring())==targetKey){ selected_=i; break; }
        }

        mode_=Mode::Details;
        detailsScrollY_=0;
        filterDirty_=true;
        if(category_==Category::Videos){
            // Run the exact same Info initialization as an in-app selection before playback.
            // This immediately restores existing timeline files into RAM. If no timeline
            // exists yet, the external-session return path below generates it after playback.
            StartPreviewWorkerForSelected();
            QueueDetailPrefetchWindow();
            InvalidateRect(hwnd_,nullptr,TRUE);
            EnterPlayerAt(0.0);
        }else{
            ResetImageZoom();
            ClearLoadingState();
            InvalidateRect(hwnd_,nullptr,TRUE);
        }
        if(hwnd_){ ShowWindow(hwnd_,SW_RESTORE); SetForegroundWindow(hwnd_); }
        return true;
    }

    void QueueExternalMediaOpen(const std::vector<std::wstring>& paths) {
        if(paths.empty()) return;
        for(const auto& path:paths){
            if(path.empty()) continue;
            const std::wstring key=ToLower(fs::path(path).lexically_normal().wstring());
            bool exists=false;
            for(const auto& pending:pendingExternalMediaPaths_){
                if(ToLower(fs::path(pending).lexically_normal().wstring())==key){ exists=true; break; }
            }
            if(!exists) pendingExternalMediaPaths_.push_back(path);
        }
        if(pendingExternalMediaPaths_.empty()) return;
        // Explorer may start one process per selected file.  A short debounce merges those
        // handoffs into one mini-library while a later, ordinary single-file open replaces it.
        KillTimer(hwnd_,kExternalOpenBatchTimerId);
        SetTimer(hwnd_,kExternalOpenBatchTimerId,250,nullptr);
        ShowWindow(hwnd_,SW_RESTORE);
        SetForegroundWindow(hwnd_);
    }

    void ProcessPendingExternalMediaOpen() {
        KillTimer(hwnd_,kExternalOpenBatchTimerId);
        if(pendingExternalMediaPaths_.empty()) return;
        std::vector<std::wstring> batch;
        batch.swap(pendingExternalMediaPaths_);
        OpenExternalMediaBatch(batch);
    }

    ~App() {
        DestroyPlayerFooterTransition();
        StopResolutionMetadataWorker();
        StopLibraryThumbLoader();
        StopPreviewWorker();
        StopDetailPrefetchWorker();
        StopFullLoadWorker();
        StopThumbnailWorker();
        ClearAllDetailInfoMemory();
        ClearThumbs(videos_);
        ClearThumbs(images_);
        for(auto& kv:fontCache_) if(kv.second) DeleteObject(kv.second);
        fontCache_.clear();
        DestroyBackBuffer();
        player_.reset();
        if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
        if (mfStarted_) MFShutdown();
        if (comInitialized_) CoUninitialize();
    }

private:
    struct ThumbJob {
        std::wstring source;
        std::wstring output;
        std::wstring uiOutput;
        bool isVideo = true;
        VRInfo vr{};
    };

    struct LibraryThumbLoadJob {
        Category category = Category::Videos;
        size_t index = 0;
        std::wstring itemPath;
        std::wstring cachePath;
        bool isVideo = true;
        bool isVr = false;
        bool loadBitmap = true;
        bool allowGenerate = false; // only visible cards may touch the source to build a missing cache
        VRInfo vr{};
        int width = 640;
        int height = 360;
        uint64_t epoch = 0;
    };

    struct LibraryThumbLoadResult {
        Category category = Category::Videos;
        size_t index = 0;
        std::wstring itemPath;
        std::wstring cachePath;
        HBITMAP bitmap = nullptr;
        int width = 0;
        int height = 0;
        bool fromPrivateCache = false;
        bool privateDecodeFailed = false;
        bool bitmapRequest = false;
        uint64_t epoch = 0;
    };

    struct ResolutionMetadataJob {
        std::wstring itemPath;
        std::wstring uiCachePath;
        bool highPriority = false;
        uint64_t generation = 0;
    };

    struct ResolutionMetadataResult {
        std::wstring itemPath;
        std::wstring uiCachePath;
        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        bool attempted = false;
        uint64_t generation = 0;
    };

    struct FullLoadJob {
        std::wstring source;
        std::wstring cachePath;
        std::wstring uiCachePath;
        std::wstring previewDir;
        bool isVideo = true;
        VRInfo vr{};
    };

    struct FolderViewState {
        int scrollY = 0;
        std::wstring selectedPath;
    };

    enum class MediaHoverSurface { None, Library, Preview };

    struct AnimatedMediaHit {
        RECT hit{};
        RECT visual{};
        size_t id = static_cast<size_t>(-1);
    };

    static constexpr UINT WM_APP_THUMB_READY = WM_APP + 10;
    static constexpr UINT WM_APP_PREVIEW_READY = WM_APP + 11;
    static constexpr UINT WM_APP_CACHE_REPAIR = WM_APP + 12;
    static constexpr UINT WM_APP_LIBRARY_THUMB_LOADED = WM_APP + 13;
    static constexpr UINT WM_APP_FULL_LOAD_PROGRESS = WM_APP + 14;
    static constexpr UINT WM_APP_FULL_LOAD_DONE = WM_APP + 15;
    static constexpr UINT WM_APP_DETAIL_PREFETCH_READY = WM_APP + 16;
    static constexpr UINT WM_APP_RESOLUTION_METADATA_READY = WM_APP + 17;
    static constexpr UINT_PTR kSlideshowTimerId = 41;
    static constexpr UINT_PTR kUiAnimationTimerId = 42;
    static constexpr UINT_PTR kResumeDetailsWorkersTimerId = 43;
    static constexpr UINT_PTR kLibraryAccessRetryTimerId = 44;
    static constexpr UINT_PTR kAppNoticeTimerId = 45;
    static constexpr UINT_PTR kLiveWindowMoveTimerId = 46;
    static constexpr UINT_PTR kExternalOpenBatchTimerId = 47;
    static constexpr ULONG_PTR kExternalOpenCopyDataMagic = 0x564D5031ull; // "VMP1"
    static constexpr ULONGLONG kUiAnimationDurationMs = 160;
    static constexpr ULONGLONG kMediaHoverFadeInMs = 110;
    static constexpr ULONGLONG kLibraryReturnHighlightDurationMs = 3000;
    static constexpr ULONGLONG kAppNoticePulseDurationMs = 5000;
    static constexpr ULONGLONG kPlayerFooterTransitionDurationMs = 240;
    static constexpr ULONGLONG kPlayerControlsFadeDurationMs = kPlayerFooterTransitionDurationMs;
    static constexpr ULONGLONG kFullLoadDonePopupDurationMs = 3000;
    static constexpr BYTE kControlsVisibleAlpha = 218;
    static constexpr int kDefaultPreviewCardWidth = 220;

    static bool PathIsWithin(const std::wstring& childRaw, const std::wstring& rootRaw) {
        if (childRaw.empty() || rootRaw.empty()) return false;
        std::wstring child = ToLower(fs::path(childRaw).lexically_normal().wstring());
        std::wstring root = ToLower(fs::path(rootRaw).lexically_normal().wstring());
        while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        if (child == root) return true;
        if (!root.empty() && root.back() != L'\\' && root.back() != L'/') root.push_back(L'\\');
        return child.size() >= root.size() && child.compare(0, root.size(), root) == 0;
    }

    static void SetRegistryString(HKEY root, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& value) {
        HKEY key{};
        if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
        RegCloseKey(key);
    }

    static void SetRegistryEmptyString(HKEY root, const std::wstring& subKey, const std::wstring& valueName) {
        HKEY key{};
        if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        const wchar_t empty[] = L"";
        RegSetValueExW(key, valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(empty), sizeof(empty));
        RegCloseKey(key);
    }

    void RegisterOpenWith() const {
        wchar_t exeBuf[32768]{};
        const DWORD len = GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
        if (!len || len >= std::size(exeBuf)) return;
        const std::wstring exePath(exeBuf, len);
        const std::wstring appKey = L"Software\\Classes\\Applications\\VisualMediaPlayer.exe";
        SetRegistryString(HKEY_CURRENT_USER, appKey, L"FriendlyAppName", L"Visual MediaPlayer");
        SetRegistryString(HKEY_CURRENT_USER, appKey + L"\\shell\\open\\command", nullptr, L"\"" + exePath + L"\" \"%1\"");

        static const wchar_t* kSupported[] = {
            L".mp4", L".m4v", L".mkv", L".mk3d", L".webm", L".avi", L".divx", L".mov", L".qt", L".wmv", L".asf", L".mpg", L".mpeg", L".mpe", L".mpv", L".mpv2", L".m1v", L".m2v", L".m2p", L".ts", L".m2t", L".mts", L".m2ts", L".tp", L".trp", L".vob", L".vro", L".ogv", L".ogm", L".flv", L".f4v", L".f4p", L".3gp", L".3g2", L".3gp2", L".3gpp", L".rm", L".rmvb", L".rv", L".mxf", L".gxf", L".dv", L".dif", L".dvr-ms", L".wtv", L".mod", L".tod", L".amv", L".ivf", L".y4m", L".nut", L".nsv", L".roq", L".smk", L".bik", L".bk2", L".mjpeg", L".mjpg", L".mjp", L".h264", L".264", L".avc", L".h265", L".265", L".hevc", L".vp8", L".vp9", L".av1", L".r3d", L".braw", L".ari", L".cine", L".crm", L".insv", L".lrv", L".360", L".evo", L".mj2",
            L".jpg", L".jpeg", L".jpe", L".jfif", L".jif", L".jfi", L".png", L".apng", L".bmp", L".dib", L".gif", L".tif", L".tiff", L".webp", L".heic", L".heif", L".hif", L".avif", L".avifs", L".jxl", L".jp2", L".j2k", L".j2c", L".jpf", L".jpx", L".jpm", L".jxr", L".wdp", L".hdp", L".tga", L".targa", L".icb", L".vda", L".vst", L".dds", L".pcx", L".ico", L".cur", L".mng", L".psd", L".psb", L".exr", L".hdr", L".rgbe", L".pic", L".pfm", L".pnm", L".ppm", L".pgm", L".pbm", L".pam", L".qoi", L".sgi", L".rgb", L".rgba", L".bw", L".ras", L".sun", L".xbm", L".xpm", L".svg", L".svgz", L".dng", L".cr2", L".cr3", L".crw", L".nef", L".nrw", L".arw", L".srf", L".sr2", L".raf", L".orf", L".rw2", L".rwl", L".pef", L".x3f", L".3fr", L".fff", L".iiq", L".erf", L".mef", L".mos", L".mrw", L".kdc", L".dcr", L".raw", L".srw", L".bay", L".cap", L".eip", L".mdc", L".rwz"
        };
        const std::wstring supportedKey = appKey + L"\\SupportedTypes";
        for (const wchar_t* ext : kSupported) SetRegistryEmptyString(HKEY_CURRENT_USER, supportedKey, ext);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    static LRESULT CALLBACK MainWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            app = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = h;
        }
        return app ? app->HandleMain(m,w,l) : DefWindowProcW(h,m,w,l);
    }

    static LRESULT CALLBACK VideoWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            app = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(h,m,w,l);
        switch (m) {
        case WM_LBUTTONDOWN:
            if (!(app->player_ && !app->player_->VR().vr && app->player_->FlatZoomActive())) app->PlayerActivity(true);
            if (app->player_) app->player_->BeginDrag(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_MOUSEMOVE:
            app->PlayerActivity(false);
            if (app->player_) app->player_->Drag(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONUP:
            if (app->player_) app->player_->EndDrag();
            return 0;
        case WM_CAPTURECHANGED:
            if (app->player_) app->player_->CancelDrag();
            return 0;
        case WM_MOUSEWHEEL:
            app->HandlePlayerWheel(w,l);
            return 0;
        case WM_KEYDOWN: SendMessageW(app->hwnd_, WM_KEYDOWN, w, l); return 0;
        case WM_SIZE: if (app->player_ && !app->liveWindowMove_) app->player_->Resize(); return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }

    static void ActivateMainFromOverlay(App* app) {
        if(!app || !app->hwnd_) return;
        // The layered player controls intentionally use WS_EX_NOACTIVATE so simply
        // clicking the popup does not activate it.  A real user click should still
        // bring the owning VMP window to the foreground before the control acts.
        if(IsIconic(app->hwnd_)) ShowWindow(app->hwnd_,SW_RESTORE);
        SetForegroundWindow(app->hwnd_);
        BringWindowToTop(app->hwnd_);
    }

    static LRESULT CALLBACK ControlsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            app = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(h,m,w,l);
        switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: app->PaintControlsWindow(); return 0;
        case WM_MOUSEMOVE:
            {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                app->UpdateAnimatedHover(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
            }
            app->PlayerActivity(false);
            app->UpdateSeekHover(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            if (app->seekDragging_ || app->volumeDragging_) app->PlayerMouseMove(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_MOUSELEAVE:
            app->ClearAnimatedHover(h);
            app->ClearSeekHover();
            return 0;
        case WM_LBUTTONDOWN:
            ActivateMainFromOverlay(app);
            app->PlayerActivity(true);
            app->PlayerMouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONUP:
            app->PlayerMouseUp(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_CAPTURECHANGED:
            app->CancelPlayerSliderDrag();
            return 0;
        case WM_MOUSEWHEEL:
            app->HandlePlayerWheel(w,l);
            return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }

    static LRESULT CALLBACK EdgeArrowWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            app = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(h,m,w,l);
        switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: app->PaintEdgeArrowWindow(h); return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            app->UpdateAnimatedHover(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
            app->PlayerActivity(false);
            return 0;
        }
        case WM_MOUSELEAVE:
            app->ClearAnimatedHover(h);
            return 0;
        case WM_LBUTTONDOWN:
            ActivateMainFromOverlay(app);
            app->PlayerActivity(true);
            return 0;
        case WM_LBUTTONUP:
            ActivateMainFromOverlay(app);
            app->PlayerActivity(true);
            app->NavigatePlayerMedia(h == app->playerPrevHwnd_ ? -1 : 1);
            return 0;
        case WM_MOUSEWHEEL:
            app->HandlePlayerWheel(w,l);
            return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }

    LRESULT HandleMain(UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_CREATE: return 0;
        case WM_COPYDATA: {
            const auto* cds=reinterpret_cast<const COPYDATASTRUCT*>(l);
            if(!cds || cds->dwData!=kExternalOpenCopyDataMagic || !cds->lpData || cds->cbData<sizeof(wchar_t)) return FALSE;
            const auto* begin=reinterpret_cast<const wchar_t*>(cds->lpData);
            const size_t count=static_cast<size_t>(cds->cbData/sizeof(wchar_t));
            std::vector<std::wstring> paths;
            size_t pos=0;
            while(pos<count && begin[pos]!=L'\0'){
                size_t end=pos; while(end<count && begin[end]!=L'\0') ++end;
                if(end>pos) paths.emplace_back(begin+pos,begin+end);
                pos=end+1;
            }
            QueueExternalMediaOpen(paths);
            return TRUE;
        }
        case WM_ERASEBKGND: return 1;
        case WM_GETMINMAXINFO: {
            auto* mmi=reinterpret_cast<MINMAXINFO*>(l);
            if(mmi){
                SIZE minimum{};
                if(GetNativeMinimumWindowSize(minimum)){
                    mmi->ptMinTrackSize.x=std::max<LONG>(mmi->ptMinTrackSize.x,minimum.cx);
                    mmi->ptMinTrackSize.y=std::max<LONG>(mmi->ptMinTrackSize.y,minimum.cy);
                    // Native media can be larger than the monitor. Do not let the default
                    // system max-track metrics contradict the strict native minimum.
                    mmi->ptMaxTrackSize.x=std::max<LONG>(mmi->ptMaxTrackSize.x,mmi->ptMinTrackSize.x);
                    mmi->ptMaxTrackSize.y=std::max<LONG>(mmi->ptMaxTrackSize.y,mmi->ptMinTrackSize.y);
                }
            }
            return 0;
        }
        case WM_ACTIVATEAPP:
            if(w && mode_==Mode::Library){
                PrimeVisibleLibraryThumbsFromPrivateCache();
                InvalidateRect(hwnd_,nullptr,FALSE);
            }
            return 0;
        case WM_ENTERSIZEMOVE:
            liveWindowMove_=true;
            SetTimer(hwnd_,kLiveWindowMoveTimerId,16,nullptr);
            return 0;
        case WM_EXITSIZEMOVE:
            liveWindowMove_=false;
            KillTimer(hwnd_,kLiveWindowMoveTimerId);
            if(mode_==Mode::Player && player_){
                Layout();
                player_->Render();
                UpdateSeekUi();
            }else if(mode_==Mode::Details && category_==Category::Images){
                InvalidateRect(hwnd_,nullptr,FALSE);
                UpdateWindow(hwnd_);
            }
            return 0;
        case WM_MOVE:
            if(mode_==Mode::Player){
                if(liveWindowMove_) RepositionPlayerOverlayWindows();
                else Layout();
                if(liveWindowMove_ && player_) player_->Render();
            }else if(liveWindowMove_ && mode_==Mode::Details && category_==Category::Images){
                InvalidateRect(hwnd_,nullptr,FALSE);
                UpdateWindow(hwnd_);
            }
            return 0;
        case WM_SIZE:
            ClearMediaHoverImmediate();
            if(w==SIZE_MINIMIZED){
                if(controlsHwnd_) ShowWindow(controlsHwnd_,SW_HIDE);
                if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
                if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
            }
            if (w != SIZE_MINIMIZED) {
                RECT zoomRc{}; GetClientRect(hwnd_, &zoomRc);
                const int clientW = std::max(1, static_cast<int>(zoomRc.right - zoomRc.left));
                if (mode_ == Mode::Library)
                    ApplyLibraryWidthForViewport(clientW);
                else if (mode_ == Mode::Details && category_ == Category::Videos)
                    previewCardWidth_ = std::min(previewCardWidth_, FourAcrossPreviewMaxWidth(clientW));
            }
            if(mode_==Mode::Player && liveWindowMove_){
                // Do not resize the video child or DXGI buffers while the frame is being
                // dragged. The last complete media surface remains visible and is resized
                // exactly once from WM_EXITSIZEMOVE.
                InvalidateRect(hwnd_,nullptr,FALSE);
                return 0;
            }
            Layout(); InvalidateRect(hwnd_, nullptr, mode_==Mode::Library ? FALSE : TRUE); return 0;
        case WM_PAINT: Paint(); return 0;
        case WM_DPICHANGED: {
            ClearMediaHoverImmediate();
            RECT* suggested = reinterpret_cast<RECT*>(l);
            if (suggested) SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right-suggested->left, suggested->bottom-suggested->top, SWP_NOZORDER|SWP_NOACTIVATE);
            Layout(); InvalidateRect(hwnd_, nullptr, TRUE); return 0;
        }
        case WM_MOUSEMOVE:
            {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                UpdateAnimatedHover(hwnd_, GET_X_LPARAM(l), GET_Y_LPARAM(l));
                UpdateMediaHover(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            }
            if (mode_ == Mode::Library && libraryScrollDragging_) {
                RECT rc{}; GetClientRect(hwnd_, &rc);
                UpdateLibraryScrollbarRects(rc);
                const int maxScroll = LibraryMaxScroll(rc);
                const int trackH = std::max(1, static_cast<int>(libraryScrollTrackRect_.bottom - libraryScrollTrackRect_.top));
                const int thumbH = std::max(1, static_cast<int>(libraryScrollThumbRect_.bottom - libraryScrollThumbRect_.top));
                const int travel = std::max(1, trackH - thumbH);
                const int wantedTop = std::clamp(GET_Y_LPARAM(l) - libraryScrollDragOffset_, static_cast<int>(libraryScrollTrackRect_.top), static_cast<int>(libraryScrollTrackRect_.bottom) - thumbH);
                const double fraction = static_cast<double>(wantedTop - libraryScrollTrackRect_.top) / static_cast<double>(travel);
                const int oldScroll = scrollY_;
                scrollY_ = std::clamp(static_cast<int>(fraction * maxScroll + 0.5), 0, maxScroll);
                UpdateLibraryScrollbarRects(rc);
                if (scrollY_ != oldScroll) {
                    // Preserve hover animation state while content moves. The painter validates
                    // the real cursor against the card's new rectangle, so stale borders cannot
                    // survive, and no-op drag movement cannot restart/flicker a highlight.
                    DeferBackgroundWork();
                    InvalidateLibraryScrollWithFooter(oldScroll);
                }
                return 0;
            }
            if (mode_ == Mode::Details && category_ == Category::Images && imageZoomDragging_) {
                PanImageZoomByDelta(GET_X_LPARAM(l)-imageZoomLastPoint_.x, GET_Y_LPARAM(l)-imageZoomLastPoint_.y);
                imageZoomLastPoint_={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                return 0;
            }
            if (mode_ == Mode::Player) PlayerActivity(false);
            break;
        case WM_MOUSELEAVE:
            ClearAnimatedHover(hwnd_);
            ClearMediaHoverImmediate();
            return 0;
        case WM_MOUSEWHEEL:
            if (mode_ == Mode::Library) {
                const int wheelSteps = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
                if ((GET_KEYSTATE_WPARAM(w) & MK_CONTROL) != 0) {
                    libraryScrollWheelPixelRemainder_=0.0;
                    // Ctrl + wheel resizes the Library cards for this Library session only.
                    // Do not disturb hover/highlight state when the requested size is already
                    // at a limit or the wheel delta is too small to produce a step.
                    if (wheelSteps != 0) {
                        RECT rc{}; GetClientRect(hwnd_, &rc);
                        const int clientW = static_cast<int>(rc.right - rc.left);
                        const int maxWidth = FourAcrossLibraryMaxWidth(clientW);
                        const int oldWidth = libraryCardWidth_;
                        const int newWidth = std::clamp(oldWidth + wheelSteps * 36, kMinLibraryCardWidth, maxWidth);
                        if (newWidth != oldWidth) {
                            if (UseEightAcrossFullscreenLibrary(clientW)) fullscreenLibraryZoomOverridden_ = true;
                            libraryCardWidth_ = newWidth;
                            DeferBackgroundWork();
                            ClampScroll();
                            InvalidateRect(hwnd_,nullptr,FALSE);
                        }
                    }
                } else {
                    const short wheelDelta=GET_WHEEL_DELTA_WPARAM(w);
                    if(wheelDelta!=0){
                        const int oldScroll=scrollY_;
                        const int scrollPixels=ConsumeWheelPixels(wheelDelta,LibraryWheelPixelsPerNotch(),libraryScrollWheelPixelRemainder_);
                        if(scrollPixels!=0){
                            scrollY_-=scrollPixels;
                            ClampScroll();
                            if(scrollY_!=oldScroll){
                                DeferBackgroundWork();
                                InvalidateLibraryScrollWithFooter(oldScroll);
                            }else{
                                // Do not carry movement that was consumed against a hard edge
                                // into the next gesture after the user reverses direction.
                                libraryScrollWheelPixelRemainder_=0.0;
                            }
                        }
                    }
                }
            } else if (mode_ == Mode::Details) {
                // Secondary-preview zoom is Ctrl + wheel only. Accumulate partial
                // high-resolution wheel/trackpad deltas instead of silently discarding them.
                const bool ctrlDown = ((GET_KEYSTATE_WPARAM(w) & MK_CONTROL) != 0) || ((GetKeyState(VK_CONTROL) & 0x8000) != 0);
                const int wheelDelta = GET_WHEEL_DELTA_WPARAM(w);

                if (category_ == Category::Images && !nativeImageSizing_) {
                    // Match flat-video behavior: plain wheel zooms the image around the
                    // current mouse position. Native Size keeps free zoom disabled.
                    POINT zoomPoint{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                    ScreenToClient(hwnd_,&zoomPoint);
                    if(PtInRect(&detailsMediaRect_,zoomPoint)){
                        const float oldScale=imageZoomScale_;
                        const float oldU=imageZoomCenterU_;
                        const float oldV=imageZoomCenterV_;
                        ZoomImageAtPoint(static_cast<short>(wheelDelta),zoomPoint);
                        if(imageZoomScale_!=oldScale || imageZoomCenterU_!=oldU || imageZoomCenterV_!=oldV)
                            DeferBackgroundWork();
                        return 0;
                    }
                }

                if (category_ == Category::Videos && ctrlDown) {
                    detailsScrollWheelPixelRemainder_=0.0;
                    previewWheelRemainder_ += wheelDelta;
                    const int wheelSteps = previewWheelRemainder_ / WHEEL_DELTA;
                    previewWheelRemainder_ -= wheelSteps * WHEEL_DELTA;
                    if (wheelSteps != 0) {
                        RECT rc{}; GetClientRect(hwnd_, &rc);
                        const int clientW = static_cast<int>(rc.right - rc.left);
                        const int maxWidth = FourAcrossPreviewMaxWidth(clientW);

                        // 7-across windowed and 10-across 4K fullscreen are defaults only.
                        // The first Ctrl+wheel gesture starts from the currently displayed
                        // default, then switches to a freely scalable card width.
                        if (!previewZoomOverridden_) {
                            if (UseTenAcrossFullscreenPreviews(clientW))
                                previewCardWidth_ = TenAcrossFullscreenPreviewWidth(clientW);
                            else if (!fullscreen_)
                                previewCardWidth_ = SevenAcrossWindowedPreviewWidth(clientW);
                            previewZoomOverridden_ = true;
                        }

                        const int oldWidth = previewCardWidth_;
                        const int newWidth = std::clamp(oldWidth + wheelSteps * 28, 140, maxWidth);
                        if (newWidth != oldWidth) {
                            previewCardWidth_ = newWidth;
                            DeferBackgroundWork();
                            ClampDetailsScroll();
                            InvalidateRect(hwnd_, nullptr, FALSE);
                        }
                    }
                } else {
                    previewWheelRemainder_ = 0;
                    if(wheelDelta!=0){
                        RECT rc{}; GetClientRect(hwnd_,&rc);
                        const int oldScroll=detailsScrollY_;
                        const int scrollPixels=ConsumeWheelPixels(static_cast<short>(wheelDelta),DetailsWheelPixelsPerNotch(static_cast<int>(rc.right-rc.left)),detailsScrollWheelPixelRemainder_);
                        if(scrollPixels!=0){
                            detailsScrollY_-=scrollPixels;
                            ClampDetailsScroll();
                            if(detailsScrollY_!=oldScroll){
                                DeferBackgroundWork();
                                InvalidateDetailsScrollOptimized(oldScroll);
                            }else{
                                detailsScrollWheelPixelRemainder_=0.0;
                            }
                        }
                    }
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (mode_ == Mode::Details && category_ == Category::Images && !nativeImageSizing_ && imageZoomScale_ > 1.001f) {
                POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                if(PtInRect(&detailsMediaRect_,p)){
                    imageZoomDragging_=true;
                    imageZoomLastPoint_=p;
                    SetCapture(hwnd_);
                    return 0;
                }
            }
            if (mode_ == Mode::Library) {
                DeferBackgroundWork(180);
                RECT rc{}; GetClientRect(hwnd_, &rc);
                UpdateLibraryScrollbarRects(rc);
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                if (!IsRectEmpty(&libraryScrollThumbRect_) && PtInRect(&libraryScrollThumbRect_, p)) {
                    libraryScrollDragging_ = true;
                    libraryScrollDragOffset_ = p.y - libraryScrollThumbRect_.top;
                    SetCapture(hwnd_);
                    return 0;
                }
                if (!IsRectEmpty(&libraryScrollTrackRect_) && PtInRect(&libraryScrollTrackRect_, p)) {
                    const int maxScroll = LibraryMaxScroll(rc);
                    const int trackH = std::max(1, static_cast<int>(libraryScrollTrackRect_.bottom - libraryScrollTrackRect_.top));
                    const int thumbH = std::max(1, static_cast<int>(libraryScrollThumbRect_.bottom - libraryScrollThumbRect_.top));
                    const int travel = std::max(1, trackH - thumbH);
                    const int wantedTop = std::clamp(static_cast<int>(p.y) - thumbH / 2, static_cast<int>(libraryScrollTrackRect_.top), static_cast<int>(libraryScrollTrackRect_.bottom) - thumbH);
                    const double fraction = static_cast<double>(wantedTop - libraryScrollTrackRect_.top) / static_cast<double>(travel);
                    const int oldScroll=scrollY_;
                    scrollY_ = std::clamp(static_cast<int>(fraction * maxScroll + 0.5), 0, maxScroll);
                    UpdateLibraryScrollbarRects(rc);
                    if(scrollY_!=oldScroll){
                        DeferBackgroundWork();
                        InvalidateLibraryScrollWithFooter(oldScroll);
                    }
                    return 0;
                }
            }
            break;
        case WM_LBUTTONUP:
            if(imageZoomDragging_){
                imageZoomDragging_=false;
                if(GetCapture()==hwnd_) ReleaseCapture();
                return 0;
            }
            if (libraryScrollDragging_) {
                libraryScrollDragging_ = false;
                if (GetCapture() == hwnd_) ReleaseCapture();
                return 0;
            }
            if (mode_ != Mode::Player) { HandleClick(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0; }
            break;
        case WM_CAPTURECHANGED:
            libraryScrollDragging_ = false;
            imageZoomDragging_ = false;
            break;
        case WM_APP_MEDIA_EVENT: {
            const DWORD ev = static_cast<DWORD>(w);
            if (player_) player_->HandleMediaEvent(ev);
            if (ev == MF_MEDIA_ENGINE_EVENT_ENDED) HandlePlaybackEnded();
            return 0;
        }
        case WM_APP_MEDIA_ERROR: {
            if(mode_==Mode::Player && selected_<videos_.size()){
                const std::wstring failedPath=videos_[selected_].path;
                if(!PathExistsNoThrow(failedPath) || !IsLibraryRootAccessible()){
                    if(player_) player_->Pause();
                    NoteLibraryAccessFailure(true);
                    ArmLibraryAccessMonitor(3000);
                }else{
                    LeavePlayer();
                    ShowInAppNotice(L"This media is unsupported.",5000);
                }
            }
            return 0;
        }
        case WM_APP_PLAYER_READY:
            UpdateWindowTitle();
            if (player_ && player_->VR().vr) {
                // Native Size is a player-session preference. VR temporarily suspends
                // the flat/native presentation, but must not turn the preference off.
                SuspendNativeVideoSizingForVr();
                Layout();
                InvalidateControls();
            } else if (nativeVideoSizing_ && player_) {
                player_->SetNativePixelSizing(true);
                if (!fullscreen_) ApplyNativeVideoWindowSize();
                Layout();
                InvalidateControls();
            }
            return 0;
        case WM_APP_THUMB_READY:
            if (mode_ == Mode::Library) InvalidateLibraryScrollableArea();
            else InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_APP_LIBRARY_THUMB_LOADED:
            ApplyLibraryThumbLoadResults();
            return 0;
        case WM_APP_FULL_LOAD_PROGRESS:
            // Load Everything generates preview JPEGs on its own worker and does not use
            // the selected-media preview notifier. If Info is open, refresh its timeline
            // when each source finishes so newly generated secondary images appear without
            // leaving and reopening the panel.
            if (mode_ == Mode::Details && category_ == Category::Videos) {
                RefreshPreviewFrames();
                ClampDetailsScroll();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            InvalidateLoadingPopupArea();
            return 0;
        case WM_APP_FULL_LOAD_DONE:
            if(w!=0) {
                fullLoadFinishedAt_=GetTickCount64();
                StartUiAnimationTimer();
            } else {
                fullLoadFinishedAt_=0;
            }
            if (mode_ == Mode::Details && category_ == Category::Videos) {
                RefreshPreviewFrames();
                ClampDetailsScroll();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            InvalidateLoadingPopupArea();
            return 0;
        case WM_APP_DETAIL_PREFETCH_READY:
            HandleDetailPrefetchResult(reinterpret_cast<DetailPrefetchResult*>(l));
            return 0;
        case WM_APP_RESOLUTION_METADATA_READY:
            HandleResolutionMetadataResult(reinterpret_cast<ResolutionMetadataResult*>(l));
            return 0;
        case WM_APP_PREVIEW_READY:
            if (mode_ == Mode::Details && category_ == Category::Videos) {
                RefreshPreviewFrames();
                ClampDetailsScroll();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_APP_SELECTED_WORK_DONE:
            if (mode_ == Mode::Details && category_ == Category::Videos) {
                ClearLoadingState();
                StartThumbnailWorker();
                QueueDetailPrefetchWindow();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_APP_CACHE_REPAIR:
            if (mode_ == Mode::Library) {
                // The visible-card loader will rebuild only the cache entry that is
                // actually needed. Never restart an all-library background walk.
                InvalidateLibraryScrollableArea();
            } else if (mode_ == Mode::Details && category_ == Category::Videos) {
                StartPreviewWorkerForSelected();
                QueueDetailPrefetchWindow();
            }
            return 0;
        case WM_CHAR:
            if (mode_ == Mode::Library && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                const wchar_t ch = static_cast<wchar_t>(w);
                if (ch == 8) {
                    if (searchVisible_ && !searchQuery_.empty()) {
                        if (searchSelectAll_) searchQuery_.clear();
                        else searchQuery_.pop_back();
                        searchSelectAll_ = false;
                        filterDirty_ = true; scrollY_ = 0; ClampScroll(); InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                    return 0;
                }
                if (ch >= 32 && ch != 127) {
                    searchVisible_ = true;
                    if (searchSelectAll_) searchQuery_.clear();
                    searchSelectAll_ = false;
                    searchQuery_.push_back(ch); filterDirty_ = true; scrollY_ = 0; ClampScroll(); InvalidateRect(hwnd_, nullptr, FALSE);
                    return 0;
                }
            }
            break;
        case WM_TIMER:
            if (w == kSlideshowTimerId) {
                AdvanceImageSlideshow();
                return 0;
            }
            if (w == kUiAnimationTimerId) {
                TickUiAnimations();
                return 0;
            }
            if (w == kResumeDetailsWorkersTimerId) {
                KillTimer(hwnd_,kResumeDetailsWorkersTimerId);
                // Definitively release the video source after the reverse footer fade.
                // Do not immediately reopen the source just to continue background Info
                // generation; already-generated timeline/banner files are consumed lazily
                // and each cache file is closed as soon as it has been copied into RAM.
                if(player_) player_->CloseSource();
                if(mode_==Mode::Details && category_==Category::Videos){
                    RefreshPreviewFrames();
                    detailsDurationSeconds_.store(ReadCachedPreviewDuration(),std::memory_order_relaxed);
                    if(!PreviewCacheIsComplete()) {
                        // Entering Player cancels any in-progress selected-media generator.
                        // Once playback releases the source, resume an incomplete timeline
                        // for every video session (not only External/Open-With). The worker
                        // re-checks the cache after claiming the source, so completed files
                        // are reused and only missing work continues.
                        StartPreviewWorkerForSelected();
                    }
                    ClampDetailsScroll();
                    QueueDetailPrefetchWindow();
                    InvalidateRect(hwnd_,nullptr,FALSE);
                }
                return 0;
            }
            if (w == kLibraryAccessRetryTimerId) {
                CheckLibraryAccessHealth();
                return 0;
            }
            if (w == kExternalOpenBatchTimerId) {
                ProcessPendingExternalMediaOpen();
                return 0;
            }
            if (w == kLiveWindowMoveTimerId) {
                if(!liveWindowMove_){
                    KillTimer(hwnd_,kLiveWindowMoveTimerId);
                    return 0;
                }
                if(mode_==Mode::Player && player_){
                    // DefWindowProc runs a modal move/size loop while the title bar is
                    // being dragged, so the normal outer render loop cannot execute.
                    // Keep presenting the existing swap-chain surface; Layout/ResizeBuffers
                    // is deliberately deferred until WM_EXITSIZEMOVE.
                    player_->Render();
                    UpdateSeekUi();
                    UpdatePlayerControlVisibility();
                }else if(mode_==Mode::Details && category_==Category::Images){
                    // Keep slideshow/fade/image UI painting live during the same modal loop.
                    InvalidateRect(hwnd_,nullptr,FALSE);
                    UpdateWindow(hwnd_);
                }
                return 0;
            }
            if (w == kAppNoticeTimerId) {
                KillTimer(hwnd_,kAppNoticeTimerId);
                appNoticeText_.clear(); appNoticeUntil_=0; appNoticeStart_=0;
                InvalidateRect(hwnd_,nullptr,FALSE);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                if (w == L'F') {
                    if(mode_==Mode::Library){
                        size_t mediaIndex=static_cast<size_t>(-1);
                        if(HoveredLibraryMediaIndex(mediaIndex)){
                            auto& list=CurrentItems();
                            if(mediaIndex<list.size()) ToggleFavorite(list[mediaIndex]);
                        }
                        return 0;
                    }
                    if(mode_==Mode::Details){
                        auto& list=CurrentItems();
                        if(selected_<list.size()) ToggleFavorite(list[selected_]);
                        return 0;
                    }
                }
                if (mode_==Mode::Library && w == L'A' && searchVisible_ && !searchQuery_.empty()) {
                    searchSelectAll_ = true;
                    InvalidateRect(hwnd_, &searchBoxRect_, FALSE);
                    return 0;
                }
            }
            if (searchVisible_ && mode_ == Mode::Library && w == VK_ESCAPE) {
                // Close search without changing the folder.  Force a fresh local-folder
                // filter next time Library is painted; never reuse stale search results.
                searchQuery_.clear();
                searchVisible_ = false;
                searchSelectAll_ = false;
                filteredIndices_.clear();
                filterDirty_ = true;
                scrollY_ = 0;
                if (mode_ == Mode::Library) { ClampScroll(); }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (searchVisible_ && mode_ == Mode::Library && w == VK_RETURN) {
                const auto& filtered = FilteredIndices();
                if (!filtered.empty()) {
                    thumbStop_.store(true,std::memory_order_release); ClearLoadingStateIf(1);
                    detailsOriginFolder_ = currentFolder_;
                    detailsSearchNavigationActive_ = searchVisible_ && !searchQuery_.empty();
                    if(detailsSearchNavigationActive_) detailsSearchNavigationIndices_ = filtered;
                    else detailsSearchNavigationIndices_.clear();
                    selected_ = filtered.front();
                    const auto& list=CurrentItems();
                    if(selected_<list.size()) SaveCurrentFolderViewState(list[selected_].path);
                    ResetLibraryZoom();
                    if(category_==Category::Images) ResetImageZoom();
                    mode_ = Mode::Details; detailsScrollY_ = 0;
                    if(category_==Category::Videos) StartPreviewWorkerForSelected(); else ClearLoadingState();
                    QueueDetailPrefetchWindow();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return 0;
            }
            if (w == VK_ESCAPE && mode_ == Mode::Player) {
                if(player_ && !player_->VR().vr && !nativeVideoSizing_ && player_->FlatZoomActive()){
                    player_->ResetFlatZoom();
                    if(videoHwnd_) InvalidateRect(videoHwnd_,nullptr,FALSE);
                    return 0;
                }
                // Escape navigates back to Info without changing the global fullscreen state.
                LeavePlayer();
                return 0;
            }
            if (w == VK_ESCAPE && mode_ == Mode::Details) {
                if(category_==Category::Images && !nativeImageSizing_ && ImageZoomActive()){
                    ResetImageZoom();
                    InvalidateRect(hwnd_,nullptr,FALSE);
                    return 0;
                }
                // Escape mirrors the visible Back button on the Info screen.
                ReturnFromDetailsToLibrary();
                return 0;
            }
            if (w == VK_ESCAPE && mode_ == Mode::Library && !IsAtLibraryRoot()) {
                // Escape mirrors the visible Back button inside a Library subfolder.
                StopImageSlideshow();
                searchQuery_.clear();
                searchVisible_ = false;
                        filteredIndices_.clear();
                filterDirty_ = true;
                SaveCurrentFolderViewState();
                fs::path parent = fs::path(currentFolder_).parent_path();
                currentFolder_ = parent.empty() ? folder_ : parent.lexically_normal().wstring();
                RestoreFolderViewState(currentFolder_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (w == VK_ESCAPE && mode_ == Mode::Library && IsAtLibraryRoot() && fullscreen_) {
                // At the top Library there is no deeper UI state left for Escape to close,
                // so Escape may finally leave fullscreen. Search/subfolder/Info/Player
                // handling above still takes priority and preserves fullscreen.
                ToggleFullscreen();
                return 0;
            }
            if (w == VK_SPACE && category_ == Category::Images && mode_ == Mode::Details && selected_ < images_.size()) {
                if (slideshowActive_) StopImageSlideshow();
                else StartImageSlideshowFromSelected();
                return 0;
            }
            if (w == VK_SPACE && mode_ == Mode::Details && category_ == Category::Videos && selected_ < videos_.size()) {
                // Exactly the same action as the Info-screen "Play" button.
                EnterPlayerAt(0.0);
                return 0;
            }
            if (w == VK_LEFT && mode_ == Mode::Details) {
                if (CanNavigateDetailsMedia(-1)) NavigateDetailsMedia(-1);
                return 0;
            }
            if (w == VK_RIGHT && mode_ == Mode::Details) {
                if (CanNavigateDetailsMedia(1)) NavigateDetailsMedia(1);
                return 0;
            }
            if (w == VK_SPACE && mode_ == Mode::Player && player_) {
                PlayerActivity(true); player_->PlayPause(); InvalidateControls(); return 0;
            }
            if (w == VK_LEFT && mode_ == Mode::Player) {
                SkipPlaybackSeconds(-30.0);
                return 0;
            }
            if (w == VK_RIGHT && mode_ == Mode::Player) {
                SkipPlaybackSeconds(30.0);
                return 0;
            }
            if (w == VK_F11) {
                if(mode_==Mode::Player) PlayerActivity(true);
                ToggleFullscreen(); return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd_,kLibraryAccessRetryTimerId);
            KillTimer(hwnd_,kLiveWindowMoveTimerId);
            KillTimer(hwnd_,kExternalOpenBatchTimerId);
            StopImageSlideshow();
            ResetLibraryZoom();
            SaveSettings();
            StopPreviewWorker();
            StopDetailPrefetchWorker();
            StopFullLoadWorker();
            StopThumbnailWorker();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd_,m,w,l);
    }

    void DestroyBackBuffer() {
        if(backDC_) {
            if(backOldBitmap_) SelectObject(backDC_,backOldBitmap_);
            if(backBitmap_) DeleteObject(backBitmap_);
            DeleteDC(backDC_);
        }
        backDC_=nullptr; backBitmap_=nullptr; backOldBitmap_=nullptr; backW_=backH_=0;
    }

    void EnsureBackBuffer(HDC reference, int w, int h) {
        if(backDC_ && backW_==w && backH_==h) return;
        DestroyBackBuffer();
        backDC_=CreateCompatibleDC(reference);
        backBitmap_=CreateCompatibleBitmap(reference,w,h);
        backOldBitmap_=SelectObject(backDC_,backBitmap_);
        backW_=w; backH_=h;
    }

    bool UseGpuLibraryRenderer(RECT /*rc*/) const {
        // One primary Library renderer at every window size. Paint() falls back to the
        // retained GDI implementation only when hardware renderer creation/presentation
        // fails, so windowed and fullscreen use identical layout and composition logic.
        return mode_==Mode::Library && hwnd_!=nullptr;
    }

    void ResetLibraryGpuRenderer() {
        libraryD2dCardBrush_.Reset();
        libraryD2dPlaceholderBrush_.Reset();
        libraryD2dUiBrush_.Reset();
        libraryD2dFavoriteIcon_.Reset();
        libraryD2dVrIcon_.Reset();
        libraryD2dResolution4k_.Reset();
        libraryD2dResolution5k_.Reset();
        libraryD2dResolution8k_.Reset();
        libraryD2dFolderIcon_.Reset();
        libraryD2dRefreshIcon_.Reset();
        libraryD2dDownloadIcon_.Reset();
        libraryD2dResizeIcon_.Reset();
        libraryD2dResizeIconWhite_.Reset();
        libraryD2dTarget_.Reset();
        libraryD2dWidth_=libraryD2dHeight_=0;
        ++libraryD2dGeneration_;
        auto clear=[&](std::vector<MediaItem>& list){
            for(auto& item:list){
                item.libraryGpuThumb.Reset();
                item.libraryGpuThumbSource=nullptr;
                item.libraryGpuGeneration=0;
                item.detailsGpuThumb.Reset();
                item.detailsGpuThumbSource=nullptr;
                item.detailsGpuGeneration=0;
            }
        };
        clear(videos_); clear(images_);
        auto clearPreviewGpu=[](std::vector<PreviewFrame>& frames){
            for(auto& frame:frames){frame.gpuBitmap.Reset();frame.gpuBitmapSource=nullptr;frame.gpuGeneration=0;}
        };
        clearPreviewGpu(previewFrames_);
        for(auto& kv:prefetchedPreviewSets_) clearPreviewGpu(kv.second.frames);
        detailsGpuWorkingSetActive_=false;
    }

    bool EnsureLibraryGpuRenderer(RECT rc) {
        const int w=std::max(1,static_cast<int>(rc.right-rc.left));
        const int h=std::max(1,static_cast<int>(rc.bottom-rc.top));
        if(!libraryD2dFactory_){
            D2D1_FACTORY_OPTIONS options{};
            if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory),&options,reinterpret_cast<void**>(libraryD2dFactory_.GetAddressOf())))) return false;
        }
        if(!libraryWicFactory_){
            if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(libraryWicFactory_.GetAddressOf())))) return false;
        }
        if(!libraryDWriteFactory_){
            if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(libraryDWriteFactory_.ReleaseAndGetAddressOf())))) return false;
        }
        if(!libraryD2dTarget_){
            D2D1_RENDER_TARGET_PROPERTIES props{};
            props.type=D2D1_RENDER_TARGET_TYPE_HARDWARE;
            props.pixelFormat.format=DXGI_FORMAT_B8G8R8A8_UNORM;
            props.pixelFormat.alphaMode=D2D1_ALPHA_MODE_IGNORE;
            props.dpiX=96.0f; props.dpiY=96.0f;
            props.usage=D2D1_RENDER_TARGET_USAGE_NONE;
            props.minLevel=D2D1_FEATURE_LEVEL_DEFAULT;
            D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps{};
            hwndProps.hwnd=hwnd_;
            hwndProps.pixelSize=D2D1_SIZE_U{static_cast<UINT32>(w),static_cast<UINT32>(h)};
            hwndProps.presentOptions=D2D1_PRESENT_OPTIONS_NONE;
            if(FAILED(libraryD2dFactory_->CreateHwndRenderTarget(&props,&hwndProps,libraryD2dTarget_.GetAddressOf()))) return false;
            const D2D1_COLOR_F cardColor{31.0f/255.0f,35.0f/255.0f,46.0f/255.0f,1.0f};
            const D2D1_COLOR_F placeholderColor{43.0f/255.0f,48.0f/255.0f,61.0f/255.0f,1.0f};
            const D2D1_COLOR_F uiColor{1.0f,1.0f,1.0f,1.0f};
            if(FAILED(libraryD2dTarget_->CreateSolidColorBrush(&cardColor,nullptr,libraryD2dCardBrush_.GetAddressOf())) ||
               FAILED(libraryD2dTarget_->CreateSolidColorBrush(&placeholderColor,nullptr,libraryD2dPlaceholderBrush_.GetAddressOf())) ||
               FAILED(libraryD2dTarget_->CreateSolidColorBrush(&uiColor,nullptr,libraryD2dUiBrush_.GetAddressOf()))){
                ResetLibraryGpuRenderer(); return false;
            }
            libraryD2dWidth_=w; libraryD2dHeight_=h;
        }else if(libraryD2dWidth_!=w || libraryD2dHeight_!=h){
            const D2D1_SIZE_U newSize{static_cast<UINT32>(w),static_cast<UINT32>(h)};
            const HRESULT hr=libraryD2dTarget_->Resize(&newSize);
            if(FAILED(hr)){ ResetLibraryGpuRenderer(); return EnsureLibraryGpuRenderer(rc); }
            libraryD2dWidth_=w; libraryD2dHeight_=h;
        }
        return true;
    }

    ID2D1Bitmap* GetLibraryGpuThumb(MediaItem& item,HBITMAP source) {
        if(!source || !libraryD2dTarget_ || !libraryWicFactory_) return nullptr;
        if(item.libraryGpuThumb && item.libraryGpuThumbSource==source && item.libraryGpuGeneration==libraryD2dGeneration_)
            return item.libraryGpuThumb.Get();
        item.libraryGpuThumb.Reset(); item.libraryGpuThumbSource=nullptr; item.libraryGpuGeneration=0;
        ComPtr<IWICBitmap> wicBitmap;
        if(FAILED(libraryWicFactory_->CreateBitmapFromHBITMAP(source,nullptr,WICBitmapIgnoreAlpha,wicBitmap.GetAddressOf()))) return nullptr;
        ComPtr<IWICFormatConverter> converter;
        if(FAILED(libraryWicFactory_->CreateFormatConverter(converter.GetAddressOf()))) return nullptr;
        if(FAILED(converter->Initialize(wicBitmap.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom))) return nullptr;
        if(FAILED(libraryD2dTarget_->CreateBitmapFromWicBitmap(converter.Get(),nullptr,item.libraryGpuThumb.GetAddressOf()))) return nullptr;
        item.libraryGpuThumbSource=source;
        item.libraryGpuGeneration=libraryD2dGeneration_;
        return item.libraryGpuThumb.Get();
    }

    static D2D1_RECT_F D2DRect(RECT r) {
        return D2D1_RECT_F{static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right),static_cast<float>(r.bottom)};
    }

    ID2D1Bitmap* GetLibraryGpuUiBitmap(Gdiplus::Bitmap* source,ComPtr<ID2D1Bitmap>& cache) {
        if(!source || !libraryD2dTarget_) return nullptr;
        if(cache) return cache.Get();
        const UINT w=source->GetWidth(),h=source->GetHeight();
        if(w==0 || h==0) return nullptr;
        Gdiplus::Rect lockRect(0,0,static_cast<INT>(w),static_cast<INT>(h));
        Gdiplus::BitmapData data{};
        if(source->LockBits(&lockRect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok) return nullptr;
        const UINT pitch=w*4u;
        std::vector<BYTE> pixels(static_cast<size_t>(pitch)*h);
        const BYTE* base=static_cast<const BYTE*>(data.Scan0);
        const INT stride=data.Stride;
        for(UINT y=0;y<h;++y){
            const BYTE* row=stride>=0 ? base+static_cast<size_t>(y)*static_cast<size_t>(stride)
                                      : base+static_cast<size_t>(h-1-y)*static_cast<size_t>(-stride);
            std::memcpy(pixels.data()+static_cast<size_t>(y)*pitch,row,pitch);
        }
        source->UnlockBits(&data);
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat=D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED};
        props.dpiX=96.0f; props.dpiY=96.0f;
        if(FAILED(libraryD2dTarget_->CreateBitmap(D2D1_SIZE_U{w,h},pixels.data(),pitch,&props,cache.GetAddressOf()))) return nullptr;
        return cache.Get();
    }

    void DrawLibraryGpuBitmapCentered(ID2D1Bitmap* bitmap,RECT r,int insetX=0,int insetY=0) {
        if(!bitmap || !libraryD2dTarget_ || EmptyRectValue(r)) return;
        const auto size=bitmap->GetSize();
        const float availW=static_cast<float>(std::max<LONG>(1L,(r.right-r.left)-static_cast<LONG>(2*insetX)));
        const float availH=static_cast<float>(std::max<LONG>(1L,(r.bottom-r.top)-static_cast<LONG>(2*insetY)));
        const float scale=std::min(1.0f,std::min(availW/std::max(1.0f,size.width),availH/std::max(1.0f,size.height)));
        const float w=size.width*scale,h=size.height*scale;
        const float cx=(r.left+r.right)*0.5f,cy=(r.top+r.bottom)*0.5f;
        const D2D1_RECT_F dst{cx-w*0.5f,cy-h*0.5f,cx+w*0.5f,cy+h*0.5f};
        libraryD2dTarget_->DrawBitmap(bitmap,&dst,1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,nullptr);
    }

    static D2D1_COLOR_F D2DColor(COLORREF c,float alpha=1.0f) {
        return D2D1_COLOR_F{GetRValue(c)/255.0f,GetGValue(c)/255.0f,GetBValue(c)/255.0f,std::clamp(alpha,0.0f,1.0f)};
    }

    void SetLibraryGpuBrush(COLORREF c,float alpha=1.0f) {
        if(libraryD2dUiBrush_) libraryD2dUiBrush_->SetColor(D2DColor(c,alpha));
    }

    void FillLibraryGpuRound(RECT r,COLORREF c,float radius) {
        if(!libraryD2dTarget_ || !libraryD2dUiBrush_ || EmptyRectValue(r)) return;
        SetLibraryGpuBrush(c);
        const D2D1_ROUNDED_RECT rr{D2DRect(r),radius,radius};
        libraryD2dTarget_->FillRoundedRectangle(&rr,libraryD2dUiBrush_.Get());
    }

    void FillLibraryGpuRect(RECT r,COLORREF c) {
        if(!libraryD2dTarget_ || !libraryD2dUiBrush_ || EmptyRectValue(r)) return;
        SetLibraryGpuBrush(c);
        const D2D1_RECT_F fr=D2DRect(r);
        libraryD2dTarget_->FillRectangle(&fr,libraryD2dUiBrush_.Get());
    }

    void FillLibraryGpuRectAlpha(RECT r,COLORREF c,float alpha) {
        if(!libraryD2dTarget_ || !libraryD2dUiBrush_ || EmptyRectValue(r)) return;
        SetLibraryGpuBrush(c,alpha);
        const D2D1_RECT_F fr=D2DRect(r);
        libraryD2dTarget_->FillRectangle(&fr,libraryD2dUiBrush_.Get());
    }

    IDWriteTextFormat* GetLibraryGpuTextFormat(int px,int weight) {
        if(!libraryDWriteFactory_) return nullptr;
        const uint64_t key=(static_cast<uint64_t>(static_cast<uint32_t>(px))<<32)|static_cast<uint32_t>(weight);
        auto it=libraryDWriteFormats_.find(key);
        if(it!=libraryDWriteFormats_.end()) return it->second.Get();
        ComPtr<IDWriteTextFormat> format;
        if(FAILED(libraryDWriteFactory_->CreateTextFormat(L"Segoe UI",nullptr,static_cast<DWRITE_FONT_WEIGHT>(weight),
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,static_cast<FLOAT>(std::max(1,px)),L"",format.GetAddressOf()))) return nullptr;
        auto inserted=libraryDWriteFormats_.emplace(key,std::move(format));
        return inserted.first->second.Get();
    }

    float MeasureLibraryGpuTextWidth(const std::wstring& text,int px,int weight=FW_NORMAL) {
        if(text.empty() || !libraryDWriteFactory_) return 0.0f;
        IDWriteTextFormat* format=GetLibraryGpuTextFormat(px,weight);if(!format)return 0.0f;
        ComPtr<IDWriteTextLayout> layout;
        if(FAILED(libraryDWriteFactory_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format,8192.0f,512.0f,layout.GetAddressOf()))) return 0.0f;
        DWRITE_TEXT_METRICS metrics{};if(FAILED(layout->GetMetrics(&metrics)))return 0.0f;
        return metrics.widthIncludingTrailingWhitespace;
    }

    void DrawLibraryGpuText(const std::wstring& text,RECT r,int px,int weight=FW_NORMAL,
                            COLORREF color=RGB(238,241,247),UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
        if(text.empty() || !libraryD2dTarget_ || !libraryD2dUiBrush_) return;
        IDWriteTextFormat* format=GetLibraryGpuTextFormat(px,weight); if(!format) return;
        format->SetTextAlignment((flags&DT_CENTER)?DWRITE_TEXT_ALIGNMENT_CENTER:((flags&DT_RIGHT)?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING));
        format->SetParagraphAlignment((flags&DT_VCENTER)?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        format->SetWordWrapping((flags&DT_WORDBREAK)?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming{};
        ComPtr<IDWriteInlineObject> ellipsis;
        if(flags&DT_END_ELLIPSIS){
            trimming.granularity=DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            if(SUCCEEDED(libraryDWriteFactory_->CreateEllipsisTrimmingSign(format,ellipsis.GetAddressOf())))
                format->SetTrimming(&trimming,ellipsis.Get());
            else {trimming.granularity=DWRITE_TRIMMING_GRANULARITY_NONE;format->SetTrimming(&trimming,nullptr);}
        }else{
            trimming.granularity=DWRITE_TRIMMING_GRANULARITY_NONE;
            format->SetTrimming(&trimming,nullptr);
        }
        SetLibraryGpuBrush(color);
        const D2D1_RECT_F bounds=D2DRect(r);
        libraryD2dTarget_->PushAxisAlignedClip(bounds,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        libraryD2dTarget_->DrawText(text.c_str(),static_cast<UINT32>(text.size()),format,&bounds,libraryD2dUiBrush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        libraryD2dTarget_->PopAxisAlignedClip();
    }

    void DrawLibraryGpuHoverBorder(RECT r,float amount,float radius) {
        amount=std::clamp(amount,0.0f,1.0f); if(amount<=0.001f || EmptyRectValue(r)) return;
        const COLORREF border=MixColor(RGB(74,81,96),RGB(238,242,250),amount);
        SetLibraryGpuBrush(border);
        D2D1_RECT_F fr=D2DRect(r); fr.left+=1.5f;fr.top+=1.5f;fr.right-=1.5f;fr.bottom-=1.5f;
        const D2D1_ROUNDED_RECT rr{fr,radius,radius};
        libraryD2dTarget_->DrawRoundedRectangle(&rr,libraryD2dUiBrush_.Get(),3.0f);
    }

    void DrawLibraryGpuButton(RECT r,const std::wstring& text,bool primary=false) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=primary?RGB(235,238,245):RGB(38,43,55);
        const COLORREF over=primary?RGB(250,251,253):RGB(53,59,73);
        FillLibraryGpuRound(r,MixColor(base,over,hover),10.0f);
        DrawLibraryGpuText(text,r,14,FW_SEMIBOLD,primary?RGB(12,14,19):RGB(245,246,250),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    void DrawLibraryGpuTab(RECT r,const std::wstring& text,bool active) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=active?RGB(235,238,245):RGB(29,33,43);
        const COLORREF over=active?RGB(250,251,253):RGB(47,52,65);
        FillLibraryGpuRound(r,MixColor(base,over,hover),12.0f);
        DrawLibraryGpuText(text,r,15,FW_BOLD,active?RGB(12,14,19):RGB(230,233,241),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    void DrawLibraryGpuLine(float x1,float y1,float x2,float y2,COLORREF color,float width=2.0f) {
        SetLibraryGpuBrush(color);
        libraryD2dTarget_->DrawLine(D2D1_POINT_2F{x1,y1},D2D1_POINT_2F{x2,y2},libraryD2dUiBrush_.Get(),width);
    }

    void DrawLibraryGpuIconButtonBase(RECT r,bool active=false) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=active?RGB(235,238,245):RGB(38,43,55);
        FillLibraryGpuRound(r,MixColor(base,active?RGB(250,251,253):RGB(53,59,73),hover),10.0f);
    }

    void DrawLibraryGpuFullscreenButton(RECT r) {
        DrawLibraryGpuIconButtonBase(r,fullscreen_);
        const COLORREF c=fullscreen_?RGB(12,14,19):RGB(245,246,250);
        const float x1=static_cast<float>(r.left+11),x2=static_cast<float>(r.right-11),y1=static_cast<float>(r.top+10),y2=static_cast<float>(r.bottom-10),arm=8.0f;
        DrawLibraryGpuLine(x1+arm,y1,x1,y1,c,3);DrawLibraryGpuLine(x1,y1,x1,y1+arm,c,3);
        DrawLibraryGpuLine(x2-arm,y1,x2,y1,c,3);DrawLibraryGpuLine(x2,y1,x2,y1+arm,c,3);
        DrawLibraryGpuLine(x1,y2-arm,x1,y2,c,3);DrawLibraryGpuLine(x1,y2,x1+arm,y2,c,3);
        DrawLibraryGpuLine(x2,y2-arm,x2,y2,c,3);DrawLibraryGpuLine(x2,y2,x2-arm,y2,c,3);
    }

    void DrawLibraryGpuAutoAdvance(RECT r,bool active) {
        DrawLibraryGpuIconButtonBase(r,active);
        const COLORREF c=active?RGB(12,14,19):RGB(245,246,250);
        const float cy=(r.top+r.bottom)*0.5f,x=static_cast<float>(r.left+11);
        DrawLibraryGpuLine(x,cy-10,x+12,cy,c,4);DrawLibraryGpuLine(x+12,cy,x,cy+10,c,4);
        DrawLibraryGpuLine(x+14,cy-10,x+26,cy,c,4);DrawLibraryGpuLine(x+26,cy,x+14,cy+10,c,4);
    }

    void DrawLibraryGpuFolderIconButton(RECT r) {
        DrawLibraryGpuIconButtonBase(r,false);
        if(auto* bmp=GetLibraryGpuUiBitmap(folderIconBitmap_.get(),libraryD2dFolderIcon_)){ DrawLibraryGpuBitmapCentered(bmp,r,9,9); return; }
        const int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
        RECT tab{cx-13,cy-10,cx-2,cy-2},body{cx-15,cy-5,cx+15,cy+12};
        FillLibraryGpuRound(tab,RGB(245,246,250),4);FillLibraryGpuRound(body,RGB(245,246,250),5);
    }

    void DrawLibraryGpuRefreshIconButton(RECT r) {
        DrawLibraryGpuIconButtonBase(r,false);
        if(auto* bmp=GetLibraryGpuUiBitmap(refreshIconBitmap_.get(),libraryD2dRefreshIcon_)){ DrawLibraryGpuBitmapCentered(bmp,r,8,8); return; }
        DrawLibraryGpuText(L"↻",r,27,FW_SEMIBOLD,RGB(245,246,250),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    void DrawLibraryGpuDownloadIconButton(RECT r) {
        DrawLibraryGpuIconButtonBase(r,false);
        if(auto* bmp=GetLibraryGpuUiBitmap(downloadIconBitmap_.get(),libraryD2dDownloadIcon_)){ DrawLibraryGpuBitmapCentered(bmp,r,9,9); return; }
        const float cx=(r.left+r.right)*0.5f,cy=(r.top+r.bottom)*0.5f;
        DrawLibraryGpuLine(cx,cy-12,cx,cy+6,RGB(245,246,250),3);
        DrawLibraryGpuLine(cx-7,cy,cx,cy+7,RGB(245,246,250),3);DrawLibraryGpuLine(cx,cy+7,cx+7,cy,RGB(245,246,250),3);
        DrawLibraryGpuLine(cx-11,cy+12,cx-11,cy+15,RGB(245,246,250),3);DrawLibraryGpuLine(cx-11,cy+15,cx+11,cy+15,RGB(245,246,250),3);DrawLibraryGpuLine(cx+11,cy+15,cx+11,cy+12,RGB(245,246,250),3);
    }

    void DrawLibraryGpuFavoriteBadge(RECT image) {
        RECT badge{image.left+8,image.top+5,image.left+42,image.top+42};
        if(auto* bmp=GetLibraryGpuUiBitmap(favoriteIconBitmap_.get(),libraryD2dFavoriteIcon_)){ DrawLibraryGpuBitmapCentered(bmp,badge); return; }
        DrawLibraryGpuText(L"♥",badge,27,FW_NORMAL,RGB(245,246,250),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    void DrawLibraryGpuVideoBadges(MediaItem& item,RECT card) {
        if(!item.isVideo) return;
        int badgeRight=card.right-8;
        if(item.vr.vr){
            RECT tag{badgeRight-28,card.top+8,badgeRight,card.top+36};
            if(auto* bmp=GetLibraryGpuUiBitmap(vrBadgeWhiteBitmap_.get(),libraryD2dVrIcon_)) DrawLibraryGpuBitmapCentered(bmp,tag);
            else {FillLibraryGpuRound(tag,RGB(16,19,25),8);DrawLibraryGpuText(L"VR",tag,11,FW_BOLD,RGB(220,225,235),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
            badgeRight=tag.left-6;
        }
        const int klass=ResolutionBadgeClass(item);
        if(klass){
            RECT tag{badgeRight-28,card.top+8,badgeRight,card.top+36};
            Gdiplus::Bitmap* src=nullptr; ComPtr<ID2D1Bitmap>* cache=nullptr;
            if(klass==8){src=resolution8kBitmap_.get();cache=&libraryD2dResolution8k_;}
            else if(klass==5){src=resolution5kBitmap_.get();cache=&libraryD2dResolution5k_;}
            else if(klass==4){src=resolution4kBitmap_.get();cache=&libraryD2dResolution4k_;}
            if(src && cache){
                if(auto* bmp=GetLibraryGpuUiBitmap(src,*cache)) DrawLibraryGpuBitmapCentered(bmp,tag);
                else {FillLibraryGpuRound(tag,RGB(16,19,25),8);DrawLibraryGpuText(std::to_wstring(klass)+L"K",tag,10,FW_BOLD,RGB(230,234,242),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
            }
        }
    }

    void DrawLibraryGpuFolderCard(const LibraryFolder& folder,RECT card) {
        FillLibraryGpuRound(card,RGB(31,35,46),12);
        const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(card.right-card.left)*9.0/16.0)));
        RECT image=card;image.bottom=image.top+imageH;FillLibraryGpuRect(image,RGB(37,42,54));
        const int iconW=118,iconH=82,cx=(image.left+image.right)/2,cy=(image.top+image.bottom)/2+4;
        RECT body{cx-iconW/2,cy-iconH/2+12,cx+iconW/2,cy+iconH/2};RECT tab{body.left+8,body.top-17,body.left+54,body.top+5};
        FillLibraryGpuRound(tab,RGB(210,170,73),7);FillLibraryGpuRound(body,RGB(225,184,82),10);
        RECT title{card.left+10,image.bottom+2,card.right-10,card.bottom-3};DrawLibraryGpuText(folder.name,title,14,FW_SEMIBOLD);
    }

    void PaintLibraryGpuScrollbar(RECT rc) {
        UpdateLibraryScrollbarRects(rc);
        if(IsRectEmpty(&libraryScrollTrackRect_)||IsRectEmpty(&libraryScrollThumbRect_)) return;
        FillLibraryGpuRound(libraryScrollTrackRect_,RGB(24,28,36),5);
        FillLibraryGpuRound(libraryScrollThumbRect_,libraryScrollDragging_?RGB(145,152,166):RGB(94,101,116),6);
    }

    void PaintLibraryGpuNavigator(RECT rc) {
        const int footerTop=std::max(0,static_cast<int>(rc.bottom)-64);
        libraryFooterRect_=RECT{0,footerTop,rc.right,rc.bottom};
        FillLibraryGpuRect(libraryFooterRect_,RGB(16,18,24));
        DrawLibraryGpuLine(0.0f,static_cast<float>(footerTop),static_cast<float>(rc.right),static_cast<float>(footerTop),RGB(42,47,60),1);
        const int buttonTop=footerTop+13,buttonBottom=rc.bottom-13;
        constexpr int iconW=48,iconGap=10,groupGap=24,rightMargin=20;
        const int iconButtonBottom=rc.bottom-10,iconButtonTop=iconButtonBottom-iconW;
        int navLeft=20;
        if(!IsAtLibraryRoot()){backRect_={20,buttonTop,100,buttonBottom};DrawLibraryGpuButton(backRect_,L"Back");navLeft=110;}else backRect_=RECT{};
        categoryToggleRect_={navLeft,buttonTop,navLeft+92,buttonBottom};DrawLibraryGpuTab(categoryToggleRect_,category_==Category::Videos?L"Videos":L"Images",true);
        mediaCountRect_={categoryToggleRect_.right+10,buttonTop,categoryToggleRect_.right+110,buttonBottom};
        DrawLibraryGpuText(L"("+std::to_wstring(CurrentFolderMediaCount())+L")",mediaCountRect_,14,FW_SEMIBOLD,RGB(175,181,194),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        int cursor=std::max(0,static_cast<int>(rc.right)-rightMargin);
        libraryFullRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};DrawLibraryGpuFullscreenButton(libraryFullRect_);cursor=libraryFullRect_.left-iconGap;
        const bool showSlideshow=(category_==Category::Images&&CurrentFolderMediaCount()>0);
        if(showSlideshow){slideshowRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};DrawLibraryGpuAutoAdvance(slideshowRect_,slideshowActive_);cursor=slideshowRect_.left-iconGap;}else slideshowRect_=RECT{};
        const bool showRootMaintenance=IsAtChosenLibraryRoot();const bool showChooseOnly=!showRootMaintenance&&(currentFolder_.empty()||externalMediaSession_);
        if(showRootMaintenance){cursor-=groupGap-iconGap;chooseRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};cursor=chooseRect_.left-iconGap;rescanRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};cursor=rescanRect_.left-iconGap;loadEverythingRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};DrawLibraryGpuFolderIconButton(chooseRect_);DrawLibraryGpuRefreshIconButton(rescanRect_);DrawLibraryGpuDownloadIconButton(loadEverythingRect_);}
        else if(showChooseOnly){rescanRect_=RECT{};loadEverythingRect_=RECT{};cursor-=groupGap-iconGap;chooseRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};DrawLibraryGpuFolderIconButton(chooseRect_);}
        else{chooseRect_=RECT{};rescanRect_=RECT{};loadEverythingRect_=RECT{};}
    }

    void PaintLibraryGpuSearch(RECT rc) {
        const int right=std::max(20,static_cast<int>(rc.right)-20),width=std::min(430,std::max(220,static_cast<int>(rc.right)/3));
        searchBoxRect_={right-width,12,right,52};FillLibraryGpuRound(searchBoxRect_,RGB(31,35,46),11);
        RECT text{searchBoxRect_.left+14,searchBoxRect_.top,searchBoxRect_.right-14,searchBoxRect_.bottom};
        if(searchSelectAll_&&!searchQuery_.empty()){const LONG selectedRight=std::min<LONG>(text.right,text.left+static_cast<LONG>(std::ceil(MeasureLibraryGpuTextWidth(searchQuery_,16,FW_SEMIBOLD)))+5);RECT selected{text.left-3,text.top+8,selectedRight,text.bottom-8};FillLibraryGpuRound(selected,RGB(72,82,105),5);}
        DrawLibraryGpuText(searchQuery_.empty()?L"Search...":searchQuery_,text,16,FW_SEMIBOLD,searchQuery_.empty()?RGB(145,151,164):RGB(244,246,250));
    }

    void PaintLibraryGpuLoadingPopup(RECT rc) {
        std::wstring label,count;const bool fullRunning=fullLoadRunning_.load(std::memory_order_acquire);const ULONGLONG now=GetTickCount64();
        const bool fullDoneVisible=!fullRunning&&fullLoadFinishedAt_!=0&&now-fullLoadFinishedAt_<kFullLoadDonePopupDurationMs;
        if(fullRunning){label=L"Loading everything";const int current=fullLoadCurrent_.load(std::memory_order_relaxed),total=fullLoadTotal_.load(std::memory_order_relaxed);count=total>0?std::to_wstring(std::min(current,total))+L" / "+std::to_wstring(total)+L" files":L"Working...";}
        else if(fullDoneVisible){const int failures=fullLoadFailures_.load(std::memory_order_relaxed);label=L"Load everything finished";count=failures==0?L"All files ready":std::to_wstring(failures)+(failures==1?L" file could not be prepared":L" files could not be prepared");}
        else{const int kind=loadingKind_.load(std::memory_order_acquire);if(kind==0)return;const int current=loadingCurrent_.load(std::memory_order_relaxed),total=loadingTotal_.load(std::memory_order_relaxed);if(kind==1)label=L"Loading library banners";else if(kind==2)label=L"Loading secondary images";else if(kind==3)label=L"Loading info banner";else return;count=total>0?std::to_wstring(std::min(current,total))+L" / "+std::to_wstring(total):L"Working...";}
        const int width=300,height=58,right=std::max(20,static_cast<int>(rc.right)-18),top=(mode_==Mode::Library&&searchVisible_)?68:18;RECT box{std::max(8,right-width),top,right,top+height};
        FillLibraryGpuRound(box,RGB(25,29,38),11);RECT labelRect{box.left+14,box.top+5,box.right-14,box.top+30};DrawLibraryGpuText(label,labelRect,13,FW_SEMIBOLD);RECT countRect{box.left+14,box.top+28,box.right-14,box.bottom-5};DrawLibraryGpuText(count,countRect,12,FW_NORMAL,RGB(165,172,185));
    }

    void PaintLibraryGpuNotice(RECT rc) {
        const ULONGLONG now=GetTickCount64();if(appNoticeText_.empty()||now>=appNoticeUntil_)return;const RECT box=AppNoticeRect(rc);FillLibraryGpuRound(box,RGB(31,35,46),12);DrawLibraryGpuHoverBorder(box,AppNoticePulseAmount(now),12);RECT text{box.left+16,box.top+8,box.right-16,box.bottom-8};DrawLibraryGpuText(appNoticeText_,text,15,FW_SEMIBOLD,RGB(238,241,247),DT_CENTER|DT_VCENTER|DT_WORDBREAK);
    }

    void DrawLibraryGpuBitmapCover(ID2D1Bitmap* bitmap,RECT r) {
        if(!bitmap || !libraryD2dTarget_) return;
        const D2D1_SIZE_F size=bitmap->GetSize();
        if(size.width<=0.0f || size.height<=0.0f) return;
        const float dw=static_cast<float>(std::max<LONG>(1,r.right-r.left));
        const float dh=static_cast<float>(std::max<LONG>(1,r.bottom-r.top));
        const float scale=std::max(dw/size.width,dh/size.height);
        const float sw=dw/scale,sh=dh/scale;
        const float sx=(size.width-sw)*0.5f,sy=(size.height-sh)*0.5f;
        const D2D1_RECT_F src{sx,sy,sx+sw,sy+sh};
        const D2D1_RECT_F dest=D2DRect(r);
        libraryD2dTarget_->DrawBitmap(bitmap,&dest,1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,&src);
    }


    ID2D1Bitmap* CreateGpuBitmapFromHBitmap(HBITMAP source,ComPtr<ID2D1Bitmap>& cache,HBITMAP& cacheSource,uint64_t& cacheGeneration) {
        if(!source || !libraryD2dTarget_ || !libraryWicFactory_) return nullptr;
        if(cache && cacheSource==source && cacheGeneration==libraryD2dGeneration_) return cache.Get();
        cache.Reset(); cacheSource=nullptr; cacheGeneration=0;
        ComPtr<IWICBitmap> wicBitmap;
        if(FAILED(libraryWicFactory_->CreateBitmapFromHBITMAP(source,nullptr,WICBitmapIgnoreAlpha,wicBitmap.GetAddressOf()))) return nullptr;
        ComPtr<IWICFormatConverter> converter;
        if(FAILED(libraryWicFactory_->CreateFormatConverter(converter.GetAddressOf()))) return nullptr;
        if(FAILED(converter->Initialize(wicBitmap.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom))) return nullptr;
        if(FAILED(libraryD2dTarget_->CreateBitmapFromWicBitmap(converter.Get(),nullptr,cache.GetAddressOf()))) return nullptr;
        cacheSource=source; cacheGeneration=libraryD2dGeneration_;
        return cache.Get();
    }

    ID2D1Bitmap* GetDetailsGpuBitmap(MediaItem& item,HBITMAP source) {
        ID2D1Bitmap* bitmap=CreateGpuBitmapFromHBitmap(source,item.detailsGpuThumb,item.detailsGpuThumbSource,item.detailsGpuGeneration);
        if(bitmap) detailsGpuWorkingSetActive_=true;
        return bitmap;
    }

    ID2D1Bitmap* GetPreviewGpuBitmap(PreviewFrame& frame,HBITMAP source) {
        ID2D1Bitmap* bitmap=CreateGpuBitmapFromHBitmap(source,frame.gpuBitmap,frame.gpuBitmapSource,frame.gpuGeneration);
        if(bitmap) detailsGpuWorkingSetActive_=true;
        return bitmap;
    }

    void DrawDetailsGpuBitmapContain(ID2D1Bitmap* bitmap,RECT r,float alpha=1.0f,bool noUpscale=false,bool fitNoUpscale=false,
                                     float zoom=1.0f,float centerU=0.5f,float centerV=0.5f) {
        if(!bitmap || !libraryD2dTarget_ || EmptyRectValue(r) || alpha<=0.0f) return;
        const D2D1_SIZE_F size=bitmap->GetSize(); if(size.width<=0.0f||size.height<=0.0f)return;
        const float rw=static_cast<float>(std::max<LONG>(1,r.right-r.left)),rh=static_cast<float>(std::max<LONG>(1,r.bottom-r.top));
        float scale=std::min(rw/size.width,rh/size.height);
        if(noUpscale) scale=1.0f;
        else if(fitNoUpscale) scale=std::min(1.0f,scale);
        else scale*=std::clamp(zoom,0.25f,8.0f);
        const float dw=std::max(1.0f,size.width*scale),dh=std::max(1.0f,size.height*scale);
        float dx=static_cast<float>(r.left)+(rw-dw)*0.5f,dy=static_cast<float>(r.top)+(rh-dh)*0.5f;
        if(!noUpscale && !fitNoUpscale && std::abs(zoom-1.0f)>0.001f){
            const float cx=static_cast<float>(r.left)+rw*0.5f,cy=static_cast<float>(r.top)+rh*0.5f;
            dx=cx-std::clamp(centerU,0.0f,1.0f)*dw; dy=cy-std::clamp(centerV,0.0f,1.0f)*dh;
        }
        const D2D1_RECT_F clip=D2DRect(r); libraryD2dTarget_->PushAxisAlignedClip(clip,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const D2D1_RECT_F dest{dx,dy,dx+dw,dy+dh};
        libraryD2dTarget_->DrawBitmap(bitmap,&dest,std::clamp(alpha,0.0f,1.0f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,nullptr);
        libraryD2dTarget_->PopAxisAlignedClip();
    }

    void DrawDetailsGpuEdgeArrowButton(RECT r,bool next,bool enabled=true) {
        const float hover=enabled?ButtonHoverAmount(r):0.0f;
        const COLORREF base=enabled?RGB(38,43,55):RGB(25,28,36),over=enabled?RGB(53,59,73):base;
        FillLibraryGpuRound(r,MixColor(base,over,hover),12.0f);
        const COLORREF fg=enabled?RGB(245,246,250):RGB(92,98,111);
        const float cx=(r.left+r.right)*0.5f,cy=(r.top+r.bottom)*0.5f,dx=next?5.0f:-5.0f;
        if(next){DrawLibraryGpuLine(cx-7+dx,cy-14,cx+7+dx,cy,fg,4);DrawLibraryGpuLine(cx+7+dx,cy,cx-7+dx,cy+14,fg,4);}
        else{DrawLibraryGpuLine(cx+7+dx,cy-14,cx-7+dx,cy,fg,4);DrawLibraryGpuLine(cx-7+dx,cy,cx+7+dx,cy+14,fg,4);}
    }

    void DrawDetailsGpuNativeSizeButton(RECT r) {
        const bool active=nativeImageSizing_;
        DrawLibraryGpuIconButtonBase(r,active);
        Gdiplus::Bitmap* icon=active?resizeIconBitmap_.get():resizeIconWhiteBitmap_.get();
        ComPtr<ID2D1Bitmap>& cache=active?libraryD2dResizeIcon_:libraryD2dResizeIconWhite_;
        if(auto* bmp=GetLibraryGpuUiBitmap(icon,cache)){DrawLibraryGpuBitmapCentered(bmp,r,9,9);return;}
        const COLORREF c=active?RGB(12,14,19):RGB(245,246,250);
        const float x1=static_cast<float>(r.left+12),x2=static_cast<float>(r.right-12),y1=static_cast<float>(r.top+12),y2=static_cast<float>(r.bottom-12),arm=7.0f;
        DrawLibraryGpuLine(x1+arm,y1,x1,y1,c,3);DrawLibraryGpuLine(x1,y1,x1,y1+arm,c,3);
        DrawLibraryGpuLine(x2-arm,y1,x2,y1,c,3);DrawLibraryGpuLine(x2,y1,x2,y1+arm,c,3);
        DrawLibraryGpuLine(x1,y2-arm,x1,y2,c,3);DrawLibraryGpuLine(x1,y2,x1+arm,y2,c,3);
        DrawLibraryGpuLine(x2,y2-arm,x2,y2,c,3);DrawLibraryGpuLine(x2,y2,x2-arm,y2,c,3);
    }

    void DrawDetailsGpuVideoBadges(MediaItem& item,RECT media) {
        if(!item.isVideo)return;
        int badgeRight=media.right-10; const int top=media.top+10,size=30;
        if(item.vr.vr){
            RECT tag{badgeRight-size,top,badgeRight,top+size};
            if(auto* bmp=GetLibraryGpuUiBitmap(vrBadgeWhiteBitmap_.get(),libraryD2dVrIcon_))DrawLibraryGpuBitmapCentered(bmp,tag);
            else{FillLibraryGpuRound(tag,RGB(16,19,25),8);DrawLibraryGpuText(L"VR",tag,11,FW_BOLD,RGB(220,225,235),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
            badgeRight=tag.left-6;
        }
        const int klass=ResolutionBadgeClass(item); if(!klass)return;
        RECT tag{badgeRight-size,top,badgeRight,top+size}; Gdiplus::Bitmap* src=nullptr;ComPtr<ID2D1Bitmap>* cache=nullptr;
        if(klass==8){src=resolution8kBitmap_.get();cache=&libraryD2dResolution8k_;}
        else if(klass==5){src=resolution5kBitmap_.get();cache=&libraryD2dResolution5k_;}
        else if(klass==4){src=resolution4kBitmap_.get();cache=&libraryD2dResolution4k_;}
        if(src && cache){
            if(auto* bmp=GetLibraryGpuUiBitmap(src,*cache))DrawLibraryGpuBitmapCentered(bmp,tag);
            else{FillLibraryGpuRound(tag,RGB(16,19,25),8);DrawLibraryGpuText(std::to_wstring(klass)+L"K",tag,10,FW_BOLD,RGB(230,234,242),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        }
    }

    void ReleaseDetailsGpuWorkingSet() {
        if(!detailsGpuWorkingSetActive_) return;
        auto clearMedia=[](std::vector<MediaItem>& list){for(auto& item:list){item.detailsGpuThumb.Reset();item.detailsGpuThumbSource=nullptr;item.detailsGpuGeneration=0;}};
        clearMedia(videos_);clearMedia(images_);
        auto clearFrames=[](std::vector<PreviewFrame>& frames){for(auto& frame:frames){frame.gpuBitmap.Reset();frame.gpuBitmapSource=nullptr;frame.gpuGeneration=0;}};
        clearFrames(previewFrames_);for(auto& kv:prefetchedPreviewSets_)clearFrames(kv.second.frames);
        detailsGpuWorkingSetActive_=false;
    }

    bool PaintDetailsGpu(RECT rc) {
        if(!EnsureLibraryGpuRenderer(rc)) return false;
        previewHitRects_.clear();previewMediaHoverHits_.clear();previewZoomRect_=RECT{};detailsMediaRect_=RECT{};
        auto& list=CurrentItems(); if(selected_>=list.size()) return false; MediaItem& item=list[selected_];
        ClampDetailsScroll();
        const int footerTop=std::max(0,static_cast<int>(rc.bottom)-64),contentOffset=detailsScrollY_;int y=18-contentOffset;
        std::set<int> visiblePreviewGpuSeconds;
        libraryD2dTarget_->BeginDraw(); const D2D1_COLOR_F clearColor=D2DColor(RGB(13,15,20));libraryD2dTarget_->Clear(&clearColor);
        RECT title{40,y,rc.right-40,y+42};DrawLibraryGpuText(item.title,title,30,FW_BOLD);y+=54;

        const int heroH=item.isVideo?480:std::max(260,footerTop-150);RECT media{40,y,rc.right-40,y+heroH};
        if(!item.isVideo&&nativeImageSizing_)media=RECT{0,0,rc.right,rc.bottom};detailsMediaRect_=media;
        if(media.bottom>0&&media.top<footerTop){
            FillLibraryGpuRect(media,RGB(20,23,31));
            const int reqW=std::min(2560,std::max(1,static_cast<int>(media.right-media.left))),reqH=std::min(1440,std::max(1,static_cast<int>(media.bottom-media.top)));
            HBITMAP hbmp=nullptr;if(item.isVideo&&!PathExistsNoThrow(item.cachePath))hbmp=GetItemThumb(item,640,360);else hbmp=GetDetailsBanner(item);
            if(hbmp){
                ID2D1Bitmap* bmp=GetDetailsGpuBitmap(item,hbmp);
                if(!bmp){
                    const HRESULT fallbackHr=libraryD2dTarget_->EndDraw();
                    if(fallbackHr==D2DERR_RECREATE_TARGET) ResetLibraryGpuRenderer();
                    return false;
                }
                {
                    if(item.isVideo)DrawLibraryGpuBitmapCover(bmp,media);
                    else if(slideshowFadeActive_&&slideshowPreviousIndex_<images_.size()){
                        MediaItem& previousItem=images_[slideshowPreviousIndex_];HBITMAP prevH=nativeImageSizing_?GetDetailsBanner(previousItem):GetItemThumb(previousItem,reqW,reqH);
                        if(prevH){if(auto* prev=GetDetailsGpuBitmap(previousItem,prevH)){
                            if(nativeImageSizing_&&fullscreen_)DrawDetailsGpuBitmapContain(prev,media,1.0f,false,true);
                            else if(nativeImageSizing_)DrawDetailsGpuBitmapContain(prev,media,1.0f,true,false);
                            else DrawDetailsGpuBitmapContain(prev,media);
                        }}
                        const float progress=EaseUi(static_cast<float>(GetTickCount64()-slideshowFadeStart_)/static_cast<float>(kUiAnimationDurationMs));
                        if(nativeImageSizing_&&fullscreen_)DrawDetailsGpuBitmapContain(bmp,media,progress,false,true);
                        else if(nativeImageSizing_)DrawDetailsGpuBitmapContain(bmp,media,progress,true,false);
                        else DrawDetailsGpuBitmapContain(bmp,media,progress);
                    }else{
                        if(nativeImageSizing_&&fullscreen_)DrawDetailsGpuBitmapContain(bmp,media,1.0f,false,true);
                        else if(nativeImageSizing_)DrawDetailsGpuBitmapContain(bmp,media,1.0f,true,false);
                        else if(ImageZoomActive())DrawDetailsGpuBitmapContain(bmp,media,1.0f,false,false,imageZoomScale_,imageZoomCenterU_,imageZoomCenterV_);
                        else DrawDetailsGpuBitmapContain(bmp,media);
                    }
                    if(item.isVideo)DrawDetailsGpuVideoBadges(item,media);
                }
            }
            if(item.favorite)DrawLibraryGpuFavoriteBadge(media);
        }
        y+=heroH+22;

        if(item.isVideo){
            const int zoomTop=std::max(0,y);if(zoomTop<footerTop&&rc.right>80)previewZoomRect_=RECT{40,zoomTop,rc.right-40,footerTop};
            const int gap=12,cardW=DetailsPreviewCardWidthForViewport(static_cast<int>(rc.right-rc.left));
            const int imageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0))),labelH=24,cardH=imageH+labelH;
            const int availW=std::max(1,static_cast<int>(rc.right)-80),cols=std::max(1,(availW+gap)/(cardW+gap));
            if(previewFrames_.empty()){
                SetMediaHoverTarget(MediaHoverSurface::Preview,static_cast<size_t>(-1),RECT{},false);RECT note{40,y,rc.right-40,y+54};
                const bool complete=!previewDir_.empty()&&PreviewCacheIsComplete();DrawLibraryGpuText(complete?L"No secondary previews were available for this video.":L"Loading Timeline",note,14,FW_NORMAL,RGB(160,167,180));y+=64;
            }else{
                const int rows=static_cast<int>((previewFrames_.size()+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols)),rowStride=cardH+gap;
                const int startRow=std::max(0,((0-y)/std::max(1,rowStride))-1),endRow=std::min(rows-1,((footerTop-y)/std::max(1,rowStride))+1);
                POINT cursor{};const bool cursorValid=GetCursorPos(&cursor)!=FALSE&&ScreenToClient(hwnd_,&cursor)!=FALSE;bool hoverFound=false;size_t hoverId=static_cast<size_t>(-1);RECT hoverRect{};
                if(cursorValid){RECT viewport{0,0,rc.right,footerTop};for(int row=startRow;row<=endRow&&!hoverFound;++row){for(int col=0;col<cols;++col){const size_t i=static_cast<size_t>(row)*cols+col;if(i>=previewFrames_.size())break;RECT card{40+col*(cardW+gap),y+row*(cardH+gap),40+col*(cardW+gap)+cardW,y+row*(cardH+gap)+cardH};if(card.bottom<0||card.top>footerTop)continue;RECT hit{};if(IntersectRect(&hit,&card,&viewport)&&PtInRect(&hit,cursor)){hoverFound=true;hoverId=i;hoverRect=card;break;}}}}
                SetMediaHoverTarget(MediaHoverSurface::Preview,hoverId,hoverRect,hoverFound);
                for(int row=startRow;row<=endRow;++row){for(int col=0;col<cols;++col){const size_t i=static_cast<size_t>(row)*cols+col;if(i>=previewFrames_.size())break;RECT card{40+col*(cardW+gap),y+row*(cardH+gap),40+col*(cardW+gap)+cardW,y+row*(cardH+gap)+cardH};if(card.bottom<0||card.top>footerTop)continue;RECT viewport{0,0,rc.right,footerTop},hit{};if(IntersectRect(&hit,&card,&viewport)){previewHitRects_.push_back({hit,previewFrames_[i].seconds});previewMediaHoverHits_.push_back({hit,card,i});}if(card.right<=0||card.left>=rc.right)continue;FillLibraryGpuRound(card,RGB(28,32,42),9);RECT image=card;image.bottom=image.top+imageH;HBITMAP ph=GetPreviewBitmap(previewFrames_[i]);if(ph){visiblePreviewGpuSeconds.insert(previewFrames_[i].seconds);if(auto* gpu=GetPreviewGpuBitmap(previewFrames_[i],ph))DrawLibraryGpuBitmapCover(gpu,image);else FillLibraryGpuRect(image,RGB(43,48,61));}else FillLibraryGpuRect(image,RGB(43,48,61));RECT label{card.left+8,image.bottom,card.right-8,card.bottom};DrawLibraryGpuText(PreviewLabel(previewFrames_[i].seconds),label,11,FW_SEMIBOLD,RGB(200,206,218),DT_CENTER|DT_VCENTER|DT_SINGLELINE);DrawLibraryGpuHoverBorder(card,MediaHoverAmount(MediaHoverSurface::Preview,i,card),9);}}
                y+=rows*(cardH+gap)+10;TrimPreviewMemory();
            }
        }
        for(auto& frame:previewFrames_){if(frame.gpuBitmap&&visiblePreviewGpuSeconds.find(frame.seconds)==visiblePreviewGpuSeconds.end()){frame.gpuBitmap.Reset();frame.gpuBitmapSource=nullptr;frame.gpuGeneration=0;}}
        MediaItem* slideshowPreviousItem=(!item.isVideo&&slideshowFadeActive_&&slideshowPreviousIndex_<images_.size())?&images_[slideshowPreviousIndex_]:nullptr;
        auto trimHeroGpu=[&](std::vector<MediaItem>& items){for(auto& candidate:items){const bool keep=(&candidate==&item)||(&candidate==slideshowPreviousItem);if(!keep){candidate.detailsGpuThumb.Reset();candidate.detailsGpuThumbSource=nullptr;candidate.detailsGpuGeneration=0;}}};
        trimHeroGpu(videos_);trimHeroGpu(images_);
        detailsContentBottom_=y+20+contentOffset;

        constexpr int edgeW=48,edgeH=76,edgePad=16;const int edgeCenterY=footerTop/2,edgeTop=std::max(8,edgeCenterY-edgeH/2);
        detailsPrevRect_={edgePad,edgeTop,edgePad+edgeW,edgeTop+edgeH};detailsNextRect_={std::max(edgePad,static_cast<int>(rc.right)-edgePad-edgeW),edgeTop,std::max(edgePad+edgeW,static_cast<int>(rc.right)-edgePad),edgeTop+edgeH};
        DrawDetailsGpuEdgeArrowButton(detailsPrevRect_,false,CanNavigateDetailsMedia(-1));DrawDetailsGpuEdgeArrowButton(detailsNextRect_,true,CanNavigateDetailsMedia(1));

        detailsFooterRect_=RECT{0,footerTop,rc.right,rc.bottom};FillLibraryGpuRectAlpha(detailsFooterRect_,RGB(16,19,25),238.0f/255.0f);DrawLibraryGpuLine(0.0f,static_cast<float>(footerTop),static_cast<float>(rc.right),static_cast<float>(footerTop),RGB(42,47,60),1);
        backRect_=StandardBackRect(rc);DrawLibraryGpuButton(backRect_,L"Back");
        if(item.isVideo){imageDetailsSlideshowRect_=RECT{};const int gap=10,playW=145;playRect_={backRect_.right+gap,backRect_.top,backRect_.right+gap+playW,backRect_.bottom};DrawLibraryGpuButton(playRect_,L"Play",true);}else playRect_=RECT{};
        constexpr int footerIconW=48,footerGap=10,footerRightMargin=20;const int footerRight=std::max(0,static_cast<int>(rc.right)-footerRightMargin),iconBottom=rc.bottom-10,iconTop=iconBottom-footerIconW;
        detailsFullRect_={footerRight-footerIconW,iconTop,footerRight,iconBottom};DrawLibraryGpuFullscreenButton(detailsFullRect_);imageDetailsNativeRect_=RECT{};
        if(!item.isVideo){imageDetailsNativeRect_={detailsFullRect_.left-footerGap-footerIconW,iconTop,detailsFullRect_.left-footerGap,iconBottom};DrawDetailsGpuNativeSizeButton(imageDetailsNativeRect_);imageDetailsSlideshowRect_={imageDetailsNativeRect_.left-footerGap-footerIconW,iconTop,imageDetailsNativeRect_.left-footerGap,iconBottom};DrawLibraryGpuAutoAdvance(imageDetailsSlideshowRect_,slideshowActive_);}
        std::wstring meta=item.isVideo?(item.vr.vr?(item.vr.projection==2?L"VR180":L"VR"):L"Video"):L"Image";
        if(item.isVideo&&item.sourceWidth&&item.sourceHeight){meta+=L"  •  ";meta+=std::to_wstring(item.sourceWidth);meta+=L"×";meta+=std::to_wstring(item.sourceHeight);}
        const LONG metaLeftLimit=item.isVideo?playRect_.right+20:backRect_.right+20,metaRightLimit=(item.isVideo?detailsFullRect_.left:imageDetailsSlideshowRect_.left)-20;
        const LONG metaLeft=std::max<LONG>(metaLeftLimit,rc.right/2-150),metaRight=std::max<LONG>(metaLeft+40,std::min<LONG>(metaRightLimit,rc.right/2+150));RECT metaTop{metaLeft,rc.bottom-62,metaRight,rc.bottom-43};
        DrawLibraryGpuText(meta,metaTop,13,FW_SEMIBOLD,RGB(165,172,185),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.isVideo){const double duration=detailsDurationSeconds_.load(std::memory_order_relaxed);RECT durationRect{metaTop.left,rc.bottom-43,metaTop.right,rc.bottom-8};DrawLibraryGpuText(duration>0.0?FormatTime(duration):L"--:--",durationRect,22,FW_SEMIBOLD,RGB(205,210,220),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        PaintLibraryGpuLoadingPopup(rc);PaintLibraryGpuNotice(rc);
        const HRESULT hr=libraryD2dTarget_->EndDraw();if(hr==D2DERR_RECREATE_TARGET){ResetLibraryGpuRenderer();return false;}return SUCCEEDED(hr);
    }

    static constexpr uint64_t kMiB=1024ull*1024ull;
    static constexpr uint64_t kNormalProcessMemoryTarget=1024ull*kMiB;
    static constexpr uint64_t kProcessMemoryHighPressure=1152ull*kMiB;
    static constexpr uint64_t kProcessMemoryEmergency=1330ull*kMiB;
    // Begin aggressive cache eviction before 1.5 GiB of total process usage. Active
    // playback is exempt from a hard cap; these thresholds govern reconstructable state.
    static constexpr uint64_t kProcessMemoryAllocationGuard=1400ull*kMiB;
    static constexpr uint64_t kProcessMemoryPanicRelease=1450ull*kMiB;

    // These are cache-pressure boundaries, not caps on active playback. Decoder surfaces,
    // current video textures and Media Foundation buffers are working memory and must be
    // allowed to grow when high-resolution/VR media genuinely requires it. The policy
    // below sheds reconstructable Library/preview state around the player instead.

    uint64_t ProcessMemoryBytes() const {
        PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
        if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))) return 0;
        // Use the more conservative of private commit and resident working set. This keeps
        // the pressure policy aligned with both allocation growth and what users see as
        // process RAM in common task-manager views.
        return std::max<uint64_t>(static_cast<uint64_t>(counters.PrivateUsage),
                                  static_cast<uint64_t>(counters.WorkingSetSize));
    }

    bool LoadEverythingOwnsMemoryPressure() const {
        return fullLoadRunning_.load(std::memory_order_acquire) && mode_!=Mode::Player;
    }

    bool SystemMemoryCriticallyLow() const {
        MEMORYSTATUSEX state{}; state.dwLength=sizeof(state);
        if(!GlobalMemoryStatusEx(&state)) return false;
        // Load Everything may legitimately need multiple GiB for one high-resolution
        // decode. Only real machine-wide pressure should force the Library working set
        // to collapse while that explicit batch operation is running.
        return state.dwMemoryLoad>=97 || state.ullAvailPhys<384ull*kMiB;
    }

    uint64_t LibraryRamBudgetBytes(uint64_t processBytes) const {
        // Keep the normal decoded-thumbnail LRU during Load Everything. The batch
        // decoder's temporary working set must not shrink the Library cache merely
        // because total process RAM is high.
        if(LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow()) return 640ull*kMiB;
        uint64_t budget=640ull*kMiB;
        if(processBytes>=kProcessMemoryEmergency) budget=160ull*kMiB;
        else if(processBytes>=kProcessMemoryHighPressure) budget=320ull*kMiB;
        else if(processBytes>=kNormalProcessMemoryTarget) budget=448ull*kMiB;
        else if(processBytes>=900ull*kMiB) budget=560ull*kMiB;
        // Playback needs decoder/frame headroom more than it needs a deep Library cache.
        // Keep a warm CPU working set so returning to Library is still fast, but do not
        // carry the full browsing cache while a video is active.
        if(mode_==Mode::Player) budget=std::min<uint64_t>(budget,256ull*kMiB);
        return budget;
    }

    uint64_t LibraryGpuBudgetBytes(uint64_t processBytes) const {
        if(LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow()) return 192ull*kMiB;
        if(processBytes>=kProcessMemoryEmergency) return 64ull*kMiB;
        if(processBytes>=kProcessMemoryHighPressure) return 96ull*kMiB;
        if(processBytes>=kNormalProcessMemoryTarget) return 128ull*kMiB;
        return 192ull*kMiB;
    }

    void EnforceProcessMemoryBudget() {
        const ULONGLONG now=GetTickCount64();
        if(now-lastMemoryPressureCheck_<250) return;
        lastMemoryPressureCheck_=now;
        uint64_t processBytes=ProcessMemoryBytes();
        if(processBytes<kNormalProcessMemoryTarget) return;
        // Load Everything is explicitly allowed to use a large temporary decoder
        // working set. Keep ordinary Library LRU limits, but do not run the process-RAM
        // emergency/panic purge unless Windows itself is critically short of memory.
        if(LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow()) {
            TrimLibraryGpuTextures();
            TrimThumbMemory();
            return;
        }
        TrimLibraryGpuTextures();
        TrimThumbMemory();
        TrimPreviewMemory();
        processBytes=ProcessMemoryBytes();
        if(processBytes>=kProcessMemoryAllocationGuard){
            // At the internal 1.4 GiB guard keep only essential detail/Library state.
            // Active decoder/player memory is not forcibly reclaimed here.
            ClearPrefetchedPreviewSets();
            TrimDetailInfoToWindow(DetailWindowIndices());
            ResetResolutionMetadataWork();
            processBytes=ProcessMemoryBytes();
        }
        if(processBytes>=kProcessMemoryPanicRelease){
            // Never blank the current Library viewport just because process RAM is high.
            // Off-screen decoded copies are reconstructable and can be discarded, but a
            // banner that is on screen (or protected for Back -> Library) stays resident.
            auto purgeCold=[&](std::vector<MediaItem>& list){
                for(auto& item:list){
                    const bool visible=visibleLibraryGpuThumbPaths_.find(item.path)!=visibleLibraryGpuThumbPaths_.end();
                    const bool playbackWarm=playbackLibraryWarmPaths_.find(item.path)!=playbackLibraryWarmPaths_.end();
                    if(visible || playbackWarm) continue;
                    if(item.thumb){DeleteObject(item.thumb);item.thumb=nullptr;}
                    item.libraryGpuThumb.Reset();item.libraryGpuThumbSource=nullptr;item.libraryGpuGeneration=0;
                    item.thumbW=item.thumbH=0;item.thumbAttempted=false;item.thumbFromPrivateCache=false;
                    item.thumbLoadRequestEpoch=0;item.thumbNextLoadAttempt=0;
                }
            };
            purgeCold(videos_);purgeCold(images_);
            ClearPrefetchedPreviewSets();
            TrimDetailInfoToWindow(DetailWindowIndices());
            TrimLibraryGpuTextures();
            if(hwnd_) InvalidateRect(hwnd_,nullptr,FALSE);
        }
        // If active playback itself is responsible for the remaining memory, stop here.
        // The player/decoder is intentionally not reset or trimmed by this cache policy.
    }

    std::set<std::wstring> CaptureLibraryReturnWarmPaths(int marginRows=2) {
        std::set<std::wstring> warm;
        if(!hwnd_) return warm;
        RECT rc{};GetClientRect(hwnd_,&rc);
        const auto& filtered=FilteredIndices();
        const auto visibleFolders=VisibleFolderIndices();
        const size_t totalCards=visibleFolders.size()+filtered.size();
        if(totalCards==0) return warm;
        const auto& list=CurrentItems();
        const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
        const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int rowStride=imageH+kLibraryTitleHeight+gap;
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
        const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap));
        const int rows=static_cast<int>((totalCards+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
        if(rows<=0) return warm;
        const int visibleBottom=std::max(pad,static_cast<int>(rc.bottom)-68);
        const int firstVisible=std::clamp(scrollY_/std::max(1,rowStride)-1,0,rows-1);
        const int lastVisible=std::clamp((scrollY_+std::max(0,visibleBottom-pad))/std::max(1,rowStride)+1,0,rows-1);
        const int firstRow=std::max(0,firstVisible-std::max(0,marginRows));
        const int lastRow=std::min(rows-1,lastVisible+std::max(0,marginRows));
        for(int row=firstRow;row<=lastRow;++row){
            const size_t rowFirst=static_cast<size_t>(row)*static_cast<size_t>(cols);
            const size_t rowLast=std::min(totalCards,rowFirst+static_cast<size_t>(cols));
            for(size_t displayIndex=rowFirst;displayIndex<rowLast;++displayIndex){
                if(displayIndex<visibleFolders.size()) continue;
                const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
                if(mediaDisplayIndex>=filtered.size()) continue;
                const size_t itemIndex=filtered[mediaDisplayIndex];
                if(itemIndex<list.size()) warm.insert(list[itemIndex].path);
            }
        }
        if(category_==Category::Videos && selected_<videos_.size()) warm.insert(videos_[selected_].path);
        return warm;
    }

    void TrimForPlayback() {
        // Preserve the exact Library viewport plus a small row margin in CPU RAM. This
        // keeps Back -> Library visually warm without retaining the deep browsing cache.
        playbackLibraryWarmPaths_=CaptureLibraryReturnWarmPaths(2);
        ResetLibraryThumbLoadView();
        protectedLibraryThumbPaths_=playbackLibraryWarmPaths_;
        visibleLibraryGpuThumbPaths_.clear();
        // GPU Library residency has no value while video covers the Library; it can be
        // reconstructed quickly from the protected CPU thumbnails on return.
        ResetLibraryGpuRenderer();
        ClearPrefetchedPreviewSets();
        TrimPreviewMemory();
        TrimThumbMemory();
    }

    void TrimLibraryGpuTextures() {
        const uint64_t processBytes=ProcessMemoryBytes();
        const uint64_t budget=LibraryGpuBudgetBytes(processBytes);
        struct Entry{MediaItem* item;ULONGLONG used;uint64_t bytes;bool visible;bool nearby;};
        std::vector<Entry> entries;uint64_t total=0;
        auto collect=[&](std::vector<MediaItem>& list){
            for(auto& item:list){
                if(!item.libraryGpuThumb) continue;
                const uint64_t bytes=static_cast<uint64_t>(std::max(1,item.thumbW))*static_cast<uint64_t>(std::max(1,item.thumbH))*4ull;
                const bool visible=visibleLibraryGpuThumbPaths_.find(item.path)!=visibleLibraryGpuThumbPaths_.end();
                const bool nearby=protectedLibraryThumbPaths_.find(item.path)!=protectedLibraryThumbPaths_.end();
                total+=bytes;entries.push_back({&item,item.thumbLastUsed,bytes,visible,nearby});
            }
        };
        collect(videos_);collect(images_);
        auto release=[&](Entry& e){if(!e.item->libraryGpuThumb)return;e.item->libraryGpuThumb.Reset();e.item->libraryGpuThumbSource=nullptr;e.item->libraryGpuGeneration=0;total=total>e.bytes?total-e.bytes:0;};
        // Distant GPU copies are never permanent cache entries. This is independent of
        // the byte budget and guarantees viewport/nearby residency semantics.
        for(auto& e:entries) if(!e.nearby) release(e);
        if(processBytes>=kProcessMemoryEmergency && !(LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow())){for(auto& e:entries) if(!e.visible) release(e);}
        if(total<=budget) return;
        std::sort(entries.begin(),entries.end(),[](const Entry&a,const Entry&b){if(a.visible!=b.visible)return !a.visible;return a.used<b.used;});
        for(auto& e:entries){if(total<=budget)break;if(e.visible)continue;release(e);}
    }

    bool PaintLibraryGpu(RECT rc) {
        if(!EnsureLibraryGpuRenderer(rc)) return false;
        ReleaseDetailsGpuWorkingSet();
        auto& mutableList=CurrentItems();const auto& filtered=FilteredIndices();const auto visibleFolders=VisibleFolderIndices();
        const size_t totalCards=visibleFolders.size()+filtered.size();
        libraryMediaHoverHits_.clear();libraryReturnHighlightRect_=RECT{};visibleLibraryGpuThumbPaths_.clear();
        RefreshLibraryThumbViewport(rc);QueuePriorityResolutionMetadataForSearch();protectedLibraryThumbPaths_.clear();

        libraryD2dTarget_->BeginDraw();
        const D2D1_COLOR_F clearColor=D2DColor(RGB(13,15,20));libraryD2dTarget_->Clear(&clearColor);

        if(totalCards==0){
            std::wstring msg;
            if(folder_.empty() || currentFolder_.empty()) msg=L"Choose a folder to load videos and images.";
            else if(!searchQuery_.empty()) msg=L"No matching media.";
            else msg=category_==Category::Videos?L"No videos or subfolders here.":L"No images or subfolders here.";
            RECT mr{40,40,rc.right-40,128};DrawLibraryGpuText(msg,mr,25,FW_SEMIBOLD,RGB(180,185,197),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            PaintLibraryGpuNavigator(rc);if(searchVisible_)PaintLibraryGpuSearch(rc);PaintLibraryGpuLoadingPopup(rc);PaintLibraryGpuNotice(rc);
            const HRESULT hr=libraryD2dTarget_->EndDraw();if(hr==D2DERR_RECREATE_TARGET){ResetLibraryGpuRenderer();return false;}return SUCCEEDED(hr);
        }

        const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
        const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0))),cardH=imageH+kLibraryTitleHeight,rowStride=cardH+gap;
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
        const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap));
        const int rows=static_cast<int>((totalCards+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
        const int startY=pad-scrollY_,visibleBottom=std::max(pad,static_cast<int>(rc.bottom)-68);
        const int firstVisibleRow=std::clamp(scrollY_/std::max(1,rowStride)-1,0,std::max(0,rows-1));
        const int lastVisibleRow=std::clamp((scrollY_+std::max(0,visibleBottom-pad))/std::max(1,rowStride)+1,0,std::max(0,rows-1));
        const size_t firstDisplay=static_cast<size_t>(firstVisibleRow)*static_cast<size_t>(cols),lastDisplay=std::min(totalCards,static_cast<size_t>(lastVisibleRow+1)*static_cast<size_t>(cols));

        POINT cursor{};const bool cursorValid=GetCursorPos(&cursor)!=FALSE&&ScreenToClient(hwnd_,&cursor)!=FALSE;bool hoverFound=false;size_t hoverId=static_cast<size_t>(-1);RECT hoverRect{};
        if(cursorValid){RECT viewport{0,pad,rc.right,visibleBottom};for(size_t displayIndex=firstDisplay;displayIndex<lastDisplay;++displayIndex){if(displayIndex<visibleFolders.size())continue;const int col=static_cast<int>(displayIndex)%cols,row=static_cast<int>(displayIndex)/cols;RECT card{pad+col*(cardW+gap),startY+row*rowStride,pad+col*(cardW+gap)+cardW,startY+row*rowStride+cardH};RECT hit{};if(!IntersectRect(&hit,&card,&viewport)||!PtInRect(&hit,cursor))continue;const size_t mdi=displayIndex-visibleFolders.size();if(mdi>=filtered.size())continue;hoverFound=true;hoverId=filtered[mdi];hoverRect=card;break;}}
        SetMediaHoverTarget(MediaHoverSurface::Library,hoverId,hoverRect,hoverFound);

        for(size_t displayIndex=firstDisplay;displayIndex<lastDisplay;++displayIndex){
            const int col=static_cast<int>(displayIndex)%cols,row=static_cast<int>(displayIndex)/cols;
            RECT card{pad+col*(cardW+gap),startY+row*rowStride,pad+col*(cardW+gap)+cardW,startY+row*rowStride+cardH};
            if(card.bottom<pad||card.top>visibleBottom)continue;
            if(displayIndex<visibleFolders.size()){DrawLibraryGpuFolderCard(folders_[visibleFolders[displayIndex]],card);continue;}
            const size_t mdi=displayIndex-visibleFolders.size();if(mdi>=filtered.size())continue;const size_t i=filtered[mdi];MediaItem& item=mutableList[i];
            protectedLibraryThumbPaths_.insert(item.path);visibleLibraryGpuThumbPaths_.insert(item.path);
            RECT viewport{0,pad,rc.right,visibleBottom},hit{};if(IntersectRect(&hit,&card,&viewport))libraryMediaHoverHits_.push_back({hit,card,i});
            FillLibraryGpuRound(card,RGB(31,35,46),12);RECT image=card;image.bottom=image.top+imageH;
            HBITMAP thumb=GetLibraryItemThumb(item,i,640,360,true);
            if(thumb){if(ID2D1Bitmap* gpu=GetLibraryGpuThumb(item,thumb))DrawLibraryGpuBitmapCover(gpu,image);else{FillLibraryGpuRect(image,RGB(43,48,61));}}
            else{FillLibraryGpuRect(image,RGB(43,48,61));}
            RECT title{card.left+10,image.bottom+2,card.right-10,card.bottom-3};DrawLibraryGpuText(item.title,title,14,FW_SEMIBOLD);
            if(item.favorite)DrawLibraryGpuFavoriteBadge(image);DrawLibraryGpuVideoBadges(item,card);
            const float returnAmount=LibraryReturnHighlightAmount(i);if(returnAmount>0.0f)libraryReturnHighlightRect_=card;
            DrawLibraryGpuHoverBorder(card,std::max(MediaHoverAmount(MediaHoverSurface::Library,i,card),returnAmount),12);
        }

        const uint64_t processBytes=ProcessMemoryBytes();
        int prefetchRows=10;
        if(LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow()) prefetchRows=2;
        else if(processBytes>=kProcessMemoryEmergency)prefetchRows=0;
        else if(processBytes>=kProcessMemoryHighPressure)prefetchRows=2;
        else if(processBytes>=kNormalProcessMemoryTarget)prefetchRows=5;
        auto queueRows=[&](int firstRow,int lastRow){firstRow=std::max(0,firstRow);lastRow=std::min(rows-1,lastRow);if(firstRow>lastRow)return;for(int row=firstRow;row<=lastRow;++row){const size_t rowFirst=static_cast<size_t>(row)*static_cast<size_t>(cols),rowLast=std::min(totalCards,rowFirst+static_cast<size_t>(cols));for(size_t displayIndex=rowFirst;displayIndex<rowLast;++displayIndex){if(displayIndex<visibleFolders.size())continue;const size_t mdi=displayIndex-visibleFolders.size();if(mdi>=filtered.size())continue;const size_t i=filtered[mdi];protectedLibraryThumbPaths_.insert(mutableList[i].path);GetLibraryItemThumb(mutableList[i],i,640,360);}}};
        if(prefetchRows>0){if(libraryThumbPrefetchDirection_>=0){queueRows(lastVisibleRow+1,lastVisibleRow+prefetchRows);queueRows(firstVisibleRow-prefetchRows,firstVisibleRow-1);}else{queueRows(firstVisibleRow-prefetchRows,firstVisibleRow-1);queueRows(lastVisibleRow+1,lastVisibleRow+prefetchRows);}}

        RECT topMask{0,0,rc.right,kLibraryPad};FillLibraryGpuRect(topMask,RGB(13,15,20));
        PaintLibraryGpuScrollbar(rc);PaintLibraryGpuNavigator(rc);if(searchVisible_)PaintLibraryGpuSearch(rc);PaintLibraryGpuLoadingPopup(rc);PaintLibraryGpuNotice(rc);
        const HRESULT drawHr=libraryD2dTarget_->EndDraw();if(drawHr==D2DERR_RECREATE_TARGET){ResetLibraryGpuRenderer();return false;}if(FAILED(drawHr))return false;
        // The normal Library viewport/prefetch set is live again, so return-specific
        // protection is no longer needed; normal LRU/prefetch behavior resumes here.
        playbackLibraryWarmPaths_.clear();
        TrimLibraryGpuTextures();TrimThumbMemory();TrimPreviewMemory();return true;
    }

    void Paint() {
        paintOwner_ = hwnd_;
        // Preserve the true update region, not merely its bounding rectangle. Fullscreen
        // scrolling invalidates a few narrow strips; collapsing those into one 4K-sized
        // rectangle defeats the benefit and makes fullscreen feel heavier than windowed.
        HRGN updateRegion=CreateRectRgn(0,0,0,0);
        const int updateRegionType=updateRegion?GetUpdateRgn(hwnd_,updateRegion,FALSE):ERROR;
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
        RECT rc{}; GetClientRect(hwnd_, &rc);
        const int w = std::max(1, static_cast<int>(rc.right-rc.left));
        const int h = std::max(1, static_cast<int>(rc.bottom-rc.top));
        if(mode_==Mode::Library && UseGpuLibraryRenderer(rc) && PaintLibraryGpu(rc)){
            EndPaint(hwnd_,&ps);
            if(updateRegion) DeleteObject(updateRegion);
            return;
        }
        if(mode_==Mode::Details && PaintDetailsGpu(rc)){
            EndPaint(hwnd_,&ps);
            if(updateRegion) DeleteObject(updateRegion);
            return;
        }
        EnsureBackBuffer(dc,w,h);

        RECT dirty = ps.rcPaint;
        if (IsRectEmpty(&dirty)) dirty = rc;
        const int savedDc = SaveDC(backDC_);
        if(updateRegion && updateRegionType!=ERROR && updateRegionType!=NULLREGION)
            SelectClipRgn(backDC_,updateRegion);
        else
            IntersectClipRect(backDC_, dirty.left, dirty.top, dirty.right, dirty.bottom);
        HBRUSH bg = CreateSolidBrush(RGB(13,15,20)); FillRect(backDC_,&rc,bg); DeleteObject(bg);
        SetBkMode(backDC_, TRANSPARENT);
        SetTextColor(backDC_, RGB(238,241,247));

        if (mode_ == Mode::Library) PaintLibrary(backDC_,rc);
        else if (mode_ == Mode::Details) PaintDetails(backDC_,rc);
        PaintLoadingPopup(backDC_, rc);
        PaintInAppNotice(backDC_, rc);
        RestoreDC(backDC_, savedDc);

        BitBlt(dc, dirty.left, dirty.top, dirty.right-dirty.left, dirty.bottom-dirty.top,
               backDC_, dirty.left, dirty.top, SRCCOPY);
        EndPaint(hwnd_,&ps);
        if(updateRegion) DeleteObject(updateRegion);
    }

    RECT AppNoticeRect(RECT rc) const {
        const bool compact=appNoticeText_.size()<=32;
        const int desiredW=compact?360:520;
        const int boxW=std::max(240,std::min(desiredW,std::max(240,static_cast<int>(rc.right)-40)));
        const int boxH=compact?54:64;
        const int right=std::max(20,static_cast<int>(rc.right)-18);
        const int top=(mode_==Mode::Library && searchVisible_)?68:18;
        return RECT{std::max(8,right-boxW),top,right,top+boxH};
    }

    static float BreathingHighlightAmountWithCadence(ULONGLONG elapsed, ULONGLONG duration, ULONGLONG cadenceDuration) {
        if(duration==0 || cadenceDuration==0 || elapsed>=duration) return 0.0f;
        // Keep the exact return-highlight cadence: four breaths every three seconds.
        // Longer-lived callers may extend the lifetime without slowing the breath rate.
        const float phaseT=static_cast<float>(elapsed)/static_cast<float>(cadenceDuration);
        const float breath=0.5f-0.5f*std::cos(8.0f*PI_F*phaseT);
        float envelope=1.0f;
        const ULONGLONG edgeMs=std::max<ULONGLONG>(1,static_cast<ULONGLONG>(std::llround(static_cast<double>(cadenceDuration)*0.06)));
        if(elapsed<edgeMs){
            const float x=std::clamp(static_cast<float>(elapsed)/static_cast<float>(edgeMs),0.0f,1.0f);
            envelope=x*x*(3.0f-2.0f*x);
        }else if(duration-elapsed<edgeMs){
            const float x=std::clamp(static_cast<float>(duration-elapsed)/static_cast<float>(edgeMs),0.0f,1.0f);
            envelope=x*x*(3.0f-2.0f*x);
        }
        return envelope*(0.28f+0.72f*breath);
    }

    static float BreathingHighlightAmount(ULONGLONG elapsed, ULONGLONG duration) {
        return BreathingHighlightAmountWithCadence(elapsed,duration,duration);
    }

    float AppNoticePulseAmount(ULONGLONG now) const {
        if(appNoticeStart_==0 || appNoticeUntil_<=appNoticeStart_ || now<appNoticeStart_ || now>=appNoticeUntil_) return 0.0f;
        const ULONGLONG elapsed=now-appNoticeStart_;
        if(elapsed>=kAppNoticePulseDurationMs) return 0.0f;
        // Same speed/intensity as the existing three-second return highlight, but
        // continue that exact cadence for the notice's full five-second lifetime.
        return BreathingHighlightAmountWithCadence(elapsed,kAppNoticePulseDurationMs,kLibraryReturnHighlightDurationMs);
    }

    void ShowInAppNotice(const std::wstring& text, ULONGLONG durationMs=5000) {
        appNoticeText_=text;
        appNoticeStart_=GetTickCount64();
        appNoticeUntil_=appNoticeStart_+durationMs;
        if(hwnd_){
            SetTimer(hwnd_,kAppNoticeTimerId,static_cast<UINT>(std::min<ULONGLONG>(durationMs,60000)),nullptr);
            StartUiAnimationTimer();
            InvalidateRect(hwnd_,nullptr,FALSE);
        }
    }

    void ClearInAppNotice() {
        appNoticeText_.clear(); appNoticeUntil_=0; appNoticeStart_=0;
        if(hwnd_) KillTimer(hwnd_,kAppNoticeTimerId);
    }

    void PaintInAppNotice(HDC dc, RECT rc) {
        const ULONGLONG now=GetTickCount64();
        if(appNoticeText_.empty() || now>=appNoticeUntil_) return;
        const RECT box=AppNoticeRect(rc);
        const float pulse=AppNoticePulseAmount(now);
        FillRound(dc,box,RGB(31,35,46),12);
        // Pulse for the entire notice lifetime with the exact same border renderer
        // used by the return-from-Info highlight.
        DrawMediaHoverBorder(dc,box,pulse,12);
        RECT text{box.left+16,box.top+8,box.right-16,box.bottom-8};
        DrawTextSimple(dc,appNoticeText_,text,15,FW_SEMIBOLD,RGB(238,241,247),DT_CENTER|DT_VCENTER|DT_WORDBREAK);
    }

    void SetLoadingState(int kind, int current, int total) {
        loadingCurrent_.store(std::max(0,current), std::memory_order_relaxed);
        loadingTotal_.store(std::max(0,total), std::memory_order_relaxed);
        loadingKind_.store(kind, std::memory_order_release);
        if(hwnd_) PostMessageW(hwnd_, WM_APP_THUMB_READY, 0, 0);
    }

    void ClearLoadingState() {
        loadingKind_.store(0, std::memory_order_release);
        loadingCurrent_.store(0, std::memory_order_relaxed);
        loadingTotal_.store(0, std::memory_order_relaxed);
        if(hwnd_) PostMessageW(hwnd_, WM_APP_THUMB_READY, 0, 0);
    }

    void ClearLoadingStateIf(int kind) {
        int expected=kind;
        if(loadingKind_.compare_exchange_strong(expected,0,std::memory_order_acq_rel)) {
            loadingCurrent_.store(0,std::memory_order_relaxed);
            loadingTotal_.store(0,std::memory_order_relaxed);
            if(hwnd_) PostMessageW(hwnd_, WM_APP_THUMB_READY, 0, 0);
        }
    }

    void PaintLoadingPopup(HDC dc, RECT rc) {
        if(mode_==Mode::Player) return;

        std::wstring label,count;
        const bool fullRunning=fullLoadRunning_.load(std::memory_order_acquire);
        const ULONGLONG now=GetTickCount64();
        const bool fullDoneVisible=!fullRunning && fullLoadFinishedAt_!=0 && now-fullLoadFinishedAt_<kFullLoadDonePopupDurationMs;
        if(fullRunning) {
            label=L"Loading everything";
            const int current=fullLoadCurrent_.load(std::memory_order_relaxed);
            const int total=fullLoadTotal_.load(std::memory_order_relaxed);
            count=total>0?std::to_wstring(std::min(current,total))+L" / "+std::to_wstring(total)+L" files":L"Working...";
        } else if(fullDoneVisible) {
            const int failures=fullLoadFailures_.load(std::memory_order_relaxed);
            label=L"Load everything finished";
            if(failures==0) count=L"All files ready";
            else count=std::to_wstring(failures)+(failures==1?L" file could not be prepared":L" files could not be prepared");
        } else {
            const int kind=loadingKind_.load(std::memory_order_acquire);
            if(kind==0) return;
            const int current=loadingCurrent_.load(std::memory_order_relaxed);
            const int total=loadingTotal_.load(std::memory_order_relaxed);
            if(kind==1) label=L"Loading library banners";
            else if(kind==2) label=L"Loading secondary images";
            else if(kind==3) label=L"Loading info banner";
            else return;
            count=total>0?std::to_wstring(std::min(current,total))+L" / "+std::to_wstring(total):L"Working...";
        }

        const int width=300,height=58,right=std::max(20,static_cast<int>(rc.right)-18);
        const int top=(mode_==Mode::Library && searchVisible_)?68:18;
        RECT box{std::max(8,right-width),top,right,top+height};
        FillRound(dc,box,RGB(25,29,38),11);
        RECT labelRect{box.left+14,box.top+5,box.right-14,box.top+30};
        DrawTextSimple(dc,label,labelRect,13,FW_SEMIBOLD,RGB(238,241,247));
        RECT countRect{box.left+14,box.top+28,box.right-14,box.bottom-5};
        DrawTextSimple(dc,count,countRect,12,FW_NORMAL,RGB(165,172,185));
    }

    void DeferBackgroundWork(ULONGLONG milliseconds = 280) {
        const ULONGLONG wanted = GetTickCount64() + milliseconds;
        ULONGLONG current = backgroundPauseUntil_.load(std::memory_order_relaxed);
        while (current < wanted && !backgroundPauseUntil_.compare_exchange_weak(current, wanted, std::memory_order_release, std::memory_order_relaxed)) {}
    }

    bool WaitForBackgroundPermit(const std::atomic<bool>& stop, bool heavyBatch = false) const {
        while (!stop.load(std::memory_order_acquire)) {
            // Ordinary background producers respect the process allocation guard. Load
            // Everything is different: one VR/8K decode may legitimately exceed it, so
            // that batch waits only for genuine machine-wide memory exhaustion.
            if(heavyBatch) {
                if(SystemMemoryCriticallyLow()){ Sleep(100); continue; }
            } else if(ProcessMemoryBytes()>=kProcessMemoryAllocationGuard){ Sleep(20); continue; }
            const ULONGLONG until = backgroundPauseUntil_.load(std::memory_order_acquire);
            const ULONGLONG now = GetTickCount64();
            if (until <= now) return true;
            const ULONGLONG remaining = until - now;
            Sleep(static_cast<DWORD>(std::min<ULONGLONG>(20, remaining)));
        }
        return false;
    }


    void PaintControlsWindow() {
        if (!controlsHwnd_ || !playerControlsVisible_) return;
        paintOwner_ = controlsHwnd_;
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(controlsHwnd_, &ps);
        RECT rc{}; GetClientRect(controlsHwnd_, &rc);
        const int w = std::max(1, static_cast<int>(rc.right - rc.left));
        const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP buffer = CreateCompatibleBitmap(dc, w, h);
        HGDIOBJ oldBitmap = SelectObject(mem, buffer);
        HBRUSH bg = CreateSolidBrush(RGB(13,15,20)); FillRect(mem,&rc,bg); DeleteObject(bg);
        SetBkMode(mem, TRANSPARENT);
        PaintPlayerControls(mem, rc);
        BitBlt(dc,0,0,w,h,mem,0,0,SRCCOPY);
        SelectObject(mem,oldBitmap); DeleteObject(buffer); DeleteDC(mem);
        EndPaint(controlsHwnd_,&ps);
    }

    void PaintEdgeArrowWindow(HWND h) {
        if (!h || !playerControlsVisible_) return;
        paintOwner_ = h;
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(h, &ps);
        RECT rc{}; GetClientRect(h, &rc);
        const bool next = h == playerNextHwnd_;
        DrawEdgeArrowButton(dc, rc, next, CanNavigatePlayerMedia(next ? 1 : -1));
        EndPaint(h, &ps);
    }

    HFONT GetFont(int px, int weight=FW_NORMAL) {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(px)) << 32) | static_cast<uint32_t>(weight);
        const auto it = fontCache_.find(key);
        if (it != fontCache_.end()) return it->second;
        HFONT f = CreateFontW(-px,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        fontCache_[key] = f;
        return f;
    }

    void DrawTextSimple(HDC dc, const std::wstring& s, RECT r, int size, int weight=FW_NORMAL, COLORREF color=RGB(238,241,247), UINT fmt=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
        HFONT f=GetFont(size,weight); HGDIOBJ old=SelectObject(dc,f); SetTextColor(dc,color); DrawTextW(dc,s.c_str(),-1,&r,fmt | DT_NOPREFIX); SelectObject(dc,old);
    }

    void FillRound(HDC dc, RECT r, COLORREF color, int radius=10) {
        HRGN region = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
        HBRUSH b = CreateSolidBrush(color); FillRgn(dc, region, b); DeleteObject(b); DeleteObject(region);
    }

    static bool SameRect(const RECT& a, const RECT& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }

    static bool EmptyRectValue(const RECT& r) {
        return r.right <= r.left || r.bottom <= r.top;
    }

    static float EaseUi(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const float inv = 1.0f - t;
        return 1.0f - inv*inv*inv;
    }

    static COLORREF MixColor(COLORREF a, COLORREF b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const auto mix = [t](BYTE x, BYTE y) -> BYTE {
            return static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(x + (y-x)*t)), 0, 255));
        };
        return RGB(mix(GetRValue(a),GetRValue(b)), mix(GetGValue(a),GetGValue(b)), mix(GetBValue(a),GetBValue(b)));
    }

    void DrawMediaHoverBorder(HDC dc, RECT r, float amount, int radius) {
        amount=std::clamp(amount,0.0f,1.0f);
        if(amount<=0.001f || EmptyRectValue(r)) return;

        // Media hover/focus never alters the thumbnail itself.  Animate one common
        // border treatment for Library cards, secondary timeline cards and the
        // brief return-to-Library focus marker.
        const COLORREF border=MixColor(RGB(74,81,96),RGB(238,242,250),amount);
        HRGN round=CreateRoundRectRgn(r.left,r.top,r.right+1,r.bottom+1,radius,radius);
        if(!round) return;
        HBRUSH brush=CreateSolidBrush(border);
        if(brush){
            FrameRgn(dc,round,brush,3,3);
            DeleteObject(brush);
        }
        DeleteObject(round);
    }

    float LibraryReturnHighlightAmount(size_t mediaIndex) const {
        if(libraryReturnHighlightStart_==0 || libraryReturnHighlightCategory_!=category_ || libraryReturnHighlightIndex_!=mediaIndex) return 0.0f;
        const ULONGLONG elapsed=GetTickCount64()-libraryReturnHighlightStart_;
        if(elapsed>=kLibraryReturnHighlightDurationMs) return 0.0f;

        return BreathingHighlightAmount(elapsed,kLibraryReturnHighlightDurationMs);
    }

    float ButtonHoverAmount(RECT r) const {
        const ULONGLONG now = GetTickCount64();
        float t = 1.0f;
        if (hoverTransitionStart_ != 0)
            t = EaseUi(static_cast<float>(now-hoverTransitionStart_) / static_cast<float>(kUiAnimationDurationMs));
        if (paintOwner_ == hoverOwner_ && SameRect(r, hoverRect_)) return t;
        if (paintOwner_ == hoverPreviousOwner_ && SameRect(r, hoverPreviousRect_)) return 1.0f-t;
        return 0.0f;
    }

    void DrawButton(HDC dc, RECT r, const wchar_t* text, bool primary=false) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=primary ? RGB(235,238,245) : RGB(38,43,55);
        const COLORREF over=primary ? RGB(250,251,253) : RGB(53,59,73);
        FillRound(dc, r, MixColor(base,over,hover), 10);
        DrawTextSimple(dc,text,r,14,FW_SEMIBOLD,primary?RGB(12,14,19):RGB(245,246,250),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    void DrawEdgeArrowButton(HDC dc, RECT r, bool next, bool enabled=true) {
        const float hover = enabled ? ButtonHoverAmount(r) : 0.0f;
        const COLORREF base = enabled ? RGB(38,43,55) : RGB(25,28,36);
        const COLORREF over = enabled ? RGB(53,59,73) : base;
        FillRound(dc, r, MixColor(base,over,hover), 12);
        const COLORREF fg = enabled ? RGB(245,246,250) : RGB(92,98,111);
        HPEN pen=CreatePen(PS_SOLID,4,fg); HGDIOBJ old=SelectObject(dc,pen);
        const int cx=(r.left+r.right)/2;
        const int cy=(r.top+r.bottom)/2;
        const int dx=next?5:-5;
        if(next){
            MoveToEx(dc,cx-7+dx,cy-14,nullptr); LineTo(dc,cx+7+dx,cy); LineTo(dc,cx-7+dx,cy+14);
        } else {
            MoveToEx(dc,cx+7+dx,cy-14,nullptr); LineTo(dc,cx-7+dx,cy); LineTo(dc,cx+7+dx,cy+14);
        }
        SelectObject(dc,old); DeleteObject(pen);
    }

    void DrawTab(HDC dc, RECT r, const wchar_t* text, bool active) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=active ? RGB(235,238,245) : RGB(29,33,43);
        const COLORREF over=active ? RGB(250,251,253) : RGB(47,52,65);
        FillRound(dc, r, MixColor(base,over,hover), 12);
        DrawTextSimple(dc, text, r, 15, FW_BOLD, active ? RGB(12,14,19) : RGB(230,233,241), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }


    std::unique_ptr<Gdiplus::Bitmap> LoadPngBitmapFromResource(int resId) {
        HRSRC hrsrc = FindResourceW(inst_, MAKEINTRESOURCEW(resId), L"PNG");
        if (!hrsrc) return {};
        const DWORD size = SizeofResource(inst_, hrsrc);
        if (!size) return {};
        HGLOBAL hres = LoadResource(inst_, hrsrc);
        if (!hres) return {};
        const void* src = LockResource(hres);
        if (!src) return {};

        HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hmem) return {};
        void* dst = GlobalLock(hmem);
        if (!dst) { GlobalFree(hmem); return {}; }
        std::memcpy(dst, src, size);
        GlobalUnlock(hmem);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hmem, TRUE, &stream))) {
            GlobalFree(hmem);
            return {};
        }

        std::unique_ptr<Gdiplus::Bitmap> copy;
        {
            std::unique_ptr<Gdiplus::Bitmap> decoded(Gdiplus::Bitmap::FromStream(stream, FALSE));
            if (decoded && decoded->GetLastStatus() == Gdiplus::Ok && decoded->GetWidth() > 0 && decoded->GetHeight() > 0) {
                copy = std::make_unique<Gdiplus::Bitmap>(decoded->GetWidth(), decoded->GetHeight(), PixelFormat32bppARGB);
                if (copy && copy->GetLastStatus() == Gdiplus::Ok) {
                    Gdiplus::Graphics g(copy.get());
                    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                    g.DrawImage(decoded.get(), 0, 0, decoded->GetWidth(), decoded->GetHeight());
                } else {
                    copy.reset();
                }
            }
        }
        stream->Release();
        return copy;
    }

    void LoadUiIcons() {
        folderIconBitmap_ = LoadPngBitmapFromResource(IDR_FOLDER_PNG);
        refreshIconBitmap_ = LoadPngBitmapFromResource(IDR_REFRESH_PNG);
        if (refreshIconBitmap_) ForceBitmapWhitePreserveAlpha(refreshIconBitmap_.get());
        skipBackIconBitmap_ = LoadPngBitmapFromResource(IDR_SKIP_BACK_30_PNG);
        skipForwardIconBitmap_ = LoadPngBitmapFromResource(IDR_SKIP_FORWARD_30_PNG);
        downloadIconBitmap_ = LoadPngBitmapFromResource(IDR_DOWNLOAD_PNG);
        if (downloadIconBitmap_) ForceBitmapWhitePreserveAlpha(downloadIconBitmap_.get());
        resolution4kBitmap_ = LoadPngBitmapFromResource(IDR_RESOLUTION_4K_PNG);
        resolution5kBitmap_ = LoadPngBitmapFromResource(IDR_RESOLUTION_5K_PNG);
        resolution8kBitmap_ = LoadPngBitmapFromResource(IDR_RESOLUTION_8K_PNG);
        vrBadgeBitmap_ = LoadPngBitmapFromResource(IDR_VR_PNG);
        vrBadgeWhiteBitmap_ = LoadPngBitmapFromResource(IDR_VR_PNG);
        if (vrBadgeWhiteBitmap_) ForceBitmapWhitePreserveAlpha(vrBadgeWhiteBitmap_.get());
        resizeIconBitmap_ = LoadPngBitmapFromResource(IDR_RESIZE_PNG);
        resizeIconWhiteBitmap_ = LoadPngBitmapFromResource(IDR_RESIZE_PNG);
        if (resizeIconWhiteBitmap_) ForceBitmapWhitePreserveAlpha(resizeIconWhiteBitmap_.get());
        // Use the user-supplied heart PNG exactly as embedded: no tint, redraw or generated replacement.
        favoriteIconBitmap_ = LoadPngBitmapFromResource(IDR_FAVORITE_PNG);
    }

    void ForceBitmapWhitePreserveAlpha(Gdiplus::Bitmap* bmp) {
        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) return;
        Gdiplus::Rect rect(0, 0, static_cast<INT>(bmp->GetWidth()), static_cast<INT>(bmp->GetHeight()));
        Gdiplus::BitmapData data{};
        if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return;
        for (UINT y = 0; y < data.Height; ++y) {
            auto* row = reinterpret_cast<BYTE*>(data.Scan0) + static_cast<INT_PTR>(y) * static_cast<INT_PTR>(data.Stride);
            for (UINT x = 0; x < data.Width; ++x) {
                BYTE* px = row + static_cast<size_t>(x) * 4u;
                const BYTE a = px[3];
                if (a == 0) continue;
                px[0] = 255;
                px[1] = 255;
                px[2] = 255;
                px[3] = a;
            }
        }
        bmp->UnlockBits(&data);
    }

    void DrawBitmapCentered(HDC dc, RECT r, Gdiplus::Bitmap* bmp, int insetX = 8, int insetY = 8) {
        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) return;
        const float availW = static_cast<float>(std::max(1L, (r.right - r.left) - insetX * 2));
        const float availH = static_cast<float>(std::max(1L, (r.bottom - r.top) - insetY * 2));
        const float bw = static_cast<float>(bmp->GetWidth());
        const float bh = static_cast<float>(bmp->GetHeight());
        if (bw <= 0.0f || bh <= 0.0f) return;
        const float scale = std::min(availW / bw, availH / bh);
        const float dw = std::max(1.0f, bw * scale);
        const float dh = std::max(1.0f, bh * scale);
        const float dx = static_cast<float>(r.left + insetX) + (availW - dw) * 0.5f;
        const float dy = static_cast<float>(r.top + insetY) + (availH - dh) * 0.5f;
        Gdiplus::Graphics g(dc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        g.DrawImage(bmp, Gdiplus::RectF(dx, dy, dw, dh));
    }


    void DrawFavoriteBadge(HDC dc, RECT imageRect) {
        if(!favoriteIconBitmap_) return;
        RECT badge{imageRect.left+8,imageRect.top+8,imageRect.left+42,imageRect.top+42};
        DrawBitmapCentered(dc,badge,favoriteIconBitmap_.get(),0,0);
    }


    void DrawFolderIconButton(HDC dc, RECT r) {
        const float hover=ButtonHoverAmount(r);
        FillRound(dc,r,MixColor(RGB(38,43,55),RGB(53,59,73),hover),10);
        if (folderIconBitmap_) {
            DrawBitmapCentered(dc, r, folderIconBitmap_.get(), 9, 9);
            return;
        }
        const COLORREF fg=RGB(245,246,250);
        const int cx=(r.left+r.right)/2, cy=(r.top+r.bottom)/2;
        RECT tab{cx-13,cy-10,cx-2,cy-2};
        RECT body{cx-15,cy-5,cx+15,cy+12};
        FillRound(dc,tab,fg,4);
        FillRound(dc,body,fg,5);
    }

    void DrawRefreshIconButton(HDC dc, RECT r) {
        const float hover=ButtonHoverAmount(r);
        FillRound(dc,r,MixColor(RGB(38,43,55),RGB(53,59,73),hover),10);
        if (refreshIconBitmap_) {
            DrawBitmapCentered(dc, r, refreshIconBitmap_.get(), 9, 7);
            return;
        }
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::Color gc(static_cast<Gdiplus::ARGB>(0xFFF5F6FAu));
        Gdiplus::Pen pen(gc,3.0f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        const float cx=(r.left+r.right)*0.5f, cy=(r.top+r.bottom)*0.5f;
        Gdiplus::RectF arc(cx-13.0f,cy-13.0f,26.0f,26.0f);
        g.DrawArc(&pen,arc,-55.0f,155.0f);
        g.DrawArc(&pen,arc,125.0f,155.0f);
        Gdiplus::SolidBrush brush(gc);
        Gdiplus::PointF head1[3]{{cx+13.0f,cy-2.0f},{cx+6.0f,cy-5.0f},{cx+10.0f,cy+3.0f}};
        Gdiplus::PointF head2[3]{{cx-13.0f,cy+2.0f},{cx-6.0f,cy+5.0f},{cx-10.0f,cy-3.0f}};
        g.FillPolygon(&brush,head1,3);
        g.FillPolygon(&brush,head2,3);
    }

    void DrawDownloadIconButton(HDC dc, RECT r) {
        const float hover=ButtonHoverAmount(r);
        FillRound(dc,r,MixColor(RGB(38,43,55),RGB(53,59,73),hover),10);
        if(downloadIconBitmap_){
            DrawBitmapCentered(dc,r,downloadIconBitmap_.get(),9,9);
            return;
        }
        // Resource-load fallback: keep the control usable and visually consistent.
        HPEN pen=CreatePen(PS_SOLID,3,RGB(245,246,250));
        HGDIOBJ oldPen=SelectObject(dc,pen);
        const int cx=(r.left+r.right)/2;
        const int cy=(r.top+r.bottom)/2;
        MoveToEx(dc,cx,cy-11,nullptr); LineTo(dc,cx,cy+7);
        MoveToEx(dc,cx-7,cy,nullptr); LineTo(dc,cx,cy+7); LineTo(dc,cx+7,cy);
        MoveToEx(dc,cx-11,cy+11,nullptr); LineTo(dc,cx-11,cy+14); LineTo(dc,cx+11,cy+14); LineTo(dc,cx+11,cy+11);
        SelectObject(dc,oldPen); DeleteObject(pen);
    }

    bool NativeVideoSizingAvailable() const {
        return mode_==Mode::Player && player_ && !player_->VR().vr;
    }

    bool NativeImageSizingAvailable() const {
        return mode_==Mode::Details && category_==Category::Images && selected_<images_.size();
    }

    void DrawNativeSizeButton(HDC dc, RECT r) {
        const float hover=ButtonHoverAmount(r);
        const bool active=nativeVideoSizing_ || nativeImageSizing_;
        const COLORREF base=active ? RGB(235,238,245) : RGB(38,43,55);
        FillRound(dc,r,MixColor(base,active?RGB(250,251,253):RGB(53,59,73),hover),10);
        Gdiplus::Bitmap* icon=active ? resizeIconBitmap_.get() : resizeIconWhiteBitmap_.get();
        if(icon){
            DrawBitmapCentered(dc,r,icon,9,9);
            return;
        }
        // Resource fallback: four inward corners communicate "fit/native size".
        const COLORREF c=active ? RGB(12,14,19) : RGB(245,246,250);
        HPEN pen=CreatePen(PS_SOLID,3,c); HGDIOBJ old=SelectObject(dc,pen);
        const int x1=r.left+12,x2=r.right-12,y1=r.top+12,y2=r.bottom-12,arm=7;
        MoveToEx(dc,x1+arm,y1,nullptr); LineTo(dc,x1,y1); LineTo(dc,x1,y1+arm);
        MoveToEx(dc,x2-arm,y1,nullptr); LineTo(dc,x2,y1); LineTo(dc,x2,y1+arm);
        MoveToEx(dc,x1,y2-arm,nullptr); LineTo(dc,x1,y2); LineTo(dc,x1+arm,y2);
        MoveToEx(dc,x2,y2-arm,nullptr); LineTo(dc,x2,y2); LineTo(dc,x2-arm,y2);
        SelectObject(dc,old); DeleteObject(pen);
    }

    void DrawFullscreenButton(HDC dc, RECT r) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=fullscreen_ ? RGB(235,238,245) : RGB(38,43,55);
        FillRound(dc,r,MixColor(base,fullscreen_?RGB(250,251,253):RGB(53,59,73),hover),10);
        const COLORREF c=fullscreen_ ? RGB(12,14,19) : RGB(245,246,250);
        HPEN pen=CreatePen(PS_SOLID,3,c); HGDIOBJ old=SelectObject(dc,pen);
        const int x1=r.left+11,x2=r.right-11,y1=r.top+10,y2=r.bottom-10,arm=8;
        MoveToEx(dc,x1+arm,y1,nullptr); LineTo(dc,x1,y1); LineTo(dc,x1,y1+arm);
        MoveToEx(dc,x2-arm,y1,nullptr); LineTo(dc,x2,y1); LineTo(dc,x2,y1+arm);
        MoveToEx(dc,x1,y2-arm,nullptr); LineTo(dc,x1,y2); LineTo(dc,x1+arm,y2);
        MoveToEx(dc,x2,y2-arm,nullptr); LineTo(dc,x2,y2); LineTo(dc,x2-arm,y2);
        SelectObject(dc,old); DeleteObject(pen);
    }

    void DrawVrProjectionToggle(HDC dc, RECT r) {
        if (!player_ || !player_->VR().vr) return;
        const bool full360 = player_->IsVr360Enabled();
        DrawButton(dc, r, full360 ? L"360°" : L"180°", full360);
    }

    void DrawAutoAdvanceIcon(HDC dc, RECT r, bool active) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=active ? RGB(235,238,245) : RGB(38,43,55);
        FillRound(dc, r, MixColor(base,active?RGB(250,251,253):RGB(53,59,73),hover), 10);
        COLORREF c = active ? RGB(12,14,19) : RGB(245,246,250);
        HPEN pen=CreatePen(PS_SOLID,4,c); HGDIOBJ old=SelectObject(dc,pen);
        const int cy=(r.top+r.bottom)/2;
        const int x=r.left+11;
        MoveToEx(dc,x,cy-10,nullptr); LineTo(dc,x+12,cy); LineTo(dc,x,cy+10);
        MoveToEx(dc,x+14,cy-10,nullptr); LineTo(dc,x+26,cy); LineTo(dc,x+14,cy+10);
        SelectObject(dc,old); DeleteObject(pen);
    }

    void DrawAutoNextIcon(HDC dc, RECT r) {
        DrawAutoAdvanceIcon(dc,r,autoNext_);
    }

    void DrawPlayPauseIcon(HDC dc, RECT r) {
        FillRound(dc, r, MixColor(RGB(239,241,246),RGB(255,255,255),ButtonHoverAmount(r)), 14);
        HBRUSH b=CreateSolidBrush(RGB(10,12,17)); HGDIOBJ old=SelectObject(dc,b);
        if (player_ && player_->IsPaused()) {
            POINT pts[3]{{r.left+18,r.top+13},{r.left+18,r.bottom-13},{r.right-14,(r.top+r.bottom)/2}};
            Polygon(dc,pts,3);
        } else {
            RECT a{r.left+16,r.top+13,r.left+23,r.bottom-13};
            RECT d{r.right-23,r.top+13,r.right-16,r.bottom-13};
            FillRect(dc,&a,b); FillRect(dc,&d,b);
        }
        SelectObject(dc,old); DeleteObject(b);
    }

    void DrawSkip30Icon(HDC dc, RECT r, bool forward) {
        FillRound(dc,r,MixColor(RGB(239,241,246),RGB(255,255,255),ButtonHoverAmount(r)),14);
        Gdiplus::Bitmap* icon = forward ? skipForwardIconBitmap_.get() : skipBackIconBitmap_.get();
        if (icon) {
            DrawBitmapCentered(dc, r, icon, 8, 8);
            return;
        }
        const COLORREF fg=RGB(245,246,250);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::Color gc(static_cast<Gdiplus::ARGB>(0xFFF5F6FAu));
        Gdiplus::Pen pen(gc,3.4f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        const float cx=(r.left+r.right)*0.5f;
        const float cy=(r.top+r.bottom)*0.5f;
        constexpr float radius=14.5f;
        Gdiplus::RectF arc(cx-radius,cy-radius,radius*2.0f,radius*2.0f);
        const float startAngle=forward?120.0f:60.0f;
        const float sweep=forward?285.0f:-285.0f;
        g.DrawArc(&pen,arc,startAngle,sweep);

        const float endAngle=(startAngle+sweep)*PI_F/180.0f;
        const float tipX=cx+radius*std::cos(endAngle);
        const float tipY=cy+radius*std::sin(endAngle);
        float tx=forward?-std::sin(endAngle):std::sin(endAngle);
        float ty=forward? std::cos(endAngle):-std::cos(endAngle);
        const float len=std::sqrt(tx*tx+ty*ty);
        if(len>0.001f){tx/=len;ty/=len;}
        const float nx=-ty, ny=tx;
        constexpr float headLen=8.0f, headHalf=5.0f;
        Gdiplus::PointF arrow[3]{
            {tipX+tx*2.0f,tipY+ty*2.0f},
            {tipX-tx*headLen+nx*headHalf,tipY-ty*headLen+ny*headHalf},
            {tipX-tx*headLen-nx*headHalf,tipY-ty*headLen-ny*headHalf}
        };
        Gdiplus::SolidBrush brush(gc);
        g.FillPolygon(&brush,arrow,3);

        RECT label{r.left+4,r.top+8,r.right-4,r.bottom-4};
        DrawTextSimple(dc,L"30",label,12,FW_BOLD,fg,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    std::wstring FormatTime(double seconds) {
        if (!(seconds >= 0.0) || !std::isfinite(seconds)) seconds = 0.0;
        const long long total = static_cast<long long>(seconds + 0.5);
        const long long h = total / 3600, m = (total % 3600) / 60, sec = total % 60;
        wchar_t buf[64]{};
        if (h > 0) swprintf_s(buf, L"%lld:%02lld:%02lld", h, m, sec);
        else swprintf_s(buf, L"%lld:%02lld", m, sec);
        return buf;
    }

    void PaintPlayerControls(HDC dc, RECT rc) {
        if (!player_ || !playerControlsVisible_) return;
        HBRUSH bg=CreateSolidBrush(RGB(16,18,24)); FillRect(dc,&rc,bg); DeleteObject(bg);
        HPEN topLine=CreatePen(PS_SOLID,1,RGB(52,57,69)); HGDIOBJ oldPen=SelectObject(dc,topLine);
        MoveToEx(dc,0,0,nullptr); LineTo(dc,rc.right,0); SelectObject(dc,oldPen); DeleteObject(topLine);

        const int cy=(seekRect_.top+seekRect_.bottom)/2;
        RECT base{seekRect_.left,cy-2,seekRect_.right,cy+2}; FillRound(dc,base,RGB(72,78,92),4);
        const double frac=std::clamp(seekFraction_,0.0,1.0);
        RECT progress=base; progress.right=progress.left+static_cast<LONG>((progress.right-progress.left)*frac);
        if(progress.right>progress.left) FillRound(dc,progress,RGB(235,238,245),4);
        const int knobX=seekRect_.left+static_cast<int>((seekRect_.right-seekRect_.left)*frac);
        HBRUSH kb=CreateSolidBrush(RGB(250,251,253)); HGDIOBJ oldBrush=SelectObject(dc,kb);
        Ellipse(dc,knobX-6,cy-6,knobX+7,cy+7); SelectObject(dc,oldBrush); DeleteObject(kb);

        // Seek-drag preview: show the exact target time only while the timeline is held.
        if (seekHoverVisible_ && player_->Duration() > 0.0) {
            const int seekWidth = std::max(1, static_cast<int>(seekRect_.right - seekRect_.left));
            const int hoverX = std::clamp(seekHoverX_, static_cast<int>(seekRect_.left), static_cast<int>(seekRect_.right));
            const double hoverFraction = static_cast<double>(hoverX - seekRect_.left) / static_cast<double>(seekWidth);
            const std::wstring hoverTime = FormatTime(player_->Duration() * std::clamp(hoverFraction, 0.0, 1.0));

            constexpr int tipW = 74;
            constexpr int tipH = 28;
            int tipLeft = hoverX - tipW / 2;
            tipLeft = std::clamp(tipLeft, static_cast<int>(rc.left + 8), static_cast<int>(rc.right - tipW - 8));
            RECT tip{tipLeft, seekRect_.top - tipH - 5, tipLeft + tipW, seekRect_.top - 5};
            FillRound(dc, tip, RGB(38,43,55), 9);
            DrawTextSimple(dc, hoverTime, tip, 12, FW_SEMIBOLD, RGB(245,246,250), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }

        DrawButton(dc,playerBackRect_,L"Back");
        DrawVrProjectionToggle(dc,playerVrToggleRect_);
        DrawSkip30Icon(dc,playerSkipBackRect_,false);
        DrawPlayPauseIcon(dc,playerPlayRect_);
        DrawSkip30Icon(dc,playerSkipForwardRect_,true);
        DrawAutoNextIcon(dc,playerAutoNextRect_);
        if(NativeVideoSizingAvailable()) DrawNativeSizeButton(dc,playerNativeSizeRect_);
        DrawFullscreenButton(dc,playerFullRect_);

        const std::wstring time=FormatTime(player_->CurrentTime())+L" / "+FormatTime(player_->Duration());
        DrawTextSimple(dc,time,playerTimeRect_,13,FW_NORMAL,RGB(190,195,206),DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        DrawTextSimple(dc,L"VOL",volumeLabelRect_,10,FW_BOLD,RGB(170,176,189),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        const int vy=(volumeRect_.top+volumeRect_.bottom)/2;
        RECT vb{volumeRect_.left,vy-2,volumeRect_.right,vy+2}; FillRound(dc,vb,RGB(72,78,92),4);
        RECT vf=vb; vf.right=vf.left+static_cast<LONG>((vf.right-vf.left)*std::clamp(volumeFraction_,0.0,1.0));
        if(vf.right>vf.left) FillRound(dc,vf,RGB(235,238,245),4);
        const int vx=volumeRect_.left+static_cast<int>((volumeRect_.right-volumeRect_.left)*std::clamp(volumeFraction_,0.0,1.0));
        HBRUSH vbk=CreateSolidBrush(RGB(250,251,253)); oldBrush=SelectObject(dc,vbk);
        Ellipse(dc,vx-5,vy-5,vx+6,vy+6); SelectObject(dc,oldBrush); DeleteObject(vbk);

        // Volume-drag preview: show percentage only while the slider is held.
        if (volumeDragging_) {
            const int percent = std::clamp(static_cast<int>(std::lround(volumeFraction_ * 100.0)), 0, 100);
            wchar_t percentText[16]{};
            swprintf_s(percentText, L"%d%%", percent);

            constexpr int tipW = 58;
            constexpr int tipH = 28;
            int tipLeft = vx - tipW / 2;
            tipLeft = std::clamp(tipLeft, static_cast<int>(rc.left + 8), static_cast<int>(rc.right - tipW - 8));
            RECT tip{tipLeft, volumeRect_.top - tipH - 7, tipLeft + tipW, volumeRect_.top - 7};
            FillRound(dc, tip, RGB(38,43,55), 9);
            DrawTextSimple(dc, percentText, tip, 12, FW_SEMIBOLD, RGB(245,246,250), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }

    static bool IsImageExtension(const std::wstring& extRaw) {
        const std::wstring ext=ToLower(extRaw);
        // Common, legacy, HDR, vector and camera-RAW image extensions. Some require
        // a Windows codec/thumbnail provider; failed decodes remain non-destructive.
        static const wchar_t* kExts[]={
            L".jpg", L".jpeg", L".jpe", L".jfif", L".jif", L".jfi", L".png", L".apng", L".bmp", L".dib", L".gif", L".tif", L".tiff", L".webp", L".heic", L".heif", L".hif", L".avif", L".avifs", L".jxl", L".jp2", L".j2k", L".j2c", L".jpf", L".jpx", L".jpm", L".jxr", L".wdp", L".hdp", L".tga", L".targa", L".icb", L".vda", L".vst", L".dds", L".pcx", L".ico", L".cur", L".mng", L".psd", L".psb", L".exr", L".hdr", L".rgbe", L".pic", L".pfm", L".pnm", L".ppm", L".pgm", L".pbm", L".pam", L".qoi", L".sgi", L".rgb", L".rgba", L".bw", L".ras", L".sun", L".xbm", L".xpm", L".svg", L".svgz", L".dng", L".cr2", L".cr3", L".crw", L".nef", L".nrw", L".arw", L".srf", L".sr2", L".raf", L".orf", L".rw2", L".rwl", L".pef", L".x3f", L".3fr", L".fff", L".iiq", L".erf", L".mef", L".mos", L".mrw", L".kdc", L".dcr", L".raw", L".srw", L".bay", L".cap", L".eip", L".mdc", L".rwz"
        };
        for(auto* e:kExts) if(ext==e) return true;
        return false;
    }

    static uint64_t Fnv1a64(const std::wstring& s) {
        uint64_t h=1469598103934665603ull;
        for(wchar_t c:s){ h^=static_cast<uint64_t>(c); h*=1099511628211ull; }
        return h;
    }

    fs::path CacheRootForSource(const std::wstring& source) const {
        // Keep generated media data beside the media itself. This means a folder that
        // only contains subfolders is never touched just because the user navigated
        // through it; only a folder containing an actual media file can receive a cache.
        return fs::path(source).parent_path() / L".visualmediaplayer-cache";
    }

    std::wstring BuildCachePath(const std::wstring& source) const {
        std::wstring sig=L"detail-info-native-v12-fullsource-10pct-sharedtime|"+source;
        std::error_code ec;
        auto sz=fs::file_size(source,ec); if(!ec) sig+=L"|"+std::to_wstring(sz);
        ec.clear(); auto ft=fs::last_write_time(source,ec); if(!ec) sig+=L"|"+std::to_wstring(ft.time_since_epoch().count());
        const uint64_t hash=Fnv1a64(sig);
        wchar_t name[40]{}; swprintf_s(name,L"%016llx.jpg",static_cast<unsigned long long>(hash));
        return (CacheRootForSource(source)/L"thumbs"/name).wstring();
    }

    std::wstring BuildUiCachePath(const std::wstring& source) const {
        std::wstring sig=L"grid-v10-vr-crop-stereo-lock-640-at-10pct-exacttime|"+source;
        std::error_code ec;
        auto sz=fs::file_size(source,ec); if(!ec) sig+=L"|"+std::to_wstring(sz);
        ec.clear(); auto ft=fs::last_write_time(source,ec); if(!ec) sig+=L"|"+std::to_wstring(ft.time_since_epoch().count());
        const uint64_t hash=Fnv1a64(sig);
        wchar_t name[48]{}; swprintf_s(name,L"%016llx.ui.jpg",static_cast<unsigned long long>(hash));
        return (CacheRootForSource(source)/L"thumbs"/name).wstring();
    }


    std::wstring BuildFavoriteMetadataPath(const std::wstring& source) const {
        const std::wstring normalized=ToLower(fs::path(source).lexically_normal().wstring());
        const uint64_t hash=Fnv1a64(L"favorite-v1|"+normalized);
        wchar_t name[40]{}; swprintf_s(name,L"%016llx.favorite",static_cast<unsigned long long>(hash));
        return (CacheRootForSource(source)/L"favorites"/name).wstring();
    }

    bool ReadFavoriteMetadata(const std::wstring& source) const {
        const fs::path path=fs::path(BuildFavoriteMetadataPath(source));
        std::error_code ec;
        if(!fs::is_regular_file(path,ec) || ec) return false;
        std::ifstream in(path,std::ios::binary);
        std::string tag;
        return static_cast<bool>(in>>tag) && tag=="VMPFAV1";
    }

    bool WriteFavoriteMetadata(MediaItem& item, bool favorite) {
        const fs::path target=fs::path(BuildFavoriteMetadataPath(item.path));
        if(favorite){
            std::error_code ec;
            fs::create_directories(target.parent_path(),ec);
            if(ec) return false;
            const fs::path tmp=target.wstring()+L".tmp";
            {
                std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
                if(!out) return false;
                out<<"VMPFAV1\n";
                if(!out){ std::error_code rm; fs::remove(tmp,rm); return false; }
            }
            DeleteFileW(target.c_str());
            if(!MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
                DeleteFileW(tmp.c_str());
                return false;
            }
            HideCacheRootIfCreated(item.path);
            item.favorite=true;
        }else{
            DeleteFileW(target.c_str());
            item.favorite=false;
        }
        filterDirty_=true;
        return true;
    }

    void ToggleFavorite(MediaItem& item) {
        if(!WriteFavoriteMetadata(item,!item.favorite)) return;
        if(mode_==Mode::Library) ClampScroll();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }


    static std::wstring BannerTimestampPath(const std::wstring& uiCachePath) {
        return uiCachePath + L".time";
    }

    static bool ReadBannerTimestamp(const std::wstring& uiCachePath, double& seconds) {
        seconds = -1.0;
        std::ifstream in(fs::path(BannerTimestampPath(uiCachePath)), std::ios::binary);
        double value = -1.0;
        if (!(in >> value) || !std::isfinite(value) || value < 0.0) return false;
        seconds = value;
        return true;
    }

    static void WriteBannerTimestamp(const std::wstring& uiCachePath, double seconds) {
        if (!std::isfinite(seconds) || seconds < 0.0) return;
        const std::wstring timePath = BannerTimestampPath(uiCachePath);
        std::ofstream out(fs::path(timePath), std::ios::binary | std::ios::trunc);
        if (out) out << seconds;
    }

    static std::wstring ResolutionMetadataPath(const std::wstring& uiCachePath) {
        return uiCachePath + L".resolution";
    }

    static bool ReadResolutionMetadata(const std::wstring& uiCachePath, UINT& width, UINT& height) {
        width=height=0;
        std::ifstream in(fs::path(ResolutionMetadataPath(uiCachePath)),std::ios::binary);
        std::string tag;
        unsigned long long w=0,h=0;
        if(!(in>>tag>>w>>h) || tag!="VMPRES1" || w==0 || h==0 || w>65535ull || h>65535ull) return false;
        width=static_cast<UINT>(w); height=static_cast<UINT>(h);
        return true;
    }

    static bool WriteResolutionMetadata(const std::wstring& uiCachePath, UINT width, UINT height) {
        if(uiCachePath.empty() || !width || !height) return false;
        const fs::path target=fs::path(ResolutionMetadataPath(uiCachePath));
        std::error_code ec; fs::create_directories(target.parent_path(),ec); if(ec) return false;
        const fs::path tmp=target.wstring()+L".tmp";
        {
            std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
            if(!out) return false;
            out<<"VMPRES1 "<<width<<" "<<height<<"\n";
            if(!out) { std::error_code rm; fs::remove(tmp,rm); return false; }
        }
        DeleteFileW(target.c_str());
        if(!MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(tmp.c_str()); return false;
        }
        return true;
    }

    static bool CacheFileLooksHealthy(const std::wstring& path, uintmax_t minimumBytes = 256) {
        if (path.empty()) return false;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) return false;
        const uintmax_t size = fs::file_size(path, ec);
        return !ec && size >= minimumBytes;
    }

    static void RemoveGeneratedCacheFile(const std::wstring& path) {
        if (path.empty()) return;
        DeleteFileW(path.c_str());
    }

    std::wstring BuildPreviewDirectory(const std::wstring& source) const {
        std::wstring sig=L"details-previews-v5-stereo-lock|"+source;
        std::error_code ec;
        auto sz=fs::file_size(source,ec); if(!ec) sig+=L"|"+std::to_wstring(sz);
        ec.clear(); auto ft=fs::last_write_time(source,ec); if(!ec) sig+=L"|"+std::to_wstring(ft.time_since_epoch().count());
        const uint64_t hash=Fnv1a64(sig);
        wchar_t name[40]{}; swprintf_s(name,L"%016llx",static_cast<unsigned long long>(hash));
        return (CacheRootForSource(source)/L"previews"/name).wstring();
    }

    void HideCacheRootIfCreated(const std::wstring& source) const {
        if (source.empty()) return;
        const fs::path root=CacheRootForSource(source);
        std::error_code ec;
        if (!fs::exists(root,ec)) return;
        const DWORD attrs=GetFileAttributesW(root.c_str());
        if (attrs==INVALID_FILE_ATTRIBUTES) return;
        if ((attrs&FILE_ATTRIBUTE_HIDDEN)==0) SetFileAttributesW(root.c_str(),attrs|FILE_ATTRIBUTE_HIDDEN);
    }

    static std::wstring CacheCleanupPathKey(const fs::path& path) {
        return ToLower(path.lexically_normal().wstring());
    }

    static bool IsHexCacheHash(const std::wstring& text) {
        if (text.size()!=16) return false;
        for (wchar_t c:text) {
            if (!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F'))) return false;
        }
        return true;
    }

    static bool IsManagedThumbCacheFilename(const std::wstring& rawName) {
        const std::wstring name=ToLower(rawName);
        if (name.size()<20 || !IsHexCacheHash(name.substr(0,16))) return false;
        const std::wstring suffix=name.substr(16);
        // Only names created by Visual MediaPlayer are eligible for orphan cleanup.
        // Unknown files are deliberately left alone even inside our cache directory.
        return suffix==L".jpg" || suffix==L".jpg.time" ||
               suffix==L".ui.jpg" || suffix==L".ui.jpg.time" || suffix==L".ui.jpg.resolution" || suffix==L".ui.jpg.resolution.tmp" ||
               suffix==L".jpg.tmp.jpg" || suffix==L".jpg.tmp.jpg.time" ||
               suffix==L".ui.jpg.tmp.jpg" || suffix==L".ui.jpg.tmp.jpg.time" ||
               suffix==L".jpg.tmp.jpg.tmp.jpg" || suffix==L".ui.jpg.tmp.jpg.tmp.jpg";
    }

    static bool IsManagedPreviewCacheFilename(const std::wstring& rawName) {
        const std::wstring name=ToLower(rawName);
        if (name==L"complete.txt" || name==L"layout.txt") return true;
        size_t i=0;
        while (i<name.size() && name[i]>=L'0' && name[i]<=L'9') ++i;
        if (i<6) return false;
        const std::wstring suffix=name.substr(i);
        return suffix==L".jpg" || suffix==L".jpg.tmp";
    }

    static bool IsRealDirectoryWithoutReparsePoint(const fs::path& path) {
        const DWORD attrs=GetFileAttributesW(path.c_str());
        return attrs!=INVALID_FILE_ATTRIBUTES &&
               (attrs&FILE_ATTRIBUTE_DIRECTORY)!=0 &&
               (attrs&FILE_ATTRIBUTE_REPARSE_POINT)==0;
    }

    void CleanupOrphanMediaCache(const std::vector<fs::path>& discoveredCacheRoots) {
        // Deletion-only and fail-closed: never create a cache during cleanup, never
        // traverse reparse points, and never delete unknown files.
        if (folder_.empty() || discoveredCacheRoots.empty()) return;
        std::error_code rootEc;
        if (!fs::is_directory(folder_,rootEc) || rootEc) return;

        std::set<std::wstring> expectedThumbFiles;
        std::set<std::wstring> expectedPreviewDirs;
        auto remember=[&](const MediaItem& item) {
            if (item.isVideo && !item.cachePath.empty()) {
                expectedThumbFiles.insert(CacheCleanupPathKey(fs::path(item.cachePath)));
                expectedThumbFiles.insert(CacheCleanupPathKey(fs::path(BannerTimestampPath(item.cachePath))));
            }
            if (!item.uiCachePath.empty()) {
                expectedThumbFiles.insert(CacheCleanupPathKey(fs::path(item.uiCachePath)));
                if(item.isVideo) {
                    expectedThumbFiles.insert(CacheCleanupPathKey(fs::path(BannerTimestampPath(item.uiCachePath))));
                    expectedThumbFiles.insert(CacheCleanupPathKey(fs::path(ResolutionMetadataPath(item.uiCachePath))));
                }
            }
            if (item.isVideo) expectedPreviewDirs.insert(CacheCleanupPathKey(fs::path(BuildPreviewDirectory(item.path))));
        };
        for (const auto& item:videos_) remember(item);
        for (const auto& item:images_) remember(item);

        for (const fs::path& rawRoot:discoveredCacheRoots) {
            const fs::path cacheRoot=rawRoot.lexically_normal();
            if (ToLower(cacheRoot.filename().wstring())!=L".visualmediaplayer-cache") continue;
            if (!PathIsWithin(cacheRoot.wstring(),folder_)) continue;
            if (!IsRealDirectoryWithoutReparsePoint(cacheRoot)) continue;

            const fs::path thumbs=cacheRoot/L"thumbs";
            if (IsRealDirectoryWithoutReparsePoint(thumbs)) {
                std::vector<fs::path> orphanThumbs;
                bool safe=true;
                std::error_code ec;
                fs::directory_iterator it(thumbs,fs::directory_options::skip_permission_denied,ec),end;
                if (ec) safe=false;
                for (; safe && it!=end; it.increment(ec)) {
                    if (ec) { safe=false; break; }
                    std::error_code entryEc;
                    if (!it->is_regular_file(entryEc) || entryEc) { if(entryEc) safe=false; continue; }
                    if (!IsManagedThumbCacheFilename(it->path().filename().wstring())) continue;
                    if (expectedThumbFiles.find(CacheCleanupPathKey(it->path()))==expectedThumbFiles.end()) orphanThumbs.push_back(it->path());
                }
                if (safe) for (const auto& path:orphanThumbs) { std::error_code removeEc; fs::remove(path,removeEc); }
            }

            const fs::path previews=cacheRoot/L"previews";
            if (IsRealDirectoryWithoutReparsePoint(previews)) {
                std::vector<fs::path> orphanDirs;
                bool safe=true;
                std::error_code ec;
                fs::directory_iterator it(previews,fs::directory_options::skip_permission_denied,ec),end;
                if(ec) safe=false;
                for(;safe && it!=end;it.increment(ec)) {
                    if(ec){safe=false;break;}
                    std::error_code entryEc;
                    if(!it->is_directory(entryEc)||entryEc){if(entryEc)safe=false;continue;}
                    if(!IsHexCacheHash(it->path().filename().wstring())) continue;
                    if(!IsRealDirectoryWithoutReparsePoint(it->path())) continue;
                    if(expectedPreviewDirs.find(CacheCleanupPathKey(it->path()))==expectedPreviewDirs.end()) orphanDirs.push_back(it->path());
                }
                if(safe) {
                    for(const auto& dir:orphanDirs) {
                        std::vector<fs::path> generated;
                        bool dirSafe=true;
                        std::error_code dirEc;
                        fs::directory_iterator pit(dir,fs::directory_options::skip_permission_denied,dirEc),pend;
                        if(dirEc) dirSafe=false;
                        for(;dirSafe && pit!=pend;pit.increment(dirEc)) {
                            if(dirEc){dirSafe=false;break;}
                            std::error_code entryEc;
                            if(!pit->is_regular_file(entryEc)||entryEc){if(entryEc)dirSafe=false;continue;}
                            if(IsManagedPreviewCacheFilename(pit->path().filename().wstring())) generated.push_back(pit->path());
                        }
                        if(!dirSafe) continue;
                        for(const auto& path:generated){std::error_code removeEc;fs::remove(path,removeEc);}
                        std::error_code removeDirEc; fs::remove(dir,removeDirEc); // only succeeds if truly empty
                    }
                }
            }

            std::error_code ec;
            if(IsRealDirectoryWithoutReparsePoint(thumbs)) fs::remove(thumbs,ec);
            ec.clear();
            if(IsRealDirectoryWithoutReparsePoint(previews)) fs::remove(previews,ec);
            ec.clear();
            fs::remove(cacheRoot,ec); // only if empty
        }
    }

    static int ResolvePreviewLayout(VRInfo vr, UINT w, UINT h) {
        if (!vr.vr || vr.layoutExplicit) return vr.layout;
        if (!w || !h) return 0;
        const float aspect=static_cast<float>(w)/static_cast<float>(h);
        if (vr.projection==1) {
            if (aspect>=3.20f) return 1;
            if (aspect<=1.20f) return 2;
            return 0;
        }
        if (vr.projection==2) {
            if (aspect>=1.70f && aspect<3.20f) return 1;
            if (aspect<=0.70f) return 2;
        }
        return 0;
    }

    struct StereoMetric {
        double mad = 1e9;
        double corr = -1.0;
        double contrast = 0.0;
    };

    static double PixelLuma(const Gdiplus::Color& c) {
        return 0.2126 * c.GetR() + 0.7152 * c.GetG() + 0.0722 * c.GetB();
    }

    static StereoMetric MeasureStereoLR(Gdiplus::Bitmap& bitmap) {
        const UINT w=bitmap.GetWidth(), h=bitmap.GetHeight();
        if (w<16 || h<8) return {};
        const UINT half=w/2u;
        const int maxShift=std::clamp(static_cast<int>(half/12u),4,64);
        StereoMetric best{};
        constexpr int samplesX=20, samplesY=12;

        for(int shift=-maxShift;shift<=maxShift;++shift){
            double sumA=0.0,sumB=0.0,sumAA=0.0,sumBB=0.0,sumAB=0.0,mad=0.0;
            int count=0;
            for(int gy=0;gy<samplesY;++gy){
                const UINT y=std::min<UINT>(h-1u,static_cast<UINT>((gy+0.5)*h/samplesY));
                for(int gx=0;gx<samplesX;++gx){
                    const UINT x=std::min<UINT>(half-1u,static_cast<UINT>((gx+0.5)*half/samplesX));
                    const int bx=std::clamp(static_cast<int>(x+half)+shift,static_cast<int>(half),static_cast<int>(w)-1);
                    Gdiplus::Color a,b;
                    if(bitmap.GetPixel(static_cast<INT>(x),static_cast<INT>(y),&a)!=Gdiplus::Ok) continue;
                    if(bitmap.GetPixel(bx,static_cast<INT>(y),&b)!=Gdiplus::Ok) continue;
                    const double av=PixelLuma(a),bv=PixelLuma(b);
                    sumA+=av;sumB+=bv;sumAA+=av*av;sumBB+=bv*bv;sumAB+=av*bv;mad+=std::abs(av-bv);++count;
                }
            }
            if(count<24) continue;
            const double meanA=sumA/count,meanB=sumB/count;
            const double varA=std::max(0.0,sumAA/count-meanA*meanA);
            const double varB=std::max(0.0,sumBB/count-meanB*meanB);
            const double denom=std::sqrt(varA*varB);
            const double corr=denom>1e-6?(sumAB/count-meanA*meanB)/denom:-1.0;
            const double contrast=std::sqrt(std::max(0.0,(varA+varB)*0.5));
            const double currentMad=mad/count;
            if(corr>best.corr || (std::abs(corr-best.corr)<0.015 && currentMad<best.mad))
                best={currentMad,corr,contrast};
        }
        return best;
    }

    static StereoMetric MeasureStereoTB(Gdiplus::Bitmap& bitmap) {
        const UINT w=bitmap.GetWidth(), h=bitmap.GetHeight();
        if (w<8 || h<16) return {};
        const UINT half=h/2u;
        const int maxShift=std::clamp(static_cast<int>(w/24u),4,64);
        StereoMetric best{};
        constexpr int samplesX=20, samplesY=12;

        for(int shift=-maxShift;shift<=maxShift;++shift){
            double sumA=0.0,sumB=0.0,sumAA=0.0,sumBB=0.0,sumAB=0.0,mad=0.0;
            int count=0;
            for(int gy=0;gy<samplesY;++gy){
                const UINT y=std::min<UINT>(half-1u,static_cast<UINT>((gy+0.5)*half/samplesY));
                for(int gx=0;gx<samplesX;++gx){
                    const UINT x=std::min<UINT>(w-1u,static_cast<UINT>((gx+0.5)*w/samplesX));
                    const int bx=std::clamp(static_cast<int>(x)+shift,0,static_cast<int>(w)-1);
                    Gdiplus::Color a,b;
                    if(bitmap.GetPixel(static_cast<INT>(x),static_cast<INT>(y),&a)!=Gdiplus::Ok) continue;
                    if(bitmap.GetPixel(bx,static_cast<INT>(y+half),&b)!=Gdiplus::Ok) continue;
                    const double av=PixelLuma(a),bv=PixelLuma(b);
                    sumA+=av;sumB+=bv;sumAA+=av*av;sumBB+=bv*bv;sumAB+=av*bv;mad+=std::abs(av-bv);++count;
                }
            }
            if(count<24) continue;
            const double meanA=sumA/count,meanB=sumB/count;
            const double varA=std::max(0.0,sumAA/count-meanA*meanA);
            const double varB=std::max(0.0,sumBB/count-meanB*meanB);
            const double denom=std::sqrt(varA*varB);
            const double corr=denom>1e-6?(sumAB/count-meanA*meanB)/denom:-1.0;
            const double contrast=std::sqrt(std::max(0.0,(varA+varB)*0.5));
            const double currentMad=mad/count;
            if(corr>best.corr || (std::abs(corr-best.corr)<0.015 && currentMad<best.mad))
                best={currentMad,corr,contrast};
        }
        return best;
    }

    static bool LikelyStereo(const StereoMetric& m) {
        // Allow realistic stereo parallax. A confident left/right or top/bottom match
        // should be preferred over a single-frame mono guess.
        if (m.contrast < 5.0) return false;
        return (m.corr >= 0.72 && m.mad <= 72.0) ||
               (m.corr >= 0.58 && m.mad <= 42.0);
    }

    static int ResolveStillLayout(VRInfo vr, Gdiplus::Bitmap& bitmap, int initialLayout) {
        if (!vr.vr) return 0;
        if (vr.layoutExplicit) return vr.layout;
        if (initialLayout==1 || initialLayout==2) return initialLayout;

        const double aspect=static_cast<double>(bitmap.GetWidth())/std::max<UINT>(1u,bitmap.GetHeight());
        // The main ambiguous case is a ~2:1 VR frame: mono 360 and two square SBS eyes
        // share the same dimensions. Compare the halves with a small parallax shift allowance.
        if (aspect >= 1.30) return LikelyStereo(MeasureStereoLR(bitmap)) ? 1 : 0;
        if (aspect <= 0.82) return LikelyStereo(MeasureStereoTB(bitmap)) ? 2 : 0;
        return 0;
    }

    static bool SaveVideoSampleJpeg(IMFSample* sample, UINT width, UINT height, LONG defaultStride,
                                    VRInfo vr, int& layoutState, const std::wstring& output,
                                    int outW, int outH, ULONG quality, bool cover) {
        if (!sample || !width || !height || outW<=0 || outH<=0) return false;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return false;

        BYTE* scan0=nullptr;
        LONG pitch=defaultStride;
        bool locked2D=false;
        ComPtr<IMF2DBuffer> buffer2D;
        BYTE* raw=nullptr; DWORD maxLen=0,currentLen=0;
        if (SUCCEEDED(buffer.As(&buffer2D)) && buffer2D && SUCCEEDED(buffer2D->Lock2D(&scan0,&pitch))) {
            locked2D=true;
        } else {
            if (FAILED(buffer->Lock(&raw,&maxLen,&currentLen)) || !raw) return false;
            if (!pitch) pitch=static_cast<LONG>(width*4u);
            scan0=raw;
            if (pitch<0) scan0=raw+static_cast<size_t>(height-1u)*static_cast<size_t>(-pitch);
        }

        bool ok=false;
        {
            Gdiplus::Bitmap frame(static_cast<INT>(width),static_cast<INT>(height),pitch,PixelFormat32bppRGB,scan0);
            if (frame.GetLastStatus()==Gdiplus::Ok) {
                if (layoutState < 0) layoutState=ResolveStillLayout(vr,frame,0);
                const int layout=std::max(0,layoutState);
                UINT sx=0,sy=0,sw=width,sh=height;
                if (layout==1 && width>=2) sw=width/2u;
                else if (layout==2 && height>=2) sh=height/2u;

                Gdiplus::Bitmap out(outW,outH,PixelFormat24bppRGB);
                Gdiplus::Graphics g(&out);
                g.Clear(Gdiplus::Color(255,16,18,24));
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

                if (cover) {
                    const double scale=std::max(static_cast<double>(outW)/std::max<UINT>(1u,sw),static_cast<double>(outH)/std::max<UINT>(1u,sh));
                    const UINT cropW=std::max<UINT>(1u,std::min<UINT>(sw,static_cast<UINT>(outW/scale+0.5)));
                    const UINT cropH=std::max<UINT>(1u,std::min<UINT>(sh,static_cast<UINT>(outH/scale+0.5)));
                    const UINT cropX=sx+(sw-cropW)/2u,cropY=sy+(sh-cropH)/2u;
                    g.DrawImage(&frame,Gdiplus::Rect(0,0,outW,outH),static_cast<INT>(cropX),static_cast<INT>(cropY),static_cast<INT>(cropW),static_cast<INT>(cropH),Gdiplus::UnitPixel);
                } else {
                    const double scale=std::min(static_cast<double>(outW)/std::max<UINT>(1u,sw),static_cast<double>(outH)/std::max<UINT>(1u,sh));
                    const int dw=std::max(1,static_cast<int>(sw*scale));
                    const int dh=std::max(1,static_cast<int>(sh*scale));
                    const int dx=(outW-dw)/2,dy=(outH-dh)/2;
                    g.DrawImage(&frame,Gdiplus::Rect(dx,dy,dw,dh),static_cast<INT>(sx),static_cast<INT>(sy),static_cast<INT>(sw),static_cast<INT>(sh),Gdiplus::UnitPixel);
                }
                ok=SaveJpeg(out,output,quality);
            }
        }

        if (locked2D) buffer2D->Unlock2D(); else buffer->Unlock();
        return ok;
    }

    static bool SavePreviewSample(IMFSample* sample, UINT width, UINT height, LONG defaultStride,
                                  VRInfo vr, int& layoutState, const std::wstring& output) {
        return SaveVideoSampleJpeg(sample,width,height,defaultStride,vr,layoutState,output,320,180,84,false);
    }

    static int DetectVideoSampleLayout(IMFSample* sample, UINT width, UINT height, LONG defaultStride, VRInfo vr) {
        // -1 means the frame is too dark/flat to make a useful decision. Keeping that
        // separate from a real mono vote prevents an intro/fade frame from poisoning
        // the layout choice for the entire video.
        if (!sample || !width || !height || !vr.vr) return 0;
        if (vr.layoutExplicit) return vr.layout;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return -1;

        BYTE* scan0=nullptr; LONG pitch=defaultStride; bool locked2D=false;
        ComPtr<IMF2DBuffer> buffer2D; BYTE* raw=nullptr; DWORD maxLen=0,currentLen=0;
        if (SUCCEEDED(buffer.As(&buffer2D)) && buffer2D && SUCCEEDED(buffer2D->Lock2D(&scan0,&pitch))) {
            locked2D=true;
        } else {
            if (FAILED(buffer->Lock(&raw,&maxLen,&currentLen)) || !raw) return -1;
            if (!pitch) pitch=static_cast<LONG>(width*4u);
            scan0=raw;
            if (pitch<0) scan0=raw+static_cast<size_t>(height-1u)*static_cast<size_t>(-pitch);
        }

        int result=-1;
        Gdiplus::Bitmap frame(static_cast<INT>(width),static_cast<INT>(height),pitch,PixelFormat32bppRGB,scan0);
        if (frame.GetLastStatus()==Gdiplus::Ok) {
            const double aspect=static_cast<double>(width)/std::max<UINT>(1u,height);
            if (aspect>=1.30) {
                const StereoMetric m=MeasureStereoLR(frame);
                if (m.contrast>=7.0) result=LikelyStereo(m)?1:0;
            } else if (aspect<=0.82) {
                const StereoMetric m=MeasureStereoTB(frame);
                if (m.contrast>=7.0) result=LikelyStereo(m)?2:0;
            } else {
                result=0;
            }
        }
        if (locked2D) buffer2D->Unlock2D(); else buffer->Unlock();
        return result;
    }

    static int ReadCachedPreviewLayout(const std::wstring& previewDir) {
        std::ifstream in(fs::path(previewDir)/L"layout.txt",std::ios::binary);
        int value=-1; if(in) in>>value;
        return (value>=0&&value<=2)?value:-1;
    }

    static void WriteCachedPreviewLayout(const std::wstring& previewDir, int layout) {
        if(layout<0||layout>2) return;
        std::error_code ec; fs::create_directories(previewDir,ec); if(ec) return;
        std::ofstream out(fs::path(previewDir)/L"layout.txt",std::ios::binary|std::ios::trunc);
        if(out) out<<layout;
    }

    static std::vector<int> BuildPreviewCaptureSeconds(double duration) {
        std::vector<int> captureSeconds;
        if (!(duration > 0.0) || !std::isfinite(duration)) return captureSeconds;
        if (duration < 1.2) return {0};

        if (duration<61.0) {
            const int maxSec=std::max(0,static_cast<int>(std::floor(duration-0.05)));
            for (double f : {0.25,0.50,0.75}) {
                int sec=std::clamp(static_cast<int>(std::round(duration*f)),0,maxSec);
                if (std::find(captureSeconds.begin(),captureSeconds.end(),sec)==captureSeconds.end()) captureSeconds.push_back(sec);
            }
        } else {
            const int lastMinute=static_cast<int>(std::floor(std::max(0.0,duration-0.5)/60.0));
            for (int minute=1;minute<=lastMinute;++minute) captureSeconds.push_back(minute*60);
        }
        return captureSeconds;
    }

    static bool FileHasData(const fs::path& path) {
        std::error_code ec;
        return fs::is_regular_file(path,ec) && !ec && fs::file_size(path,ec)>=256 && !ec;
    }

    static bool GenerateVideoPreviewsMF(const std::wstring& source, const std::wstring& previewDir, VRInfo vr, std::atomic<bool>& stop, HWND notifyHwnd, std::atomic<double>* durationOut = nullptr, std::atomic<int>* progressCurrent = nullptr, std::atomic<int>* progressTotal = nullptr, const std::atomic<ULONGLONG>* pauseUntil = nullptr) {
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs,2))) return false;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,FALSE);

        auto waitForPermit=[&]()->bool{
            while(!stop.load(std::memory_order_acquire)){
                if(!pauseUntil) return true;
                const ULONGLONG until=pauseUntil->load(std::memory_order_acquire);
                const ULONGLONG now=GetTickCount64();
                if(until<=now) return true;
                Sleep(static_cast<DWORD>(std::min<ULONGLONG>(20,until-now)));
            }
            return false;
        };
        if(!waitForPermit()) return false;

        ComPtr<IMFSourceReader> reader;
        HRESULT hr=MFCreateSourceReaderFromURL(source.c_str(),attrs.Get(),&reader);
        if (FAILED(hr)||!reader) return false;
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE);
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),TRUE);

        ComPtr<IMFMediaType> nativeType;
        hr=reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&nativeType);
        if (FAILED(hr)||!nativeType) return false;
        UINT nativeW=0,nativeH=0;
        if (FAILED(MFGetAttributeSize(nativeType.Get(),MF_MT_FRAME_SIZE,&nativeW,&nativeH))||!nativeW||!nativeH) return false;
        const int resolvedLayout=ResolvePreviewLayout(vr,nativeW,nativeH);
        int layoutState=resolvedLayout;
        if(vr.vr&&!vr.layoutExplicit&&resolvedLayout==0){
            layoutState=ReadCachedPreviewLayout(previewDir);
        }

        const double scale=std::min(1.0,std::min(640.0/static_cast<double>(nativeW),360.0/static_cast<double>(nativeH)));
        UINT outW=std::max<UINT>(2u,static_cast<UINT>(nativeW*scale));
        UINT outH=std::max<UINT>(2u,static_cast<UINT>(nativeH*scale));
        outW=(outW+1u)&~1u; outH=(outH+1u)&~1u;

        ComPtr<IMFMediaType> outType;
        if (FAILED(MFCreateMediaType(&outType))) return false;
        outType->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
        MFSetAttributeSize(outType.Get(),MF_MT_FRAME_SIZE,outW,outH);
        MFSetAttributeRatio(outType.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        outType->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        hr=reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,outType.Get());
        if (FAILED(hr)) return false;

        ComPtr<IMFMediaType> actualType;
        reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&actualType);
        UINT actualW=outW,actualH=outH;
        LONG stride=static_cast<LONG>(actualW*4u);
        if (actualType) {
            MFGetAttributeSize(actualType.Get(),MF_MT_FRAME_SIZE,&actualW,&actualH);
            UINT32 strideValue=0;
            if (SUCCEEDED(actualType->GetUINT32(MF_MT_DEFAULT_STRIDE,&strideValue))) stride=static_cast<LONG>(strideValue);
        }

        PROPVARIANT durationVar; PropVariantInit(&durationVar);
        LONGLONG duration100ns=0;
        if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&durationVar))) {
            if (durationVar.vt==VT_UI8) duration100ns=static_cast<LONGLONG>(durationVar.uhVal.QuadPart);
            else if (durationVar.vt==VT_I8) duration100ns=durationVar.hVal.QuadPart;
        }
        PropVariantClear(&durationVar);
        if (duration100ns<=0) return false;
        const double duration=static_cast<double>(duration100ns)/10000000.0;
        if (durationOut) durationOut->store(duration, std::memory_order_relaxed);
        if (notifyHwnd) PostMessageW(notifyHwnd,WM_APP_PREVIEW_READY,0,0);

        const std::vector<int> captureSeconds=BuildPreviewCaptureSeconds(duration);
        if (captureSeconds.empty()) return false;
        int alreadyComplete=0;
        for(int sec:captureSeconds){
            wchar_t progressName[32]{}; swprintf_s(progressName,L"%06d.jpg",sec);
            if(FileHasData(fs::path(previewDir)/progressName)) ++alreadyComplete;
        }
        if(progressTotal) progressTotal->store(static_cast<int>(captureSeconds.size()),std::memory_order_relaxed);
        if(progressCurrent) progressCurrent->store(alreadyComplete,std::memory_order_relaxed);

        struct PendingSample { int sec=0; ComPtr<IMFSample> sample; };
        std::vector<PendingSample> pending;
        int layoutVotes[3]{0,0,0};
        bool any=false,allComplete=true;
        ULONGLONG lastPreviewNotify=0;
        int previewNotifyBudget=0;
        auto postPreviewProgress=[&](bool force){
            if(!notifyHwnd) return;
            ++previewNotifyBudget;
            const ULONGLONG now=GetTickCount64();
            if(force || previewNotifyBudget>=8 || now-lastPreviewNotify>=250){
                PostMessageW(notifyHwnd,WM_APP_PREVIEW_READY,0,0);
                lastPreviewNotify=now;
                previewNotifyBudget=0;
            }
        };

        auto saveOne=[&](int sec, IMFSample* sample)->bool{
            wchar_t name[32]{}; swprintf_s(name,L"%06d.jpg",sec);
            const std::wstring output=(fs::path(previewDir)/name).wstring();
            if(FileHasData(output)){ any=true; return true; }
            std::error_code dirEc; fs::create_directories(previewDir,dirEc);
            if(dirEc){ allComplete=false; return false; }
            int finalLayout=std::max(0,layoutState);
            const std::wstring tmp=output+L".tmp"; DeleteFileW(tmp.c_str());
            if(!SavePreviewSample(sample,actualW,actualH,stride,vr,finalLayout,tmp)){
                DeleteFileW(tmp.c_str()); allComplete=false; return false;
            }
            DeleteFileW(output.c_str());
            if(!MoveFileExW(tmp.c_str(),output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
                DeleteFileW(tmp.c_str()); allComplete=false; return false;
            }
            any=true;
            if(progressCurrent) progressCurrent->fetch_add(1,std::memory_order_relaxed);
            postPreviewProgress(false);
            Sleep(1);
            return true;
        };

        auto finalizePendingLayout=[&](){
            if(layoutState<0){
                if(layoutVotes[1]>=2) layoutState=1;
                else if(layoutVotes[2]>=2) layoutState=2;
                // If only one useful frame was visible and it confidently identified a
                // stereo orientation, prefer that over otherwise inconclusive dark frames.
                else if(layoutVotes[1]>0 && layoutVotes[2]==0 && layoutVotes[1]>=layoutVotes[0]) layoutState=1;
                else if(layoutVotes[2]>0 && layoutVotes[1]==0 && layoutVotes[2]>=layoutVotes[0]) layoutState=2;
                else if(layoutVotes[1]>layoutVotes[0] && layoutVotes[1]>layoutVotes[2]) layoutState=1;
                else if(layoutVotes[2]>layoutVotes[0] && layoutVotes[2]>layoutVotes[1]) layoutState=2;
                else layoutState=0;
                WriteCachedPreviewLayout(previewDir,layoutState);
            }
            for(auto& p:pending){
                if(stop.load()){ allComplete=false; break; }
                saveOne(p.sec,p.sample.Get());
            }
            pending.clear();
        };

        for (int sec : captureSeconds) {
            if (!waitForPermit() || stop.load()) { allComplete=false; break; }
            wchar_t name[32]{}; swprintf_s(name,L"%06d.jpg",sec);
            const std::wstring output=(fs::path(previewDir)/name).wstring();
            if (FileHasData(output)) { any=true; continue; }

            PROPVARIANT pos; PropVariantInit(&pos); pos.vt=VT_I8; pos.hVal.QuadPart=static_cast<LONGLONG>(sec)*10000000LL;
            hr=reader->SetCurrentPosition(GUID_NULL,pos); PropVariantClear(&pos);
            if (FAILED(hr)) { allComplete=false; continue; }

            ComPtr<IMFSample> chosen;
            for (int attempts=0;attempts<240 && !stop.load();++attempts) {
                if((attempts&7)==0 && !waitForPermit()) break;
                DWORD streamIndex=0,flags=0; LONGLONG timestamp=0; ComPtr<IMFSample> sample;
                hr=reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&streamIndex,&flags,&timestamp,&sample);
                if (FAILED(hr)||(flags&MF_SOURCE_READERF_ENDOFSTREAM)) break;
                if (!sample) continue;
                chosen=sample;
                if (timestamp>=static_cast<LONGLONG>(sec)*10000000LL) break;
            }
            if (!chosen||stop.load()) { allComplete=false; continue; }

            if(layoutState<0){
                const int candidate=DetectVideoSampleLayout(chosen.Get(),actualW,actualH,stride,vr);
                if(candidate>=0 && candidate<=2) ++layoutVotes[candidate];
                PendingSample pendingItem; pendingItem.sec=sec; pendingItem.sample=chosen;
                pending.push_back(std::move(pendingItem));
                // Two agreeing stereo detections are enough. Otherwise inspect up to
                // three representative frames; inconclusive dark frames do not vote mono.
                if(layoutVotes[1]>=2 || layoutVotes[2]>=2 || pending.size()>=5) finalizePendingLayout();
            } else {
                saveOne(sec,chosen.Get());
            }
        }

        if(!pending.empty()&&!stop.load()) finalizePendingLayout();
        postPreviewProgress(true);

        const fs::path markerPath=fs::path(previewDir)/L"complete.txt";
        if (!stop.load() && any && allComplete) {
            std::ofstream marker(markerPath,std::ios::binary|std::ios::trunc);
            if (marker) marker<<duration;
        } else {
            std::error_code markerEc; fs::remove(markerPath,markerEc);
        }
        return any;
    }

    static void DeletePreviewFrameBitmaps(std::vector<PreviewFrame>& frames) {
        for(auto& p:frames){ if(p.bitmap) DeleteObject(p.bitmap); p.bitmap=nullptr; p.gpuBitmap.Reset(); p.gpuBitmapSource=nullptr; p.gpuGeneration=0; }
    }

    void ClearPreviewBitmaps() {
        DeletePreviewFrameBitmaps(previewFrames_);
        previewFrames_.clear();
        previewMediaPath_.clear();
    }

    void ClearPrefetchedPreviewSets() {
        for(auto& kv:prefetchedPreviewSets_) DeletePreviewFrameBitmaps(kv.second.frames);
        prefetchedPreviewSets_.clear();
    }

    void FreeAllDetailBanners() {
        auto freeList=[](std::vector<MediaItem>& list){
            for(auto& item:list){
                if(item.detailThumb) DeleteObject(item.detailThumb);
                item.detailThumb=nullptr; item.detailThumbW=0; item.detailThumbH=0;
                item.detailsGpuThumb.Reset(); item.detailsGpuThumbSource=nullptr; item.detailsGpuGeneration=0;
            }
        };
        freeList(videos_); freeList(images_);
    }

    void CancelDetailPrefetchJobs() {
        detailPrefetchGeneration_.fetch_add(1,std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(detailPrefetchMutex_);
            detailPrefetchJobs_.clear();
        }
        detailPrefetchCv_.notify_all();
    }

    void ClearAllDetailInfoMemory() {
        CancelDetailPrefetchJobs();
        ClearPreviewBitmaps();
        ClearPrefetchedPreviewSets();
        FreeAllDetailBanners();
        previewDir_.clear();
        detailsDurationSeconds_.store(0.0,std::memory_order_relaxed);
    }

    PrefetchedPreviewSet LoadPrefetchedPreviewSet(const std::wstring& dir) {
        PrefetchedPreviewSet out;
        out.duration=ReadCachedPreviewDurationFromDir(dir);
        if(!(out.duration>0.0)) return out;
        const auto expected=BuildPreviewCaptureSeconds(out.duration);
        out.frames.reserve(expected.size());
        size_t decoded=0;
        for(int sec:expected){
            wchar_t name[32]{}; swprintf_s(name,L"%06d.jpg",sec);
            PreviewFrame frame; frame.seconds=sec; frame.path=(fs::path(dir)/name).wstring();
            if(decoded<24 && FileHasData(frame.path)){
                frame.bitmap=LoadScaledBitmap(frame.path,320,180);
                if(frame.bitmap){frame.lastUsed=GetTickCount64();++decoded;}
            }
            out.frames.push_back(std::move(frame));
        }
        return out;
    }

    void ParkActivePreviewSet() {
        if(previewMediaPath_.empty() || previewFrames_.empty()) return;
        auto it=prefetchedPreviewSets_.find(previewMediaPath_);
        if(it!=prefetchedPreviewSets_.end()){
            DeletePreviewFrameBitmaps(it->second.frames);
            prefetchedPreviewSets_.erase(it);
        }
        PrefetchedPreviewSet set;
        set.frames=std::move(previewFrames_);
        set.duration=detailsDurationSeconds_.load(std::memory_order_relaxed);
        prefetchedPreviewSets_.emplace(previewMediaPath_,std::move(set));
        previewFrames_.clear();
        previewMediaPath_.clear();
    }

    bool RestorePrefetchedPreviewSet(const std::wstring& mediaPath) {
        auto it=prefetchedPreviewSets_.find(mediaPath);
        if(it==prefetchedPreviewSets_.end()) return false;
        previewFrames_=std::move(it->second.frames);
        detailsDurationSeconds_.store(it->second.duration,std::memory_order_relaxed);
        prefetchedPreviewSets_.erase(it);
        previewMediaPath_=mediaPath;
        return true;
    }

    void RefreshPreviewFrames() {
        if (previewDir_.empty()) return;
        std::map<int,HBITMAP> old;
        std::map<int,ULONGLONG> oldUsed;
        std::map<int,int> oldFailures;
        std::map<int,ULONGLONG> oldNextAttempt;
        std::map<int,ComPtr<ID2D1Bitmap>> oldGpu;
        std::map<int,HBITMAP> oldGpuSource;
        std::map<int,uint64_t> oldGpuGeneration;
        for (auto& p : previewFrames_) {
            if (p.bitmap) old[p.seconds]=p.bitmap;
            if (p.gpuBitmap) oldGpu[p.seconds]=std::move(p.gpuBitmap);
            if (p.gpuBitmapSource) oldGpuSource[p.seconds]=p.gpuBitmapSource;
            if (p.gpuGeneration) oldGpuGeneration[p.seconds]=p.gpuGeneration;
            oldUsed[p.seconds]=p.lastUsed;
            oldFailures[p.seconds]=p.loadFailures;
            oldNextAttempt[p.seconds]=p.nextLoadAttempt;
        }
        previewFrames_.clear();

        // A valid complete.txt marker is authoritative. Do not stat thousands of JPEGs
        // every time Details opens; construct their deterministic paths directly. If a
        // cached preview repeatedly fails to decode, GetPreviewBitmap invalidates that entry
        // and the marker, so the next pass repairs only what is actually broken.
        const double cachedDuration=ReadCachedPreviewDuration();
        if(cachedDuration>0.0){
            const auto expected=BuildPreviewCaptureSeconds(cachedDuration);
            previewFrames_.reserve(expected.size());
            for(int sec:expected){
                wchar_t name[32]{}; swprintf_s(name,L"%06d.jpg",sec);
                PreviewFrame f; f.seconds=sec; f.path=(fs::path(previewDir_)/name).wstring();
                auto it=old.find(f.seconds); if(it!=old.end()){f.bitmap=it->second;old.erase(it);}
                auto git=oldGpu.find(f.seconds);if(git!=oldGpu.end()){f.gpuBitmap=std::move(git->second);oldGpu.erase(git);}
                auto gsit=oldGpuSource.find(f.seconds);if(gsit!=oldGpuSource.end())f.gpuBitmapSource=gsit->second;
                auto ggit=oldGpuGeneration.find(f.seconds);if(ggit!=oldGpuGeneration.end())f.gpuGeneration=ggit->second;
                auto uit=oldUsed.find(f.seconds); if(uit!=oldUsed.end()) f.lastUsed=uit->second;
                auto fit=oldFailures.find(f.seconds); if(fit!=oldFailures.end()) f.loadFailures=fit->second;
                auto nit=oldNextAttempt.find(f.seconds); if(nit!=oldNextAttempt.end()) f.nextLoadAttempt=nit->second;
                previewFrames_.push_back(std::move(f));
            }
        } else {
            // Incomplete generation has no authoritative marker yet, so enumerate only
            // the partial files that already exist and show those while generation runs.
            std::error_code ec;
            if (fs::exists(previewDir_,ec)) {
                for (const auto& e : fs::directory_iterator(previewDir_,ec)) {
                    if (ec) break;
                    if (!e.is_regular_file(ec)||ToLower(e.path().extension().wstring())!=L".jpg") continue;
                    const std::wstring stem=e.path().stem().wstring();
                    wchar_t* end=nullptr; const long sec=wcstol(stem.c_str(),&end,10);
                    if (!end||*end!=L'\0'||sec<0) continue;
                    PreviewFrame f; f.seconds=static_cast<int>(sec); f.path=e.path().wstring();
                    auto it=old.find(f.seconds); if(it!=old.end()){f.bitmap=it->second;old.erase(it);}
                auto git=oldGpu.find(f.seconds);if(git!=oldGpu.end()){f.gpuBitmap=std::move(git->second);oldGpu.erase(git);}
                auto gsit=oldGpuSource.find(f.seconds);if(gsit!=oldGpuSource.end())f.gpuBitmapSource=gsit->second;
                auto ggit=oldGpuGeneration.find(f.seconds);if(ggit!=oldGpuGeneration.end())f.gpuGeneration=ggit->second;
                    auto uit=oldUsed.find(f.seconds); if(uit!=oldUsed.end()) f.lastUsed=uit->second;
                    auto fit=oldFailures.find(f.seconds); if(fit!=oldFailures.end()) f.loadFailures=fit->second;
                    auto nit=oldNextAttempt.find(f.seconds); if(nit!=oldNextAttempt.end()) f.nextLoadAttempt=nit->second;
                    previewFrames_.push_back(std::move(f));
                }
            }
            std::sort(previewFrames_.begin(),previewFrames_.end(),[](const PreviewFrame&a,const PreviewFrame&b){return a.seconds<b.seconds;});
        }
        for (auto& kv:old) if(kv.second) DeleteObject(kv.second);
    }

    static double ReadCachedPreviewDurationFromDir(const std::wstring& dir) {
        if (dir.empty()) return 0.0;
        std::ifstream in(fs::path(dir) / L"complete.txt", std::ios::binary);
        double value=0.0;
        if(in) in>>value;
        return (value>0.0 && std::isfinite(value)) ? value : 0.0;
    }

    static bool PreviewCacheIsCompleteForDir(const std::wstring& dir) {
        const double duration=ReadCachedPreviewDurationFromDir(dir);
        return duration>0.0 && !BuildPreviewCaptureSeconds(duration).empty();
    }

    static bool PreviewCacheFilesCompleteForDir(const std::wstring& dir) {
        const double duration=ReadCachedPreviewDurationFromDir(dir);
        if(!(duration>0.0)) return false;
        const auto expected=BuildPreviewCaptureSeconds(duration);
        if(expected.empty()) return false;
        for(int sec:expected){
            wchar_t name[32]{}; swprintf_s(name,L"%06d.jpg",sec);
            if(!FileHasData(fs::path(dir)/name)) return false;
        }
        return true;
    }

    double ReadCachedPreviewDuration() const {
        return ReadCachedPreviewDurationFromDir(previewDir_);
    }

    static double ProbeVideoDurationMF(const std::wstring& path) {
        ComPtr<IMFSourceReader> reader;
        if(FAILED(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader)) || !reader) return 0.0;
        PROPVARIANT durationVar; PropVariantInit(&durationVar);
        LONGLONG duration100ns=0;
        if(SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&durationVar))){
            if(durationVar.vt==VT_UI8) duration100ns=static_cast<LONGLONG>(durationVar.uhVal.QuadPart);
            else if(durationVar.vt==VT_I8) duration100ns=durationVar.hVal.QuadPart;
        }
        PropVariantClear(&durationVar);
        return duration100ns>0 ? static_cast<double>(duration100ns)/10000000.0 : 0.0;
    }

    bool PreviewCacheIsComplete() const {
        // Normal Details opening trusts complete.txt for speed. Individual bad files
        // are still repaired lazily; explicit Load everything performs a full check.
        return PreviewCacheIsCompleteForDir(previewDir_);
    }

    void StopPreviewWorker() {
        previewStop_=true;
        if (previewThread_.joinable()) previewThread_.join();
        previewStop_=false;
        const int kind=loadingKind_.load(std::memory_order_acquire);
        if(kind==1 || kind==2 || kind==3) ClearLoadingState();
    }

    void StartPreviewWorkerForSelected() {
        StopPreviewWorker();
        // Selected media has priority, but entering Info must not block the UI waiting
        // for the low-priority library generator to join. Signal it to stop; it is
        // reaped later when library generation is restarted.
        thumbStop_.store(true,std::memory_order_release);
        ClearLoadingStateIf(1);
        if(category_!=Category::Videos||selected_>=videos_.size()) {
            ClearPreviewBitmaps();
            previewDir_.clear();
            detailsDurationSeconds_.store(0.0,std::memory_order_relaxed);
            ClearLoadingState();
            return;
        }
        MediaItem& selectedItem=videos_[selected_];
        if(!selectedItem.resolutionProbeAttempted && !selectedItem.resolutionMetadataQueued){
            selectedItem.resolutionMetadataQueued=true;
            QueueResolutionMetadata(selectedItem.path,selectedItem.uiCachePath,true);
        }
        const MediaItem item=selectedItem;
        if(!previewMediaPath_.empty() && previewMediaPath_!=item.path) ParkActivePreviewSet();
        previewDir_=BuildPreviewDirectory(item.path);
        const bool restored=(previewMediaPath_==item.path) || RestorePrefetchedPreviewSet(item.path);
        if(!restored){
            DeletePreviewFrameBitmaps(previewFrames_);
            previewFrames_.clear();
            previewMediaPath_=item.path;
            detailsDurationSeconds_.store(0.0,std::memory_order_relaxed);
        }
        RefreshPreviewFrames();
        detailsDurationSeconds_.store(ReadCachedPreviewDuration(), std::memory_order_relaxed);
        const bool previewsComplete=PreviewCacheIsComplete();
        if(!previewsComplete){ std::error_code markerEc; fs::remove(fs::path(previewDir_)/L"complete.txt",markerEc); }
        double libraryBannerTime=-1.0, infoBannerTime=-1.0;
        const bool libraryBannerComplete=CacheFileLooksHealthy(item.uiCachePath,512) && ReadBannerTimestamp(item.uiCachePath,libraryBannerTime);
        bool bannerComplete=CacheFileLooksHealthy(item.cachePath,1024) && ReadBannerTimestamp(item.cachePath,infoBannerTime);
        if(!libraryBannerComplete || !bannerComplete || std::abs(infoBannerTime-libraryBannerTime)>0.001){
            // The Info banner is tied to the exact timestamp recorded by the Library banner.
            // If either cache/metadata entry is unhealthy or the linkage differs, rebuild the
            // Info banner after the Library banner has established the authoritative frame time.
            if(CacheFileLooksHealthy(item.cachePath,1)) RemoveGeneratedCacheFile(item.cachePath);
            RemoveGeneratedCacheFile(BannerTimestampPath(item.cachePath));
            bannerComplete=false;
        }
        if(previewsComplete && bannerComplete && libraryBannerComplete){ ClearLoadingState(); StartThumbnailWorker(); return; }
        previewStop_=false;
        previewThread_=std::thread([this,item,dir=previewDir_]() {
            const HRESULT coHr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);

            // Duration is metadata, not timeline-cache state. Read it before waiting for
            // a generation claim so an open Info panel can show the real time even while
            // Load Everything owns the decoder/cache-generation slot for this video.
            if(detailsDurationSeconds_.load(std::memory_order_relaxed)<=0.0 && !previewStop_.load(std::memory_order_acquire)){
                const double duration=ProbeVideoDurationMF(item.path);
                if(duration>0.0 && !previewStop_.load(std::memory_order_acquire)){
                    detailsDurationSeconds_.store(duration,std::memory_order_relaxed);
                    if(hwnd_) PostMessageW(hwnd_,WM_APP_PREVIEW_READY,0,0);
                }
            }

            const bool claimed=WaitForGenerationClaim(item.path,previewStop_);
            if(claimed && !previewStop_.load(std::memory_order_acquire)) {
                // Re-evaluate after the generation claim. Load everything may have filled
                // this cache while the selected-media worker was waiting.
                double libraryTime=-1.0,infoTime=-1.0;
                bool libraryReady=CacheFileLooksHealthy(item.uiCachePath,512) && ReadBannerTimestamp(item.uiCachePath,libraryTime);
                bool infoReady=CacheFileLooksHealthy(item.cachePath,1024) && ReadBannerTimestamp(item.cachePath,infoTime);
                bool previewsReady=PreviewCacheIsCompleteForDir(dir);
                if(!libraryReady || !infoReady || std::abs(infoTime-libraryTime)>0.001) {
                    if(CacheFileLooksHealthy(item.cachePath,1)) RemoveGeneratedCacheFile(item.cachePath);
                    RemoveGeneratedCacheFile(BannerTimestampPath(item.cachePath));
                    infoReady=false;
                }

                // Strict selected-media loading order:
                // 1) Library banner, 2) native Info banner, 3) secondary timeline.
                if(!previewStop_.load(std::memory_order_acquire) && !libraryReady) {
                    SetLoadingState(1,0,1);
                    ThumbJob grid{item.path,item.cachePath,item.uiCachePath,true,item.vr};
                    if(GenerateGridThumb(grid,&previewStop_,&backgroundPauseUntil_)) {
                        HideCacheRootIfCreated(item.path);
                        loadingCurrent_.store(1,std::memory_order_relaxed);
                        libraryReady=CacheFileLooksHealthy(item.uiCachePath,512) && ReadBannerTimestamp(item.uiCachePath,libraryTime);
                        if(hwnd_) PostMessageW(hwnd_,WM_APP_THUMB_READY,0,0);
                    }
                }

                if(!previewStop_.load(std::memory_order_acquire) && libraryReady && !infoReady) {
                    SetLoadingState(3,0,1);
                    ThumbJob high{item.path,item.cachePath,item.uiCachePath,true,item.vr};
                    if(GenerateVideoCache(high,&previewStop_,&backgroundPauseUntil_)) {
                        HideCacheRootIfCreated(item.path);
                        loadingCurrent_.store(1,std::memory_order_relaxed);
                        if(hwnd_) PostMessageW(hwnd_,WM_APP_THUMB_READY,0,0);
                    }
                }

                if(!previewStop_.load(std::memory_order_acquire) && !previewsReady) {
                    SetLoadingState(2,0,0);
                    if(GenerateVideoPreviewsMF(item.path,dir,item.vr,previewStop_,hwnd_,&detailsDurationSeconds_,&loadingCurrent_,&loadingTotal_,&backgroundPauseUntil_))
                        HideCacheRootIfCreated(item.path);
                }
            }
            if(claimed) ReleaseGenerationClaim(item.path);

            if(!previewStop_.load(std::memory_order_acquire)) {
                ClearLoadingState();
                if(hwnd_) PostMessageW(hwnd_,WM_APP_SELECTED_WORK_DONE,0,0);
            }
            if(hwnd_&&!previewStop_.load()) PostMessageW(hwnd_,WM_APP_PREVIEW_READY,0,0);
            if(SUCCEEDED(coHr)) CoUninitialize();
        });
    }

    HBITMAP GetPreviewBitmap(PreviewFrame& frame) {
        const ULONGLONG now=GetTickCount64();
        frame.lastUsed=now;
        if(frame.bitmap) return frame.bitmap;
        if(frame.nextLoadAttempt>now) return nullptr;

        if(!FileHasData(frame.path)){
            // Give the filesystem a grace retry before declaring an expected cache file missing.
            ++frame.loadFailures;
            frame.nextLoadAttempt=now+750;
        } else {
            frame.bitmap=LoadScaledBitmap(frame.path,320,180);
            if(frame.bitmap){
                frame.loadFailures=0;
                frame.nextLoadAttempt=0;
                return frame.bitmap;
            }
            ++frame.loadFailures;
            frame.nextLoadAttempt=now+750;
        }

        if(frame.loadFailures>=3){
            // Do not invalidate a finished timeline after one transient disk/GDI+ decode failure.
            // Only repeated failures are treated as a genuinely broken generated preview.
            if(FileHasData(frame.path)) RemoveGeneratedCacheFile(frame.path);
            if(!previewDir_.empty()) RemoveGeneratedCacheFile((fs::path(previewDir_)/L"complete.txt").wstring());
            frame.loadFailures=0;
            frame.nextLoadAttempt=now+1000;
            if(hwnd_) PostMessageW(hwnd_,WM_APP_CACHE_REPAIR,0,0);
        }
        return nullptr;
    }

    void TrimPreviewMemory() {
        // Do not make the currently displayed Info timeline disappear merely because
        // Load Everything is decoding another large file in the background.
        if(LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow()) return;
        const uint64_t processBytes=ProcessMemoryBytes();
        if(processBytes<kNormalProcessMemoryTarget) return;
        // Parked timelines are pure cache and are the first detail allocations to go.
        ClearPrefetchedPreviewSets();
        if(processBytes<kProcessMemoryHighPressure) return;
        std::vector<PreviewFrame*> loaded;for(auto& frame:previewFrames_)if(frame.bitmap)loaded.push_back(&frame);
        std::sort(loaded.begin(),loaded.end(),[](const PreviewFrame*a,const PreviewFrame*b){return a->lastUsed<b->lastUsed;});
        const size_t keep=processBytes>=kProcessMemoryEmergency?8u:24u;
        while(loaded.size()>keep){PreviewFrame* frame=loaded.front();loaded.erase(loaded.begin());if(frame->bitmap){DeleteObject(frame->bitmap);frame->bitmap=nullptr;}frame->gpuBitmap.Reset();frame->gpuBitmapSource=nullptr;frame->gpuGeneration=0;}
        if(processBytes>=kProcessMemoryEmergency) TrimDetailInfoToWindow(DetailWindowIndices());
    }

    std::wstring PreviewLabel(int seconds) {
        return FormatTime(static_cast<double>(seconds));
    }

    static int GetEncoderClsid(const WCHAR* format, CLSID* clsid) {
        UINT num=0,size=0; Gdiplus::GetImageEncodersSize(&num,&size); if(!size) return -1;
        std::vector<BYTE> data(size); auto* codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(data.data());
        if(Gdiplus::GetImageEncoders(num,size,codecs)!=Gdiplus::Ok) return -1;
        for(UINT i=0;i<num;++i){ if(wcscmp(codecs[i].MimeType,format)==0){ *clsid=codecs[i].Clsid; return static_cast<int>(i); } }
        return -1;
    }

    static bool SaveJpeg(Gdiplus::Image& image, const std::wstring& path, ULONG quality=94) {
        CLSID clsid{}; if(GetEncoderClsid(L"image/jpeg",&clsid)<0) return false;
        Gdiplus::EncoderParameters params{}; params.Count=1; params.Parameter[0].Guid=Gdiplus::EncoderQuality;
        params.Parameter[0].Type=Gdiplus::EncoderParameterValueTypeLong; params.Parameter[0].NumberOfValues=1; params.Parameter[0].Value=&quality;
        return image.Save(path.c_str(),&clsid,&params)==Gdiplus::Ok;
    }

    static bool GenerateVideoStillMF(const std::wstring& source, const std::wstring& output, VRInfo vr,
                                     int targetW, int targetH, ULONG quality, double thumbFraction = 0.25,
                                     double exactSeconds = -1.0, double* capturedSecondsOut = nullptr,
                                     const std::atomic<bool>* cancel = nullptr,
                                     const std::atomic<ULONGLONG>* pauseUntil = nullptr) {
        auto waitForPermit=[&]()->bool{
            while(!(cancel && cancel->load(std::memory_order_acquire))){
                if(!pauseUntil) return true;
                const ULONGLONG until=pauseUntil->load(std::memory_order_acquire);
                const ULONGLONG now=GetTickCount64();
                if(until<=now) return true;
                Sleep(static_cast<DWORD>(std::min<ULONGLONG>(20,until-now)));
            }
            return false;
        };
        if(!waitForPermit()) return false;
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs,2))) return false;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,FALSE);

        ComPtr<IMFSourceReader> reader;
        if (FAILED(MFCreateSourceReaderFromURL(source.c_str(),attrs.Get(),&reader)) || !reader) return false;
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE);
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),TRUE);

        ComPtr<IMFMediaType> nativeType;
        if (FAILED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&nativeType)) || !nativeType) return false;
        UINT nativeW=0,nativeH=0;
        if (FAILED(MFGetAttributeSize(nativeType.Get(),MF_MT_FRAME_SIZE,&nativeW,&nativeH)) || !nativeW || !nativeH) return false;

        const int resolvedLayout=ResolvePreviewLayout(vr,nativeW,nativeH);
        int layoutState=(vr.vr && !vr.layoutExplicit && resolvedLayout==0) ? -1 : resolvedLayout;
        const bool nativeOutput = targetW<=0 || targetH<=0;
        const double scale=nativeOutput ? 1.0 : std::min(1.0,std::min(static_cast<double>(targetW)/nativeW,static_cast<double>(targetH)/nativeH));
        UINT outW=std::max<UINT>(2u,static_cast<UINT>(nativeW*scale));
        UINT outH=std::max<UINT>(2u,static_cast<UINT>(nativeH*scale));
        outW=(outW+1u)&~1u; outH=(outH+1u)&~1u;

        ComPtr<IMFMediaType> outType;
        if (FAILED(MFCreateMediaType(&outType))) return false;
        outType->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
        MFSetAttributeSize(outType.Get(),MF_MT_FRAME_SIZE,outW,outH);
        MFSetAttributeRatio(outType.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        outType->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        if (FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),nullptr,outType.Get()))) return false;

        ComPtr<IMFMediaType> actualType;
        reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&actualType);
        UINT actualW=outW,actualH=outH; LONG stride=static_cast<LONG>(actualW*4u);
        if(actualType){
            MFGetAttributeSize(actualType.Get(),MF_MT_FRAME_SIZE,&actualW,&actualH);
            UINT32 strideValue=0; if(SUCCEEDED(actualType->GetUINT32(MF_MT_DEFAULT_STRIDE,&strideValue))) stride=static_cast<LONG>(strideValue);
        }

        PROPVARIANT durationVar; PropVariantInit(&durationVar); LONGLONG duration100ns=0;
        if(SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&durationVar))){
            if(durationVar.vt==VT_UI8) duration100ns=static_cast<LONGLONG>(durationVar.uhVal.QuadPart);
            else if(durationVar.vt==VT_I8) duration100ns=durationVar.hVal.QuadPart;
        }
        PropVariantClear(&durationVar);
        const double duration=duration100ns>0?static_cast<double>(duration100ns)/10000000.0:0.0;
        const double safeFraction=std::clamp(thumbFraction,0.0,0.95);
        const double requestedSeconds=(std::isfinite(exactSeconds) && exactSeconds>=0.0) ? exactSeconds : duration*safeFraction;
        const double seekSeconds=duration>0.2?std::clamp(requestedSeconds,0.0,std::max(0.0,duration-0.1)):0.0;

        auto readAt=[&](double seconds, double* actualSeconds)->ComPtr<IMFSample>{
            ComPtr<IMFSample> chosenSample;
            PROPVARIANT pos; PropVariantInit(&pos); pos.vt=VT_I8;
            pos.hVal.QuadPart=static_cast<LONGLONG>(std::max(0.0,seconds)*10000000.0);
            const HRESULT seekHr=reader->SetCurrentPosition(GUID_NULL,pos); PropVariantClear(&pos);
            if(FAILED(seekHr)) return chosenSample;
            const LONGLONG target=static_cast<LONGLONG>(std::max(0.0,seconds)*10000000.0);
            for(int attempts=0;attempts<240;++attempts){
                if((attempts&7)==0 && !waitForPermit()) break;
                if(cancel && cancel->load(std::memory_order_acquire)) break;
                DWORD streamIndex=0,flags=0; LONGLONG timestamp=0; ComPtr<IMFSample> sample;
                const HRESULT readHr=reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&streamIndex,&flags,&timestamp,&sample);
                if(FAILED(readHr)||(flags&MF_SOURCE_READERF_ENDOFSTREAM)) break;
                if(!sample) continue;
                chosenSample=sample;
                if(actualSeconds) *actualSeconds=static_cast<double>(timestamp)/10000000.0;
                if(timestamp>=target) break;
            }
            return chosenSample;
        };

        double capturedSeconds=seekSeconds;
        ComPtr<IMFSample> chosen=readAt(seekSeconds,&capturedSeconds);
        if(!chosen || (cancel && cancel->load(std::memory_order_acquire))) return false;
        if(capturedSecondsOut) *capturedSecondsOut=capturedSeconds;

        // A banner/grid frame must use the same stable stereo decision as the secondary
        // previews. For ambiguous VR, inspect representative frames instead of trusting
        // a single dark or unusual frame and then lock one layout for the saved image.
        if(layoutState<0){
            int votes[3]{0,0,0};
            auto vote=[&](IMFSample* sample){
                const int candidate=DetectVideoSampleLayout(sample,actualW,actualH,stride,vr);
                if(candidate>=0 && candidate<=2) ++votes[candidate];
            };
            vote(chosen.Get());
            if(duration>0.4){
                for(double fraction : {0.15,0.40,0.60,0.82}){
                    if(!waitForPermit()) break;
                    if(votes[1]>=2 || votes[2]>=2) break;
                    const double probeSeconds=std::clamp(duration*fraction,0.0,std::max(0.0,duration-0.1));
                    ComPtr<IMFSample> probe=readAt(probeSeconds,nullptr);
                    if(probe) vote(probe.Get());
                }
            }
            if(votes[1]>=2) layoutState=1;
            else if(votes[2]>=2) layoutState=2;
            else if(votes[1]>0 && votes[2]==0 && votes[1]>=votes[0]) layoutState=1;
            else if(votes[2]>0 && votes[1]==0 && votes[2]>=votes[0]) layoutState=2;
            else if(votes[1]>votes[0] && votes[1]>votes[2]) layoutState=1;
            else if(votes[2]>votes[0] && votes[2]>votes[1]) layoutState=2;
            else layoutState=0;
        }

        if(!waitForPermit() || (cancel && cancel->load(std::memory_order_acquire))) return false;
        std::error_code dirEc; fs::create_directories(fs::path(output).parent_path(),dirEc);
        if(dirEc) return false;
        int saveW=targetW, saveH=targetH; bool cover=true;
        if(nativeOutput){
            saveW=static_cast<int>(actualW); saveH=static_cast<int>(actualH); cover=false;
            if(layoutState==1) saveW=std::max(1,saveW/2);
            else if(layoutState==2) saveH=std::max(1,saveH/2);
        }
        return SaveVideoSampleJpeg(chosen.Get(),actualW,actualH,stride,vr,layoutState,output,saveW,saveH,quality,cover);
    }

    static bool GenerateVideoCache(const ThumbJob& job, const std::atomic<bool>* cancel = nullptr, const std::atomic<ULONGLONG>* pauseUntil = nullptr) {
        double existingLinkedTime=-1.0;
        if(CacheFileLooksHealthy(job.output,1024) && ReadBannerTimestamp(job.output,existingLinkedTime)) return true;
        if(PathExistsNoThrow(job.output)) RemoveGeneratedCacheFile(job.output);
        RemoveGeneratedCacheFile(BannerTimestampPath(job.output));
        if(cancel && cancel->load(std::memory_order_acquire)) return false;
        const std::wstring tmp=job.output+L".tmp.jpg"; DeleteFileW(tmp.c_str());
        double exactSeconds=-1.0, capturedSeconds=-1.0;
        ReadBannerTimestamp(job.uiOutput,exactSeconds);
        if(!GenerateVideoStillMF(job.source,tmp,job.vr,0,0,98,0.10,exactSeconds,&capturedSeconds,cancel,pauseUntil)){ DeleteFileW(tmp.c_str()); return false; }
        if(cancel && cancel->load(std::memory_order_acquire)){ DeleteFileW(tmp.c_str()); return false; }
        DeleteFileW(job.output.c_str());
        if(!MoveFileExW(tmp.c_str(),job.output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ DeleteFileW(tmp.c_str()); return false; }
        const double linkedTime=(std::isfinite(exactSeconds) && exactSeconds>=0.0) ? exactSeconds : capturedSeconds;
        WriteBannerTimestamp(job.output,linkedTime);
        double verifyTime=-1.0;
        return CacheFileLooksHealthy(job.output,1024) && ReadBannerTimestamp(job.output,verifyTime);
    }


    static bool GenerateGridThumb(const ThumbJob& job, const std::atomic<bool>* cancel = nullptr, const std::atomic<ULONGLONG>* pauseUntil = nullptr) {
        if (job.uiOutput.empty()) return true;
        double existingTime=-1.0;
        const bool imageHealthy=CacheFileLooksHealthy(job.uiOutput,512);
        const bool timeHealthy=!job.isVideo || ReadBannerTimestamp(job.uiOutput,existingTime);
        if(imageHealthy && timeHealthy) return true;
        if(PathExistsNoThrow(job.uiOutput)) RemoveGeneratedCacheFile(job.uiOutput);
        if(job.isVideo) RemoveGeneratedCacheFile(BannerTimestampPath(job.uiOutput));
        if(cancel && cancel->load(std::memory_order_acquire)) return false;

        const std::wstring tmp=job.uiOutput+L".tmp.jpg"; DeleteFileW(tmp.c_str());
        bool ok=false;
        double capturedSeconds=-1.0;
        if (job.isVideo) {
            // Library banners use the frame at 10%. Record the exact decoded timestamp so
            // the native Info banner can request the same frame rather than seeking again
            // from a percentage and potentially landing on a neighboring keyframe.
            ok=GenerateVideoStillMF(job.source,tmp,job.vr,640,360,92,0.10,-1.0,&capturedSeconds,cancel,pauseUntil);
        } else {
            Gdiplus::Image src(job.source.c_str());
            if (src.GetLastStatus()==Gdiplus::Ok&&src.GetWidth()&&src.GetHeight()) {
                const UINT sw=src.GetWidth(),sh=src.GetHeight();
                const double scale=std::min(1.0,std::min(640.0/static_cast<double>(sw),360.0/static_cast<double>(sh)));
                const UINT dw=std::max<UINT>(1u,static_cast<UINT>(sw*scale)); const UINT dh=std::max<UINT>(1u,static_cast<UINT>(sh*scale));
                Gdiplus::Bitmap out(dw,dh,PixelFormat24bppRGB); Gdiplus::Graphics g(&out);
                g.Clear(Gdiplus::Color(255,0,0,0)); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality); g.DrawImage(&src,Gdiplus::Rect(0,0,static_cast<INT>(dw),static_cast<INT>(dh)));
                std::error_code dirEc; fs::create_directories(fs::path(job.uiOutput).parent_path(),dirEc);
                if (!dirEc) ok=SaveJpeg(out,tmp,90);
            }
        }
        if (!ok || (cancel && cancel->load(std::memory_order_acquire))){DeleteFileW(tmp.c_str());return false;}
        DeleteFileW(job.uiOutput.c_str());
        if(!MoveFileExW(tmp.c_str(),job.uiOutput.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());return false;}
        if(job.isVideo) WriteBannerTimestamp(job.uiOutput,capturedSeconds);
        return CacheFileLooksHealthy(job.uiOutput,512);
    }

    void StartThumbnailWorker() {
        // Do not walk the entire media drive in the background. Missing Library
        // thumbnails are generated lazily by the viewport loader only when a card is
        // actually visible. This leaves removable/VeraCrypt libraries idle once the
        // current on-screen data has been copied into RAM.
        thumbWorkerRunning_.store(false,std::memory_order_release);
        thumbRepairRequested_.store(false,std::memory_order_release);
        ClearLoadingStateIf(1);
    }

    void StopThumbnailWorker() {
        thumbStop_.store(true,std::memory_order_release);
        if(thumbThread_.joinable()) thumbThread_.join();
        thumbWorkerRunning_.store(false,std::memory_order_release);
        thumbRepairRequested_.store(false,std::memory_order_release);
        ClearLoadingStateIf(1);
    }

    static std::wstring GenerationClaimKey(const std::wstring& source) {
        return ToLower(fs::path(source).lexically_normal().wstring());
    }

    bool TryClaimGeneration(const std::wstring& source) {
        const std::wstring key=GenerationClaimKey(source);
        if(key.empty()) return false;
        std::lock_guard<std::mutex> lock(generationClaimMutex_);
        return generationClaims_.insert(key).second;
    }

    void ReleaseGenerationClaim(const std::wstring& source) {
        const std::wstring key=GenerationClaimKey(source);
        std::lock_guard<std::mutex> lock(generationClaimMutex_);
        generationClaims_.erase(key);
    }

    bool WaitForGenerationClaim(const std::wstring& source,const std::atomic<bool>& stop) {
        while(!stop.load(std::memory_order_acquire)) {
            if(TryClaimGeneration(source)) return true;
            Sleep(30);
        }
        return false;
    }

    void InvalidateLoadingPopupArea() {
        if(!hwnd_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        RECT dirty{std::max<LONG>(0,rc.right-330),0,rc.right,std::min<LONG>(110,rc.bottom)};
        InvalidateRect(hwnd_,&dirty,FALSE);
    }

    void StopFullLoadWorker() {
        fullLoadStop_.store(true,std::memory_order_release);
        if(fullLoadThread_.joinable()) fullLoadThread_.join();
        fullLoadRunning_.store(false,std::memory_order_release);
        fullLoadStop_.store(false,std::memory_order_release);
        fullLoadCurrent_.store(0,std::memory_order_relaxed);
        fullLoadTotal_.store(0,std::memory_order_relaxed);
        fullLoadFailures_.store(0,std::memory_order_relaxed);
        fullLoadFinishedAt_=0;
        InvalidateLoadingPopupArea();
    }

    void StartFullLoadEverything() {
        if(fullLoadRunning_.load(std::memory_order_acquire)) {
            MessageBoxW(hwnd_,L"Load everything is already running in the background.",L"Visual MediaPlayer",MB_OK|MB_ICONINFORMATION);
            return;
        }
        if(videos_.empty() && images_.empty()) return;
        const int answer=MessageBoxW(hwnd_,
            L"Generate all library banners, info banners, and timeline images for every media file in the current library?\n\n"
            L"This can take a long time on a large library. You can keep using Visual MediaPlayer while it runs.",
            L"Load everything",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
        if(answer!=IDYES) return;

        // Reap a previously completed worker before starting a new run.
        if(fullLoadThread_.joinable()) fullLoadThread_.join();
        StopThumbnailWorker();

        std::vector<FullLoadJob> jobs;
        jobs.reserve(videos_.size()+images_.size());
        for(const auto& item:videos_) jobs.push_back({item.path,item.cachePath,item.uiCachePath,BuildPreviewDirectory(item.path),true,item.vr});
        for(const auto& item:images_) jobs.push_back({item.path,item.cachePath,item.uiCachePath,L"",false,item.vr});
        if(jobs.empty()) return;

        fullLoadStop_.store(false,std::memory_order_release);
        fullLoadCurrent_.store(0,std::memory_order_relaxed);
        fullLoadTotal_.store(static_cast<int>(jobs.size()),std::memory_order_relaxed);
        fullLoadFailures_.store(0,std::memory_order_relaxed);
        fullLoadFinishedAt_=0;
        fullLoadRunning_.store(true,std::memory_order_release);
        InvalidateLoadingPopupArea();

        fullLoadThread_=std::thread([this,jobs=std::move(jobs)]() mutable {
            const HRESULT coHr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
            int processed=0;
            for(const auto& job:jobs) {
                if(fullLoadStop_.load(std::memory_order_acquire)) break;
                if(!WaitForBackgroundPermit(fullLoadStop_,true)) break;

                std::error_code sourceEc;
                const bool sourceReady=fs::is_regular_file(job.source,sourceEc) && !sourceEc;
                bool jobOk=false;
                if(sourceReady && WaitForGenerationClaim(job.source,fullLoadStop_)) {
                    // Always re-check after acquiring the claim. The selected-media worker
                    // may have completed this exact file while Load everything was waiting.
                    if(job.isVideo) {
                        ThumbJob videoJob{job.source,job.cachePath,job.uiCachePath,true,job.vr};
                        GenerateGridThumb(videoJob,&fullLoadStop_,&backgroundPauseUntil_);

                        double libraryTime=-1.0,infoTime=-1.0;
                        bool libraryReady=CacheFileLooksHealthy(job.uiCachePath,512) && ReadBannerTimestamp(job.uiCachePath,libraryTime);
                        bool infoReady=CacheFileLooksHealthy(job.cachePath,1024) && ReadBannerTimestamp(job.cachePath,infoTime);
                        if(libraryReady && (!infoReady || std::abs(infoTime-libraryTime)>0.001)) {
                            if(CacheFileLooksHealthy(job.cachePath,1)) RemoveGeneratedCacheFile(job.cachePath);
                            RemoveGeneratedCacheFile(BannerTimestampPath(job.cachePath));
                            infoReady=false;
                        }
                        if(libraryReady && !infoReady && !fullLoadStop_.load(std::memory_order_acquire))
                            GenerateVideoCache(videoJob,&fullLoadStop_,&backgroundPauseUntil_);

                        if(!fullLoadStop_.load(std::memory_order_acquire) && !PreviewCacheFilesCompleteForDir(job.previewDir)) {
                            std::error_code markerEc; fs::remove(fs::path(job.previewDir)/L"complete.txt",markerEc);
                            GenerateVideoPreviewsMF(job.source,job.previewDir,job.vr,fullLoadStop_,nullptr,nullptr,nullptr,nullptr,&backgroundPauseUntil_);
                        }
                        UINT resolutionW=0,resolutionH=0;
                        const bool resolutionReady=EnsureResolutionMetadataCached(job.source,job.uiCachePath,resolutionW,resolutionH);
                        if(resolutionReady) QueueResolutionMetadata(job.source,job.uiCachePath,true);
                        HideCacheRootIfCreated(job.source);

                        libraryTime=-1.0; infoTime=-1.0;
                        libraryReady=CacheFileLooksHealthy(job.uiCachePath,512) && ReadBannerTimestamp(job.uiCachePath,libraryTime);
                        infoReady=CacheFileLooksHealthy(job.cachePath,1024) && ReadBannerTimestamp(job.cachePath,infoTime);
                        jobOk=libraryReady && infoReady && resolutionReady && std::abs(infoTime-libraryTime)<=0.001 && PreviewCacheFilesCompleteForDir(job.previewDir);
                    } else {
                        ThumbJob imageJob{job.source,job.cachePath,job.uiCachePath,false,job.vr};
                        if(GenerateGridThumb(imageJob,&fullLoadStop_,&backgroundPauseUntil_)) HideCacheRootIfCreated(job.source);
                        jobOk=CacheFileLooksHealthy(job.uiCachePath,512);
                    }
                    ReleaseGenerationClaim(job.source);
                }
                if(!jobOk && !fullLoadStop_.load(std::memory_order_acquire)) fullLoadFailures_.fetch_add(1,std::memory_order_relaxed);

                fullLoadCurrent_.store(++processed,std::memory_order_relaxed);
                if(hwnd_) PostMessageW(hwnd_,WM_APP_FULL_LOAD_PROGRESS,0,0);
                if(fullLoadStop_.load(std::memory_order_acquire)) break;
                Sleep(4);
            }
            const bool completed=!fullLoadStop_.load(std::memory_order_acquire);
            fullLoadRunning_.store(false,std::memory_order_release);
            if(hwnd_) PostMessageW(hwnd_,WM_APP_FULL_LOAD_DONE,completed?1:0,0);
            if(SUCCEEDED(coHr)) CoUninitialize();
        });
    }

    HBITMAP LoadBitmapViaWic(const std::wstring& file, int maxW, int maxH) {
        ComPtr<IWICImagingFactory> factory;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())))) return nullptr;
        ComPtr<IWICBitmapDecoder> decoder;
        if(FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.GetAddressOf()))) return nullptr;
        ComPtr<IWICBitmapFrameDecode> frame;
        if(FAILED(decoder->GetFrame(0,frame.GetAddressOf())) || !frame) return nullptr;
        UINT sw=0,sh=0;
        if(FAILED(frame->GetSize(&sw,&sh)) || !sw || !sh) return nullptr;

        UINT dw=sw,dh=sh;
        if(maxW>0 && maxH>0){
            const double scale=std::min(static_cast<double>(maxW)/static_cast<double>(sw),static_cast<double>(maxH)/static_cast<double>(sh));
            dw=std::max<UINT>(1u,static_cast<UINT>(std::lround(static_cast<double>(sw)*scale)));
            dh=std::max<UINT>(1u,static_cast<UINT>(std::lround(static_cast<double>(sh)*scale)));
        }

        IWICBitmapSource* source=frame.Get();
        ComPtr<IWICBitmapScaler> scaler;
        if(dw!=sw || dh!=sh){
            if(FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) || !scaler) return nullptr;
            if(FAILED(scaler->Initialize(frame.Get(),dw,dh,WICBitmapInterpolationModeFant))) return nullptr;
            source=scaler.Get();
        }

        ComPtr<IWICFormatConverter> converter;
        if(FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) || !converter) return nullptr;
        if(FAILED(converter->Initialize(source,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom))) return nullptr;
        if(dw>UINT_MAX/4u) return nullptr;
        const UINT stride=dw*4u;
        if(stride && dh>UINT_MAX/stride) return nullptr;
        const UINT bytes=stride*dh;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=static_cast<LONG>(dw);
        bmi.bmiHeader.biHeight=-static_cast<LONG>(dh);
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        HBITMAP hbmp=CreateDIBSection(nullptr,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!hbmp || !bits){ if(hbmp) DeleteObject(hbmp); return nullptr; }
        if(FAILED(converter->CopyPixels(nullptr,stride,bytes,static_cast<BYTE*>(bits)))){ DeleteObject(hbmp); return nullptr; }
        return hbmp;
    }

    HBITMAP LoadScaledBitmap(const std::wstring& file, int maxW, int maxH) {
        Gdiplus::Image src(file.c_str());
        if(src.GetLastStatus()==Gdiplus::Ok){
            const UINT sw=src.GetWidth(),sh=src.GetHeight();
            if(sw&&sh){
                const double scale=std::min(static_cast<double>(maxW)/sw,static_cast<double>(maxH)/sh);
                const int dw=std::max(1,static_cast<int>(sw*scale)); const int dh=std::max(1,static_cast<int>(sh*scale));
                Gdiplus::Bitmap out(dw,dh,PixelFormat32bppARGB); Gdiplus::Graphics g(&out);
                g.Clear(Gdiplus::Color(255,0,0,0)); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality); g.DrawImage(&src,Gdiplus::Rect(0,0,dw,dh));
                HBITMAP hbmp=nullptr;
                if(out.GetHBITMAP(Gdiplus::Color(255,0,0,0),&hbmp)==Gdiplus::Ok && hbmp) return hbmp;
            }
        }
        return LoadBitmapViaWic(file,maxW,maxH);
    }

    HBITMAP LoadShellCachedThumbByPath(const std::wstring& path, int w, int h) {
        ComPtr<IShellItem> shell;
        if(FAILED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&shell)))) return nullptr;
        ComPtr<IShellItemImageFactory> factory;
        if(FAILED(shell.As(&factory))) return nullptr;
        SIZE size{w,h}; HBITMAP bmp=nullptr;
        const SIIGBF flags=static_cast<SIIGBF>(SIIGBF_THUMBNAILONLY|SIIGBF_INCACHEONLY|SIIGBF_BIGGERSIZEOK);
        if(SUCCEEDED(factory->GetImage(size,flags,&bmp)) && bmp) return bmp;
        return nullptr;
    }

    static bool ProbeVideoFrameSizeMF(const std::wstring& path, UINT& width, UINT& height) {
        width=height=0;
        ComPtr<IMFSourceReader> reader;
        if(FAILED(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader))) return false;
        ComPtr<IMFMediaType> type;
        if(FAILED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&type)) || !type) return false;
        UINT w=0,h=0;
        if(FAILED(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h)) || !w || !h) return false;
        width=w; height=h;
        return true;
    }

    bool EnsureResolutionMetadataCached(const std::wstring& source,const std::wstring& uiCachePath,UINT& width,UINT& height) {
        width=height=0;
        std::lock_guard<std::mutex> cacheLock(resolutionMetadataCacheMutex_);
        if(ReadResolutionMetadata(uiCachePath,width,height)) return true;
        if(!ProbeVideoFrameSizeMF(source,width,height)) return false;
        if(!WriteResolutionMetadata(uiCachePath,width,height)) return false;
        HideCacheRootIfCreated(source);
        return true;
    }

    void StartResolutionMetadataWorker() {
        if(resolutionMetadataThread_.joinable()) return;
        resolutionMetadataStop_.store(false,std::memory_order_release);
        resolutionMetadataThread_=std::thread([this](){
            const HRESULT coHr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
            for(;;){
                ResolutionMetadataJob job;
                {
                    std::unique_lock<std::mutex> lock(resolutionMetadataMutex_);
                    resolutionMetadataCv_.wait(lock,[this]{
                        return resolutionMetadataStop_.load(std::memory_order_acquire) || !resolutionMetadataJobs_.empty();
                    });
                    if(resolutionMetadataStop_.load(std::memory_order_acquire)) break;
                    job=std::move(resolutionMetadataJobs_.front());
                    resolutionMetadataJobs_.pop_front();
                }
                if(job.generation!=resolutionMetadataGeneration_.load(std::memory_order_acquire)) continue;
                if((ProcessMemoryBytes()>=kProcessMemoryAllocationGuard || !job.highPriority) &&
                   !WaitForBackgroundPermit(resolutionMetadataStop_)) break;

                auto* result=new ResolutionMetadataResult();
                result->itemPath=job.itemPath; result->uiCachePath=job.uiCachePath;
                result->generation=job.generation; result->attempted=true;
                EnsureResolutionMetadataCached(job.itemPath,job.uiCachePath,result->sourceWidth,result->sourceHeight);

                {
                    std::lock_guard<std::mutex> lock(resolutionMetadataMutex_);
                    resolutionMetadataPendingPaths_.erase(ToLower(fs::path(job.uiCachePath).lexically_normal().wstring()));
                }
                if(job.generation!=resolutionMetadataGeneration_.load(std::memory_order_acquire)) { delete result; continue; }
                if(!hwnd_ || !PostMessageW(hwnd_,WM_APP_RESOLUTION_METADATA_READY,0,reinterpret_cast<LPARAM>(result))) delete result;
            }
            if(SUCCEEDED(coHr)) CoUninitialize();
        });
    }

    void StopResolutionMetadataWorker() {
        resolutionMetadataStop_.store(true,std::memory_order_release);
        resolutionMetadataCv_.notify_all();
        if(resolutionMetadataThread_.joinable()) resolutionMetadataThread_.join();
        resolutionMetadataStop_.store(false,std::memory_order_release);
        std::lock_guard<std::mutex> lock(resolutionMetadataMutex_);
        resolutionMetadataJobs_.clear(); resolutionMetadataPendingPaths_.clear();
    }

    void ResetResolutionMetadataWork() {
        resolutionMetadataGeneration_.fetch_add(1,std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(resolutionMetadataMutex_);
        resolutionMetadataJobs_.clear(); resolutionMetadataPendingPaths_.clear();
    }

    void QueueResolutionMetadata(const std::wstring& source,const std::wstring& uiCachePath,bool highPriority) {
        if(source.empty()||uiCachePath.empty()) return;
        if(!highPriority && ProcessMemoryBytes()>=kProcessMemoryEmergency) return;
        const std::wstring key=ToLower(fs::path(uiCachePath).lexically_normal().wstring());
        ResolutionMetadataJob job;
        job.itemPath=source; job.uiCachePath=uiCachePath; job.highPriority=highPriority;
        job.generation=resolutionMetadataGeneration_.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(resolutionMetadataMutex_);
            if(resolutionMetadataPendingPaths_.find(key)!=resolutionMetadataPendingPaths_.end()) return;
            resolutionMetadataPendingPaths_.insert(key);
            if(highPriority) resolutionMetadataJobs_.push_front(std::move(job));
            else resolutionMetadataJobs_.push_back(std::move(job));
        }
        resolutionMetadataCv_.notify_one();
    }

    void HandleResolutionMetadataResult(ResolutionMetadataResult* result) {
        if(!result) return;
        if(result->generation!=resolutionMetadataGeneration_.load(std::memory_order_acquire)){delete result;return;}
        bool changed=false;
        for(auto& item:videos_){
            if(!PathEquals(item.path,result->itemPath)) continue;
            item.resolutionMetadataQueued=false;
            item.resolutionProbeAttempted=result->attempted;
            if(result->sourceWidth&&result->sourceHeight){
                changed=item.sourceWidth!=result->sourceWidth || item.sourceHeight!=result->sourceHeight;
                item.sourceWidth=result->sourceWidth; item.sourceHeight=result->sourceHeight;
            }
            break;
        }
        if(searchVisible_&&!searchQuery_.empty()) filterDirty_=true;
        if((changed || (searchVisible_&&!searchQuery_.empty())) && mode_==Mode::Library) InvalidateLibraryScrollableArea();
        if(mode_==Mode::Details && category_==Category::Videos && selected_<videos_.size() && PathEquals(videos_[selected_].path,result->itemPath))
            InvalidateRect(hwnd_,nullptr,FALSE);
        delete result;
    }

    static int ResolutionBadgeClass(const MediaItem& item) {
        if(!item.isVideo || !item.sourceWidth || !item.sourceHeight) return 0;
        const UINT span=std::max(item.sourceWidth,item.sourceHeight);
        // Map encoded widths into the existing visual badge set. The project has
        // dedicated PNG artwork for 4K, 5K and 8K only, so intermediate VR widths
        // such as 5760/6144/7168 use the 5K badge rather than a mismatched text badge.
        if(span>=7680u) return 8;
        if(span>=5120u) return 5;
        if(span>=3840u) return 4;
        return 0;
    }

    Gdiplus::Bitmap* ResolutionBadgeBitmap(const MediaItem& item) const {
        switch(ResolutionBadgeClass(item)){
            case 4: return resolution4kBitmap_.get();
            case 5: return resolution5kBitmap_.get();
            case 8: return resolution8kBitmap_.get();
            default: return nullptr;
        }
    }

    void StartLibraryThumbLoader() {
        if(!libraryThumbLoadThreads_.empty()) return;
        libraryThumbLoadStop_.store(false,std::memory_order_release);
        auto worker=[this]() {
            const HRESULT coHr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            // Keep the UI/paint thread responsive even while several cached thumbnails are
            // being decoded in parallel. More workers provide throughput; lower priority
            // prevents them from stealing the frame budget from fullscreen scrolling.
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
            for(;;){
                LibraryThumbLoadJob job;
                {
                    std::unique_lock<std::mutex> lock(libraryThumbLoadMutex_);
                    libraryThumbLoadCv_.wait(lock,[this]{
                        return libraryThumbLoadStop_.load(std::memory_order_acquire) || !libraryThumbLoadJobs_.empty();
                    });
                    if(libraryThumbLoadStop_.load(std::memory_order_acquire)) break;
                    job=std::move(libraryThumbLoadJobs_.front());
                    libraryThumbLoadJobs_.pop_front();
                }

                if(job.epoch!=libraryThumbViewEpoch_.load(std::memory_order_acquire)) continue;
                const uint64_t memoryBefore=ProcessMemoryBytes();
                const bool fullLoadActive=fullLoadRunning_.load(std::memory_order_acquire) && !SystemMemoryCriticallyLow();
                // Existing disk-cache banners are cheap reconstructable UI state. Always
                // allow queued jobs to read them, even when process RAM is high. The old
                // early-continue path could drop a visible request without returning a
                // result, leaving that card gray indefinitely. Only opening the original
                // media to generate a missing banner remains pressure-gated.
                const bool allowSourceWork=fullLoadActive || memoryBefore<kProcessMemoryAllocationGuard;

                LibraryThumbLoadResult result;
                result.category=job.category;
                result.index=job.index;
                result.itemPath=job.itemPath;
                result.cachePath=job.cachePath;
                result.epoch=job.epoch;
                result.bitmapRequest=job.loadBitmap;

                // Thumbnail filesystem access and JPEG decoding happen here, never in
                // WM_PAINT. A missing/corrupt private cache is generated only for a
                // currently visible card. Off-screen prefetch may read an existing cache,
                // but it never opens the original media file to create new work.
                bool cacheReady=job.loadBitmap && CacheFileLooksHealthy(job.cachePath,512);
                if(cacheReady){
                    result.bitmap=LoadScaledBitmap(job.cachePath,std::max(1,job.width),std::max(1,job.height));
                    if(result.bitmap) result.fromPrivateCache=true;
                    else if(job.allowGenerate){
                        RemoveGeneratedCacheFile(job.cachePath);
                        if(job.isVideo) RemoveGeneratedCacheFile(BannerTimestampPath(job.cachePath));
                        cacheReady=false;
                    } else {
                        result.privateDecodeFailed=true;
                    }
                }

                if(job.loadBitmap && !result.bitmap && !cacheReady && job.allowGenerate && allowSourceWork &&
                   !fullLoadRunning_.load(std::memory_order_acquire) && PathExistsNoThrow(job.itemPath)){
                    const bool claimed=TryClaimGeneration(job.itemPath);
                    if(claimed){
                        ThumbJob generated{job.itemPath,L"",job.cachePath,job.isVideo,job.vr};
                        if(GenerateGridThumb(generated,&libraryThumbLoadStop_,nullptr)){
                            HideCacheRootIfCreated(job.itemPath);
                            result.bitmap=LoadScaledBitmap(job.cachePath,std::max(1,job.width),std::max(1,job.height));
                            if(result.bitmap) result.fromPrivateCache=true;
                        }
                        ReleaseGenerationClaim(job.itemPath);
                    }
                }

                // A Shell thumbnail is a one-shot fallback for non-VR media. Once copied
                // into our HBITMAP cache, painting it does not keep a source-file handle.
                // VR waits for the private cropped thumbnail so stereo packing never flashes.
                if(job.loadBitmap && job.allowGenerate && allowSourceWork && !result.bitmap && !(job.isVideo && job.isVr))
                    result.bitmap=LoadShellCachedThumbByPath(job.itemPath,job.width,job.height);

                if(result.bitmap){
                    BITMAP bm{}; GetObjectW(result.bitmap,sizeof(bm),&bm);
                    result.width=bm.bmWidth; result.height=bm.bmHeight;
                }

                if(libraryThumbLoadStop_.load(std::memory_order_acquire) ||
                   job.epoch!=libraryThumbViewEpoch_.load(std::memory_order_acquire)){
                    if(result.bitmap) DeleteObject(result.bitmap);
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
                    libraryThumbLoadResults_.push_back(std::move(result));
                }
                if(hwnd_ && !libraryThumbResultMessagePending_.exchange(true,std::memory_order_acq_rel))
                    PostMessageW(hwnd_,WM_APP_LIBRARY_THUMB_LOADED,0,0);
            }
            if(SUCCEEDED(coHr)) CoUninitialize();
        };

        // Smoothness-first Library policy: keep enough parallel decode work in flight that
        // a fast fullscreen scroll is normally drawing from RAM rather than waiting on disk.
        // The cache remains bounded, but deliberately much larger than the old low-RAM setup.
        for(int i=0;i<8;++i) libraryThumbLoadThreads_.emplace_back(worker);
    }

    void StopLibraryThumbLoader() {
        libraryThumbLoadStop_.store(true,std::memory_order_release);
        libraryThumbLoadCv_.notify_all();
        for(auto& thread:libraryThumbLoadThreads_) if(thread.joinable()) thread.join();
        libraryThumbLoadThreads_.clear();

        std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
        libraryThumbLoadJobs_.clear();
        for(auto& result:libraryThumbLoadResults_) if(result.bitmap) DeleteObject(result.bitmap);
        libraryThumbLoadResults_.clear();
        libraryThumbResultMessagePending_.store(false,std::memory_order_release);
    }

    void ResetLibraryThumbLoadView() {
        libraryThumbViewEpoch_.fetch_add(1,std::memory_order_acq_rel);
        libraryThumbViewportScrollY_=-1;
        libraryThumbViewportCardWidth_=-1;
        libraryThumbViewportClientWidth_=-1;
        libraryThumbViewportFolder_.clear();
        libraryThumbViewportSearch_.clear();
        protectedLibraryThumbPaths_.clear();
        visibleLibraryGpuThumbPaths_.clear();
        {
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            libraryThumbLoadJobs_.clear();
            for(auto& result:libraryThumbLoadResults_) if(result.bitmap) DeleteObject(result.bitmap);
            libraryThumbLoadResults_.clear();
        }
        libraryThumbResultMessagePending_.store(false,std::memory_order_release);
    }

    void RefreshLibraryThumbViewport(RECT rc) {
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left));
        const bool scrollChanged=libraryThumbViewportScrollY_!=scrollY_;
        const bool identityChanged=libraryThumbViewportCardWidth_!=libraryCardWidth_ ||
                                   libraryThumbViewportClientWidth_!=clientWidth ||
                                   libraryThumbViewportCategory_!=category_ ||
                                   libraryThumbViewportFolder_!=currentFolder_ ||
                                   libraryThumbViewportSearch_!=searchQuery_;
        if(!scrollChanged && !identityChanged) return;

        if(libraryThumbViewportScrollY_>=0 && scrollChanged)
            libraryThumbPrefetchDirection_=(scrollY_>libraryThumbViewportScrollY_)?1:-1;

        // Normal wheel scrolling keeps useful visible work in flight, but queued prefetch
        // from the OLD viewport is disposable. Purging it keeps a fast scroll/scrollbar drag
        // from building a long tail of far-away disk/decode work behind the new viewport.
        // A large jump (or live scrollbar drag) additionally advances the epoch so already
        // running far-away work is discarded when it completes.
        const int scrollDistance=(libraryThumbViewportScrollY_>=0)?std::abs(scrollY_-libraryThumbViewportScrollY_):0;
        const int viewportHeight=std::max(1,static_cast<int>(rc.bottom-rc.top)-68);
        const bool largeJump=scrollChanged && (libraryScrollDragging_ || scrollDistance>viewportHeight);
        if(identityChanged || largeJump){
            libraryThumbViewEpoch_.fetch_add(1,std::memory_order_acq_rel);
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            libraryThumbLoadJobs_.clear();
            for(auto& result:libraryThumbLoadResults_) if(result.bitmap) DeleteObject(result.bitmap);
            libraryThumbLoadResults_.clear();
            libraryThumbResultMessagePending_.store(false,std::memory_order_release);
        }else if(scrollChanged){
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            for(auto it=libraryThumbLoadJobs_.begin();it!=libraryThumbLoadJobs_.end();){
                if(it->allowGenerate){ ++it; continue; } // visible/high-priority work stays
                auto& list=it->category==Category::Videos?videos_:images_;
                if(it->index<list.size() && PathEquals(list[it->index].path,it->itemPath) &&
                   list[it->index].thumbLoadRequestEpoch==it->epoch)
                    list[it->index].thumbLoadRequestEpoch=0;
                it=libraryThumbLoadJobs_.erase(it);
            }
        }
        libraryThumbViewportScrollY_=scrollY_;
        libraryThumbViewportCardWidth_=libraryCardWidth_;
        libraryThumbViewportClientWidth_=clientWidth;
        libraryThumbViewportCategory_=category_;
        libraryThumbViewportFolder_=currentFolder_;
        libraryThumbViewportSearch_=searchQuery_;
    }

    void PrimeVisibleLibraryThumbsFromPrivateCache() {
        if(mode_!=Mode::Library || !hwnd_) return;
        visibleLibraryGpuThumbPaths_.clear();
        RECT rc{};GetClientRect(hwnd_,&rc);const auto& filtered=FilteredIndices();const auto visibleFolders=VisibleFolderIndices();const size_t totalCards=visibleFolders.size()+filtered.size();if(totalCards==0)return;
        auto& list=CurrentItems();const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_,imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0))),cardH=imageH+kLibraryTitleHeight,rowStride=cardH+gap;
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve),cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap)),rows=static_cast<int>((totalCards+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols)),visibleBottom=std::max(pad,static_cast<int>(rc.bottom)-68);
        const int firstRow=std::clamp(scrollY_/std::max(1,rowStride)-1,0,std::max(0,rows-1)),lastRow=std::clamp((scrollY_+std::max(0,visibleBottom-pad))/std::max(1,rowStride)+1,0,std::max(0,rows-1));
        const size_t first=static_cast<size_t>(firstRow)*static_cast<size_t>(cols),last=std::min(totalCards,static_cast<size_t>(lastRow+1)*static_cast<size_t>(cols));
        for(size_t displayIndex=first;displayIndex<last;++displayIndex){if(displayIndex<visibleFolders.size())continue;const size_t mdi=displayIndex-visibleFolders.size();if(mdi>=filtered.size())continue;MediaItem& item=list[filtered[mdi]];protectedLibraryThumbPaths_.insert(item.path);visibleLibraryGpuThumbPaths_.insert(item.path);if(item.thumb||!CacheFileLooksHealthy(item.uiCachePath,512))continue;HBITMAP bitmap=LoadScaledBitmap(item.uiCachePath,640,360);if(!bitmap)continue;BITMAP bm{};GetObjectW(bitmap,sizeof(bm),&bm);item.thumb=bitmap;item.thumbW=bm.bmWidth;item.thumbH=bm.bmHeight;item.thumbFromPrivateCache=true;item.thumbAttempted=true;item.thumbLastUsed=GetTickCount64();}
        TrimThumbMemory();
    }

    HBITMAP GetLibraryItemThumb(MediaItem& item, size_t index, int w, int h, bool highPriority=false) {
        const ULONGLONG now=GetTickCount64();
        item.thumbLastUsed=now;
        // Resolution probing is also demand-driven: only a visible video may cause a
        // one-time source metadata read. Search-specific probing still has its own
        // explicit high-priority path.
        if(highPriority && item.isVideo && !item.resolutionProbeAttempted && !item.resolutionMetadataQueued){
            item.resolutionMetadataQueued=true;
            QueueResolutionMetadata(item.path,item.uiCachePath,true);
        }

        // Once a thumbnail has been copied into RAM, never reopen its file merely because
        // the card was repainted or its aspect ratio does not fill both requested bounds.
        if(item.thumb) return item.thumb;

        if(!highPriority && !fullLoadRunning_.load(std::memory_order_acquire) && ProcessMemoryBytes()>=kProcessMemoryEmergency) return nullptr;
        const uint64_t epoch=libraryThumbViewEpoch_.load(std::memory_order_acquire);
        const bool alreadyRequested=item.thumbLoadRequestEpoch==epoch &&
                                    item.thumbLoadRequestW>=w && item.thumbLoadRequestH>=h;

        // Absolute visible-card priority: if this card was merely prefetched and then
        // scrolls into view, promote the existing queued request instead of leaving it
        // buried behind obsolete prefetch work. If a worker already owns the request,
        // it is already actively being decoded and needs no promotion.
        if(highPriority && alreadyRequested){
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            for(auto it=libraryThumbLoadJobs_.begin();it!=libraryThumbLoadJobs_.end();++it){
                if(it->epoch!=epoch || it->category!=category_ || it->index!=index || !PathEquals(it->itemPath,item.path)) continue;
                LibraryThumbLoadJob promoted=std::move(*it);
                libraryThumbLoadJobs_.erase(it);
                promoted.allowGenerate=true;
                libraryThumbLoadJobs_.push_front(std::move(promoted));
                libraryThumbLoadCv_.notify_one();
                break;
            }
        }
        if(!alreadyRequested && now>=item.thumbNextLoadAttempt){
            item.thumbLoadRequestEpoch=epoch;
            item.thumbLoadRequestW=w;
            item.thumbLoadRequestH=h;
            LibraryThumbLoadJob job;
            job.category=category_;
            job.index=index;
            job.itemPath=item.path;
            job.cachePath=item.uiCachePath;
            job.isVideo=item.isVideo;
            job.isVr=item.vr.vr;
            job.loadBitmap=true;
            job.allowGenerate=highPriority;
            job.vr=item.vr;
            job.width=w; job.height=h; job.epoch=epoch;
            {
                std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
                if(highPriority) libraryThumbLoadJobs_.push_front(std::move(job));
                else libraryThumbLoadJobs_.push_back(std::move(job));
            }
            libraryThumbLoadCv_.notify_one();
        }
        return item.thumb;
    }

    void ApplyLibraryThumbLoadResults() {
        std::vector<LibraryThumbLoadResult> ready;
        {
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            ready.swap(libraryThumbLoadResults_);
        }
        libraryThumbResultMessagePending_.store(false,std::memory_order_release);

        const uint64_t activeEpoch=libraryThumbViewEpoch_.load(std::memory_order_acquire);
        const ULONGLONG now=GetTickCount64();
        bool changed=false;
        for(auto& result:ready){
            if(result.epoch!=activeEpoch){ if(result.bitmap) DeleteObject(result.bitmap); continue; }
            auto& list=result.category==Category::Videos?videos_:images_;
            if(result.index>=list.size() || !PathEquals(list[result.index].path,result.itemPath)){
                if(result.bitmap) DeleteObject(result.bitmap);
                continue;
            }

            MediaItem& item=list[result.index];
            if(result.bitmapRequest && item.thumbLoadRequestEpoch==result.epoch) item.thumbLoadRequestEpoch=0;


            if(result.privateDecodeFailed && !thumbWorkerRunning_.load(std::memory_order_acquire)){
                // Corrupt generated files are repaired outside WM_PAINT. Never remove a
                // cache file while the thumbnail generator may still be writing it.
                RemoveGeneratedCacheFile(result.cachePath);
                RemoveGeneratedCacheFile(BannerTimestampPath(result.cachePath));
                if(hwnd_) PostMessageW(hwnd_,WM_APP_CACHE_REPAIR,0,0);
            }

            if(result.bitmap){
                const bool protectedNow=protectedLibraryThumbPaths_.find(item.path)!=protectedLibraryThumbPaths_.end();
                const bool visibleNow=visibleLibraryGpuThumbPaths_.find(item.path)!=visibleLibraryGpuThumbPaths_.end();
                if(!fullLoadRunning_.load(std::memory_order_acquire) && ProcessMemoryBytes()>=kProcessMemoryAllocationGuard && !protectedNow && !visibleNow){DeleteObject(result.bitmap);result.bitmap=nullptr;item.thumbNextLoadAttempt=now+1500;continue;}
                const bool shouldReplace=!item.thumb || result.fromPrivateCache || !item.thumbFromPrivateCache;
                if(shouldReplace){
                    if(item.thumb) DeleteObject(item.thumb);
                    item.libraryGpuThumb.Reset(); item.libraryGpuThumbSource=nullptr; item.libraryGpuGeneration=0;
                    item.thumb=result.bitmap; result.bitmap=nullptr;
                    item.thumbW=result.width; item.thumbH=result.height;
                    item.thumbFromPrivateCache=result.fromPrivateCache;
                    item.thumbAttempted=true;
                    item.thumbLastUsed=now;
                    changed=true;
                }
                if(result.bitmap) DeleteObject(result.bitmap);
                // Shell fallback is deliberately retried later so it upgrades to the
                // private thumbnail once background generation completes.
                item.thumbNextLoadAttempt=result.fromPrivateCache?0:(now+1500);
            } else if(!item.thumb) {
                item.thumbNextLoadAttempt=now+600;
            }
        }

        TrimThumbMemory();
        if(changed && mode_==Mode::Library) InvalidateLibraryScrollableArea();

        // If the worker raced with the message handler and produced another result after
        // our swap, make sure a follow-up message is posted.
        bool hasMore=false;
        {
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            hasMore=!libraryThumbLoadResults_.empty();
        }
        if(hasMore && hwnd_ && !libraryThumbResultMessagePending_.exchange(true,std::memory_order_acq_rel))
            PostMessageW(hwnd_,WM_APP_LIBRARY_THUMB_LOADED,0,0);
    }

    void InvalidateLibraryScrollableArea() {
        if(!hwnd_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        RECT dirty{0,0,rc.right,std::max<LONG>(0,rc.bottom-64)};
        InvalidateRect(hwnd_,&dirty,FALSE);
    }

    bool ScrollBufferedRegionVertical(int pixelDelta, RECT region) {
        if(!hwnd_ || pixelDelta==0 || !backDC_) return false;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        RECT clipped{};
        if(!IntersectRect(&clipped,&region,&rc) || IsRectEmpty(&clipped)) return false;
        const int regionH=static_cast<int>(clipped.bottom-clipped.top);
        if(std::abs(pixelDelta)>=regionH || backW_!=rc.right-rc.left || backH_!=rc.bottom-rc.top) return false;

        // Shift the retained backbuffer and the visible client pixels first. This makes
        // the scroll response immediate; WM_PAINT then redraws only newly exposed areas.
        if(!ScrollDC(backDC_,0,pixelDelta,&clipped,&clipped,nullptr,nullptr)) return false;
        HRGN update=CreateRectRgn(0,0,0,0);
        if(!update) return false;
        ScrollWindowEx(hwnd_,0,pixelDelta,&clipped,&clipped,update,nullptr,0);
        InvalidateRgn(hwnd_,update,FALSE);
        DeleteObject(update);
        return true;
    }

    void InvalidateLibraryScrollWithFooter(int /*oldScrollY*/) {
        if(!hwnd_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        // Library scrolling is deliberately rendered as one fresh buffered frame.
        // Reusing/shifting pixels from the previous frame creates exposed-edge seams
        // at high resolutions. The thumbnail cache keeps the redraw RAM-backed and
        // the painter only walks rows that intersect the viewport.
        InvalidateRect(hwnd_,&rc,FALSE);
    }

    void InvalidateDetailsScrollOptimized(int /*oldScrollY*/) {
        if(!hwnd_) return;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        // Details/Info now uses the same GPU compositor as Library. Repaint from the
        // current scroll offset instead of shifting old GDI pixels with ScrollWindowEx.
        InvalidateRect(hwnd_,&rc,FALSE);
    }

    HBITMAP LoadNativeBitmap(const std::wstring& file) {
        Gdiplus::Image src(file.c_str());
        if(src.GetLastStatus()==Gdiplus::Ok){
            const UINT sw=src.GetWidth(),sh=src.GetHeight();
            if(sw&&sh){
                Gdiplus::Bitmap out(sw,sh,PixelFormat32bppARGB); Gdiplus::Graphics g(&out);
                g.Clear(Gdiplus::Color(255,0,0,0));
                g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                g.DrawImage(&src,Gdiplus::Rect(0,0,static_cast<INT>(sw),static_cast<INT>(sh)));
                HBITMAP hbmp=nullptr;
                if(out.GetHBITMAP(Gdiplus::Color(255,0,0,0),&hbmp)==Gdiplus::Ok && hbmp) return hbmp;
            }
        }
        return LoadBitmapViaWic(file,0,0);
    }

    HBITMAP TryShellCachedThumb(MediaItem& item, int w, int h) {
        if(item.thumb || item.thumbAttempted) return item.thumb;
        item.thumbAttempted=true;
        // A Shell thumbnail contains the packed VR frame and can briefly show a doubled
        // image before our cropped cache is ready. For VR, wait for the private MF cache.
        if(item.isVideo && item.vr.vr) return nullptr;
        ComPtr<IShellItem> shell; if(FAILED(SHCreateItemFromParsingName(item.path.c_str(),nullptr,IID_PPV_ARGS(&shell)))) return nullptr;
        ComPtr<IShellItemImageFactory> f; if(FAILED(shell.As(&f))) return nullptr;
        SIZE size{w,h}; HBITMAP bmp=nullptr;
        SIIGBF flags=static_cast<SIIGBF>(SIIGBF_THUMBNAILONLY|SIIGBF_INCACHEONLY|SIIGBF_BIGGERSIZEOK);
        if(SUCCEEDED(f->GetImage(size,flags,&bmp))&&bmp){
            item.thumb=bmp; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); item.thumbW=bm.bmWidth; item.thumbH=bm.bmHeight;
            item.thumbFromPrivateCache=false;
        }
        return item.thumb;
    }

    HBITMAP GetItemThumb(MediaItem& item, int w, int h) {
        item.thumbLastUsed=GetTickCount64();
        const bool gridSize = w <= 800 && h <= 450;
        const std::wstring preferred = gridSize ? item.uiCachePath : (item.isVideo ? item.cachePath : item.path);
        const bool generated = preferred==item.uiCachePath || (item.isVideo && preferred==item.cachePath);
        bool cached = generated ? CacheFileLooksHealthy(preferred,gridSize?512:1024) : PathExistsNoThrow(preferred);
        if(!cached && generated && PathExistsNoThrow(preferred)){
            RemoveGeneratedCacheFile(preferred);
            if(preferred==item.uiCachePath) RemoveGeneratedCacheFile(BannerTimestampPath(item.uiCachePath));
            item.thumbAttempted=false;
            if(hwnd_) PostMessageW(hwnd_,WM_APP_CACHE_REPAIR,0,0);
        }
        if(cached){
            const bool tooSmall=item.thumb && (item.thumbW < w || item.thumbH < h);
            if(!item.thumbFromPrivateCache || tooSmall){
                if(item.thumb){ DeleteObject(item.thumb); item.thumb=nullptr; }
                item.libraryGpuThumb.Reset(); item.libraryGpuThumbSource=nullptr; item.libraryGpuGeneration=0;
                item.thumb=LoadScaledBitmap(preferred,std::max(1,w),std::max(1,h));
                if(item.thumb){
                    BITMAP bm{}; GetObjectW(item.thumb,sizeof(bm),&bm); item.thumbW=bm.bmWidth; item.thumbH=bm.bmHeight; item.thumbFromPrivateCache=true;
                } else if(generated) {
                    RemoveGeneratedCacheFile(preferred);
                    if(preferred==item.uiCachePath) RemoveGeneratedCacheFile(BannerTimestampPath(item.uiCachePath));
                    item.thumbAttempted=false;
                    if(hwnd_) PostMessageW(hwnd_,WM_APP_CACHE_REPAIR,0,0);
                    cached=false;
                }
            }
            if(item.thumb) return item.thumb;
        }
        // Never decode the native private Info cache on the UI thread just to paint a card.
        // The existing Windows thumbnail remains an acceptable temporary fallback for
        // non-VR video while our deterministic 10% Library banner is regenerated.
        return TryShellCachedThumb(item,w,h);
    }

    HBITMAP GetDetailsBanner(MediaItem& item) {
        const std::wstring preferred=item.isVideo ? item.cachePath : item.path;
        if(item.isVideo && PathExistsNoThrow(preferred) && !CacheFileLooksHealthy(preferred,1024)){
            RemoveGeneratedCacheFile(preferred);
            RemoveGeneratedCacheFile(BannerTimestampPath(preferred));
            if(hwnd_) PostMessageW(hwnd_,WM_APP_CACHE_REPAIR,0,0);
            return nullptr;
        }
        if(!PathExistsNoThrow(preferred)) return nullptr;
        if(!item.isVideo && item.detailDecodeUnsupported) return nullptr;
        if(!item.detailThumb){
            item.detailThumb=LoadNativeBitmap(preferred);
            if(item.detailThumb){
                item.detailDecodeUnsupported=false;
                BITMAP bm{}; GetObjectW(item.detailThumb,sizeof(bm),&bm); item.detailThumbW=bm.bmWidth; item.detailThumbH=bm.bmHeight;
            } else if(item.isVideo) {
                RemoveGeneratedCacheFile(preferred);
                RemoveGeneratedCacheFile(BannerTimestampPath(preferred));
                if(hwnd_) PostMessageW(hwnd_,WM_APP_CACHE_REPAIR,0,0);
            } else {
                item.detailDecodeUnsupported=true;
                ShowInAppNotice(L"This media is unsupported.",5000);
            }
        }
        return item.detailThumb;
    }

    std::vector<size_t> DetailWindowIndices() const {
        const auto& items=CurrentItems();
        std::vector<size_t> indices;
        if(mode_!=Mode::Details || selected_>=items.size()) return indices;
        indices.push_back(selected_);
        size_t left=selected_,right=selected_;
        for(int distance=1;distance<=4;++distance){
            size_t next=0;
            if(FindAdjacentInSameFolder(items,right,1,next)){right=next;indices.push_back(right);}
            if(FindAdjacentInSameFolder(items,left,-1,next)){left=next;indices.push_back(left);}
        }
        return indices;
    }

    bool IsPathInDetailWindow(const std::wstring& path,const std::vector<size_t>& indices) const {
        const auto& items=CurrentItems();
        for(size_t idx:indices) if(idx<items.size() && items[idx].path==path) return true;
        return false;
    }

    void TrimDetailInfoToWindow(const std::vector<size_t>& indices) {
        const auto allowed=[this,&indices](const std::wstring& path){return IsPathInDetailWindow(path,indices);};
        auto trim=[&](std::vector<MediaItem>& list,bool activeList){
            for(auto& item:list){
                const bool keep=activeList && allowed(item.path);
                if(!keep && item.detailThumb){
                    DeleteObject(item.detailThumb); item.detailThumb=nullptr; item.detailThumbW=0; item.detailThumbH=0;
                    item.detailsGpuThumb.Reset(); item.detailsGpuThumbSource=nullptr; item.detailsGpuGeneration=0;
                }
            }
        };
        trim(videos_,category_==Category::Videos);
        trim(images_,category_==Category::Images);
        for(auto it=prefetchedPreviewSets_.begin();it!=prefetchedPreviewSets_.end();){
            if(!allowed(it->first)){
                DeletePreviewFrameBitmaps(it->second.frames);
                it=prefetchedPreviewSets_.erase(it);
            } else ++it;
        }
    }

    void EnsureDetailPrefetchWorker() {
        if(detailPrefetchThread_.joinable()) return;
        detailPrefetchStop_=false;
        detailPrefetchThread_=std::thread([this](){
            while(true){
                DetailPrefetchJob job;
                {
                    std::unique_lock<std::mutex> lock(detailPrefetchMutex_);
                    detailPrefetchCv_.wait(lock,[this]{return detailPrefetchStop_ || !detailPrefetchJobs_.empty();});
                    if(detailPrefetchStop_) break;
                    job=std::move(detailPrefetchJobs_.front());
                    detailPrefetchJobs_.erase(detailPrefetchJobs_.begin());
                }
                auto* result=new DetailPrefetchResult();
                result->generation=job.generation; result->category=job.category; result->index=job.index; result->mediaPath=job.mediaPath;
                const bool bannerFileReady=job.isVideo ? CacheFileLooksHealthy(job.bannerPath,1024) : FileHasData(fs::path(job.bannerPath));
                if(job.loadBanner && !job.bannerPath.empty() && bannerFileReady){
                    result->banner=LoadNativeBitmap(job.bannerPath);
                    if(result->banner){BITMAP bm{};GetObjectW(result->banner,sizeof(bm),&bm);result->bannerW=bm.bmWidth;result->bannerH=bm.bmHeight;}
                }
                if(job.loadPreviews && job.isVideo){
                    result->previewSet=LoadPrefetchedPreviewSet(job.previewDir);
                    result->hasPreviewSet=result->previewSet.duration>0.0;
                }
                if(job.generation!=detailPrefetchGeneration_.load(std::memory_order_acquire)){
                    if(result->banner) DeleteObject(result->banner);
                    DeletePreviewFrameBitmaps(result->previewSet.frames);
                    delete result; continue;
                }
                if(!hwnd_ || !PostMessageW(hwnd_,WM_APP_DETAIL_PREFETCH_READY,0,reinterpret_cast<LPARAM>(result))){
                    if(result->banner) DeleteObject(result->banner);
                    DeletePreviewFrameBitmaps(result->previewSet.frames);
                    delete result;
                }
            }
        });
    }

    void StopDetailPrefetchWorker() {
        {
            std::lock_guard<std::mutex> lock(detailPrefetchMutex_);
            detailPrefetchStop_=true;
            detailPrefetchJobs_.clear();
        }
        detailPrefetchGeneration_.fetch_add(1,std::memory_order_acq_rel);
        detailPrefetchCv_.notify_all();
        if(detailPrefetchThread_.joinable()) detailPrefetchThread_.join();
        detailPrefetchStop_=false;
    }

    void QueueDetailPrefetchWindow() {
        if(mode_!=Mode::Details) { CancelDetailPrefetchJobs(); return; }
        // Keep the existing nine-media RAM window for items the user has actually
        // visited, but never read banners/timelines for neighboring media in advance.
        // This makes an idle Info screen stop touching the media volume.
        const auto indices=DetailWindowIndices();
        if(!indices.empty()) TrimDetailInfoToWindow(indices);
        CancelDetailPrefetchJobs();
    }

    void MergePrefetchedFramesIntoActive(PrefetchedPreviewSet& set) {
        std::map<int,HBITMAP> incoming;
        for(auto& f:set.frames){if(f.bitmap){incoming[f.seconds]=f.bitmap;f.bitmap=nullptr;}}
        for(auto& active:previewFrames_){
            if(active.bitmap) continue;
            auto it=incoming.find(active.seconds);
            if(it!=incoming.end()){active.bitmap=it->second;active.lastUsed=GetTickCount64();incoming.erase(it);}
        }
        for(auto& kv:incoming) if(kv.second) DeleteObject(kv.second);
    }

    void HandleDetailPrefetchResult(DetailPrefetchResult* result) {
        if(!result) return;
        const auto cleanup=[&](){
            if(result->banner) DeleteObject(result->banner);
            DeletePreviewFrameBitmaps(result->previewSet.frames);
            delete result;
        };
        if(result->generation!=detailPrefetchGeneration_.load(std::memory_order_acquire) || mode_!=Mode::Details || result->category!=category_){cleanup();return;}
        auto& items=CurrentItems();
        if(result->index>=items.size() || items[result->index].path!=result->mediaPath){cleanup();return;}
        const auto indices=DetailWindowIndices();
        if(!IsPathInDetailWindow(result->mediaPath,indices)){cleanup();return;}
        auto& item=items[result->index];
        if(result->banner && !item.detailThumb){
            item.detailThumb=result->banner; item.detailThumbW=result->bannerW; item.detailThumbH=result->bannerH; result->banner=nullptr;
        }
        if(result->hasPreviewSet && item.isVideo){
            if(previewMediaPath_==result->mediaPath){
                MergePrefetchedFramesIntoActive(result->previewSet);
                if(result->previewSet.duration>0.0) detailsDurationSeconds_.store(result->previewSet.duration,std::memory_order_relaxed);
            } else {
                auto existing=prefetchedPreviewSets_.find(result->mediaPath);
                if(existing!=prefetchedPreviewSets_.end()){
                    DeletePreviewFrameBitmaps(existing->second.frames);
                    prefetchedPreviewSets_.erase(existing);
                }
                prefetchedPreviewSets_.emplace(result->mediaPath,std::move(result->previewSet));
            }
        }
        cleanup();
        TrimDetailInfoToWindow(indices);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    static void DrawBitmapCover(HDC dc,HBITMAP bmp,RECT r) {
        if(!bmp) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); SetStretchBltMode(dc,HALFTONE);
        const double sx=static_cast<double>(r.right-r.left)/bm.bmWidth, sy=static_cast<double>(r.bottom-r.top)/bm.bmHeight;
        const double s=std::max(sx,sy); const int sw=std::max(1,static_cast<int>((r.right-r.left)/s)); const int sh=std::max(1,static_cast<int>((r.bottom-r.top)/s));
        const int srcx=(bm.bmWidth-sw)/2,srcy=(bm.bmHeight-sh)/2;
        StretchBlt(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,mem,srcx,srcy,sw,sh,SRCCOPY); SelectObject(mem,old); DeleteDC(mem);
    }

    static void DrawBitmapCoverWithSourceDC(HDC dc,HDC sourceDc,HBITMAP bmp,RECT r) {
        if(!bmp || !sourceDc) return;
        BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HGDIOBJ old=SelectObject(sourceDc,bmp);
        const double sx=static_cast<double>(r.right-r.left)/bm.bmWidth, sy=static_cast<double>(r.bottom-r.top)/bm.bmHeight;
        const double scale=std::max(sx,sy);
        const int sw=std::max(1,static_cast<int>((r.right-r.left)/scale));
        const int sh=std::max(1,static_cast<int>((r.bottom-r.top)/scale));
        const int srcx=(bm.bmWidth-sw)/2,srcy=(bm.bmHeight-sh)/2;
        StretchBlt(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,sourceDc,srcx,srcy,sw,sh,SRCCOPY);
        SelectObject(sourceDc,old);
    }

    static void DrawBitmapContain(HDC dc,HBITMAP bmp,RECT r) {
        if(!bmp) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); SetStretchBltMode(dc,HALFTONE);
        const double s=std::min(static_cast<double>(r.right-r.left)/bm.bmWidth,static_cast<double>(r.bottom-r.top)/bm.bmHeight);
        const int dw=std::max(1,static_cast<int>(bm.bmWidth*s)),dh=std::max(1,static_cast<int>(bm.bmHeight*s));
        const int dx=r.left+((r.right-r.left)-dw)/2,dy=r.top+((r.bottom-r.top)-dh)/2;
        StretchBlt(dc,dx,dy,dw,dh,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY); SelectObject(mem,old); DeleteDC(mem);
    }

    static void DrawBitmapContainAlpha(HDC dc,HBITMAP bmp,RECT r,BYTE alpha) {
        if(!bmp || alpha==0) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp);
        const double scale=std::min(static_cast<double>(r.right-r.left)/bm.bmWidth,static_cast<double>(r.bottom-r.top)/bm.bmHeight);
        const int dw=std::max(1,static_cast<int>(bm.bmWidth*scale)),dh=std::max(1,static_cast<int>(bm.bmHeight*scale));
        const int dx=r.left+((r.right-r.left)-dw)/2,dy=r.top+((r.bottom-r.top)-dh)/2;
        BLENDFUNCTION bf{AC_SRC_OVER,0,alpha,0};
        AlphaBlend(dc,dx,dy,dw,dh,mem,0,0,bm.bmWidth,bm.bmHeight,bf);
        SelectObject(mem,old); DeleteDC(mem);
    }

    static void DrawBitmapContainNoUpscale(HDC dc,HBITMAP bmp,RECT r) {
        if(!bmp) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); SetStretchBltMode(dc,HALFTONE);
        const int dw=bm.bmWidth,dh=bm.bmHeight;
        const int dx=r.left+((r.right-r.left)-dw)/2,dy=r.top+((r.bottom-r.top)-dh)/2;
        const int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
        BitBlt(dc,dx,dy,dw,dh,mem,0,0,SRCCOPY);RestoreDC(dc,saved);SelectObject(mem,old);DeleteDC(mem);
    }

    static void DrawBitmapContainAlphaNoUpscale(HDC dc,HBITMAP bmp,RECT r,BYTE alpha) {
        if(!bmp || alpha==0) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp);
        const int dw=bm.bmWidth,dh=bm.bmHeight;
        const int dx=r.left+((r.right-r.left)-dw)/2,dy=r.top+((r.bottom-r.top)-dh)/2;
        BLENDFUNCTION bf{AC_SRC_OVER,0,alpha,0};
        const int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
        AlphaBlend(dc,dx,dy,dw,dh,mem,0,0,bm.bmWidth,bm.bmHeight,bf);
        RestoreDC(dc,saved);SelectObject(mem,old);DeleteDC(mem);
    }

    static void DrawBitmapContainFitNoUpscale(HDC dc,HBITMAP bmp,RECT r) {
        if(!bmp) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); SetStretchBltMode(dc,HALFTONE);
        const int rw=std::max(1,static_cast<int>(r.right-r.left)),rh=std::max(1,static_cast<int>(r.bottom-r.top));
        const double scale=std::min(1.0,std::min(static_cast<double>(rw)/bm.bmWidth,static_cast<double>(rh)/bm.bmHeight));
        const int dw=std::max(1,static_cast<int>(std::lround(bm.bmWidth*scale))),dh=std::max(1,static_cast<int>(std::lround(bm.bmHeight*scale)));
        const int dx=r.left+(rw-dw)/2,dy=r.top+(rh-dh)/2;
        StretchBlt(dc,dx,dy,dw,dh,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
        SelectObject(mem,old);DeleteDC(mem);
    }

    static void DrawBitmapContainAlphaFitNoUpscale(HDC dc,HBITMAP bmp,RECT r,BYTE alpha) {
        if(!bmp || alpha==0) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp);
        const int rw=std::max(1,static_cast<int>(r.right-r.left)),rh=std::max(1,static_cast<int>(r.bottom-r.top));
        const double scale=std::min(1.0,std::min(static_cast<double>(rw)/bm.bmWidth,static_cast<double>(rh)/bm.bmHeight));
        const int dw=std::max(1,static_cast<int>(std::lround(bm.bmWidth*scale))),dh=std::max(1,static_cast<int>(std::lround(bm.bmHeight*scale)));
        const int dx=r.left+(rw-dw)/2,dy=r.top+(rh-dh)/2;
        BLENDFUNCTION bf{AC_SRC_OVER,0,alpha,0};
        AlphaBlend(dc,dx,dy,dw,dh,mem,0,0,bm.bmWidth,bm.bmHeight,bf);
        SelectObject(mem,old);DeleteDC(mem);
    }

    static void DrawBitmapContainZoom(HDC dc,HBITMAP bmp,RECT r,float zoom,float centerU,float centerV) {
        if(!bmp) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        const int rw=std::max(1,static_cast<int>(r.right-r.left)),rh=std::max(1,static_cast<int>(r.bottom-r.top));
        const double fit=std::min(static_cast<double>(rw)/bm.bmWidth,static_cast<double>(rh)/bm.bmHeight);
        const double scale=fit*std::clamp(zoom,0.25f,8.0f);
        const int dw=std::max(1,static_cast<int>(std::lround(bm.bmWidth*scale))),dh=std::max(1,static_cast<int>(std::lround(bm.bmHeight*scale)));
        const int cx=r.left+rw/2,cy=r.top+rh/2;
        const int dx=static_cast<int>(std::lround(static_cast<double>(cx)-static_cast<double>(centerU)*dw));
        const int dy=static_cast<int>(std::lround(static_cast<double>(cy)-static_cast<double>(centerV)*dh));
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); SetStretchBltMode(dc,HALFTONE);
        const int saved=SaveDC(dc); IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);
        StretchBlt(dc,dx,dy,dw,dh,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
        RestoreDC(dc,saved); SelectObject(mem,old); DeleteDC(mem);
    }

    void ClearThumbs(std::vector<MediaItem>& list) {
        for(auto& v:list){ if(v.thumb) DeleteObject(v.thumb); if(v.detailThumb) DeleteObject(v.detailThumb); v.thumb=nullptr; v.detailThumb=nullptr; v.libraryGpuThumb.Reset(); v.libraryGpuThumbSource=nullptr; v.libraryGpuGeneration=0; v.detailsGpuThumb.Reset(); v.detailsGpuThumbSource=nullptr; v.detailsGpuGeneration=0; v.thumbW=v.thumbH=0; v.detailThumbW=v.detailThumbH=0; v.thumbLoadRequestEpoch=0; v.thumbNextLoadAttempt=0; }
    }

    void TrimThumbMemory() {
        const uint64_t processBytes=ProcessMemoryBytes();
        const uint64_t targetBytes=LibraryRamBudgetBytes(processBytes);
        struct Ref{MediaItem* item;ULONGLONG tick;uint64_t bytes;};std::vector<Ref> loaded;uint64_t totalBytes=0;
        auto collect=[&](std::vector<MediaItem>& list){for(auto& v:list){if(!v.thumb)continue;const uint64_t bytes=static_cast<uint64_t>(std::max(1,v.thumbW))*static_cast<uint64_t>(std::max(1,v.thumbH))*4ull;loaded.push_back({&v,v.thumbLastUsed,bytes});totalBytes+=bytes;}};
        const bool fullLoadOwnsPressure=LoadEverythingOwnsMemoryPressure() && !SystemMemoryCriticallyLow();
        const bool forceEviction=!fullLoadOwnsPressure && processBytes>=kProcessMemoryAllocationGuard;
        collect(videos_);collect(images_);if(totalBytes<=targetBytes&&!forceEviction)return;
        std::sort(loaded.begin(),loaded.end(),[](const Ref&a,const Ref&b){return a.tick<b.tick;});
        for(const auto& ref:loaded){
            if(totalBytes<=targetBytes&&!forceEviction)break;
            auto* v=ref.item;
            const bool keepPlaybackReturn=playbackLibraryWarmPaths_.find(v->path)!=playbackLibraryWarmPaths_.end();
            const bool keepVisible=visibleLibraryGpuThumbPaths_.find(v->path)!=visibleLibraryGpuThumbPaths_.end();
            const bool keepCurrentViewport=protectedLibraryThumbPaths_.find(v->path)!=protectedLibraryThumbPaths_.end();
            // Visible cards are never sacrificed to satisfy the cache budget. Under
            // emergency pressure, nearby/prefetched cards may be evicted, but the user
            // should never watch the current viewport turn into gray placeholders.
            if(keepPlaybackReturn || keepVisible || (keepCurrentViewport&&!forceEviction)) continue;
            if(v->thumb){DeleteObject(v->thumb);v->thumb=nullptr;v->libraryGpuThumb.Reset();v->libraryGpuThumbSource=nullptr;v->libraryGpuGeneration=0;v->thumbW=v->thumbH=0;v->thumbAttempted=false;v->thumbFromPrivateCache=false;v->thumbLoadRequestEpoch=0;v->thumbNextLoadAttempt=0;totalBytes=ref.bytes>=totalBytes?0:totalBytes-ref.bytes;}
        }
    }

    std::vector<MediaItem>& CurrentItems() { return category_==Category::Videos?videos_:images_; }
    const std::vector<MediaItem>& CurrentItems() const { return category_==Category::Videos?videos_:images_; }

    bool PathEquals(const std::wstring& a, const std::wstring& b) const {
        return ToLower(fs::path(a).lexically_normal().wstring()) == ToLower(fs::path(b).lexically_normal().wstring());
    }

    std::wstring FolderViewKey(const std::wstring& folder) const {
        return ToLower(fs::path(folder).lexically_normal().wstring());
    }

    void SaveCurrentFolderViewState(const std::wstring& selectedPathOverride = L"") {
        if (currentFolder_.empty()) return;
        FolderViewState state;
        state.scrollY = std::max(0, scrollY_);
        state.selectedPath = selectedPathOverride;
        if (state.selectedPath.empty()) {
            const auto& list = CurrentItems();
            if (selected_ < list.size()) {
                const std::wstring parentKey = ToLower(fs::path(list[selected_].path).parent_path().lexically_normal().wstring());
                if (parentKey == FolderViewKey(currentFolder_)) state.selectedPath = list[selected_].path;
            }
        }
        folderViewStates_[FolderViewKey(currentFolder_)] = std::move(state);
    }

    void RestoreFolderViewState(const std::wstring& folder) {
        selected_ = 0;
        scrollY_ = 0;
        const auto it = folderViewStates_.find(FolderViewKey(folder));
        if (it == folderViewStates_.end()) { filterDirty_ = true; ClampScroll(); return; }
        const auto& list = CurrentItems();
        if (!it->second.selectedPath.empty()) {
            const std::wstring selectedKey = ToLower(fs::path(it->second.selectedPath).lexically_normal().wstring());
            for (size_t i = 0; i < list.size(); ++i) {
                if (ToLower(fs::path(list[i].path).lexically_normal().wstring()) == selectedKey) { selected_ = i; break; }
            }
        }
        scrollY_ = std::max(0, it->second.scrollY);
        filterDirty_ = true;
        ClampScroll();
    }

    bool IsAtLibraryRoot() const {
        if(externalMediaSession_) return true;
        return currentFolder_.empty() || PathEquals(currentFolder_, folder_);
    }

    bool IsAtChosenLibraryRoot() const {
        if(externalMediaSession_) return false;
        return !persistentFolder_.empty() && !currentFolder_.empty() && PathEquals(currentFolder_, persistentFolder_);
    }

    size_t CurrentFolderMediaCount() const {
        const auto& list=CurrentItems();
        if(externalMediaSession_) return list.size();
        if(currentFolder_.empty()) return 0;
        const std::wstring currentKey=FolderViewKey(currentFolder_);
        size_t count=0;
        for(const auto& item:list){
            if(FolderViewKey(fs::path(item.path).parent_path().wstring())==currentKey) ++count;
        }
        return count;
    }

    std::vector<size_t> VisibleFolderIndices() const {
        std::vector<size_t> out;
        if(externalMediaSession_) return out;
        // While actively searching, keep the existing media-card layout and show only matching media.
        if (!searchQuery_.empty()) return out;
        const std::wstring currentKey = ToLower(fs::path(currentFolder_).lexically_normal().wstring());
        for (size_t i = 0; i < folders_.size(); ++i) {
            const std::wstring parentKey = ToLower(fs::path(folders_[i].path).parent_path().lexically_normal().wstring());
            if (parentKey == currentKey) out.push_back(i);
        }
        return out;
    }

    struct SearchMediaFilters {
        int minResolutionClass=0;
        bool requireVr=false;
        std::wstring text;
    };

    static SearchMediaFilters ParseSearchMediaFilters(const std::wstring& query) {
        SearchMediaFilters out;
        std::wstring token;
        auto flush=[&](){
            if(token.empty()) return;
            if(token==L"vr") out.requireVr=true;
            else if(token==L"4k") out.minResolutionClass=std::max(out.minResolutionClass,4);
            else if(token==L"5k") out.minResolutionClass=std::max(out.minResolutionClass,5);
            else if(token==L"8k") out.minResolutionClass=std::max(out.minResolutionClass,8);
            else { if(!out.text.empty()) out.text.push_back(L' '); out.text+=token; }
            token.clear();
        };
        for(wchar_t ch:ToLower(query)){
            if(iswspace(ch)) flush(); else token.push_back(ch);
        }
        flush();
        return out;
    }

    void QueuePriorityResolutionMetadataForSearch() {
        if(category_!=Category::Videos || searchQuery_.empty()) return;
        const SearchMediaFilters filters=ParseSearchMediaFilters(searchQuery_);
        if(filters.minResolutionClass==0) return;
        const std::wstring currentKey=ToLower(fs::path(currentFolder_).lexically_normal().wstring());
        for(auto& item:videos_){
            if(item.resolutionProbeAttempted) continue;
            if(!externalMediaSession_ && ToLower(fs::path(item.path).parent_path().lexically_normal().wstring())!=currentKey) continue;
            if(item.resolutionMetadataQueued) continue;
            item.resolutionMetadataQueued=true;
            QueueResolutionMetadata(item.path,item.uiCachePath,true);
        }
    }

    const std::vector<size_t>& FilteredIndices() {
        if(!filterDirty_) return filteredIndices_;
        filteredIndices_.clear();
        const auto& list=CurrentItems();
        const SearchMediaFilters filters=ParseSearchMediaFilters(searchQuery_);
        const std::wstring currentKey=ToLower(fs::path(currentFolder_).lexically_normal().wstring());
        filteredIndices_.reserve(list.size());
        for(size_t i=0;i<list.size();++i){
            const auto& item=list[i];
            const std::wstring parentKey=ToLower(fs::path(item.path).parent_path().lexically_normal().wstring());
            if(!externalMediaSession_ && parentKey!=currentKey) continue;
            if(filters.requireVr && (!item.isVideo || !item.vr.vr)) continue;
            if(filters.minResolutionClass>0){
                if(!item.isVideo || !item.resolutionProbeAttempted) continue;
                if(ResolutionBadgeClass(item)<filters.minResolutionClass) continue;
            }
            if(!filters.text.empty()){
                bool textMatch=true; size_t start=0;
                while(start<filters.text.size()){
                    const size_t end=filters.text.find(L' ',start);
                    const std::wstring word=filters.text.substr(start,end==std::wstring::npos?std::wstring::npos:end-start);
                    if(!word.empty()){
                        const bool normalNameMatch=item.searchText.find(word)!=std::wstring::npos;
                        // A Favorite behaves like an additional searchable media name. This is
                        // intentionally additive: typing "f" still finds ordinary filenames with
                        // an f, while favorited media also match because they carry the virtual
                        // search words "favorite/favorites".
                        static const std::wstring favoriteSearchWords=L"favorite favorites favourite favourites";
                        const bool favoriteNameMatch=item.favorite && favoriteSearchWords.find(word)!=std::wstring::npos;
                        if(!normalNameMatch && !favoriteNameMatch){textMatch=false;break;}
                    }
                    if(end==std::wstring::npos) break;
                    start=end+1;
                }
                if(!textMatch) continue;
            }
            filteredIndices_.push_back(i);
        }
        filterDirty_=false;
        return filteredIndices_;
    }

    bool HoveredLibraryMediaIndex(size_t& mediaIndex) const {
        mediaIndex=static_cast<size_t>(-1);
        if(mode_!=Mode::Library || !hwnd_) return false;
        POINT p{};
        if(!GetCursorPos(&p) || !ScreenToClient(hwnd_,&p)) return false;
        for(const auto& hit:libraryMediaHoverHits_){
            if(PtInRect(&hit.hit,p)){
                mediaIndex=hit.id;
                return true;
            }
        }
        return false;
    }

    RECT StandardBackRect(RECT rc) const {
        return RECT{20, rc.bottom - 51, 100, rc.bottom - 13};
    }

    void PaintFooterBackground(HDC dc, RECT rc) {
        const int footerH = 64;
        RECT footer{0, std::max<LONG>(0, rc.bottom - footerH), rc.right, rc.bottom};
        // One semi-opaque footer surface across the full width.  Hit-testing uses the
        // same full-width rectangle, so media underneath can never receive clicks.
        Gdiplus::Graphics g(dc);
        Gdiplus::SolidBrush fb(Gdiplus::Color(238,16,19,25));
        g.FillRectangle(&fb, footer.left, footer.top, footer.right-footer.left, footer.bottom-footer.top);
        HPEN line = CreatePen(PS_SOLID, 1, RGB(42,47,60)); HGDIOBJ oldPen = SelectObject(dc, line);
        MoveToEx(dc, 0, footer.top, nullptr); LineTo(dc, rc.right, footer.top); SelectObject(dc, oldPen); DeleteObject(line);
    }

    int LibraryMaxScroll(RECT rc) {
        const int clientWidth = std::max(1, static_cast<int>(rc.right - rc.left) - kLibraryScrollbarReserve);
        const int clientHeight = std::max(1, static_cast<int>(rc.bottom - rc.top));
        const int cardW = libraryCardWidth_;
        const int imageH = std::max(113, static_cast<int>(std::lround(static_cast<double>(cardW) * 9.0 / 16.0)));
        const int cardH = imageH + kLibraryTitleHeight;
        const int cols = std::max(1, (clientWidth - kLibraryPad * 2 + kLibraryGap) / (cardW + kLibraryGap));
        const auto& filtered = FilteredIndices();
        const size_t count = filtered.size() + VisibleFolderIndices().size();
        const int rows = static_cast<int>((count + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
        const int total = kLibraryPad + rows * (cardH + kLibraryGap) + 86;
        return std::max(0, total - clientHeight);
    }

    void UpdateLibraryScrollbarRects(RECT rc) {
        libraryScrollTrackRect_ = RECT{};
        libraryScrollThumbRect_ = RECT{};
        if (mode_ != Mode::Library) return;
        const int maxScroll = LibraryMaxScroll(rc);
        if (maxScroll <= 0) return;

        const int top = kLibraryPad;
        const int bottom = std::max(top + 1, static_cast<int>(rc.bottom) - 72);
        libraryScrollTrackRect_ = RECT{std::max<LONG>(0, rc.right - 12), top, std::max<LONG>(0, rc.right - 5), bottom};
        const int trackH = std::max(1, bottom - top);
        const int visibleContentH = std::max(1, static_cast<int>(rc.bottom - rc.top));
        const int contentH = visibleContentH + maxScroll;
        const int thumbH = std::clamp(static_cast<int>((static_cast<long long>(trackH) * visibleContentH) / std::max(1, contentH)), 44, trackH);
        const int travel = std::max(0, trackH - thumbH);
        const int thumbTop = top + (maxScroll > 0 ? static_cast<int>((static_cast<long long>(travel) * scrollY_) / maxScroll) : 0);
        libraryScrollThumbRect_ = RECT{rc.right - 13, thumbTop, rc.right - 4, thumbTop + thumbH};
    }

    void PaintLibraryScrollbar(HDC dc, RECT rc) {
        UpdateLibraryScrollbarRects(rc);
        if (IsRectEmpty(&libraryScrollTrackRect_) || IsRectEmpty(&libraryScrollThumbRect_)) return;
        FillRound(dc, libraryScrollTrackRect_, RGB(24, 28, 36), 5);
        FillRound(dc, libraryScrollThumbRect_, libraryScrollDragging_ ? RGB(145, 152, 166) : RGB(94, 101, 116), 6);
    }

    void PaintLibraryNavigator(HDC dc, RECT rc) {
        PaintFooterBackground(dc, rc);
        const int footerTop = std::max(0, static_cast<int>(rc.bottom) - 64);
        libraryFooterRect_ = RECT{0, footerTop, rc.right, rc.bottom};
        const int buttonTop = footerTop + 13;
        const int buttonBottom = rc.bottom - 13;
        // Match the video-player control geometry exactly: 48 px icons, 20 px right
        // margin and 10 px bottom margin.
        constexpr int iconW=48;
        constexpr int iconGap=10;
        constexpr int groupGap=24;
        constexpr int rightMargin=20;
        const int iconButtonBottom=rc.bottom-10;
        const int iconButtonTop=iconButtonBottom-iconW;

        // Left side: optional folder Back, then one persistent Videos/Images toggle.
        int navLeft = 20;
        if (!IsAtLibraryRoot()) {
            backRect_ = {20, buttonTop, 100, buttonBottom};
            DrawButton(dc, backRect_, L"Back");
            navLeft = 110;
        } else {
            backRect_ = RECT{};
        }
        categoryToggleRect_ = {navLeft, buttonTop, navLeft + 92, buttonBottom};
        DrawTab(dc, categoryToggleRect_, category_==Category::Videos?L"Videos":L"Images", true);
        const std::wstring countText=L"("+std::to_wstring(CurrentFolderMediaCount())+L")";
        mediaCountRect_={categoryToggleRect_.right+10,buttonTop,categoryToggleRect_.right+110,buttonBottom};
        DrawTextSimple(dc,countText,mediaCountRect_,14,FW_SEMIBOLD,RGB(175,181,194),DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        // Right group: identical spacing to video playback.
        int cursor=std::max(0,static_cast<int>(rc.right)-rightMargin);
        libraryFullRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};
        DrawFullscreenButton(dc,libraryFullRect_);
        cursor=libraryFullRect_.left-iconGap;

        // Never offer a slideshow for an empty Images folder.
        const bool showSlideshow=(category_==Category::Images && CurrentFolderMediaCount()>0);
        if(showSlideshow){
            slideshowRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};
            DrawAutoAdvanceIcon(dc,slideshowRect_,slideshowActive_);
            cursor=slideshowRect_.left-iconGap;
        }else{
            slideshowRect_=RECT{};
        }

        // Root-only maintenance group.  Left-to-right: Download, Refresh, Folder,
        // then a deliberate larger gap before slideshow/fullscreen.
        const bool showRootMaintenance=IsAtChosenLibraryRoot();
        const bool showChooseOnly=!showRootMaintenance && (currentFolder_.empty() || externalMediaSession_);
        if(showRootMaintenance){
            cursor-=groupGap-iconGap;
            chooseRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};
            cursor=chooseRect_.left-iconGap;
            rescanRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};
            cursor=rescanRect_.left-iconGap;
            loadEverythingRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};
            DrawFolderIconButton(dc,chooseRect_);
            DrawRefreshIconButton(dc,rescanRect_);
            DrawDownloadIconButton(dc,loadEverythingRect_);
        }else if(showChooseOnly){
            rescanRect_=RECT{};
            loadEverythingRect_=RECT{};
            cursor-=groupGap-iconGap;
            chooseRect_={cursor-iconW,iconButtonTop,cursor,iconButtonBottom};
            DrawFolderIconButton(dc,chooseRect_);
        }else{
            chooseRect_=RECT{};
            rescanRect_=RECT{};
            loadEverythingRect_=RECT{};
        }
    }

    void DrawFolderCard(HDC dc, const LibraryFolder& folder, RECT card) {
        FillRound(dc, card, RGB(31,35,46), 12);
        const int imageH = std::max(113, static_cast<int>(std::lround(static_cast<double>(card.right - card.left) * 9.0 / 16.0)));
        RECT image = card; image.bottom = image.top + imageH;
        HBRUSH bg = CreateSolidBrush(RGB(37,42,54)); FillRect(dc, &image, bg); DeleteObject(bg);

        const int iconW = 118, iconH = 82;
        const int cx = (image.left + image.right) / 2;
        const int cy = (image.top + image.bottom) / 2 + 4;
        RECT body{cx - iconW/2, cy - iconH/2 + 12, cx + iconW/2, cy + iconH/2};
        RECT tab{body.left + 8, body.top - 17, body.left + 54, body.top + 5};
        FillRound(dc, tab, RGB(210,170,73), 7);
        FillRound(dc, body, RGB(225,184,82), 10);

        RECT title{card.left+10,image.bottom+2,card.right-10,card.bottom-3};
        DrawTextSimple(dc,folder.name,title,14,FW_SEMIBOLD);
    }

    void PaintLibrary(HDC dc, RECT rc) {
        libraryMediaHoverHits_.clear();
        libraryReturnHighlightRect_=RECT{};
        visibleLibraryGpuThumbPaths_.clear();
        auto& mutableList=CurrentItems();
        const auto& filtered=FilteredIndices();
        const auto visibleFolders=VisibleFolderIndices();
        const size_t totalCards=visibleFolders.size()+filtered.size();
        RefreshLibraryThumbViewport(rc);
        QueuePriorityResolutionMetadataForSearch();
        protectedLibraryThumbPaths_.clear();
        if(totalCards==0){
            std::wstring msg;
            if(folder_.empty() || currentFolder_.empty()) msg=L"Choose a folder to load videos and images.";
            else if(!searchQuery_.empty()) msg=L"No matching media.";
            else msg=category_==Category::Videos?L"No videos or subfolders here.":L"No images or subfolders here.";
            if(!msg.empty()){
                RECT mr{40,40,rc.right-40,128};
                DrawTextSimple(dc,msg,mr,25,FW_SEMIBOLD,RGB(180,185,197),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            PaintLibraryNavigator(dc,rc);
            if(searchVisible_) PaintLibrarySearch(dc,rc);
            return;
        }

        const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
        const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int cardH=imageH+kLibraryTitleHeight;
        const int rowStride=cardH+gap;
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
        const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap));
        const int rows=static_cast<int>((totalCards+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
        const int startY=pad-scrollY_;
        const int visibleBottom=std::max(pad,static_cast<int>(rc.bottom)-68);

        // Paint only rows that can intersect the viewport. The previous implementation
        // walked every card in the folder on every wheel tick and rejected most of them.
        const int firstVisibleRow=std::clamp(scrollY_/std::max(1,rowStride)-1,0,std::max(0,rows-1));
        const int lastVisibleRow=std::clamp((scrollY_+std::max(0,visibleBottom-pad))/std::max(1,rowStride)+1,
                                            0,std::max(0,rows-1));
        const size_t firstDisplay=static_cast<size_t>(firstVisibleRow)*static_cast<size_t>(cols);
        const size_t lastDisplay=std::min(totalCards,static_cast<size_t>(lastVisibleRow+1)*static_cast<size_t>(cols));

        // Re-evaluate media hover from the real cursor against the NEW card geometry
        // before drawing. Wheel scrolling can move cards underneath a stationary mouse,
        // so WM_MOUSEMOVE alone is not enough to keep the highlight attached correctly.
        POINT libraryCursor{};
        bool libraryCursorValid=GetCursorPos(&libraryCursor)!=FALSE && ScreenToClient(hwnd_,&libraryCursor)!=FALSE;
        bool libraryHoverFound=false;
        size_t libraryHoverId=static_cast<size_t>(-1);
        RECT libraryHoverRect{};
        if(libraryCursorValid){
            RECT mediaViewport{0,pad,rc.right,visibleBottom};
            for(size_t displayIndex=firstDisplay;displayIndex<lastDisplay;++displayIndex){
                if(displayIndex<visibleFolders.size()) continue;
                const int col=static_cast<int>(displayIndex)%cols,row=static_cast<int>(displayIndex)/cols;
                RECT card{pad+col*(cardW+gap),startY+row*rowStride,pad+col*(cardW+gap)+cardW,startY+row*rowStride+cardH};
                if(card.bottom<pad||card.top>visibleBottom) continue;
                RECT mediaHit{};
                if(!IntersectRect(&mediaHit,&card,&mediaViewport) || !PtInRect(&mediaHit,libraryCursor)) continue;
                const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
                if(mediaDisplayIndex>=filtered.size()) continue;
                libraryHoverFound=true;
                libraryHoverId=filtered[mediaDisplayIndex];
                libraryHoverRect=card;
                break;
            }
        }
        SetMediaHoverTarget(MediaHoverSurface::Library,libraryHoverId,libraryHoverRect,libraryHoverFound);

        HDC libraryImageDc=CreateCompatibleDC(dc);
        SetStretchBltMode(dc,HALFTONE);

        for(size_t displayIndex=firstDisplay;displayIndex<lastDisplay;++displayIndex){
            const int col=static_cast<int>(displayIndex)%cols,row=static_cast<int>(displayIndex)/cols;
            RECT card{pad+col*(cardW+gap),startY+row*rowStride,pad+col*(cardW+gap)+cardW,startY+row*rowStride+cardH};
            if(card.bottom<pad||card.top>visibleBottom) continue;

            if(displayIndex<visibleFolders.size()) {
                if(RectVisible(dc,&card)) DrawFolderCard(dc, folders_[visibleFolders[displayIndex]], card);
                continue;
            }

            const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
            const size_t i=filtered[mediaDisplayIndex];
            RECT mediaViewport{0,pad,rc.right,visibleBottom};
            RECT mediaHit{};
            if(IntersectRect(&mediaHit,&card,&mediaViewport)) libraryMediaHoverHits_.push_back({mediaHit,card,i});
            // Visibility protection and return-highlight geometry are state, not paint work;
            // keep them current even when this card lies outside the current dirty region.
            protectedLibraryThumbPaths_.insert(mutableList[i].path);
            visibleLibraryGpuThumbPaths_.insert(mutableList[i].path);
            const float returnAmount=LibraryReturnHighlightAmount(i);
            if(returnAmount>0.0f) libraryReturnHighlightRect_=card;
            if(!RectVisible(dc,&card)) continue;

            FillRound(dc,card,RGB(31,35,46),12);
            RECT image=card; image.bottom=image.top+imageH;
            HBITMAP bmp=GetLibraryItemThumb(mutableList[i],i,640,360,true);
            if(bmp){ if(libraryImageDc) DrawBitmapCoverWithSourceDC(dc,libraryImageDc,bmp,image); else DrawBitmapCover(dc,bmp,image); }
            else { HBRUSH pb=CreateSolidBrush(RGB(43,48,61)); FillRect(dc,&image,pb); DeleteObject(pb); }
            RECT title{card.left+10,image.bottom+2,card.right-10,card.bottom-3}; DrawTextSimple(dc,mutableList[i].title,title,14,FW_SEMIBOLD);
            if(mutableList[i].favorite) DrawFavoriteBadge(dc,image);
            if(mutableList[i].isVideo){
                int badgeRight=card.right-8;
                if(mutableList[i].vr.vr){
                    RECT vrTag{badgeRight-28,card.top+8,badgeRight,card.top+36};
                    if(vrBadgeWhiteBitmap_) DrawBitmapCentered(dc,vrTag,vrBadgeWhiteBitmap_.get(),0,0);
                    else { FillRound(dc,vrTag,RGB(16,19,25),8); DrawTextSimple(dc,L"VR",vrTag,11,FW_BOLD,RGB(220,225,235),DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
                    badgeRight=vrTag.left-6;
                }
                const int resolutionClass=ResolutionBadgeClass(mutableList[i]);
                if(resolutionClass){
                    RECT resolutionTag{badgeRight-28,card.top+8,badgeRight,card.top+36};
                    if(Gdiplus::Bitmap* resolutionIcon=ResolutionBadgeBitmap(mutableList[i])) DrawBitmapCentered(dc,resolutionTag,resolutionIcon,0,0);
                    else { FillRound(dc,resolutionTag,RGB(16,19,25),8); DrawTextSimple(dc,std::to_wstring(resolutionClass)+L"K",resolutionTag,10,FW_BOLD,RGB(230,234,242),DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
                }
            }
            DrawMediaHoverBorder(dc,card,std::max(MediaHoverAmount(MediaHoverSurface::Library,i,card),returnAmount),12);
        }
        if(libraryImageDc) DeleteDC(libraryImageDc);

        // Warm a deep window around the viewport. Visible requests always outrank these
        // prefetch requests, and stale queued prefetch is discarded as the viewport moves.
        // This keeps fast scrollbar jumps responsive without giving up nearby RAM warmth.
        auto queueRows=[&](int firstRow,int lastRow){
            firstRow=std::max(0,firstRow); lastRow=std::min(rows-1,lastRow);
            if(firstRow>lastRow) return;
            for(int row=firstRow;row<=lastRow;++row){
                const size_t rowFirst=static_cast<size_t>(row)*static_cast<size_t>(cols);
                const size_t rowLast=std::min(totalCards,rowFirst+static_cast<size_t>(cols));
                for(size_t displayIndex=rowFirst;displayIndex<rowLast;++displayIndex){
                    if(displayIndex<visibleFolders.size()) continue;
                    const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
                    if(mediaDisplayIndex>=filtered.size()) continue;
                    const size_t i=filtered[mediaDisplayIndex];
                    protectedLibraryThumbPaths_.insert(mutableList[i].path);
                    GetLibraryItemThumb(mutableList[i],i,640,360);
                }
            }
        };
        constexpr int kPrefetchRows=10;
        if(libraryThumbPrefetchDirection_>=0){
            queueRows(lastVisibleRow+1,lastVisibleRow+kPrefetchRows);
            queueRows(firstVisibleRow-kPrefetchRows,firstVisibleRow-1);
        }else{
            queueRows(firstVisibleRow-kPrefetchRows,firstVisibleRow-1);
            queueRows(lastVisibleRow+1,lastVisibleRow+kPrefetchRows);
        }

        // Keep the top spacing fixed while the library content scrolls underneath it.
        // This is a permanent 20px mask matching the left/right library padding; it is
        // not a header and contains no text or controls.
        RECT topMask{0,0,rc.right,kLibraryPad};
        HBRUSH topMaskBrush=CreateSolidBrush(RGB(13,15,20));
        FillRect(dc,&topMask,topMaskBrush);
        DeleteObject(topMaskBrush);

        PaintLibraryScrollbar(dc,rc);
        PaintLibraryNavigator(dc,rc);
        if(searchVisible_) PaintLibrarySearch(dc,rc);
        playbackLibraryWarmPaths_.clear();
        TrimThumbMemory();
    }

    void PaintLibrarySearch(HDC dc, RECT rc) {
        const int right=std::max(20,static_cast<int>(rc.right)-20);
        const int width=std::min(430,std::max(220,static_cast<int>(rc.right)/3));
        searchBoxRect_={right-width,12,right,52};
        FillRound(dc,searchBoxRect_,RGB(31,35,46),11);
        RECT text{searchBoxRect_.left+14,searchBoxRect_.top,searchBoxRect_.right-14,searchBoxRect_.bottom};
        const std::wstring shown=searchQuery_.empty()?L"Search...":searchQuery_;
        if(searchSelectAll_ && !searchQuery_.empty()) {
            HFONT f=GetFont(16,FW_SEMIBOLD); HGDIOBJ oldFont=SelectObject(dc,f);
            SIZE sz{}; GetTextExtentPoint32W(dc,searchQuery_.c_str(),static_cast<int>(searchQuery_.size()),&sz);
            SelectObject(dc,oldFont);
            RECT selected{text.left-3,text.top+8,std::min<LONG>(text.right,text.left+sz.cx+5),text.bottom-8};
            FillRound(dc,selected,RGB(72,82,105),5);
        }
        DrawTextSimple(dc,shown,text,16,FW_SEMIBOLD,searchQuery_.empty()?RGB(145,151,164):RGB(244,246,250));
    }

    void PaintDetails(HDC dc, RECT rc) {
        previewHitRects_.clear();
        previewMediaHoverHits_.clear();
        previewZoomRect_ = RECT{0,0,0,0};
        detailsMediaRect_ = RECT{};
        auto& list=CurrentItems(); if(selected_>=list.size()) return; MediaItem& item=list[selected_];
        ClampDetailsScroll();
        const int footerTop=std::max(0,static_cast<int>(rc.bottom)-64);
        const int contentOffset=detailsScrollY_;
        int y=18-contentOffset;
        RECT title{40,y,rc.right-40,y+42}; DrawTextSimple(dc,item.title,title,30,FW_BOLD); y+=54;

        const int heroH=item.isVideo?480:std::max(260,footerTop-150);
        RECT media{40,y,rc.right-40,y+heroH};
        if(!item.isVideo && nativeImageSizing_) media=RECT{0,0,rc.right,rc.bottom};
        detailsMediaRect_=media;
        if(media.bottom>0 && media.top<footerTop){
            HBRUSH b=CreateSolidBrush(RGB(20,23,31)); FillRect(dc,&media,b); DeleteObject(b);
            const int reqW=std::min(2560,std::max(1,static_cast<int>(media.right-media.left)));
            const int reqH=std::min(1440,std::max(1,static_cast<int>(media.bottom-media.top)));
            HBITMAP bmp=nullptr;
            if(item.isVideo && !PathExistsNoThrow(item.cachePath)) bmp=GetItemThumb(item,640,360);
            else bmp=GetDetailsBanner(item);
            if(bmp){
                if(item.isVideo) {
                    // Info-screen video banner: enlarge proportionally to fill the hero
                    // area. Aspect ratio is preserved; excess edges are cropped rather than stretched.
                    DrawBitmapCover(dc,bmp,media);
                } else if(slideshowFadeActive_ && slideshowPreviousIndex_ < images_.size()) {
                    HBITMAP previous=nativeImageSizing_ ? GetDetailsBanner(images_[slideshowPreviousIndex_]) : GetItemThumb(images_[slideshowPreviousIndex_],reqW,reqH);
                    const float progress=EaseUi(static_cast<float>(GetTickCount64()-slideshowFadeStart_) / static_cast<float>(kUiAnimationDurationMs));
                    if(previous){
                        if(nativeImageSizing_ && fullscreen_) DrawBitmapContainFitNoUpscale(dc,previous,media);
                        else if(nativeImageSizing_) DrawBitmapContainNoUpscale(dc,previous,media);
                        else DrawBitmapContain(dc,previous,media);
                    }
                    if(nativeImageSizing_ && fullscreen_) DrawBitmapContainAlphaFitNoUpscale(dc,bmp,media,static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(progress*255.0f)),0,255)));
                    else if(nativeImageSizing_) DrawBitmapContainAlphaNoUpscale(dc,bmp,media,static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(progress*255.0f)),0,255)));
                    else DrawBitmapContainAlpha(dc,bmp,media,static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(progress*255.0f)),0,255)));
                } else {
                    // Still images keep their original aspect ratio in the Info view.
                    // Do not use the video-banner cover/crop behavior here.
                    if(nativeImageSizing_ && fullscreen_) DrawBitmapContainFitNoUpscale(dc,bmp,media);
                    else if(nativeImageSizing_) DrawBitmapContainNoUpscale(dc,bmp,media);
                    else if(ImageZoomActive()) DrawBitmapContainZoom(dc,bmp,media,imageZoomScale_,imageZoomCenterU_,imageZoomCenterV_);
                    else DrawBitmapContain(dc,bmp,media);
                }
                if(item.isVideo){
                    int badgeRight=media.right-10;
                    const int badgeTop=media.top+10;
                    const int badgeSize=30;
                    if(item.vr.vr){
                        RECT vrTag{badgeRight-badgeSize,badgeTop,badgeRight,badgeTop+badgeSize};
                        if(vrBadgeWhiteBitmap_) DrawBitmapCentered(dc,vrTag,vrBadgeWhiteBitmap_.get(),0,0);
                        else { FillRound(dc,vrTag,RGB(16,19,25),8); DrawTextSimple(dc,L"VR",vrTag,11,FW_BOLD,RGB(220,225,235),DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
                        badgeRight=vrTag.left-6;
                    }
                    const int resolutionClass=ResolutionBadgeClass(item);
                    if(resolutionClass){
                        RECT resolutionTag{badgeRight-badgeSize,badgeTop,badgeRight,badgeTop+badgeSize};
                        if(Gdiplus::Bitmap* resolutionIcon=ResolutionBadgeBitmap(item)) DrawBitmapCentered(dc,resolutionTag,resolutionIcon,0,0);
                        else { FillRound(dc,resolutionTag,RGB(16,19,25),8); DrawTextSimple(dc,std::to_wstring(resolutionClass)+L"K",resolutionTag,10,FW_BOLD,RGB(230,234,242),DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
                    }
                }
            }
            if(item.favorite) DrawFavoriteBadge(dc,media);
        }
        y+=heroH+22;

        if(item.isVideo){
            // The whole visible secondary-preview section is a zoom target, including the
            // loading/empty state and the gaps between cards. This avoids wheel zoom becoming
            // unavailable while previews are still arriving from the background worker.
            const int zoomTop = std::max(0, y);
            if (zoomTop < footerTop && rc.right > 80)
                previewZoomRect_ = RECT{40, zoomTop, rc.right-40, footerTop};

            const int gap=12;
            const int cardW=DetailsPreviewCardWidthForViewport(static_cast<int>(rc.right - rc.left));
            const int imageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
            const int labelH=24;
            const int cardH=imageH+labelH;
            const int availW=std::max(1,static_cast<int>(rc.right)-80);
            const int cols=std::max(1,(availW+gap)/(cardW+gap));

            if(previewFrames_.empty()){
                SetMediaHoverTarget(MediaHoverSurface::Preview,static_cast<size_t>(-1),RECT{},false);
                RECT note{40,y,rc.right-40,y+54};
                const bool complete=!previewDir_.empty()&&PreviewCacheIsComplete();
                DrawTextSimple(dc,complete?L"No secondary previews were available for this video.":L"Loading Timeline",note,14,FW_NORMAL,RGB(160,167,180));
                y+=64;
            } else {
                const int rows=static_cast<int>((previewFrames_.size()+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
                const int rowStride=cardH+gap;
                const int startRow=std::max(0, ((0 - y) / std::max(1,rowStride)) - 1);
                const int endRow=std::min(rows-1, ((footerTop - y) / std::max(1,rowStride)) + 1);

                POINT previewCursor{};
                const bool previewCursorValid=GetCursorPos(&previewCursor)!=FALSE && ScreenToClient(hwnd_,&previewCursor)!=FALSE;
                bool previewHoverFound=false;
                size_t previewHoverId=static_cast<size_t>(-1);
                RECT previewHoverRect{};
                if(previewCursorValid){
                    RECT previewViewport{0,0,rc.right,footerTop};
                    for(int row=startRow;row<=endRow && !previewHoverFound;++row){
                        for(int col=0;col<cols;++col){
                            const size_t i=static_cast<size_t>(row)*static_cast<size_t>(cols)+static_cast<size_t>(col);
                            if(i>=previewFrames_.size()) break;
                            RECT card{40+col*(cardW+gap),y+row*(cardH+gap),40+col*(cardW+gap)+cardW,y+row*(cardH+gap)+cardH};
                            if(card.bottom<0||card.top>footerTop) continue;
                            RECT previewHit{};
                            if(IntersectRect(&previewHit,&card,&previewViewport) && PtInRect(&previewHit,previewCursor)){
                                previewHoverFound=true;
                                previewHoverId=i;
                                previewHoverRect=card;
                                break;
                            }
                        }
                    }
                }
                SetMediaHoverTarget(MediaHoverSurface::Preview,previewHoverId,previewHoverRect,previewHoverFound);

                for(int row=startRow; row<=endRow; ++row){
                    for(int col=0; col<cols; ++col){
                        const size_t i=static_cast<size_t>(row)*static_cast<size_t>(cols)+static_cast<size_t>(col);
                        if(i>=previewFrames_.size()) break;
                        RECT card{40+col*(cardW+gap),y+row*(cardH+gap),40+col*(cardW+gap)+cardW,y+row*(cardH+gap)+cardH};
                        if(card.bottom<0||card.top>footerTop) continue;
                        // Keep preview input strictly inside the scrollable content area.
                        // A card can be partially visible behind the fixed footer, but that hidden
                        // portion must never remain clickable through the footer controls.
                        RECT previewHit{};
                        RECT previewViewport{0,0,rc.right,footerTop};
                        if (IntersectRect(&previewHit, &card, &previewViewport)) {
                            previewHitRects_.push_back({previewHit, previewFrames_[i].seconds});
                            previewMediaHoverHits_.push_back({previewHit,card,i});
                        }
                        if(!RectVisible(dc,&card)) continue;
                        FillRound(dc,card,RGB(28,32,42),9);
                        RECT image=card; image.bottom=image.top+imageH;
                        HBITMAP pbmp=GetPreviewBitmap(previewFrames_[i]);
                        if(pbmp) DrawBitmapCover(dc,pbmp,image);
                        else { HBRUSH ph=CreateSolidBrush(RGB(43,48,61)); FillRect(dc,&image,ph); DeleteObject(ph); }
                        RECT label{card.left+8,image.bottom,card.right-8,card.bottom};
                        DrawTextSimple(dc,PreviewLabel(previewFrames_[i].seconds),label,11,FW_SEMIBOLD,RGB(200,206,218),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                        DrawMediaHoverBorder(dc,card,MediaHoverAmount(MediaHoverSurface::Preview,i,card),9);
                    }
                }
                y+=rows*(cardH+gap)+10;
                TrimPreviewMemory();
            }
        }

        detailsContentBottom_=y+20+contentOffset;

        // Previous / next media controls stay fixed at the screen edges while the
        // open-media view scrolls underneath them. Navigation is restricted to the
        // same media type and exact current folder.
        constexpr int edgeW=48, edgeH=76, edgePad=16;
        const int edgeCenterY=footerTop/2;
        const int edgeTop=std::max(8,edgeCenterY-edgeH/2);
        detailsPrevRect_={edgePad,edgeTop,edgePad+edgeW,edgeTop+edgeH};
        detailsNextRect_={std::max(edgePad,static_cast<int>(rc.right)-edgePad-edgeW),edgeTop,std::max(edgePad+edgeW,static_cast<int>(rc.right)-edgePad),edgeTop+edgeH};
        DrawEdgeArrowButton(dc,detailsPrevRect_,false,CanNavigateDetailsMedia(-1));
        DrawEdgeArrowButton(dc,detailsNextRect_,true,CanNavigateDetailsMedia(1));

        // Info footer is a fixed top interaction layer.  It may visually sit over
        // scrolling preview cards, but nothing underneath it is allowed to receive clicks.
        detailsFooterRect_ = RECT{0, footerTop, rc.right, rc.bottom};
        PaintFooterBackground(dc, rc);
        backRect_=StandardBackRect(rc); DrawButton(dc,backRect_,L"Back");
        if(item.isVideo){
            imageDetailsSlideshowRect_=RECT{};
            const int gap=10, playW=145;
            playRect_={backRect_.right+gap,backRect_.top,backRect_.right+gap+playW,backRect_.bottom};
            DrawButton(dc,playRect_,L"Play",true);
        } else {
            playRect_=RECT{};
        }

        constexpr int footerIconW=48, footerGap=10, footerRightMargin=20;
        const int footerRight=std::max(0,static_cast<int>(rc.right)-footerRightMargin);
        const int detailsIconBottom=rc.bottom-10;
        const int detailsIconTop=detailsIconBottom-footerIconW;
        detailsFullRect_={footerRight-footerIconW,detailsIconTop,footerRight,detailsIconBottom};
        DrawFullscreenButton(dc,detailsFullRect_);
        imageDetailsNativeRect_=RECT{};
        if(!item.isVideo){
            imageDetailsNativeRect_={detailsFullRect_.left-footerGap-footerIconW,detailsIconTop,detailsFullRect_.left-footerGap,detailsIconBottom};
            DrawNativeSizeButton(dc,imageDetailsNativeRect_);
            imageDetailsSlideshowRect_={imageDetailsNativeRect_.left-footerGap-footerIconW,detailsIconTop,imageDetailsNativeRect_.left-footerGap,detailsIconBottom};
            DrawAutoAdvanceIcon(dc,imageDetailsSlideshowRect_,slideshowActive_);
        }
        std::wstring meta=item.isVideo?(item.vr.vr?(item.vr.projection==2?L"VR180":L"VR"):L"Video"):L"Image";
        if(item.isVideo && item.sourceWidth && item.sourceHeight){
            meta += L"  \u2022  ";
            meta += std::to_wstring(item.sourceWidth);
            meta += L"\u00D7";
            meta += std::to_wstring(item.sourceHeight);
        }
        const LONG metaLeftLimit=item.isVideo?playRect_.right+20:backRect_.right+20;
        const LONG metaRightLimit=(item.isVideo?detailsFullRect_.left:imageDetailsSlideshowRect_.left)-20;
        const LONG metaLeft=std::max<LONG>(metaLeftLimit,rc.right/2-150);
        const LONG metaRight=std::max<LONG>(metaLeft+40,std::min<LONG>(metaRightLimit,rc.right/2+150));
        RECT metaTop{metaLeft,rc.bottom-62,metaRight,rc.bottom-43};
        DrawTextSimple(dc,meta,metaTop,13,FW_SEMIBOLD,RGB(165,172,185),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.isVideo){
            const double duration=detailsDurationSeconds_.load(std::memory_order_relaxed);
            RECT durationRect{metaTop.left,rc.bottom-43,metaTop.right,rc.bottom-8};
            DrawTextSimple(dc,duration>0.0?FormatTime(duration):L"--:--",durationRect,22,FW_SEMIBOLD,RGB(205,210,220),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }

    void ResetPreviewZoom() {
        previewCardWidth_ = kDefaultPreviewCardWidth;
        previewWheelRemainder_ = 0;
        previewZoomOverridden_ = false;
        preFullscreenPreviewCardWidth_ = -1;
        preFullscreenPreviewZoomOverridden_ = false;
        preFullscreenPreviewStateValid_ = false;
    }

    void ResetLibraryZoom() {
        libraryCardWidth_ = kDefaultLibraryCardWidth;
    }

    bool CenterLibraryOnMedia(size_t mediaIndex) {
        if(mode_!=Mode::Library) return false;
        RECT rc{}; GetClientRect(hwnd_,&rc);
        const auto visibleFolders=VisibleFolderIndices();
        const auto& filtered=FilteredIndices();
        const auto found=std::find(filtered.begin(),filtered.end(),mediaIndex);
        if(found==filtered.end()) return false;

        const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
        const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int cardH=imageH+kLibraryTitleHeight;
        const int rowStride=cardH+gap;
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
        const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap));
        const size_t mediaPosition=static_cast<size_t>(std::distance(filtered.begin(),found));
        const size_t displayIndex=visibleFolders.size()+mediaPosition;
        const int row=static_cast<int>(displayIndex/static_cast<size_t>(cols));
        const int logicalCenter=pad+row*rowStride+cardH/2;
        const int visibleBottom=std::max(pad,static_cast<int>(rc.bottom)-68);
        const int viewportCenter=(pad+visibleBottom)/2;
        scrollY_=logicalCenter-viewportCenter;
        ClampScroll();
        return true;
    }

    void StartLibraryReturnHighlight(size_t mediaIndex) {
        libraryReturnHighlightCategory_=category_;
        libraryReturnHighlightIndex_=mediaIndex;
        libraryReturnHighlightStart_=GetTickCount64();
        libraryReturnHighlightRect_=RECT{};
        StartUiAnimationTimer();
    }

    void ReturnFromDetailsToLibrary() {
        if(mode_!=Mode::Details) return;
        KillTimer(hwnd_,kResumeDetailsWorkersTimerId);
        if(player_) player_->CloseSource();
        const Category returnCategory=category_;
        const size_t returnIndex=selected_;
        std::wstring returnPath;
        const auto& returnList=CurrentItems();
        if(returnIndex<returnList.size()) returnPath=returnList[returnIndex].path;

        StopImageSlideshow();
        ResetImageZoom();
        // Leaving the image viewer ends its Native Size session. Restore the exact
        // pre-native window geometry (or make fullscreen restore to it later).
        if(nativeImageSizing_ || nativeImageSizingRestoreRectValid_){
            nativeImageSizing_=false;
            if(fullscreen_){
                SetFullscreenRestoreToStandardWindow();
            }else{
                RestoreStandardWindowSize();
            }
            nativeImageSizingRestoreRectValid_=false;
        }
        StopPreviewWorker();
        ClearAllDetailInfoMemory();
        ResetPreviewZoom();
        if(hoverOwner_==hwnd_ || hoverPreviousOwner_==hwnd_){
            hoverOwner_=nullptr; hoverPreviousOwner_=nullptr; hoverRect_=RECT{}; hoverPreviousRect_=RECT{}; hoverTransitionStart_=0;
        }
        mode_=Mode::Library;
        detailsScrollY_=0;
        if(fullscreen_){
            RECT libraryRc{}; GetClientRect(hwnd_,&libraryRc);
            ApplyLibraryWidthForViewport(std::max(1,static_cast<int>(libraryRc.right-libraryRc.left)));
        }

        std::error_code navEc;
        if(!detailsOriginFolder_.empty() && fs::exists(detailsOriginFolder_,navEc) && !navEc)
            currentFolder_=fs::path(detailsOriginFolder_).lexically_normal().wstring();
        else if(!folder_.empty() && fs::exists(folder_,navEc) && !navEc)
            currentFolder_=fs::path(folder_).lexically_normal().wstring();

        if(searchQuery_.empty()) searchVisible_=false;
        filteredIndices_.clear();
        filterDirty_=true;
        RestoreFolderViewState(currentFolder_);

        // The media that was actually open wins over the old Library scroll state.
        // This matters after using the Info-screen left/right arrows.
        category_=returnCategory;
        selected_=returnIndex;
        filteredIndices_.clear();
        filterDirty_=true;
        const bool centered=CenterLibraryOnMedia(returnIndex);
        if(centered){
            if(!returnPath.empty()) SaveCurrentFolderViewState(returnPath);
            StartLibraryReturnHighlight(returnIndex);
        }

        detailsSearchNavigationActive_=false;
        detailsSearchNavigationIndices_.clear();
        PrimeVisibleLibraryThumbsFromPrivateCache();
        StartThumbnailWorker();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    std::vector<size_t> ImageIndicesInCurrentFolder() const {
        std::vector<size_t> out;
        if(externalMediaSession_){
            out.reserve(images_.size());
            for(size_t i=0;i<images_.size();++i) out.push_back(i);
            return out;
        }
        const std::wstring currentKey = ToLower(fs::path(currentFolder_).lexically_normal().wstring());
        for (size_t i = 0; i < images_.size(); ++i) {
            const std::wstring parentKey = ToLower(fs::path(images_[i].path).parent_path().lexically_normal().wstring());
            if (parentKey == currentKey) out.push_back(i);
        }
        return out;
    }

    void StopImageSlideshow() {
        const bool wasActive = slideshowActive_;
        if (slideshowActive_) KillTimer(hwnd_, kSlideshowTimerId);
        slideshowActive_ = false;
        slideshowIndices_.clear();
        slideshowPos_ = 0;
        slideshowFadeActive_ = false;
        slideshowPreviousIndex_ = static_cast<size_t>(-1);
        if (wasActive && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void StartImageSlideshow() {
        StopImageSlideshow();
        ResetImageZoom();
        if (category_ != Category::Images) return;
        slideshowIndices_ = ImageIndicesInCurrentFolder();
        if (slideshowIndices_.empty()) return;
        SaveCurrentFolderViewState();
        slideshowActive_ = true;
        slideshowPos_ = 0;
        detailsOriginFolder_ = currentFolder_;
        selected_ = slideshowIndices_.front();
        detailsScrollY_ = 0;
        ResetPreviewZoom();
        ResetLibraryZoom();
        mode_ = Mode::Details;
        if(nativeImageSizing_ && !fullscreen_) ApplyNativeImageWindowSize();
        SetTimer(hwnd_, kSlideshowTimerId, 3000, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void StartImageSlideshowFromSelected() {
        StopImageSlideshow();
        ResetImageZoom();
        if (category_ != Category::Images || selected_ >= images_.size()) return;
        slideshowIndices_ = ImageIndicesInCurrentFolder();
        if (slideshowIndices_.empty()) return;

        const auto it = std::find(slideshowIndices_.begin(), slideshowIndices_.end(), selected_);
        if (it == slideshowIndices_.end()) return;

        slideshowActive_ = true;
        slideshowPos_ = static_cast<size_t>(std::distance(slideshowIndices_.begin(), it));
        detailsOriginFolder_ = currentFolder_;
        detailsScrollY_ = 0;
        mode_ = Mode::Details;
        if(slideshowIndices_.size()>1 && slideshowPos_+1>=slideshowIndices_.size()){
            slideshowPreviousIndex_=selected_;
            slideshowPos_=0;
            selected_=slideshowIndices_.front();
            ResetImageZoom();
            slideshowFadeStart_=GetTickCount64();
            slideshowFadeActive_=true;
            StartUiAnimationTimer();
        }
        if(nativeImageSizing_ && !fullscreen_) ApplyNativeImageWindowSize();
        SetTimer(hwnd_, kSlideshowTimerId, 3000, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void AdvanceImageSlideshow() {
        if (!slideshowActive_ || mode_ != Mode::Details || category_ != Category::Images || slideshowIndices_.empty()) {
            StopImageSlideshow();
            return;
        }
        if (slideshowPos_ + 1 >= slideshowIndices_.size()) {
            StopImageSlideshow();
            return;
        }
        slideshowPreviousIndex_ = selected_;
        ++slideshowPos_;
        selected_ = slideshowIndices_[slideshowPos_];
        ResetImageZoom();
        if(nativeImageSizing_ && !fullscreen_) ApplyNativeImageWindowSize();
        slideshowFadeStart_ = GetTickCount64();
        slideshowFadeActive_ = true;
        StartUiAnimationTimer();
        detailsScrollY_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void HandleClick(int x,int y) {
        POINT p{x,y};
        if(mode_==Mode::Library){
            if(!IsAtLibraryRoot() && PtInRect(&backRect_,p)){
                StopImageSlideshow();

                // Search belongs to the folder where it was started. Leaving that
                // folder cancels it completely so the parent folder always opens in
                // its normal unfiltered state.
                searchQuery_.clear();
                searchVisible_=false;
                filteredIndices_.clear();
                filterDirty_=true;

                SaveCurrentFolderViewState();
                fs::path parent=fs::path(currentFolder_).parent_path();
                currentFolder_=parent.empty()?folder_:parent.lexically_normal().wstring();
                RestoreFolderViewState(currentFolder_); InvalidateRect(hwnd_,nullptr,FALSE); return;
            }
            if(PtInRect(&categoryToggleRect_,p)){
                StopImageSlideshow();
                category_=category_==Category::Videos?Category::Images:Category::Videos;
                selected_=0; scrollY_=0; filterDirty_=true;
                SaveSettings(); ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); return;
            }
            if(category_==Category::Images && PtInRect(&slideshowRect_,p)){ StartImageSlideshow(); return; }
            if(PtInRect(&libraryFullRect_,p)){ ToggleFullscreen(); return; }
            if(IsAtChosenLibraryRoot() && PtInRect(&loadEverythingRect_,p)){ StopImageSlideshow(); StartFullLoadEverything(); return; }
            if((IsAtChosenLibraryRoot() || currentFolder_.empty() || externalMediaSession_) && PtInRect(&chooseRect_,p)){ StopImageSlideshow(); ChooseFolder(); return; }
            if(IsAtChosenLibraryRoot() && PtInRect(&rescanRect_,p)){ StopImageSlideshow(); Scan(true); return; }
            // The entire fixed footer is an input barrier, including empty/semi-transparent areas.
            if(PtInRect(&libraryFooterRect_,p)) return;
            RECT rc{}; GetClientRect(hwnd_,&rc);
            const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
            const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
            const int cardH=imageH+kLibraryTitleHeight; const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
            // The fixed top padding is a visual/input mask. Cards may scroll underneath it,
            // but they cannot be clicked through the masked area.
            if(y<pad) return;
            const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap)); const int ly=y-pad+scrollY_; if(ly<0) return;
            const int row=ly/(cardH+gap),col=(x-pad)/(cardW+gap); if(col<0||col>=cols) return;
            const int localX=(x-pad)%(cardW+gap),localY=ly%(cardH+gap); if(localX<0||localX>=cardW||localY<0||localY>=cardH) return;
            const size_t displayIndex=static_cast<size_t>(row)*static_cast<size_t>(cols)+static_cast<size_t>(col);
            const auto visibleFolders=VisibleFolderIndices();
            if(displayIndex<visibleFolders.size()){
                StopImageSlideshow();
                SaveCurrentFolderViewState();
                currentFolder_=folders_[visibleFolders[displayIndex]].path;
                RestoreFolderViewState(currentFolder_); InvalidateRect(hwnd_,nullptr,FALSE); return;
            }
            const auto& filtered=FilteredIndices();
            const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
            if(mediaDisplayIndex<filtered.size()){
                StopImageSlideshow(); thumbStop_.store(true,std::memory_order_release); ClearLoadingStateIf(1); ResetPreviewZoom();
                detailsOriginFolder_=currentFolder_;
                detailsSearchNavigationActive_ = searchVisible_ && !searchQuery_.empty();
                if(detailsSearchNavigationActive_) detailsSearchNavigationIndices_ = filtered;
                else detailsSearchNavigationIndices_.clear();
                selected_=filtered[mediaDisplayIndex];
                const auto& list=CurrentItems();
                if(selected_<list.size()) SaveCurrentFolderViewState(list[selected_].path);
                ResetLibraryZoom();
                if(category_==Category::Images) ResetImageZoom();
                mode_=Mode::Details; detailsScrollY_=0;
                if(category_==Category::Videos) StartPreviewWorkerForSelected(); else ClearLoadingState();
                QueueDetailPrefetchWindow();
                InvalidateRect(hwnd_,nullptr,FALSE);
            }
        } else if(mode_==Mode::Details){
            if(PtInRect(&detailsPrevRect_,p) && CanNavigateDetailsMedia(-1)){ NavigateDetailsMedia(-1); return; }
            if(PtInRect(&detailsNextRect_,p) && CanNavigateDetailsMedia(1)){ NavigateDetailsMedia(1); return; }

            // Fixed footer controls always win hit-testing over scrollable content.
            if(PtInRect(&backRect_,p)){
                ReturnFromDetailsToLibrary();
                return;
            }
            if(category_==Category::Videos&&PtInRect(&playRect_,p)){ EnterPlayerAt(0.0); return; }
            if(category_==Category::Images&&PtInRect(&imageDetailsSlideshowRect_,p)){
                if(slideshowActive_) StopImageSlideshow();
                else StartImageSlideshowFromSelected();
                return;
            }
            if(category_==Category::Images&&PtInRect(&imageDetailsNativeRect_,p)){ ToggleNativeImageSizing(); return; }
            if(PtInRect(&detailsFullRect_,p)){ ToggleFullscreen(); return; }

            // Treat the entire footer as an input barrier, including its transparent/empty
            // areas, so a preview card underneath can never receive a click through it.
            if(PtInRect(&detailsFooterRect_,p)) return;

            if(category_==Category::Videos){
                for(const auto& hit : previewHitRects_){
                    RECT r = hit.first;
                    if(PtInRect(&r,p)){ EnterPlayerAt(static_cast<double>(hit.second)); return; }
                }
            }
        }
    }

    static bool PathExistsNoThrow(const fs::path& path) {
        std::error_code ec;
        return fs::exists(path,ec) && !ec;
    }

    void ArmLibraryAccessMonitor(ULONGLONG milliseconds=5000) {
        const ULONGLONG until=GetTickCount64()+milliseconds;
        if(until>libraryAccessMonitorUntil_) libraryAccessMonitorUntil_=until;
    }

    bool IsLibraryRootAccessible() const {
        if(folder_.empty()) return false;
        const DWORD attrs=GetFileAttributesW(folder_.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_DIRECTORY)==0) return false;
        HANDLE h=CreateFileW(folder_.c_str(),FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                             nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(h==INVALID_HANDLE_VALUE) return false;
        CloseHandle(h);
        return true;
    }

    static wchar_t DriveLetterFromPath(const std::wstring& path) {
        if(path.size()<2 || path[1]!=L':' || !iswalpha(path[0])) return 0;
        return static_cast<wchar_t>(towupper(path[0]));
    }

    bool IsLibraryVolumePresent() const {
        // For a normal drive-letter library (for example X:\Media), monitor only
        // the Windows drive mount state. GetLogicalDrives does not open the selected
        // folder or any media file, so this once-per-second health check does not keep
        // a VeraCrypt volume busy with a long-lived filesystem handle.
        const std::wstring& path = externalMediaSession_ ? folder_ : (!persistentFolder_.empty() ? persistentFolder_ : folder_);
        const wchar_t drive = DriveLetterFromPath(path);
        if(drive){
            const DWORD mask=GetLogicalDrives();
            if(mask==0) return false;
            const unsigned bit=static_cast<unsigned>(drive-L'A');
            return bit<26u && (mask&(1u<<bit))!=0;
        }

        // UNC paths and unusual mount-point paths have no drive-letter bit to query.
        // Fall back to the existing short-lived root check for those cases.
        return IsLibraryRootAccessible();
    }

    bool LibraryIoWorkActive() {
        if(GetTickCount64()<libraryAccessMonitorUntil_) return true;
        if(thumbWorkerRunning_.load(std::memory_order_acquire) ||
           fullLoadRunning_.load(std::memory_order_acquire) ||
           loadingKind_.load(std::memory_order_acquire)!=0) return true;
        {
            std::lock_guard<std::mutex> lock(libraryThumbLoadMutex_);
            if(!libraryThumbLoadJobs_.empty()) return true;
        }
        {
            std::lock_guard<std::mutex> lock(resolutionMetadataMutex_);
            if(!resolutionMetadataJobs_.empty()) return true;
        }
        {
            std::lock_guard<std::mutex> lock(detailPrefetchMutex_);
            if(!detailPrefetchJobs_.empty()) return true;
        }
        return false;
    }

    void UnloadUnavailableLibrarySession() {
        if(libraryUnavailableLatched_) return;
        libraryUnavailableLatched_=true;
        libraryAccessFailCount_=3;
        libraryAccessRetryNeedsRescan_=false;

        StopImageSlideshow();
        autoNext_=false;
        nativeVideoSizing_=false;
        nativeImageSizing_=false;
        if(player_) player_->ResetFlatZoom();
        ResetImageZoom();
        DestroyPlayerFooterTransition();
        KillTimer(hwnd_,kResumeDetailsWorkersTimerId);

        if(player_) player_->Pause();
        if(videoHwnd_) ShowWindow(videoHwnd_,SW_HIDE);
        if(controlsHwnd_) ShowWindow(controlsHwnd_,SW_HIDE);
        if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
        if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
        playerControlsVisible_=false; controlsFading_=false; controlsAlpha_=0;
        player_.reset();

        StopPreviewWorker();
        CancelDetailPrefetchJobs();
        StopFullLoadWorker();
        StopThumbnailWorker();
        ResetLibraryThumbLoadView();
        ResetResolutionMetadataWork();
        ClearAllDetailInfoMemory();
        ClearThumbs(videos_); ClearThumbs(images_);
        videos_.clear(); images_.clear(); folders_.clear();
        filteredIndices_.clear(); filterDirty_=true;
        detailsSearchNavigationIndices_.clear(); detailsSearchNavigationActive_=false;
        folderViewStates_.clear();
        currentFolder_.clear(); detailsOriginFolder_.clear();
        searchQuery_.clear(); searchVisible_=false; searchSelectAll_=false;
        selected_=0; scrollY_=0; detailsScrollY_=0;
        ResetPreviewZoom(); ResetLibraryZoom();
        libraryMediaHoverHits_.clear(); previewMediaHoverHits_.clear();
        libraryReturnHighlightIndex_=static_cast<size_t>(-1); libraryReturnHighlightStart_=0; libraryReturnHighlightRect_=RECT{};
        mode_=Mode::Library;
        // Losing the backing drive must not resize or recenter the main window. The
        // unavailable notice is purely an in-app overlay; preserve the user's geometry.
        SetWindowTextW(hwnd_,L"Visual MediaPlayer");
        ApplyMainWindowCornerPreference();
        Layout();
        ShowInAppNotice(L"This folder is unavailable.",5000);
        InvalidateRect(hwnd_,nullptr,TRUE);
    }

    void NoteLibraryAccessFailure(bool retryNeedsRescan) {
        if(libraryUnavailableLatched_) return;
        libraryAccessRetryNeedsRescan_=libraryAccessRetryNeedsRescan_ || retryNeedsRescan;
        libraryAccessFailCount_=std::min(3,libraryAccessFailCount_+1);
        if(libraryAccessFailCount_>=3) UnloadUnavailableLibrarySession();
    }

    void CheckLibraryAccessHealth() {
        if(folder_.empty()){
            libraryAccessFailCount_=0; libraryUnavailableLatched_=false; libraryAccessRetryNeedsRescan_=false;
            return;
        }

        // Once the library is latched unavailable, never run the unload/notice path
        // again. That used to restart the notice every second and made its pulse appear
        // endless. Wait only for the drive to return, then verify the saved folder once.
        if(libraryUnavailableLatched_){
            if(!IsLibraryVolumePresent()) return;
            if(IsLibraryRootAccessible()){
                libraryAccessFailCount_=0;
                libraryUnavailableLatched_=false;
                libraryAccessRetryNeedsRescan_=false;
                ClearInAppNotice();
                if(mode_==Mode::Library){
                    if(externalMediaSession_ && !externalMediaPaths_.empty()){
                        const auto reopenPaths=externalMediaPaths_;
                        OpenExternalMediaBatch(reopenPaths);
                    }else Scan(false);
                }
            }
            return;
        }

        // While healthy, check only whether the backing drive/volume is still mounted.
        // For drive-letter libraries this uses GetLogicalDrives, so the once-per-second
        // health monitor does not touch the selected folder or any media file.
        if(!IsLibraryVolumePresent()){
            NoteLibraryAccessFailure(false);
            return;
        }

        // A short drive disappearance recovered before reaching the three-failure threshold.
        // Reset the counter without probing the library folder.
        if(libraryAccessFailCount_>0){
            libraryAccessFailCount_=0;
            libraryAccessRetryNeedsRescan_=false;
        }
    }

    void ChooseFolder() {
        StopImageSlideshow();
        ComPtr<IFileDialog> dlg; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dlg)))) return;
        DWORD opts=0; dlg->GetOptions(&opts); dlg->SetOptions(opts|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
        if(!folder_.empty()&&IsLibraryRootAccessible()){
            ComPtr<IShellItem> start; if(SUCCEEDED(SHCreateItemFromParsingName(folder_.c_str(),nullptr,IID_PPV_ARGS(&start)))) dlg->SetFolder(start.Get());
        }
        if(SUCCEEDED(dlg->Show(hwnd_))){
            ComPtr<IShellItem> item; if(SUCCEEDED(dlg->GetResult(&item))){
                PWSTR p=nullptr; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){ folder_=p; persistentFolder_=folder_; CoTaskMemFree(p); RememberLibraryRoot(folder_); SaveSettings(); Scan(); }
            }
        }
    }

    void Scan(bool cleanupOrphanCache=false) {
        StopImageSlideshow();
        externalMediaSession_=false;
        externalMediaPaths_.clear();
        if(!folder_.empty() && !IsLibraryRootAccessible()){
            NoteLibraryAccessFailure(true);
            InvalidateRect(hwnd_,nullptr,TRUE);
            return;
        }
        libraryAccessFailCount_=0; libraryUnavailableLatched_=false; libraryAccessRetryNeedsRescan_=false;
        ArmLibraryAccessMonitor(10000);
        StopFullLoadWorker();
        const std::wstring previousFolder=currentFolder_;
        if(mode_==Mode::Library && !previousFolder.empty()) SaveCurrentFolderViewState();
        StopPreviewWorker();
        ClearAllDetailInfoMemory();
        StopThumbnailWorker();
        ResetLibraryThumbLoadView();
        ResetResolutionMetadataWork();
        ClearThumbs(videos_); ClearThumbs(images_);
        videos_.clear(); images_.clear(); folders_.clear(); detailsOriginFolder_.clear();
        selected_=0; scrollY_=0; detailsScrollY_=0; ResetPreviewZoom(); ResetLibraryZoom();
        libraryMediaHoverHits_.clear(); previewMediaHoverHits_.clear();
        libraryReturnHighlightIndex_=static_cast<size_t>(-1); libraryReturnHighlightStart_=0; libraryReturnHighlightRect_=RECT{};
        if(folder_.empty()){ currentFolder_.clear(); InvalidateRect(hwnd_,nullptr,TRUE); return; }
        folder_=fs::path(folder_).lexically_normal().wstring();
        std::error_code previousEc;
        if(!previousFolder.empty() && PathIsWithin(previousFolder,folder_) && fs::exists(previousFolder,previousEc) && !previousEc)
            currentFolder_=fs::path(previousFolder).lexically_normal().wstring();
        else
            currentFolder_=folder_;

        bool scanReliable=true;
        std::vector<fs::path> discoveredCacheRoots;
        std::error_code ec;
        fs::recursive_directory_iterator it(folder_,fs::directory_options::skip_permission_denied,ec),end;
        if(ec) scanReliable=false;
        for(;it!=end;){
            if(ec){scanReliable=false;ec.clear();}
            const auto pth=it->path();
            std::error_code entryEc;
            const bool isDir=it->is_directory(entryEc);
            if(entryEc){
                scanReliable=false;
            } else if(isDir){
                if(ToLower(pth.filename().wstring())==L".visualmediaplayer-cache"){
                    // Remember only an already-existing cache. Never enter or create it.
                    discoveredCacheRoots.push_back(pth.lexically_normal());
                    it.disable_recursion_pending();
                } else {
                    LibraryFolder folderItem; folderItem.path=pth.lexically_normal().wstring(); folderItem.name=pth.filename().wstring();
                    folders_.push_back(std::move(folderItem));
                }
            } else {
                entryEc.clear();
                const bool isFile=it->is_regular_file(entryEc);
                if(entryEc){
                    scanReliable=false;
                } else if(isFile){
                    const std::wstring ext=pth.extension().wstring();
                    const bool video=IsVideoExtension(ext), image=IsImageExtension(ext);
                    if(video||image){
                        MediaItem item; item.path=pth.lexically_normal().wstring(); item.title=pth.stem().wstring(); std::replace(item.title.begin(),item.title.end(),L'_',L' ');
                        item.isVideo=video; if(video) item.vr=DetectVR(item.path); else item.title=StripLeadingImageResolutionPrefix(std::move(item.title)); item.cachePath=BuildCachePath(item.path); item.uiCachePath=BuildUiCachePath(item.path);
                        item.favorite=ReadFavoriteMetadata(item.path);
                        item.searchText=ToLower(item.title+L"\n"+item.path);
                        if(video) videos_.push_back(std::move(item)); else images_.push_back(std::move(item));
                    }
                }
            }
            it.increment(ec);
            if(ec){scanReliable=false;ec.clear();}
        }

        auto mediaSorter=[](const MediaItem& a,const MediaItem& b){
            const MediaNameSortKey ka=BuildMediaNameSortKey(a.title);
            const MediaNameSortKey kb=BuildMediaNameSortKey(b.title);
            if(ka.primary!=kb.primary) return ka.primary<kb.primary;
            if(ka.group!=kb.group) return ka.group<kb.group;
            if(ka.secondary!=kb.secondary) return ka.secondary<kb.secondary;
            if(ka.hasNumber!=kb.hasNumber) return !ka.hasNumber;
            if(ka.hasNumber){
                const int numberCmp=CompareSortNumbers(ka.number,kb.number);
                if(numberCmp!=0) return numberCmp<0;
            }
            if(ka.fallback!=kb.fallback) return ka.fallback<kb.fallback;
            return ToLower(a.path)<ToLower(b.path);
        };
        auto folderSorter=[](const LibraryFolder&a,const LibraryFolder&b){return ToLower(a.name)<ToLower(b.name);};
        std::sort(videos_.begin(),videos_.end(),mediaSorter); std::sort(images_.begin(),images_.end(),mediaSorter); std::sort(folders_.begin(),folders_.end(),folderSorter);

        // Explicit Rescan is the only operation allowed to remove orphaned cache.
        // Any filesystem uncertainty makes cleanup fail closed.
        if(cleanupOrphanCache && scanReliable) CleanupOrphanMediaCache(discoveredCacheRoots);

        if(!scanReliable && !IsLibraryRootAccessible()){
            NoteLibraryAccessFailure(true);
            filterDirty_=true;
            InvalidateRect(hwnd_,nullptr,TRUE);
            return;
        }

        filterDirty_=true; RestoreFolderViewState(currentFolder_); InvalidateRect(hwnd_,nullptr,FALSE); StartThumbnailWorker();
    }

    void ClampScroll() {
        RECT rc{}; GetClientRect(hwnd_,&rc);
        const int maxScroll = LibraryMaxScroll(rc);
        scrollY_ = std::clamp(scrollY_, 0, maxScroll);
        UpdateLibraryScrollbarRects(rc);
    }

    void ClampDetailsScroll() {
        if(mode_!=Mode::Details){detailsScrollY_=0;return;}
        RECT rc{}; GetClientRect(hwnd_,&rc);
        if(category_!=Category::Videos){detailsScrollY_=0;return;}
        const int footerTop=std::max(0,static_cast<int>(rc.bottom)-64);
        const int gap=12;
        const int cardW=DetailsPreviewCardWidthForViewport(static_cast<int>(rc.right - rc.left));
        const int imageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int cardH=imageH+24;
        const int availW=std::max(1,static_cast<int>(rc.right)-80);
        const int cols=std::max(1,(availW+gap)/(cardW+gap));
        const int rows=previewFrames_.empty()?0:static_cast<int>((previewFrames_.size()+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
        const int previewsHeight=previewFrames_.empty()?64:rows*(cardH+gap)+10;
        const int contentBottom=18+54+480+22+38+previewsHeight+20;
        const int maxScroll=std::max(0,contentBottom-footerTop+16);
        detailsScrollY_=std::clamp(detailsScrollY_,0,maxScroll);
    }

    std::wstring SettingsPath() const {
        wchar_t local[MAX_PATH]{}; if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local))) return L"VisualMediaPlayer.ini";
        fs::path dir=fs::path(local)/L"VisualMediaPlayer"; std::error_code ec; fs::create_directories(dir,ec); return (dir/L"settings.ini").wstring();
    }

    void RememberLibraryRoot(const std::wstring& rawRoot) {
        if(rawRoot.empty()) return;
        fs::path rootPath=fs::path(rawRoot).lexically_normal();
        const std::wstring key=ToLower(rootPath.wstring());
        if(key.empty()) return;
        for(const auto& existing:knownLibraryRoots_){
            if(ToLower(fs::path(existing).lexically_normal().wstring())==key) return;
        }
        knownLibraryRoots_.push_back(rootPath.wstring());
    }

    void LoadSettings() {
        const std::wstring settings=SettingsPath();
        wchar_t buf[32768]{};
        GetPrivateProfileStringW(L"Library",L"Folder",L"",buf,static_cast<DWORD>(std::size(buf)),settings.c_str());
        folder_=buf; persistentFolder_=folder_;

        wchar_t categoryBuf[32]{};
        GetPrivateProfileStringW(L"View",L"Category",L"Videos",categoryBuf,static_cast<DWORD>(std::size(categoryBuf)),settings.c_str());
        category_=ToLower(categoryBuf)==L"images"?Category::Images:Category::Videos;

        std::vector<wchar_t> section(65536, L'\0');
        const DWORD chars=GetPrivateProfileSectionW(L"CacheRoots",section.data(),static_cast<DWORD>(section.size()),settings.c_str());
        if(chars>0 && static_cast<size_t>(chars)<section.size()-2){
            const wchar_t* p=section.data();
            while(*p){
                std::wstring entry=p;
                const size_t eq=entry.find(L'=');
                if(eq!=std::wstring::npos && eq+1<entry.size()) RememberLibraryRoot(entry.substr(eq+1));
                p+=entry.size()+1;
            }
        }
        RememberLibraryRoot(persistentFolder_);
    }

    void SaveSettings() const {
        const std::wstring settings=SettingsPath();
        WritePrivateProfileStringW(L"Library",L"Folder",persistentFolder_.c_str(),settings.c_str());
        WritePrivateProfileStringW(L"View",L"Category",category_==Category::Images?L"Images":L"Videos",settings.c_str());
        WritePrivateProfileStringW(L"CacheRoots",nullptr,nullptr,settings.c_str());
        for(size_t i=0;i<knownLibraryRoots_.size();++i){
            wchar_t key[32]{}; swprintf_s(key,L"Root%zu",i);
            WritePrivateProfileStringW(L"CacheRoots",key,knownLibraryRoots_[i].c_str(),settings.c_str());
        }
    }

    bool CreatePlayerControls() {
        if(videoHwnd_&&controlsHwnd_&&player_) return true;
        if(!videoHwnd_){
            videoHwnd_=CreateWindowExW(0,L"VisualMediaPlayerVideo",nullptr,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,100,100,hwnd_,nullptr,inst_,this);
            if(!videoHwnd_){MessageBoxW(hwnd_,L"Could not create the Direct3D video surface.",L"Visual MediaPlayer",MB_ICONERROR);return false;}
        }
        if(!controlsHwnd_){
            // A layered CHILD window is unreliable for this overlay.  Use an owned popup instead:
            // it can alpha-blend over the D3D child surface without changing the video viewport.
            const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            const DWORD style = WS_POPUP | WS_CLIPSIBLINGS;
            controlsHwnd_=CreateWindowExW(exStyle,L"VisualMediaPlayerControls",nullptr,style,0,0,100,100,hwnd_,nullptr,inst_,this);
            if(!controlsHwnd_){
                const DWORD err=GetLastError();
                const std::wstring msg=L"Could not create the player control overlay.\n\nWindows error: "+std::to_wstring(err);
                MessageBoxW(hwnd_,msg.c_str(),L"Visual MediaPlayer",MB_ICONERROR);
                return false;
            }
            controlsAlpha_ = 0;
            if(!SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA)){
                const DWORD err=GetLastError();
                DestroyWindow(controlsHwnd_); controlsHwnd_=nullptr;
                const std::wstring msg=L"Could not enable the semi-transparent player overlay.\n\nWindows error: "+std::to_wstring(err);
                MessageBoxW(hwnd_,msg.c_str(),L"Visual MediaPlayer",MB_ICONERROR);
                return false;
            }
        }
        if(!playerPrevHwnd_ || !playerNextHwnd_){
            const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
            const DWORD style = WS_POPUP | WS_CLIPSIBLINGS;
            if(!playerPrevHwnd_) playerPrevHwnd_=CreateWindowExW(exStyle,L"VisualMediaPlayerEdgeArrow",nullptr,style,0,0,48,76,hwnd_,nullptr,inst_,this);
            if(!playerNextHwnd_) playerNextHwnd_=CreateWindowExW(exStyle,L"VisualMediaPlayerEdgeArrow",nullptr,style,0,0,48,76,hwnd_,nullptr,inst_,this);
            if(!playerPrevHwnd_ || !playerNextHwnd_){
                MessageBoxW(hwnd_,L"Could not create the previous / next media controls.",L"Visual MediaPlayer",MB_ICONERROR);
                return false;
            }
            SetLayeredWindowAttributes(playerPrevHwnd_,0,controlsAlpha_,LWA_ALPHA);
            SetLayeredWindowAttributes(playerNextHwnd_,0,controlsAlpha_,LWA_ALPHA);
        }
        player_=std::make_unique<NativePlayer>(); const HRESULT hr=player_->Initialize(hwnd_,videoHwnd_);
        if(FAILED(hr)){MessageBoxW(hwnd_,(L"Could not initialize Direct3D 11 / Media Foundation.\n\n"+HrText(hr)).c_str(),L"Visual MediaPlayer",MB_ICONERROR);player_.reset();return false;}
        player_->SetVolume(volumeFraction_); Layout(); return true;
    }

    void DestroyPlayerFooterTransition() {
        if(playerFooterTransitionHwnd_){ DestroyWindow(playerFooterTransitionHwnd_); playerFooterTransitionHwnd_=nullptr; }
        if(playerFooterTransitionBitmap_){ DeleteObject(playerFooterTransitionBitmap_); playerFooterTransitionBitmap_=nullptr; }
        playerFooterTransitionStart_=0;
    }

    void StartPlayerFooterTransitionSnapshot(bool captureScreen=false) {
        DestroyPlayerFooterTransition();
        if(!hwnd_) return;
        RECT client{}; GetClientRect(hwnd_,&client);
        const int width=std::max(1,static_cast<int>(client.right-client.left));
        const int height=std::min(64,std::max(1,static_cast<int>(client.bottom-client.top)));
        const int top=std::max(0,static_cast<int>(client.bottom)-height);
        POINT screen{0,top}; ClientToScreen(hwnd_,&screen);
        HDC src=captureScreen?GetDC(nullptr):GetDC(hwnd_);
        if(!src) return;
        HDC mem=CreateCompatibleDC(src);
        HBITMAP bmp=CreateCompatibleBitmap(src,width,height);
        if(!mem || !bmp){ if(bmp) DeleteObject(bmp); if(mem) DeleteDC(mem); ReleaseDC(captureScreen?nullptr:hwnd_,src); return; }
        HGDIOBJ old=SelectObject(mem,bmp);
        const int srcX=captureScreen?screen.x:0;
        const int srcY=captureScreen?screen.y:top;
        const DWORD rop=captureScreen?(SRCCOPY|CAPTUREBLT):SRCCOPY;
        const BOOL copied=BitBlt(mem,0,0,width,height,src,srcX,srcY,rop);
        SelectObject(mem,old); DeleteDC(mem); ReleaseDC(captureScreen?nullptr:hwnd_,src);
        if(!copied){DeleteObject(bmp);return;}

        const DWORD exStyle=WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT;
        const DWORD style=WS_POPUP|SS_BITMAP;
        HWND overlay=CreateWindowExW(exStyle,L"STATIC",nullptr,style,screen.x,screen.y,width,height,hwnd_,nullptr,inst_,nullptr);
        if(!overlay){DeleteObject(bmp);return;}
        SendMessageW(overlay,STM_SETIMAGE,IMAGE_BITMAP,reinterpret_cast<LPARAM>(bmp));
        SetLayeredWindowAttributes(overlay,0,255,LWA_ALPHA);
        if(!fullscreen_){
            constexpr int cornerDiameter=20;
            HRGN rounded=CreateRoundRectRgn(0,0,width+1,height+1,cornerDiameter,cornerDiameter);
            HRGN squareTop=CreateRectRgn(0,0,width,std::min(height,cornerDiameter));
            if(rounded&&squareTop){
                CombineRgn(rounded,rounded,squareTop,RGN_OR);
                if(SetWindowRgn(overlay,rounded,FALSE)!=0) rounded=nullptr;
            }
            if(rounded) DeleteObject(rounded);
            if(squareTop) DeleteObject(squareTop);
        }
        playerFooterTransitionHwnd_=overlay;
        playerFooterTransitionBitmap_=bmp;
        playerFooterTransitionStart_=GetTickCount64();
        ShowWindow(overlay,SW_SHOWNOACTIVATE);
        StartUiAnimationTimer();
    }

    void UpdatePlayerFooterTransition(ULONGLONG now, bool& active) {
        if(!playerFooterTransitionHwnd_ || playerFooterTransitionStart_==0) return;
        const ULONGLONG elapsed=now-playerFooterTransitionStart_;
        if(elapsed>=kPlayerFooterTransitionDurationMs){ DestroyPlayerFooterTransition(); return; }
        active=true;
        const float raw=static_cast<float>(elapsed)/static_cast<float>(kPlayerFooterTransitionDurationMs);
        const float smooth=raw*raw*(3.0f-2.0f*raw);
        const BYTE alpha=static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(255.0f*(1.0f-smooth))),0,255));
        SetLayeredWindowAttributes(playerFooterTransitionHwnd_,0,alpha,LWA_ALPHA);
    }

    void RequestBackgroundStopForPlayback() {
        // Playback has absolute priority, but entering the player must never block the UI
        // waiting for a thumbnail/preview worker to join. Signal cancellation immediately;
        // the threads are joined later when Details loading is restarted or at shutdown.
        previewStop_.store(true, std::memory_order_release);
        thumbStop_.store(true, std::memory_order_release);
        ClearLoadingState();
    }

    void EnterPlayerAt(double startSeconds) {
        if(category_!=Category::Videos||selected_>=videos_.size()) return;
        // Every newly opened video starts at the standard 30% volume.
        // Volume changes are intentionally limited to the current playback session.
        volumeFraction_ = 0.30;
        RequestBackgroundStopForPlayback();
        ReleaseDetailsGpuWorkingSet();
        StartPlayerFooterTransitionSnapshot();
        mode_=Mode::Player;
        TrimForPlayback();
        if(!CreatePlayerControls()){DestroyPlayerFooterTransition();mode_=Mode::Details;StartPreviewWorkerForSelected();QueueDetailPrefetchWindow();InvalidateRect(hwnd_,nullptr,TRUE);return;}
        ApplyMainWindowCornerPreference();
        ShowWindow(videoHwnd_,SW_SHOW); playerControlsVisible_=false; controlsHideDeadline_=0; lastCursorValid_=false; controlsFading_=false; controlsAlpha_=0;
        if(controlsHwnd_) SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
        if(playerPrevHwnd_) SetLayeredWindowAttributes(playerPrevHwnd_,0,controlsAlpha_,LWA_ALPHA);
        if(playerNextHwnd_) SetLayeredWindowAttributes(playerNextHwnd_,0,controlsAlpha_,LWA_ALPHA);
        Layout();
        const std::wstring openingPath=videos_[selected_].path;
        const HRESULT hr=player_->Open(openingPath,videos_[selected_].vr,std::max(0.0,startSeconds));
        if(FAILED(hr)){
            if(!PathExistsNoThrow(openingPath) || !IsLibraryRootAccessible()){
                LeavePlayer();
                NoteLibraryAccessFailure(true);
                ArmLibraryAccessMonitor(3000);
            }else{
                LeavePlayer();
                ShowInAppNotice(L"This media is unsupported.",5000);
            }
            return;
        }
        player_->SetVolume(volumeFraction_); SetFocus(videoHwnd_); UpdateWindowTitle();
        // Cross-fade the new player controls under the captured Info footer instead
        // of snapping from one bottom menu to the other.
        PlayerActivity(true);
        if(playerFooterTransitionHwnd_) SetWindowPos(playerFooterTransitionHwnd_,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }

    void LeavePlayer() {
        // Reverse the opening transition: keep the player controls at exactly their
        // current geometry and fade them out over the Info footer. No screenshot/capture
        // is needed on close, which also removes the short capture/join hitch.
        DestroyPlayerFooterTransition();
        if(player_) player_->Pause();

        // Native Size stays latched while moving through videos (including intervening
        // VR videos), but leaving Player ends that session and returns to normal sizing.
        if(nativeVideoSizing_ || nativeSizingRestoreRectValid_){
            if(player_) player_->SetNativePixelSizing(false);
            nativeVideoSizing_=false;
            if(fullscreen_){
                // Leaving Player keeps fullscreen active. If fullscreen is later disabled,
                // restore the fixed standard app window rather than a user/native resize.
                SetFullscreenRestoreToStandardWindow();
            }else{
                RestoreStandardWindowSize();
            }
            nativeSizingRestoreRectValid_=false;
        }
        // Auto Next is a playback-session control. Back/Escape always starts the next
        // Player session with it off.
        autoNext_=false;
        // The source is shut down just after the reverse footer fade. Deferring the
        // Media Foundation shutdown keeps the close animation smooth while still
        // releasing the file handle within a fraction of a second.
        volumeFraction_=0.30;
        controlsHideDeadline_=0;
        const bool fadePlayerControls=playerControlsVisible_ && controlsHwnd_ && IsWindowVisible(controlsHwnd_);
        if(videoHwnd_) ShowWindow(videoHwnd_,SW_HIDE);
        mode_=Mode::Details;
        SetWindowTextW(hwnd_,L"Visual MediaPlayer");
        ApplyMainWindowCornerPreference();
        InvalidateRect(hwnd_,nullptr,TRUE);
        UpdateWindow(hwnd_);
        if(fadePlayerControls) BeginControlsFade(0);
        else {
            playerControlsVisible_=false; controlsFading_=false; controlsAlpha_=0;
            if(controlsHwnd_) ShowWindow(controlsHwnd_,SW_HIDE);
            if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
            if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
        }
        // Existing Info bitmaps remain available immediately. Give cancelled background
        // workers time to exit before reaping/restarting them, outside the visual transition.
        SetTimer(hwnd_,kResumeDetailsWorkersTimerId,320,nullptr);
    }

    void BeginControlsFade(BYTE target) {
        if(!controlsHwnd_) return;
        UpdateControlsFade();
        controlsFadeFrom_=controlsAlpha_;
        controlsFadeTo_=target;
        controlsFadeStart_=GetTickCount64();
        controlsFading_=controlsFadeFrom_!=controlsFadeTo_;
        if(controlsFading_) StartUiAnimationTimer();
        if(target>0){
            if(!IsWindowVisible(controlsHwnd_)) ShowWindow(controlsHwnd_,SW_SHOWNOACTIVATE);
            if(playerPrevHwnd_ && !IsWindowVisible(playerPrevHwnd_)) ShowWindow(playerPrevHwnd_,SW_SHOWNOACTIVATE);
            if(playerNextHwnd_ && !IsWindowVisible(playerNextHwnd_)) ShowWindow(playerNextHwnd_,SW_SHOWNOACTIVATE);
        }
        if(!controlsFading_ && target==0){
            playerControlsVisible_=false; ShowWindow(controlsHwnd_,SW_HIDE);
            if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
            if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
        }
    }

    void UpdateControlsFade() {
        if(!controlsFading_ || !controlsHwnd_) return;
        const ULONGLONG now=GetTickCount64();
        const float raw=static_cast<float>(now-controlsFadeStart_) / static_cast<float>(kPlayerControlsFadeDurationMs);
        const float t=EaseUi(raw);
        const int value=static_cast<int>(std::lround(controlsFadeFrom_ + (static_cast<int>(controlsFadeTo_)-static_cast<int>(controlsFadeFrom_))*t));
        controlsAlpha_=static_cast<BYTE>(std::clamp(value,0,255));
        SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
        if(playerPrevHwnd_) SetLayeredWindowAttributes(playerPrevHwnd_,0,controlsAlpha_,LWA_ALPHA);
        if(playerNextHwnd_) SetLayeredWindowAttributes(playerNextHwnd_,0,controlsAlpha_,LWA_ALPHA);
        if(raw>=1.0f){
            controlsAlpha_=controlsFadeTo_; controlsFading_=false;
            SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
            if(playerPrevHwnd_) SetLayeredWindowAttributes(playerPrevHwnd_,0,controlsAlpha_,LWA_ALPHA);
            if(playerNextHwnd_) SetLayeredWindowAttributes(playerNextHwnd_,0,controlsAlpha_,LWA_ALPHA);
            if(controlsAlpha_==0){
                playerControlsVisible_=false; ShowWindow(controlsHwnd_,SW_HIDE);
                if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
                if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
            }
        }
    }

    void PlayerActivity(bool force) {
        if(mode_!=Mode::Player) return;
        POINT pt{}; GetCursorPos(&pt);
        if(!force&&lastCursorValid_&&pt.x==lastCursorScreen_.x&&pt.y==lastCursorScreen_.y) return;
        lastCursorScreen_=pt; lastCursorValid_=true; controlsHideDeadline_=GetTickCount64()+2200;
        if(!playerControlsVisible_){
            playerControlsVisible_=true; controlsAlpha_=0; controlsFading_=false;
            if(controlsHwnd_) SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
            if(playerPrevHwnd_) SetLayeredWindowAttributes(playerPrevHwnd_,0,controlsAlpha_,LWA_ALPHA);
            if(playerNextHwnd_) SetLayeredWindowAttributes(playerNextHwnd_,0,controlsAlpha_,LWA_ALPHA);
            Layout();
            if(controlsHwnd_) ShowWindow(controlsHwnd_,SW_SHOWNOACTIVATE);
            if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_SHOWNOACTIVATE);
            if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_SHOWNOACTIVATE);
            BeginControlsFade(kControlsVisibleAlpha); InvalidateControls();
        } else if(controlsFading_ && controlsFadeTo_==0) {
            BeginControlsFade(kControlsVisibleAlpha);
        }
    }

    void UpdatePlayerControlVisibility() {
        UpdateControlsFade();
        if(mode_!=Mode::Player||!playerControlsVisible_||seekDragging_||volumeDragging_) return;
        if(controlsHideDeadline_&&GetTickCount64()>=controlsHideDeadline_){
            controlsHideDeadline_=0;
            if(!controlsFading_ || controlsFadeTo_!=0) BeginControlsFade(0);
        }
    }

    void InvalidateControls() {
        if(controlsHwnd_&&playerControlsVisible_) InvalidateRect(controlsHwnd_,nullptr,FALSE);
        if(playerPrevHwnd_&&playerControlsVisible_) InvalidateRect(playerPrevHwnd_,nullptr,FALSE);
        if(playerNextHwnd_&&playerControlsVisible_) InvalidateRect(playerNextHwnd_,nullptr,FALSE);
    }

    bool FindAnimatedButtonRect(HWND owner, POINT p, RECT& out) const {
        auto hit=[&](const RECT& r)->bool{ if(!EmptyRectValue(r) && PtInRect(&r,p)){ out=r; return true; } return false; };
        if(owner==controlsHwnd_){
            if(hit(playerBackRect_)) return true;
            if(player_ && player_->VR().vr && hit(playerVrToggleRect_)) return true;
            if(hit(playerSkipBackRect_)) return true;
            if(hit(playerPlayRect_)) return true;
            if(hit(playerSkipForwardRect_)) return true;
            if(hit(playerAutoNextRect_)) return true;
            if(NativeVideoSizingAvailable() && hit(playerNativeSizeRect_)) return true;
            if(hit(playerFullRect_)) return true;
            return false;
        }
        if(owner==playerPrevHwnd_ || owner==playerNextHwnd_){
            const int direction=owner==playerPrevHwnd_?-1:1;
            if(!CanNavigatePlayerMedia(direction)) return false;
            RECT client{}; GetClientRect(owner,&client);
            if(PtInRect(&client,p)){ out=client; return true; }
            return false;
        }
        if(owner!=hwnd_) return false;
        if(mode_==Mode::Library){
            if(!IsAtLibraryRoot() && hit(backRect_)) return true;
            if(hit(categoryToggleRect_)) return true;
            if(category_==Category::Images && hit(slideshowRect_)) return true;
            if(hit(libraryFullRect_)) return true;
            if(IsAtChosenLibraryRoot() && hit(loadEverythingRect_)) return true;
            if((IsAtChosenLibraryRoot() || currentFolder_.empty() || externalMediaSession_) && hit(chooseRect_)) return true;
            if(IsAtChosenLibraryRoot() && hit(rescanRect_)) return true;
        } else if(mode_==Mode::Details){
            if(CanNavigateDetailsMedia(-1) && hit(detailsPrevRect_)) return true;
            if(CanNavigateDetailsMedia(1) && hit(detailsNextRect_)) return true;
            if(hit(backRect_)) return true;
            if(category_==Category::Videos && hit(playRect_)) return true;
            if(category_==Category::Images && hit(imageDetailsSlideshowRect_)) return true;
            if(category_==Category::Images && hit(imageDetailsNativeRect_)) return true;
            if(hit(detailsFullRect_)) return true;
        }
        return false;
    }

    float MediaHoverAmount(MediaHoverSurface surface, size_t id, const RECT& visual) const {
        if(mediaHoverSurface_!=surface || mediaHoverId_!=id || EmptyRectValue(visual)) return 0.0f;

        // Validate against the real cursor position as well as mouse messages. This
        // prevents a stale border after scrolling, resizing, view changes, or a missed
        // mouse-leave transition. Only the one currently tracked media item pays for
        // this check during painting.
        POINT cursor{};
        if(!GetCursorPos(&cursor)) return 0.0f;
        if(!ScreenToClient(hwnd_,&cursor) || !PtInRect(&visual,cursor)) return 0.0f;

        if(mediaHoverStart_==0) return 1.0f;
        const ULONGLONG elapsed=GetTickCount64()-mediaHoverStart_;
        return EaseUi(static_cast<float>(elapsed)/static_cast<float>(kMediaHoverFadeInMs));
    }

    void SetMediaHoverTarget(MediaHoverSurface nextSurface,size_t nextId,RECT nextRect,bool found) {
        if(!found){
            nextSurface=MediaHoverSurface::None;
            nextId=static_cast<size_t>(-1);
            nextRect=RECT{};
        }

        // Scrolling/zooming moves the card rectangle even when the cursor remains over
        // the same media item. Update that rectangle in place without restarting the
        // fade-in; restarting it on every wheel tick is what made the border flicker.
        if(nextSurface==mediaHoverSurface_ && nextId==mediaHoverId_){
            if(SameRect(nextRect,mediaHoverRect_)) return;
            const RECT oldRect=mediaHoverRect_;
            mediaHoverRect_=nextRect;
            if(!EmptyRectValue(oldRect)) InvalidateAnimatedRegion(hwnd_,oldRect);
            if(found && !EmptyRectValue(nextRect)) InvalidateAnimatedRegion(hwnd_,nextRect);
            return;
        }

        const RECT oldRect=mediaHoverRect_;
        mediaHoverSurface_=nextSurface;
        mediaHoverId_=nextId;
        mediaHoverRect_=nextRect;
        mediaHoverStart_=found?GetTickCount64():0;

        // There is deliberately no media fade-out state. The old border disappears
        // on the very next repaint, so rapid movement can never strand a highlighted
        // card. The new card still gets the requested subtle fade-in.
        if(!EmptyRectValue(oldRect)) InvalidateAnimatedRegion(hwnd_,oldRect);
        if(found){
            InvalidateAnimatedRegion(hwnd_,nextRect);
            StartUiAnimationTimer();
        }
    }

    void UpdateMediaHover(int x,int y) {
        POINT p{x,y};
        MediaHoverSurface nextSurface=MediaHoverSurface::None;
        size_t nextId=static_cast<size_t>(-1);
        RECT nextRect{};

        const std::vector<AnimatedMediaHit>* hits=nullptr;
        if(mode_==Mode::Library){
            nextSurface=MediaHoverSurface::Library;
            hits=&libraryMediaHoverHits_;
        } else if(mode_==Mode::Details && category_==Category::Videos){
            nextSurface=MediaHoverSurface::Preview;
            hits=&previewMediaHoverHits_;
        }

        bool found=false;
        if(hits){
            for(const auto& mediaHit:*hits){
                if(!EmptyRectValue(mediaHit.hit) && PtInRect(&mediaHit.hit,p)){
                    nextId=mediaHit.id;
                    nextRect=mediaHit.visual;
                    found=true;
                    break;
                }
            }
        }
        SetMediaHoverTarget(nextSurface,nextId,nextRect,found);
    }

    void ClearMediaHoverImmediate() {
        const RECT oldRect=mediaHoverRect_;
        mediaHoverSurface_=MediaHoverSurface::None;
        mediaHoverId_=static_cast<size_t>(-1);
        mediaHoverRect_=RECT{};
        mediaHoverStart_=0;
        if(!EmptyRectValue(oldRect)) InvalidateAnimatedRegion(hwnd_,oldRect);
    }

    void StartUiAnimationTimer() {
        if(hwnd_) SetTimer(hwnd_,kUiAnimationTimerId,16,nullptr);
    }

    void InvalidateAnimatedRegion(HWND owner, RECT r) {
        if(!owner || EmptyRectValue(r)) return;
        InflateRect(&r,3,3);
        InvalidateRect(owner,&r,FALSE);
    }

    void UpdateAnimatedHover(HWND owner,int x,int y) {
        POINT p{x,y}; RECT next{}; HWND nextOwner=nullptr;
        if(FindAnimatedButtonRect(owner,p,next)) nextOwner=owner;
        if(nextOwner==hoverOwner_ && SameRect(next,hoverRect_)) return;
        const HWND oldOwner=hoverOwner_;
        const RECT oldRect=hoverRect_;
        const HWND stalePreviousOwner=hoverPreviousOwner_;
        const RECT stalePreviousRect=hoverPreviousRect_;
        hoverPreviousOwner_=hoverOwner_; hoverPreviousRect_=hoverRect_;
        hoverOwner_=nextOwner; hoverRect_=next; hoverTransitionStart_=GetTickCount64();
        StartUiAnimationTimer();
        // If a second transition begins before the first fade-out finishes, repaint
        // the displaced previous rectangle once so it can never remain stranded.
        InvalidateAnimatedRegion(stalePreviousOwner,stalePreviousRect);
        InvalidateAnimatedRegion(oldOwner,oldRect);
        InvalidateAnimatedRegion(hoverOwner_,hoverRect_);
    }

    void ClearAnimatedHover(HWND owner) {
        if(hoverOwner_!=owner) return;
        const RECT oldRect=hoverRect_;
        const HWND stalePreviousOwner=hoverPreviousOwner_;
        const RECT stalePreviousRect=hoverPreviousRect_;
        hoverPreviousOwner_=hoverOwner_; hoverPreviousRect_=hoverRect_;
        hoverOwner_=nullptr; hoverRect_=RECT{}; hoverTransitionStart_=GetTickCount64();
        StartUiAnimationTimer();
        InvalidateAnimatedRegion(stalePreviousOwner,stalePreviousRect);
        InvalidateAnimatedRegion(owner,oldRect);
    }

    void ResetAnimatedHoverImmediate(HWND owner) {
        if(!owner) return;
        RECT current{},previous{};
        bool hadCurrent=false,hadPrevious=false;
        if(hoverOwner_==owner){ current=hoverRect_; hadCurrent=!EmptyRectValue(current); }
        if(hoverPreviousOwner_==owner){ previous=hoverPreviousRect_; hadPrevious=!EmptyRectValue(previous); }
        if(!hadCurrent && !hadPrevious && hoverOwner_!=owner && hoverPreviousOwner_!=owner) return;
        if(hoverOwner_==owner){ hoverOwner_=nullptr; hoverRect_=RECT{}; }
        if(hoverPreviousOwner_==owner){ hoverPreviousOwner_=nullptr; hoverPreviousRect_=RECT{}; }
        hoverTransitionStart_=0;
        if(hadCurrent) InvalidateAnimatedRegion(owner,current);
        if(hadPrevious && (!hadCurrent || !SameRect(previous,current))) InvalidateAnimatedRegion(owner,previous);
    }

    void TickUiAnimations() {
        const ULONGLONG now=GetTickCount64();
        bool active=false;
        UpdatePlayerFooterTransition(now,active);
        if(controlsFading_){
            UpdateControlsFade();
            if(controlsFading_) active=true;
        }
        HWND expiredHoverOwner=nullptr;
        RECT expiredHoverRect{};
        if(hoverTransitionStart_!=0){
            if(now-hoverTransitionStart_>=kUiAnimationDurationMs){
                expiredHoverOwner=hoverPreviousOwner_;
                expiredHoverRect=hoverPreviousRect_;
                hoverTransitionStart_=0; hoverPreviousOwner_=nullptr; hoverPreviousRect_=RECT{};
            } else active=true;
        }

        if(mediaHoverSurface_!=MediaHoverSurface::None && mediaHoverStart_!=0){
            if(now-mediaHoverStart_>=kMediaHoverFadeInMs){
                mediaHoverStart_=0;
                if(!EmptyRectValue(mediaHoverRect_)) InvalidateAnimatedRegion(hwnd_,mediaHoverRect_);
            } else {
                active=true;
                if(!EmptyRectValue(mediaHoverRect_)) InvalidateAnimatedRegion(hwnd_,mediaHoverRect_);
            }
        }

        bool repaintSlideshow=false;
        if(slideshowFadeActive_){
            repaintSlideshow=true;
            if(now-slideshowFadeStart_>=kUiAnimationDurationMs){
                slideshowFadeActive_=false; slideshowPreviousIndex_=static_cast<size_t>(-1);
            } else active=true;
        }

        bool returnHighlightActive=false;
        RECT expiredReturnRect{};
        if(libraryReturnHighlightStart_!=0){
            if(now-libraryReturnHighlightStart_>=kLibraryReturnHighlightDurationMs){
                expiredReturnRect=libraryReturnHighlightRect_;
                libraryReturnHighlightStart_=0;
                libraryReturnHighlightIndex_=static_cast<size_t>(-1);
                libraryReturnHighlightRect_=RECT{};
            } else {
                active=true;
                returnHighlightActive=true;
            }
        }

        if(!appNoticeText_.empty() && appNoticeStart_!=0){
            if(appNoticeUntil_==0 || now>=appNoticeUntil_){
                RECT expiredNoticeRect{};
                if(hwnd_){
                    RECT noticeClient{}; GetClientRect(hwnd_,&noticeClient);
                    expiredNoticeRect=AppNoticeRect(noticeClient);
                }
                appNoticeText_.clear();
                appNoticeUntil_=0;
                appNoticeStart_=0;
                if(hwnd_){
                    KillTimer(hwnd_,kAppNoticeTimerId);
                    if(!EmptyRectValue(expiredNoticeRect)) InvalidateRect(hwnd_,&expiredNoticeRect,FALSE);
                }
            }else{
                const ULONGLONG noticeElapsed=now-appNoticeStart_;
                if(noticeElapsed<kAppNoticePulseDurationMs) active=true;
                if(hwnd_){
                    // Repaint the notice for its full five-second pulse lifetime.
                    RECT noticeClient{}; GetClientRect(hwnd_,&noticeClient);
                    RECT noticeRect=AppNoticeRect(noticeClient);
                    InvalidateRect(hwnd_,&noticeRect,FALSE);
                }
            }
        }

        if(fullLoadFinishedAt_!=0){
            if(now-fullLoadFinishedAt_>=kFullLoadDonePopupDurationMs){
                fullLoadFinishedAt_=0;
                InvalidateLoadingPopupArea();
            } else {
                active=true;
            }
        }

        // Main-window hover animations repaint only their cards/buttons. This keeps the
        // new media hover effect from undoing the optimized Library scrolling pipeline.
        if(repaintSlideshow && hwnd_) InvalidateRect(hwnd_,nullptr,FALSE);
        else {
            if(hoverOwner_==hwnd_) InvalidateAnimatedRegion(hwnd_,hoverRect_);
            if(hoverPreviousOwner_==hwnd_) InvalidateAnimatedRegion(hwnd_,hoverPreviousRect_);
            if(expiredHoverOwner==hwnd_) InvalidateAnimatedRegion(hwnd_,expiredHoverRect);
            if(returnHighlightActive && mode_==Mode::Library){
                if(!EmptyRectValue(libraryReturnHighlightRect_)) InvalidateAnimatedRegion(hwnd_,libraryReturnHighlightRect_);
                else InvalidateLibraryScrollableArea();
            }
            if(!EmptyRectValue(expiredReturnRect) && hwnd_) InvalidateAnimatedRegion(hwnd_,expiredReturnRect);
        }

        const bool controlsHoverActive=(hoverOwner_==controlsHwnd_ || hoverPreviousOwner_==controlsHwnd_ || expiredHoverOwner==controlsHwnd_);
        const bool prevHoverActive=(hoverOwner_==playerPrevHwnd_ || hoverPreviousOwner_==playerPrevHwnd_ || expiredHoverOwner==playerPrevHwnd_);
        const bool nextHoverActive=(hoverOwner_==playerNextHwnd_ || hoverPreviousOwner_==playerNextHwnd_ || expiredHoverOwner==playerNextHwnd_);
        if(controlsHwnd_ && playerControlsVisible_ && controlsHoverActive) InvalidateRect(controlsHwnd_,nullptr,FALSE);
        if(playerPrevHwnd_ && playerControlsVisible_ && prevHoverActive) InvalidateRect(playerPrevHwnd_,nullptr,FALSE);
        if(playerNextHwnd_ && playerControlsVisible_ && nextHoverActive) InvalidateRect(playerNextHwnd_,nullptr,FALSE);
        if(!active && hwnd_) KillTimer(hwnd_,kUiAnimationTimerId);
    }

    static bool FindAdjacentInSameFolder(const std::vector<MediaItem>& items, size_t current, int direction, size_t& target) {
        if(current>=items.size() || direction==0) return false;
        const std::wstring folderKey=ToLower(fs::path(items[current].path).parent_path().lexically_normal().wstring());
        if(direction<0){
            for(size_t i=current;i>0;){
                --i;
                const std::wstring parentKey=ToLower(fs::path(items[i].path).parent_path().lexically_normal().wstring());
                if(parentKey==folderKey){ target=i; return true; }
            }
        } else {
            for(size_t i=current+1;i<items.size();++i){
                const std::wstring parentKey=ToLower(fs::path(items[i].path).parent_path().lexically_normal().wstring());
                if(parentKey==folderKey){ target=i; return true; }
            }
        }
        return false;
    }

    bool FindAdjacentMedia(const std::vector<MediaItem>& items, size_t current, int direction, size_t& target) const {
        if(!externalMediaSession_) return FindAdjacentInSameFolder(items,current,direction,target);
        if(current>=items.size() || direction==0) return false;
        if(direction<0){ if(current==0) return false; target=current-1; return true; }
        if(current+1>=items.size()) return false;
        target=current+1; return true;
    }

    bool FindAdjacentDetailsMedia(size_t current, int direction, size_t& target) const {
        if(detailsSearchNavigationActive_ && !detailsSearchNavigationIndices_.empty()){
            const auto it=std::find(detailsSearchNavigationIndices_.begin(),detailsSearchNavigationIndices_.end(),current);
            if(it==detailsSearchNavigationIndices_.end()) return false;
            if(direction<0){
                if(it==detailsSearchNavigationIndices_.begin()) return false;
                target=*(it-1); return true;
            }
            if(direction>0){
                const auto next=it+1;
                if(next==detailsSearchNavigationIndices_.end()) return false;
                target=*next; return true;
            }
            return false;
        }
        return FindAdjacentMedia(CurrentItems(),current,direction,target);
    }

    bool CanNavigateDetailsMedia(int direction) const {
        if(mode_!=Mode::Details) return false;
        size_t target=0; return FindAdjacentDetailsMedia(selected_,direction,target);
    }

    bool CanNavigatePlayerMedia(int direction) const {
        if(mode_!=Mode::Player || category_!=Category::Videos) return false;
        size_t target=0; return FindAdjacentMedia(videos_,selected_,direction,target);
    }

    void NavigateDetailsMedia(int direction) {
        if(mode_!=Mode::Details) return;
        auto& items=CurrentItems(); size_t target=0;
        if(!FindAdjacentDetailsMedia(selected_,direction,target)) return;
        StopImageSlideshow();
        thumbStop_.store(true,std::memory_order_release); ClearLoadingStateIf(1);
        if(category_==Category::Videos) StopPreviewWorker();
        selected_=target; detailsScrollY_=0; ResetPreviewZoom();
        if(category_==Category::Images){
            ResetImageZoom();
            if(nativeImageSizing_ && !fullscreen_) ApplyNativeImageWindowSize();
        }
        if(selected_<items.size()) SaveCurrentFolderViewState(items[selected_].path);
        if(category_==Category::Videos) StartPreviewWorkerForSelected();
        QueueDetailPrefetchWindow();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void NavigatePlayerMedia(int direction) {
        if(mode_!=Mode::Player || !player_) return;
        size_t target=0; if(!FindAdjacentMedia(videos_,selected_,direction,target)) return;
        const size_t oldSelected=selected_;
        const double oldTime=player_->CurrentTime();
        const double oldVolume=volumeFraction_;
        const bool oldPaused=player_->IsPaused();
        CancelPlayerSliderDrag();
        selected_=target; seekFraction_=0.0; volumeFraction_=0.30;
        const std::wstring targetPath=videos_[selected_].path;
        const HRESULT hr=player_->Open(targetPath,videos_[selected_].vr,0.0);
        if(FAILED(hr)){
            const bool mediaMissing=!PathExistsNoThrow(targetPath) || !IsLibraryRootAccessible();
            selected_=oldSelected; volumeFraction_=oldVolume;
            if(!mediaMissing){
                player_->Open(videos_[selected_].path,videos_[selected_].vr,std::max(0.0,oldTime));
                player_->SetVolume(volumeFraction_);
                if(oldPaused) player_->Pause();
                LeavePlayer();
                ShowInAppNotice(L"This media is unsupported.",5000);
            }else{
                if(player_) player_->Pause();
                NoteLibraryAccessFailure(true);
                ArmLibraryAccessMonitor(3000);
            }
            return;
        }
        SaveCurrentFolderViewState(videos_[selected_].path);
        player_->SetVolume(volumeFraction_); UpdateWindowTitle();
        if(nativeVideoSizing_ && videos_[selected_].vr.vr) SuspendNativeVideoSizingForVr();
        Layout();
        if(playerControlsVisible_) controlsHideDeadline_=GetTickCount64()+2200;
        InvalidateControls();
    }

    bool NextVideoInCurrentFolder(size_t current, size_t& next) const {
        return FindAdjacentMedia(videos_,current,1,next);
    }

    bool CurrentVideoIsAtEnd() const {
        if(mode_!=Mode::Player || !player_) return false;
        const double duration=player_->Duration();
        if(!(duration>0.0)) return false;
        const double current=player_->CurrentTime();
        // Media Foundation can report the final timestamp a few milliseconds shy of
        // Duration(). Treat the last quarter second as completed so enabling Auto Next
        // after the ENDED event still advances immediately.
        return current >= std::max(0.0,duration-0.25);
    }

    void HandlePlaybackEnded() {
        if(!autoNext_||videos_.empty()) return;
        size_t next=0; if(!NextVideoInCurrentFolder(selected_,next)) return;
        selected_=next; seekFraction_=0.0;
        SaveCurrentFolderViewState(videos_[selected_].path);
        if(player_){
            // Auto Next stays inside the current playback session, so carry the user's
            // current volume into the next video. LeavePlayer() resets the next session to 30%.
            const std::wstring targetPath=videos_[selected_].path;
            const HRESULT hr=player_->Open(targetPath,videos_[selected_].vr);
            if(FAILED(hr)){
                if(!PathExistsNoThrow(targetPath) || !IsLibraryRootAccessible()){
                    if(player_) player_->Pause();
                    NoteLibraryAccessFailure(true);
                    ArmLibraryAccessMonitor(3000);
                }else{
                    LeavePlayer();
                    ShowInAppNotice(L"This media is unsupported.",5000);
                }
                return;
            }
            player_->SetVolume(volumeFraction_); UpdateWindowTitle();
            if(nativeVideoSizing_ && videos_[selected_].vr.vr) SuspendNativeVideoSizingForVr();
            Layout(); InvalidateControls();
        }
    }

    void RepositionPlayerOverlayWindows() {
        // controlsHwnd_ and the edge arrows are owned WS_POPUP windows rather than
        // children of the player. Windows therefore does not move them with hwnd_ while
        // the user is inside the modal move/size loop. Keep their existing sizes and
        // follow the owner in screen coordinates without touching the D3D video child or
        // its swap-chain buffers. This preserves smooth live moving and prevents the
        // controls from being stranded at the old position until WM_EXITSIZEMOVE.
        if(mode_!=Mode::Player || !hwnd_) return;

        RECT client{}; GetClientRect(hwnd_,&client);
        const int cw=std::max(1,static_cast<int>(client.right-client.left));
        const int ch=std::max(1,static_cast<int>(client.bottom-client.top));
        POINT origin{0,0}; ClientToScreen(hwnd_,&origin);

        if(controlsHwnd_){
            RECT wr{}; GetWindowRect(controlsHwnd_,&wr);
            const int ow=std::max(1,static_cast<int>(wr.right-wr.left));
            const int oh=std::max(1,static_cast<int>(wr.bottom-wr.top));
            SetWindowPos(controlsHwnd_,HWND_TOP,origin.x,origin.y+std::max(0,ch-oh),ow,oh,
                         SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        }

        auto moveEdge=[&](HWND edge,bool rightSide){
            if(!edge) return;
            RECT wr{}; GetWindowRect(edge,&wr);
            const int ew=std::max(1,static_cast<int>(wr.right-wr.left));
            const int eh=std::max(1,static_cast<int>(wr.bottom-wr.top));
            constexpr int edgePad=18;
            const int x=rightSide
                ? origin.x+std::max(edgePad,cw-edgePad-ew)
                : origin.x+edgePad;
            const int y=origin.y+std::max(0,(ch-eh)/2);
            SetWindowPos(edge,HWND_TOP,x,y,ew,eh,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        };
        moveEdge(playerPrevHwnd_,false);
        moveEdge(playerNextHwnd_,true);

        // The short footer cross-fade is another owned popup and can overlap a move if
        // the user grabs the title bar immediately after opening a video. Move that
        // snapshot with the owner as well so no transient overlay is left behind.
        if(playerFooterTransitionHwnd_){
            RECT wr{}; GetWindowRect(playerFooterTransitionHwnd_,&wr);
            const int ow=std::max(1,static_cast<int>(wr.right-wr.left));
            const int oh=std::max(1,static_cast<int>(wr.bottom-wr.top));
            SetWindowPos(playerFooterTransitionHwnd_,HWND_TOP,origin.x,origin.y+std::max(0,ch-oh),ow,oh,
                         SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        }
    }

    void Layout() {
        if(mode_!=Mode::Player||!videoHwnd_) return; RECT rc{}; GetClientRect(hwnd_,&rc);
        const int cw=std::max(1,static_cast<int>(rc.right-rc.left)),ch=std::max(1,static_cast<int>(rc.bottom-rc.top));
        MoveWindow(videoHwnd_,0,0,cw,ch,TRUE);
        if(!controlsHwnd_) return;

        // Below this width the old single-row footer could make Auto Next / Native /
        // Fullscreen collide with transport and volume controls. Switch to a taller,
        // deliberately separated layout instead of allowing any rectangles to overlap.
        const bool compactControls = cw < 760;
        constexpr int hoverStrip = 28;
        const int oh=(compactControls?200:122)+hoverStrip;
        POINT clientOrigin{0,0}; ClientToScreen(hwnd_,&clientOrigin);
        const int overlayX=clientOrigin.x;
        const int overlayY=clientOrigin.y+std::max(0,ch-oh);
        SetWindowPos(controlsHwnd_,HWND_TOP,overlayX,overlayY,cw,oh,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        ApplyPlayerOverlayCornerRegion(cw,oh);

        constexpr int edgeW=48,edgeH=76,edgePad=18;
        const int edgeY=clientOrigin.y+std::max(0,(ch-edgeH)/2);
        if(playerPrevHwnd_){
            SetWindowPos(playerPrevHwnd_,HWND_TOP,clientOrigin.x+edgePad,edgeY,edgeW,edgeH,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
            HRGN region=CreateRoundRectRgn(0,0,edgeW+1,edgeH+1,12,12); SetWindowRgn(playerPrevHwnd_,region,FALSE);
        }
        if(playerNextHwnd_){
            SetWindowPos(playerNextHwnd_,HWND_TOP,clientOrigin.x+std::max(edgePad,cw-edgePad-edgeW),edgeY,edgeW,edgeH,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
            HRGN region=CreateRoundRectRgn(0,0,edgeW+1,edgeH+1,12,12); SetWindowRgn(playerNextHwnd_,region,FALSE);
        }

        if(!playerControlsVisible_){
            ShowWindow(controlsHwnd_,SW_HIDE);
            if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_HIDE);
            if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_HIDE);
            return;
        }
        ShowWindow(controlsHwnd_,SW_SHOWNOACTIVATE);
        if(playerPrevHwnd_) ShowWindow(playerPrevHwnd_,SW_SHOWNOACTIVATE);
        if(playerNextHwnd_) ShowWindow(playerNextHwnd_,SW_SHOWNOACTIVATE);

        if(compactControls){
            const int sidePad=std::clamp(cw/30,8,12);
            seekRect_={sidePad,10+hoverStrip,std::max(sidePad+1,cw-sidePad),30+hoverStrip};
            playerTimeRect_={std::max(sidePad,cw/2-110),32+hoverStrip,std::min(cw-sidePad,cw/2+110),53+hoverStrip};

            // Dedicated transport row.
            const int playSize=54,skipSize=48,transportGap=10;
            const int playTop=56+hoverStrip;
            playerPlayRect_={cw/2-playSize/2,playTop,cw/2+(playSize-playSize/2),playTop+playSize};
            playerSkipBackRect_={playerPlayRect_.left-transportGap-skipSize,playTop+3,playerPlayRect_.left-transportGap,playTop+3+skipSize};
            playerSkipForwardRect_={playerPlayRect_.right+transportGap,playTop+3,playerPlayRect_.right+transportGap+skipSize,playTop+3+skipSize};

            // Volume gets its own row so it cannot be squeezed between transport and toggles.
            const int volumeAvailable=std::max(1,cw-sidePad*2);
            const int desiredVolumeW=std::max(60,std::min(190,cw-86));
            const int volumeW=std::min(volumeAvailable,desiredVolumeW);
            const int volumeLeft=(cw-volumeW)/2;
            volumeRect_={volumeLeft,121+hoverStrip,volumeLeft+volumeW,139+hoverStrip};
            if(cw>=390) volumeLabelRect_={std::max(sidePad,volumeLeft-42),113+hoverStrip,std::max(sidePad,volumeLeft-4),147+hoverStrip};
            else volumeLabelRect_=RECT{};

            // Bottom action row: Back/VR stay on the left; session toggles stay on the right.
            // The groups are sized from the edges inward, so even a native-sized narrow
            // player cannot paint one button on top of another.
            const int iconSize=(cw<300?44:48);
            const int iconGap=(cw<300?5:6);
            const int iconBottom=oh-10;
            const int iconTop=iconBottom-iconSize;
            const int textTop=iconTop+(iconSize-38)/2;
            const int backW=(cw<300?56:70);
            const int vrW=(cw<300?52:68);
            playerBackRect_={sidePad,textTop,sidePad+backW,textTop+38};
            if(player_ && player_->VR().vr){
                playerVrToggleRect_={playerBackRect_.right+iconGap,textTop,playerBackRect_.right+iconGap+vrW,textTop+38};
            }else playerVrToggleRect_=RECT{};

            playerFullRect_={cw-sidePad-iconSize,iconTop,cw-sidePad,iconBottom};
            int rightCursor=playerFullRect_.left-iconGap;
            if(NativeVideoSizingAvailable()){
                playerNativeSizeRect_={rightCursor-iconSize,iconTop,rightCursor,iconBottom};
                rightCursor=playerNativeSizeRect_.left-iconGap;
            }else playerNativeSizeRect_=RECT{};
            playerAutoNextRect_={rightCursor-iconSize,iconTop,rightCursor,iconBottom};
        }else{
            const int pad=20; seekRect_={pad,10+hoverStrip,cw-pad,30+hoverStrip};
            constexpr int rowDrop = 10;
            constexpr int footerButtonH = 38;
            const int textButtonTop = oh - 51;
            playerBackRect_={20,textButtonTop,100,textButtonTop+footerButtonH};
            playerVrToggleRect_={112,textButtonTop,192,textButtonTop+footerButtonH};
            playerPlayRect_={cw/2-27,50+rowDrop+hoverStrip,cw/2+27,104+rowDrop+hoverStrip};
            constexpr int skipSize=48;
            constexpr int skipGap=12;
            playerSkipBackRect_={playerPlayRect_.left-skipGap-skipSize,54+rowDrop+hoverStrip,playerPlayRect_.left-skipGap,102+rowDrop+hoverStrip};
            playerSkipForwardRect_={playerPlayRect_.right+skipGap,54+rowDrop+hoverStrip,playerPlayRect_.right+skipGap+skipSize,102+rowDrop+hoverStrip};
            playerTimeRect_={cw/2-120,32+hoverStrip,cw/2+120,56+hoverStrip};
            playerFullRect_={cw-pad-48,54+rowDrop+hoverStrip,cw-pad,102+rowDrop+hoverStrip};
            if(NativeVideoSizingAvailable()){
                playerNativeSizeRect_={playerFullRect_.left-58,54+rowDrop+hoverStrip,playerFullRect_.left-10,102+rowDrop+hoverStrip};
                playerAutoNextRect_={playerNativeSizeRect_.left-58,54+rowDrop+hoverStrip,playerNativeSizeRect_.left-10,102+rowDrop+hoverStrip};
            }else{
                playerNativeSizeRect_=RECT{};
                playerAutoNextRect_={playerFullRect_.left-58,54+rowDrop+hoverStrip,playerFullRect_.left-10,102+rowDrop+hoverStrip};
            }
            const int volumeRight=playerAutoNextRect_.left-18;
            const int volumeLeft=std::max(static_cast<int>(playerSkipForwardRect_.right)+18,volumeRight-190);
            volumeRect_={volumeLeft,68+rowDrop+hoverStrip,volumeRight,86+rowDrop+hoverStrip};
            volumeLabelRect_={volumeLeft-42,60+rowDrop+hoverStrip,volumeLeft-4,94+rowDrop+hoverStrip};
        }
        InvalidateControls();
    }

    void UpdateSeekHover(int x, int) {
        // Timestamp is a seek-drag indicator only: never show it on hover.
        if (!seekDragging_) {
            if (seekHoverVisible_) {
                seekHoverVisible_ = false;
                InvalidateControls();
            }
            return;
        }
        const int newX = std::clamp(x, static_cast<int>(seekRect_.left), static_cast<int>(seekRect_.right));
        if (!seekHoverVisible_ || newX != seekHoverX_) {
            seekHoverVisible_ = true;
            seekHoverX_ = newX;
            InvalidateControls();
        }
    }

    void ClearSeekHover() {
        if (!seekHoverVisible_) return;
        seekHoverVisible_ = false;
        InvalidateControls();
    }

    void SetSeekFromX(int x,bool commit) {
        if(!player_) return; const int width=std::max(1,static_cast<int>(seekRect_.right-seekRect_.left));
        const int cx=std::clamp(x,static_cast<int>(seekRect_.left),static_cast<int>(seekRect_.right)); seekFraction_=static_cast<double>(cx-seekRect_.left)/width;
        if(commit){const double d=player_->Duration();if(d>0.0)player_->Seek(d*seekFraction_);} InvalidateControls();
    }

    void SetVolumeFromX(int x) {
        if(!player_) return; const int width=std::max(1,static_cast<int>(volumeRect_.right-volumeRect_.left));
        const int cx=std::clamp(x,static_cast<int>(volumeRect_.left),static_cast<int>(volumeRect_.right)); volumeFraction_=static_cast<double>(cx-volumeRect_.left)/width;
        player_->SetVolume(volumeFraction_); InvalidateControls();
    }

    void SkipPlaybackSeconds(double deltaSeconds) {
        if(!player_) return;
        const double duration=player_->Duration();
        double target=player_->CurrentTime()+deltaSeconds;
        if(duration>0.0) target=std::clamp(target,0.0,duration);
        else target=std::max(0.0,target);
        player_->Seek(target);
        if(duration>0.0) seekFraction_=std::clamp(target/duration,0.0,1.0);
        InvalidateControls();
    }

    void CancelPlayerSliderDrag() {
        const bool changed=seekDragging_||volumeDragging_||seekHoverVisible_;
        seekDragging_=false; volumeDragging_=false; seekHoverVisible_=false;
        if(changed) InvalidateControls();
    }

    void PlayerMouseDown(int x,int y) {
        POINT p{x,y}; if(PtInRect(&seekRect_,p)){seekDragging_=true;UpdateSeekHover(x,y);SetSeekFromX(x,false);SetCapture(controlsHwnd_);return;}
        if(PtInRect(&volumeRect_,p)){volumeDragging_=true;SetVolumeFromX(x);SetCapture(controlsHwnd_);return;}
    }

    void PlayerMouseMove(int x,int y) {
        if(seekDragging_){ UpdateSeekHover(x,y); SetSeekFromX(x,false); }
        if(volumeDragging_)SetVolumeFromX(x);
    }

    void PlayerMouseUp(int x,int y) {
        POINT p{x,y}; PlayerActivity(true);
        if(seekDragging_){SetSeekFromX(x,true);seekDragging_=false;if(GetCapture()==controlsHwnd_)ReleaseCapture();ClearSeekHover();return;}
        if(volumeDragging_){SetVolumeFromX(x);volumeDragging_=false;if(GetCapture()==controlsHwnd_)ReleaseCapture();InvalidateControls();return;}
        if(PtInRect(&playerBackRect_,p)){LeavePlayer();return;}
        if(player_ && player_->VR().vr && PtInRect(&playerVrToggleRect_,p)){player_->ToggleVrBackside();InvalidateControls();return;}
        if(PtInRect(&playerSkipBackRect_,p)){SkipPlaybackSeconds(-30.0);return;}
        if(PtInRect(&playerPlayRect_,p)){if(player_)player_->PlayPause();InvalidateControls();return;}
        if(PtInRect(&playerSkipForwardRect_,p)){SkipPlaybackSeconds(30.0);return;}
        if(PtInRect(&playerAutoNextRect_,p)){
            const bool enabling=!autoNext_;
            autoNext_=enabling;
            InvalidateControls();
            if(enabling && CurrentVideoIsAtEnd()) HandlePlaybackEnded();
            return;
        }
        if(NativeVideoSizingAvailable() && PtInRect(&playerNativeSizeRect_,p)){ToggleNativeVideoSizing();PlayerActivity(true);return;}
        if(PtInRect(&playerFullRect_,p)){ToggleFullscreen();PlayerActivity(true);return;}
        if(PtInRect(&seekRect_,p)){SetSeekFromX(x,true);return;}
        if(PtInRect(&volumeRect_,p)){SetVolumeFromX(x);return;}
    }

    void UpdateSeekUi() {
        static ULONGLONG last=0; const ULONGLONG now=GetTickCount64(); if(now-last<100)return;last=now;
        if(!player_)return;const double d=player_->Duration(),t=player_->CurrentTime();if(d>0.0&&!seekDragging_)seekFraction_=std::clamp(t/d,0.0,1.0);InvalidateControls();
    }

    void HandlePlayerWheel(WPARAM w, LPARAM l) {
        if(!player_) return;
        const short delta=GET_WHEEL_DELTA_WPARAM(w);
        if(player_->VR().vr){
            // Preserve the established VR wheel/FOV behavior exactly.
            PlayerActivity(true);
            player_->Wheel(delta);
            return;
        }
        // Flat/non-VR playback uses the plain wheel for cursor-anchored zoom.
        // Native Size still owns the render scale while enabled, and VR keeps its
        // separate FOV wheel behavior above.
        if(nativeVideoSizing_) return;
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(videoHwnd_) ScreenToClient(videoHwnd_,&p);
        player_->FlatWheelZoom(delta,p.x,p.y);
        if(videoHwnd_) InvalidateRect(videoHwnd_,nullptr,FALSE);
    }

    bool ImageZoomActive() const { return std::abs(imageZoomScale_-1.0f)>0.001f; }
    void ResetImageZoom() {
        imageZoomScale_=1.0f; imageZoomCenterU_=0.5f; imageZoomCenterV_=0.5f;
        imageZoomDragging_=false;
        if(hwnd_ && GetCapture()==hwnd_) ReleaseCapture();
    }
    void ZoomImageAtPoint(short delta, POINT p) {
        if(delta==0 || nativeImageSizing_ || mode_!=Mode::Details || category_!=Category::Images || selected_>=images_.size()) return;
        if(!PtInRect(&detailsMediaRect_,p)) return;
        auto& item=images_[selected_];
        UINT sourceW=item.detailThumbW>0?static_cast<UINT>(item.detailThumbW):item.sourceWidth;
        UINT sourceH=item.detailThumbH>0?static_cast<UINT>(item.detailThumbH):item.sourceHeight;
        if(!sourceW || !sourceH){ if(!GetCurrentImageNativeSize(sourceW,sourceH)) return; }
        const float rw=static_cast<float>(std::max<LONG>(1L,detailsMediaRect_.right-detailsMediaRect_.left));
        const float rh=static_cast<float>(std::max<LONG>(1L,detailsMediaRect_.bottom-detailsMediaRect_.top));
        const float fit=std::min(rw/static_cast<float>(sourceW),rh/static_cast<float>(sourceH));
        const float oldZoom=std::clamp(imageZoomScale_,0.25f,8.0f);
        const float factor=std::pow(1.15f,static_cast<float>(delta)/120.0f);
        const float newZoom=std::clamp(oldZoom*factor,0.25f,8.0f);
        if(std::abs(newZoom-1.0f)<=0.001f){ ResetImageZoom(); InvalidateRect(hwnd_,nullptr,FALSE); return; }
        if(newZoom<1.0f){
            imageZoomScale_=newZoom; imageZoomCenterU_=0.5f; imageZoomCenterV_=0.5f;
            InvalidateRect(hwnd_,nullptr,FALSE); return;
        }
        const float oldDw=std::max(1.0f,static_cast<float>(sourceW)*fit*oldZoom);
        const float oldDh=std::max(1.0f,static_cast<float>(sourceH)*fit*oldZoom);
        const float newDw=std::max(1.0f,static_cast<float>(sourceW)*fit*newZoom);
        const float newDh=std::max(1.0f,static_cast<float>(sourceH)*fit*newZoom);
        const float cx=0.5f*static_cast<float>(detailsMediaRect_.left+detailsMediaRect_.right);
        const float cy=0.5f*static_cast<float>(detailsMediaRect_.top+detailsMediaRect_.bottom);
        const float sourceU=imageZoomCenterU_+(static_cast<float>(p.x)-cx)/oldDw;
        const float sourceV=imageZoomCenterV_+(static_cast<float>(p.y)-cy)/oldDh;
        float centerU=sourceU-(static_cast<float>(p.x)-cx)/newDw;
        float centerV=sourceV-(static_cast<float>(p.y)-cy)/newDh;
        const float halfU=rw/(2.0f*newDw),halfV=rh/(2.0f*newDh);
        centerU=halfU>=0.5f?0.5f:std::clamp(centerU,halfU,1.0f-halfU);
        centerV=halfV>=0.5f?0.5f:std::clamp(centerV,halfV,1.0f-halfV);
        imageZoomScale_=newZoom; imageZoomCenterU_=centerU; imageZoomCenterV_=centerV;
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void PanImageZoomByDelta(int dx,int dy) {
        if(!imageZoomDragging_ || nativeImageSizing_ || imageZoomScale_<=1.001f || mode_!=Mode::Details || category_!=Category::Images || selected_>=images_.size()) return;
        auto& item=images_[selected_];
        UINT sourceW=item.detailThumbW>0?static_cast<UINT>(item.detailThumbW):item.sourceWidth;
        UINT sourceH=item.detailThumbH>0?static_cast<UINT>(item.detailThumbH):item.sourceHeight;
        if(!sourceW || !sourceH){ if(!GetCurrentImageNativeSize(sourceW,sourceH)) return; }
        const float rw=static_cast<float>(std::max<LONG>(1L,detailsMediaRect_.right-detailsMediaRect_.left));
        const float rh=static_cast<float>(std::max<LONG>(1L,detailsMediaRect_.bottom-detailsMediaRect_.top));
        const float fit=std::min(rw/static_cast<float>(sourceW),rh/static_cast<float>(sourceH));
        const float dw=std::max(1.0f,static_cast<float>(sourceW)*fit*imageZoomScale_);
        const float dh=std::max(1.0f,static_cast<float>(sourceH)*fit*imageZoomScale_);
        imageZoomCenterU_-=static_cast<float>(dx)/dw;
        imageZoomCenterV_-=static_cast<float>(dy)/dh;
        const float halfU=rw/(2.0f*dw),halfV=rh/(2.0f*dh);
        imageZoomCenterU_=halfU>=0.5f?0.5f:std::clamp(imageZoomCenterU_,halfU,1.0f-halfU);
        imageZoomCenterV_=halfV>=0.5f?0.5f:std::clamp(imageZoomCenterV_,halfV,1.0f-halfV);
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    RECT StandardWindowRectForCurrentMonitor() const {
        const int fallbackW=std::max(1,GetSystemMetrics(SM_CXSCREEN)/2);
        const int fallbackH=std::max(1,GetSystemMetrics(SM_CYSCREEN)/2);
        RECT r{0,0,fallbackW,fallbackH};
        if(!hwnd_) return r;
        MONITORINFO mi{sizeof(mi)};
        if(!GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&mi)) return r;
        const int monitorW=std::max(1,static_cast<int>(mi.rcMonitor.right-mi.rcMonitor.left));
        const int monitorH=std::max(1,static_cast<int>(mi.rcMonitor.bottom-mi.rcMonitor.top));
        const int w=std::max(1,monitorW/2);
        const int h=std::max(1,monitorH/2);
        // Standard/restored windows are centered against the full monitor rectangle,
        // not the work area, so Native/standard transitions never visibly drift.
        const int x=mi.rcMonitor.left+(monitorW-w)/2;
        const int y=mi.rcMonitor.top+(monitorH-h)/2;
        return RECT{x,y,x+w,y+h};
    }

    void RestoreStandardWindowSize() {
        if(!hwnd_ || fullscreen_) return;
        const RECT r=StandardWindowRectForCurrentMonitor();
        SetWindowPos(hwnd_,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }

    void SetFullscreenRestoreToStandardWindow() {
        savedRect_=StandardWindowRectForCurrentMonitor();
    }

    void SuspendNativeVideoSizingForVr() {
        if(!nativeVideoSizing_ || !player_ || !player_->VR().vr) return;
        player_->SetNativePixelSizing(false);
        // In windowed mode, VR uses the normal pre-native player window while the
        // preference remains latched. Keep the restore rectangle so the next flat
        // video can immediately resume its own native dimensions.
        if(!fullscreen_){
            RestoreStandardWindowSize();
        }
    }

    bool GetNativeMinimumWindowSize(SIZE& minimum) {
        minimum=SIZE{};if(fullscreen_||!hwnd_)return false;UINT sourceW=0,sourceH=0;
        if(nativeVideoSizing_&&mode_==Mode::Player&&player_&&!player_->VR().vr){const auto size=player_->EyeSize();sourceW=size.first;sourceH=size.second;}
        else if(nativeImageSizing_&&mode_==Mode::Details&&category_==Category::Images){if(!GetCurrentImageNativeSize(sourceW,sourceH))return false;}
        else return false;
        if(!sourceW||!sourceH)return false;RECT wr{},cr{};if(!GetWindowRect(hwnd_,&wr)||!GetClientRect(hwnd_,&cr))return false;
        const LONG nonClientW=std::max<LONG>(0,(wr.right-wr.left)-(cr.right-cr.left)),nonClientH=std::max<LONG>(0,(wr.bottom-wr.top)-(cr.bottom-cr.top));
        minimum.cx=static_cast<LONG>(std::min<uint64_t>(static_cast<uint64_t>(LONG_MAX),static_cast<uint64_t>(sourceW)+static_cast<uint64_t>(nonClientW)));
        minimum.cy=static_cast<LONG>(std::min<uint64_t>(static_cast<uint64_t>(LONG_MAX),static_cast<uint64_t>(sourceH)+static_cast<uint64_t>(nonClientH)));
        return true;
    }

    void ApplyNativeVideoWindowSize() {
        if (!nativeVideoSizing_ || fullscreen_ || mode_ != Mode::Player || !player_ || !hwnd_ || player_->VR().vr) return;
        const auto sourceSize=player_->EyeSize();
        if (!sourceSize.first || !sourceSize.second) return;

        RECT windowRect{}; RECT clientRect{};
        if (!GetWindowRect(hwnd_,&windowRect) || !GetClientRect(hwnd_,&clientRect)) return;
        const int currentWindowW=std::max(1,static_cast<int>(windowRect.right-windowRect.left));
        const int currentWindowH=std::max(1,static_cast<int>(windowRect.bottom-windowRect.top));
        const int currentClientW=std::max(1,static_cast<int>(clientRect.right-clientRect.left));
        const int currentClientH=std::max(1,static_cast<int>(clientRect.bottom-clientRect.top));
        const int nonClientW=std::max(0,currentWindowW-currentClientW);
        const int nonClientH=std::max(0,currentWindowH-currentClientH);

        MONITORINFO mi{sizeof(mi)};
        if(!GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&mi)) return;
        const int monitorW=std::max(1,static_cast<int>(mi.rcMonitor.right-mi.rcMonitor.left));
        const int monitorH=std::max(1,static_cast<int>(mi.rcMonitor.bottom-mi.rcMonitor.top));
        // Strict Native Size: the client render area is exactly the source dimensions.
        // Oversized media may extend beyond the monitor; it is never silently downscaled.
        const int targetClientW=std::max(1,static_cast<int>(sourceSize.first));
        const int targetClientH=std::max(1,static_cast<int>(sourceSize.second));
        const int targetWindowW=targetClientW+nonClientW;
        const int targetWindowH=targetClientH+nonClientH;
        int x=mi.rcMonitor.left+(monitorW-targetWindowW)/2;
        int y=mi.rcMonitor.top+(monitorH-targetWindowH)/2;
        SetWindowPos(hwnd_,nullptr,x,y,targetWindowW,targetWindowH,SWP_NOZORDER|SWP_NOACTIVATE);
        // Correct after Windows has applied DPI/non-client metrics. This second pass
        // guarantees that the client surface itself is the requested pixel size.
        RECT actualClient{},actualWindow{};
        if(GetClientRect(hwnd_,&actualClient) && GetWindowRect(hwnd_,&actualWindow)){
            const int actualClientW=std::max(1,static_cast<int>(actualClient.right-actualClient.left));
            const int actualClientH=std::max(1,static_cast<int>(actualClient.bottom-actualClient.top));
            const int correctedW=std::max(1,static_cast<int>(actualWindow.right-actualWindow.left)+(targetClientW-actualClientW));
            const int correctedH=std::max(1,static_cast<int>(actualWindow.bottom-actualWindow.top)+(targetClientH-actualClientH));
            if(actualClientW!=targetClientW || actualClientH!=targetClientH){
                x=mi.rcMonitor.left+(monitorW-correctedW)/2;
                y=mi.rcMonitor.top+(monitorH-correctedH)/2;
                SetWindowPos(hwnd_,nullptr,x,y,correctedW,correctedH,SWP_NOZORDER|SWP_NOACTIVATE);
            }
        }
    }

    void ToggleNativeVideoSizing() {
        if(mode_!=Mode::Player || !player_ || !hwnd_ || player_->VR().vr) return;
        if(!nativeVideoSizing_){
            if(!nativeSizingRestoreRectValid_){
                nativeSizingRestoreRect_=StandardWindowRectForCurrentMonitor();
                nativeSizingRestoreRectValid_=true;
            }
            nativeVideoSizing_=true;
            player_->SetNativePixelSizing(true);
            if(!fullscreen_) ApplyNativeVideoWindowSize();
        }else{
            nativeVideoSizing_=false;
            player_->SetNativePixelSizing(false);
            if(fullscreen_){
                // Native OFF while fullscreen stays fullscreen. When fullscreen is later
                // left, return to the app's standard half-monitor window.
                SetFullscreenRestoreToStandardWindow();
            }else{
                RestoreStandardWindowSize();
            }
            nativeSizingRestoreRectValid_=false;
        }
        Layout();
        InvalidateControls();
        if(videoHwnd_) InvalidateRect(videoHwnd_,nullptr,FALSE);
    }

    bool GetCurrentImageNativeSize(UINT& width, UINT& height) {
        if(mode_!=Mode::Details || category_!=Category::Images || selected_>=images_.size()) return false;
        auto& item=images_[selected_];
        if(!item.sourceWidth || !item.sourceHeight){
            Gdiplus::Bitmap probe(item.path.c_str());
            if(probe.GetLastStatus()!=Gdiplus::Ok) return false;
            item.sourceWidth=probe.GetWidth();
            item.sourceHeight=probe.GetHeight();
        }
        if(!item.sourceWidth || !item.sourceHeight) return false;
        width=item.sourceWidth; height=item.sourceHeight; return true;
    }

    void ApplyNativeImageWindowSize() {
        if (!nativeImageSizing_ || fullscreen_ || mode_ != Mode::Details || category_ != Category::Images || !hwnd_) return;
        UINT sourceW=0, sourceH=0;
        if (!GetCurrentImageNativeSize(sourceW, sourceH)) return;

        RECT windowRect{}; RECT clientRect{};
        if (!GetWindowRect(hwnd_,&windowRect) || !GetClientRect(hwnd_,&clientRect)) return;
        const int currentWindowW=std::max(1,static_cast<int>(windowRect.right-windowRect.left));
        const int currentWindowH=std::max(1,static_cast<int>(windowRect.bottom-windowRect.top));
        const int currentClientW=std::max(1,static_cast<int>(clientRect.right-clientRect.left));
        const int currentClientH=std::max(1,static_cast<int>(clientRect.bottom-clientRect.top));
        const int nonClientW=std::max(0,currentWindowW-currentClientW);
        const int nonClientH=std::max(0,currentWindowH-currentClientH);

        MONITORINFO mi{sizeof(mi)};
        if(!GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&mi)) return;
        const int monitorW=std::max(1,static_cast<int>(mi.rcMonitor.right-mi.rcMonitor.left));
        const int monitorH=std::max(1,static_cast<int>(mi.rcMonitor.bottom-mi.rcMonitor.top));
        const int targetClientW=std::max(1,static_cast<int>(sourceW));
        const int targetClientH=std::max(1,static_cast<int>(sourceH));
        const int targetWindowW=targetClientW+nonClientW;
        const int targetWindowH=targetClientH+nonClientH;
        int x=mi.rcMonitor.left+(monitorW-targetWindowW)/2;
        int y=mi.rcMonitor.top+(monitorH-targetWindowH)/2;
        SetWindowPos(hwnd_,nullptr,x,y,targetWindowW,targetWindowH,SWP_NOZORDER|SWP_NOACTIVATE);
        RECT actualClient{},actualWindow{};
        if(GetClientRect(hwnd_,&actualClient) && GetWindowRect(hwnd_,&actualWindow)){
            const int actualClientW=std::max(1,static_cast<int>(actualClient.right-actualClient.left));
            const int actualClientH=std::max(1,static_cast<int>(actualClient.bottom-actualClient.top));
            const int correctedW=std::max(1,static_cast<int>(actualWindow.right-actualWindow.left)+(targetClientW-actualClientW));
            const int correctedH=std::max(1,static_cast<int>(actualWindow.bottom-actualWindow.top)+(targetClientH-actualClientH));
            if(actualClientW!=targetClientW || actualClientH!=targetClientH){
                x=mi.rcMonitor.left+(monitorW-correctedW)/2;
                y=mi.rcMonitor.top+(monitorH-correctedH)/2;
                SetWindowPos(hwnd_,nullptr,x,y,correctedW,correctedH,SWP_NOZORDER|SWP_NOACTIVATE);
            }
        }
    }

    void ToggleNativeImageSizing() {
        if(mode_!=Mode::Details || category_!=Category::Images || !hwnd_ || selected_>=images_.size()) return;
        if(!nativeImageSizing_){
            if(!nativeImageSizingRestoreRectValid_){
                nativeImageSizingRestoreRect_=StandardWindowRectForCurrentMonitor();
                nativeImageSizingRestoreRectValid_=true;
            }
            ResetImageZoom();
            nativeImageSizing_=true;
            if(!fullscreen_) ApplyNativeImageWindowSize();
        }else{
            nativeImageSizing_=false;
            if(fullscreen_){
                SetFullscreenRestoreToStandardWindow();
            }else{
                RestoreStandardWindowSize();
            }
            nativeImageSizingRestoreRectValid_=false;
        }
        Layout();
        InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void ApplyMainWindowCornerPreference() {
        if(!hwnd_) return;
        // DWMWA_WINDOW_CORNER_PREFERENCE (33): 1 = do not round, 2 = round.
        // Numeric values keep this compatible with older Windows SDK headers.
        const DWORD preference=fullscreen_?1u:2u;
        DwmSetWindowAttribute(hwnd_,33,&preference,sizeof(preference));
    }

    void ApplyPlayerOverlayCornerRegion(int width,int height) {
        if(!controlsHwnd_ || width<=0 || height<=0) return;
        if(fullscreen_){
            SetWindowRgn(controlsHwnd_,nullptr,FALSE);
            return;
        }
        // The overlay is a top-level layered popup. Give it square top corners but
        // rounded bottom corners so it cannot visually square off the main window.
        constexpr int cornerDiameter=20;
        HRGN rounded=CreateRoundRectRgn(0,0,width+1,height+1,cornerDiameter,cornerDiameter);
        HRGN squareTop=CreateRectRgn(0,0,width,std::min(height,cornerDiameter));
        if(rounded && squareTop){
            CombineRgn(rounded,rounded,squareTop,RGN_OR);
            if(SetWindowRgn(controlsHwnd_,rounded,FALSE)!=0) rounded=nullptr; // window owns it on success
        }
        if(rounded) DeleteObject(rounded);
        if(squareTop) DeleteObject(squareTop);
    }

    void ToggleFullscreen() {
        const bool entering=!fullscreen_;

        // Preserve the Library's logical scroll position across the fullscreen geometry
        // change. Fullscreen can fit many more cards per row, so carrying the old raw
        // pixel offset across can leave the viewport below the newly shortened grid.
        // Keep top/middle/bottom relative to the old scroll range, and make bottom exact.
        double libraryScrollFraction=0.0;
        bool libraryWasAtBottom=false;
        if(mode_==Mode::Library){
            RECT oldLibraryRc{}; GetClientRect(hwnd_,&oldLibraryRc);
            const int oldMaxScroll=LibraryMaxScroll(oldLibraryRc);
            const int oldScroll=std::clamp(scrollY_,0,oldMaxScroll);
            if(oldMaxScroll>0){
                libraryScrollFraction=static_cast<double>(oldScroll)/static_cast<double>(oldMaxScroll);
                libraryWasAtBottom=oldScroll>=oldMaxScroll-2;
            }
        }

        if(entering){
            preFullscreenLibraryCardWidth_=libraryCardWidth_;
            fullscreenLibraryZoomOverridden_=false;
            if(mode_==Mode::Details && category_==Category::Videos){
                // Fullscreen starts from its 10-across default without destroying a
                // custom windowed timeline size; restore that windowed state on exit.
                preFullscreenPreviewCardWidth_=previewCardWidth_;
                preFullscreenPreviewZoomOverridden_=previewZoomOverridden_;
                preFullscreenPreviewStateValid_=true;
                previewZoomOverridden_=false;
                previewWheelRemainder_=0;
            }
        }
        fullscreen_=entering;
        if(fullscreen_){
            savedStyle_=static_cast<DWORD>(GetWindowLongPtrW(hwnd_,GWL_STYLE));
            GetWindowRect(hwnd_,&savedRect_);
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&mi);
            const DWORD fsStyle=savedStyle_&~(WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_CAPTION);
            SetWindowLongPtrW(hwnd_,GWL_STYLE,fsStyle);
            SetWindowPos(hwnd_,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);
            if(mode_==Mode::Library){
                RECT libraryRc{}; GetClientRect(hwnd_,&libraryRc);
                ApplyLibraryWidthForViewport(std::max(1,static_cast<int>(libraryRc.right-libraryRc.left)));
            }
        }else{
            SetWindowLongPtrW(hwnd_,GWL_STYLE,savedStyle_);
            SetWindowPos(hwnd_,nullptr,savedRect_.left,savedRect_.top,savedRect_.right-savedRect_.left,savedRect_.bottom-savedRect_.top,SWP_FRAMECHANGED|SWP_NOZORDER);
            if(preFullscreenLibraryCardWidth_>0){
                libraryCardWidth_=preFullscreenLibraryCardWidth_;
                preFullscreenLibraryCardWidth_=-1;
            }
            fullscreenLibraryZoomOverridden_=false;
            if(mode_==Mode::Details && category_==Category::Videos){
                if(preFullscreenPreviewStateValid_){
                    previewCardWidth_=preFullscreenPreviewCardWidth_;
                    previewZoomOverridden_=preFullscreenPreviewZoomOverridden_;
                }else{
                    // If Details was entered while already fullscreen, windowed mode
                    // returns to the normal seven-across default.
                    previewZoomOverridden_=false;
                }
                previewWheelRemainder_=0;
                preFullscreenPreviewCardWidth_=-1;
                preFullscreenPreviewZoomOverridden_=false;
                preFullscreenPreviewStateValid_=false;
            }
        }
        ApplyMainWindowCornerPreference();
        if(!fullscreen_ && nativeVideoSizing_ && mode_==Mode::Player) ApplyNativeVideoWindowSize();
        if(!fullscreen_ && nativeImageSizing_ && mode_==Mode::Details && category_==Category::Images) ApplyNativeImageWindowSize();
        Layout();
        if(mode_==Mode::Library){
            RECT newLibraryRc{}; GetClientRect(hwnd_,&newLibraryRc);
            const int newMaxScroll=LibraryMaxScroll(newLibraryRc);
            if(libraryWasAtBottom) scrollY_=newMaxScroll;
            else scrollY_=std::clamp(static_cast<int>(std::lround(libraryScrollFraction*static_cast<double>(newMaxScroll))),0,newMaxScroll);
            UpdateLibraryScrollbarRects(newLibraryRc);
            PrimeVisibleLibraryThumbsFromPrivateCache();
        }
        if(mode_!=Mode::Player) InvalidateRect(hwnd_,nullptr,FALSE);
        InvalidateRect(hwnd_,nullptr,FALSE); InvalidateControls();
    }

    void UpdateWindowTitle() {
        if(mode_!=Mode::Player||selected_>=videos_.size())return;
        SetWindowTextW(hwnd_,(L"Visual MediaPlayer - "+videos_[selected_].title).c_str());
    }

    HINSTANCE inst_{}; HWND hwnd_{}; HWND playerPrevHwnd_{},playerNextHwnd_{}; Mode mode_=Mode::Library; Category category_=Category::Videos;
    std::wstring folder_; std::wstring persistentFolder_; std::wstring currentFolder_; std::wstring detailsOriginFolder_; std::vector<MediaItem> videos_,images_; std::vector<LibraryFolder> folders_; bool externalMediaSession_=false; std::vector<std::wstring> externalMediaPaths_,pendingExternalMediaPaths_; size_t selected_=0; int scrollY_=0; int detailsScrollY_=0; int detailsContentBottom_=0; double libraryScrollWheelPixelRemainder_=0.0,detailsScrollWheelPixelRemainder_=0.0; int previewCardWidth_=kDefaultPreviewCardWidth; int previewWheelRemainder_=0; bool previewZoomOverridden_=false; int preFullscreenPreviewCardWidth_=-1; bool preFullscreenPreviewZoomOverridden_=false; bool preFullscreenPreviewStateValid_=false; int libraryCardWidth_=kDefaultLibraryCardWidth; int preFullscreenLibraryCardWidth_=-1; bool fullscreenLibraryZoomOverridden_=false;
    std::map<std::wstring,FolderViewState> folderViewStates_;
    std::vector<std::wstring> knownLibraryRoots_;
    std::wstring searchQuery_; bool searchVisible_=false; bool searchSelectAll_=false; bool filterDirty_=true; std::vector<size_t> filteredIndices_;
    bool detailsSearchNavigationActive_=false; std::vector<size_t> detailsSearchNavigationIndices_;
    bool slideshowActive_=false; size_t slideshowPos_=0; std::vector<size_t> slideshowIndices_;
    bool slideshowFadeActive_=false; size_t slideshowPreviousIndex_=static_cast<size_t>(-1); ULONGLONG slideshowFadeStart_=0;
    HWND paintOwner_=nullptr; HWND hoverOwner_=nullptr; HWND hoverPreviousOwner_=nullptr; RECT hoverRect_{},hoverPreviousRect_{}; ULONGLONG hoverTransitionStart_=0;
    std::vector<AnimatedMediaHit> libraryMediaHoverHits_,previewMediaHoverHits_;
    MediaHoverSurface mediaHoverSurface_=MediaHoverSurface::None; size_t mediaHoverId_=static_cast<size_t>(-1); RECT mediaHoverRect_{}; ULONGLONG mediaHoverStart_=0;
    Category libraryReturnHighlightCategory_=Category::Videos; size_t libraryReturnHighlightIndex_=static_cast<size_t>(-1); ULONGLONG libraryReturnHighlightStart_=0; RECT libraryReturnHighlightRect_{};
    std::unique_ptr<Gdiplus::Bitmap> folderIconBitmap_,refreshIconBitmap_,skipBackIconBitmap_,skipForwardIconBitmap_,downloadIconBitmap_,resolution4kBitmap_,resolution5kBitmap_,resolution8kBitmap_,vrBadgeBitmap_,vrBadgeWhiteBitmap_,resizeIconBitmap_,resizeIconWhiteBitmap_,favoriteIconBitmap_;
    RECT chooseRect_{},rescanRect_{},loadEverythingRect_{},categoryToggleRect_{},mediaCountRect_{},slideshowRect_{},imageDetailsSlideshowRect_{},imageDetailsNativeRect_{},backRect_{},playRect_{},searchBoxRect_{},libraryFooterRect_{},detailsFooterRect_{},libraryFullRect_{},detailsFullRect_{},previewZoomRect_{},detailsMediaRect_{},detailsPrevRect_{},detailsNextRect_{};
    RECT libraryScrollTrackRect_{}, libraryScrollThumbRect_{}; bool libraryScrollDragging_=false; int libraryScrollDragOffset_=0;
    std::map<uint64_t,HFONT> fontCache_; HDC backDC_{}; HBITMAP backBitmap_{}; HGDIOBJ backOldBitmap_{}; int backW_=0,backH_=0;
    ComPtr<ID2D1Factory> libraryD2dFactory_; ComPtr<ID2D1HwndRenderTarget> libraryD2dTarget_; ComPtr<IWICImagingFactory> libraryWicFactory_; ComPtr<IDWriteFactory> libraryDWriteFactory_;
    std::map<uint64_t,ComPtr<IDWriteTextFormat>> libraryDWriteFormats_;
    ComPtr<ID2D1SolidColorBrush> libraryD2dCardBrush_,libraryD2dPlaceholderBrush_,libraryD2dUiBrush_;
    ComPtr<ID2D1Bitmap> libraryD2dFavoriteIcon_,libraryD2dVrIcon_,libraryD2dResolution4k_,libraryD2dResolution5k_,libraryD2dResolution8k_,libraryD2dFolderIcon_,libraryD2dRefreshIcon_,libraryD2dDownloadIcon_,libraryD2dResizeIcon_,libraryD2dResizeIconWhite_;
    uint64_t libraryD2dGeneration_=1; int libraryD2dWidth_=0,libraryD2dHeight_=0; bool detailsGpuWorkingSetActive_=false;
    HWND videoHwnd_{},controlsHwnd_{}; std::unique_ptr<NativePlayer> player_;
    BYTE controlsAlpha_=0,controlsFadeFrom_=0,controlsFadeTo_=0; ULONGLONG controlsFadeStart_=0; bool controlsFading_=false;
    HWND playerFooterTransitionHwnd_{}; HBITMAP playerFooterTransitionBitmap_{}; ULONGLONG playerFooterTransitionStart_=0;
    RECT playerBackRect_{},playerVrToggleRect_{},playerSkipBackRect_{},playerPlayRect_{},playerSkipForwardRect_{},playerFullRect_{},playerNativeSizeRect_{},playerAutoNextRect_{},seekRect_{},volumeRect_{},volumeLabelRect_{},playerTimeRect_{};
    RECT nativeSizingRestoreRect_{}; bool nativeSizingRestoreRectValid_=false;
    RECT nativeImageSizingRestoreRect_{}; bool nativeImageSizingRestoreRectValid_=false;
    float imageZoomScale_=1.0f,imageZoomCenterU_=0.5f,imageZoomCenterV_=0.5f; bool imageZoomDragging_=false; POINT imageZoomLastPoint_{};
    double seekFraction_=0.0,volumeFraction_=0.30; bool fullscreen_=false,seekDragging_=false,volumeDragging_=false,autoNext_=false,nativeVideoSizing_=false,nativeImageSizing_=false,playerControlsVisible_=false;
    bool seekHoverVisible_=false; int seekHoverX_=0;
    ULONGLONG controlsHideDeadline_=0; POINT lastCursorScreen_{}; bool lastCursorValid_=false; DWORD savedStyle_{}; RECT savedRect_{};
    bool comInitialized_=false,mfStarted_=false; ULONG_PTR gdiplusToken_=0; std::thread thumbThread_; std::atomic<bool> thumbStop_{false}; std::atomic<bool> thumbWorkerRunning_{false}; std::atomic<bool> thumbRepairRequested_{false};
    std::vector<std::thread> libraryThumbLoadThreads_; std::atomic<bool> libraryThumbLoadStop_{false};
    std::mutex libraryThumbLoadMutex_; std::condition_variable libraryThumbLoadCv_;
    std::deque<LibraryThumbLoadJob> libraryThumbLoadJobs_; std::vector<LibraryThumbLoadResult> libraryThumbLoadResults_;
    std::atomic<bool> libraryThumbResultMessagePending_{false}; std::atomic<uint64_t> libraryThumbViewEpoch_{1};
    int libraryThumbViewportScrollY_=-1,libraryThumbViewportCardWidth_=-1,libraryThumbViewportClientWidth_=-1,libraryThumbPrefetchDirection_=1;
    Category libraryThumbViewportCategory_=Category::Videos; std::wstring libraryThumbViewportFolder_,libraryThumbViewportSearch_;
    std::set<std::wstring> protectedLibraryThumbPaths_,visibleLibraryGpuThumbPaths_,playbackLibraryWarmPaths_;
    std::thread resolutionMetadataThread_; std::atomic<bool> resolutionMetadataStop_{false};
    std::mutex resolutionMetadataMutex_,resolutionMetadataCacheMutex_; std::condition_variable resolutionMetadataCv_;
    std::deque<ResolutionMetadataJob> resolutionMetadataJobs_; std::set<std::wstring> resolutionMetadataPendingPaths_;
    std::atomic<uint64_t> resolutionMetadataGeneration_{1};
    std::atomic<ULONGLONG> backgroundPauseUntil_{0}; ULONGLONG lastMemoryPressureCheck_=0;
    int libraryAccessFailCount_=0; bool libraryUnavailableLatched_=false; bool libraryAccessRetryNeedsRescan_=false; ULONGLONG libraryAccessMonitorUntil_=0;
    bool liveWindowMove_=false;
    std::wstring appNoticeText_; ULONGLONG appNoticeStart_=0,appNoticeUntil_=0;
    std::atomic<int> loadingKind_{0}, loadingCurrent_{0}, loadingTotal_{0};
    std::thread fullLoadThread_; std::atomic<bool> fullLoadStop_{false},fullLoadRunning_{false};
    std::atomic<int> fullLoadCurrent_{0},fullLoadTotal_{0},fullLoadFailures_{0}; ULONGLONG fullLoadFinishedAt_=0;
    std::mutex generationClaimMutex_; std::set<std::wstring> generationClaims_;
    std::wstring previewDir_,previewMediaPath_; std::vector<PreviewFrame> previewFrames_; std::map<std::wstring,PrefetchedPreviewSet> prefetchedPreviewSets_; std::vector<std::pair<RECT,int>> previewHitRects_; std::thread previewThread_; std::atomic<bool> previewStop_{false}; std::atomic<double> detailsDurationSeconds_{0.0};
    std::thread detailPrefetchThread_; std::mutex detailPrefetchMutex_; std::condition_variable detailPrefetchCv_; std::vector<DetailPrefetchJob> detailPrefetchJobs_; bool detailPrefetchStop_=false; std::atomic<uint64_t> detailPrefetchGeneration_{0};
};

static std::vector<std::wstring> GetExternalCommandLineMediaPaths() {
    std::vector<std::wstring> paths;
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv){
        for(int i=1;i<argc;++i) if(argv[i] && *argv[i]) paths.emplace_back(argv[i]);
        LocalFree(argv);
    }
    return paths;
}

static bool SendExternalMediaToRunningInstance(const std::vector<std::wstring>& paths) {
    HWND target=nullptr;
    for(int attempt=0;attempt<50 && !target;++attempt){
        target=FindWindowW(L"VisualMediaPlayerMain",nullptr);
        if(!target) Sleep(100);
    }
    if(!target) return false;
    if(!paths.empty()){
        std::vector<wchar_t> payload;
        size_t total=1;
        for(const auto& path:paths) total+=path.size()+1;
        payload.reserve(total);
        for(const auto& path:paths){ payload.insert(payload.end(),path.begin(),path.end()); payload.push_back(L'\0'); }
        payload.push_back(L'\0');
        COPYDATASTRUCT cds{};
        cds.dwData=0x564D5031ull; // "VMP1"
        cds.cbData=static_cast<DWORD>(payload.size()*sizeof(wchar_t));
        cds.lpData=payload.data();
        SendMessageTimeoutW(target,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&cds),SMTO_ABORTIFHUNG,5000,nullptr);
    }
    ShowWindow(target,SW_RESTORE);
    SetForegroundWindow(target);
    return true;
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::vector<std::wstring> externalPaths=GetExternalCommandLineMediaPaths();

    HANDLE instanceMutex=CreateMutexW(nullptr,FALSE,L"Local\\VisualMediaPlayer.SingleInstance.v1");
    const bool anotherInstance=instanceMutex && GetLastError()==ERROR_ALREADY_EXISTS;
    if(anotherInstance && SendExternalMediaToRunningInstance(externalPaths)){
        if(instanceMutex) CloseHandle(instanceMutex);
        return 0;
    }

    App app;
    if(!app.Initialize(hInst)){
        if(instanceMutex) CloseHandle(instanceMutex);
        return 1;
    }
    if(!externalPaths.empty()) app.QueueExternalMediaOpen(externalPaths);
    const int result=app.Run();
    if(instanceMutex) CloseHandle(instanceMutex);
    return result;
}
