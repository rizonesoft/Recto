#include "recto_core.h"

#include "recto_bridge.h"

namespace recto::core {

int abi_version()
{
	return rc_bridge_abi_version();
}

} // namespace recto::core
