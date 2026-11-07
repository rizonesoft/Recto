// Cancels from inside the Nth poll at a named place (D01 T02 section 7), so a test knows the loop it means was running,
// and times from `cancel()` to the return, cleanup included. Scheduling is out of the measure: the cancel is on this thread.
#pragma once

#include "../../src/pdf/cancel_token.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

namespace recto::test {

// 10 ms is the promise, held in the build users run. Unwinding what a loop built is thousands of small frees, and Debug
// and AddressSanitizer multiply the cost of each (a CMap cancelled 95% through: 3.6 ms in Release, over 100 under ASan), so
// those builds keep the proof that the poll was reached and the cancel returned, with room for their allocator.
#if defined(_DEBUG) || defined(__SANITIZE_ADDRESS__)
inline constexpr double kCancelLatencyLimitMs = 250.0;
#else
inline constexpr double kCancelLatencyLimitMs = 10.0;
#endif

struct CancelAtPoll {
	recto::pdf::CancelSource source;
	recto::pdf::detail::CancelSite site;
	uint32_t nth;
	uint32_t hits{0u};
	std::chrono::steady_clock::time_point cancelled_at{};

	static void hook(recto::pdf::detail::CancelSite at, void* context) noexcept
	{
		auto& self = *static_cast<CancelAtPoll*>(context);
		if (at == self.site && ++self.hits == self.nth) {
			self.cancelled_at = std::chrono::steady_clock::now();
			self.source.cancel();
		}
	}
};

// Runs `work(token)`, cancelled from inside poll `nth` at `site`, and reads what it must: that poll was reached, the work
// returned `Cancelled`, within the 10 ms the contract gives it. Returns the latency in milliseconds.
template <typename Work>
double expect_cancelled_at(recto::pdf::detail::CancelSite site, uint32_t nth, Work&& work)
{
	auto source = recto::pdf::CancelSource::create();
	EXPECT_TRUE(source.has_value());
	if (!source.has_value()) {
		return 0.0;
	}
	CancelAtPoll at{*source, site, nth};
	const recto::pdf::detail::ScopedCancelPollHookForTesting hook(&CancelAtPoll::hook, &at);
	auto result = work(source->token());
	const auto returned = std::chrono::steady_clock::now();
	EXPECT_GE(at.hits, nth) << "the poll this test targets was never reached";
	EXPECT_FALSE(result.has_value());
	if (!result.has_value()) {
		EXPECT_EQ(result.error().code, recto::pdf::ErrorCode::Cancelled);
	}
	const double ms = std::chrono::duration<double, std::milli>(returned - at.cancelled_at).count();
	if (at.hits >= nth) {
		EXPECT_LT(ms, kCancelLatencyLimitMs) << "a cancel took " << ms << " ms to return";
	}
	return ms;
}

} // namespace recto::test
