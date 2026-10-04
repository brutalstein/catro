#pragma once

#include <catro/platform/windows/screen_capture.hpp>

#include <winrt/Microsoft.UI.Xaml.h>

namespace catro::shell {

// A GPU with its own memory streams 1440p60 comfortably; integrated graphics get gentler presets.
[[nodiscard]] bool strong_gpu() noexcept;

// One row of the share picker: the program's icon and the name a person recognizes
// ("Brave - YouTube", "Counter-Strike 2", "Screen 1 (main)"). display_number counts screens.
[[nodiscard]] winrt::Microsoft::UI::Xaml::FrameworkElement share_source_row(
    const catro::platform::windows::CaptureSource& source, int display_number);

} // namespace catro::shell
