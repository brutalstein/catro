#include "pch.h"

#include "Share/ShareView.xaml.h"
#if __has_include("ShareView.g.cpp")
#include "ShareView.g.cpp"
#endif

namespace winrt::Catro::implementation {

void ShareView::InitializeComponent() {
    ShareViewT<ShareView>::InitializeComponent();
}

} // namespace winrt::Catro::implementation
