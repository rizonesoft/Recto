// recto::core::TileScheduler and RenderPool (D01 T02 section 9).
#include "../../src/core/document_actor.h"
#include "../../src/core/render_pool.h"
#include "../../src/core/tile_scheduler.h"

#include "../../src/pdf/document.h"
#include "../../src/pdf/interpreter.h"
#include "../../src/pdf/rasterizer.h"

#include "cancel_test_support.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace recto::core {
namespace {

using pdf::detail::CancelSite;
using pdf::ErrorCode;
using Delivered = pdf::PdfResult<std::shared_ptr<const TileImage>>;

// A PDF of `pages` pages, each drawing `content`.
std::vector<uint8_t> pdf_of(size_t pages, std::string_view content)
{
	std::vector<std::string> objects;
	objects.emplace_back("<< /Type /Catalog /Pages 2 0 R >>");
	std::string kids;
	for (size_t i = 0; i < pages; ++i) {
		kids += std::to_string(4u + i) + " 0 R ";
	}
	objects.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>");
	objects.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + std::string(content) + "\nendstream");
	for (size_t i = 0; i < pages; ++i) {
		objects.emplace_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 3 0 R >>");
	}
	std::string out = "%PDF-1.7\n";
	std::vector<size_t> offsets;
	for (size_t i = 0; i < objects.size(); ++i) {
		offsets.push_back(out.size());
		out += std::to_string(i + 1u) + " 0 obj\n" + objects[i] + "\nendobj\n";
	}
	const size_t xref = out.size();
	out += "xref\n0 " + std::to_string(objects.size() + 1u) + "\n0000000000 65535 f \n";
	for (const size_t offset : offsets) {
		char row[32];
		std::snprintf(row, sizeof(row), "%010zu 00000 n \n", offset);
		out += row;
	}
	out += "trailer\n<< /Size " + std::to_string(objects.size() + 1u) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return std::vector<uint8_t>(out.begin(), out.end());
}

// Many small fills, so a tile polls (and can be held) while it renders.
std::string busy_content()
{
	std::string content;
	for (int i = 0; i < 400; ++i) {
		content += "0." + std::to_string(i % 10) + " 0 0 rg " + std::to_string(i % 180) + " 10 8 8 re f ";
	}
	return content;
}

std::shared_ptr<DocumentHandle> opened(const std::filesystem::path& path, const ActorOptions& options = {})
{
	auto handle = DocumentHandle::start(options);
	EXPECT_TRUE(handle.has_value());
	if (!handle.has_value()) {
		return nullptr;
	}
	auto open = (*handle)->open(path);
	EXPECT_EQ(open.wait_for(std::chrono::seconds(60)), std::future_status::ready);
	EXPECT_TRUE(open.get().has_value());
	return *handle;
}

// Every delivery, in order, safe from any thread.
struct Deliveries {
	std::mutex mutex;
	std::condition_variable arrived;
	std::vector<std::pair<TileKey, Delivered>> all;

	TileDelivery sink()
	{
		return [this](const TileKey& key, Delivered result) noexcept {
			{
				const std::lock_guard lock(mutex);
				all.emplace_back(key, std::move(result));
			}
			arrived.notify_all();
		};
	}

	bool wait_for(size_t count, std::chrono::seconds limit = std::chrono::seconds(60))
	{
		std::unique_lock lock(mutex);
		return arrived.wait_for(lock, limit, [&] { return all.size() >= count; });
	}

	size_t count()
	{
		const std::lock_guard lock(mutex);
		return all.size();
	}

	size_t of(const TileKey& key)
	{
		const std::lock_guard lock(mutex);
		return static_cast<size_t>(std::count_if(all.begin(), all.end(), [&](const auto& d) { return d.first == key; }));
	}
};

TileRequest tile(uint64_t doc, uint32_t page, uint32_t x, TilePriority priority = TilePriority::VisibleCurrentPage, uint32_t edge = 64u)
{
	TileRequest request;
	request.key = TileKey{doc, page, 1'000u, 0, x, 0u};
	request.priority = priority;
	request.spec.dpi_scale = 1.0;
	request.spec.tile_x_px = x * edge;
	request.spec.tile_width_px = edge;
	request.spec.tile_height_px = edge;
	return request;
}

std::unique_ptr<TileScheduler> scheduler(Deliveries& deliveries, uint32_t workers, pdf::detail::CancelPollHook hook = nullptr, void* context = nullptr)
{
	SchedulerOptions options;
	options.worker_count = workers;
	options.deliver = deliveries.sink();
	options.poll_hook = hook;
	options.poll_hook_context = context;
	auto made = TileScheduler::create(std::move(options));
	EXPECT_TRUE(made.has_value());
	return made.has_value() ? std::move(*made) : nullptr;
}

// Holds a worker inside its tile at its first `RasterCommand` poll, until released or the tile is cancelled.
struct Hold {
	std::atomic<bool> released{false};
	std::atomic<int> held{0};

	static void hook(CancelSite site, void* context) noexcept
	{
		auto& self = *static_cast<Hold*>(context);
		if (site != CancelSite::RasterCommand || self.released.load()) {
			return;
		}
		self.held.fetch_add(1);
		const pdf::CancelToken* const token = pdf::detail::g_cancel_scope;
		while (!self.released.load() && (token == nullptr || !token->is_cancelled())) {
			std::this_thread::yield();
		}
		self.held.fetch_sub(1);
	}

	void wait_held(int count) const
	{
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (held.load() < count && std::chrono::steady_clock::now() < until) {
			std::this_thread::yield();
		}
		EXPECT_GE(held.load(), count);
	}
};

TEST(Scheduler, WorkerCount)
{
	for (const unsigned cores : {0u, 1u, 2u, 3u, 4u}) {
		EXPECT_EQ(RenderPool::default_worker_count(cores), 2u) << cores;
	}
	EXPECT_EQ(RenderPool::default_worker_count(8u), 6u);
	EXPECT_EQ(RenderPool::default_worker_count(10u), 8u);
	EXPECT_EQ(RenderPool::default_worker_count(64u), 8u);
	Deliveries deliveries;
	for (const uint32_t workers : {0u, 1u, 16u}) {
		const auto made = scheduler(deliveries, workers);
		ASSERT_NE(made, nullptr);
		EXPECT_EQ(made->worker_count(), workers == 0u ? RenderPool::default_worker_count() : workers);
	}
	SchedulerOptions past;
	past.worker_count = 17u;
	past.deliver = deliveries.sink();
	const auto refused = TileScheduler::create(std::move(past));
	ASSERT_FALSE(refused.has_value());
	EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
	const auto silent = TileScheduler::create(SchedulerOptions{});
	ASSERT_FALSE(silent.has_value());
	EXPECT_EQ(silent.error().code, ErrorCode::InvalidArgument);
}

TEST(Scheduler, TileKeyIdentity)
{
	// Every field tells two tiles apart, and a set holds every tile of two documents, pages, zooms and rotations.
	const TileKey base{7u, 3u, 1'500u, 90, 4u, 5u};
	const std::vector<TileKey> neighbours{
		{8u, 3u, 1'500u, 90, 4u, 5u}, {7u, 4u, 1'500u, 90, 4u, 5u}, {7u, 3u, 1'501u, 90, 4u, 5u},
		{7u, 3u, 1'500u, 180, 4u, 5u}, {7u, 3u, 1'500u, 90, 5u, 5u}, {7u, 3u, 1'500u, 90, 4u, 6u},
	};
	for (const TileKey& other : neighbours) {
		EXPECT_NE(base, other);
	}
	EXPECT_EQ(base, (TileKey{7u, 3u, 1'500u, 90, 4u, 5u}));
	std::unordered_set<TileKey> all;
	size_t made = 0u;
	for (uint64_t doc = 1; doc <= 2u; ++doc) {
		for (uint32_t page = 0; page < 3u; ++page) {
			for (const uint32_t zoom : {500u, 1'000u, 2'000u}) {
				for (const int32_t rotation : {0, 90, 180, 270}) {
					for (uint32_t x = 0; x < 4u; ++x) {
						for (uint32_t y = 0; y < 4u; ++y) {
							all.insert(TileKey{doc, page, zoom, rotation, x, y});
							++made;
						}
					}
				}
			}
		}
	}
	EXPECT_EQ(all.size(), made);
}

TEST(Scheduler, WorkerLifecycle)
{
	const int64_t before = debug_live_workers();
	{
		Deliveries deliveries;
		const auto made = scheduler(deliveries, 4u);
		ASSERT_NE(made, nullptr);
		EXPECT_EQ(debug_live_workers(), before + 4);
	}
	EXPECT_EQ(debug_live_workers(), before);
}

TEST(Scheduler, WorkerStartFailure)
{
	const int64_t before = debug_live_workers();
	Deliveries deliveries;
	{
		detail::fail_worker_start_for_testing(3u);
		SchedulerOptions options;
		options.worker_count = 4u;
		options.deliver = deliveries.sink();
		const auto made = TileScheduler::create(std::move(options));
		ASSERT_FALSE(made.has_value());
		EXPECT_EQ(made.error().code, ErrorCode::InternalError);
	}
	EXPECT_EQ(debug_live_workers(), before);
	{
		detail::fail_next_worker_rasterizer_for_testing();
		SchedulerOptions options;
		options.worker_count = 4u;
		options.deliver = deliveries.sink();
		const auto made = TileScheduler::create(std::move(options));
		ASSERT_FALSE(made.has_value());
		EXPECT_EQ(made.error().code, ErrorCode::InternalError);
	}
	EXPECT_EQ(debug_live_workers(), before);
	EXPECT_EQ(deliveries.count(), 0u);
}

TEST(Scheduler, PriorityOrdering)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("one.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, "0 0 10 10 re f"));
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	auto made = scheduler(deliveries, 1u); // one worker: delivery order is start order
	ASSERT_NE(made, nullptr);
	ASSERT_TRUE(made->attach(1u, document).has_value());
	made->pause();
	// Two of each tier, the lowest first, so the order out can only come from the tiers.
	const std::vector<TileRequest> requests{
		tile(1u, 0u, 0u, TilePriority::Thumbnail), tile(1u, 0u, 1u, TilePriority::Thumbnail),
		tile(1u, 0u, 2u, TilePriority::PrefetchRing), tile(1u, 0u, 3u, TilePriority::PrefetchRing),
		tile(1u, 0u, 4u, TilePriority::VisibleAdjacentPage), tile(1u, 0u, 5u, TilePriority::VisibleAdjacentPage),
		tile(1u, 0u, 6u, TilePriority::VisibleCurrentPage), tile(1u, 0u, 7u, TilePriority::VisibleCurrentPage),
	};
	made->update_viewport(1u, requests);
	// The same set, with the first thumbnail raised to the current page.
	std::vector<TileRequest> raised = requests;
	raised[0].priority = TilePriority::VisibleCurrentPage;
	made->update_viewport(1u, raised);
	made->resume();
	ASSERT_TRUE(deliveries.wait_for(8u));
	std::vector<uint32_t> order;
	for (const auto& [key, result] : deliveries.all) {
		EXPECT_TRUE(result.has_value());
		order.push_back(key.tile_x);
	}
	EXPECT_EQ(order, (std::vector<uint32_t>{0u, 6u, 7u, 4u, 5u, 2u, 3u, 1u}));
}

TEST(Scheduler, DeduplicatesInFlightTileKeys)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("busy.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, busy_content()));
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	Hold hold;
	auto made = scheduler(deliveries, 1u, &Hold::hook, &hold);
	ASSERT_NE(made, nullptr);
	ASSERT_TRUE(made->attach(1u, document).has_value());
	made->update_viewport(1u, {tile(1u, 0u, 0u), tile(1u, 0u, 1u)});
	hold.wait_held(1);
	// Overlapping sets while the first tile renders: nothing is rendered twice and nothing is cancelled.
	made->update_viewport(1u, {tile(1u, 0u, 0u), tile(1u, 0u, 1u), tile(1u, 0u, 2u)});
	made->update_viewport(1u, {tile(1u, 0u, 1u), tile(1u, 0u, 0u), tile(1u, 0u, 2u), tile(1u, 0u, 2u)});
	hold.released.store(true);
	ASSERT_TRUE(deliveries.wait_for(3u));
	made.reset();
	EXPECT_EQ(deliveries.count(), 3u);
	for (const auto& [key, result] : deliveries.all) {
		EXPECT_TRUE(result.has_value()) << key.tile_x;
		EXPECT_EQ(deliveries.of(key), 1u) << key.tile_x;
	}
}

TEST(Scheduler, ViewportJumpCancelsStaleTiles)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("sixty.pdf");
	recto::test::write_file_bytes(path, pdf_of(60u, busy_content()));
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	Hold hold;
	auto made = scheduler(deliveries, 2u, &Hold::hook, &hold);
	ASSERT_NE(made, nullptr);
	ASSERT_TRUE(made->attach(1u, document).has_value());
	std::vector<TileRequest> first;
	for (uint32_t x = 0; x < 8u; ++x) {
		first.push_back(tile(1u, 0u, x));
	}
	made->update_viewport(1u, first);
	hold.wait_held(2);
	std::vector<TileRequest> jump;
	for (uint32_t x = 0; x < 4u; ++x) {
		jump.push_back(tile(1u, 50u, x));
	}
	made->update_viewport(1u, jump);
	hold.released.store(true);
	ASSERT_TRUE(deliveries.wait_for(12u));
	for (const auto& [key, result] : deliveries.all) {
		if (key.page_index == 0u) {
			ASSERT_FALSE(result.has_value()) << "a stale tile was delivered";
			EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
		} else {
			EXPECT_TRUE(result.has_value()) << "a page 50 tile failed";
		}
	}
}

TEST(Scheduler, JumpLatency)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("jump.pdf");
	recto::test::write_file_bytes(path, pdf_of(2u, busy_content()));
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	Hold hold;
	auto made = scheduler(deliveries, 2u, &Hold::hook, &hold);
	ASSERT_NE(made, nullptr);
	ASSERT_TRUE(made->attach(1u, document).has_value());
	// Page 1's list is compiled first, so the jump measures the scheduler and not the interpreter.
	auto warm = document->get_display_list(1u);
	ASSERT_TRUE(warm.get().has_value());
	made->update_viewport(1u, {tile(1u, 0u, 0u), tile(1u, 0u, 1u), tile(1u, 0u, 2u), tile(1u, 0u, 3u)});
	hold.wait_held(2);
	hold.released.store(true); // from here a held worker returns as soon as its tile is cancelled
	const auto jumped = std::chrono::steady_clock::now();
	made->update_viewport(1u, {TileRequest{TileKey{1u, 1u, 1'000u, 0, 0u, 0u}, TilePriority::VisibleCurrentPage, tile(1u, 1u, 0u, TilePriority::VisibleCurrentPage, 8u).spec}});
	std::chrono::steady_clock::time_point first{};
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (first == std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() < until) {
		{
			const std::lock_guard lock(deliveries.mutex);
			for (const auto& [key, result] : deliveries.all) {
				if (key.page_index == 1u) {
					first = std::chrono::steady_clock::now();
				}
			}
		}
		std::this_thread::yield();
	}
	ASSERT_NE(first, std::chrono::steady_clock::time_point{}) << "the new page's tile never came";
	recto::test::expect_cancel_latency(std::chrono::duration<double, std::milli>(first - jumped).count(), "the first tile after a jump");
}

TEST(Scheduler, DeliveredPixels)
{
	const auto path = recto::test::corpus_path("render/images-and-vectors.pdf");
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	auto list_future = document->get_display_list(0u);
	const auto list = list_future.get();
	ASSERT_TRUE(list.has_value());
	auto rasterizer = pdf::Rasterizer::create();
	ASSERT_TRUE(rasterizer.has_value());
	Deliveries deliveries;
	auto made = scheduler(deliveries, 3u);
	ASSERT_NE(made, nullptr);
	ASSERT_TRUE(made->attach(1u, document).has_value());
	std::vector<TileRequest> requests;
	uint32_t index = 0u;
	for (const uint32_t zoom : {1'000u, 2'000u}) {
		for (const int32_t rotation : {0, 90, 180, 270}) {
			for (const auto& [x, y] : {std::pair{0u, 0u}, std::pair{96u, 160u}}) {
				TileRequest request;
				request.key = TileKey{1u, 0u, zoom, rotation, index++, 0u};
				request.spec.dpi_scale = zoom / 1'000.0;
				request.spec.rotation_deg = rotation;
				request.spec.tile_x_px = x;
				request.spec.tile_y_px = y;
				request.spec.tile_width_px = 96u;
				request.spec.tile_height_px = 80u;
				requests.push_back(request);
			}
		}
	}
	made->update_viewport(1u, requests);
	ASSERT_TRUE(deliveries.wait_for(requests.size()));
	for (const auto& [key, result] : deliveries.all) {
		SCOPED_TRACE(key.tile_x);
		ASSERT_TRUE(result.has_value());
		const TileRequest& request = requests[key.tile_x];
		const TileImage& image = **result;
		EXPECT_EQ(image.width, 96u);
		EXPECT_EQ(image.height, 80u);
		EXPECT_EQ(image.stride, 96u * 4u);
		std::vector<uint8_t> direct(image.stride * image.height);
		ASSERT_TRUE((*rasterizer)->render_tile(*list, request.spec, direct, image.stride).has_value());
		EXPECT_EQ(image.pixels, direct) << "zoom " << request.key.zoom_bucket_milli << ", rotation " << request.key.rotation_deg;
	}
}

TEST(Scheduler, ListsComeFromTheActor)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("eight.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, busy_content()));
	struct Threads {
		std::mutex mutex;
		std::set<std::thread::id> compiling;
		std::set<std::thread::id> rendering;
		static void actor(CancelSite site, void* context) noexcept
		{
			if (site == CancelSite::InterpreterOperator) {
				auto& self = *static_cast<Threads*>(context);
				const std::lock_guard lock(self.mutex);
				self.compiling.insert(std::this_thread::get_id());
			}
		}
		static void worker(CancelSite site, void* context) noexcept
		{
			auto& self = *static_cast<Threads*>(context);
			const std::lock_guard lock(self.mutex);
			if (site == CancelSite::InterpreterOperator) {
				self.compiling.insert(std::this_thread::get_id());
			} else {
				self.rendering.insert(std::this_thread::get_id());
			}
		}
	} threads;
	ActorOptions actor_options;
	actor_options.poll_hook = &Threads::actor;
	actor_options.poll_hook_context = &threads;
	const auto document = opened(path, actor_options);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	auto made = scheduler(deliveries, 4u, &Threads::worker, &threads);
	ASSERT_NE(made, nullptr);
	ASSERT_TRUE(made->attach(1u, document).has_value());
	std::vector<TileRequest> eight;
	for (uint32_t x = 0; x < 8u; ++x) {
		eight.push_back(tile(1u, 0u, x));
	}
	made->update_viewport(1u, eight);
	ASSERT_TRUE(deliveries.wait_for(8u));
	EXPECT_EQ(document->stats().cache_misses, 1u) << "one fetch for the page's eight tiles";
	EXPECT_EQ(document->stats().cache_hits, 0u);
	const std::lock_guard lock(threads.mutex);
	ASSERT_EQ(threads.compiling.size(), 1u) << "a page compiled on more than one thread";
	EXPECT_FALSE(threads.rendering.empty());
	EXPECT_FALSE(threads.rendering.contains(*threads.compiling.begin())) << "a worker compiled a page";
}

TEST(Scheduler, DeliversOnce)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("two.pdf");
	recto::test::write_file_bytes(path, pdf_of(2u, "0 0 10 10 re f"));
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	{
		auto made = scheduler(deliveries, 2u);
		ASSERT_NE(made, nullptr);
		ASSERT_TRUE(made->attach(1u, document).has_value());
		EXPECT_EQ(made->attach(1u, document).error().code, ErrorCode::InvalidArgument) << "attached twice";
		// Rendered.
		made->update_viewport(1u, {tile(1u, 0u, 0u), tile(1u, 1u, 0u)});
		ASSERT_TRUE(deliveries.wait_for(2u));
		// A document never attached, and a page that does not compile: each tile its typed error.
		made->update_viewport(9u, {tile(9u, 0u, 0u)});
		made->update_viewport(1u, {tile(1u, 7u, 0u), tile(1u, 7u, 1u)});
		ASSERT_TRUE(deliveries.wait_for(5u));
		// Detached with tiles waiting, then the scheduler destroyed with more.
		made->pause();
		made->update_viewport(1u, {tile(1u, 0u, 4u), tile(1u, 0u, 5u)});
		made->detach(1u);
		ASSERT_TRUE(made->attach(2u, document).has_value());
		made->update_viewport(2u, {tile(2u, 0u, 0u), tile(2u, 1u, 0u)});
	}
	ASSERT_TRUE(deliveries.wait_for(9u));
	EXPECT_EQ(deliveries.count(), 9u);
	std::map<std::tuple<uint64_t, uint32_t, uint32_t>, int> seen;
	for (const auto& [key, result] : deliveries.all) {
		++seen[{key.doc_id, key.page_index, key.tile_x}];
		if (key.doc_id == 9u) {
			EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
		} else if (key.page_index == 7u) {
			EXPECT_FALSE(result.has_value()) << "a page that is not there rendered";
		} else if (key.tile_x >= 4u || key.doc_id == 2u) {
			ASSERT_FALSE(result.has_value());
			EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
		} else {
			EXPECT_TRUE(result.has_value());
		}
	}
	for (const auto& [key, count] : seen) {
		EXPECT_EQ(count, 1);
	}
}

TEST(Scheduler, Bounded)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("one.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, "0 0 10 10 re f"));
	const auto document = opened(path);
	ASSERT_NE(document, nullptr);
	Deliveries deliveries;
	{
		auto made = scheduler(deliveries, 1u);
		ASSERT_NE(made, nullptr);
		ASSERT_TRUE(made->attach(1u, document).has_value());
		made->pause();
		std::vector<TileRequest> many;
		for (uint32_t x = 0; x <= kMaxScheduledTiles; ++x) {
			many.push_back(tile(1u, 0u, x, TilePriority::VisibleCurrentPage, 8u));
		}
		made->update_viewport(1u, many);
		ASSERT_TRUE(deliveries.wait_for(1u));
		ASSERT_EQ(deliveries.count(), 1u) << "only the one past the limit is answered at once";
		const auto& [key, result] = deliveries.all.front();
		EXPECT_EQ(key.tile_x, kMaxScheduledTiles);
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(result.error().refusal_reason, pdf::RefusalReason::SchedulerFull);
	}
	EXPECT_EQ(deliveries.count(), kMaxScheduledTiles + 1u) << "everything taken was answered";
}

TEST(Scheduler, ShutdownDuringFetch)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("held.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, busy_content()));
	struct Compile {
		std::atomic<bool> armed{false};
		std::atomic<bool> reached{false};
		std::atomic<bool> released{false};
		static void hook(CancelSite site, void* context) noexcept
		{
			auto& self = *static_cast<Compile*>(context);
			if (site == CancelSite::InterpreterOperator && self.armed.load() && !self.released.load()) {
				self.reached.store(true);
				while (!self.released.load()) {
					std::this_thread::yield();
				}
			}
		}
	} compile;
	ActorOptions options;
	options.poll_hook = &Compile::hook;
	options.poll_hook_context = &compile;
	const auto document = opened(path, options);
	ASSERT_NE(document, nullptr);
	compile.armed.store(true);
	Deliveries deliveries;
	{
		auto made = scheduler(deliveries, 2u);
		ASSERT_NE(made, nullptr);
		ASSERT_TRUE(made->attach(1u, document).has_value());
		made->update_viewport(1u, {tile(1u, 0u, 0u), tile(1u, 0u, 1u), tile(1u, 0u, 2u)});
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (!compile.reached.load() && std::chrono::steady_clock::now() < until) {
			std::this_thread::yield();
		}
		ASSERT_TRUE(compile.reached.load());
	}
	EXPECT_EQ(deliveries.count(), 3u);
	for (const auto& [key, result] : deliveries.all) {
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
	}
	compile.released.store(true);
	auto after = document->get_description(); // the actor has answered the fetch by the time it answers this
	EXPECT_TRUE(after.get().has_value());
	EXPECT_EQ(deliveries.count(), 3u) << "a tile was delivered after the scheduler was gone";
}

TEST(Scheduler, CallbackAdmission)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("pages.pdf");
	recto::test::write_file_bytes(path, pdf_of(6u, "0 0 10 10 re f"));
	Deliveries deliveries;
	// A full actor queue: pages past it are refused at once, on the asking thread, and every tile is answered once.
	{
		struct Compile {
			std::atomic<bool> released{false};
			std::atomic<bool> reached{false};
			static void hook(CancelSite site, void* context) noexcept
			{
				auto& self = *static_cast<Compile*>(context);
				if (site == CancelSite::InterpreterOperator && !self.released.load()) {
					self.reached.store(true);
					while (!self.released.load()) {
						std::this_thread::yield();
					}
				}
			}
		} compile;
		ActorOptions options;
		options.max_queue = 2u;
		options.poll_hook = &Compile::hook;
		options.poll_hook_context = &compile;
		compile.released.store(true); // the open compiles nothing, but let it through
		const auto document = opened(path, options);
		ASSERT_NE(document, nullptr);
		compile.released.store(false);
		auto made = scheduler(deliveries, 1u);
		ASSERT_NE(made, nullptr);
		ASSERT_TRUE(made->attach(1u, document).has_value());
		std::vector<TileRequest> pages;
		for (uint32_t page = 0; page < 6u; ++page) {
			pages.push_back(tile(1u, page, 0u));
		}
		made->update_viewport(1u, pages);
		// Page 0 compiles and is held there; at most two more wait in the queue, so three or more are refused at once.
		ASSERT_TRUE(deliveries.wait_for(3u));
		size_t refused = 0u;
		{
			const std::lock_guard lock(deliveries.mutex);
			for (const auto& [key, result] : deliveries.all) {
				ASSERT_FALSE(result.has_value());
				EXPECT_EQ(result.error().refusal_reason, pdf::RefusalReason::ActorQueueFull);
				++refused;
			}
		}
		EXPECT_GE(refused, 3u);
		compile.released.store(true);
		ASSERT_TRUE(deliveries.wait_for(6u));
		made.reset();
		EXPECT_EQ(deliveries.count(), 6u) << "a tile was answered twice";
		std::set<uint32_t> pages_seen;
		for (const auto& [key, result] : deliveries.all) {
			EXPECT_TRUE(pages_seen.insert(key.page_index).second);
		}
	}	// A closed actor.
	{
		const auto document = opened(path);
		ASSERT_NE(document, nullptr);
		document->close().wait();
		auto made = scheduler(deliveries, 1u);
		ASSERT_NE(made, nullptr);
		ASSERT_TRUE(made->attach(1u, document).has_value());
		const size_t before = deliveries.count();
		made->update_viewport(1u, {tile(1u, 0u, 0u)});
		ASSERT_TRUE(deliveries.wait_for(before + 1u));
		EXPECT_EQ(deliveries.all.back().second.error().code, ErrorCode::Cancelled);
	}
	// No memory to take the fetch.
	{
		const auto document = opened(path);
		ASSERT_NE(document, nullptr);
		auto made = scheduler(deliveries, 1u);
		ASSERT_NE(made, nullptr);
		ASSERT_TRUE(made->attach(1u, document).has_value());
		const size_t before = deliveries.count();
		{
			const detail::ScopedActorFailureForTesting failure(detail::ActorFailure::QueueInsert);
			made->update_viewport(1u, {tile(1u, 0u, 0u)});
		}
		ASSERT_TRUE(deliveries.wait_for(before + 1u));
		EXPECT_EQ(deliveries.all.back().second.error().code, ErrorCode::OutOfMemory);
	}
}

} // namespace
} // namespace recto::core
