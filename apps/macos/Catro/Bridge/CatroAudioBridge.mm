#import "CatroCapabilitiesBridge.h"

#include <AudioViewModel.hpp>
#include <catro/platform/macos/audio_platform.hpp>

#include <memory>
#include <optional>
#include <string>

namespace {

namespace app = catro::app;
namespace audio = catro::audio;

NSString* copy_string(const std::string& text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding] ?: @"";
}

std::optional<catro::capabilities::AudioEndpointId> endpoint(NSString* _Nullable identifier) {
    if (identifier == nil) {
        return std::nullopt;
    }
    return catro::capabilities::AudioEndpointId{identifier.UTF8String, catro::capabilities::IdentityScope::persistent};
}

CatroTone tone(app::Tone value) {
    switch (value) {
    case app::Tone::positive:
        return CatroTonePositive;
    case app::Tone::caution:
        return CatroToneCaution;
    case app::Tone::critical:
        return CatroToneCritical;
    case app::Tone::neutral:
        break;
    }
    return CatroToneNeutral;
}

// The platform outlives the engine that opens streams on it.
struct AudioState {
    catro::platform::macos::CoreAudioPlatform platform;
    audio::AudioEngine engine{platform};
};

} // namespace

// Implemented in CatroCapabilitiesBridge.mm.
@interface CatroDiagnosticsRow ()
- (instancetype)initWithRow:(const app::DiagnosticsRow&)row;
@end

@implementation CatroAudioDevice
- (instancetype)initWithChoice:(const app::AudioDeviceChoice&)choice {
    if ((self = [super init])) {
        _identifier = choice.id ? copy_string(choice.id->value) : nil;
        _label = copy_string(choice.label);
    }
    return self;
}
@end

@implementation CatroAudioSession
- (instancetype)initWithStatistics:(const audio::AudioStatistics&)statistics {
    if ((self = [super init])) {
        const auto view = app::describe_audio(statistics);
        _status = copy_string(view.status);
        _tone = tone(view.tone);
        _running = view.running;
        // Levels are shown only while running.
        _hasInput = view.running && view.input.has_value();
        _inputFraction = _hasInput ? view.input->fraction : 0;
        _inputLevel = _hasInput ? copy_string(view.input->text) : @"—";
        _hasOutput = view.running && view.output.has_value();
        _outputFraction = _hasOutput ? view.output->fraction : 0;
        _outputLevel = _hasOutput ? copy_string(view.output->text) : @"—";
        NSMutableArray<CatroDiagnosticsRow*>* rows = [NSMutableArray arrayWithCapacity:view.rows.size()];
        for (const auto& row : view.rows) {
            [rows addObject:[[CatroDiagnosticsRow alloc] initWithRow:row]];
        }
        _rows = [rows copy];
        _failure = statistics.state == audio::EngineState::failed && statistics.error
                       ? copy_string(std::string(name(statistics.error->code)))
                       : nil;
    }
    return self;
}
@end

@implementation CatroAudioBridge {
    std::unique_ptr<AudioState> _state;
}

- (instancetype)init {
    if ((self = [super init])) {
        _state = std::make_unique<AudioState>();
    }
    return self;
}

- (void)startWithMode:(CatroAudioMode)mode
                input:(nullable NSString*)input
               output:(nullable NSString*)output
           completion:(void (^)(NSString* _Nullable error))completion {
    audio::SessionConfig config{.input = endpoint(input), .output = endpoint(output)};
    switch (mode) {
    case CatroAudioModeMeter:
        config.mode = audio::SessionMode::meter;
        break;
    case CatroAudioModeTone:
        config.mode = audio::SessionMode::tone;
        break;
    case CatroAudioModeMonitor:
        config.mode = audio::SessionMode::monitor;
        break;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      const auto error = _state->engine.start(config);
      NSString* message = error ? copy_string(std::string(name(error->code))) : nil;
      dispatch_async(dispatch_get_main_queue(), ^{
        completion(message);
      });
    });
}

- (void)stop {
    _state->engine.stop();
}

- (CatroAudioSession*)session {
    return [[CatroAudioSession alloc] initWithStatistics:_state->engine.statistics()];
}

@end
