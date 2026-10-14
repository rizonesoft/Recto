// recto-cli: the engine's command-line face for testing, profiling and
// fuzz reproduction (foundation plan 3.1). Verbs land in D01 T03 section 1;
// this scaffold (D00 T01 section 3) reports the engine ABI it links.
#include "recto_core.h"

#include <cstdio>

int main()
{
	std::printf("recto-cli scaffold: core abi %d (verbs land in D01 T03)\n", recto::core::abi_version());
	return 0;
}
