// Staged revisions (D01 T02 section 13): change sets, push, undo and redo with revision ids, the base prefix,
// compaction past the reader's revision limit, the squashed export of exactly the current state, stream and encrypted
// bases, the bounds, and all-or-nothing transitions.

#include "document.h"
#include "incremental_writer.h"
#include "parser.h"
#include "serialize.h"
#include "staged_revisions.h"
#include "test_support.h"
#include "xref.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

using Bytes = std::vector<uint8_t>;

[[nodiscard]] Bytes corpus(std::string_view rel)
{
	return ::recto::test::read_file_bytes(::recto::test::corpus_path(rel));
}

[[nodiscard]] Bytes bytes_of(std::string_view text)
{
	return Bytes(text.begin(), text.end());
}

[[nodiscard]] std::string text_of(std::span<const uint8_t> bytes)
{
	return std::string(bytes.begin(), bytes.end());
}

[[nodiscard]] PdfDict dict_of(std::string_view literal)
{
	const Bytes bytes = bytes_of(literal);
	Parser parser(bytes);
	auto parsed = parser.parse_dictionary();
	EXPECT_TRUE(parsed.has_value()) << literal;
	return parsed.has_value() ? std::move(*parsed) : PdfDict{};
}

[[nodiscard]] std::unique_ptr<StagedRevisionChain> make_chain(std::span<const uint8_t> base,
	std::string_view password = {}, uint64_t base_id = 100u, uint64_t first_id = 200u)
{
	auto chain = StagedRevisionChain::create(base, password, RevisionId{base_id}, RevisionId{first_id});
	EXPECT_TRUE(chain.has_value()) << (chain.has_value() ? "" : std::string(chain.error().message.view()));
	return chain.has_value() ? std::move(*chain) : nullptr;
}

[[nodiscard]] int32_t rotation(const Document& doc)
{
	auto page = doc.page_description(0u);
	EXPECT_TRUE(page.has_value());
	return page.has_value() ? page->rotation : -1;
}

// Page 1 of `render/simple-1p.pdf` (object 3) at `rotate` degrees.
[[nodiscard]] PdfValue page_at(const Document& doc, int rotate)
{
	auto page = doc.resolve_object(IndirectRef(3u));
	EXPECT_TRUE(page.has_value());
	PdfDict dict = page.has_value() && page->as_dict() != nullptr ? *page->as_dict() : PdfDict{};
	dict.set("Rotate", PdfValue(static_cast<int64_t>(rotate)));
	return PdfValue(std::move(dict));
}

[[nodiscard]] std::string title_of(const Document& doc, IndirectRef ref)
{
	auto value = doc.resolve_object(ref);
	if (!value.has_value() || value->as_dict() == nullptr) {
		return "<none>";
	}
	const PdfString* s = value->as_dict()->get_string("Title");
	return s != nullptr ? std::string(s->as_byte_view()) : "<none>";
}

[[nodiscard]] bool strict_ok(std::span<const uint8_t> bytes, size_t* revisions = nullptr)
{
	auto table = XrefTable::parse(bytes, kMaxXrefTotalEntries, /*allow_recovery=*/false);
	if (table.has_value() && revisions != nullptr) {
		*revisions = table->revision_count();
	}
	return table.has_value() && !table->recovered();
}

// Pushes one edit: page 1 rotated to `rotate`, and a new `<< /Title (label) >>` object, returned.
[[nodiscard]] IndirectRef push_edit(StagedRevisionChain& chain, int rotate, std::string_view label)
{
	ObjectChangeSet changes = chain.begin_change();
	EXPECT_TRUE(changes.replace(IndirectRef(3u), page_at(chain.working(), rotate)).has_value());
	auto added = changes.allocate(PdfValue(dict_of("<< /Title (" + std::string(label) + ") >>")));
	EXPECT_TRUE(added.has_value());
	auto pushed = chain.push(std::move(changes));
	EXPECT_TRUE(pushed.has_value()) << (pushed.has_value() ? "" : std::string(pushed.error().message.view()));
	return added.has_value() ? *added : IndirectRef{};
}

TEST(PdfStagedRevisions, MergeOrder)
{
	ObjectChangeSet first(RevisionId{1u}, 10u);
	RECTO_ASSERT_OK(first.replace(IndirectRef(3u), PdfValue(int64_t{1})));
	auto a = first.allocate(PdfValue(int64_t{10}));
	RECTO_ASSERT_OK(a);
	ObjectChangeSet later(RevisionId{2u}, 11u);
	RECTO_ASSERT_OK(later.replace(IndirectRef(3u), PdfValue(int64_t{2})));
	RECTO_ASSERT_OK(later.replace(*a, PdfValue(int64_t{20}))); // replacing what the earlier set allocated
	RECTO_ASSERT_OK(later.replace(IndirectRef(4u), PdfValue(int64_t{4})));
	auto b = later.allocate(PdfValue(int64_t{11}));
	RECTO_ASSERT_OK(b);
	RECTO_ASSERT_OK(first.merge(later));
	EXPECT_EQ(first.built_on(), RevisionId{1u});
	ASSERT_EQ(first.entries().size(), 4u);
	EXPECT_EQ(first.entries().at(3u).value.as_int(), 2); // the later value wins
	EXPECT_EQ(first.entries().at(4u).value.as_int(), 4);
	EXPECT_TRUE(first.entries().at(a->obj_num).allocated); // still allocated, though the later set only replaced it
	EXPECT_EQ(first.entries().at(a->obj_num).value.as_int(), 20);
	EXPECT_TRUE(first.entries().at(b->obj_num).allocated);
	EXPECT_EQ(first.next_free(), 12u);
	// Replacing an allocated object keeps it allocated; a number past the first free one is allocated, not replaced.
	ObjectChangeSet set(RevisionId{1u}, 10u);
	auto c = set.allocate(PdfValue(int64_t{0}));
	RECTO_ASSERT_OK(c);
	RECTO_ASSERT_OK(set.replace(*c, PdfValue(int64_t{5})));
	EXPECT_TRUE(set.entries().at(c->obj_num).allocated);
	RECTO_EXPECT_ERR(set.replace(IndirectRef(11u), PdfValue{}), ErrorCode::InvalidArgument);
	RECTO_EXPECT_ERR(set.replace(IndirectRef(0u), PdfValue{}), ErrorCode::InvalidArgument);
	RECTO_EXPECT_ERR(set.replace(IndirectRef(c->obj_num, 1u), PdfValue{}), ErrorCode::InvalidArgument);
}

TEST(PdfStagedRevisions, PushPopRedo)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	ASSERT_EQ(rotation(chain->working()), 0);
	ObjectChangeSet changes = chain->begin_change();
	RECTO_ASSERT_OK(changes.replace(IndirectRef(3u), page_at(chain->working(), 90)));
	RECTO_ASSERT_OK(chain->push(std::move(changes)));
	EXPECT_EQ(rotation(chain->working()), 90);
	EXPECT_EQ(chain->depth(), 1u);
	RECTO_ASSERT_OK(chain->undo());
	EXPECT_EQ(rotation(chain->working()), 0);
	EXPECT_EQ(chain->working().file_bytes().size(), base.size()); // truncated, not rewritten
	RECTO_EXPECT_ERR(chain->undo(), ErrorCode::InvalidArgument);
	RECTO_ASSERT_OK(chain->redo());
	EXPECT_EQ(rotation(chain->working()), 90);
	RECTO_ASSERT_OK(chain->undo());
	ObjectChangeSet other = chain->begin_change();
	RECTO_ASSERT_OK(other.replace(IndirectRef(3u), page_at(chain->working(), 180)));
	RECTO_ASSERT_OK(chain->push(std::move(other)));
	EXPECT_EQ(rotation(chain->working()), 180);
	RECTO_EXPECT_ERR(chain->redo(), ErrorCode::InvalidArgument); // a new push cleared the redo stack
	EXPECT_EQ(chain->redo_depth(), 0u);
}

TEST(PdfStagedRevisions, StaleChangeSet)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	const IndirectRef first = push_edit(*chain, 90, "one");
	ObjectChangeSet stale = chain->begin_change(); // built on the first push
	RECTO_ASSERT_OK(stale.allocate(PdfValue(int64_t{1})));
	RECTO_ASSERT_OK(chain->undo());
	const auto bytes = text_of(chain->working().file_bytes());
	RECTO_EXPECT_ERR(chain->push(std::move(stale)), ErrorCode::InvalidArgument);
	EXPECT_EQ(text_of(chain->working().file_bytes()), bytes);
	EXPECT_EQ(chain->current_revision(), RevisionId{100u});
	EXPECT_EQ(chain->redo_depth(), 1u);
	RECTO_EXPECT_ERR(chain->push(chain->begin_change()), ErrorCode::InvalidArgument); // an empty change
	RECTO_ASSERT_OK(chain->redo());
	EXPECT_EQ(title_of(chain->working(), first), "one");
}

TEST(PdfStagedRevisions, FailedPushKeepsState)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	static_cast<void>(push_edit(*chain, 90, "kept"));
	const auto bytes = text_of(chain->working().file_bytes());
	const RevisionId id = chain->current_revision();
	ObjectChangeSet bad = chain->begin_change();
	RECTO_ASSERT_OK(bad.allocate(PdfValue(std::numeric_limits<double>::infinity())));
	RECTO_EXPECT_ERR(chain->push(std::move(bad)), ErrorCode::InvalidArgument);
	EXPECT_EQ(text_of(chain->working().file_bytes()), bytes);
	EXPECT_EQ(chain->current_revision(), id);
	EXPECT_EQ(chain->depth(), 1u);
	// A base the writer cannot extend is refused at creation with the writer's error.
	std::string damaged = text_of(base);
	damaged.replace(damaged.rfind("startxref\n786") + 10u, 3u, "999");
	RECTO_EXPECT_ERR(StagedRevisionChain::create(bytes_of(damaged), {}, RevisionId{1u}, RevisionId{2u}),
		ErrorCode::DamagedFormat);
}

TEST(PdfStagedRevisions, RevisionIds)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base, {}, 7u, 50u);
	ASSERT_NE(chain, nullptr);
	EXPECT_EQ(chain->current_revision(), RevisionId{7u});
	static_cast<void>(push_edit(*chain, 90, "a"));
	const RevisionId a = chain->current_revision();
	static_cast<void>(push_edit(*chain, 180, "b"));
	const RevisionId b = chain->current_revision();
	RECTO_ASSERT_OK(chain->undo());
	EXPECT_EQ(chain->current_revision(), a);
	static_cast<void>(push_edit(*chain, 270, "c"));
	const RevisionId c = chain->current_revision();
	EXPECT_EQ(a, RevisionId{50u});
	EXPECT_EQ(b, RevisionId{51u});
	EXPECT_GT(c, b); // never reused, the dropped redo's id included
	RECTO_ASSERT_OK(chain->undo());
	RECTO_ASSERT_OK(chain->redo());
	EXPECT_EQ(chain->current_revision(), c); // redo restores the same id
}

TEST(PdfStagedRevisions, BasePrefixPreserved)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	for (int i = 1; i <= 3; ++i) {
		static_cast<void>(push_edit(*chain, 90 * i, "p"));
	}
	RECTO_ASSERT_OK(chain->undo());
	RECTO_ASSERT_OK(chain->undo());
	RECTO_ASSERT_OK(chain->redo());
	const auto now = chain->working().file_bytes();
	ASSERT_GT(now.size(), base.size());
	EXPECT_EQ(std::memcmp(now.data(), base.data(), base.size()), 0);
	EXPECT_EQ(rotation(chain->working()), 180);
}

TEST(PdfStagedRevisions, ManyEdits)
{
	// More edits than a reader follows revisions: the chain compacts, every buffer parses strictly within the
	// limit, and undo walks all the way back.
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	constexpr int kEdits = 600;
	std::vector<IndirectRef> added;
	for (int i = 1; i <= kEdits; ++i) {
		added.push_back(push_edit(*chain, (i % 4) * 90, "edit " + std::to_string(i)));
		size_t revisions = 0;
		ASSERT_TRUE(strict_ok(chain->working().file_bytes(), &revisions)) << i;
		ASSERT_LE(revisions, kMaxXrefRevisions) << i;
	}
	EXPECT_EQ(chain->depth(), static_cast<size_t>(kEdits));
	for (int i = kEdits; i >= 1; --i) {
		if (i % 50 == 0) {
			EXPECT_EQ(rotation(chain->working()), (i % 4) * 90) << i;
			EXPECT_EQ(title_of(chain->working(), added[static_cast<size_t>(i - 1)]), "edit " + std::to_string(i)) << i;
			if (i < kEdits) {
				EXPECT_EQ(title_of(chain->working(), added[static_cast<size_t>(i)]), "<none>") << i; // undone
			}
		}
		RECTO_ASSERT_OK(chain->undo());
		ASSERT_TRUE(strict_ok(chain->working().file_bytes())) << i;
	}
	EXPECT_EQ(chain->depth(), 0u);
	EXPECT_EQ(text_of(chain->working().file_bytes()), text_of(base));
}

// `render/simple-1p.pdf` with `extra` empty revisions appended by hand.
[[nodiscard]] Bytes with_revisions(size_t extra)
{
	std::string text = text_of(corpus("render/simple-1p.pdf"));
	uint64_t prev = 786u;
	for (size_t i = 0; i < extra; ++i) {
		const uint64_t at = text.size();
		text += "xref\n0 1\n0000000000 65535 f\r\ntrailer\n<< /Size 7 /Root 1 0 R /Prev " + std::to_string(prev) +
			" >>\nstartxref\n" + std::to_string(at) + "\n%%EOF\n";
		prev = at;
	}
	return bytes_of(text);
}

TEST(PdfStagedRevisions, NearLimitBases)
{
	for (const size_t revisions : {253u, 254u, 255u}) {
		const Bytes base = with_revisions(revisions - 1u);
		auto chain = make_chain(base);
		ASSERT_NE(chain, nullptr) << revisions;
		std::vector<IndirectRef> added;
		for (int i = 1; i <= 20; ++i) {
			added.push_back(push_edit(*chain, (i % 4) * 90, "near " + std::to_string(i)));
			size_t now = 0;
			ASSERT_TRUE(strict_ok(chain->working().file_bytes(), &now)) << revisions << " " << i;
			ASSERT_LE(now, kMaxXrefRevisions) << revisions << " " << i;
		}
		for (size_t i = 0; i < added.size(); ++i) {
			EXPECT_EQ(title_of(chain->working(), added[i]), "near " + std::to_string(i + 1u)) << revisions;
		}
		for (int i = 20; i >= 1; --i) {
			EXPECT_EQ(rotation(chain->working()), (i % 4) * 90) << revisions << " " << i;
			RECTO_ASSERT_OK(chain->undo());
		}
		EXPECT_EQ(text_of(chain->working().file_bytes()), text_of(base)) << revisions;
	}
	RECTO_EXPECT_ERR(StagedRevisionChain::create(with_revisions(255u), {}, RevisionId{1u}, RevisionId{2u}),
		ErrorCode::ResourceLimit);
}

// The export reopened: every allocated object's title and page 1's rotation, against the working document.
void expect_export_matches(const StagedRevisionChain& chain, const std::vector<IndirectRef>& allocated,
	std::string_view password = {})
{
	auto exported = chain.export_squashed();
	RECTO_ASSERT_OK(exported);
	EXPECT_EQ(exported->revision, chain.current_revision());
	ASSERT_NE(exported->bytes, nullptr);
	size_t revisions = 0;
	ASSERT_TRUE(strict_ok(*exported->bytes, &revisions));
	auto doc = password.empty() ? Document::open_memory(*exported->bytes) : Document::open_memory(*exported->bytes, password);
	RECTO_ASSERT_OK(doc);
	EXPECT_EQ(rotation(**doc), rotation(chain.working()));
	for (const IndirectRef ref : allocated) {
		EXPECT_EQ(title_of(**doc, ref), title_of(chain.working(), ref)) << ref.obj_num;
	}
	if (chain.depth() == 0u) {
		EXPECT_EQ(text_of(*exported->bytes), text_of(chain.working().file_bytes()));
	}
}

TEST(PdfStagedRevisions, SquashEquivalent)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	std::vector<IndirectRef> allocated;
	for (int i = 1; i <= 3; ++i) {
		allocated.push_back(push_edit(*chain, 90 * i, "s" + std::to_string(i)));
	}
	expect_export_matches(*chain, allocated);
	RECTO_ASSERT_OK(chain->undo()); // partial undo: s3 is not exported
	expect_export_matches(*chain, allocated);
	RECTO_ASSERT_OK(chain->undo());
	RECTO_ASSERT_OK(chain->undo()); // full undo: the base
	expect_export_matches(*chain, allocated);
	RECTO_ASSERT_OK(chain->redo());
	RECTO_ASSERT_OK(chain->redo()); // redo
	expect_export_matches(*chain, allocated);
	allocated.push_back(push_edit(*chain, 270, "branch")); // a branch: s3 discarded
	expect_export_matches(*chain, allocated);
	for (int i = 0; i < 300; ++i) { // past a compaction
		allocated.push_back(push_edit(*chain, (i % 4) * 90, "c" + std::to_string(i)));
	}
	expect_export_matches(*chain, allocated);
	for (int i = 0; i < 280; ++i) { // undo into the compacted part
		RECTO_ASSERT_OK(chain->undo());
	}
	expect_export_matches(*chain, allocated);
}

TEST(PdfStagedRevisions, CompressedAndEncrypted)
{
	for (const bool encrypted : {false, true}) {
		const Bytes base = corpus(encrypted ? "render/encrypted-aes-256.pdf" : "render/compressed-xref-2p.pdf");
		const std::string_view password = encrypted ? "recto-test" : "";
		auto chain = make_chain(base, password);
		ASSERT_NE(chain, nullptr) << encrypted;
		auto info = chain->working().xref().info_ref();
		ASSERT_TRUE(info.has_value());
		ObjectChangeSet changes = chain->begin_change();
		RECTO_ASSERT_OK(changes.replace(*info, PdfValue(dict_of("<< /Title (Staged secret title) >>"))));
		auto added = changes.allocate(PdfValue(dict_of("<< /Title (Staged new object) >>")));
		RECTO_ASSERT_OK(added);
		RECTO_ASSERT_OK(chain->push(std::move(changes)));
		const auto check = [&](bool applied) {
			EXPECT_EQ(title_of(chain->working(), *info) == "Staged secret title", applied) << encrypted;
			EXPECT_EQ(title_of(chain->working(), *added), applied ? "Staged new object" : "<none>") << encrypted;
			if (encrypted) {
				EXPECT_EQ(text_of(chain->working().file_bytes()).find("Staged secret title"), std::string::npos);
			}
		};
		check(true);
		RECTO_ASSERT_OK(chain->undo());
		check(false);
		RECTO_ASSERT_OK(chain->redo());
		check(true);
		expect_export_matches(*chain, {*added, *info}, password);
		auto exported = chain->export_squashed();
		RECTO_ASSERT_OK(exported);
		if (encrypted) {
			EXPECT_EQ(text_of(*exported->bytes).find("Staged secret title"), std::string::npos);
		}
	}
}

TEST(PdfStagedRevisions, Bounds)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	{
		// Objects per change set.
		ObjectChangeSet set(RevisionId{1u}, 7u);
		for (size_t i = 0; i < kMaxChangeSetObjects; ++i) {
			ASSERT_TRUE(set.allocate(PdfValue(int64_t{0})).has_value()) << i;
		}
		RECTO_EXPECT_ERR(set.allocate(PdfValue(int64_t{0})), ErrorCode::ResourceLimit);
		RECTO_EXPECT_ERR(set.replace(IndirectRef(3u), PdfValue{}), ErrorCode::ResourceLimit);
		EXPECT_EQ(set.entries().size(), kMaxChangeSetObjects);
	}
	{
		// History: a large compressible stream replaced again and again, across a compaction and an undo.
		detail::ScopedRetainedHistoryLimit limit(24u * 1024u * 1024u); // about 384 edits of 64 KiB
		auto chain = make_chain(base);
		ASSERT_NE(chain, nullptr);
		const IndirectRef content(4u);
		size_t pushed = 0;
		std::optional<ErrorCode> refused;
		for (int i = 0; i < 500; ++i) {
			ObjectChangeSet changes = chain->begin_change();
			Bytes data(64u * 1024u, static_cast<uint8_t>('A' + i % 26));
			EXPECT_TRUE(changes.replace(content, PdfValue(PdfStream(PdfDict{}, std::move(data)))).has_value());
			const auto bytes = text_of(chain->working().file_bytes());
			const RevisionId id = chain->current_revision();
			auto result = chain->push(std::move(changes));
			if (!result.has_value()) {
				refused = result.error().code;
				EXPECT_EQ(text_of(chain->working().file_bytes()), bytes);
				EXPECT_EQ(chain->current_revision(), id);
				break;
			}
			++pushed;
			if (pushed == 300u) {
				RECTO_ASSERT_OK(chain->undo());
				RECTO_ASSERT_OK(chain->redo());
			}
		}
		EXPECT_EQ(refused, std::optional<ErrorCode>(ErrorCode::ResourceLimit));
		EXPECT_GT(pushed, 255u); // past a compaction before the limit
		EXPECT_LE(chain->retained_bytes(), 24u * 1024u * 1024u);
	}
	{
		// Incompressible streams: a push whose changes fit but whose written revision would not is refused too, so
		// the history never passes its limit; one whose changes alone pass it is refused before anything is written.
		auto chain = make_chain(base);
		ASSERT_NE(chain, nullptr);
		uint32_t seed = 99u;
		const auto noise = [&seed](size_t n) {
			Bytes data(n);
			for (uint8_t& b : data) {
				seed = seed * 1'103'515'245u + 12'345u;
				b = static_cast<uint8_t>(seed >> 24u);
			}
			return data;
		};
		// One push holds about three times its changes (the changes, the stored revision and the working bytes), so
		// a limit of five and a half pushes lets the sixth pass the check on its changes and fail the one on its
		// written revision.
		{
			ObjectChangeSet first = chain->begin_change();
			RECTO_ASSERT_OK(first.replace(IndirectRef(4u), PdfValue(PdfStream(PdfDict{}, noise(256u * 1024u)))));
			RECTO_ASSERT_OK(chain->push(std::move(first)));
		}
		const uint64_t per_push = chain->retained_bytes();
		const uint64_t kLimit = per_push * 5u + per_push / 2u;
		detail::ScopedRetainedHistoryLimit limit(kLimit);
		bool refused = false;
		int pushes = 1;
		for (int i = 0; i < 40 && !refused; ++i) {
			ObjectChangeSet changes = chain->begin_change();
			EXPECT_TRUE(changes.replace(IndirectRef(4u), PdfValue(PdfStream(PdfDict{}, noise(256u * 1024u)))).has_value());
			const RevisionId id = chain->current_revision();
			auto result = chain->push(std::move(changes));
			refused = !result.has_value();
			pushes += refused ? 0 : 1;
			if (refused) {
				EXPECT_EQ(result.error().code, ErrorCode::ResourceLimit);
				EXPECT_EQ(chain->current_revision(), id);
			}
			EXPECT_LE(chain->retained_bytes(), kLimit) << i;
		}
		EXPECT_TRUE(refused);
		EXPECT_EQ(pushes, 5); // the sixth was refused on its written revision
		ObjectChangeSet huge = chain->begin_change();
		RECTO_ASSERT_OK(huge.replace(IndirectRef(4u), PdfValue(PdfStream(PdfDict{}, noise(kLimit)))));
		const size_t examined = detail::serialize_bytes_examined();
		RECTO_EXPECT_ERR(chain->push(std::move(huge)), ErrorCode::ResourceLimit);
		EXPECT_EQ(detail::serialize_bytes_examined(), examined); // refused before any write
	}
	{
		// States: the history keeps kMaxChainStates edits, then asks for a save.
		auto chain = make_chain(base);
		ASSERT_NE(chain, nullptr);
		for (size_t i = 0; i < kMaxChainStates; ++i) {
			ObjectChangeSet changes = chain->begin_change();
			ASSERT_TRUE(changes.replace(IndirectRef(6u), PdfValue(dict_of("<< /Title (n) >>"))).has_value());
			ASSERT_TRUE(chain->push(std::move(changes)).has_value()) << i;
		}
		ObjectChangeSet one_more = chain->begin_change();
		RECTO_ASSERT_OK(one_more.replace(IndirectRef(6u), PdfValue(dict_of("<< /Title (n) >>"))));
		RECTO_EXPECT_ERR(chain->push(std::move(one_more)), ErrorCode::ResourceLimit);
		EXPECT_EQ(chain->depth(), kMaxChainStates);
	}
}

TEST(PdfStagedRevisions, TransitionsAreAtomic)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto chain = make_chain(base);
	ASSERT_NE(chain, nullptr);
	struct Snapshot {
		std::string bytes;
		RevisionId id;
		size_t depth;
		size_t redo;
	};
	const auto snap = [&]() {
		return Snapshot{text_of(chain->working().file_bytes()), chain->current_revision(), chain->depth(), chain->redo_depth()};
	};
	const auto same = [&](const Snapshot& s, const char* what) {
		const Snapshot now = snap();
		EXPECT_EQ(now.bytes, s.bytes) << what;
		EXPECT_EQ(now.id, s.id) << what;
		EXPECT_EQ(now.depth, s.depth) << what;
		EXPECT_EQ(now.redo, s.redo) << what;
	};
	// A push, under a write failure and a reopen failure.
	for (int injection = 0; injection < 2; ++injection) {
		ObjectChangeSet changes = chain->begin_change();
		RECTO_ASSERT_OK(changes.replace(IndirectRef(3u), page_at(chain->working(), 90)));
		const Snapshot before = snap();
		if (injection == 0) {
			detail::ScopedSerializeAllocFailure fail(16u);
			EXPECT_FALSE(chain->push(changes).has_value());
		} else {
			detail::ScopedWorkingReopenFailure fail;
			EXPECT_FALSE(chain->push(changes).has_value());
		}
		same(before, "push");
	}
	// Enough edits to compact, so an undo below the compacted part rebuilds.
	for (int i = 0; i < 260; ++i) {
		static_cast<void>(push_edit(*chain, (i % 4) * 90, "t"));
	}
	{
		const Snapshot before = snap();
		detail::ScopedWorkingReopenFailure fail;
		EXPECT_FALSE(chain->undo().has_value()); // a truncating undo
		same(before, "truncating undo");
	}
	while (chain->depth() > 0u) {
		// Walk down until an undo rebuilds (the bytes of the next state are not a prefix of these).
		const auto now = chain->working().file_bytes().size();
		const Snapshot before = snap();
		{
			detail::ScopedSerializeAllocFailure fail(16u);
			const bool ok = chain->undo().has_value();
			if (!ok) {
				same(before, "rebuilding undo, write failure");
				detail::ScopedWorkingReopenFailure reopen_fail;
				EXPECT_FALSE(chain->undo().has_value());
				same(before, "rebuilding undo, reopen failure");
				break;
			}
		}
		ASSERT_LT(chain->working().file_bytes().size(), now);
	}
	ASSERT_GT(chain->depth(), 0u) << "no undo rebuilt";
	RECTO_ASSERT_OK(chain->undo()); // now without injection
	{
		// A redo that rewrites (its stored revision belongs to the older layout): a write failure.
		const Snapshot before = snap();
		detail::ScopedSerializeAllocFailure fail(16u);
		EXPECT_FALSE(chain->redo().has_value());
		same(before, "rewriting redo");
	}
	RECTO_ASSERT_OK(chain->redo());
	RECTO_ASSERT_OK(chain->undo());
	{
		// A redo that appends its stored bytes: only the reopen can fail.
		const Snapshot before = snap();
		detail::ScopedWorkingReopenFailure fail;
		EXPECT_FALSE(chain->redo().has_value());
		same(before, "appending redo");
	}
	RECTO_ASSERT_OK(chain->redo());
}

} // namespace
} // namespace recto::pdf
