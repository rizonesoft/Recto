// recto::core::DocumentHandle (D01 T02 section 8).
#include "../../src/core/atomic_write.h"
#include "../../src/core/document_actor.h"

#include "../../src/pdf/error.h"

#include "cancel_test_support.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace recto::core {
namespace {

using pdf::CancelSource;
using pdf::CancelToken;
using pdf::DisplayList;
using pdf::Document;
using pdf::ErrorCode;
using pdf::detail::CancelSite;

// A complete PDF of `pages` pages of `content`, with the `extra` objects numbered from 5 upward after each page's.
std::vector<uint8_t> pdf_of(size_t pages, std::string_view content, std::string_view resources = "<< >>", const std::vector<std::string>& extra = {},
	std::string_view trailer_extra = {}, bool page_contents = true)
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
		objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources " + std::string(resources) +
			(page_contents ? " /Contents 3 0 R" : "") + " >>");
	}
	for (const std::string& object : extra) {
		objects.push_back(object);
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
	out += "trailer\n<< /Size " + std::to_string(objects.size() + 1u) + " /Root 1 0 R " + std::string(trailer_extra) + " >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return std::vector<uint8_t>(out.begin(), out.end());
}

std::filesystem::path written(const recto::test::TempDir& dir, std::string_view name, const std::vector<uint8_t>& bytes)
{
	const std::filesystem::path path = dir.file_path(name);
	recto::test::write_file_bytes(path, bytes);
	return path;
}

std::shared_ptr<DocumentHandle> started(const ActorOptions& options = {})
{
	auto handle = DocumentHandle::start(options);
	EXPECT_TRUE(handle.has_value());
	return handle.has_value() ? *handle : nullptr;
}

template <typename T>
pdf::PdfResult<T> finish(ActorFuture<T> future)
{
	EXPECT_TRUE(future.valid());
	EXPECT_EQ(future.wait_for(std::chrono::seconds(60)), std::future_status::ready) << "a request was never answered";
	return future.get();
}

template <typename T>
void expect_code(ActorFuture<T> future, ErrorCode code)
{
	const auto result = finish(std::move(future));
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, code);
}

// Holds the actor at the first poll of `site` until released, so what is queued behind it stays queued.
struct Gate {
	CancelSite site{CancelSite::InterpreterOperator};
	std::atomic<bool> reached{false};
	std::atomic<bool> released{false};

	static void hook(CancelSite at, void* context) noexcept
	{
		auto& gate = *static_cast<Gate*>(context);
		if (at == gate.site && !gate.released.load()) {
			gate.reached.store(true);
			while (!gate.released.load()) {
				std::this_thread::yield();
			}
		}
	}

	void wait_reached() const
	{
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (!reached.load() && std::chrono::steady_clock::now() < until) {
			std::this_thread::yield();
		}
		EXPECT_TRUE(reached.load());
	}

	void release()
	{
		released.store(true);
	}
};

ActorOptions held_by(Gate& gate)
{
	ActorOptions options;
	options.poll_hook = &Gate::hook;
	options.poll_hook_context = &gate;
	return options;
}

TEST(Actor, AsyncOpenAndDescription)
{
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	EXPECT_EQ(handle->state(), DocumentState::Unopened);
	ASSERT_TRUE(finish(handle->open(recto::test::corpus_path("text/two-column-links-outline.pdf"))).has_value());
	EXPECT_EQ(handle->state(), DocumentState::Opened);
	const auto description = finish(handle->get_description());
	ASSERT_TRUE(description.has_value());
	EXPECT_GE(description->page_count, 1u);
	const auto outline = finish(handle->get_outline());
	ASSERT_TRUE(outline.has_value());
	EXPECT_FALSE(outline->empty()) << "this file has a multi-level outline";
	const auto links = finish(handle->get_page_links(0u));
	ASSERT_TRUE(links.has_value());
	EXPECT_FALSE(links->empty()) << "its first page has links";
	const auto fonts = finish(handle->get_fonts());
	ASSERT_TRUE(fonts.has_value());
	EXPECT_FALSE(fonts->empty());
	// The same as a Document read directly.
	auto direct = Document::open(recto::test::corpus_path("text/two-column-links-outline.pdf"));
	ASSERT_TRUE(direct.has_value());
	EXPECT_EQ(description->page_count, (*direct)->page_count());
	EXPECT_EQ(outline->size(), (*direct)->outline().size());
	EXPECT_EQ(fonts->size(), (*direct)->fonts().size());
	expect_code(handle->get_page_links(10'000u), (*direct)->page_links(10'000u).error().code);
}

TEST(Actor, PasswordStateMachine)
{
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	expect_code(handle->open(recto::test::corpus_path("render/encrypted-aes-256.pdf")), ErrorCode::NeedsPassword);
	EXPECT_EQ(handle->state(), DocumentState::NeedsPassword);
	expect_code(handle->authenticate("not it"), ErrorCode::WrongPassword);
	EXPECT_EQ(handle->state(), DocumentState::WrongPassword);
	expect_code(handle->authenticate("nor this"), ErrorCode::WrongPassword);
	EXPECT_EQ(handle->state(), DocumentState::WrongPassword);
	ASSERT_TRUE(finish(handle->authenticate("recto-test")).has_value());
	EXPECT_EQ(handle->state(), DocumentState::Opened);
	const auto description = finish(handle->get_description());
	ASSERT_TRUE(description.has_value());
	EXPECT_GE(description->page_count, 1u);
	const auto list = finish(handle->get_display_list(0u));
	ASSERT_TRUE(list.has_value());
	EXPECT_GT((*list)->command_count(), 0u);
}

TEST(Actor, StateTable)
{
	recto::test::TempDir dir;
	// Unopened: queries and authenticate are refused, and open works.
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		expect_code(handle->get_description(), ErrorCode::InvalidArgument);
		expect_code(handle->get_display_list(0u), ErrorCode::InvalidArgument);
		expect_code(handle->get_outline(), ErrorCode::InvalidArgument);
		expect_code(handle->get_page_links(0u), ErrorCode::InvalidArgument);
		expect_code(handle->get_fonts(), ErrorCode::InvalidArgument);
		expect_code(handle->get_first_free_object(), ErrorCode::InvalidArgument);
		expect_code(handle->authenticate("x"), ErrorCode::InvalidArgument);
		EXPECT_EQ(handle->state(), DocumentState::Unopened);
	}
	// A file that cannot open: Failed, refusing queries, and able to open again.
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		EXPECT_FALSE(finish(handle->open(dir.file_path("missing.pdf"))).has_value());
		EXPECT_EQ(handle->state(), DocumentState::Failed);
		expect_code(handle->get_description(), ErrorCode::InvalidArgument);
		expect_code(handle->authenticate("x"), ErrorCode::InvalidArgument);
		ASSERT_TRUE(finish(handle->open(written(dir, "good.pdf", pdf_of(1u, "0 0 10 10 re f")))).has_value());
		EXPECT_EQ(handle->state(), DocumentState::Opened);
		// Opened: no second open, no authenticate.
		expect_code(handle->open(dir.file_path("good.pdf")), ErrorCode::InvalidArgument);
		expect_code(handle->authenticate("x"), ErrorCode::InvalidArgument);
	}
	// Locked: every query and a save say so, and never answer with an empty description.
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		expect_code(handle->open(recto::test::corpus_path("render/encrypted-aes-128.pdf")), ErrorCode::NeedsPassword);
		for (int pass = 0; pass < 2; ++pass) {
			expect_code(handle->get_description(), ErrorCode::NeedsPassword);
			expect_code(handle->get_display_list(0u), ErrorCode::NeedsPassword);
			expect_code(handle->get_outline(), ErrorCode::NeedsPassword);
			expect_code(handle->get_page_links(0u), ErrorCode::NeedsPassword);
			expect_code(handle->get_fonts(), ErrorCode::NeedsPassword);
			expect_code(handle->get_first_free_object(), ErrorCode::NeedsPassword);
			expect_code(handle->save_incremental(pdf::ObjectChangeSet(pdf::RevisionId{0u}, 100u), dir.file_path("out.pdf")), ErrorCode::NeedsPassword);
			expect_code(handle->open(dir.file_path("good.pdf")), ErrorCode::InvalidArgument);
			if (pass == 0) {
				expect_code(handle->authenticate("wrong"), ErrorCode::WrongPassword); // the same from WrongPassword
			}
		}
	}
	// An owner-restricted file, whose user password is empty, opens with none.
	{
		const std::string encrypt =
			"<< /Filter /Standard /V 2 /R 3 /Length 128 /P -3904 "
			"/O <0000000000000000000000000000000000000000000000000000000000000000> "
			"/U <A0685F035CD33F87FA04C93FA5380B3300000000000000000000000000000000> >>";
		const auto bytes = pdf_of(1u, "", "<< >>", {encrypt},
			"/Encrypt 5 0 R /ID [<00000000000000000000000000000000> <00000000000000000000000000000000>]", false);
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		const auto opened = finish(handle->open(written(dir, "owner.pdf", bytes)));
		ASSERT_TRUE(opened.has_value()) << opened.error().message.view();
		EXPECT_EQ(handle->state(), DocumentState::Opened);
	}
	// Closed: everything is Cancelled, and the state says so.
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		handle->close().wait();
		EXPECT_EQ(handle->state(), DocumentState::Closed);
		expect_code(handle->get_description(), ErrorCode::Cancelled);
		expect_code(handle->open(dir.file_path("good.pdf")), ErrorCode::Cancelled);
		expect_code(handle->authenticate("x"), ErrorCode::Cancelled);
	}
}

TEST(Actor, PasswordWiped)
{
	const auto wiped = [] { return detail::debug_wiped_passwords(); };
	// Used and right; used and wrong.
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		expect_code(handle->open(recto::test::corpus_path("render/encrypted-aes-128.pdf")), ErrorCode::NeedsPassword);
		uint64_t before = wiped();
		expect_code(handle->authenticate("wrong"), ErrorCode::WrongPassword);
		EXPECT_EQ(wiped(), before + 1u) << "after a wrong password";
		before = wiped();
		ASSERT_TRUE(finish(handle->authenticate("recto-test")).has_value());
		EXPECT_EQ(wiped(), before + 1u) << "after the right one";
	}
	// Refused by a full queue, and dropped by a close: never reaching the thread.
	{
		Gate gate;
		recto::test::TempDir dir;
		ActorOptions options = held_by(gate);
		options.max_queue = 2u;
		gate.site = CancelSite::FileRead;
		const auto handle = started(options);
		ASSERT_NE(handle, nullptr);
		auto held = handle->open(written(dir, "a.pdf", pdf_of(1u, "")));
		gate.wait_reached();
		const uint64_t before = wiped();
		auto first = handle->authenticate("queued");
		auto second = handle->authenticate("queued too");
		auto refused = handle->authenticate("past the limit");
		EXPECT_EQ(wiped(), before + 1u) << "refused by the full queue";
		expect_code(std::move(refused), ErrorCode::ResourceLimit);
		const auto closed = handle->close();
		gate.release();
		closed.wait();
		EXPECT_EQ(wiped(), before + 3u) << "dropped by the close";
		expect_code(std::move(first), ErrorCode::Cancelled);
		expect_code(std::move(second), ErrorCode::Cancelled);
		(void)finish(std::move(held));
	}
}

TEST(Actor, DisplayListLruCache)
{
	recto::test::TempDir dir;
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(written(dir, "many.pdf", pdf_of(40u, "0 0 10 10 re f")))).has_value());
	// Ten requests, one compile, one list.
	std::shared_ptr<const DisplayList> first;
	for (int i = 0; i < 10; ++i) {
		const auto list = finish(handle->get_display_list(0u));
		ASSERT_TRUE(list.has_value());
		if (first == nullptr) {
			first = *list;
		}
		EXPECT_EQ(list->get(), first.get());
	}
	EXPECT_EQ(handle->stats().cache_hits, 9u);
	EXPECT_EQ(handle->stats().cache_misses, 1u);
	// Forty pages: never more than 32 kept, and page 0, bumped along the way, survives where page 1 does not.
	for (size_t page = 0; page < 40u; ++page) {
		ASSERT_TRUE(finish(handle->get_display_list(page)).has_value());
		if (page % 8u == 0u) {
			ASSERT_TRUE(finish(handle->get_display_list(0u)).has_value());
		}
		EXPECT_LE(handle->stats().cached_pages, kMaxActorCachedPages);
	}
	EXPECT_EQ(handle->stats().cached_pages, kMaxActorCachedPages);
	const uint64_t misses = handle->stats().cache_misses;
	EXPECT_EQ(finish(handle->get_display_list(0u))->get(), first.get()) << "a page in use was evicted";
	EXPECT_EQ(handle->stats().cache_misses, misses);
	ASSERT_TRUE(finish(handle->get_display_list(1u)).has_value());
	EXPECT_EQ(handle->stats().cache_misses, misses + 1u) << "the oldest page should have gone";
	// A page that cannot be compiled, or whose compile was cancelled, is not kept.
	const size_t kept = handle->stats().cached_pages;
	EXPECT_FALSE(finish(handle->get_display_list(10'000u)).has_value());
	auto source = CancelSource::create();
	ASSERT_TRUE(source.has_value());
	source->cancel();
	const uint64_t before_cancel = handle->stats().cache_misses;
	expect_code(handle->get_display_list(2u, source->token()), ErrorCode::Cancelled);
	EXPECT_EQ(handle->stats().cached_pages, kept);
	EXPECT_EQ(handle->stats().cache_misses, before_cancel) << "it was cancelled at its turn in the queue";
}

TEST(Actor, DisplayListByteBudget)
{
	recto::test::TempDir dir;
	std::string content;
	for (int i = 0; i < 2000; ++i) {
		content += "0 0 10 10 re f\n";
	}
	const auto path = written(dir, "heavy.pdf", pdf_of(8u, content));
	size_t one_list = 0u;
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		ASSERT_TRUE(finish(handle->open(path)).has_value());
		const auto list = finish(handle->get_display_list(0u));
		ASSERT_TRUE(list.has_value());
		one_list = (*list)->memory_bytes();
		EXPECT_GT(one_list, 2000u * sizeof(pdf::DisplayCommand));
	}
	ActorOptions options;
	options.max_list_bytes = one_list * 3u + one_list / 2u;
	const auto handle = started(options);
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(path)).has_value());
	for (size_t page = 0; page < 8u; ++page) {
		ASSERT_TRUE(finish(handle->get_display_list(page)).has_value());
		EXPECT_LE(handle->stats().cached_bytes, options.max_list_bytes);
	}
	EXPECT_EQ(handle->stats().cached_pages, 3u);
	// A limit smaller than one list still holds the newest.
	options.max_list_bytes = 1u;
	const auto tight = started(options);
	ASSERT_NE(tight, nullptr);
	ASSERT_TRUE(finish(tight->open(path)).has_value());
	for (size_t page = 0; page < 3u; ++page) {
		ASSERT_TRUE(finish(tight->get_display_list(page)).has_value());
		EXPECT_EQ(tight->stats().cached_pages, 1u);
	}
}

TEST(Actor, ConcurrentRequests)
{
	recto::test::TempDir dir;
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(written(dir, "many.pdf", pdf_of(8u, "0 0 10 10 re f")))).has_value());
	std::atomic<int> answered{0};
	std::vector<std::thread> callers;
	for (int t = 0; t < 8; ++t) {
		callers.emplace_back([&, t] {
			for (int i = 0; i < 40; ++i) {
				auto list = handle->get_display_list(static_cast<size_t>((t + i) % 8));
				auto description = handle->get_description();
				const auto got = finish(std::move(list));
				ASSERT_TRUE(got.has_value());
				EXPECT_EQ((*got)->command_count() > 0u, true);
				ASSERT_TRUE(finish(std::move(description)).has_value());
				answered.fetch_add(1);
			}
		});
	}
	for (std::thread& caller : callers) {
		caller.join();
	}
	EXPECT_EQ(answered.load(), 8 * 40);
	EXPECT_EQ(handle->stats().cache_misses, 8u) << "each page compiled once, whichever caller asked first";
	EXPECT_EQ(handle->stats().cache_hits, 8u * 40u - 8u);
}

TEST(Actor, TwentyDocumentsLifecycle)
{
	recto::test::TempDir dir;
	const auto path = written(dir, "doc.pdf", pdf_of(4u, "0 0 10 10 re f"));
	const int64_t before_documents = debug_live_documents();
	const int64_t before_lists = pdf::debug_live_display_lists();
	{
		std::vector<std::thread> tabs;
		std::atomic<int> opened{0};
		for (int i = 0; i < 20; ++i) {
			tabs.emplace_back([&] {
				const auto handle = started();
				ASSERT_NE(handle, nullptr);
				ASSERT_TRUE(finish(handle->open(path)).has_value());
				for (size_t page = 0; page < 4u; ++page) {
					ASSERT_TRUE(finish(handle->get_display_list(page)).has_value());
				}
				opened.fetch_add(1);
			});
		}
		for (std::thread& tab : tabs) {
			tab.join();
		}
		EXPECT_EQ(opened.load(), 20);
	}
	EXPECT_EQ(debug_live_documents(), before_documents);
	EXPECT_EQ(pdf::debug_live_display_lists(), before_lists);
}

TEST(Actor, DocumentOnActorThread)
{
	// Diagnostics carry the thread they were recorded on, from inside the engine: a recovered file while it opens, a refused
	// XObject while a page compiles.
	recto::test::TempDir dir;
	const std::string postscript = "<< /Type /XObject /Subtype /PS /Length 0 >>\nstream\n\nendstream";
	const auto bytes = pdf_of(1u, "/P Do", "<< /XObject << /P 5 0 R >> >>", {postscript});
	pdf::clear_diagnostics();
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(written(dir, "ps.pdf", bytes))).has_value());
	ASSERT_TRUE(finish(handle->get_display_list(0u)).has_value());
	const auto second = started();
	ASSERT_NE(second, nullptr);
	ASSERT_TRUE(finish(second->open(recto::test::corpus_path("broken/corrupt-xref-recoverable.pdf"))).has_value());
	const auto tail = pdf::diagnostics_tail(64u);
	ASSERT_FALSE(tail.empty()) << "nothing was recorded, so nothing is proven";
	const uint32_t caller = ::GetCurrentThreadId();
	for (const pdf::DiagnosticEntry& entry : tail) {
		EXPECT_NE(entry.thread_id, caller) << entry.message;
	}
}

TEST(Actor, DocumentDestroyedBeforeClosed)
{
	// The handle outlives the close, so a `Document` left to the handle's destructor would still be alive when `closed` is
	// ready, and would then die on this thread instead of the actor's.
	recto::test::TempDir dir;
	const auto path = written(dir, "doc.pdf", pdf_of(1u, "0 0 10 10 re f"));
	const int64_t before = pdf::debug_live_document_objects();
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(path)).has_value());
	EXPECT_EQ(pdf::debug_live_document_objects(), before + 1) << "the open document is the only one";
	handle->close().wait();
	EXPECT_EQ(pdf::debug_live_document_objects(), before) << "the document outlived its actor's close";
	EXPECT_EQ(handle->state(), DocumentState::Closed);
}

TEST(Actor, RequestsReturnAtOnce)
{
	recto::test::TempDir dir;
	Gate gate;
	const auto handle = started(held_by(gate));
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(written(dir, "doc.pdf", pdf_of(1u, "0 0 10 10 re f")))).has_value());
	auto compile = handle->get_display_list(0u);
	gate.wait_reached();
	// The actor is held inside a compile: each of these has returned, and none is answered yet.
	auto description = handle->get_description();
	auto outline = handle->get_outline();
	auto links = handle->get_page_links(0u);
	auto fonts = handle->get_fonts();
	auto first_free = handle->get_first_free_object();
	EXPECT_EQ(compile.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	EXPECT_EQ(description.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	EXPECT_EQ(outline.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	EXPECT_EQ(links.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	EXPECT_EQ(fonts.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	EXPECT_EQ(first_free.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
	gate.release();
	EXPECT_TRUE(finish(std::move(compile)).has_value());
	EXPECT_TRUE(finish(std::move(description)).has_value());
	EXPECT_TRUE(finish(std::move(outline)).has_value());
	EXPECT_TRUE(finish(std::move(links)).has_value());
	EXPECT_TRUE(finish(std::move(fonts)).has_value());
	EXPECT_TRUE(finish(std::move(first_free)).has_value());
}

TEST(Actor, PendingRequestsCancelledOnClose)
{
	recto::test::TempDir dir;
	Gate gate;
	const auto handle = started(held_by(gate));
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(written(dir, "doc.pdf", pdf_of(2u, "0 0 10 10 re f")))).has_value());
	auto held = handle->get_display_list(0u);
	gate.wait_reached();
	std::vector<ActorFuture<std::shared_ptr<const DisplayList>>> pending;
	for (int i = 0; i < 50; ++i) {
		pending.push_back(handle->get_display_list(1u));
	}
	const auto closed = handle->close();
	gate.release();
	closed.wait();
	EXPECT_EQ(handle->state(), DocumentState::Closed);
	for (auto& future : pending) {
		expect_code(std::move(future), ErrorCode::Cancelled);
	}
	expect_code(std::move(held), ErrorCode::Cancelled);
	expect_code(handle->get_display_list(0u), ErrorCode::Cancelled);
}

TEST(Actor, CloseDuringCompile)
{
	// Ten forms, each drawing the next ten times: it would run for far longer than this test.
	std::vector<std::string> forms;
	const size_t first_form = 5u;
	for (int level = 0; level < 10; ++level) {
		std::string content;
		if (level == 9) {
			content = "0 0 1 1 re f";
		} else {
			for (int k = 0; k < 10; ++k) {
				content += "/F Do ";
			}
		}
		const std::string resources = level == 9 ? "" : " /Resources << /XObject << /F " + std::to_string(first_form + 1u + static_cast<size_t>(level)) + " 0 R >> >>";
		forms.push_back("<< /Type /XObject /Subtype /Form /BBox [0 0 100 100]" + resources + " /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
	}
	recto::test::TempDir dir;
	const auto path = written(dir, "fan.pdf", pdf_of(1u, "/F Do", "<< /XObject << /F 5 0 R >> >>", forms));
	struct Compiling {
		std::atomic<uint32_t> operators{0u};
		static void hook(CancelSite site, void* context) noexcept
		{
			if (site == CancelSite::InterpreterOperator) {
				static_cast<Compiling*>(context)->operators.fetch_add(1u);
			}
		}
	} compiling;
	ActorOptions options;
	options.poll_hook = &Compiling::hook;
	options.poll_hook_context = &compiling;
	const auto handle = started(options);
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(path)).has_value());
	auto list = handle->get_display_list(0u);
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (compiling.operators.load() < 2'000u && std::chrono::steady_clock::now() < until) {
		std::this_thread::yield();
	}
	ASSERT_GE(compiling.operators.load(), 2'000u);
	const auto started_close = std::chrono::steady_clock::now();
	const auto closed = handle->close();
	const auto returned_close = std::chrono::steady_clock::now();
	closed.wait();
	const auto finished_close = std::chrono::steady_clock::now();
	expect_code(std::move(list), ErrorCode::Cancelled);
	recto::test::expect_cancel_latency(std::chrono::duration<double, std::milli>(finished_close - started_close).count(), "closing during a compile");
	const double close_ms = std::chrono::duration<double, std::milli>(returned_close - started_close).count();
	if (recto::test::cancel_latency_held()) {
		EXPECT_LT(close_ms, 50.0) << "close() itself must not wait";
	}
}

TEST(Actor, CloseDuringOpen)
{
	// A cross-reference table of 300,000 rows, and the same file with it gone, so recovery scans all of it.
	recto::test::TempDir dir;
	std::vector<std::string> extra;
	extra.reserve(300'000u);
	for (int i = 0; i < 300'000; ++i) {
		extra.push_back("<< /A " + std::to_string(i) + " >>");
	}
	const auto bytes = pdf_of(1u, "0 0 1 1 re f", "<< >>", extra);
	std::vector<uint8_t> broken = bytes;
	const std::string needle = "startxref";
	broken.resize(static_cast<size_t>(std::search(broken.begin(), broken.end(), needle.begin(), needle.end()) - broken.begin()));
	for (const auto& [name, file] : {std::pair{std::string_view("the table"), bytes}, std::pair{std::string_view("recovery"), broken}}) {
		SCOPED_TRACE(name);
		const auto path = written(dir, std::string(name) + ".pdf", file);
		struct Parsing {
			std::atomic<bool> started{false};
			static void hook(CancelSite site, void* context) noexcept
			{
				if (site == CancelSite::XrefParse) {
					static_cast<Parsing*>(context)->started.store(true);
				}
			}
		} parsing;
		ActorOptions options;
		options.poll_hook = &Parsing::hook;
		options.poll_hook_context = &parsing;
		const auto handle = started(options);
		ASSERT_NE(handle, nullptr);
		auto opening = handle->open(path);
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (!parsing.started.load() && std::chrono::steady_clock::now() < until) {
			std::this_thread::yield();
		}
		ASSERT_TRUE(parsing.started.load()) << "the parse was never seen";
		const auto closing = std::chrono::steady_clock::now();
		const auto closed = handle->close();
		closed.wait();
		recto::test::expect_cancel_latency(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - closing).count(), "closing during an open");
		expect_code(std::move(opening), ErrorCode::Cancelled);
		EXPECT_EQ(handle->state(), DocumentState::Closed);
	}
}

TEST(Actor, QueueBounded)
{
	recto::test::TempDir dir;
	Gate gate;
	ActorOptions options = held_by(gate);
	options.max_queue = 16u;
	const auto handle = started(options);
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(written(dir, "doc.pdf", pdf_of(1u, "0 0 10 10 re f")))).has_value());
	auto held = handle->get_display_list(0u);
	gate.wait_reached();
	std::vector<ActorFuture<pdf::DocumentDescription>> taken;
	for (size_t i = 0; i < options.max_queue; ++i) {
		taken.push_back(handle->get_description());
	}
	pdf::clear_diagnostics();
	const auto refused = finish(handle->get_description());
	ASSERT_FALSE(refused.has_value());
	EXPECT_EQ(refused.error().code, ErrorCode::ResourceLimit);
	EXPECT_EQ(refused.error().refusal_reason, pdf::RefusalReason::ActorQueueFull);
	EXPECT_EQ(refused.error().bound, options.max_queue);
	gate.release();
	EXPECT_TRUE(finish(std::move(held)).has_value());
	for (auto& future : taken) {
		EXPECT_TRUE(finish(std::move(future)).has_value()) << "a request before the limit was lost";
	}
	EXPECT_TRUE(finish(handle->get_description()).has_value()) << "room again";
}

TEST(Actor, CloseRaces)
{
	recto::test::TempDir dir;
	const auto path = written(dir, "doc.pdf", pdf_of(4u, "0 0 10 10 re f"));
	// With the queue full.
	{
		Gate gate;
		ActorOptions options = held_by(gate);
		options.max_queue = 4u;
		const auto handle = started(options);
		ASSERT_NE(handle, nullptr);
		ASSERT_TRUE(finish(handle->open(path)).has_value());
		auto held = handle->get_display_list(0u);
		gate.wait_reached();
		std::vector<ActorFuture<pdf::DocumentDescription>> taken;
		for (size_t i = 0; i < options.max_queue; ++i) {
			taken.push_back(handle->get_description());
		}
		const auto started_close = std::chrono::steady_clock::now();
		const auto closed = handle->close();
		const double close_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_close).count();
		if (recto::test::cancel_latency_held()) {
			EXPECT_LT(close_ms, 50.0) << "close() waited";
		}
		// Three times over: the same future.
		const auto again = handle->close();
		const auto third = handle->close();
		gate.release();
		closed.wait();
		again.wait();
		third.wait();
		for (auto& future : taken) {
			expect_code(std::move(future), ErrorCode::Cancelled);
		}
		(void)finish(std::move(held));
	}
	// Four producers racing it, many times: every future answered.
	for (int round = 0; round < 20; ++round) {
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		ASSERT_TRUE(finish(handle->open(path)).has_value());
		std::vector<std::thread> producers;
		std::atomic<int> answered{0};
		std::atomic<int> sent{0};
		for (int p = 0; p < 4; ++p) {
			producers.emplace_back([&] {
				for (int i = 0; i < 50; ++i) {
					auto list = handle->get_display_list(static_cast<size_t>(i % 4));
					sent.fetch_add(1);
					ASSERT_TRUE(list.valid());
					ASSERT_EQ(list.wait_for(std::chrono::seconds(30)), std::future_status::ready) << "an answer never came";
					const auto result = list.get();
					if (!result.has_value()) {
						EXPECT_TRUE(result.error().code == ErrorCode::Cancelled) << static_cast<int>(result.error().code);
					}
					answered.fetch_add(1);
				}
			});
		}
		std::this_thread::yield();
		(void)handle->close();
		for (std::thread& producer : producers) {
			producer.join();
		}
		EXPECT_EQ(answered.load(), sent.load());
	}
	// Destroyed with requests outstanding: the destructor joins, and every future still answers.
	{
		Gate gate;
		std::vector<ActorFuture<pdf::DocumentDescription>> outstanding;
		std::shared_future<void> closed;
		{
			auto handle = started(held_by(gate));
			ASSERT_NE(handle, nullptr);
			ASSERT_TRUE(finish(handle->open(path)).has_value());
			auto held = handle->get_display_list(0u);
			gate.wait_reached();
			for (int i = 0; i < 20; ++i) {
				outstanding.push_back(handle->get_description());
			}
			closed = handle->close();
			gate.release();
			(void)finish(std::move(held));
		}
		closed.wait();
		for (auto& future : outstanding) {
			expect_code(std::move(future), ErrorCode::Cancelled);
		}
	}
	EXPECT_EQ(debug_live_documents(), 0);
}

TEST(Actor, CancelledRequests)
{
	recto::test::TempDir dir;
	const auto path = written(dir, "doc.pdf", pdf_of(2u, "0 0 10 10 re f"));
	// A queued request, cancelled before its turn.
	{
		Gate gate;
		const auto handle = started(held_by(gate));
		ASSERT_NE(handle, nullptr);
		ASSERT_TRUE(finish(handle->open(path)).has_value());
		auto held = handle->get_display_list(0u);
		gate.wait_reached();
		auto source = CancelSource::create();
		ASSERT_TRUE(source.has_value());
		auto queued = handle->get_display_list(1u, source->token());
		source->cancel();
		gate.release();
		expect_code(std::move(queued), ErrorCode::Cancelled);
		EXPECT_EQ(handle->stats().cache_misses, 1u) << "only the held page compiled";
		EXPECT_TRUE(finish(std::move(held)).has_value());
	}
	// An open, cancelled while it reads the file.
	{
		auto source = CancelSource::create();
		ASSERT_TRUE(source.has_value());
		struct Cancel {
			pdf::CancelSource* source;
			static void hook(CancelSite site, void* context) noexcept
			{
				if (site == CancelSite::FileRead) {
					static_cast<Cancel*>(context)->source->cancel();
				}
			}
		} cancel{&*source};
		ActorOptions options;
		options.poll_hook = &Cancel::hook;
		options.poll_hook_context = &cancel;
		const auto handle = started(options);
		ASSERT_NE(handle, nullptr);
		expect_code(handle->open(path, source->token()), ErrorCode::Cancelled);
		EXPECT_EQ(handle->state(), DocumentState::Failed);
		// And it can open again, uncancelled.
		EXPECT_TRUE(finish(handle->open(path)).has_value());
	}
	// A save, before its commit point: the destination unchanged. And after it: saved, whatever the token says.
	{
		const auto handle = started();
		ASSERT_NE(handle, nullptr);
		ASSERT_TRUE(finish(handle->open(path)).has_value());
		const auto first_free = finish(handle->get_first_free_object());
		ASSERT_TRUE(first_free.has_value());
		pdf::ObjectChangeSet changes(pdf::RevisionId{0u}, *first_free);
		ASSERT_TRUE(changes.allocate(pdf::PdfValue(int64_t{42})).has_value());
		const auto destination = dir.file_path("saved.pdf");
		const std::vector<uint8_t> before{1u, 2u, 3u};
		recto::test::write_file_bytes(destination, before);
		auto source = CancelSource::create();
		ASSERT_TRUE(source.has_value());
		source->cancel();
		expect_code(handle->save_incremental(changes, destination, source->token()), ErrorCode::Cancelled);
		EXPECT_EQ(recto::test::read_file_bytes(destination), before);
		// A token cancelled after the write finishes has no way in: the file stays saved.
		auto late = CancelSource::create();
		ASSERT_TRUE(late.has_value());
		auto saved = handle->save_incremental(changes, destination, late->token());
		EXPECT_TRUE(finish(std::move(saved)).has_value());
		late->cancel();
		auto reopened = Document::open(destination);
		EXPECT_TRUE(reopened.has_value());
		EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir.path()), std::filesystem::directory_iterator()), 2) << "a temporary file was left";
	}
}

TEST(Actor, AsyncFailures)
{
	recto::test::TempDir dir;
	const auto path = written(dir, "doc.pdf", pdf_of(1u, "0 0 10 10 re f"));
	{
		const detail::ScopedActorFailureForTesting failure(detail::ActorFailure::ThreadStart);
		const auto handle = DocumentHandle::start();
		ASSERT_FALSE(handle.has_value());
		EXPECT_EQ(handle.error().code, ErrorCode::InternalError);
	}
	EXPECT_EQ(debug_live_documents(), 0);
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(path)).has_value());
	{
		const detail::ScopedActorFailureForTesting failure(detail::ActorFailure::QueueInsert);
		expect_code(handle->get_description(), ErrorCode::OutOfMemory);
	}
	{
		const detail::ScopedActorFailureForTesting failure(detail::ActorFailure::ActorCopy);
		expect_code(handle->get_description(), ErrorCode::OutOfMemory);
	}
	// The actor is still answering.
	EXPECT_TRUE(finish(handle->get_description()).has_value());
	EXPECT_TRUE(finish(handle->get_display_list(0u)).has_value());
}

TEST(Actor, SaveIncremental)
{
	recto::test::TempDir dir;
	const auto original = pdf_of(1u, "0 0 10 10 re f");
	const auto source_path = written(dir, "source.pdf", original);
	const auto handle = started();
	ASSERT_NE(handle, nullptr);
	ASSERT_TRUE(finish(handle->open(source_path)).has_value());
	const auto first_free = finish(handle->get_first_free_object());
	ASSERT_TRUE(first_free.has_value());
	pdf::ObjectChangeSet changes(pdf::RevisionId{0u}, *first_free);
	const auto added = changes.allocate(pdf::PdfValue(int64_t{4242}));
	ASSERT_TRUE(added.has_value());
	const auto check_saved = [&](const std::filesystem::path& path) {
		const auto bytes = recto::test::read_file_bytes(path);
		ASSERT_GT(bytes.size(), original.size());
		EXPECT_TRUE(std::equal(original.begin(), original.end(), bytes.begin())) << "the original revision is its prefix";
		auto reopened = Document::open(path);
		ASSERT_TRUE(reopened.has_value());
		const auto value = (*reopened)->resolve_object(*added);
		ASSERT_TRUE(value.has_value());
		EXPECT_EQ(value->as_int(), std::optional<int64_t>(4242));
	};
	// To a new path, and over an existing one.
	const auto fresh = dir.file_path("new.pdf");
	ASSERT_TRUE(finish(handle->save_incremental(changes, fresh)).has_value());
	check_saved(fresh);
	const auto existing = written(dir, "existing.pdf", {9u, 9u, 9u});
	ASSERT_TRUE(finish(handle->save_incremental(changes, existing)).has_value());
	check_saved(existing);
	// The source and the open document did not move.
	EXPECT_EQ(recto::test::read_file_bytes(source_path), original);
	// A folder that is not there; a read-only destination, unchanged.
	EXPECT_FALSE(finish(handle->save_incremental(changes, dir.file_path("no/such/folder/out.pdf"))).has_value());
	const auto locked = written(dir, "readonly.pdf", {7u, 7u});
	ASSERT_NE(::SetFileAttributesW(locked.c_str(), FILE_ATTRIBUTE_READONLY), FALSE);
	const auto refused = finish(handle->save_incremental(changes, locked));
	EXPECT_FALSE(refused.has_value());
	if (!refused.has_value()) {
		EXPECT_EQ(refused.error().code, ErrorCode::IoError);
	}
	EXPECT_EQ(recto::test::read_file_bytes(locked), (std::vector<uint8_t>{7u, 7u}));
	::SetFileAttributesW(locked.c_str(), FILE_ATTRIBUTE_NORMAL);
	for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
		EXPECT_EQ(entry.path().extension(), ".pdf") << entry.path() << " was left behind";
	}
	// A recovered base is the writer's own refusal.
	const auto recovered = started();
	ASSERT_NE(recovered, nullptr);
	ASSERT_TRUE(finish(recovered->open(recto::test::corpus_path("broken/corrupt-xref-recoverable.pdf"))).has_value());
	pdf::ObjectChangeSet none(pdf::RevisionId{0u}, 1000u);
	expect_code(recovered->save_incremental(none, dir.file_path("recovered.pdf")), ErrorCode::DamagedFormat);
}

TEST(AtomicWrite, PublicationStates)
{
	using detail::AtomicWriteStage;
	using detail::ScopedAtomicWriteFailureForTesting;
	recto::test::TempDir dir;
	const std::vector<uint8_t> before{1u, 2u, 3u};
	const std::vector<uint8_t> after{4u, 5u, 6u, 7u};
	const auto target = dir.file_path("target.pdf");
	const auto temporaries = [&] {
		size_t count = 0u;
		for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
			count += entry.path().extension() == ".tmp" ? 1u : 0u;
		}
		return count;
	};
	const auto reset = [&] {
		std::error_code ignored;
		std::filesystem::remove(target, ignored);
		recto::test::write_file_bytes(target, before);
	};
	reset();
	// Before the replace, whatever fails: the destination unchanged, no temporary file.
	for (const auto& [stage, error, code] : {
			 std::tuple{AtomicWriteStage::Write, uint32_t{ERROR_DISK_FULL}, ErrorCode::IoError},
			 std::tuple{AtomicWriteStage::Flush, uint32_t{ERROR_WRITE_FAULT}, ErrorCode::IoError},
			 std::tuple{AtomicWriteStage::ReadBack, uint32_t{ERROR_FILE_CORRUPT}, ErrorCode::IoError},
			 std::tuple{AtomicWriteStage::Write, uint32_t{ERROR_NOT_ENOUGH_MEMORY}, ErrorCode::OutOfMemory},
		 }) {
		SCOPED_TRACE(static_cast<int>(stage));
		const ScopedAtomicWriteFailureForTesting failure(stage, error);
		const auto done = atomic_write_file(target, after);
		ASSERT_FALSE(done.has_value());
		EXPECT_EQ(done.error().code, code);
		EXPECT_EQ(recto::test::read_file_bytes(target), before);
		EXPECT_EQ(temporaries(), 0u);
	}
	// A replace that fails with the destination still there.
	{
		const ScopedAtomicWriteFailureForTesting failure(AtomicWriteStage::Replace, ERROR_UNABLE_TO_MOVE_REPLACEMENT);
		EXPECT_FALSE(atomic_write_file(target, after).has_value());
		EXPECT_EQ(recto::test::read_file_bytes(target), before);
		EXPECT_EQ(temporaries(), 0u);
	}
	// A temporary file changed on disk before its read-back, by a byte and keeping its size: the real comparison refuses it.
	{
		struct Tamper {
			static void hook(AtomicWriteStage stage, const std::filesystem::path& temporary, void*) noexcept
			{
				if (stage == AtomicWriteStage::ReadBack) {
					const std::vector<uint8_t> other{4u, 5u, 6u, 8u};
					recto::test::write_file_bytes(temporary, other);
				}
			}
		};
		const detail::ScopedAtomicWriteHookForTesting hook(&Tamper::hook, nullptr);
		const auto done = atomic_write_file(target, after);
		ASSERT_FALSE(done.has_value());
		EXPECT_EQ(done.error().code, ErrorCode::IoError);
		EXPECT_EQ(recto::test::read_file_bytes(target), before);
		EXPECT_EQ(temporaries(), 0u);
	}
	// Cancelled after the read-back, at the last check before the commit point: nothing changed.
	{
		auto source = pdf::CancelSource::create();
		ASSERT_TRUE(source.has_value());
		struct Late {
			static void hook(AtomicWriteStage stage, const std::filesystem::path&, void* context) noexcept
			{
				if (stage == AtomicWriteStage::Replace) {
					static_cast<pdf::CancelSource*>(context)->cancel();
				}
			}
		};
		const detail::ScopedAtomicWriteHookForTesting hook(&Late::hook, &*source);
		const auto done = atomic_write_file(target, after, source->token());
		ASSERT_FALSE(done.has_value());
		EXPECT_EQ(done.error().code, ErrorCode::Cancelled);
		EXPECT_EQ(recto::test::read_file_bytes(target), before);
		EXPECT_EQ(temporaries(), 0u);
	}
	// A move to a path that had no file fails: there is nothing to lose, so the temporary file goes too.
	{
		const auto absent = dir.file_path("absent.pdf");
		const ScopedAtomicWriteFailureForTesting failure(AtomicWriteStage::Replace, ERROR_ACCESS_DENIED);
		const auto done = atomic_write_file(absent, after);
		ASSERT_FALSE(done.has_value());
		EXPECT_EQ(done.error().code, ErrorCode::IoError);
		EXPECT_EQ(std::string(done.error().message.view()).find("is gone"), std::string::npos) << done.error().message.view();
		EXPECT_FALSE(std::filesystem::exists(absent));
		EXPECT_EQ(temporaries(), 0u) << "a temporary file was kept although no file had been there";
	}
	// A replace after which the destination is gone: the temporary file is the only copy, kept and named. The destination
	// is moved aside on disk at the commit point, as `ReplaceFileW` leaves it with this error.
	{
		struct Aside {
			static void hook(AtomicWriteStage stage, const std::filesystem::path&, void* context) noexcept
			{
				if (stage == AtomicWriteStage::Replace) {
					const auto& from = *static_cast<const std::filesystem::path*>(context);
					::MoveFileW(from.c_str(), (from.wstring() + L".moved").c_str());
				}
			}
		};
		const detail::ScopedAtomicWriteHookForTesting hook(&Aside::hook, const_cast<std::filesystem::path*>(&target));
		const ScopedAtomicWriteFailureForTesting failure(AtomicWriteStage::Replace, ERROR_UNABLE_TO_MOVE_REPLACEMENT_2);
		const auto done = atomic_write_file(target, after);
		ASSERT_FALSE(done.has_value());
		EXPECT_FALSE(std::filesystem::exists(target));
		ASSERT_EQ(temporaries(), 1u) << "the only copy was deleted";
		for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
			if (entry.path().extension() == ".tmp") {
				EXPECT_NE(std::string(done.error().message.view()).find(entry.path().filename().string()), std::string::npos) << "the error does not name it";
				EXPECT_EQ(recto::test::read_file_bytes(entry.path()), after);
				std::filesystem::remove(entry.path());
			}
		}
		std::filesystem::remove(dir.file_path("target.pdf.moved"));
		reset();
	}
	// A real sharing violation: refused after its retries, unchanged, and fine once released.
	{
		const HANDLE held = ::CreateFileW(target.c_str(), GENERIC_READ, 0u, nullptr, OPEN_EXISTING, 0u, nullptr);
		ASSERT_NE(held, INVALID_HANDLE_VALUE);
		EXPECT_FALSE(atomic_write_file(target, after).has_value());
		::CloseHandle(held);
		EXPECT_EQ(recto::test::read_file_bytes(target), before);
		EXPECT_EQ(temporaries(), 0u);
		// Released during the retries: a scanner holding the file briefly costs nothing.
		const HANDLE brief = ::CreateFileW(target.c_str(), GENERIC_READ, 0u, nullptr, OPEN_EXISTING, 0u, nullptr);
		ASSERT_NE(brief, INVALID_HANDLE_VALUE);
		std::thread releaser([brief] {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			::CloseHandle(brief);
		});
		EXPECT_TRUE(atomic_write_file(target, after).has_value());
		releaser.join();
		EXPECT_EQ(recto::test::read_file_bytes(target), after);
		reset();
	}
	// A failed read-back after publication: said plainly, naming the destination.
	{
		const ScopedAtomicWriteFailureForTesting failure(AtomicWriteStage::Verify, ERROR_FILE_CORRUPT);
		const auto done = atomic_write_file(target, after);
		ASSERT_FALSE(done.has_value());
		EXPECT_NE(std::string(done.error().message.view()).find("target.pdf"), std::string::npos);
		EXPECT_EQ(temporaries(), 0u);
		reset();
	}
	// Cancelled before the commit point: nothing changed.
	{
		auto source = pdf::CancelSource::create();
		ASSERT_TRUE(source.has_value());
		source->cancel();
		const auto done = atomic_write_file(target, after, source->token());
		ASSERT_FALSE(done.has_value());
		EXPECT_EQ(done.error().code, ErrorCode::Cancelled);
		EXPECT_EQ(recto::test::read_file_bytes(target), before);
		EXPECT_EQ(temporaries(), 0u);
	}
	// Nothing wrong: a new file and an existing one, each holding what was asked and normal attributes.
	EXPECT_TRUE(atomic_write_file(target, after).has_value());
	EXPECT_EQ(recto::test::read_file_bytes(target), after);
	const auto fresh = dir.file_path("fresh.pdf");
	EXPECT_TRUE(atomic_write_file(fresh, after).has_value());
	EXPECT_EQ(recto::test::read_file_bytes(fresh), after);
	const DWORD attributes = ::GetFileAttributesW(fresh.c_str());
	EXPECT_EQ(attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY), 0u) << "a saved file kept the temporary file's attributes";
	EXPECT_EQ(temporaries(), 0u);
}

} // namespace
} // namespace recto::core
