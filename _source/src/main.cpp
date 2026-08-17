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
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <map>
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
#pragma comment(lib, "msimg32.lib")

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

static constexpr UINT WM_APP_MEDIA_EVENT = WM_APP + 1;
static constexpr UINT WM_APP_SEEK_COMMIT = WM_APP + 2;
static constexpr UINT WM_APP_PLAYER_READY = WM_APP + 3;
static constexpr float PI_F = 3.14159265358979323846f;

static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

static bool IsVideoExtension(const std::wstring& extRaw) {
    const std::wstring ext = ToLower(extRaw);
    static const wchar_t* kExts[] = {
        L".mp4", L".mkv", L".mov", L".m4v", L".avi", L".webm", L".wmv", L".mts", L".m2ts"
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

static bool EndsWith360Marker(const std::wstring& file) {
    std::wstring stem = ToLower(fs::path(file).stem().wstring());
    while (!stem.empty() && iswspace(stem.back())) stem.pop_back();

    if (stem.size() >= 3 && stem.compare(stem.size() - 3, 3, L"360") == 0) return true;

    // Also accept names ending in: 360 (1), 360 (12), 360 (123), ...
    // The text inside the final parentheses must be numeric and the marker must be the suffix.
    if (!stem.empty() && stem.back() == L')') {
        const size_t marker = stem.rfind(L"360 (");
        if (marker != std::wstring::npos && marker + 5 < stem.size() - 1) {
            const size_t digitsBegin = marker + 5;
            const size_t digitsEnd = stem.size() - 1;
            bool allDigits = digitsBegin < digitsEnd;
            for (size_t i = digitsBegin; i < digitsEnd && allDigits; ++i) {
                if (!iswdigit(stem[i])) allDigits = false;
            }
            if (allDigits) return true;
        }
    }
    return false;
}

static VRInfo DetectVR(const std::wstring& file) {
    const std::wstring n = ToLower(fs::path(file).filename().wstring());
    VRInfo v;
    const bool has360 = EndsWith360Marker(file);
    const bool has180 = n.find(L"vr180") != std::wstring::npos || n.find(L"180vr") != std::wstring::npos;
    const bool hasVr = n.find(L"vr") != std::wstring::npos || has360 || has180;
    if (!hasVr) return v;

    v.vr = true;
    v.projection = has180 ? 2 : 1;

    const bool tb = n.find(L" top bottom") != std::wstring::npos ||
                    n.find(L"top-bottom") != std::wstring::npos ||
                    n.find(L"_tb") != std::wstring::npos ||
                    n.find(L" tb") != std::wstring::npos ||
                    n.find(L"_ou") != std::wstring::npos ||
                    n.find(L" ou") != std::wstring::npos;
    const bool sbs = n.find(L"sbs") != std::wstring::npos ||
                     n.find(L"side by side") != std::wstring::npos ||
                     n.find(L"side-by-side") != std::wstring::npos ||
                     n.find(L"_lr") != std::wstring::npos ||
                     n.find(L" lr") != std::wstring::npos;

    if (tb) { v.layout = 2; v.layoutExplicit = true; }
    else if (sbs) { v.layout = 1; v.layoutExplicit = true; }
    else v.layout = 0; // projection is automatic; stereo packing is resolved from aspect ratio later

    // Stereo-packed VR should default to front-facing 180 playback.
    // Only true mono panoramas remain 360.
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
    bool thumbAttempted = false;
    bool thumbFromPrivateCache = false;
    ULONGLONG thumbLastUsed = 0;
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
        if (!engine_) return E_FAIL;
        vrInfo_ = vr;
        // VR always opens in the standard front-only 180 mode.  The 360 state is
        // user-enabled only, so the button stays dark until the user turns 360 on.
        projectionOverride_ = vr.vr ? 2 : 0;
        layoutDetectionDone_ = vr.layoutExplicit;
        layoutDetectionPending_ = false;
        layoutDetectionAttempts_ = 0;
        pendingStartSeconds_ = startSeconds;
        yaw_ = 0.0f;
        pitch_ = 0.0f;
        fovRadians_ = 65.0f * PI_F / 180.0f;
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

    void Shutdown() {
        if (engine_) engine_->Shutdown();
        engine_.Reset();
        notify_.Reset();
        dxgiManager_.Reset();
        videoSRV_.Reset();
        videoTexture_.Reset();
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
            MessageBoxW(eventWindow_, L"Media Foundation could not decode this file with the codecs installed in Windows.", L"Playback error", MB_ICONERROR);
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
                    if (detected >= 0) {
                        vrInfo_.layout = detected;
                        if (vrInfo_.layout != 0) vrInfo_.projection = 2;
                        layoutDetectionDone_ = true;
                        layoutDetectionPending_ = false;
                        yaw_ = 0.0f;
                        pitch_ = 0.0f;
                        EnsureVideoTexture();
                    } else if (++layoutDetectionAttempts_ >= 3) {
                        // If the tiny probe cannot be read, preserve safe mono behavior.
                        layoutDetectionDone_ = true;
                        layoutDetectionPending_ = false;
                        vrInfo_.layout = 0;
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
                    engine_->TransferVideoFrame(videoTexture_.Get(), &src, &dst, &border);
                }
            }
        }

        const float clear[4] = {0.008f, 0.010f, 0.014f, 1.0f};
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        context_->ClearRenderTargetView(renderTarget_.Get(), clear);

        if (videoSRV_) {
            RECT rc{}; GetClientRect(videoWindow_, &rc);
            D3D11_VIEWPORT vp{};
            vp.Width = static_cast<float>(std::max<LONG>(1L, rc.right - rc.left));
            vp.Height = static_cast<float>(std::max<LONG>(1L, rc.bottom - rc.top));
            vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
            context_->RSSetViewports(1, &vp);

            ShaderConstants c{};
            c.yaw = yaw_;
            c.pitch = pitch_;
            c.fov = fovRadians_;
            c.vrMode = vrInfo_.vr ? 1.0f : 0.0f;
            c.layout = 0.0f; // stereo eye has already been unpacked into videoTexture_
            c.projection = static_cast<float>(EffectiveProjection());
            c.sourceAspect = eyeH_ ? static_cast<float>(eyeW_) / static_cast<float>(eyeH_) : 1.f;
            c.viewportAspect = vp.Height > 0.f ? vp.Width / vp.Height : 1.f;
            c.mirrorBack = IsMirroredBack360() ? 1.0f : 0.0f;
            context_->UpdateSubresource(constantBuffer_.Get(), 0, nullptr, &c, 0, 0);

            UINT stride = sizeof(Vertex), offset = 0;
            context_->IASetInputLayout(inputLayout_.Get());
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
            context_->VSSetShader(vs_.Get(), nullptr, 0);
            context_->PSSetShader(ps_.Get(), nullptr, 0);
            context_->PSSetShaderResources(0, 1, videoSRV_.GetAddressOf());
            context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
            context_->PSSetConstantBuffers(0, 1, constantBuffer_.GetAddressOf());
            context_->Draw(6, 0);
            ID3D11ShaderResourceView* nullSrv = nullptr;
            context_->PSSetShaderResources(0, 1, &nullSrv);
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
    int ResolvedLayout() const { return vrInfo_.layout; }
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
        if (!vrInfo_.vr) return;
        dragging_ = true; lastX_ = x; lastY_ = y; SetCapture(videoWindow_);
    }
    void Drag(int x, int y) {
        if (!dragging_ || !vrInfo_.vr) return;
        const int dx = x - lastX_, dy = y - lastY_;
        lastX_ = x; lastY_ = y;
        // VR mouse-look: keep the approved horizontal direction, with gentler vertical movement.
        yaw_ -= dx * 0.0032f;
        // Vertical direction is intentionally opposite to the horizontal grab direction.
        pitch_ = std::clamp(pitch_ - dy * 0.0026f, -1.48f, 1.48f);
    }
    void EndDrag() {
        if (!dragging_) return;
        dragging_ = false; ReleaseCapture();
    }
    void Wheel(short delta) {
        if (!vrInfo_.vr) return;
        float deg = fovRadians_ * 180.f / PI_F;
        deg = std::clamp(deg - (delta / 120.f) * 5.f, 35.f, 110.f);
        fovRadians_ = deg * PI_F / 180.f;
    }

private:
    struct Vertex { float x,y,u,v; };
    struct ShaderConstants {
        float yaw, pitch, fov, vrMode;
        float layout, projection, sourceAspect, viewportAspect;
        float mirrorBack, pad0, pad1, pad2;
    };

    int DetectPackedStereoFromCurrentFrame() {
        if (!engine_ || !device_ || !context_ || !nativeW_ || !nativeH_) return -1;

        // Keep the probe tiny.  Video decoding is the expensive part; comparing this
        // small BGRA image is effectively free and happens only once for ambiguous VR.
        const float aspect = static_cast<float>(nativeW_) / static_cast<float>(nativeH_);
        UINT probeW = 256u;
        UINT probeH = static_cast<UINT>(std::clamp<int>(static_cast<int>(std::lround(256.0f / std::max(0.01f, aspect))), 64, 256));
        if (aspect < 1.0f) {
            probeH = 256u;
            probeW = static_cast<UINT>(std::clamp<int>(static_cast<int>(std::lround(256.0f * aspect)), 64, 256));
        }
        probeW = std::max<UINT>(4u, probeW & ~1u);
        probeH = std::max<UINT>(4u, probeH & ~1u);

        D3D11_TEXTURE2D_DESC td{};
        td.Width = probeW;
        td.Height = probeH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;

        ComPtr<ID3D11Texture2D> gpu;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &gpu))) return -1;

        MFVideoNormalizedRect src{0.f, 0.f, 1.f, 1.f};
        RECT dst{0, 0, static_cast<LONG>(probeW), static_cast<LONG>(probeH)};
        MFARGB border{0,0,0,255};
        if (FAILED(engine_->TransferVideoFrame(gpu.Get(), &src, &dst, &border))) return -1;

        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&sd, nullptr, &staging))) return -1;
        context_->CopyResource(staging.Get(), gpu.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return -1;

        auto pixelDiff = [](const BYTE* a, const BYTE* b) -> double {
            // BGRA: alpha is irrelevant.
            return static_cast<double>(std::abs(static_cast<int>(a[0]) - static_cast<int>(b[0])) +
                                       std::abs(static_cast<int>(a[1]) - static_cast<int>(b[1])) +
                                       std::abs(static_cast<int>(a[2]) - static_cast<int>(b[2]))) / 3.0;
        };

        double lrTotal = 0.0, tbTotal = 0.0;
        int lrCount = 0, tbCount = 0;
        constexpr int samplesX = 12, samplesY = 8;
        const UINT halfW = probeW / 2u;
        const UINT halfH = probeH / 2u;

        for (int gy = 0; gy < samplesY; ++gy) {
            const UINT yFull = std::min<UINT>(probeH - 1u, static_cast<UINT>((gy + 0.5) * probeH / samplesY));
            const UINT yHalf = std::min<UINT>(halfH - 1u, static_cast<UINT>((gy + 0.5) * halfH / samplesY));
            for (int gx = 0; gx < samplesX; ++gx) {
                const UINT xHalf = std::min<UINT>(halfW - 1u, static_cast<UINT>((gx + 0.5) * halfW / samplesX));
                const UINT xFull = std::min<UINT>(probeW - 1u, static_cast<UINT>((gx + 0.5) * probeW / samplesX));

                const BYTE* lrA = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(yFull) * mapped.RowPitch + static_cast<size_t>(xHalf) * 4u;
                const BYTE* lrB = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(yFull) * mapped.RowPitch + static_cast<size_t>(xHalf + halfW) * 4u;
                lrTotal += pixelDiff(lrA, lrB);
                ++lrCount;

                const BYTE* tbA = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(yHalf) * mapped.RowPitch + static_cast<size_t>(xFull) * 4u;
                const BYTE* tbB = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(yHalf + halfH) * mapped.RowPitch + static_cast<size_t>(xFull) * 4u;
                tbTotal += pixelDiff(tbA, tbB);
                ++tbCount;
            }
        }
        context_->Unmap(staging.Get(), 0);

        const double lr = lrCount ? lrTotal / lrCount : 1e9;
        const double tb = tbCount ? tbTotal / tbCount : 1e9;
        constexpr double kStereoThreshold = 52.0;
        if (lr < kStereoThreshold && lr + 5.0 < tb) return 1; // SBS: keep left eye
        if (tb < kStereoThreshold && tb + 5.0 < lr) return 2; // TB: keep top eye
        if (lr < 38.0) return 1;
        if (tb < 38.0) return 2;
        return 0; // true mono panorama
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
        if (abs(p.x) > sx || abs(p.y) > sy) return float4(0,0,0,1);
        float2 uv = float2(p.x / sx, p.y / sy) * 0.5 + 0.5;
        return tex0.Sample(samp0, uv);
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
        if (abs(lon) > 1.57079632679) return float4(0,0,0,1);
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

    return tex0.Sample(samp0, uv);
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
        return device_->CreateSamplerState(&sd, &sampler_);
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
    ComPtr<ID3D11Texture2D> videoTexture_;
    ComPtr<ID3D11ShaderResourceView> videoSRV_;
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
    int projectionOverride_ = 0; // 0=automatic, 1=force 360, 2=force front-only 180
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    float yaw_ = 0.f, pitch_ = 0.f, fovRadians_ = 65.f * PI_F / 180.f;
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

    static int FourAcrossPreviewMaxWidth(int clientWidth) {
        // Same rule for Info secondary previews: maximum zoom still shows four
        // preview cards side-by-side in the current window.
        constexpr int sideMargins = 80;
        constexpr int previewGap = 12;
        constexpr int minPreviewWidth = 140;
        const int usable = std::max(1, clientWidth - sideMargins - previewGap * 3);
        return std::max(minPreviewWidth, usable / 4);
    }

    enum class Mode { Library, Details, Search, Player };
    enum class Category { Videos, Images };

    struct SearchHit {
        Category category = Category::Videos;
        size_t index = 0;
    };

    bool Initialize(HINSTANCE inst) {
        inst_ = inst;
        INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_BAR_CLASSES}; InitCommonControlsEx(&ic);
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) return false;
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) return false;

        Gdiplus::GdiplusStartupInput gdiplusInput;
        if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusInput, nullptr) != Gdiplus::Ok) return false;

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

        // Open large by default: target a 2560x1440 (QHD/2K) window, but never exceed the usable desktop.
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const int workW = std::max(900, static_cast<int>(workArea.right - workArea.left));
        const int workH = std::max(600, static_cast<int>(workArea.bottom - workArea.top));
        const int initialW = std::min(2560, workW);
        const int initialH = std::min(1440, workH);
        const int initialX = workArea.left + std::max(0, (workW - initialW) / 2);
        const int initialY = workArea.top + std::max(0, (workH - initialH) / 2);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Visual MediaPlayer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            initialX, initialY, initialW, initialH, nullptr, nullptr, inst_, this);
        if (!hwnd_) return false;
        HICON appIconBig = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        HICON appIconSmall = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (appIconBig) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIconBig));
        if (appIconSmall) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIconSmall));
        BOOL darkTitle = TRUE;
        DwmSetWindowAttribute(hwnd_, 20, &darkTitle, sizeof(darkTitle));
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        // Register this exact EXE path as an "Open with Visual MediaPlayer" handler.
        // Windows still lets the user choose whether it becomes the default app.
        RegisterOpenWith();

        LoadSettings();
        if (!folder_.empty() && fs::exists(folder_)) Scan();
        return true;
    }

    int Run() {
        MSG msg{};
        while (true) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
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
        if (rawPath.empty()) return false;

        std::error_code ec;
        fs::path mediaPath = fs::path(rawPath);
        if (mediaPath.is_relative()) mediaPath = fs::absolute(mediaPath, ec);
        if (ec) { ec.clear(); mediaPath = fs::path(rawPath); }
        mediaPath = mediaPath.lexically_normal();
        if (!fs::exists(mediaPath, ec) || ec || !fs::is_regular_file(mediaPath, ec) || ec) return false;

        const std::wstring ext = mediaPath.extension().wstring();
        const bool isVideo = IsVideoExtension(ext);
        const bool isImage = IsImageExtension(ext);
        if (!isVideo && !isImage) return false;

        const std::wstring targetPath = mediaPath.wstring();
        const std::wstring targetFolder = mediaPath.parent_path().lexically_normal().wstring();

        StopImageSlideshow();
        searchQuery_.clear();
        searchVisible_ = false;
        searchScrollY_ = 0;
        filteredIndices_.clear();
        filterDirty_ = true;

        // If the external file lives inside the user's saved library, keep that library
        // root and simply navigate to the file's containing folder. Otherwise use the
        // file's folder for this session without replacing the saved Library setting.
        if (folder_.empty() || !PathIsWithin(targetPath, folder_)) {
            folder_ = targetFolder;
            Scan();
        } else if (videos_.empty() && images_.empty()) {
            Scan();
        }
        currentFolder_ = targetFolder;
        detailsOriginFolder_ = targetFolder;
        scrollY_ = 0;
        detailsScrollY_ = 0;
        ResetPreviewZoom();
        ResetLibraryZoom();

        const std::wstring targetKey = ToLower(mediaPath.lexically_normal().wstring());
        if (isVideo) {
            category_ = Category::Videos;
            for (size_t i = 0; i < videos_.size(); ++i) {
                if (ToLower(fs::path(videos_[i].path).lexically_normal().wstring()) == targetKey) {
                    selected_ = i;
                    mode_ = Mode::Details;
                    EnterPlayerAt(0.0);
                    return mode_ == Mode::Player;
                }
            }
        } else {
            category_ = Category::Images;
            for (size_t i = 0; i < images_.size(); ++i) {
                if (ToLower(fs::path(images_[i].path).lexically_normal().wstring()) == targetKey) {
                    selected_ = i;
                    mode_ = Mode::Details;
                    InvalidateRect(hwnd_, nullptr, TRUE);
                    return true;
                }
            }
        }
        return false;
    }

    ~App() {
        StopPreviewWorker();
        StopThumbnailWorker();
        ClearPreviewBitmaps();
        ClearThumbs(videos_);
        ClearThumbs(images_);
        for(auto& kv:fontCache_) if(kv.second) DeleteObject(kv.second);
        fontCache_.clear();
        DestroyBackBuffer();
        player_.reset();
        if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
        MFShutdown();
        CoUninitialize();
    }

private:
    struct ThumbJob {
        std::wstring source;
        std::wstring output;
        std::wstring uiOutput;
        bool isVideo = true;
        VRInfo vr{};
    };

    static constexpr UINT WM_APP_THUMB_READY = WM_APP + 10;
    static constexpr UINT WM_APP_PREVIEW_READY = WM_APP + 11;
    static constexpr UINT_PTR kSlideshowTimerId = 41;
    static constexpr UINT_PTR kUiAnimationTimerId = 42;
    static constexpr ULONGLONG kUiAnimationDurationMs = 160;
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
            L".mp4", L".mkv", L".mov", L".m4v", L".avi", L".webm", L".wmv", L".mts", L".m2ts",
            L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tif", L".tiff", L".webp", L".heic", L".heif", L".avif"
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
            app->PlayerActivity(true);
            if (app->player_) app->player_->BeginDrag(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_MOUSEMOVE:
            app->PlayerActivity(false);
            if (app->player_) app->player_->Drag(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONUP:
            if (app->player_) app->player_->EndDrag();
            return 0;
        case WM_MOUSEWHEEL:
            app->PlayerActivity(true);
            if (app->player_) app->player_->Wheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        case WM_KEYDOWN: SendMessageW(app->hwnd_, WM_KEYDOWN, w, l); return 0;
        case WM_SIZE: if (app->player_) app->player_->Resize(); return 0;
        }
        return DefWindowProcW(h,m,w,l);
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
            app->PlayerActivity(true);
            app->PlayerMouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONUP:
            app->PlayerMouseUp(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_MOUSEWHEEL:
            app->PlayerActivity(true);
            if (app->player_) app->player_->Wheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        }
        return DefWindowProcW(h,m,w,l);
    }

    LRESULT HandleMain(UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_CREATE: return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOVE: if(mode_==Mode::Player) Layout(); return 0;
        case WM_SIZE:
            if(w==SIZE_MINIMIZED && controlsHwnd_) ShowWindow(controlsHwnd_,SW_HIDE);
            if (w != SIZE_MINIMIZED) {
                RECT zoomRc{}; GetClientRect(hwnd_, &zoomRc);
                const int clientW = std::max(1, static_cast<int>(zoomRc.right - zoomRc.left));
                if (mode_ == Mode::Library)
                    libraryCardWidth_ = std::min(libraryCardWidth_, FourAcrossLibraryMaxWidth(clientW));
                else if (mode_ == Mode::Details && category_ == Category::Videos)
                    previewCardWidth_ = std::min(previewCardWidth_, FourAcrossPreviewMaxWidth(clientW));
            }
            Layout(); InvalidateRect(hwnd_, nullptr, TRUE); return 0;
        case WM_PAINT: Paint(); return 0;
        case WM_DPICHANGED: {
            RECT* suggested = reinterpret_cast<RECT*>(l);
            if (suggested) SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right-suggested->left, suggested->bottom-suggested->top, SWP_NOZORDER|SWP_NOACTIVATE);
            Layout(); InvalidateRect(hwnd_, nullptr, TRUE); return 0;
        }
        case WM_MOUSEMOVE:
            {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                UpdateAnimatedHover(hwnd_, GET_X_LPARAM(l), GET_Y_LPARAM(l));
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
                scrollY_ = std::clamp(static_cast<int>(fraction * maxScroll + 0.5), 0, maxScroll);
                UpdateLibraryScrollbarRects(rc);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (mode_ == Mode::Player) PlayerActivity(false);
            break;
        case WM_MOUSELEAVE:
            ClearAnimatedHover(hwnd_);
            return 0;
        case WM_MOUSEWHEEL:
            if (mode_ == Mode::Library) {
                const int wheelSteps = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
                if ((GET_KEYSTATE_WPARAM(w) & MK_CONTROL) != 0) {
                    // Ctrl + wheel resizes the Library cards for this Library session only.
                    // The size is deliberately not persisted and is reset when Library is left.
                    if (wheelSteps != 0) {
                        RECT rc{}; GetClientRect(hwnd_, &rc);
                        const int maxWidth = FourAcrossLibraryMaxWidth(static_cast<int>(rc.right - rc.left));
                        const int oldWidth = libraryCardWidth_;
                        libraryCardWidth_ = std::clamp(libraryCardWidth_ + wheelSteps * 36, kMinLibraryCardWidth, maxWidth);
                        if (libraryCardWidth_ != oldWidth) {
                            ClampScroll();
                            InvalidateRect(hwnd_, nullptr, FALSE);
                        }
                    }
                } else {
                    scrollY_ -= wheelSteps * 120;
                    ClampScroll(); InvalidateRect(hwnd_, nullptr, FALSE);
                }
            } else if (mode_ == Mode::Details) {
                // Secondary-preview zoom is Ctrl + wheel only. The cursor can still be
                // anywhere over the foreground app, including while previews are loading.
                POINT screenPoint{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                RECT windowRect{};
                GetWindowRect(hwnd_, &windowRect);
                const bool appIsForeground = GetForegroundWindow() == hwnd_;
                const bool pointerOverApp = PtInRect(&windowRect, screenPoint) != FALSE;
                const bool ctrlDown = (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) != 0;

                if (category_ == Category::Videos && appIsForeground && pointerOverApp && ctrlDown) {
                    const int wheelSteps = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
                    if (wheelSteps != 0) {
                        RECT rc{}; GetClientRect(hwnd_, &rc);
                        const int maxWidth = FourAcrossPreviewMaxWidth(static_cast<int>(rc.right - rc.left));
                        const int oldWidth = previewCardWidth_;
                        previewCardWidth_ = std::clamp(previewCardWidth_ + wheelSteps * 28, 140, maxWidth);
                        if (previewCardWidth_ != oldWidth) {
                            ClampDetailsScroll();
                            InvalidateRect(hwnd_, nullptr, FALSE);
                        }
                    }
                } else {
                    detailsScrollY_ -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 120;
                    ClampDetailsScroll(); InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (mode_ == Mode::Library) {
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
                    scrollY_ = std::clamp(static_cast<int>(fraction * maxScroll + 0.5), 0, maxScroll);
                    UpdateLibraryScrollbarRects(rc);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return 0;
                }
            }
            break;
        case WM_LBUTTONUP:
            if (libraryScrollDragging_) {
                libraryScrollDragging_ = false;
                if (GetCapture() == hwnd_) ReleaseCapture();
                return 0;
            }
            if (mode_ != Mode::Player) { HandleClick(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0; }
            break;
        case WM_CAPTURECHANGED:
            libraryScrollDragging_ = false;
            break;
        case WM_APP_MEDIA_EVENT: {
            const DWORD ev = static_cast<DWORD>(w);
            if (player_) player_->HandleMediaEvent(ev);
            if (ev == MF_MEDIA_ENGINE_EVENT_ENDED) HandlePlaybackEnded();
            return 0;
        }
        case WM_APP_PLAYER_READY: UpdateWindowTitle(); return 0;
        case WM_APP_THUMB_READY:
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_APP_PREVIEW_READY:
            if (mode_ == Mode::Details && category_ == Category::Videos) {
                RefreshPreviewFrames();
                ClampDetailsScroll();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_CHAR:
            if (mode_ == Mode::Library && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                const wchar_t ch = static_cast<wchar_t>(w);
                if (ch == 8) {
                    if (searchVisible_ && !searchQuery_.empty()) {
                        searchQuery_.pop_back(); filterDirty_ = true; scrollY_ = 0; ClampScroll(); InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                    return 0;
                }
                if (ch >= 32 && ch != 127) {
                    searchVisible_ = true;
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
            break;
        case WM_KEYDOWN:
            if (searchVisible_ && mode_ != Mode::Player && w == VK_ESCAPE) {
                // Close search without changing the folder.  Force a fresh local-folder
                // filter next time Library is painted; never reuse stale search results.
                searchQuery_.clear();
                searchVisible_ = false;
                filteredIndices_.clear();
                filterDirty_ = true;
                scrollY_ = 0;
                if (mode_ == Mode::Library) ClampScroll();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (searchVisible_ && mode_ == Mode::Library && w == VK_RETURN) {
                const auto& filtered = FilteredIndices();
                if (!filtered.empty()) { ResetLibraryZoom(); detailsOriginFolder_ = currentFolder_; selected_ = filtered.front(); mode_ = Mode::Details; detailsScrollY_ = 0; StartPreviewWorkerForSelected(); InvalidateRect(hwnd_, nullptr, FALSE); }
                return 0;
            }
            if (w == VK_ESCAPE && mode_ == Mode::Player) {
                if (fullscreen_) ToggleFullscreen(); else LeavePlayer();
                return 0;
            }
            if (w == VK_SPACE && mode_ == Mode::Player && player_) {
                PlayerActivity(true); player_->PlayPause(); InvalidateControls(); return 0;
            }
            if (w == VK_F11 && mode_ == Mode::Player) {
                PlayerActivity(true); ToggleFullscreen(); return 0;
            }
            break;
        case WM_DESTROY:
            StopImageSlideshow();
            ResetLibraryZoom();
            SaveSettings();
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

    void Paint() {
        paintOwner_ = hwnd_;
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps);
        RECT rc{}; GetClientRect(hwnd_, &rc);
        const int w = std::max(1, static_cast<int>(rc.right-rc.left));
        const int h = std::max(1, static_cast<int>(rc.bottom-rc.top));
        EnsureBackBuffer(dc,w,h);
        HBRUSH bg = CreateSolidBrush(RGB(13,15,20)); FillRect(backDC_,&rc,bg); DeleteObject(bg);
        SetBkMode(backDC_, TRANSPARENT);
        SetTextColor(backDC_, RGB(238,241,247));

        if (mode_ == Mode::Library) PaintLibrary(backDC_,rc);
        else if (mode_ == Mode::Details) PaintDetails(backDC_,rc);

        BitBlt(dc, 0, 0, w, h, backDC_, 0, 0, SRCCOPY);
        EndPaint(hwnd_,&ps);
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

    void DrawTab(HDC dc, RECT r, const wchar_t* text, bool active) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=active ? RGB(235,238,245) : RGB(29,33,43);
        const COLORREF over=active ? RGB(250,251,253) : RGB(47,52,65);
        FillRound(dc, r, MixColor(base,over,hover), 12);
        DrawTextSimple(dc, text, r, 15, FW_BOLD, active ? RGB(12,14,19) : RGB(230,233,241), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    void DrawSlideshowButton(HDC dc, RECT r, bool active=false) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base = active ? RGB(235,238,245) : RGB(38,43,55);
        const COLORREF bg = MixColor(base, active ? RGB(250,251,253) : RGB(53,59,73), hover);
        const COLORREF fg = active ? RGB(12,14,19) : RGB(245,246,250);
        FillRound(dc, r, bg, 10);
        HBRUSH b = CreateSolidBrush(fg);
        HGDIOBJ old = SelectObject(dc, b);
        const int cx = (r.left + r.right) / 2;
        const int cy = (r.top + r.bottom) / 2;
        POINT pts[3]{{cx-6,cy-11},{cx-6,cy+11},{cx+11,cy}};
        Polygon(dc, pts, 3);
        SelectObject(dc, old);
        DeleteObject(b);
    }

    void DrawFullscreenButton(HDC dc, RECT r) {
        FillRound(dc, r, MixColor(RGB(38,43,55),RGB(53,59,73),ButtonHoverAmount(r)), 10);
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(245,246,250)); HGDIOBJ old = SelectObject(dc, pen);
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

    void DrawAutoNextIcon(HDC dc, RECT r) {
        const float hover=ButtonHoverAmount(r);
        const COLORREF base=autoNext_ ? RGB(235,238,245) : RGB(38,43,55);
        FillRound(dc, r, MixColor(base,autoNext_?RGB(250,251,253):RGB(53,59,73),hover), 10);
        COLORREF c = autoNext_ ? RGB(12,14,19) : RGB(245,246,250);
        HPEN pen=CreatePen(PS_SOLID,4,c); HGDIOBJ old=SelectObject(dc,pen);
        const int cy=(r.top+r.bottom)/2;
        const int x=r.left+11;
        MoveToEx(dc,x,cy-10,nullptr); LineTo(dc,x+12,cy); LineTo(dc,x,cy+10);
        MoveToEx(dc,x+14,cy-10,nullptr); LineTo(dc,x+26,cy); LineTo(dc,x+14,cy+10);
        SelectObject(dc,old); DeleteObject(pen);
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
        DrawPlayPauseIcon(dc,playerPlayRect_);
        DrawAutoNextIcon(dc,playerAutoNextRect_);
        DrawFullscreenButton(dc,playerFullRect_);

        const std::wstring time=FormatTime(player_->CurrentTime())+L" / "+FormatTime(player_->Duration());
        DrawTextSimple(dc,time,playerTimeRect_,13,FW_NORMAL,RGB(190,195,206),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

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
        static const wchar_t* kExts[]={L".jpg",L".jpeg",L".png",L".bmp",L".gif",L".tif",L".tiff",L".webp",L".heic",L".heif",L".avif"};
        for(auto* e:kExts) if(ext==e) return true;
        return false;
    }

    static uint64_t Fnv1a64(const std::wstring& s) {
        uint64_t h=1469598103934665603ull;
        for(wchar_t c:s){ h^=static_cast<uint64_t>(c); h*=1099511628211ull; }
        return h;
    }

    std::wstring BuildCachePath(const std::wstring& source) const {
        std::wstring sig=L"detail-vr-crop-v3-content-detect|"+source;
        std::error_code ec;
        auto sz=fs::file_size(source,ec); if(!ec) sig+=L"|"+std::to_wstring(sz);
        ec.clear(); auto ft=fs::last_write_time(source,ec); if(!ec) sig+=L"|"+std::to_wstring(ft.time_since_epoch().count());
        const uint64_t hash=Fnv1a64(sig);
        wchar_t name[40]{}; swprintf_s(name,L"%016llx.jpg",static_cast<unsigned long long>(hash));
        return (cacheDir_/name).wstring();
    }

    std::wstring BuildUiCachePath(const std::wstring& source) const {
        std::wstring sig=L"grid-v5-vr-crop-content-detect-640|"+source;
        std::error_code ec;
        auto sz=fs::file_size(source,ec); if(!ec) sig+=L"|"+std::to_wstring(sz);
        ec.clear(); auto ft=fs::last_write_time(source,ec); if(!ec) sig+=L"|"+std::to_wstring(ft.time_since_epoch().count());
        const uint64_t hash=Fnv1a64(sig);
        wchar_t name[48]{}; swprintf_s(name,L"%016llx.ui.jpg",static_cast<unsigned long long>(hash));
        return (cacheDir_/name).wstring();
    }


    std::wstring BuildPreviewDirectory(const std::wstring& source) const {
        std::wstring sig=L"details-previews-v3-vr-content-detect|"+source;
        std::error_code ec;
        auto sz=fs::file_size(source,ec); if(!ec) sig+=L"|"+std::to_wstring(sz);
        ec.clear(); auto ft=fs::last_write_time(source,ec); if(!ec) sig+=L"|"+std::to_wstring(ft.time_since_epoch().count());
        const uint64_t hash=Fnv1a64(sig);
        wchar_t name[40]{}; swprintf_s(name,L"%016llx",static_cast<unsigned long long>(hash));
        return (cacheDir_.parent_path()/L"previews"/name).wstring();
    }

    void HideCacheRootIfCreated() const {
        if (cacheDir_.empty()) return;
        const fs::path root=cacheDir_.parent_path();
        std::error_code ec;
        if (!fs::exists(root,ec)) return;
        const DWORD attrs=GetFileAttributesW(root.c_str());
        if (attrs==INVALID_FILE_ATTRIBUTES) return;
        if ((attrs&FILE_ATTRIBUTE_HIDDEN)==0) SetFileAttributesW(root.c_str(),attrs|FILE_ATTRIBUTE_HIDDEN);
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

    static double HalfDifferenceLR(Gdiplus::Bitmap& bitmap) {
        const UINT w=bitmap.GetWidth(), h=bitmap.GetHeight();
        if (w<4 || h<2) return 1e9;
        const UINT half=w/2u;
        double total=0.0; int count=0;
        const int samplesX=12, samplesY=8;
        for(int gy=0;gy<samplesY;++gy){
            const UINT y=std::min<UINT>(h-1u,static_cast<UINT>((gy+0.5)*h/samplesY));
            for(int gx=0;gx<samplesX;++gx){
                const UINT x=std::min<UINT>(half-1u,static_cast<UINT>((gx+0.5)*half/samplesX));
                Gdiplus::Color a,b;
                if(bitmap.GetPixel(static_cast<INT>(x),static_cast<INT>(y),&a)!=Gdiplus::Ok) continue;
                if(bitmap.GetPixel(static_cast<INT>(x+half),static_cast<INT>(y),&b)!=Gdiplus::Ok) continue;
                total += std::abs(static_cast<int>(a.GetR())-static_cast<int>(b.GetR()));
                total += std::abs(static_cast<int>(a.GetG())-static_cast<int>(b.GetG()));
                total += std::abs(static_cast<int>(a.GetB())-static_cast<int>(b.GetB()));
                count += 3;
            }
        }
        return count?total/static_cast<double>(count):1e9;
    }

    static double HalfDifferenceTB(Gdiplus::Bitmap& bitmap) {
        const UINT w=bitmap.GetWidth(), h=bitmap.GetHeight();
        if (w<2 || h<4) return 1e9;
        const UINT half=h/2u;
        double total=0.0; int count=0;
        const int samplesX=12, samplesY=8;
        for(int gy=0;gy<samplesY;++gy){
            const UINT y=std::min<UINT>(half-1u,static_cast<UINT>((gy+0.5)*half/samplesY));
            for(int gx=0;gx<samplesX;++gx){
                const UINT x=std::min<UINT>(w-1u,static_cast<UINT>((gx+0.5)*w/samplesX));
                Gdiplus::Color a,b;
                if(bitmap.GetPixel(static_cast<INT>(x),static_cast<INT>(y),&a)!=Gdiplus::Ok) continue;
                if(bitmap.GetPixel(static_cast<INT>(x),static_cast<INT>(y+half),&b)!=Gdiplus::Ok) continue;
                total += std::abs(static_cast<int>(a.GetR())-static_cast<int>(b.GetR()));
                total += std::abs(static_cast<int>(a.GetG())-static_cast<int>(b.GetG()));
                total += std::abs(static_cast<int>(a.GetB())-static_cast<int>(b.GetB()));
                count += 3;
            }
        }
        return count?total/static_cast<double>(count):1e9;
    }

    static int ResolveStillLayout(VRInfo vr, Gdiplus::Bitmap& bitmap, int initialLayout) {
        if (!vr.vr) return 0;
        if (vr.layoutExplicit) return vr.layout;
        if (initialLayout==1 || initialLayout==2) return initialLayout;

        // 2:1 can be either mono 360 or two square SBS eyes, so aspect ratio alone
        // is insufficient. Compare corresponding pixels in both halves. Stereo eyes
        // remain visually similar even with parallax; unrelated halves of a mono
        // panorama generally do not.
        const double lr=HalfDifferenceLR(bitmap);
        const double tb=HalfDifferenceTB(bitmap);
        constexpr double kStereoThreshold=52.0;
        if (lr<kStereoThreshold && lr+5.0<tb) return 1;
        if (tb<kStereoThreshold && tb+5.0<lr) return 2;
        if (lr<38.0) return 1;
        if (tb<38.0) return 2;
        return 0;
    }

    static bool SavePreviewSample(IMFSample* sample, UINT width, UINT height, LONG defaultStride, VRInfo vr, int layout, const std::wstring& output) {
        if (!sample || !width || !height) return false;
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
                layout=ResolveStillLayout(vr,frame,layout);
                UINT sx=0,sy=0,sw=width,sh=height;
                if (layout==1 && width>=2) sw=width/2u;
                else if (layout==2 && height>=2) sh=height/2u;

                const int outW=320,outH=180;
                Gdiplus::Bitmap out(outW,outH,PixelFormat24bppRGB);
                Gdiplus::Graphics g(&out);
                g.Clear(Gdiplus::Color(255,16,18,24));
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                const double scale=std::min(static_cast<double>(outW)/std::max<UINT>(1u,sw),static_cast<double>(outH)/std::max<UINT>(1u,sh));
                const int dw=std::max(1,static_cast<int>(sw*scale));
                const int dh=std::max(1,static_cast<int>(sh*scale));
                const int dx=(outW-dw)/2,dy=(outH-dh)/2;
                g.DrawImage(&frame,Gdiplus::Rect(dx,dy,dw,dh),static_cast<INT>(sx),static_cast<INT>(sy),static_cast<INT>(sw),static_cast<INT>(sh),Gdiplus::UnitPixel);
                ok=SaveJpeg(out,output,84);
            }
        }

        if (locked2D) buffer2D->Unlock2D(); else buffer->Unlock();
        return ok;
    }

    static bool GenerateVideoPreviewsMF(const std::wstring& source, const std::wstring& previewDir, VRInfo vr, std::atomic<bool>& stop, HWND notifyHwnd, std::atomic<double>* durationOut = nullptr) {
        ComPtr<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs,2))) return false;
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);
        attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,FALSE);

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

        std::vector<int> captureSeconds;
        if (duration<60.0) {
            for (double f : {0.25,0.50,0.75}) {
                int sec=static_cast<int>(std::round(duration*f));
                sec=std::clamp(sec,1,std::max(1,static_cast<int>(std::floor(duration-0.2))));
                if (captureSeconds.empty()||captureSeconds.back()!=sec) captureSeconds.push_back(sec);
            }
        } else {
            const int lastMinute=static_cast<int>(std::floor(std::max(0.0,duration-0.5)/60.0));
            for (int minute=1;minute<=lastMinute;++minute) captureSeconds.push_back(minute*60);
        }
        if (captureSeconds.empty()) return false;

        bool any=false;
        for (int sec : captureSeconds) {
            if (stop.load()) return any;
            wchar_t name[32]{}; swprintf_s(name,L"%06d.jpg",sec);
            const std::wstring output=(fs::path(previewDir)/name).wstring();
            if (fs::exists(output)) { any=true; continue; }

            PROPVARIANT pos; PropVariantInit(&pos); pos.vt=VT_I8; pos.hVal.QuadPart=static_cast<LONGLONG>(sec)*10000000LL;
            hr=reader->SetCurrentPosition(GUID_NULL,pos); PropVariantClear(&pos);
            if (FAILED(hr)) continue;

            ComPtr<IMFSample> chosen;
            for (int attempts=0;attempts<240 && !stop.load();++attempts) {
                DWORD streamIndex=0,flags=0; LONGLONG timestamp=0; ComPtr<IMFSample> sample;
                hr=reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&streamIndex,&flags,&timestamp,&sample);
                if (FAILED(hr)||(flags&MF_SOURCE_READERF_ENDOFSTREAM)) break;
                if (!sample) continue;
                chosen=sample;
                if (timestamp>=static_cast<LONGLONG>(sec)*10000000LL) break;
            }
            if (!chosen||stop.load()) continue;

            std::error_code dirEc;
            fs::create_directories(previewDir,dirEc);
            if (dirEc) continue;
            const std::wstring tmp=output+L".tmp"; DeleteFileW(tmp.c_str());
            if (SavePreviewSample(chosen.Get(),actualW,actualH,stride,vr,resolvedLayout,tmp)) {
                DeleteFileW(output.c_str());
                if (MoveFileExW(tmp.c_str(),output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                    any=true;
                    if (notifyHwnd) PostMessageW(notifyHwnd,WM_APP_PREVIEW_READY,0,0);
                } else DeleteFileW(tmp.c_str());
            } else DeleteFileW(tmp.c_str());
        }

        if (!stop.load() && any) {
            std::ofstream marker(fs::path(previewDir)/L"complete.txt",std::ios::binary|std::ios::trunc);
            if (marker) marker<<duration;
        }
        return any;
    }

    void ClearPreviewBitmaps() {
        for (auto& p : previewFrames_) if (p.bitmap) DeleteObject(p.bitmap);
        previewFrames_.clear();
    }

    void RefreshPreviewFrames() {
        if (previewDir_.empty()) return;
        std::map<int,HBITMAP> old;
        std::map<int,ULONGLONG> oldUsed;
        for (auto& p : previewFrames_) { if (p.bitmap) old[p.seconds]=p.bitmap; oldUsed[p.seconds]=p.lastUsed; }
        previewFrames_.clear();
        std::error_code ec;
        if (fs::exists(previewDir_,ec)) {
            for (const auto& e : fs::directory_iterator(previewDir_,ec)) {
                if (ec) break;
                if (!e.is_regular_file(ec)||ToLower(e.path().extension().wstring())!=L".jpg") continue;
                const std::wstring stem=e.path().stem().wstring();
                wchar_t* end=nullptr; const long sec=wcstol(stem.c_str(),&end,10);
                if (!end||*end!=L'\0'||sec<=0) continue;
                PreviewFrame f; f.seconds=static_cast<int>(sec); f.path=e.path().wstring();
                auto it=old.find(f.seconds); if(it!=old.end()){f.bitmap=it->second;old.erase(it);}
                auto uit=oldUsed.find(f.seconds); if(uit!=oldUsed.end()) f.lastUsed=uit->second;
                previewFrames_.push_back(std::move(f));
            }
        }
        for (auto& kv:old) if(kv.second) DeleteObject(kv.second);
        std::sort(previewFrames_.begin(),previewFrames_.end(),[](const PreviewFrame&a,const PreviewFrame&b){return a.seconds<b.seconds;});
    }

    double ReadCachedPreviewDuration() const {
        if (previewDir_.empty()) return 0.0;
        std::ifstream in(fs::path(previewDir_) / L"complete.txt", std::ios::binary);
        double value = 0.0;
        if (in) in >> value;
        return (value > 0.0 && std::isfinite(value)) ? value : 0.0;
    }

    void StopPreviewWorker() {
        previewStop_=true;
        if (previewThread_.joinable()) previewThread_.join();
        previewStop_=false;
    }

    void StartPreviewWorkerForSelected() {
        StopPreviewWorker();
        ClearPreviewBitmaps();
        previewDir_.clear();
        detailsDurationSeconds_.store(0.0, std::memory_order_relaxed);
        if (category_!=Category::Videos||selected_>=videos_.size()) return;
        const MediaItem item=videos_[selected_];
        previewDir_=BuildPreviewDirectory(item.path);
        RefreshPreviewFrames();
        detailsDurationSeconds_.store(ReadCachedPreviewDuration(), std::memory_order_relaxed);
        const bool previewsComplete=fs::exists(fs::path(previewDir_)/L"complete.txt");
        if(previewsComplete && fs::exists(item.cachePath)) return;
        previewStop_=false;
        previewThread_=std::thread([this,item,dir=previewDir_,previewsComplete]() {
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
            if(!previewStop_.load()&&!fs::exists(item.cachePath)) {
                ThumbJob high{item.path,item.cachePath,item.uiCachePath,true,item.vr};
                if (GenerateVideoCache(high)) HideCacheRootIfCreated();
                if(hwnd_&&!previewStop_.load()) PostMessageW(hwnd_,WM_APP_THUMB_READY,0,0);
            }
            if(!previewStop_.load()&&!previewsComplete) {
                if (GenerateVideoPreviewsMF(item.path,dir,item.vr,previewStop_,hwnd_,&detailsDurationSeconds_)) HideCacheRootIfCreated();
            }
            if (hwnd_&&!previewStop_.load()) PostMessageW(hwnd_,WM_APP_PREVIEW_READY,0,0);
            CoUninitialize();
        });
    }

    HBITMAP GetPreviewBitmap(PreviewFrame& frame) {
        frame.lastUsed=GetTickCount64();
        if (!frame.bitmap&&fs::exists(frame.path)) frame.bitmap=LoadScaledBitmap(frame.path,320,180);
        return frame.bitmap;
    }

    void TrimPreviewMemory() {
        if (previewFrames_.size()<=24) return;
        std::vector<PreviewFrame*> loaded;
        for (auto& p:previewFrames_) if(p.bitmap) loaded.push_back(&p);
        if (loaded.size()<=24) return;
        std::sort(loaded.begin(),loaded.end(),[](const PreviewFrame*a,const PreviewFrame*b){return a->lastUsed<b->lastUsed;});
        for (size_t i=0;i<loaded.size()-24;++i){DeleteObject(loaded[i]->bitmap);loaded[i]->bitmap=nullptr;}
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

    static bool SaveBitmapJpeg(HBITMAP hbmp, const std::wstring& path) {
        if(!hbmp) return false;
        Gdiplus::Bitmap bitmap(hbmp,nullptr);
        if(bitmap.GetLastStatus()!=Gdiplus::Ok) return false;
        return SaveJpeg(bitmap,path,95);
    }

    static bool SaveVideoThumbJpeg(HBITMAP hbmp, const std::wstring& path, VRInfo vr, int targetW, int targetH, ULONG quality) {
        if(!hbmp) return false;
        Gdiplus::Bitmap src(hbmp,nullptr);
        if(src.GetLastStatus()!=Gdiplus::Ok || !src.GetWidth() || !src.GetHeight()) return false;

        const UINT sourceW=src.GetWidth(), sourceH=src.GetHeight();
        int layout=ResolvePreviewLayout(vr,sourceW,sourceH);
        layout=ResolveStillLayout(vr,src,layout);
        if(!vr.vr || layout==0) return SaveJpeg(src,path,quality);

        UINT sx=0,sy=0,sw=sourceW,sh=sourceH;
        if(layout==1 && sourceW>=2) sw=sourceW/2u;          // SBS: keep left eye
        else if(layout==2 && sourceH>=2) sh=sourceH/2u;     // TB/OU: keep top eye

        targetW=std::max(1,targetW); targetH=std::max(1,targetH);
        Gdiplus::Bitmap out(targetW,targetH,PixelFormat24bppRGB);
        Gdiplus::Graphics g(&out);
        g.Clear(Gdiplus::Color(255,16,18,24));
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        const double scale=std::max(static_cast<double>(targetW)/std::max<UINT>(1u,sw), static_cast<double>(targetH)/std::max<UINT>(1u,sh));
        const double visibleW=static_cast<double>(targetW)/scale;
        const double visibleH=static_cast<double>(targetH)/scale;
        const UINT cropW=std::max<UINT>(1u,std::min<UINT>(sw,static_cast<UINT>(visibleW+0.5)));
        const UINT cropH=std::max<UINT>(1u,std::min<UINT>(sh,static_cast<UINT>(visibleH+0.5)));
        const UINT cropX=sx+(sw-cropW)/2u;
        const UINT cropY=sy+(sh-cropH)/2u;
        g.DrawImage(&src,Gdiplus::Rect(0,0,targetW,targetH),static_cast<INT>(cropX),static_cast<INT>(cropY),static_cast<INT>(cropW),static_cast<INT>(cropH),Gdiplus::UnitPixel);
        return SaveJpeg(out,path,quality);
    }

    static bool GenerateImageCache(const ThumbJob& job) {
        Gdiplus::Image src(job.source.c_str()); if(src.GetLastStatus()!=Gdiplus::Ok) return false;
        UINT sw=src.GetWidth(),sh=src.GetHeight(); if(!sw||!sh) return false;
        const double scale=std::min(1.0,std::min(3840.0/static_cast<double>(sw),2160.0/static_cast<double>(sh)));
        const UINT dw=std::max<UINT>(1u,static_cast<UINT>(sw*scale));
        const UINT dh=std::max<UINT>(1u,static_cast<UINT>(sh*scale));
        Gdiplus::Bitmap out(dw,dh,PixelFormat24bppRGB); Gdiplus::Graphics g(&out);
        g.Clear(Gdiplus::Color(255,0,0,0)); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality); g.DrawImage(&src,Gdiplus::Rect(0,0,static_cast<INT>(dw),static_cast<INT>(dh)));
        std::error_code dirEc; fs::create_directories(fs::path(job.output).parent_path(),dirEc);
        if (dirEc) return false;
        return SaveJpeg(out,job.output,95);
    }

    static bool GenerateVideoCache(const ThumbJob& job) {
        ComPtr<IShellItem> shell; if(FAILED(SHCreateItemFromParsingName(job.source.c_str(),nullptr,IID_PPV_ARGS(&shell)))) return false;
        ComPtr<IShellItemImageFactory> factory; if(FAILED(shell.As(&factory))) return false;
        SIZE requested{3840,2160}; HBITMAP hbmp=nullptr;
        const SIIGBF flags=static_cast<SIIGBF>(SIIGBF_THUMBNAILONLY|SIIGBF_BIGGERSIZEOK);
        HRESULT hr=factory->GetImage(requested,flags,&hbmp); if(FAILED(hr)||!hbmp) return false;
        std::error_code dirEc; fs::create_directories(fs::path(job.output).parent_path(),dirEc);
        if (dirEc) { DeleteObject(hbmp); return false; }
        bool ok=SaveVideoThumbJpeg(hbmp,job.output,job.vr,3840,2160,95); DeleteObject(hbmp); return ok;
    }

    static bool GenerateUiCache(const std::wstring& sourcePath, const std::wstring& outputPath) {
        if (fs::exists(outputPath)) return true;
        Gdiplus::Image src(sourcePath.c_str()); if(src.GetLastStatus()!=Gdiplus::Ok) return false;
        const UINT sw=src.GetWidth(),sh=src.GetHeight(); if(!sw||!sh) return false;
        const double scale=std::min(1.0,std::min(640.0/static_cast<double>(sw),360.0/static_cast<double>(sh)));
        const UINT dw=std::max<UINT>(1u,static_cast<UINT>(sw*scale));
        const UINT dh=std::max<UINT>(1u,static_cast<UINT>(sh*scale));
        Gdiplus::Bitmap out(dw,dh,PixelFormat24bppRGB); Gdiplus::Graphics g(&out);
        g.Clear(Gdiplus::Color(255,0,0,0)); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality); g.DrawImage(&src,Gdiplus::Rect(0,0,static_cast<INT>(dw),static_cast<INT>(dh)));
        return SaveJpeg(out,outputPath,92);
    }


    static bool GenerateGridThumb(const ThumbJob& job) {
        if (job.uiOutput.empty()||fs::exists(job.uiOutput)) return true;
        const std::wstring tmp=job.uiOutput+L".tmp.jpg"; DeleteFileW(tmp.c_str());
        bool ok=false;
        if (job.isVideo) {
            ComPtr<IShellItem> shell;
            if (SUCCEEDED(SHCreateItemFromParsingName(job.source.c_str(),nullptr,IID_PPV_ARGS(&shell)))) {
                ComPtr<IShellItemImageFactory> factory;
                if (SUCCEEDED(shell.As(&factory))) {
                    SIZE requested{640,360}; HBITMAP hbmp=nullptr;
                    const SIIGBF flags=static_cast<SIIGBF>(SIIGBF_THUMBNAILONLY|SIIGBF_BIGGERSIZEOK);
                    if (SUCCEEDED(factory->GetImage(requested,flags,&hbmp))&&hbmp) {
                        std::error_code dirEc; fs::create_directories(fs::path(job.uiOutput).parent_path(),dirEc);
                        if (!dirEc) ok=SaveVideoThumbJpeg(hbmp,tmp,job.vr,640,360,92);
                        DeleteObject(hbmp);
                    }
                }
            }
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
        if (!ok){DeleteFileW(tmp.c_str());return false;}
        DeleteFileW(job.uiOutput.c_str());
        if(!MoveFileExW(tmp.c_str(),job.uiOutput.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp.c_str());return false;}
        return true;
    }

    static bool GenerateThumb(const ThumbJob& job) {
        std::error_code ec; fs::create_directories(fs::path(job.output).parent_path(),ec);
        if(!fs::exists(job.output)) {
            std::wstring tmp=job.output+L".tmp.jpg"; DeleteFileW(tmp.c_str());
            ThumbJob temp=job; temp.output=tmp;
            bool ok=job.isVideo?GenerateVideoCache(temp):GenerateImageCache(temp);
            if(!ok){ DeleteFileW(tmp.c_str()); return false; }
            DeleteFileW(job.output.c_str());
            if(!MoveFileExW(tmp.c_str(),job.output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ DeleteFileW(tmp.c_str()); return false; }
        }
        if(!job.uiOutput.empty() && !fs::exists(job.uiOutput)) {
            std::wstring tmpUi=job.uiOutput+L".tmp.jpg"; DeleteFileW(tmpUi.c_str());
            if(GenerateUiCache(job.output,tmpUi)) {
                DeleteFileW(job.uiOutput.c_str());
                if(!MoveFileExW(tmpUi.c_str(),job.uiOutput.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) DeleteFileW(tmpUi.c_str());
            } else DeleteFileW(tmpUi.c_str());
        }
        return fs::exists(job.output);
    }

    void StartThumbnailWorker() {
        StopThumbnailWorker();
        std::vector<ThumbJob> jobs;
        jobs.reserve(videos_.size()+images_.size());
        const size_t quickCount=24;
        auto needs=[](const MediaItem& item){ return !fs::exists(item.uiCachePath); };
        for(size_t i=0;i<std::min(quickCount,videos_.size());++i) if(needs(videos_[i])) jobs.push_back({videos_[i].path,videos_[i].cachePath,videos_[i].uiCachePath,true,videos_[i].vr});
        for(size_t i=0;i<std::min(quickCount,images_.size());++i) if(needs(images_[i])) jobs.push_back({images_[i].path,images_[i].cachePath,images_[i].uiCachePath,false,images_[i].vr});
        for(size_t i=quickCount;i<videos_.size();++i) if(needs(videos_[i])) jobs.push_back({videos_[i].path,videos_[i].cachePath,videos_[i].uiCachePath,true,videos_[i].vr});
        for(size_t i=quickCount;i<images_.size();++i) if(needs(images_[i])) jobs.push_back({images_[i].path,images_[i].cachePath,images_[i].uiCachePath,false,images_[i].vr});
        if(jobs.empty()) return;
        thumbStop_=false;
        thumbThread_=std::thread([this,jobs=std::move(jobs)]() mutable {
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
            ULONGLONG lastUiPost=0;
            for(const auto& job:jobs){
                if(thumbStop_.load()) break;
                if(!fs::exists(job.source)) continue;
                if(GenerateGridThumb(job)) {
                    HideCacheRootIfCreated();
                    if (hwnd_) {
                        const ULONGLONG now=GetTickCount64();
                        if(now-lastUiPost>=300){ PostMessageW(hwnd_,WM_APP_THUMB_READY,0,0); lastUiPost=now; }
                    }
                }
                for(int i=0;i<8 && !thumbStop_.load();++i) Sleep(10);
            }
            if(hwnd_ && !thumbStop_.load()) PostMessageW(hwnd_,WM_APP_THUMB_READY,0,0);
            CoUninitialize();
        });
    }

    void StopThumbnailWorker() {
        thumbStop_=true;
        if(thumbThread_.joinable()) thumbThread_.join();
    }

    HBITMAP LoadScaledBitmap(const std::wstring& file, int maxW, int maxH) {
        Gdiplus::Image src(file.c_str()); if(src.GetLastStatus()!=Gdiplus::Ok) return nullptr;
        const UINT sw=src.GetWidth(),sh=src.GetHeight(); if(!sw||!sh) return nullptr;
        const double scale=std::min(static_cast<double>(maxW)/sw,static_cast<double>(maxH)/sh);
        const int dw=std::max(1,static_cast<int>(sw*scale)); const int dh=std::max(1,static_cast<int>(sh*scale));
        Gdiplus::Bitmap out(dw,dh,PixelFormat32bppARGB); Gdiplus::Graphics g(&out);
        g.Clear(Gdiplus::Color(255,0,0,0)); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality); g.DrawImage(&src,Gdiplus::Rect(0,0,dw,dh));
        HBITMAP hbmp=nullptr; if(out.GetHBITMAP(Gdiplus::Color(255,0,0,0),&hbmp)!=Gdiplus::Ok) return nullptr;
        return hbmp;
    }

    HBITMAP TryShellCachedThumb(MediaItem& item, int w, int h) {
        if(item.thumb || item.thumbAttempted) return item.thumb;
        item.thumbAttempted=true;
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
        const bool cached=fs::exists(preferred);
        if(cached){
            const bool tooSmall=item.thumb && (item.thumbW < w || item.thumbH < h);
            if(!item.thumbFromPrivateCache || tooSmall){
                if(item.thumb){ DeleteObject(item.thumb); item.thumb=nullptr; }
                item.thumb=LoadScaledBitmap(preferred,std::max(1,w),std::max(1,h));
                if(item.thumb){ BITMAP bm{}; GetObjectW(item.thumb,sizeof(bm),&bm); item.thumbW=bm.bmWidth; item.thumbH=bm.bmHeight; item.thumbFromPrivateCache=true; }
            }
            return item.thumb;
        }
        // Never decode the 4K private cache on the UI thread just to paint a small card.
        return TryShellCachedThumb(item,w,h);
    }

    static void DrawBitmapCover(HDC dc,HBITMAP bmp,RECT r) {
        if(!bmp) return; BITMAP bm{}; GetObjectW(bmp,sizeof(bm),&bm); if(!bm.bmWidth||!bm.bmHeight) return;
        HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); SetStretchBltMode(dc,HALFTONE);
        const double sx=static_cast<double>(r.right-r.left)/bm.bmWidth, sy=static_cast<double>(r.bottom-r.top)/bm.bmHeight;
        const double s=std::max(sx,sy); const int sw=std::max(1,static_cast<int>((r.right-r.left)/s)); const int sh=std::max(1,static_cast<int>((r.bottom-r.top)/s));
        const int srcx=(bm.bmWidth-sw)/2,srcy=(bm.bmHeight-sh)/2;
        StretchBlt(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,mem,srcx,srcy,sw,sh,SRCCOPY); SelectObject(mem,old); DeleteDC(mem);
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

    void ClearThumbs(std::vector<MediaItem>& list) {
        for(auto& v:list){ if(v.thumb) DeleteObject(v.thumb); v.thumb=nullptr; v.thumbW=v.thumbH=0; }
    }

    void TrimThumbMemory() {
        struct Ref{MediaItem* item;ULONGLONG tick;}; std::vector<Ref> loaded;
        for(auto& v:videos_) if(v.thumb) loaded.push_back({&v,v.thumbLastUsed});
        for(auto& v:images_) if(v.thumb) loaded.push_back({&v,v.thumbLastUsed});
        if(loaded.size()<=120) return;
        std::sort(loaded.begin(),loaded.end(),[](const Ref&a,const Ref&b){return a.tick<b.tick;});
        const size_t removeCount=loaded.size()-120;
        for(size_t i=0;i<removeCount;++i){ auto* v=loaded[i].item; if(v->thumb){DeleteObject(v->thumb);v->thumb=nullptr;v->thumbW=v->thumbH=0;v->thumbAttempted=false;v->thumbFromPrivateCache=false;} }
    }

    std::vector<MediaItem>& CurrentItems() { return category_==Category::Videos?videos_:images_; }
    const std::vector<MediaItem>& CurrentItems() const { return category_==Category::Videos?videos_:images_; }

    bool PathEquals(const std::wstring& a, const std::wstring& b) const {
        return ToLower(fs::path(a).lexically_normal().wstring()) == ToLower(fs::path(b).lexically_normal().wstring());
    }

    bool IsAtLibraryRoot() const {
        return currentFolder_.empty() || PathEquals(currentFolder_, folder_);
    }

    std::vector<size_t> VisibleFolderIndices() const {
        std::vector<size_t> out;
        // While actively searching, keep the existing media-card layout and show only matching media.
        if (!searchQuery_.empty()) return out;
        const std::wstring currentKey = ToLower(fs::path(currentFolder_).lexically_normal().wstring());
        for (size_t i = 0; i < folders_.size(); ++i) {
            const std::wstring parentKey = ToLower(fs::path(folders_[i].path).parent_path().lexically_normal().wstring());
            if (parentKey == currentKey) out.push_back(i);
        }
        return out;
    }

    const std::vector<size_t>& FilteredIndices() {
        if(!filterDirty_) return filteredIndices_;
        filteredIndices_.clear();
        const auto& list=CurrentItems();
        const std::wstring needle=ToLower(searchQuery_);
        const std::wstring currentKey=ToLower(fs::path(currentFolder_).lexically_normal().wstring());
        filteredIndices_.reserve(list.size());
        for(size_t i=0;i<list.size();++i){
            // Library search is intentionally local to the folder currently being viewed.
            // It never pulls matches in from sibling folders or subfolders.
            const std::wstring parentKey=ToLower(fs::path(list[i].path).parent_path().lexically_normal().wstring());
            if(parentKey!=currentKey) continue;
            if(!needle.empty() && list[i].searchText.find(needle)==std::wstring::npos) continue;
            filteredIndices_.push_back(i);
        }
        filterDirty_=false;
        return filteredIndices_;
    }

    RECT StandardBackRect(RECT rc) const {
        return RECT{20, rc.bottom - 51, 100, rc.bottom - 13};
    }

    void PaintFooterBackground(HDC dc, RECT rc) {
        const int footerH = 64;
        RECT footer{0, std::max<LONG>(0, rc.bottom - footerH), rc.right, rc.bottom};
        HBRUSH fb = CreateSolidBrush(RGB(16,19,25)); FillRect(dc, &footer, fb); DeleteObject(fb);
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
        const int total = 82 + rows * (cardH + kLibraryGap) + 86;
        return std::max(0, total - clientHeight);
    }

    void UpdateLibraryScrollbarRects(RECT rc) {
        libraryScrollTrackRect_ = RECT{};
        libraryScrollThumbRect_ = RECT{};
        if (mode_ != Mode::Library) return;
        const int maxScroll = LibraryMaxScroll(rc);
        if (maxScroll <= 0) return;

        const int top = 72;
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
        const int buttonTop = footerTop + 13;
        const int buttonBottom = rc.bottom - 13;

        // Root library: Videos / Images start at the normal bottom-left anchor.
        // Inside a folder: Back occupies that same anchor and the category toggles move beside it.
        int navLeft = 20;
        if (!IsAtLibraryRoot()) {
            backRect_ = {20, buttonTop, 100, buttonBottom};
            DrawButton(dc, backRect_, L"Back");
            navLeft = 110;
        } else {
            backRect_ = RECT{};
        }
        videosTabRect_ = {navLeft, buttonTop, navLeft + 88, buttonBottom};
        imagesTabRect_ = {navLeft + 98, buttonTop, navLeft + 186, buttonBottom};
        DrawTab(dc, videosTabRect_, L"Videos", category_ == Category::Videos);
        DrawTab(dc, imagesTabRect_, L"Images", category_ == Category::Images);
        if (category_ == Category::Images) {
            slideshowRect_ = {imagesTabRect_.right + 10, buttonTop, imagesTabRect_.right + 56, buttonBottom};
            DrawSlideshowButton(dc, slideshowRect_, slideshowActive_);
        } else {
            slideshowRect_ = RECT{};
        }

        // Folder controls stay pinned to the bottom-right, independent of the media grid.
        const int rightMargin = 18, rescanW = 100, chooseW = 152, gap = 10;
        const int right = std::max(0, static_cast<int>(rc.right) - rightMargin);
        rescanRect_ = {right - rescanW, buttonTop, right, buttonBottom};
        chooseRect_ = {rescanRect_.left - gap - chooseW, buttonTop, rescanRect_.left - gap, buttonBottom};
        DrawButton(dc, chooseRect_, L"Choose folder");
        DrawButton(dc, rescanRect_, L"Rescan");
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
        RECT top{0,0,rc.right,64}; HBRUSH tb=CreateSolidBrush(RGB(18,21,28)); FillRect(dc,&top,tb); DeleteObject(tb);
        RECT logo{20,0,std::min<LONG>(430,rc.right-20),64}; DrawTextSimple(dc,L"Visual MediaPlayer",logo,23,FW_BOLD);

        if (!IsAtLibraryRoot() && !currentFolder_.empty()) {
            const std::wstring folderName = fs::path(currentFolder_).filename().wstring();
            RECT crumb{270,0,std::max<LONG>(280,rc.right-470),64};
            DrawTextSimple(dc,folderName,crumb,14,FW_SEMIBOLD,RGB(155,161,174),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }

        auto& mutableList=CurrentItems();
        const auto& filtered=FilteredIndices();
        const auto visibleFolders=VisibleFolderIndices();
        const size_t totalCards=visibleFolders.size()+filtered.size();
        if(totalCards==0){
            std::wstring msg;
            if(folder_.empty()) msg=L"Choose a folder to load videos and images.";
            else if(!fs::exists(folder_)) msg=L"The saved media folder is currently unavailable.";
            else if(!searchQuery_.empty()) msg=L"No matching media.";
            else msg=category_==Category::Videos?L"No videos or subfolders here.":L"No images or subfolders here.";
            RECT mr{40,92,rc.right-40,180}; DrawTextSimple(dc,msg,mr,25,FW_SEMIBOLD,RGB(180,185,197),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            PaintLibraryNavigator(dc,rc);
            if(searchVisible_) PaintLibrarySearch(dc,rc);
            return;
        }

        const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
        const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int cardH=imageH+kLibraryTitleHeight;
        const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
        const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap));
        const int startY=82-scrollY_;
        for(size_t displayIndex=0;displayIndex<totalCards;++displayIndex){
            const int col=static_cast<int>(displayIndex)%cols,row=static_cast<int>(displayIndex)/cols;
            RECT card{pad+col*(cardW+gap),startY+row*(cardH+gap),pad+col*(cardW+gap)+cardW,startY+row*(cardH+gap)+cardH};
            if(card.bottom<66||card.top>rc.bottom-68) continue;

            if(displayIndex<visibleFolders.size()) {
                DrawFolderCard(dc, folders_[visibleFolders[displayIndex]], card);
                continue;
            }

            const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
            const size_t i=filtered[mediaDisplayIndex];
            FillRound(dc,card,RGB(31,35,46),12);
            RECT image=card; image.bottom=image.top+imageH;
            HBITMAP bmp=GetItemThumb(mutableList[i],640,360);
            if(bmp) DrawBitmapCover(dc,bmp,image); else { HBRUSH pb=CreateSolidBrush(RGB(43,48,61)); FillRect(dc,&image,pb); DeleteObject(pb); }
            RECT title{card.left+10,image.bottom+2,card.right-10,card.bottom-3}; DrawTextSimple(dc,mutableList[i].title,title,14,FW_SEMIBOLD);
            if(mutableList[i].isVideo&&mutableList[i].vr.vr){
                RECT vrTag{card.left+8,card.top+8,card.left+82,card.top+31}; FillRound(dc,vrTag,RGB(16,19,25),8);
                DrawTextSimple(dc,mutableList[i].vr.projection==2?L"VR180":L"360 VR",vrTag,11,FW_BOLD,RGB(220,225,235),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
        }
        PaintLibraryScrollbar(dc,rc);
        PaintLibraryNavigator(dc,rc);
        if(searchVisible_) PaintLibrarySearch(dc,rc);
        TrimThumbMemory();
    }

    void PaintLibrarySearch(HDC dc, RECT rc) {
        const int right=std::max(20,static_cast<int>(rc.right)-20);
        const int width=std::min(430,std::max(220,static_cast<int>(rc.right)/3));
        searchBoxRect_={right-width,12,right,52};
        FillRound(dc,searchBoxRect_,RGB(31,35,46),11);
        RECT text{searchBoxRect_.left+14,searchBoxRect_.top,searchBoxRect_.right-14,searchBoxRect_.bottom};
        const std::wstring shown=searchQuery_.empty()?L"Search...":searchQuery_;
        DrawTextSimple(dc,shown,text,16,FW_SEMIBOLD,searchQuery_.empty()?RGB(145,151,164):RGB(244,246,250));
    }

    void PaintDetails(HDC dc, RECT rc) {
        previewHitRects_.clear();
        previewZoomRect_ = RECT{0,0,0,0};
        auto& list=CurrentItems(); if(selected_>=list.size()) return; MediaItem& item=list[selected_];
        ClampDetailsScroll();
        RECT top{0,0,rc.right,64}; HBRUSH tb=CreateSolidBrush(RGB(18,21,28)); FillRect(dc,&top,tb); DeleteObject(tb);
        RECT brand{20,0,std::min<LONG>(430,rc.right-20),64}; DrawTextSimple(dc,L"Visual MediaPlayer",brand,23,FW_BOLD);

        const int footerTop=std::max(64,static_cast<int>(rc.bottom)-64);
        const int contentOffset=detailsScrollY_;
        int y=78-contentOffset;
        RECT title{40,y,rc.right-40,y+42}; DrawTextSimple(dc,item.title,title,30,FW_BOLD); y+=54;

        const int heroH=item.isVideo?320:std::max(260,footerTop-150);
        RECT media{40,y,rc.right-40,y+heroH};
        if(media.bottom>64 && media.top<footerTop){
            HBRUSH b=CreateSolidBrush(RGB(20,23,31)); FillRect(dc,&media,b); DeleteObject(b);
            const int reqW=std::min(2560,std::max(1,static_cast<int>(media.right-media.left)));
            const int reqH=std::min(1440,std::max(1,static_cast<int>(media.bottom-media.top)));
            HBITMAP bmp=nullptr;
            if(item.isVideo && !fs::exists(item.cachePath)) bmp=GetItemThumb(item,640,360);
            else bmp=GetItemThumb(item,reqW,reqH);
            if(bmp){
                if(item.isVideo) {
                    DrawBitmapCover(dc,bmp,media);
                } else if(slideshowFadeActive_ && slideshowPreviousIndex_ < images_.size()) {
                    HBITMAP previous=GetItemThumb(images_[slideshowPreviousIndex_],reqW,reqH);
                    const float progress=EaseUi(static_cast<float>(GetTickCount64()-slideshowFadeStart_) / static_cast<float>(kUiAnimationDurationMs));
                    if(previous) DrawBitmapContain(dc,previous,media);
                    DrawBitmapContainAlpha(dc,bmp,media,static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(progress*255.0f)),0,255)));
                } else {
                    DrawBitmapContain(dc,bmp,media);
                }
            }
        }
        y+=heroH+22;

        if(item.isVideo){
            // The whole visible secondary-preview section is a zoom target, including the
            // loading/empty state and the gaps between cards. This avoids wheel zoom becoming
            // unavailable while previews are still arriving from the background worker.
            const int zoomTop = std::max(64, y);
            if (zoomTop < footerTop && rc.right > 80)
                previewZoomRect_ = RECT{40, zoomTop, rc.right-40, footerTop};

            const int gap=12;
            const int cardW=previewCardWidth_;
            const int imageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
            const int labelH=24;
            const int cardH=imageH+labelH;
            const int availW=std::max(1,static_cast<int>(rc.right)-80);
            const int cols=std::max(1,(availW+gap)/(cardW+gap));

            if(previewFrames_.empty()){
                RECT note{40,y,rc.right-40,y+54};
                const bool complete=!previewDir_.empty()&&fs::exists(fs::path(previewDir_)/L"complete.txt");
                DrawTextSimple(dc,complete?L"No secondary previews were available for this video.":L"Loading secondary previews in the background...",note,14,FW_NORMAL,RGB(160,167,180));
                y+=64;
            } else {
                for(size_t i=0;i<previewFrames_.size();++i){
                    const int row=static_cast<int>(i)/cols,col=static_cast<int>(i)%cols;
                    RECT card{40+col*(cardW+gap),y+row*(cardH+gap),40+col*(cardW+gap)+cardW,y+row*(cardH+gap)+cardH};
                    if(card.bottom<64||card.top>footerTop) continue;
                    // Keep preview input strictly inside the scrollable content area.
                    // A card can be partially visible behind the fixed footer, but that hidden
                    // portion must never remain clickable through the footer controls.
                    RECT previewHit{};
                    RECT previewViewport{0,64,rc.right,footerTop};
                    if (IntersectRect(&previewHit, &card, &previewViewport))
                        previewHitRects_.push_back({previewHit, previewFrames_[i].seconds});
                    FillRound(dc,card,RGB(28,32,42),9);
                    RECT image=card; image.bottom=image.top+imageH;
                    HBITMAP pbmp=GetPreviewBitmap(previewFrames_[i]);
                    if(pbmp) DrawBitmapCover(dc,pbmp,image);
                    else { HBRUSH ph=CreateSolidBrush(RGB(43,48,61)); FillRect(dc,&image,ph); DeleteObject(ph); }
                    RECT label{card.left+8,image.bottom,card.right-8,card.bottom};
                    DrawTextSimple(dc,PreviewLabel(previewFrames_[i].seconds),label,11,FW_SEMIBOLD,RGB(200,206,218),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                }
                const int rows=static_cast<int>((previewFrames_.size()+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
                y+=rows*(cardH+gap)+10;
                TrimPreviewMemory();
            }
        }

        detailsContentBottom_=y+20+contentOffset;

        // Info footer is a fixed top interaction layer.  It may visually sit over
        // scrolling preview cards, but nothing underneath it is allowed to receive clicks.
        detailsFooterRect_ = RECT{0, footerTop, rc.right, rc.bottom};
        PaintFooterBackground(dc, rc);
        backRect_=StandardBackRect(rc); DrawButton(dc,backRect_,L"Back");
        if(item.isVideo){
            imageDetailsSlideshowRect_={0,0,0,0};
            const int gap=10, playW=145;
            playRect_={backRect_.right+gap,backRect_.top,backRect_.right+gap+playW,backRect_.bottom};
            DrawButton(dc,playRect_,L"Play video",true);
        } else {
            playRect_={0,0,0,0};
            // The same slideshow icon used in the Library is available while an image is open.
            imageDetailsSlideshowRect_={backRect_.right+10,backRect_.top,backRect_.right+56,backRect_.bottom};
            DrawSlideshowButton(dc,imageDetailsSlideshowRect_,slideshowActive_);
        }
        std::wstring meta=item.isVideo?(item.vr.vr?(item.vr.projection==2?L"VR180":L"360 VR"):L"Video"):L"Image";
        RECT metaTop{std::max<LONG>(300,rc.right/2-150),rc.bottom-62,std::min<LONG>(rc.right-20,rc.right/2+150),rc.bottom-43};
        DrawTextSimple(dc,meta,metaTop,13,FW_SEMIBOLD,RGB(165,172,185),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.isVideo){
            const double duration=detailsDurationSeconds_.load(std::memory_order_relaxed);
            RECT durationRect{metaTop.left,rc.bottom-43,metaTop.right,rc.bottom-8};
            DrawTextSimple(dc,duration>0.0?FormatTime(duration):L"--:--",durationRect,22,FW_SEMIBOLD,RGB(205,210,220),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }

    std::vector<SearchHit> BuildSearchHits() const {
        std::vector<SearchHit> hits;
        const std::wstring needle = ToLower(searchQuery_);
        if (needle.empty()) return hits;
        auto addMatches = [&](const std::vector<MediaItem>& items, Category cat) {
            for (size_t i=0; i<items.size(); ++i) {
                const std::wstring title = ToLower(items[i].title);
                const std::wstring path = ToLower(items[i].path);
                if (title.find(needle) != std::wstring::npos || path.find(needle) != std::wstring::npos) hits.push_back({cat,i});
            }
        };
        addMatches(videos_, Category::Videos);
        addMatches(images_, Category::Images);
        return hits;
    }

    void ClampSearchScroll() {
        RECT rc{}; GetClientRect(hwnd_, &rc);
        const auto hits = BuildSearchHits();
        const int rowH = 62;
        const int usable = std::max(1, static_cast<int>(rc.bottom) - 154 - 72);
        const int total = static_cast<int>(hits.size()) * rowH;
        searchScrollY_ = std::clamp(searchScrollY_, 0, std::max(0, total - usable));
    }

    void PaintSearch(HDC dc, RECT rc) {
        RECT top{0,0,rc.right,64}; HBRUSH tb=CreateSolidBrush(RGB(18,21,28)); FillRect(dc,&top,tb); DeleteObject(tb);
        RECT logo{20,0,std::min<LONG>(430,rc.right-20),64}; DrawTextSimple(dc,L"Visual MediaPlayer",logo,23,FW_BOLD);

        RECT searchBox{24,80,rc.right-24,126}; FillRound(dc,searchBox,RGB(31,35,46),12);
        std::wstring display = searchQuery_.empty() ? L"Type to search..." : searchQuery_;
        RECT queryRect{searchBox.left+16,searchBox.top,searchBox.right-16,searchBox.bottom};
        DrawTextSimple(dc,display,queryRect,18,FW_SEMIBOLD,searchQuery_.empty()?RGB(140,146,159):RGB(244,246,250));

        const auto hits = BuildSearchHits();
        RECT countRect{24,130,rc.right-24,153};
        DrawTextSimple(dc,std::to_wstring(hits.size())+L" result"+(hits.size()==1?L"":L"s"),countRect,12,FW_NORMAL,RGB(155,161,174));

        const int rowH=62; const int startY=154-searchScrollY_; const int bottomLimit=static_cast<int>(rc.bottom)-66;
        searchResultRects_.clear(); searchResultRects_.reserve(hits.size());
        for(size_t i=0;i<hits.size();++i){
            const int topY=startY+static_cast<int>(i)*rowH;
            RECT row{24,topY,rc.right-24,topY+54};
            searchResultRects_.push_back(row);
            if(row.bottom<154||row.top>bottomLimit) continue;
            FillRound(dc,row,RGB(28,32,42),9);
            const MediaItem& item = hits[i].category==Category::Videos ? videos_[hits[i].index] : images_[hits[i].index];
            RECT tag{row.left+10,row.top+10,row.left+76,row.bottom-10}; FillRound(dc,tag,RGB(40,45,58),7);
            DrawTextSimple(dc,hits[i].category==Category::Videos?L"VIDEO":L"IMAGE",tag,10,FW_BOLD,RGB(210,216,228),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            RECT name{row.left+90,row.top+3,row.right-12,row.top+29}; DrawTextSimple(dc,item.title,name,15,FW_SEMIBOLD);
            RECT path{row.left+90,row.top+28,row.right-12,row.bottom-3}; DrawTextSimple(dc,item.path,path,11,FW_NORMAL,RGB(145,151,164));
        }

        PaintFooterBackground(dc,rc);
        searchBackRect_=StandardBackRect(rc); DrawButton(dc,searchBackRect_,L"Back");
        RECT hint{120,rc.bottom-64,rc.right-24,rc.bottom-22};
        DrawTextSimple(dc,L"Type to refine  •  Backspace to edit  •  Esc to close",hint,12,FW_NORMAL,RGB(155,161,174));
    }

    void ResetPreviewZoom() {
        previewCardWidth_ = kDefaultPreviewCardWidth;
    }

    void ResetLibraryZoom() {
        libraryCardWidth_ = kDefaultLibraryCardWidth;
    }

    void OpenSearchHit(const SearchHit& hit) {
        StopImageSlideshow();
        ResetPreviewZoom();
        ResetLibraryZoom();
        detailsOriginFolder_ = currentFolder_;
        category_ = hit.category; selected_ = hit.index; mode_ = Mode::Details;
        searchQuery_.clear(); searchVisible_ = false; filteredIndices_.clear(); filterDirty_ = true; searchScrollY_ = 0; InvalidateRect(hwnd_, nullptr, TRUE);
    }

    std::vector<size_t> ImageIndicesInCurrentFolder() const {
        std::vector<size_t> out;
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
        if (category_ != Category::Images) return;
        slideshowIndices_ = ImageIndicesInCurrentFolder();
        if (slideshowIndices_.empty()) return;
        slideshowActive_ = true;
        slideshowPos_ = 0;
        detailsOriginFolder_ = currentFolder_;
        selected_ = slideshowIndices_.front();
        detailsScrollY_ = 0;
        ResetPreviewZoom();
        ResetLibraryZoom();
        mode_ = Mode::Details;
        SetTimer(hwnd_, kSlideshowTimerId, 3000, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void StartImageSlideshowFromSelected() {
        StopImageSlideshow();
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
                searchScrollY_=0;
                filteredIndices_.clear();
                filterDirty_=true;

                fs::path parent=fs::path(currentFolder_).parent_path();
                currentFolder_=parent.empty()?folder_:parent.lexically_normal().wstring();
                selected_=0; scrollY_=0; ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); return;
            }
            if(PtInRect(&videosTabRect_,p)){ StopImageSlideshow(); category_=Category::Videos; selected_=0; scrollY_=0; filterDirty_=true; ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); return; }
            if(PtInRect(&imagesTabRect_,p)){ StopImageSlideshow(); category_=Category::Images; selected_=0; scrollY_=0; filterDirty_=true; ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); return; }
            if(category_==Category::Images && PtInRect(&slideshowRect_,p)){ StartImageSlideshow(); return; }
            if(PtInRect(&chooseRect_,p)){ StopImageSlideshow(); ChooseFolder(); return; }
            if(PtInRect(&rescanRect_,p)){ StopImageSlideshow(); Scan(); return; }
            RECT rc{}; GetClientRect(hwnd_,&rc);
            const int pad=kLibraryPad,gap=kLibraryGap,cardW=libraryCardWidth_;
            const int imageH=std::max(113,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
            const int cardH=imageH+kLibraryTitleHeight; const int clientWidth=std::max(1,static_cast<int>(rc.right-rc.left)-kLibraryScrollbarReserve);
            const int cols=std::max(1,(clientWidth-pad*2+gap)/(cardW+gap)); const int ly=y-82+scrollY_; if(ly<0) return;
            const int row=ly/(cardH+gap),col=(x-pad)/(cardW+gap); if(col<0||col>=cols) return;
            const int localX=(x-pad)%(cardW+gap),localY=ly%(cardH+gap); if(localX<0||localX>=cardW||localY<0||localY>=cardH) return;
            const size_t displayIndex=static_cast<size_t>(row)*static_cast<size_t>(cols)+static_cast<size_t>(col);
            const auto visibleFolders=VisibleFolderIndices();
            if(displayIndex<visibleFolders.size()){
                StopImageSlideshow();
                currentFolder_=folders_[visibleFolders[displayIndex]].path; selected_=0; scrollY_=0; filterDirty_=true; ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); return;
            }
            const auto& filtered=FilteredIndices();
            const size_t mediaDisplayIndex=displayIndex-visibleFolders.size();
            if(mediaDisplayIndex<filtered.size()){ StopImageSlideshow(); ResetPreviewZoom(); ResetLibraryZoom(); detailsOriginFolder_=currentFolder_; selected_=filtered[mediaDisplayIndex]; mode_=Mode::Details; detailsScrollY_=0; StartPreviewWorkerForSelected(); InvalidateRect(hwnd_,nullptr,FALSE); }
        } else if(mode_==Mode::Details){
            // Fixed footer controls always win hit-testing over scrollable content.
            if(PtInRect(&backRect_,p)){
                StopImageSlideshow(); StopPreviewWorker(); ClearPreviewBitmaps(); ResetPreviewZoom();
                mode_=Mode::Library; detailsScrollY_=0;
                std::error_code navEc;
                if(!detailsOriginFolder_.empty() && fs::exists(detailsOriginFolder_,navEc) && !navEc)
                    currentFolder_=fs::path(detailsOriginFolder_).lexically_normal().wstring();
                else if(!folder_.empty() && fs::exists(folder_,navEc) && !navEc)
                    currentFolder_=fs::path(folder_).lexically_normal().wstring();
                if(searchQuery_.empty()) searchVisible_=false;
                filteredIndices_.clear(); filterDirty_=true;
                ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); return;
            }
            if(category_==Category::Videos&&PtInRect(&playRect_,p)){ EnterPlayerAt(0.0); return; }
            if(category_==Category::Images&&PtInRect(&imageDetailsSlideshowRect_,p)){
                if(slideshowActive_) StopImageSlideshow();
                else StartImageSlideshowFromSelected();
                return;
            }

            // Treat the entire footer as an input barrier, including its transparent/empty
            // areas, so a preview card underneath can never receive a click through it.
            if(PtInRect(&detailsFooterRect_,p)) return;

            if(category_==Category::Videos){
                for(const auto& hit : previewHitRects_){
                    RECT r = hit.first;
                    if(PtInRect(&r,p)){ EnterPlayerAt(static_cast<double>(hit.second)); return; }
                }
            }
        } else if(mode_==Mode::Search){
            if(PtInRect(&searchBackRect_,p)){ mode_=searchReturnMode_; searchQuery_.clear(); searchScrollY_=0; InvalidateRect(hwnd_,nullptr,TRUE); return; }
            RECT rc{}; GetClientRect(hwnd_,&rc);
            if(y>=154 && y<rc.bottom-66){
                const int logicalY=y-154+searchScrollY_;
                if(logicalY>=0){
                    const int row=logicalY/62, localY=logicalY%62;
                    const auto hits=BuildSearchHits();
                    if(localY<54 && row>=0 && static_cast<size_t>(row)<hits.size()){ OpenSearchHit(hits[static_cast<size_t>(row)]); return; }
                }
            }
        }
    }

    void ChooseFolder() {
        StopImageSlideshow();
        ComPtr<IFileDialog> dlg; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dlg)))) return;
        DWORD opts=0; dlg->GetOptions(&opts); dlg->SetOptions(opts|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
        if(!folder_.empty()&&fs::exists(folder_)){
            ComPtr<IShellItem> start; if(SUCCEEDED(SHCreateItemFromParsingName(folder_.c_str(),nullptr,IID_PPV_ARGS(&start)))) dlg->SetFolder(start.Get());
        }
        if(SUCCEEDED(dlg->Show(hwnd_))){
            ComPtr<IShellItem> item; if(SUCCEEDED(dlg->GetResult(&item))){
                PWSTR p=nullptr; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){ folder_=p; persistentFolder_=folder_; CoTaskMemFree(p); SaveSettings(); Scan(); }
            }
        }
    }

    void Scan() {
        StopImageSlideshow();
        StopPreviewWorker(); ClearPreviewBitmaps(); StopThumbnailWorker(); ClearThumbs(videos_); ClearThumbs(images_); videos_.clear(); images_.clear(); folders_.clear(); detailsOriginFolder_.clear(); selected_=0; scrollY_=0; detailsScrollY_=0; ResetPreviewZoom(); ResetLibraryZoom();
        if(folder_.empty()||!fs::exists(folder_)){ currentFolder_.clear(); InvalidateRect(hwnd_,nullptr,TRUE); return; }
        folder_=fs::path(folder_).lexically_normal().wstring();
        currentFolder_=folder_;
        cacheDir_=fs::path(folder_)/L".visualmediaplayer-cache"/L"thumbs";
        std::error_code ec;
        fs::recursive_directory_iterator it(folder_,fs::directory_options::skip_permission_denied,ec),end;
        for(;it!=end;it.increment(ec)){
            if(ec){ec.clear();continue;}
            const auto p=it->path();
            if(it->is_directory(ec)){
                if(ToLower(p.filename().wstring())==L".visualmediaplayer-cache") { it.disable_recursion_pending(); continue; }
                LibraryFolder folderItem; folderItem.path=p.lexically_normal().wstring(); folderItem.name=p.filename().wstring();
                folders_.push_back(std::move(folderItem));
                continue;
            }
            if(!it->is_regular_file(ec)) continue;
            const std::wstring ext=p.extension().wstring();
            const bool video=IsVideoExtension(ext), image=IsImageExtension(ext); if(!video&&!image) continue;
            MediaItem item; item.path=p.lexically_normal().wstring(); item.title=p.stem().wstring(); std::replace(item.title.begin(),item.title.end(),L'_',L' ');
            item.isVideo=video; if(video) item.vr=DetectVR(item.path); item.cachePath=BuildCachePath(item.path); item.uiCachePath=BuildUiCachePath(item.path);
            item.searchText=ToLower(item.title+L"\n"+item.path);
            if(video) videos_.push_back(std::move(item)); else images_.push_back(std::move(item));
        }
        auto mediaSorter=[](const MediaItem&a,const MediaItem&b){return ToLower(a.title)<ToLower(b.title);};
        auto folderSorter=[](const LibraryFolder&a,const LibraryFolder&b){return ToLower(a.name)<ToLower(b.name);};
        std::sort(videos_.begin(),videos_.end(),mediaSorter); std::sort(images_.begin(),images_.end(),mediaSorter); std::sort(folders_.begin(),folders_.end(),folderSorter);
        filterDirty_=true; ClampScroll(); InvalidateRect(hwnd_,nullptr,FALSE); StartThumbnailWorker();
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
        const int footerTop=std::max(64,static_cast<int>(rc.bottom)-64);
        const int gap=12;
        const int cardW=previewCardWidth_;
        const int imageH=std::max(79,static_cast<int>(std::lround(static_cast<double>(cardW)*9.0/16.0)));
        const int cardH=imageH+24;
        const int availW=std::max(1,static_cast<int>(rc.right)-80);
        const int cols=std::max(1,(availW+gap)/(cardW+gap));
        const int rows=previewFrames_.empty()?0:static_cast<int>((previewFrames_.size()+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
        const int previewsHeight=previewFrames_.empty()?64:rows*(cardH+gap)+10;
        const int contentBottom=78+54+320+22+38+previewsHeight+20;
        const int maxScroll=std::max(0,contentBottom-footerTop+16);
        detailsScrollY_=std::clamp(detailsScrollY_,0,maxScroll);
    }

    std::wstring SettingsPath() const {
        wchar_t local[MAX_PATH]{}; if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local))) return L"VisualMediaPlayer.ini";
        fs::path dir=fs::path(local)/L"VisualMediaPlayer"; std::error_code ec; fs::create_directories(dir,ec); return (dir/L"settings.ini").wstring();
    }

    void LoadSettings() {
        wchar_t buf[32768]{}; GetPrivateProfileStringW(L"Library",L"Folder",L"",buf,static_cast<DWORD>(std::size(buf)),SettingsPath().c_str()); folder_=buf; persistentFolder_=folder_;
    }

    void SaveSettings() const { WritePrivateProfileStringW(L"Library",L"Folder",persistentFolder_.c_str(),SettingsPath().c_str()); }

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
        player_=std::make_unique<NativePlayer>(); const HRESULT hr=player_->Initialize(hwnd_,videoHwnd_);
        if(FAILED(hr)){MessageBoxW(hwnd_,(L"Could not initialize Direct3D 11 / Media Foundation.\n\n"+HrText(hr)).c_str(),L"Visual MediaPlayer",MB_ICONERROR);player_.reset();return false;}
        player_->SetVolume(volumeFraction_); Layout(); return true;
    }

    void EnterPlayer() { EnterPlayerAt(0.0); }

    void EnterPlayerAt(double startSeconds) {
        if(category_!=Category::Videos||selected_>=videos_.size()) return;
        // Every newly opened video starts at the standard 30% volume.
        // Volume changes are intentionally limited to the current playback session.
        volumeFraction_ = 0.30;
        StopPreviewWorker();
        mode_=Mode::Player; if(!CreatePlayerControls()){mode_=Mode::Details;StartPreviewWorkerForSelected();InvalidateRect(hwnd_,nullptr,TRUE);return;}
        ShowWindow(videoHwnd_,SW_SHOW); playerControlsVisible_=false; controlsHideDeadline_=0; lastCursorValid_=false; controlsFading_=false; controlsAlpha_=0;
        if(controlsHwnd_) SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
        Layout();
        const HRESULT hr=player_->Open(videos_[selected_].path,videos_[selected_].vr,std::max(0.0,startSeconds));
        if(FAILED(hr)){MessageBoxW(hwnd_,(L"Could not open file.\n\n"+HrText(hr)).c_str(),L"Playback error",MB_ICONERROR);LeavePlayer();return;}
        player_->SetVolume(volumeFraction_); SetFocus(videoHwnd_); UpdateWindowTitle();
    }

    void LeavePlayer() {
        if(fullscreen_) ToggleFullscreen(); if(player_) player_->Pause(); volumeFraction_=0.30; playerControlsVisible_=false; controlsFading_=false; controlsAlpha_=0;
        if(controlsHwnd_){ SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA); ShowWindow(controlsHwnd_,SW_HIDE); } if(videoHwnd_) ShowWindow(videoHwnd_,SW_HIDE);
        mode_=Mode::Details; SetWindowTextW(hwnd_,L"Visual MediaPlayer"); StartPreviewWorkerForSelected(); InvalidateRect(hwnd_,nullptr,TRUE);
    }

    void BeginControlsFade(BYTE target) {
        if(!controlsHwnd_) return;
        UpdateControlsFade();
        controlsFadeFrom_=controlsAlpha_;
        controlsFadeTo_=target;
        controlsFadeStart_=GetTickCount64();
        controlsFading_=controlsFadeFrom_!=controlsFadeTo_;
        if(target>0 && !IsWindowVisible(controlsHwnd_)) ShowWindow(controlsHwnd_,SW_SHOWNOACTIVATE);
        if(!controlsFading_ && target==0){ playerControlsVisible_=false; ShowWindow(controlsHwnd_,SW_HIDE); }
    }

    void UpdateControlsFade() {
        if(!controlsFading_ || !controlsHwnd_) return;
        const ULONGLONG now=GetTickCount64();
        const float raw=static_cast<float>(now-controlsFadeStart_) / static_cast<float>(kUiAnimationDurationMs);
        const float t=EaseUi(raw);
        const int value=static_cast<int>(std::lround(controlsFadeFrom_ + (static_cast<int>(controlsFadeTo_)-static_cast<int>(controlsFadeFrom_))*t));
        controlsAlpha_=static_cast<BYTE>(std::clamp(value,0,255));
        SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
        if(raw>=1.0f){
            controlsAlpha_=controlsFadeTo_; controlsFading_=false;
            SetLayeredWindowAttributes(controlsHwnd_,0,controlsAlpha_,LWA_ALPHA);
            if(controlsAlpha_==0){ playerControlsVisible_=false; ShowWindow(controlsHwnd_,SW_HIDE); }
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
            Layout(); if(controlsHwnd_)ShowWindow(controlsHwnd_,SW_SHOWNOACTIVATE); BeginControlsFade(kControlsVisibleAlpha); InvalidateControls();
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

    void InvalidateControls() { if(controlsHwnd_&&playerControlsVisible_) InvalidateRect(controlsHwnd_,nullptr,FALSE); }

    bool FindAnimatedButtonRect(HWND owner, POINT p, RECT& out) const {
        auto hit=[&](const RECT& r)->bool{ if(!EmptyRectValue(r) && PtInRect(&r,p)){ out=r; return true; } return false; };
        if(owner==controlsHwnd_){
            if(hit(playerBackRect_)) return true;
            if(player_ && player_->VR().vr && hit(playerVrToggleRect_)) return true;
            if(hit(playerPlayRect_)) return true;
            if(hit(playerAutoNextRect_)) return true;
            if(hit(playerFullRect_)) return true;
            return false;
        }
        if(owner!=hwnd_) return false;
        if(mode_==Mode::Library){
            if(!IsAtLibraryRoot() && hit(backRect_)) return true;
            if(hit(videosTabRect_)) return true;
            if(hit(imagesTabRect_)) return true;
            if(category_==Category::Images && hit(slideshowRect_)) return true;
            if(hit(chooseRect_)) return true;
            if(hit(rescanRect_)) return true;
        } else if(mode_==Mode::Details){
            if(hit(backRect_)) return true;
            if(category_==Category::Videos && hit(playRect_)) return true;
            if(category_==Category::Images && hit(imageDetailsSlideshowRect_)) return true;
        }
        return false;
    }

    void StartUiAnimationTimer() {
        if(hwnd_) SetTimer(hwnd_,kUiAnimationTimerId,16,nullptr);
    }

    void UpdateAnimatedHover(HWND owner,int x,int y) {
        POINT p{x,y}; RECT next{}; HWND nextOwner=nullptr;
        if(FindAnimatedButtonRect(owner,p,next)) nextOwner=owner;
        if(nextOwner==hoverOwner_ && SameRect(next,hoverRect_)) return;
        hoverPreviousOwner_=hoverOwner_; hoverPreviousRect_=hoverRect_;
        hoverOwner_=nextOwner; hoverRect_=next; hoverTransitionStart_=GetTickCount64();
        StartUiAnimationTimer();
        if(owner==controlsHwnd_) InvalidateControls(); else if(hwnd_) InvalidateRect(hwnd_,nullptr,FALSE);
    }

    void ClearAnimatedHover(HWND owner) {
        if(hoverOwner_!=owner) return;
        hoverPreviousOwner_=hoverOwner_; hoverPreviousRect_=hoverRect_;
        hoverOwner_=nullptr; hoverRect_=RECT{}; hoverTransitionStart_=GetTickCount64();
        StartUiAnimationTimer();
    }

    void TickUiAnimations() {
        const ULONGLONG now=GetTickCount64();
        bool active=false;
        if(hoverTransitionStart_!=0){
            if(now-hoverTransitionStart_>=kUiAnimationDurationMs){
                hoverTransitionStart_=0; hoverPreviousOwner_=nullptr; hoverPreviousRect_=RECT{};
            } else active=true;
        }
        if(slideshowFadeActive_){
            if(now-slideshowFadeStart_>=kUiAnimationDurationMs){
                slideshowFadeActive_=false; slideshowPreviousIndex_=static_cast<size_t>(-1);
            } else active=true;
        }
        if(hwnd_) InvalidateRect(hwnd_,nullptr,FALSE);
        if(controlsHwnd_ && playerControlsVisible_) InvalidateRect(controlsHwnd_,nullptr,FALSE);
        if(!active && hwnd_) KillTimer(hwnd_,kUiAnimationTimerId);
    }

    void HandlePlaybackEnded() {
        if(!autoNext_||videos_.empty()||selected_+1>=videos_.size()) return; ++selected_; seekFraction_=0.0;
        if(player_){
            // Auto Next is a new video session too, so restore the standard volume.
            volumeFraction_ = 0.30;
            const HRESULT hr=player_->Open(videos_[selected_].path,videos_[selected_].vr);
            if(FAILED(hr)){MessageBoxW(hwnd_,(L"Could not open the next video.\n\n"+HrText(hr)).c_str(),L"Playback error",MB_ICONERROR);return;}
            player_->SetVolume(volumeFraction_); UpdateWindowTitle();
        }
    }

    void Layout() {
        if(mode_!=Mode::Player||!videoHwnd_) return; RECT rc{}; GetClientRect(hwnd_,&rc);
        const int cw=std::max(1,static_cast<int>(rc.right-rc.left)),ch=std::max(1,static_cast<int>(rc.bottom-rc.top));
        MoveWindow(videoHwnd_,0,0,cw,ch,TRUE);
        if(!controlsHwnd_) return;
        // Reserve a small strip above the timeline for the press/drag time bubble.
        // Existing controls are offset by the same amount, so their on-screen position does not move.
        constexpr int hoverStrip = 28;
        const int oh=122+hoverStrip;
        POINT clientOrigin{0,0}; ClientToScreen(hwnd_,&clientOrigin);
        const int overlayX=clientOrigin.x;
        const int overlayY=clientOrigin.y+std::max(0,ch-oh);
        SetWindowPos(controlsHwnd_,HWND_TOP,overlayX,overlayY,cw,oh,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        if(!playerControlsVisible_){ShowWindow(controlsHwnd_,SW_HIDE);return;}
        ShowWindow(controlsHwnd_,SW_SHOWNOACTIVATE);
        const int pad=20; seekRect_={pad,10+hoverStrip,cw-pad,30+hoverStrip};
        // Keep the seek bar and button row at their established screen positions.
        constexpr int rowDrop = 10;
        constexpr int footerButtonH = 38;
        const int textButtonTop = 60 + rowDrop + hoverStrip;
        playerBackRect_={20,textButtonTop,100,textButtonTop+footerButtonH};
        playerVrToggleRect_={112,textButtonTop,192,textButtonTop+footerButtonH};
        playerPlayRect_={cw/2-27,50+rowDrop+hoverStrip,cw/2+27,104+rowDrop+hoverStrip};
        const int timeRight=static_cast<int>(playerPlayRect_.left)-16;
        const int timeLeft=std::max(static_cast<int>(playerVrToggleRect_.right)+16,timeRight-210);
        playerTimeRect_={timeLeft,textButtonTop,std::max(timeLeft,timeRight),textButtonTop+footerButtonH};
        playerFullRect_={cw-pad-48,54+rowDrop+hoverStrip,cw-pad,102+rowDrop+hoverStrip}; playerAutoNextRect_={playerFullRect_.left-58,54+rowDrop+hoverStrip,playerFullRect_.left-10,102+rowDrop+hoverStrip};
        const int volumeRight=playerAutoNextRect_.left-18,volumeLeft=std::max(cw/2+90,volumeRight-190);
        volumeRect_={volumeLeft,68+rowDrop+hoverStrip,volumeRight,86+rowDrop+hoverStrip}; volumeLabelRect_={volumeLeft-42,60+rowDrop+hoverStrip,volumeLeft-4,94+rowDrop+hoverStrip}; InvalidateControls();
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
        if(PtInRect(&playerPlayRect_,p)){if(player_)player_->PlayPause();InvalidateControls();return;}
        if(PtInRect(&playerAutoNextRect_,p)){autoNext_=!autoNext_;InvalidateControls();return;}
        if(PtInRect(&playerFullRect_,p)){ToggleFullscreen();PlayerActivity(true);return;}
        if(PtInRect(&seekRect_,p)){SetSeekFromX(x,true);return;}
        if(PtInRect(&volumeRect_,p)){SetVolumeFromX(x);return;}
    }

    void UpdateSeekUi() {
        static ULONGLONG last=0; const ULONGLONG now=GetTickCount64(); if(now-last<100)return;last=now;
        if(!player_)return;const double d=player_->Duration(),t=player_->CurrentTime();if(d>0.0&&!seekDragging_)seekFraction_=std::clamp(t/d,0.0,1.0);InvalidateControls();
    }

    void ToggleFullscreen() {
        if(mode_!=Mode::Player)return;fullscreen_=!fullscreen_;
        if(fullscreen_){savedStyle_=static_cast<DWORD>(GetWindowLongPtrW(hwnd_,GWL_STYLE));GetWindowRect(hwnd_,&savedRect_);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&mi);SetWindowLongPtrW(hwnd_,GWL_STYLE,savedStyle_&~WS_OVERLAPPEDWINDOW);SetWindowPos(hwnd_,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}
        else{SetWindowLongPtrW(hwnd_,GWL_STYLE,savedStyle_);SetWindowPos(hwnd_,nullptr,savedRect_.left,savedRect_.top,savedRect_.right-savedRect_.left,savedRect_.bottom-savedRect_.top,SWP_FRAMECHANGED|SWP_NOZORDER);}Layout();
    }

    void UpdateWindowTitle() {
        if(mode_!=Mode::Player||selected_>=videos_.size())return;
        SetWindowTextW(hwnd_,(L"Visual MediaPlayer - "+videos_[selected_].title).c_str());
    }

    HINSTANCE inst_{}; HWND hwnd_{}; Mode mode_=Mode::Library; Mode searchReturnMode_=Mode::Library; Category category_=Category::Videos;
    std::wstring folder_; std::wstring persistentFolder_; std::wstring currentFolder_; std::wstring detailsOriginFolder_; fs::path cacheDir_; std::vector<MediaItem> videos_,images_; std::vector<LibraryFolder> folders_; size_t selected_=0; int scrollY_=0; int detailsScrollY_=0; int detailsContentBottom_=0; int previewCardWidth_=kDefaultPreviewCardWidth; int libraryCardWidth_=kDefaultLibraryCardWidth;
    std::wstring searchQuery_; bool searchVisible_=false; bool filterDirty_=true; std::vector<size_t> filteredIndices_;
    int searchScrollY_=0; std::vector<RECT> searchResultRects_;
    bool slideshowActive_=false; size_t slideshowPos_=0; std::vector<size_t> slideshowIndices_;
    bool slideshowFadeActive_=false; size_t slideshowPreviousIndex_=static_cast<size_t>(-1); ULONGLONG slideshowFadeStart_=0;
    HWND paintOwner_=nullptr; HWND hoverOwner_=nullptr; HWND hoverPreviousOwner_=nullptr; RECT hoverRect_{},hoverPreviousRect_{}; ULONGLONG hoverTransitionStart_=0;
    RECT chooseRect_{},rescanRect_{},videosTabRect_{},imagesTabRect_{},slideshowRect_{},imageDetailsSlideshowRect_{},backRect_{},playRect_{},searchBackRect_{},searchBoxRect_{},detailsFooterRect_{},previewZoomRect_{};
    RECT libraryScrollTrackRect_{}, libraryScrollThumbRect_{}; bool libraryScrollDragging_=false; int libraryScrollDragOffset_=0;
    std::map<uint64_t,HFONT> fontCache_; HDC backDC_{}; HBITMAP backBitmap_{}; HGDIOBJ backOldBitmap_{}; int backW_=0,backH_=0;
    HWND videoHwnd_{},controlsHwnd_{}; std::unique_ptr<NativePlayer> player_;
    BYTE controlsAlpha_=0,controlsFadeFrom_=0,controlsFadeTo_=0; ULONGLONG controlsFadeStart_=0; bool controlsFading_=false;
    RECT playerBackRect_{},playerVrToggleRect_{},playerPlayRect_{},playerFullRect_{},playerAutoNextRect_{},seekRect_{},volumeRect_{},volumeLabelRect_{},playerTimeRect_{};
    double seekFraction_=0.0,volumeFraction_=0.30; bool fullscreen_=false,seekDragging_=false,volumeDragging_=false,autoNext_=false,playerControlsVisible_=false;
    bool seekHoverVisible_=false; int seekHoverX_=0;
    ULONGLONG controlsHideDeadline_=0; POINT lastCursorScreen_{}; bool lastCursorValid_=false; DWORD savedStyle_{}; RECT savedRect_{};
    ULONG_PTR gdiplusToken_=0; std::thread thumbThread_; std::atomic<bool> thumbStop_{false};
    std::wstring previewDir_; std::vector<PreviewFrame> previewFrames_; std::vector<std::pair<RECT,int>> previewHitRects_; std::thread previewThread_; std::atomic<bool> previewStop_{false}; std::atomic<double> detailsDurationSeconds_{0.0};
};

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    App app; if(!app.Initialize(hInst)) return 1;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 2) app.OpenExternalMedia(argv[1]);
        LocalFree(argv);
    }
    return app.Run();
}
