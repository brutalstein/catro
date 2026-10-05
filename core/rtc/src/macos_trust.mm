#include "macos_trust.hpp"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>

namespace catro::rtc {
namespace {

std::string utf8(NSString* value) {
    if (value == nil) {
        return {};
    }
    const char* const bytes = value.UTF8String;
    return bytes != nullptr
               ? std::string{bytes}
               : std::string{};
}

MacosTrustBundle failure(std::string message) {
    return MacosTrustBundle{
        {}, std::move(message)};
}

MacosTrustBundle export_system_trust_bundle() {
    @autoreleasepool {
        CFArrayRef copied_anchors = nullptr;
        const auto status =
            SecTrustCopyAnchorCertificates(
                &copied_anchors);
        if (status != errSecSuccess ||
            copied_anchors == nullptr) {
            return failure(
                "macOS system trust anchors unavailable (" +
                std::to_string(status) + ")");
        }

        NSArray* anchors =
            CFBridgingRelease(copied_anchors);
        NSMutableString* pem =
            [NSMutableString string];
        for (id value in anchors) {
            SecCertificateRef certificate =
                (__bridge SecCertificateRef)value;
            CFDataRef copied_data =
                SecCertificateCopyData(certificate);
            if (copied_data == nullptr) {
                continue;
            }
            NSData* data =
                CFBridgingRelease(copied_data);
            NSString* base64 =
                [data base64EncodedStringWithOptions:0];
            if (base64.length == 0) {
                continue;
            }

            [pem appendString:
                @"-----BEGIN CERTIFICATE-----\n"];
            for (NSUInteger offset = 0;
                 offset < base64.length;
                 offset += 64) {
                const auto length =
                    std::min<NSUInteger>(
                        64,
                        base64.length - offset);
                [pem appendString:
                    [base64 substringWithRange:
                        NSMakeRange(offset, length)]];
                [pem appendString:@"\n"];
            }
            [pem appendString:
                @"-----END CERTIFICATE-----\n"];
        }

        if (pem.length == 0) {
            return failure(
                "macOS system trust store contained no exportable anchors");
        }

        NSArray<NSURL*>* cache_urls =
            [[NSFileManager defaultManager]
                URLsForDirectory:NSCachesDirectory
                inDomains:NSUserDomainMask];
        NSURL* cache_root =
            cache_urls.firstObject;
        if (cache_root == nil) {
            return failure(
                "macOS cache directory unavailable");
        }
        NSURL* directory =
            [cache_root
                URLByAppendingPathComponent:
                    @"dev.catro.Catro"
                isDirectory:YES];
        NSError* error = nil;
        if (![[NSFileManager defaultManager]
                createDirectoryAtURL:directory
                withIntermediateDirectories:YES
                attributes:nil
                error:&error]) {
            return failure(
                "could not create RTC trust cache: " +
                utf8(error.localizedDescription));
        }

        NSURL* file =
            [directory
                URLByAppendingPathComponent:
                    @"rtc-ca-bundle.pem"
                isDirectory:NO];
        NSData* bytes =
            [pem dataUsingEncoding:
                NSASCIIStringEncoding];
        error = nil;
        if (bytes == nil ||
            ![bytes writeToURL:file
                options:NSDataWritingAtomic
                error:&error]) {
            return failure(
                "could not write RTC trust bundle: " +
                utf8(error.localizedDescription));
        }

        const auto path = utf8(file.path);
        if (path.empty()) {
            return failure(
                "RTC trust bundle path unavailable");
        }
        return MacosTrustBundle{path, {}};
    }
}

} // namespace

MacosTrustBundle macos_system_trust_bundle() noexcept {
    try {
        static std::once_flag once;
        static MacosTrustBundle cached;
        std::call_once(once, [] {
            cached = export_system_trust_bundle();
        });
        return cached;
    } catch (...) {
        return failure(
            "macOS RTC trust export failed");
    }
}

} // namespace catro::rtc
