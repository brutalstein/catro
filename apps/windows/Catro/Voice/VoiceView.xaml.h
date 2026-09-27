#pragma once

#include "VoiceView.g.h"

namespace winrt::Catro::implementation {

struct VoiceView : VoiceViewT<VoiceView> {
    VoiceView() = default;
    void InitializeComponent();
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct VoiceView : VoiceViewT<VoiceView, implementation::VoiceView> {};
} // namespace winrt::Catro::factory_implementation
