// Cooperative cancellation (D01 T02 section 7; foundation plan sections 3.3, 4.5). `recto::core` re-exports these
// (`src/core/cancel_token.h`); nothing here includes upward.
//
// A `CancelToken` is checked where work starts. Below that, the long loops of the filters, the font code and replay poll
// through `detail::poll_cancel`, which reads the token `detail::ScopedCancelScope` set for the calling thread, so one flag
// does not travel through dozens of signatures. With no scope open a poll costs two thread-local reads.
#pragma once

#include "error.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace recto::pdf {

class CancelSource;

// A handle to a cancel flag: copied in O(1), read from any number of threads with no lock. A default token is never
// cancelled and allocates nothing.
class CancelToken {
public:
	CancelToken() noexcept = default;

	[[nodiscard]] bool is_cancelled() const noexcept
	{
		return (flag_ != nullptr && flag_->load(std::memory_order_relaxed)) ||
			(second_ != nullptr && second_->load(std::memory_order_relaxed));
	}

	// Cancelled when either is: a request's own token and its owner's, say. Each must hold one flag at most.
	[[nodiscard]] static CancelToken either(const CancelToken& a, const CancelToken& b) noexcept
	{
		CancelToken out = a;
		out.second_ = b.flag_;
		return out;
	}

private:
	friend class CancelSource;

	explicit CancelToken(std::shared_ptr<const std::atomic<bool>> flag) noexcept
		: flag_(std::move(flag))
	{
	}

	std::shared_ptr<const std::atomic<bool>> flag_;
	std::shared_ptr<const std::atomic<bool>> second_;
};

class CancelSource {
public:
	[[nodiscard]] static PdfResult<CancelSource> create() noexcept
	{
		try {
			return CancelSource(std::make_shared<std::atomic<bool>>(false));
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating a cancel source");
		}
	}

	[[nodiscard]] CancelToken token() const noexcept
	{
		return CancelToken(flag_);
	}

	void cancel() const noexcept
	{
		flag_->store(true, std::memory_order_release);
	}

private:
	explicit CancelSource(std::shared_ptr<std::atomic<bool>> flag) noexcept
		: flag_(std::move(flag))
	{
	}

	std::shared_ptr<std::atomic<bool>> flag_;
};

namespace detail {

// Each place a long loop polls, named so a test can stop work at exactly that place.
enum class CancelSite : uint8_t {
	RasterCommand = 0,
	RasterGlyph,
	RasterFinish,
	ImageRow,
	ImageReduce,
	DashPiece,
	GeometryVerb,
	InterpreterOperator,
	ImageConvert,
	FileRead,
	XrefParse,
	Inflate,
	FilterLoop,
	CMapEntry,
	WidthEntry,
	CffParse,
	Charstring,
	GlyfExpand,
	JpxPacket,
	JpxCodeBlock,
	JpxWavelet,
	DctStrip,
	Jbig2Row,
};

// Called at each poll of the calling thread: a test cancels from inside the loop it means to stop.
using CancelPollHook = void (*)(CancelSite site, void* context) noexcept;

inline thread_local const CancelToken* g_cancel_scope = nullptr;
inline thread_local CancelPollHook g_cancel_hook = nullptr;
inline thread_local void* g_cancel_hook_context = nullptr;

[[nodiscard]] inline bool poll_cancel(CancelSite site) noexcept
{
	if (g_cancel_hook != nullptr) {
		g_cancel_hook(site, g_cancel_hook_context);
	}
	return g_cancel_scope != nullptr && g_cancel_scope->is_cancelled();
}

// The error a cancelled loop returns; never a damaged file, and never retried.
[[nodiscard]] inline PdfError cancelled_error() noexcept
{
	return make_pdf_error(ErrorCode::Cancelled, "The work was cancelled");
}

// Sets the token long loops on this thread poll, restoring the one before it on the way out.
class ScopedCancelScope {
public:
	explicit ScopedCancelScope(const CancelToken& token) noexcept
		: token_(token)
		, previous_(g_cancel_scope)
	{
		g_cancel_scope = &token_;
	}
	~ScopedCancelScope() noexcept
	{
		g_cancel_scope = previous_;
	}
	ScopedCancelScope(const ScopedCancelScope&) = delete;
	ScopedCancelScope& operator=(const ScopedCancelScope&) = delete;

private:
	CancelToken token_;
	const CancelToken* previous_;
};

class ScopedCancelPollHookForTesting {
public:
	ScopedCancelPollHookForTesting(CancelPollHook hook, void* context) noexcept
		: previous_hook_(g_cancel_hook)
		, previous_context_(g_cancel_hook_context)
	{
		g_cancel_hook = hook;
		g_cancel_hook_context = context;
	}
	~ScopedCancelPollHookForTesting() noexcept
	{
		g_cancel_hook = previous_hook_;
		g_cancel_hook_context = previous_context_;
	}
	ScopedCancelPollHookForTesting(const ScopedCancelPollHookForTesting&) = delete;
	ScopedCancelPollHookForTesting& operator=(const ScopedCancelPollHookForTesting&) = delete;

private:
	CancelPollHook previous_hook_;
	void* previous_context_;
};

} // namespace detail

} // namespace recto::pdf
