#include "pch.h"

#include "Home/HomeView.xaml.h"
#if __has_include("HomeView.g.cpp")
#include "HomeView.g.cpp"
#endif

namespace winrt::Catro::implementation {

void HomeView::InitializeComponent() {
    HomeViewT<HomeView>::InitializeComponent();
}

} // namespace winrt::Catro::implementation
