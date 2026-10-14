#pragma once

#include "App.xaml.g.h"

namespace winrt::Recto::implementation {

struct App : AppT<App>
{
	App();
	void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

private:
	winrt::Microsoft::UI::Xaml::Window window{ nullptr };
};

} // namespace winrt::Recto::implementation

