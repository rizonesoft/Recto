// Scaffold translation unit for recto-pdf (D00 T01 section 3). The lexer,
// cross-reference parser, stream filters and object model land in D01 T01.
#include "recto_pdf.h"

namespace recto::pdf {

int abi_version() noexcept
{
	return 1;
}

} // namespace recto::pdf
