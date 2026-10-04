#include <catro/platform/windows/screen_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

using namespace catro::platform::windows;
using namespace std::chrono_literals;

TEST_CASE("capture source sizes are physical pixels even for a DPI-unaware caller") {
    struct DpiScope {
        DPI_AWARENESS_CONTEXT previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
        ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
    } dpi;
    const auto sources = enumerate_capture_sources();
    CHECK(AreDpiAwarenessContextsEqual(
        GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_UNAWARE));
    const auto primary = std::ranges::find_if(sources, [](const auto& source) {
        return source.kind == CaptureSourceKind::display && source.primary;
    });
    if (primary == sources.end()) {
        SKIP("No display is attached to this machine");
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    REQUIRE(EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode));
    CHECK(primary->width == mode.dmPelsWidth);
    CHECK(primary->height == mode.dmPelsHeight);
}

TEST_CASE("Windows screen capture starts with no hidden GPU or capture work") {
    WindowsGraphicsCapture capture;
    const auto stats = capture.statistics();
    CHECK(stats.state == ScreenCaptureState::idle);
    CHECK_FALSE(stats.error);
    CHECK(stats.frames_received == 0);
    CHECK(stats.frames_published == 0);
    CHECK(stats.mailbox_overwrites == 0);
    CHECK(stats.contention_drops == 0);

    GpuCaptureFrame frame;
    CHECK_FALSE(capture.wait_for_latest(frame, 0ms));
}

TEST_CASE("stopping an idle Windows screen capture is idempotent") {
    WindowsGraphicsCapture capture;
    capture.stop();
    capture.stop();
    CHECK(capture.statistics().state == ScreenCaptureState::idle);
}

TEST_CASE("a window share captures only that window, games included") {
    // Desktop duplication copies whatever is on the monitor, so switching to another app would
    // put that app in the stream. A window or game share must stay on the window's own pixels.
    CaptureSource windowed{
        .kind = CaptureSourceKind::window,
        .native_handle = 1,
        .monitor_handle = 2,
        .title = "Counter-Strike 2",
        .process_name = "cs2.exe",
        .width = 2560,
        .height = 1600,
        .game = true,
    };
    CHECK(recommended_capture_backend(windowed) ==
          ScreenCaptureBackend::windows_graphics_capture);

    windowed.process_name = "notepad.exe";
    windowed.game = false;
    CHECK(recommended_capture_backend(windowed) ==
          ScreenCaptureBackend::windows_graphics_capture);

    CaptureSource display{
        .kind = CaptureSourceKind::display,
        .native_handle = 2,
        .monitor_handle = 2,
        .title = "Primary display",
        .width = 1920,
        .height = 1080,
        .primary = true,
    };
    CHECK(recommended_capture_backend(display) ==
          ScreenCaptureBackend::desktop_duplication);
}

TEST_CASE("capture source descriptors are inert value objects") {
    const CaptureSource source{
        .kind = CaptureSourceKind::window,
        .native_handle = 42,
        .title = "Example",
        .width = 1280,
        .height = 720,
    };
    CHECK(source.kind == CaptureSourceKind::window);
    CHECK(source.native_handle == 42);
    CHECK(source.width == 1280);
    CHECK(source.height == 720);
}

TEST_CASE("an occluded game window stays isolated even when desktop capture is requested") {
    struct Window {
        HWND handle;
        ~Window() { if (handle) DestroyWindow(handle); }
    } selected{CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Catro capture test", WS_POPUP | SS_BLACKRECT,
                              40, 40, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    if (!selected.handle) SKIP("No desktop is available for the native capture test");
    ShowWindow(selected.handle, SW_SHOWNOACTIVATE);
    UpdateWindow(selected.handle);
    const CaptureSource source{
        .kind = CaptureSourceKind::window,
        .native_handle = reinterpret_cast<std::uintptr_t>(selected.handle),
        .monitor_handle = reinterpret_cast<std::uintptr_t>(
            MonitorFromWindow(selected.handle, MONITOR_DEFAULTTONEAREST)),
        .width = 320,
        .height = 180,
        .game = true,
    };
    WindowsGraphicsCapture capture;
    ScreenCaptureConfig config;
    config.backend = ScreenCaptureBackend::desktop_duplication;
    const auto error = capture.start_source(source, config);
    if (error && (error->code == ScreenCaptureErrorCode::device_creation_failed ||
                  error->code == ScreenCaptureErrorCode::unsupported)) {
        SKIP("This machine has no usable native capture device");
    }
    REQUIRE_FALSE(error);
    REQUIRE(capture.statistics().backend == ScreenCaptureBackend::windows_graphics_capture);

    const auto center_pixel = [](const GpuCaptureFrame& frame) {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        frame.texture->GetDevice(&device);
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        D3D11_TEXTURE2D_DESC description{};
        frame.texture->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
        REQUIRE(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &readback)));
        context->CopyResource(readback.Get(), frame.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        REQUIRE(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        const auto* pixel = static_cast<const unsigned char*>(mapped.pData) +
            (description.Height / 2) * mapped.RowPitch + (description.Width / 2) * 4;
        const std::array<unsigned char, 3> result{pixel[0], pixel[1], pixel[2]};
        context->Unmap(readback.Get(), 0);
        return result;
    };
    GpuCaptureFrame initial;
    REQUIRE(capture.wait_for_latest(initial, 2s));
    const auto selected_color = center_pixel(initial);

    Window covering{CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Other app", WS_POPUP | SS_WHITERECT,
                                  40, 40, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    REQUIRE(covering.handle);
    ShowWindow(covering.handle, SW_SHOWNOACTIVATE);
    UpdateWindow(covering.handle);
    auto covering_source = source;
    covering_source.native_handle = reinterpret_cast<std::uintptr_t>(covering.handle);
    WindowsGraphicsCapture covering_capture;
    REQUIRE_FALSE(covering_capture.start_source(covering_source));
    GpuCaptureFrame covering_frame;
    REQUIRE(covering_capture.wait_for_latest(covering_frame, 2s));
    REQUIRE(center_pixel(covering_frame) != selected_color);
    // A static occluded window may stop producing new frames. As in the sender, retain its
    // last frame; any subsequent frame must still contain the selected window's pixels.
    std::this_thread::sleep_for(250ms);
    GpuCaptureFrame frame = initial;
    (void)capture.wait_for_latest(frame, 250ms);
    REQUIRE(frame.texture);
    CHECK(center_pixel(frame) == selected_color);
    CHECK_FALSE(capture.statistics().error);
    covering_capture.stop();
    capture.stop();
}
