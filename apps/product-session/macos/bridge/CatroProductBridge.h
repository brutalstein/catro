#import <Foundation/Foundation.h>

@class CALayer;

// The only surface Swift sees of the product session: immutable value objects copied from each
// published C++ snapshot, and command methods. No C++ type or native media object crosses here.

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CatroConnection) {
    CatroConnectionLocalOnly,
    CatroConnectionConnecting,
    CatroConnectionSynchronized,
    CatroConnectionFailed,
};

typedef NS_ENUM(NSInteger, CatroVoicePhase) {
    CatroVoicePhaseIdle,
    CatroVoicePhaseJoining,
    CatroVoicePhaseJoined,
    CatroVoicePhaseFailed,
};

__attribute__((objc_subclassing_restricted))
@interface CatroServer : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* name;
// Empty unless this client owns the server.
@property(nonatomic, readonly, copy) NSString* publicCode;
@property(nonatomic, readonly) BOOL owner;
@property(nonatomic, readonly) BOOL hasVoice;
@property(nonatomic, readonly) NSUInteger memberCount;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroMember : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* displayName;
@property(nonatomic, readonly) BOOL owner;
@property(nonatomic, readonly) BOOL isSelf;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroMessage : NSObject
@property(nonatomic, readonly) uint64_t sequence;
@property(nonatomic, readonly, copy) NSString* author;
@property(nonatomic, readonly, copy) NSString* content;
@property(nonatomic, readonly, copy) NSDate* createdAt;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroJoinRequest : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* requester;
@property(nonatomic, readonly, copy) NSString* message;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroServerLookup : NSObject
@property(nonatomic, readonly, copy) NSString* publicCode;
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly) NSUInteger memberCount;
// "none", "member", "pending", ... as reported by the directory.
@property(nonatomic, readonly, copy) NSString* relationship;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroShareSource : NSObject
@property(nonatomic, readonly) uint64_t nativeID;
@property(nonatomic, readonly, copy) NSString* title;
@property(nonatomic, readonly, copy) NSString* application;
@property(nonatomic, readonly) uint32_t width;
@property(nonatomic, readonly) uint32_t height;
@property(nonatomic, readonly) BOOL window;
@property(nonatomic, readonly) BOOL primary;
- (instancetype)init NS_UNAVAILABLE;
@end

__attribute__((objc_subclassing_restricted))
@interface CatroProductSnapshot : NSObject
@property(nonatomic, readonly) uint64_t revision;
@property(nonatomic, readonly) CatroConnection connection;
@property(nonatomic, readonly, copy) NSString* connectionMessage;
@property(nonatomic, readonly) BOOL canJoinServer;
@property(nonatomic, readonly) BOOL canSendMessage;
@property(nonatomic, readonly) BOOL canJoinVoice;
@property(nonatomic, readonly) BOOL canShareScreen;
@property(nonatomic, readonly, copy) NSString* identityName;
@property(nonatomic, readonly, copy) NSArray<CatroServer*>* servers;
@property(nonatomic, readonly, copy) NSString* activeServerID;
@property(nonatomic, readonly, copy) NSArray<CatroMember*>* members;
@property(nonatomic, readonly, copy) NSArray<CatroMessage*>* messages;
@property(nonatomic, readonly, copy) NSArray<CatroJoinRequest*>* pendingRequests;
@property(nonatomic, readonly, copy) NSString* inviteCode;
@property(nonatomic, readonly, nullable) CatroServerLookup* lookup;
// One-shot outcome of the last command; empty when there is nothing new to announce.
@property(nonatomic, readonly, copy) NSString* notice;
@property(nonatomic, readonly) CatroVoicePhase voicePhase;
@property(nonatomic, readonly, copy) NSString* voiceServerID;
@property(nonatomic, readonly, copy) NSString* voiceStatus;
@property(nonatomic, readonly) BOOL muted;
@property(nonatomic, readonly) BOOL deafened;
@property(nonatomic, readonly) NSUInteger peerCount;
@property(nonatomic, readonly) BOOL sharing;
// Empty when nobody shares.
@property(nonatomic, readonly, copy) NSString* screenOwner;
@property(nonatomic, readonly) BOOL remoteAvailable;
@property(nonatomic, readonly) BOOL watching;
@property(nonatomic, readonly, copy) NSArray<CatroShareSource*>* sources;
- (instancetype)init NS_UNAVAILABLE;
@end

// Owns the product session. Snapshots are built off the main thread and delivered on the main
// queue; the handler is never called after -stop returns. Commands never block the caller.
__attribute__((objc_subclassing_restricted))
@interface CatroProductBridge : NSObject
- (void)startWithHandler:(void (^)(CatroProductSnapshot* snapshot))handler;
- (void)stop;

- (void)poll;
- (void)selectServer:(NSString*)identifier;
- (void)sendMessage:(NSString*)content;
- (void)createInvite;
- (void)acceptInvite:(NSString*)code;
- (void)lookupServer:(NSString*)code;
- (void)requestJoin:(NSString*)code note:(NSString*)note NS_SWIFT_NAME(requestJoin(code:note:));
- (void)decideRequest:(NSString*)identifier approve:(BOOL)approve NS_SWIFT_NAME(decideRequest(_:approve:));
- (void)renameProfile:(NSString*)name;

- (void)joinVoice;
- (void)leaveVoice;
- (void)setMuted:(BOOL)muted;
- (void)setDeafened:(BOOL)deafened;
- (void)loadSources;
// Same ceilings as the Windows share dialog; out-of-range settings are rejected with a status.
- (void)startShare:(CatroShareSource*)source
          maxWidth:(uint32_t)maxWidth
         maxHeight:(uint32_t)maxHeight
               fps:(uint32_t)fps
       bitrateMbps:(double)bitrateMbps
             audio:(BOOL)audio NS_SWIFT_NAME(startShare(_:maxWidth:maxHeight:fps:bitrateMbps:audio:));
- (void)stopShare;
- (void)setWatching:(BOOL)watching;
// Main thread. Member identifiers speaking right now; empty outside voice.
- (NSArray<NSString*>*)speakingMembers;
// 0 silences, 1 is unchanged, 2 doubles.
- (void)setVolume:(float)volume forMember:(NSString*)identifier NS_SWIFT_NAME(setVolume(_:forMember:));

// Main thread only. Hosts the local preview / remote stream inside a caller-owned layer; nil
// detaches. Returns nil on success, otherwise the failure.
- (nullable NSString*)attachPreviewLayer:(nullable CALayer*)layer NS_SWIFT_NAME(attachPreview(_:));
- (nullable NSString*)attachRemoteLayer:(nullable CALayer*)layer NS_SWIFT_NAME(attachRemote(_:));
@end

NS_ASSUME_NONNULL_END
