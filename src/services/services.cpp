#include "recto_services.h"

#include "recto_core.h"

namespace recto::services {

int abi_version()
{
	return core::abi_version();
}

} // namespace recto::services
