#import "CatroProductBridge+Testing.h"

#import <QuartzCore/QuartzCore.h>

#include <catro/platform/macos/local_state.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace product = catro::product;
namespace macos = catro::platform::macos;

namespace {

NSString* copy_string(const std::string& text) {
    NSString* value = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return value != nil ? value : @"";
}

std::string utf8(NSString* _Nullable text) {
    const char* bytes = text.UTF8String;
    return bytes == nullptr ? std::string{} : std::string{bytes};
}

CatroConnection connection(catro::app::ConnectionState state) {
    switch (state) {
    case catro::app::ConnectionState::connecting:
        return CatroConnectionConnecting;
    case catro::app::ConnectionState::synchronized:
        return CatroConnectionSynchronized;
    case catro::app::ConnectionState::failed:
        return CatroConnectionFailed;
    case catro::app::ConnectionState::local_only:
        break;
    }
    return CatroConnectionLocalOnly;
}

CatroVoicePhase voice_phase(product::VoicePhase phase) {
    switch (phase) {
    case product::VoicePhase::joining:
        return CatroVoicePhaseJoining;
    case product::VoicePhase::joined:
        return CatroVoicePhaseJoined;
    case product::VoicePhase::failed:
        return CatroVoicePhaseFailed;
    case product::VoicePhase::idle:
        break;
    }
    return CatroVoicePhaseIdle;
}

product::ProductSessionDependencies production_dependencies() {
    product::ProductSessionDependencies deps;
    if (NSURL* executable = NSBundle.mainBundle.executableURL) {
        deps.capability_probe_helper =
            std::filesystem::path(executable.URLByDeletingLastPathComponent.fileSystemRepresentation) /
            "catro-capability-probe";
    }
    auto state = macos::load_or_create_default_local_state();
    if (auto* local = std::get_if<catro::community::LocalState>(&state)) {
        deps.local_state = std::move(*local);
    }
    deps.load_config = [] { return macos::load_directory_service_config(); };
    deps.load_credential = [] { return macos::load_or_create_directory_credential(); };
    deps.save_local_state = [](const catro::community::LocalState& updated) -> std::optional<std::string> {
        const auto path = macos::default_local_state_path();
        if (const auto* error = std::get_if<macos::LocalStateError>(&path)) {
            return error->detail;
        }
        if (const auto error = macos::save_local_state_atomic(std::get<std::filesystem::path>(path), updated)) {
            return error->detail;
        }
        return std::nullopt;
    };
    deps.make_transport = [](const catro::community::DirectoryServiceConfig& service) {
        return macos::make_foundation_directory_http_transport(service);
    };
    return deps;
}

} // namespace

@interface CatroServer ()
- (instancetype)initWithItem:(const product::ServerItem&)item;
@end

@interface CatroMember ()
- (instancetype)initWithItem:(const product::MemberItem&)item;
@end

@interface CatroMessage ()
- (instancetype)initWithItem:(const product::MessageItem&)item;
@end

@interface CatroJoinRequest ()
- (instancetype)initWithItem:(const product::JoinRequestItem&)item;
@end

@interface CatroServerLookup ()
- (instancetype)initWithItem:(const product::ServerLookupItem&)item;
@end

@interface CatroShareQuality ()
- (instancetype)initWithItem:(const product::AdaptiveShareQuality&)item;
@end

@interface CatroShareSource ()
- (instancetype)initWithItem:(const macos::CaptureSource&)item;
- (const macos::CaptureSource&)captureSource;
@end

@interface CatroProductSnapshot ()
- (instancetype)initWithSnapshot:(const product::ProductSnapshot&)snapshot;
@end

namespace {

template <class Object, class Item>
NSArray<Object*>* objects(const std::vector<Item>& items) {
    NSMutableArray<Object*>* result = [NSMutableArray arrayWithCapacity:items.size()];
    for (const auto& item : items) {
        [result addObject:[[Object alloc] initWithItem:item]];
    }
    return result;
}

} // namespace

@implementation CatroServer
- (instancetype)initWithItem:(const product::ServerItem&)item {
    if ((self = [super init])) {
        _identifier = copy_string(item.id);
        _name = copy_string(item.name);
        _publicCode = copy_string(item.public_code);
        _owner = item.owner;
        _hasVoice = !item.voice_channel_id.empty();
        _textChannelID = copy_string(item.text_channel_id);
        _voiceChannelID = copy_string(item.voice_channel_id);
        _memberCount = item.member_count;
    }
    return self;
}
@end

@implementation CatroMember
- (instancetype)initWithItem:(const product::MemberItem&)item {
    if ((self = [super init])) {
        _identifier = copy_string(item.user_id);
        _displayName = copy_string(item.display_name);
        _owner = item.owner;
        _isSelf = item.is_self;
        _voiceChannelID = copy_string(item.voice_channel_id);
    }
    return self;
}
@end

@implementation CatroMessage
- (instancetype)initWithItem:(const product::MessageItem&)item {
    if ((self = [super init])) {
        _sequence = item.sequence;
        _author = copy_string(item.author);
        _content = copy_string(item.content);
        _createdAt = [NSDate dateWithTimeIntervalSince1970:static_cast<double>(item.created_at_ms) / 1000.0];
    }
    return self;
}
@end

@implementation CatroJoinRequest
- (instancetype)initWithItem:(const product::JoinRequestItem&)item {
    if ((self = [super init])) {
        _identifier = copy_string(item.id);
        _requester = copy_string(item.requester);
        _message = copy_string(item.message);
    }
    return self;
}
@end

@implementation CatroServerLookup
- (instancetype)initWithItem:(const product::ServerLookupItem&)item {
    if ((self = [super init])) {
        _publicCode = copy_string(item.public_code);
        _name = copy_string(item.name);
        _memberCount = item.member_count;
        _relationship = copy_string(item.relationship);
    }
    return self;
}
@end

@implementation CatroShareQuality
- (instancetype)initWithItem:(const product::AdaptiveShareQuality&)item {
    if ((self = [super init])) {
        _label = copy_string(item.label);
        _detail = copy_string(item.detail);
        _maxWidth = item.max_width;
        _maxHeight = item.max_height;
        _fps = item.fps;
        _bitrateMbps = static_cast<double>(item.bitrate) / 1'000'000.0;
        _recommended = item.recommended;
    }
    return self;
}
@end

@implementation CatroShareSource {
    macos::CaptureSource _captureSource;
}
- (instancetype)initWithItem:(const macos::CaptureSource&)item {
    if ((self = [super init])) {
        _captureSource = item;
        _nativeID = item.native_id;
        _title = copy_string(item.title);
        _application = copy_string(item.application_name);
        _width = item.width;
        _height = item.height;
        _window = item.kind == macos::CaptureSourceKind::window;
        _camera = item.kind == macos::CaptureSourceKind::camera;
        _game = item.game;
        _primary = item.primary;
    }
    return self;
}
- (const macos::CaptureSource&)captureSource {
    return _captureSource;
}
@end

@implementation CatroProductSnapshot
- (instancetype)initWithSnapshot:(const product::ProductSnapshot&)snapshot {
    if ((self = [super init])) {
        const auto& workspace = snapshot.workspace;
        const auto& media = snapshot.media;
        _revision = snapshot.revision;
        _capabilityGeneration = snapshot.capability_generation;
        _connection = connection(workspace.connection);
        _connectionMessage = copy_string(workspace.connection_message);
        _canJoinServer = workspace.join_server.available();
        _canSendMessage = workspace.send_message.available();
        _canJoinVoice = workspace.join_voice.available();
        _canShareScreen = workspace.share_screen.available();
        _identityName = copy_string(snapshot.identity_name);
        _servers = objects<CatroServer>(snapshot.servers);
        _activeServerID = copy_string(snapshot.active_server_id);
        _members = objects<CatroMember>(snapshot.members);
        _messages = objects<CatroMessage>(snapshot.messages);
        _pendingRequests = objects<CatroJoinRequest>(snapshot.pending_requests);
        _inviteCode = copy_string(snapshot.invite_code);
        _lookup = snapshot.lookup ? [[CatroServerLookup alloc] initWithItem:*snapshot.lookup] : nil;
        _notice = copy_string(snapshot.notice);
        _voicePhase = voice_phase(media.phase);
        _voiceServerID = copy_string(media.voice_server_id);
        _voiceStatus = copy_string(media.status);
        _muted = media.muted;
        _deafened = media.deafened;
        _peerCount = media.peer_count;
        _sharing = media.sharing;
        _shareSourceTitle = copy_string(media.share_source_title);
        _encodedWidth = media.encoded_width;
        _encodedHeight = media.encoded_height;
        _framesSent = media.frames_sent;
        _streamAudioActive = media.stream_audio_active;
        _screenOwner = copy_string(media.screen_owner);
        _remoteAvailable = media.remote_available;
        _watching = media.watching;
        _sources = objects<CatroShareSource>(media.sources);
    }
    return self;
}
@end

@implementation CatroProductBridge {
    std::optional<product::ProductSessionDependencies> _dependencies;
    std::unique_ptr<product::ProductSession> _session;
    // Main thread only; cleared by -stop so already queued deliveries become no-ops.
    void (^_handler)(CatroProductSnapshot*);
}

- (instancetype)init {
    return [self initWithDependencies:production_dependencies()];
}

- (instancetype)initWithDependencies:(product::ProductSessionDependencies)dependencies {
    if ((self = [super init])) {
        _dependencies = std::move(dependencies);
    }
    return self;
}

- (void)startWithHandler:(void (^)(CatroProductSnapshot*))handler {
    if (_session || !_dependencies) {
        return;
    }
    _handler = [handler copy];
    __weak CatroProductBridge* weakSelf = self;
    _session = std::make_unique<product::ProductSession>(
        std::move(*_dependencies), [weakSelf](const product::ProductSnapshot& snapshot) {
            // Built on the session worker; the main thread only receives an immutable object.
            @autoreleasepool {
                CatroProductSnapshot* value = [[CatroProductSnapshot alloc] initWithSnapshot:snapshot];
                dispatch_async(dispatch_get_main_queue(), ^{
                    CatroProductBridge* strongSelf = weakSelf;
                    if (strongSelf != nil && strongSelf->_handler != nil) {
                        strongSelf->_handler(value);
                    }
                });
            }
        });
    _dependencies.reset();
    _session->start();
}

- (void)stop {
    _handler = nil;
    if (_session) {
        _session->stop();
    }
}

- (void)poll {
    if (_session) {
        _session->poll();
    }
}

- (void)selectServer:(NSString*)identifier {
    if (_session) {
        _session->select_server(utf8(identifier));
    }
}

- (void)sendMessage:(NSString*)content {
    if (_session) {
        _session->send_message(utf8(content));
    }
}

- (void)createInvite {
    if (_session) {
        _session->create_invite();
    }
}

- (void)acceptInvite:(NSString*)code {
    if (_session) {
        _session->accept_invite(utf8(code));
    }
}

- (void)lookupServer:(NSString*)code {
    if (_session) {
        _session->lookup_server(utf8(code));
    }
}

- (void)requestJoin:(NSString*)code note:(NSString*)note {
    if (_session) {
        _session->request_join(utf8(code), utf8(note));
    }
}

- (void)decideRequest:(NSString*)identifier approve:(BOOL)approve {
    if (_session) {
        _session->decide_request(utf8(identifier), approve);
    }
}

- (void)renameProfile:(NSString*)name {
    if (_session) {
        _session->rename_profile(utf8(name));
    }
}

- (void)joinVoice {
    if (_session) {
        _session->join_voice();
    }
}

- (void)leaveVoice {
    if (_session) {
        _session->leave_voice();
    }
}

- (void)setMuted:(BOOL)muted {
    if (_session) {
        _session->set_muted(muted);
    }
}

- (void)setDeafened:(BOOL)deafened {
    if (_session) {
        _session->set_deafened(deafened);
    }
}

- (void)loadSources {
    if (_session) {
        _session->load_sources();
    }
}

- (NSArray<CatroShareQuality*>*)shareQualitiesForSource:(CatroShareSource*)source {
    if (!_session) {
        return @[];
    }
    const auto choices = _session->share_quality_choices([source captureSource]);
    NSMutableArray<CatroShareQuality*>* result = [NSMutableArray arrayWithCapacity:choices.size()];
    for (const auto& choice : choices) {
        [result addObject:[[CatroShareQuality alloc] initWithItem:choice]];
    }
    return [result copy];
}

- (void)startShare:(CatroShareSource*)source
          maxWidth:(uint32_t)maxWidth
         maxHeight:(uint32_t)maxHeight
               fps:(uint32_t)fps
       bitrateMbps:(double)bitrateMbps
             audio:(BOOL)audio {
    if (!_session) {
        return;
    }
    product::ShareRequest request;
    request.source = [source captureSource];
    request.max_width = maxWidth;
    request.max_height = maxHeight;
    request.fps = fps;
    // Out-of-range values become 0, which the session rejects as invalid settings.
    request.bitrate = std::isfinite(bitrateMbps) && bitrateMbps > 0 && bitrateMbps <= 50
                          ? static_cast<std::uint32_t>(bitrateMbps * 1'000'000.0 + 0.5)
                          : 0;
    request.share_audio = audio;
    _session->start_share(std::move(request));
}

- (void)stopShare {
    if (_session) {
        _session->stop_share();
    }
}

- (void)setWatching:(BOOL)watching {
    if (_session) {
        _session->set_watching(watching);
    }
}

- (void)setStreamVolume:(float)volume {
    if (_session) {
        _session->set_stream_volume(volume);
    }
}

- (void)setLocalPreviewEnabled:(BOOL)enabled {
    if (_session) {
        _session->set_local_preview_enabled(enabled);
    }
}

- (nullable NSString*)attachPreviewLayer:(nullable CALayer*)layer {
    if (!_session) {
        return @"Session is not running";
    }
    const auto failure = _session->attach_preview_surface((__bridge void*)layer);
    return failure ? copy_string(failure->message) : nil;
}

- (NSArray<NSString*>*)speakingMembers {
    if (!_session) {
        return @[];
    }
    NSMutableArray<NSString*>* result = [NSMutableArray array];
    for (const auto& identifier : _session->speaking_members()) {
        [result addObject:copy_string(identifier)];
    }
    return result;
}

- (void)setVoiceProcessingEcho:(BOOL)echo noise:(BOOL)noise gain:(BOOL)gain {
    if (_session) {
        _session->set_voice_processing(echo, noise, gain);
    }
}

- (void)setInputThreshold:(float)dbfs {
    if (_session) {
        _session->set_input_threshold(dbfs);
    }
}

- (void)setTransmit:(BOOL)transmit {
    if (_session) {
        _session->set_transmit(transmit);
    }
}

- (void)setInputDevice:(nullable NSString*)input outputDevice:(nullable NSString*)output {
    if (_session) {
        _session->set_audio_devices(input ? utf8(input) : std::string{}, output ? utf8(output) : std::string{});
    }
}

- (float)inputLevel {
    return _session ? _session->input_level() : -100.0F;
}

- (void)setVolume:(float)volume forMember:(NSString*)identifier {
    if (_session) {
        _session->set_member_volume(utf8(identifier), volume);
    }
}

- (nullable NSString*)attachRemoteLayer:(nullable CALayer*)layer {
    if (!_session) {
        return @"Session is not running";
    }
    const auto failure = _session->attach_remote_surface((__bridge void*)layer);
    return failure ? copy_string(failure->message) : nil;
}

@end
