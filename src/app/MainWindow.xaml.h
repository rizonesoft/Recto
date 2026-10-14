#pragma once

#include "MainWindow.g.h"

namespace winrt::Recto::implementation {

struct MainWindow : MainWindowT<MainWindow>
{
	MainWindow();
};

} // namespace winrt::Recto::implementation

namespace winrt::Recto::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
{
};

} // namespace winrt::Recto::factory_implementation
