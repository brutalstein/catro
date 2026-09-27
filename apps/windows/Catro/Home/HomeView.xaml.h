#pragma once

#include "HomeView.g.h"

namespace winrt::Catro::implementation {

struct HomeView : HomeViewT<HomeView> {
    HomeView() = default;
    void InitializeComponent();
};

} // namespace winrt::Catro::implementation

namespace winrt::Catro::factory_implementation {
struct HomeView : HomeViewT<HomeView, implementation::HomeView> {};
} // namespace winrt::Catro::factory_implementation
