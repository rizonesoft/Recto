#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace winrt::Recto::implementation {

MainWindow::MainWindow()
{
	Title(L"Recto");
}

} // namespace winrt::Recto::implementation
