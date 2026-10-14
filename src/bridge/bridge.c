/* Scaffold translation unit for recto-bridge (D00 T01 section 3). The locked
 * MuPDF runtime lands in D01 T01 section 1 (runtime.c). */
#include "recto_bridge.h"

int rc_bridge_abi_version(void)
{
	return 1;
}
