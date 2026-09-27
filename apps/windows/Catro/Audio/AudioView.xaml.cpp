#include "pch.h"

#include "Audio/AudioView.xaml.h"
#if __has_include("AudioView.g.cpp")
#include "AudioView.g.cpp"
#endif

#include <algorithm>
#include <chrono>

namespace winrt::Catro::implementation {
namespace {

namespace xaml = Microsoft::UI::Xaml;
namespace controls = Microsoft::UI::Xaml::Controls;
namespace automation = Microsoft::UI::Xaml::Automation;
using catro::app::AudioDeviceChoice;
using catro::audio::AudioErrorCode;
using catro::audio::SessionMode;

// What the user can do about each failure on Windows.
hstring guidance(AudioErrorCode code) {
    switch (code) {
    case AudioErrorCode::permission_denied:
        return L"Allow microphone access for desktop apps in Settings > Privacy & security > Microphone.";
    case AudioErrorCode::device_in_use:
        return L"Another app holds the device exclusively. Close it and try again.";
    case AudioErrorCode::device_not_found:
        return L"The device is not available. Pick another device or refresh.";
    case AudioErrorCode::device_lost:
        return L"The device was removed or reconfigured. Pick a device and start again.";
    case AudioErrorCode::format_unsupported:
        return L"The device rejected the shared-mode audio format.";
    case AudioErrorCode::os_failure:
        break;
    }
    return L"Windows reported an audio failure.";
}

void fill(controls::ComboBox const& picker, std::vector<AudioDeviceChoice>& current,
          std::vector<AudioDeviceChoice> choices) {
    const auto index = picker.SelectedIndex();
    const auto selected = index > 0 && static_cast<std::size_t>(index) < current.size()
                              ? current[static_cast<std::size_t>(index)].id
                              : std::nullopt;
    current = std::move(choices);
    picker.Items().Clear();
    int32_t reselect = 0;
    for (std::size_t choice = 0; choice < current.size(); ++choice) {
        picker.Items().Append(box_value(to_hstring(current[choice].label)));
        if (selected && current[choice].id == selected) {
            reselect = static_cast<int32_t>(choice);
        }
    }
    picker.SelectedIndex(reselect);
}

std::optional<catro::capabilities::AudioEndpointId> chosen(controls::ComboBox const& picker,
                                                           std::vector<AudioDeviceChoice> const& choices) {
    const auto index = picker.SelectedIndex();
    return index >= 0 && static_cast<std::size_t>(index) < choices.size() ? choices[static_cast<std::size_t>(index)].id
                                                                         : std::nullopt;
}

controls::TextBlock text(xaml::FrameworkElement const& owner, wchar_t const* key, hstring const& value) {
    controls::TextBlock block;
    block.Style(owner.Resources().Lookup(box_value(key)).as<xaml::Style>());
    block.Text(value);
    return block;
}

} // namespace

void AudioView::InitializeComponent() {
    AudioViewT<AudioView>::InitializeComponent();
    // Until the first snapshot arrives only the system default is offered.
    SetEndpoints({});
    timer_ = DispatcherQueue().CreateTimer();
    timer_.Interval(std::chrono::milliseconds(100));
    timer_.Tick([this](auto&&, auto&&) { Refresh(); });
    Unloaded([this](auto&&, auto&&) { StopSession(); });
    // Selecting after load: the handler reads named elements that exist only now.
    Modes().SelectedIndex(0);
}

void AudioView::SetEndpoints(catro::capabilities::CapabilitySnapshot const& snapshot) {
    fill(InputPicker(), inputs_, catro::app::audio_choices(snapshot, catro::capabilities::AudioDirection::input));
    fill(OutputPicker(), outputs_, catro::app::audio_choices(snapshot, catro::capabilities::AudioDirection::output));
}

SessionMode AudioView::Mode() {
    switch (Modes().SelectedIndex()) {
    case 1:
        return SessionMode::tone;
    case 2:
        return SessionMode::monitor;
    default:
        return SessionMode::meter;
    }
}

void AudioView::OnModeChanged(IInspectable const&, controls::SelectionChangedEventArgs const&) {
    const auto mode = Mode();
    InputPicker().IsEnabled(mode != SessionMode::tone);
    OutputPicker().IsEnabled(mode != SessionMode::meter);
    HeadphonesNotice().IsOpen(mode == SessionMode::monitor);
}

void AudioView::OnStart(IInspectable const&, xaml::RoutedEventArgs const&) {
    Failure().IsOpen(false);
    const catro::audio::SessionConfig config{
        .mode = Mode(),
        .input = chosen(InputPicker(), inputs_),
        .output = chosen(OutputPicker(), outputs_),
    };
    // ponytail: opening blocks the UI thread for the device activation (typically well under
    // 100 ms); move start to a worker if slow Bluetooth endpoints make the page stutter.
    if (const auto error = engine_.start(config)) {
        Failure().Title(L"Could not start: " + to_hstring(name(error->code)));
        Failure().Message(guidance(error->code));
        Failure().IsOpen(true);
        Refresh();
        return;
    }
    timer_.Start();
    Refresh();
}

void AudioView::OnStop(IInspectable const&, xaml::RoutedEventArgs const&) {
    StopSession();
}

void AudioView::StopSession() {
    engine_.stop();
    Refresh();
}

void AudioView::Refresh() {
    const auto statistics = engine_.statistics();
    const auto view = catro::app::describe_audio(statistics);
    if (!view.running) {
        timer_.Stop();
    }
    StartButton().IsEnabled(!view.running);
    StopButton().IsEnabled(view.running);
    Modes().IsEnabled(!view.running);
    if (view.running) {
        InputPicker().IsEnabled(false);
        OutputPicker().IsEnabled(false);
    } else {
        OnModeChanged(nullptr, nullptr);
    }
    SessionStatus().Text(to_hstring(view.status));
    if (statistics.state == catro::audio::EngineState::failed && statistics.error && !Failure().IsOpen()) {
        Failure().Title(L"Session stopped: " + to_hstring(name(statistics.error->code)));
        Failure().Message(guidance(statistics.error->code));
        Failure().IsOpen(true);
    }

    const auto level = [](controls::ProgressBar const& meter, controls::TextBlock const& label,
                          std::optional<catro::app::AudioLevel> const& value) {
        meter.Value(value ? value->fraction : 0.0);
        label.Text(value ? to_hstring(value->text) : hstring(L"—"));
    };
    level(InputMeter(), InputLevel(), view.running ? view.input : std::nullopt);
    level(OutputMeter(), OutputLevel(), view.running ? view.output : std::nullopt);

    // Rebuilt only on change, so assistive technology is not flooded ten times a second.
    if (view.rows == rows_) {
        return;
    }
    rows_ = view.rows;
    auto details = Details().Children();
    details.Clear();
    for (const auto& row : rows_) {
        controls::Grid grid;
        grid.ColumnSpacing(16);
        controls::ColumnDefinition label_column;
        label_column.Width(xaml::GridLengthHelper::FromPixels(200));
        controls::ColumnDefinition value_column;
        value_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
        grid.ColumnDefinitions().Append(label_column);
        grid.ColumnDefinitions().Append(value_column);
        const auto label = to_hstring(row.label);
        const auto value = to_hstring(row.value);
        grid.Children().Append(text(*this, L"LabelText", label));
        auto value_block = text(*this, L"ValueText", value);
        controls::Grid::SetColumn(value_block, 1);
        grid.Children().Append(value_block);
        automation::AutomationProperties::SetName(grid, label + L": " + value);
        details.Append(grid);
    }
}

} // namespace winrt::Catro::implementation
