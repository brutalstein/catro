#pragma once

#include "AudioView.g.h"

#include <AudioViewModel.hpp>

#include <catro/platform/windows/audio_platform.hpp>

#include <vector>

namespace winrt::Catro::implementation {

// The audio test page: pick devices, run a meter, tone, or monitor session, and watch its
// statistics. Devices stay closed unless a session runs; leaving the page stops the session.
struct AudioView : AudioViewT<AudioView> {
    AudioView() = default;

    void InitializeComponent();

    // Rebuilds the pickers from the latest capability snapshot, keeping the current selections.
    void SetEndpoints(catro::capabilities::CapabilitySnapshot const& snapshot);
    void StopSession();

    void OnModeChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnStart(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStop(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    catro::audio::SessionMode Mode();
    void Refresh();

    catro::platform::windows::WasapiAudioPlatform platform_;
    catro::audio::AudioEngine engine_{platform_};
    std::vector<catro::app::AudioDeviceChoice> inputs_;
    std::vector<catro::app::AudioDeviceChoice> outputs_;
    std::vector<catro::app::DiagnosticsRow> rows_;
    Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {

struct AudioView : AudioViewT<AudioView, implementation::AudioView> {};

} // namespace winrt::Catro::factory_implementation
