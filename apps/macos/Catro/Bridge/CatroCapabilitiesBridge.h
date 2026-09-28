#import <Foundation/Foundation.h>

// The only surface Swift sees of the C++ core: immutable value objects copied from the shared
// diagnostics view model, and a service handle with a start/refresh/stop lifecycle. No C++ type,
// CoreFoundation reference, or Metal object crosses this header.

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CatroTone) {
    CatroToneNeutral,
    CatroTonePositive,
    CatroToneCaution,
    CatroToneCritical,
};

// How certain a presented value is; plain rows are structure or plan data, not evidence.
typedef NS_ENUM(NSInteger, CatroFactState) {
    CatroFactStatePlain,
    CatroFactStateKnown,
    CatroFactStateDegraded,
    CatroFactStateUnknown,
    CatroFactStateUnavailable,
};

typedef NS_ENUM(NSInteger, CatroExportFormat) {
    // Device names redacted, as in the default human report.
    CatroExportFormatText,
    // The complete canonical report.
    CatroExportFormatJSON,
};

__attribute__((objc_subclassing_restricted))
@interface CatroDiagnosticsRow : NSObject
@property(nonatomic, readonly) NSUInteger depth;
@property(nonatomic, readonly, copy) NSString* label;
// Empty on a heading row.
@property(nonatomic, readonly, copy) NSString* value;
@property(nonatomic, readonly) CatroFactState state;
@property(nonatomic, readonly) BOOL listItem;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroDiagnosticsSection : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* title;
@property(nonatomic, readonly, copy) NSArray<CatroDiagnosticsRow*>* rows;
// Rows past the presentation bound; the exported report still holds every one.
@property(nonatomic, readonly) NSUInteger omittedRows;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroProbeHealth : NSObject
@property(nonatomic, readonly, copy) NSString* probeID;
@property(nonatomic, readonly, copy) NSString* outcome;
@property(nonatomic, readonly, copy) NSString* duration;
@property(nonatomic, readonly) NSUInteger facts;
@property(nonatomic, readonly) CatroTone tone;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroDiagnostics : NSObject
@property(nonatomic, readonly, copy) NSString* headline;
@property(nonatomic, readonly, copy) NSString* detail;
@property(nonatomic, readonly) CatroTone tone;
@property(nonatomic, readonly) uint64_t generation;
@property(nonatomic, readonly, copy) NSArray<CatroProbeHealth*>* probes;
// Changed domains, then changed identifiers; empty on the first publication.
@property(nonatomic, readonly, copy) NSArray<NSString*>* changes;
// Fixed order: plan, fallbacks, decisions, profile, probes, devices, system, runtime, snapshot.
@property(nonatomic, readonly, copy) NSArray<CatroDiagnosticsSection*>* sections;
- (instancetype)init NS_UNAVAILABLE;
@end

typedef NS_ENUM(NSInteger, CatroAudioMode) {
    CatroAudioModeMeter,
    CatroAudioModeTone,
    CatroAudioModeMonitor,
};

__attribute__((objc_subclassing_restricted))
@interface CatroAudioDevice : NSObject
// Nil selects the system default device.
@property(nonatomic, readonly, copy, nullable) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* label;
- (instancetype)init NS_UNAVAILABLE;
@end

// One reading of the running session, copied from the shared audio view model.
__attribute__((objc_subclassing_restricted))
@interface CatroAudioSession : NSObject
@property(nonatomic, readonly, copy) NSString* status;
@property(nonatomic, readonly) CatroTone tone;
@property(nonatomic, readonly) BOOL running;
@property(nonatomic, readonly) BOOL hasInput;
@property(nonatomic, readonly) double inputFraction;
@property(nonatomic, readonly, copy) NSString* inputLevel;
@property(nonatomic, readonly) BOOL hasOutput;
@property(nonatomic, readonly) double outputFraction;
@property(nonatomic, readonly, copy) NSString* outputLevel;
@property(nonatomic, readonly, copy) NSArray<CatroDiagnosticsRow*>* rows;
// The failure that stopped the session, e.g. "microphone access denied"; nil otherwise.
@property(nonatomic, readonly, copy, nullable) NSString* failure;
- (instancetype)init NS_UNAVAILABLE;
@end

// Owns the CoreAudio platform and the audio engine. Devices open only while a session runs.
// Called on the main thread; starting may prompt asynchronously for microphone access.
__attribute__((objc_subclassing_restricted))
@interface CatroAudioBridge : NSObject
// The completion runs on the main thread with nil on success, otherwise the failure name.
- (void)startWithMode:(CatroAudioMode)mode
                input:(nullable NSString*)input
               output:(nullable NSString*)output
           completion:(void (^)(NSString* _Nullable error))completion
    NS_SWIFT_NAME(start(mode:input:output:completion:));
- (void)stop;
- (CatroAudioSession*)session NS_SWIFT_NAME(session());
@end

// Owns the macOS capability service. Updates are planned off the main thread and delivered on
// the main queue; the handler is never called after -stop returns.
__attribute__((objc_subclassing_restricted))
@interface CatroCapabilitiesBridge : NSObject
// The passive probe helper beside the application executable.
- (instancetype)initWithProbeURL:(NSURL*)probeURL NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

- (void)startWithHandler:(void (^)(CatroDiagnostics* diagnostics))handler;
- (void)refresh;
- (void)stop;

// Nil until the first publication.
- (nullable NSString*)exportReportWithFormat:(CatroExportFormat)format;

// The system default first, then the active endpoints of the latest snapshot.
- (NSArray<CatroAudioDevice*>*)audioDevicesForInput:(BOOL)input NS_SWIFT_NAME(audioDevices(input:));
@end

NS_ASSUME_NONNULL_END
