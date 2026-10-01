#import "CatroProductBridge+Testing.h"

#import <QuartzCore/QuartzCore.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <utility>

using namespace catro;

namespace {

product::ProductSessionDependencies local_only() {
    product::ProductSessionDependencies deps;
    community::LocalState state;
    state.identity.id.bytes[0] = std::byte{1};
    state.identity.display_name = "Owner";
    state.personal_server.id.bytes[0] = std::byte{2};
    state.personal_server.owner_id = state.identity.id;
    state.personal_server.name = "Catro";
    state.personal_server.members = {{state.identity.id, community::ServerRole::owner}};
    deps.local_state = std::move(state);
    deps.load_config = [] {
        return community::DirectoryConfigResult{
            community::DirectoryError{community::DirectoryErrorCode::not_configured, "missing"}};
    };
    return deps;
}

// Snapshots arrive through the main queue, so the test spins the main run loop while it waits.
bool spin_until(const std::function<bool()>& condition) {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (!condition() && [deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    return condition();
}

} // namespace

// Handlers record into main-thread arrays: C++ lambdas cannot capture __block variables.
TEST_CASE("macOS product bridge delivers immutable snapshots on the main thread", "[macos][bridge]") {
    CatroProductBridge* bridge = [[CatroProductBridge alloc] initWithDependencies:local_only()];
    NSMutableArray<CatroProductSnapshot*>* received = [NSMutableArray array];
    NSMutableArray<NSNumber*>* on_main = [NSMutableArray array];
    [bridge startWithHandler:^(CatroProductSnapshot* snapshot) {
        [on_main addObject:@([NSThread isMainThread])];
        [received addObject:snapshot];
    }];

    REQUIRE(spin_until([&] {
        return received.lastObject != nil && received.lastObject.connection == CatroConnectionFailed;
    }));
    CHECK_FALSE([on_main containsObject:@NO]);
    CatroProductSnapshot* latest = received.lastObject;
    CHECK([latest.connectionMessage
        isEqualToString:@"Online services are not configured. Local mode remains available."]);
    CHECK_FALSE(latest.canSendMessage);
    CHECK([latest.identityName isEqualToString:@"Owner"]);
    REQUIRE(latest.servers.count == 1);
    CHECK([latest.servers.firstObject.name isEqualToString:@"Catro"]);
    CHECK(latest.servers.firstObject.owner);
    CHECK([latest.activeServerID isEqualToString:latest.servers.firstObject.identifier]);
    CHECK(latest.voicePhase == CatroVoicePhaseIdle);
    CHECK(latest.lookup == nil);
    [bridge stop];
}

TEST_CASE("macOS product bridge never calls the handler after stop", "[macos][bridge]") {
    CatroProductBridge* bridge = [[CatroProductBridge alloc] initWithDependencies:local_only()];
    NSMutableArray<CatroProductSnapshot*>* received = [NSMutableArray array];
    [bridge startWithHandler:^(CatroProductSnapshot* snapshot) {
        [received addObject:snapshot];
    }];
    REQUIRE(spin_until([&] { return received.count > 0; }));

    // Commands queued right before stop may already have published; their deliveries are dropped.
    [bridge sendMessage:@"hello"];
    [bridge joinVoice];
    [bridge stop];
    const NSUInteger stopped = received.count;
    [bridge poll];
    [bridge selectServer:@"anything"];
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
    CHECK(received.count == stopped);
}

TEST_CASE("macOS product bridge commands are safe before start and surfaces detach", "[macos][bridge]") {
    CatroProductBridge* bridge = [[CatroProductBridge alloc] initWithDependencies:local_only()];
    [bridge poll];
    [bridge joinVoice];
    CHECK([bridge attachRemoteLayer:nil] != nil);

    NSMutableArray<CatroProductSnapshot*>* received = [NSMutableArray array];
    [bridge startWithHandler:^(CatroProductSnapshot* snapshot) {
        [received addObject:snapshot];
    }];
    REQUIRE(spin_until([&] { return received.count > 0; }));
    CALayer* host = [CALayer layer];
    CHECK([bridge attachRemoteLayer:host] == nil);
    CHECK([bridge attachRemoteLayer:nil] == nil);
    CHECK([bridge attachPreviewLayer:host] == nil);
    CHECK([bridge attachPreviewLayer:nil] == nil);
    [bridge stop];
}
