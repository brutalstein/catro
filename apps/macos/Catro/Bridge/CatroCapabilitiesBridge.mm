#import "CatroCapabilitiesBridge.h"

#include <AudioViewModel.hpp>
#include <DiagnosticsViewModel.hpp>
#include <catro/platform/macos/capability_service.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

namespace app = catro::app;

NSString* copy_string(const std::string& text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding] ?: @"";
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

CatroFactState fact_state(catro::reporting::FactState value) {
    switch (value) {
    case catro::reporting::FactState::known:
        return CatroFactStateKnown;
    case catro::reporting::FactState::degraded:
        return CatroFactStateDegraded;
    case catro::reporting::FactState::unknown:
        return CatroFactStateUnknown;
    case catro::reporting::FactState::unavailable:
        return CatroFactStateUnavailable;
    case catro::reporting::FactState::plain:
        break;
    }
    return CatroFactStatePlain;
}

} // namespace

// Implemented in CatroAudioBridge.mm.
@interface CatroAudioDevice ()
- (instancetype)initWithChoice:(const app::AudioDeviceChoice&)choice;
@end

@implementation CatroDiagnosticsRow
- (instancetype)initWithRow:(const app::DiagnosticsRow&)row {
    if ((self = [super init])) {
        _depth = row.depth;
        _label = copy_string(row.label);
        _value = copy_string(row.value);
        _state = fact_state(row.state);
        _listItem = row.list_item;
    }
    return self;
}
@end

@implementation CatroDiagnosticsSection
- (instancetype)initWithSection:(const app::DiagnosticsSection&)section {
    if ((self = [super init])) {
        _identifier = copy_string(section.id);
        _title = copy_string(section.title);
        NSMutableArray<CatroDiagnosticsRow*>* rows = [NSMutableArray arrayWithCapacity:section.rows.size()];
        for (const auto& row : section.rows) {
            [rows addObject:[[CatroDiagnosticsRow alloc] initWithRow:row]];
        }
        _rows = [rows copy];
        _omittedRows = section.omitted_rows;
    }
    return self;
}
@end

@implementation CatroProbeHealth
- (instancetype)initWithProbe:(const app::ProbeHealth&)probe {
    if ((self = [super init])) {
        _probeID = copy_string(probe.probe_id);
        _outcome = copy_string(probe.outcome);
        _duration = copy_string(probe.duration);
        _facts = probe.facts;
        _tone = tone(probe.tone);
    }
    return self;
}
@end

@implementation CatroDiagnostics
- (instancetype)initWithModel:(const app::DiagnosticsModel&)model {
    if ((self = [super init])) {
        _headline = copy_string(model.headline);
        _detail = copy_string(model.detail);
        _tone = tone(model.tone);
        _generation = model.generation;
        NSMutableArray<CatroProbeHealth*>* probes = [NSMutableArray arrayWithCapacity:model.probes.size()];
        for (const auto& probe : model.probes) {
            [probes addObject:[[CatroProbeHealth alloc] initWithProbe:probe]];
        }
        _probes = [probes copy];
        NSMutableArray<NSString*>* changes = [NSMutableArray arrayWithCapacity:model.changes.size()];
        for (const auto& change : model.changes) {
            [changes addObject:copy_string(change)];
        }
        _changes = [changes copy];
        NSMutableArray<CatroDiagnosticsSection*>* sections = [NSMutableArray arrayWithCapacity:model.sections.size()];
        for (const auto& section : model.sections) {
            [sections addObject:[[CatroDiagnosticsSection alloc] initWithSection:section]];
        }
        _sections = [sections copy];
    }
    return self;
}
@end

// All methods are called on the main thread; the service calls back on its own queue.
@implementation CatroCapabilitiesBridge {
    NSURL* _probeURL;
    std::unique_ptr<catro::platform::macos::CapabilityService> _service;
    void (^_handler)(CatroDiagnostics*);
    std::optional<catro::reporting::CapabilityReport> _report;
}

- (instancetype)initWithProbeURL:(NSURL*)probeURL {
    if ((self = [super init])) {
        _probeURL = [probeURL copy];
    }
    return self;
}

- (void)dealloc {
    [self stop];
}

- (void)startWithHandler:(void (^)(CatroDiagnostics*))handler {
    if (_service) {
        return;
    }
    _handler = [handler copy];
    _service = std::make_unique<catro::platform::macos::CapabilityService>(
        std::filesystem::path(_probeURL.fileSystemRepresentation));
    __weak CatroCapabilitiesBridge* weakSelf = self;
    // Planning runs on the service queue; only the finished model crosses to the main queue.
    _service->start([weakSelf](catro::capabilities::SnapshotUpdate update) {
        auto report =
            catro::reporting::make_report(std::move(update.snapshot), catro::reporting::representative_request());
        auto model = app::build_diagnostics(report, update.changes);
        auto shared = std::make_shared<catro::reporting::CapabilityReport>(std::move(report));
        CatroDiagnostics* diagnostics = [[CatroDiagnostics alloc] initWithModel:model];
        dispatch_async(dispatch_get_main_queue(), ^{
            CatroCapabilitiesBridge* strongSelf = weakSelf;
            // A block queued before -stop must not reach a stopped bridge.
            if (strongSelf == nil || strongSelf->_handler == nil) {
                return;
            }
            strongSelf->_report = std::move(*shared);
            strongSelf->_handler(diagnostics);
        });
    });
}

- (void)refresh {
    if (_service) {
        _service->refresh(catro::capabilities::RefreshReason::diagnostics);
    }
}

- (void)stop {
    _handler = nil;
    if (_service) {
        _service->stop();
        _service.reset();
    }
}

- (nullable NSString*)exportReportWithFormat:(CatroExportFormat)format {
    if (!_report) {
        return nil;
    }
    const auto kind = format == CatroExportFormatJSON ? app::ExportFormat::json : app::ExportFormat::human;
    return copy_string(app::export_report(*_report, kind));
}

- (NSArray<CatroAudioDevice*>*)audioDevicesForInput:(BOOL)input {
    const auto direction = input ? catro::capabilities::AudioDirection::input : catro::capabilities::AudioDirection::output;
    const auto choices = app::audio_choices(_report ? _report->snapshot : catro::capabilities::CapabilitySnapshot{}, direction);
    NSMutableArray<CatroAudioDevice*>* devices = [NSMutableArray arrayWithCapacity:choices.size()];
    for (const auto& choice : choices) {
        [devices addObject:[[CatroAudioDevice alloc] initWithChoice:choice]];
    }
    return [devices copy];
}

@end
