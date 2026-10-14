// Scaffold fuzz entry point (D00 T01 section 3). The real libFuzzer
// targets over recto-pdf's open, stream-filter and render paths land in
// D01 T03 section 2 and replace this file's plain main with the libFuzzer
// harness.
#include "recto_pdf.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
	// Nothing parses input yet; touching recto-pdf proves the link chain
	// fuzz -> pdf.
	(void)data;
	(void)size;
	(void)recto::pdf::abi_version();
	return 0;
}

#ifndef RECTO_FUZZER_MAIN
int main()
{
	const std::uint8_t empty = 0;
	const int result = LLVMFuzzerTestOneInput(&empty, 0);
	std::printf("recto-fuzz scaffold: smoke input result %d\n", result);
	return result;
}
#endif
