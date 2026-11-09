// recto::core::Runtime (D01 T02 section 10).
#include "../../src/core/runtime.h"

#include "../../src/pdf/display_list.h"

#include "cancel_test_support.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <winioctl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace recto::core {
namespace {

using pdf::ErrorCode;
using pdf::detail::CancelSite;
using Delivered = pdf::PdfResult<std::shared_ptr<const TileImage>>;

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

std::string busy_content()
{
	std::string content;
	for (int i = 0; i < 2'000; ++i) {
		content += "0." + std::to_string(i % 10) + " 0 0 rg " + std::to_string(i % 180) + " 10 8 8 re f ";
	}
	return content;
}

struct Deliveries {
	std::mutex mutex;
	std::condition_variable arrived;
	std::vector<std::pair<TileKey, Delivered>> all;

	Deliveries()
	{
		all.reserve(16'384u);
	}

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

	size_t count()
	{
		const std::lock_guard lock(mutex);
		return all.size();
	}
};

TileRequest tile(DocumentId doc, uint32_t page, uint32_t x)
{
	TileRequest request;
	request.key = TileKey{doc, page, 1'000u, 0, x, 0u};
	request.spec.tile_x_px = x * 64u;
	request.spec.tile_width_px = 64u;
	request.spec.tile_height_px = 64u;
	return request;
}

std::unique_ptr<Runtime> runtime(Deliveries& deliveries, RuntimeOptions options = {})
{
	options.deliver = deliveries.sink();
	auto made = Runtime::create(std::move(options));
	EXPECT_TRUE(made.has_value());
	return made.has_value() ? std::move(*made) : nullptr;
}

template <typename Ready>
bool wait_until(Ready&& ready, std::chrono::seconds limit = std::chrono::seconds(30))
{
	const auto until = std::chrono::steady_clock::now() + limit;
	while (!ready() && std::chrono::steady_clock::now() < until) {
		std::this_thread::yield();
	}
	return ready();
}

// Counts polls at a site on any thread it is installed on, and holds them while `held` is set.
struct Polls {
	std::atomic<uint64_t> compiling{0u};
	std::atomic<uint64_t> rendering{0u};
	std::atomic<uint64_t> reading{0u};
	std::atomic<bool> hold_reads{false};

	static void hook(CancelSite site, void* context) noexcept
	{
		auto& self = *static_cast<Polls*>(context);
		if (site == CancelSite::InterpreterOperator) {
			self.compiling.fetch_add(1u);
		} else if (site == CancelSite::FileRead) {
			self.reading.fetch_add(1u);
			const pdf::CancelToken* const token = pdf::detail::g_cancel_scope;
			while (self.hold_reads.load() && (token == nullptr || !token->is_cancelled())) {
				std::this_thread::yield();
			}
		} else if (site == CancelSite::RasterCommand || site == CancelSite::RasterFinish) {
			self.rendering.fetch_add(1u);
		}
	}
};

TEST(Runtime, OpenReturnsAtOnce)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("doc.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, "0 0 10 10 re f"));
	Polls polls;
	polls.hold_reads.store(true);
	RuntimeOptions options;
	options.actor_options.poll_hook = &Polls::hook;
	options.actor_options.poll_hook_context = &polls;
	Deliveries deliveries;
	auto made = runtime(deliveries, std::move(options));
	ASSERT_NE(made, nullptr);
	// Held at its file read: the call has returned and the open is still pending.
	auto first = made->open_document(path);
	ASSERT_TRUE(first.has_value());
	ASSERT_TRUE(wait_until([&] { return polls.reading.load() > 0u; }));
	EXPECT_EQ(first->opened.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	EXPECT_NE(made->document(first->id), nullptr);
	polls.hold_reads.store(false);
	EXPECT_TRUE(first->opened.get().has_value());
	// A password file asks for it, and opens through `authenticate`.
	auto locked = made->open_document(recto::test::corpus_path("render/encrypted-aes-128.pdf"));
	ASSERT_TRUE(locked.has_value());
	const auto asked = locked->opened.get();
	ASSERT_FALSE(asked.has_value());
	EXPECT_EQ(asked.error().code, ErrorCode::NeedsPassword);
	auto unlocked = made->document(locked->id)->authenticate("recto-test");
	EXPECT_TRUE(unlocked.get().has_value());
	EXPECT_EQ(made->document(locked->id)->state(), DocumentState::Opened);
	// A file that is not there: its typed failure, and the id stays until it is closed.
	auto missing = made->open_document(dir.file_path("missing.pdf"));
	ASSERT_TRUE(missing.has_value());
	EXPECT_FALSE(missing->opened.get().has_value());
	EXPECT_EQ(made->document(missing->id)->state(), DocumentState::Failed);
	EXPECT_TRUE(made->close_document(missing->id).has_value());
	EXPECT_EQ(made->document(missing->id), nullptr);
	EXPECT_EQ(made->close_document(missing->id).error().code, ErrorCode::InvalidArgument);
	// Closed while it opens.
	polls.hold_reads.store(true);
	const uint64_t reads = polls.reading.load();
	auto closing = made->open_document(path);
	ASSERT_TRUE(closing.has_value());
	ASSERT_TRUE(wait_until([&] { return polls.reading.load() > reads; }));
	EXPECT_TRUE(made->close_document(closing->id).has_value());
	const auto cancelled = closing->opened.get();
	ASSERT_FALSE(cancelled.has_value());
	EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);
	polls.hold_reads.store(false);
	const ShutdownReport report = made->shutdown();
	EXPECT_EQ(report, ShutdownReport{});
}

TEST(Runtime, AdmissionFailures)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("doc.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, "0 0 10 10 re f"));
	Deliveries deliveries;
	auto made = runtime(deliveries);
	ASSERT_NE(made, nullptr);
	const int64_t before = debug_live_documents();
	{
		const detail::ScopedActorFailureForTesting failure(detail::ActorFailure::ThreadStart);
		const auto refused = made->open_document(path);
		ASSERT_FALSE(refused.has_value());
		EXPECT_EQ(refused.error().code, ErrorCode::InternalError);
	}
	EXPECT_EQ(debug_live_documents(), before);
	EXPECT_EQ(made->document(1u), nullptr) << "a refused document was recorded";
	EXPECT_EQ(made->close_document(77u).error().code, ErrorCode::InvalidArgument);
	if (recto::test::ScopedAllocationFailure::available()) {
		size_t refused = 0u;
		for (uint32_t nth = 1u; nth <= 24u; ++nth) {
			SCOPED_TRACE(nth);
			pdf::PdfResult<OpenedDocument> opened = pdf::detail::cancelled_error();
			std::filesystem::path copy = path; // copied out here, so only the runtime allocates inside
			{
				const recto::test::ScopedAllocationFailure fail(nth, 32u);
				opened = made->open_document(std::move(copy));
			}
			if (opened.has_value()) {
				(void)opened->opened.get();
				auto closed = made->close_document(opened->id);
				ASSERT_TRUE(closed.has_value());
				closed->wait(); // closing no longer waits, so the count is read once the actor has finished
			} else {
				++refused;
				EXPECT_EQ(opened.error().code, ErrorCode::OutOfMemory);
			}
			EXPECT_EQ(debug_live_documents(), before) << "an actor was left behind";
		}
		EXPECT_GT(refused, 1u) << "no allocation inside open_document was reached";
	}
	EXPECT_EQ(made->shutdown(), ShutdownReport{});
}

TEST(Runtime, OrderedShutdownSequence)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("busy.pdf");
	recto::test::write_file_bytes(path, pdf_of(2u, busy_content()));
	std::vector<int> steps;
	RuntimeOptions options;
	options.worker_count = 2u;
	options.on_step = [&steps](int step) noexcept { steps.push_back(step); };
	Deliveries deliveries;
	auto made = runtime(deliveries, std::move(options));
	ASSERT_NE(made, nullptr);
	auto opened = made->open_document(path);
	ASSERT_TRUE(opened.has_value());
	ASSERT_TRUE(opened->opened.get().has_value());
	made->update_viewport(opened->id, {tile(opened->id, 0u, 0u), tile(opened->id, 1u, 0u)});
	// A real job thread, stopped by its own flag, and a real presentation thread.
	std::atomic<bool> job_stop{false};
	std::atomic<bool> job_joined{false};
	std::thread job([&] {
		while (!job_stop.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});
	int64_t documents_at_job_join = -1;
	int64_t workers_at_job_join = -1;
	JobHooks hooks;
	hooks.cancel = [&]() noexcept { job_stop.store(true); };
	hooks.join = [&]() noexcept {
		documents_at_job_join = debug_live_documents();
		workers_at_job_join = debug_live_workers();
		job.join();
		job_joined.store(true);
	};
	ASSERT_TRUE(made->register_job(std::move(hooks)).has_value());
	std::atomic<bool> present_stop{false};
	std::thread presenter([&] {
		while (!present_stop.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});
	int64_t workers_at_presentation = -1;
	bool job_done_at_presentation = false;
	ASSERT_TRUE(made->register_presentation_hook([&]() noexcept {
		workers_at_presentation = debug_live_workers();
		job_done_at_presentation = job_joined.load();
		present_stop.store(true);
		presenter.join();
	}).has_value());
	const ShutdownReport report = made->shutdown();
	EXPECT_EQ(steps, (std::vector<int>{1, 2, 3, 4, 5, 6}));
	EXPECT_EQ(documents_at_job_join, 0) << "an actor was still running when the jobs were joined";
	EXPECT_EQ(workers_at_job_join, 2) << "the workers were gone before the jobs were joined";
	EXPECT_EQ(workers_at_presentation, 0) << "the workers were still running at the presentation hooks";
	EXPECT_TRUE(job_done_at_presentation);
	EXPECT_EQ(report, ShutdownReport{});
	EXPECT_FALSE(presenter.joinable());
	EXPECT_FALSE(job.joinable());
}

TEST(Runtime, IdempotentShutdown)
{
	int steps = 0;
	RuntimeOptions options;
	options.on_step = [&steps](int) noexcept { ++steps; };
	Deliveries deliveries;
	auto made = runtime(deliveries, std::move(options));
	ASSERT_NE(made, nullptr);
	const ShutdownReport first = made->shutdown();
	const ShutdownReport second = made->shutdown();
	EXPECT_EQ(first, second);
	made.reset(); // the destructor runs nothing more
	EXPECT_EQ(steps, 6);
}

TEST(Runtime, AfterShutdown)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("doc.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, "0 0 10 10 re f"));
	Deliveries deliveries;
	auto made = runtime(deliveries);
	ASSERT_NE(made, nullptr);
	int cancelled = 0;
	int joined = 0;
	int stale = 0;
	JobHooks kept;
	kept.cancel = [&]() noexcept { ++cancelled; };
	kept.join = [&]() noexcept { ++joined; };
	ASSERT_TRUE(made->register_job(std::move(kept)).has_value());
	JobHooks gone;
	gone.cancel = [&]() noexcept { ++stale; };
	gone.join = [&]() noexcept { ++stale; };
	const auto gone_id = made->register_job(std::move(gone));
	ASSERT_TRUE(gone_id.has_value());
	made->unregister_job(*gone_id);
	std::vector<int> order;
	ASSERT_TRUE(made->register_presentation_hook([&]() noexcept { order.push_back(1); }).has_value());
	ASSERT_TRUE(made->register_presentation_hook([&]() noexcept { order.push_back(2); }).has_value());
	(void)made->shutdown();
	EXPECT_EQ(cancelled, 1);
	EXPECT_EQ(joined, 1);
	EXPECT_EQ(stale, 0) << "an unregistered job was called";
	EXPECT_EQ(order, (std::vector<int>{1, 2}));
	EXPECT_EQ(made->open_document(path).error().code, ErrorCode::Cancelled);
	made->update_viewport(1u, {tile(1u, 0u, 0u)});
	ASSERT_EQ(deliveries.count(), 1u);
	EXPECT_EQ(deliveries.all.back().second.error().code, ErrorCode::Cancelled);
	EXPECT_EQ(made->register_job(JobHooks{}).error().code, ErrorCode::Cancelled);
	EXPECT_EQ(made->register_presentation_hook([]() noexcept {}).error().code, ErrorCode::Cancelled);
}

// Holds every compile and every tile render where it is until it is cancelled, counting those held, so a shutdown is
// known to meet work running on every thread.
struct Busy {
	std::atomic<bool> armed{false};
	std::atomic<int> compiling{0};
	std::atomic<int> rendering{0};

	static void hook(CancelSite site, void* context) noexcept
	{
		auto& self = *static_cast<Busy*>(context);
		if (!self.armed.load()) {
			return;
		}
		std::atomic<int>* counter = site == CancelSite::InterpreterOperator ? &self.compiling
			: site == CancelSite::RasterCommand                          ? &self.rendering
																		  : nullptr;
		if (counter == nullptr) {
			return;
		}
		const pdf::CancelToken* const token = pdf::detail::g_cancel_scope;
		counter->fetch_add(1);
		while (token != nullptr && !token->is_cancelled()) {
			std::this_thread::yield();
		}
		counter->fetch_sub(1);
	}
};

// Four documents compiling and eight workers rendering 512 x 512 tiles, every one of them held there, then shut down by
// `shutdown()` or by the destructor.
ShutdownReport shutdown_under_load(const std::filesystem::path& path, double& shutdown_ms, bool by_destructor = false)
{
	Busy busy;
	RuntimeOptions options;
	options.worker_count = 8u;
	options.actor_options.poll_hook = &Busy::hook;
	options.actor_options.poll_hook_context = &busy;
	options.worker_poll_hook = &Busy::hook;
	options.worker_poll_hook_context = &busy;
	Deliveries deliveries;
	auto made = runtime(deliveries, std::move(options));
	EXPECT_NE(made, nullptr);
	if (made == nullptr) {
		return {};
	}
	std::vector<DocumentId> ids;
	for (int doc = 0; doc < 5; ++doc) {
		auto opened = made->open_document(path);
		EXPECT_TRUE(opened.has_value());
		if (!opened.has_value()) {
			return {};
		}
		EXPECT_TRUE(opened->opened.get().has_value());
		ids.push_back(opened->id);
	}
	// Document 0's page is compiled before the hold, so its eight tiles reach the workers while documents 1 to 4 compile.
	auto warm = made->document(ids[0])->get_display_list(0u);
	EXPECT_TRUE(warm.get().has_value());
	busy.armed.store(true);
	std::vector<TileRequest> render;
	for (uint32_t x = 0; x < 8u; ++x) {
		TileRequest request = tile(ids[0], 0u, x);
		request.spec.dpi_scale = 20.0; // a 4,000 pixel page, so each of the eight 512 x 512 tiles draws
		request.spec.tile_x_px = x * 500u;
		request.spec.tile_width_px = 512u;
		request.spec.tile_height_px = 512u;
		render.push_back(request);
	}
	made->update_viewport(ids[0], std::move(render));
	for (size_t i = 1; i < ids.size(); ++i) {
		made->update_viewport(ids[i], {tile(ids[i], static_cast<uint32_t>(i), 0u)});
	}
	EXPECT_TRUE(wait_until([&] { return busy.compiling.load() == 4 && busy.rendering.load() == 8; }))
		<< "not every actor was compiling and every worker rendering: " << busy.compiling.load() << " and " << busy.rendering.load();	const auto started = std::chrono::steady_clock::now();
	ShutdownReport report{};
	if (by_destructor) {
		made.reset();
		report = ShutdownReport{debug_live_documents(), pdf::debug_live_display_lists(), debug_live_workers()};
	} else {
		report = made->shutdown();
	}
	shutdown_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	return report;
}
TEST(Runtime, ShutdownUnderActiveTileLoad)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("load.pdf");
	recto::test::write_file_bytes(path, pdf_of(6u, busy_content()));
	for (int iteration = 0; iteration < 50; ++iteration) {
		SCOPED_TRACE(iteration);
		double ms = 0.0;
		EXPECT_EQ(shutdown_under_load(path, ms, iteration % 2 == 1), ShutdownReport{});
		EXPECT_EQ(debug_live_runtimes(), 0);
	}
}

TEST(Runtime, ShutdownLatency)
{
	recto::test::TempDir dir;
	const auto path = dir.file_path("load.pdf");
	recto::test::write_file_bytes(path, pdf_of(6u, busy_content()));
	double ms = 0.0;
	EXPECT_EQ(shutdown_under_load(path, ms), ShutdownReport{});
	std::printf("[          ] shutdown under load: returned in %.3f ms\n", ms);
#if !defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
	if (recto::test::cancel_latency_held() && ms >= 100.0) {
		const double busy = recto::test::other_processes_busy();
		if (busy > recto::test::kBusyHostShare) {
			std::printf("[          ] shutdown under load: not held, other processes use %.0f%% of the processors\n", busy * 100.0);
		} else {
			ADD_FAILURE() << "shutdown under load took " << ms << " ms on a host that was not otherwise busy";
		}
	}
#endif
}

TEST(Runtime, CloseReturnsAtOnce)
{
	// A compile that will not stop for a while: closing its document returns at once, and its future is ready later.
	recto::test::TempDir dir;
	const auto path = dir.file_path("busy.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, busy_content()));
	struct Stuck {
		std::atomic<bool> reached{false};
		std::atomic<bool> released{false};
		static void hook(CancelSite site, void* context) noexcept
		{
			auto& self = *static_cast<Stuck*>(context);
			if (site == CancelSite::InterpreterOperator && !self.released.load()) {
				self.reached.store(true);
				while (!self.released.load()) {
					std::this_thread::yield(); // deaf to the cancel, as a long uninterruptible call would be
				}
			}
		}
	} stuck;
	RuntimeOptions options;
	options.actor_options.poll_hook = &Stuck::hook;
	options.actor_options.poll_hook_context = &stuck;
	Deliveries deliveries;
	auto made = runtime(deliveries, std::move(options));
	ASSERT_NE(made, nullptr);
	auto opened = made->open_document(path);
	ASSERT_TRUE(opened.has_value());
	ASSERT_TRUE(opened->opened.get().has_value());
	auto compile = made->document(opened->id)->get_display_list(0u);
	ASSERT_TRUE(wait_until([&] { return stuck.reached.load(); }));
	const auto asked = std::chrono::steady_clock::now();
	auto closed = made->close_document(opened->id);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - asked).count();
	ASSERT_TRUE(closed.has_value());
	EXPECT_LT(ms, 50.0) << "close_document waited on the actor";
	EXPECT_EQ(closed->wait_for(std::chrono::milliseconds(0)), std::future_status::timeout) << "the actor was not still busy";
	stuck.released.store(true);
	EXPECT_EQ(closed->wait_for(std::chrono::seconds(30)), std::future_status::ready);
	EXPECT_EQ(compile.get().error().code, ErrorCode::Cancelled);
	EXPECT_EQ(made->shutdown(), ShutdownReport{});
}

TEST(Runtime, CloseDuringStalledOpen)
{
	// The file is held by a batch oplock this test never lets go: the actor's open waits inside the system, where no poll
	// reaches it. Closing the document cancels that wait, and shutdown does not hang on it.
	recto::test::TempDir dir;
	const auto path = dir.file_path("held.pdf");
	recto::test::write_file_bytes(path, pdf_of(1u, "0 0 10 10 re f"));
	const HANDLE holder = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	ASSERT_NE(holder, INVALID_HANDLE_VALUE);
	OVERLAPPED oplock{};
	oplock.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	ASSERT_NE(oplock.hEvent, nullptr);
	const BOOL granted = ::DeviceIoControl(holder, FSCTL_REQUEST_BATCH_OPLOCK, nullptr, 0u, nullptr, 0u, nullptr, &oplock);
	if (granted != FALSE || ::GetLastError() != ERROR_IO_PENDING) {
		::CloseHandle(oplock.hEvent);
		::CloseHandle(holder);
		GTEST_SKIP() << "this volume does not grant batch oplocks";
	}
	Deliveries deliveries;
	auto made = runtime(deliveries);
	ASSERT_NE(made, nullptr);
	auto opened = made->open_document(path);
	ASSERT_TRUE(opened.has_value());
	// The break is signalled when the actor's open reaches the file; from here that open waits for an acknowledgement.
	EXPECT_EQ(::WaitForSingleObject(oplock.hEvent, 30'000u), static_cast<DWORD>(WAIT_OBJECT_0)) << "the open never reached the file";
	EXPECT_EQ(opened->opened.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout) << "the open was not held";
	const auto asked = std::chrono::steady_clock::now();
	auto closed = made->close_document(opened->id);
	ASSERT_TRUE(closed.has_value());
	const ShutdownReport report = made->shutdown(); // joins the actor, cancelling its wait
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - asked).count();
	std::printf("[          ] closing during a stalled open: returned in %.3f ms\n", ms);
	EXPECT_LT(ms, 5'000.0) << "shutdown waited for the system to give the file up";
	const auto result = opened->opened.get();
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
	EXPECT_EQ(report, ShutdownReport{});
	::CloseHandle(holder);
	::CloseHandle(oplock.hEvent);
}
} // namespace
} // namespace recto::core
