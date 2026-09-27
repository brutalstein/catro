#pragma once

#include "ShareView.g.h"

namespace winrt::Catro::implementation {

struct ShareView : ShareViewT<ShareView> {
    ShareView() = default;
    void InitializeComponent();
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct ShareView : ShareViewT<ShareView, implementation::ShareView> {};
} // namespace winrt::Catro::factory_implementation
