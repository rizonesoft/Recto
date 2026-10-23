#include "pch.h"

#include <recto/ui/ui.h>

namespace recto::ui {

int version() noexcept
{
	return kVersion;
}

bool built_with_address_sanitizer() noexcept
{
#if defined(__SANITIZE_ADDRESS__)
	return true;
#else
	return false;
#endif
}

} // namespace recto::ui
