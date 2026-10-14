#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "recto_services.h"

namespace winrt::Recto::implementation {

MainWindow::MainWindow()
{
	(void)recto::services::abi_version();
	Title(L"Recto");
}

} // namespace winrt::Recto::implementation
