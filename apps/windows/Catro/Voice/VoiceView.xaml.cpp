#include "pch.h"

#include "Voice/VoiceView.xaml.h"
#if __has_include("VoiceView.g.cpp")
#include "VoiceView.g.cpp"
#endif

namespace winrt::Catro::implementation {

void VoiceView::InitializeComponent() {
    VoiceViewT<VoiceView>::InitializeComponent();
}

} // namespace winrt::Catro::implementation
