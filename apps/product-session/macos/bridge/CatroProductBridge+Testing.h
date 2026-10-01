#import "CatroProductBridge.h"

#include <catro/macos_product_session.hpp>

// Objective-C++ only: tests inject a fake directory and media room instead of the production
// profile, Keychain credential, and network transport that -init wires up.

NS_ASSUME_NONNULL_BEGIN

// A class extension, so the main @implementation provides the method without category warnings.
@interface CatroProductBridge ()
- (instancetype)initWithDependencies:(catro::product::ProductSessionDependencies)dependencies;
@end

NS_ASSUME_NONNULL_END
