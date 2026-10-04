#include "pch.h"

#include "Server/SharePicker.hpp"
#include "SharePresets.hpp"

#include <dxgi1_3.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Text.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace catro::shell {
namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
using winrt::to_hstring;

std::wstring process_image_path(std::uint32_t process_id) {
    std::wstring path;
    if (process_id == 0) {
        return path;
    }
    if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id)) {
        wchar_t buffer[MAX_PATH * 2];
        DWORD size = static_cast<DWORD>(std::size(buffer));
        if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
            path.assign(buffer, size);
        }
        CloseHandle(process);
    }
    return path;
}

// The program's own name from its version resource ("Visual Studio Code"), or empty.
std::wstring file_description(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD size = path.empty() ? 0 : GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0) {
        return {};
    }
    std::vector<std::byte> data(size);
    struct Translation {
        WORD language;
        WORD code_page;
    };
    Translation* translation = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\VarFileInfo\Translation",
                        reinterpret_cast<void**>(&translation), &length) ||
        length < sizeof(Translation)) {
        return {};
    }
    wchar_t key[64];
    swprintf_s(key, L"\StringFileInfo\%04x%04x\FileDescription",
               translation->language, translation->code_page);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &length) ||
        value == nullptr || length == 0) {
        return {};
    }
    return value;
}

// The program's 32-pixel icon as a XAML image, or null.
winrt::Microsoft::UI::Xaml::Media::ImageSource app_icon(const std::wstring& path) {
    SHFILEINFOW info{};
    if (path.empty() ||
        SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) == 0 ||
        info.hIcon == nullptr) {
        return nullptr;
    }
    winrt::Microsoft::UI::Xaml::Media::ImageSource result{nullptr};
    ICONINFO icon{};
    if (GetIconInfo(info.hIcon, &icon) && icon.hbmColor != nullptr) {
        BITMAP bitmap{};
        GetObjectW(icon.hbmColor, sizeof(bitmap), &bitmap);
        const int width = bitmap.bmWidth;
        const int height = bitmap.bmHeight;
        BITMAPINFO format{};
        format.bmiHeader.biSize = sizeof(format.bmiHeader);
        format.bmiHeader.biWidth = width;
        format.bmiHeader.biHeight = -height;
        format.bmiHeader.biPlanes = 1;
        format.bmiHeader.biBitCount = 32;
        format.bmiHeader.biCompression = BI_RGB;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        const HDC screen = GetDC(nullptr);
        const bool read = width > 0 && height > 0 &&
                          GetDIBits(screen, icon.hbmColor, 0, height, pixels.data(), &format,
                                    DIB_RGB_COLORS) == height;
        ReleaseDC(nullptr, screen);
        if (read) {
            bool has_alpha = false;
            for (std::size_t i = 3; i < pixels.size(); i += 4) {
                has_alpha = has_alpha || pixels[i] != 0;
            }
            // XAML wants premultiplied BGRA; old icons without alpha are opaque.
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                const unsigned alpha = has_alpha ? pixels[i + 3] : 255U;
                pixels[i] = static_cast<std::uint8_t>(pixels[i] * alpha / 255U);
                pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] * alpha / 255U);
                pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] * alpha / 255U);
                pixels[i + 3] = static_cast<std::uint8_t>(alpha);
            }
            winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap image(width, height);
            std::memcpy(image.PixelBuffer().data(), pixels.data(), pixels.size());
            image.Invalidate();
            result = image;
        }
    }
    if (icon.hbmColor != nullptr) {
        DeleteObject(icon.hbmColor);
    }
    if (icon.hbmMask != nullptr) {
        DeleteObject(icon.hbmMask);
    }
    DestroyIcon(info.hIcon);
    return result;
}

} // namespace

bool strong_gpu() noexcept {
    ::Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    ::Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapters1(0, &adapter)) || FAILED(adapter->GetDesc1(&desc))) {
        return false;
    }
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
           desc.DedicatedVideoMemory >= (std::size_t{2} << 30);
}

xaml::FrameworkElement share_source_row(
    const catro::platform::windows::CaptureSource& source, int display_number) {
    using catro::platform::windows::CaptureSourceKind;
    controls::Grid row;
    row.ColumnSpacing(12);
    row.Padding(xaml::Thickness{0.0, 6.0, 0.0, 6.0});
    controls::ColumnDefinition icon_column;
    icon_column.Width(xaml::GridLengthHelper::FromPixels(32.0));
    controls::ColumnDefinition text_column;
    text_column.Width(xaml::GridLengthHelper::FromValueAndType(1.0, xaml::GridUnitType::Star));
    row.ColumnDefinitions().Append(icon_column);
    row.ColumnDefinitions().Append(text_column);

    const std::wstring title = to_hstring(source.title).c_str();
    std::wstring name;
    std::wstring detail;
    xaml::FrameworkElement icon{nullptr};
    if (source.kind == CaptureSourceKind::display) {
        name = L"Screen " + std::to_wstring(display_number);
        if (source.primary) {
            name += L" (main)";
        }
        detail = std::to_wstring(source.width) + L"×" + std::to_wstring(source.height);
    } else {
        const auto path = process_image_path(source.process_id);
        name = catro::shell::share_window_name(
            title, to_hstring(source.process_name).c_str(), file_description(path),
            source.game);
        detail = source.game ? std::wstring{L"Game"} : name == title ? std::wstring{} : title;
        if (const auto image = app_icon(path)) {
            controls::Image picture;
            picture.Source(image);
            picture.Width(32.0);
            picture.Height(32.0);
            icon = picture;
        }
    }
    if (!icon) {
        controls::FontIcon glyph;
        glyph.Glyph(source.kind == CaptureSourceKind::display ? L"\xE7F4" : L"\xE737");
        glyph.FontSize(24.0);
        icon = glyph;
    }
    icon.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(icon);

    controls::StackPanel text;
    text.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid::SetColumn(text, 1);
    controls::TextBlock name_text;
    name_text.Text(name);
    name_text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    name_text.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    text.Children().Append(name_text);
    if (!detail.empty()) {
        controls::TextBlock detail_text;
        detail_text.Text(detail);
        detail_text.FontSize(12.0);
        detail_text.Opacity(0.7);
        detail_text.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
        text.Children().Append(detail_text);
    }
    row.Children().Append(text);
    return row;
}

} // namespace catro::shell
