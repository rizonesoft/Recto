// Worker threads that rasterize tiles (D01 T02 section 9; render_pool.h has the contract).
#include "render_pool.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>

namespace recto::core {

namespace {

using pdf::ErrorCode;
using pdf::make_pdf_error;
using pdf::PdfError;
using pdf::PdfResult;

std::atomic<int64_t> g_live_workers{0};
std::atomic<uint32_t> g_fail_worker_start{0u}; // 0: none; n: the nth start from now
std::atomic<bool> g_fail_worker_rasterizer{false};

// The tile `job` asks for, in a buffer of its own.
[[nodiscard]] PdfResult<std::shared_ptr<const TileImage>> render(pdf::Rasterizer& rasterizer, const TileJob& job) noexcept
{
	try {
		auto image = std::make_shared<TileImage>();
		image->width = job.spec.tile_width_px;
		image->height = job.spec.tile_height_px;
		image->stride = size_t{image->width} * 4u;
		image->pixels.resize(image->stride * image->height);
		RECTO_PDF_RETURN_IF_ERROR(rasterizer.render_tile(job.list, job.spec, image->pixels, image->stride, job.cancel));
		return std::shared_ptr<const TileImage>(std::move(image));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory for a tile's pixels");
	}
}

} // namespace

struct RenderPool::State {
	TileJobSource* source{nullptr};
	pdf::detail::CancelPollHook poll_hook{nullptr};
	void* poll_hook_context{nullptr};
	std::vector<std::thread> threads;

	// Starting: each worker reports whether it can render, then waits to be let go or sent home.
	std::mutex mutex;
	std::condition_variable changed;
	uint32_t reported{0u};
	bool go{false};
	bool abort{false};
	std::optional<PdfError> failure;

	void work() noexcept
	{
		g_live_workers.fetch_add(1, std::memory_order_relaxed);
		{
			const pdf::detail::ScopedCancelPollHookForTesting hook(poll_hook, poll_hook_context);
			// Made here, so it lives and dies on this thread.
			auto rasterizer = g_fail_worker_rasterizer.exchange(false)
				? PdfResult<std::unique_ptr<pdf::Rasterizer>>(make_pdf_error(ErrorCode::InternalError, "A worker's rasterizer could not be made"))
				: pdf::Rasterizer::create();
			bool proceed = false;
			{
				std::unique_lock lock(mutex);
				if (!rasterizer.has_value() && !failure.has_value()) {
					failure = rasterizer.error();
				}
				++reported;
				changed.notify_all();
				changed.wait(lock, [&] { return go || abort; });
				proceed = go && rasterizer.has_value();
			}
			if (proceed) {
				while (std::optional<TileJob> job = source->next_job()) {
					source->finished(*job, render(**rasterizer, *job));
				}
			}
		}
		g_live_workers.fetch_sub(1, std::memory_order_relaxed);
	}

	// Sends the workers already running home and waits for them.
	void stop_starting() noexcept
	{
		{
			const std::lock_guard lock(mutex);
			abort = true;
		}
		changed.notify_all();
		for (std::thread& thread : threads) {
			if (thread.joinable()) {
				thread.join();
			}
		}
		threads.clear();
	}
};

RenderPool::RenderPool(std::unique_ptr<State> state) noexcept
	: state_(std::move(state))
{
}

uint32_t RenderPool::default_worker_count(unsigned cores) noexcept
{
	return std::clamp(std::max(cores, 2u) - 2u, 2u, 8u);
}

uint32_t RenderPool::default_worker_count() noexcept
{
	return default_worker_count(std::thread::hardware_concurrency());
}

PdfResult<std::unique_ptr<RenderPool>> RenderPool::start(
	TileJobSource& source, uint32_t workers, pdf::detail::CancelPollHook poll_hook, void* poll_hook_context) noexcept
{
	if (workers < kMinRenderWorkers || workers > kMaxRenderWorkers) {
		return make_pdf_error(ErrorCode::InvalidArgument, "A render pool has 1 to 16 workers");
	}
	std::unique_ptr<RenderPool> pool;
	try {
		auto state = std::make_unique<State>();
		state->source = &source;
		state->poll_hook = poll_hook;
		state->poll_hook_context = poll_hook_context;
		state->threads.reserve(workers);
		pool.reset(new RenderPool(std::move(state)));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory starting a render pool");
	}
	State& s = *pool->state_;
	for (uint32_t i = 0; i < workers; ++i) {
		try {
			if (const uint32_t nth = g_fail_worker_start.load(); nth != 0u && g_fail_worker_start.fetch_sub(1u) == 1u) {
				throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
			}
			s.threads.emplace_back([&s] { s.work(); }); // reserved, so only the thread itself can fail
		} catch (const std::system_error&) {
			s.stop_starting();
			return make_pdf_error(ErrorCode::InternalError, "A render worker could not start");
		}
	}
	{
		std::unique_lock lock(s.mutex);
		s.changed.wait(lock, [&] { return s.reported == workers; });
		if (s.failure.has_value()) {
			const PdfError failure = *s.failure;
			lock.unlock();
			s.stop_starting();
			return failure;
		}
		s.go = true;
	}
	s.changed.notify_all();
	return pool;
}

RenderPool::~RenderPool() noexcept
{
	for (std::thread& thread : state_->threads) {
		if (thread.joinable()) {
			thread.join();
		}
	}
}

uint32_t RenderPool::size() const noexcept
{
	return static_cast<uint32_t>(state_->threads.size());
}

namespace detail {

void fail_worker_start_for_testing(uint32_t nth) noexcept
{
	g_fail_worker_start.store(nth);
}

void fail_next_worker_rasterizer_for_testing() noexcept
{
	g_fail_worker_rasterizer.store(true);
}

} // namespace detail

int64_t debug_live_workers() noexcept
{
	return g_live_workers.load(std::memory_order_relaxed);
}

} // namespace recto::core
