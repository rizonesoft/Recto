#include "recto_core.h"

#include "recto_pdf.h"

namespace recto::core {

int abi_version()
{
	return pdf::abi_version();
}

} // namespace recto::core
