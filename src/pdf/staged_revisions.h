#pragma once

// Staged revisions (D01 T02 section 13): the engine half of undo. An `ObjectChangeSet` lists the objects one edit
// replaces and allocates, as plain values; a `StagedRevisionChain` appends each set as one incremental revision
// (`incremental_writer.h`) to an in-memory copy of the opened bytes, reopens a working `Document` from that copy,
// undoes by truncating it (or, below a compacted part, by rebuilding it), and exports one squashed revision for the
// save path. It writes no file, and the opened bytes stay an untouched prefix of every buffer it makes, so a signature
// over them stays valid.
//
// A chain, its working document and every change set belong to the one thread that creates them (the `DocumentActor`)
// and are never shared; a change set holds no pointer into any document, and an export is immutable.

#include "document.h"
#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {

// The states a chain keeps for undo, the objects one change set may hold, and the history it retains: the working
// bytes past the base, every stored revision and every state's change set, as `staged_value_bytes` counts values.
inline constexpr size_t kMaxChainStates = 1'000u;
inline constexpr size_t kMaxChangeSetObjects = 100'000u;
inline constexpr uint64_t kMaxRetainedHistoryBytes = 256ull * 1024ull * 1024ull;

// Names one state of a chain: the base's (given by the caller) or a push's (assigned in order, never reused).
struct RevisionId {
	uint64_t value{0};

	constexpr auto operator<=>(const RevisionId&) const noexcept = default;
};

class ObjectChangeSet {
public:
	struct Entry {
		uint16_t generation{0};
		PdfValue value{};
		bool allocated{false}; // numbered by this set (or a set merged into it) rather than replacing an object
	};

	// A set for the state `built_on`, allocating from `first_free` up. `StagedRevisionChain::begin_change` makes it.
	ObjectChangeSet(RevisionId built_on, uint32_t first_free) noexcept
		: built_on_(built_on)
		, first_free_(first_free)
		, next_free_(first_free)
	{
	}

	ObjectChangeSet(const ObjectChangeSet&) = default;
	ObjectChangeSet& operator=(const ObjectChangeSet&) = default;
	ObjectChangeSet(ObjectChangeSet&&) noexcept = default;
	ObjectChangeSet& operator=(ObjectChangeSet&&) noexcept = default;

	// Replaces object `ref`: an existing object (a number below the first free one this set was made with) or one this
	// set allocated (generation 0). Object 0, a number at or past the first free one this set did not allocate, or a
	// wrong generation for an allocated one is `InvalidArgument`; past `kMaxChangeSetObjects` objects
	// `ResourceLimit`. The last value written for a number wins.
	[[nodiscard]] PdfResult<void> replace(IndirectRef ref, PdfValue value) noexcept;

	// A new object at the next number, in order from the first free one.
	[[nodiscard]] PdfResult<IndirectRef> allocate(PdfValue value) noexcept;

	// Folds in a set made after this one: its value wins for a number both hold, every allocated number of either is
	// kept as allocated, and numbering continues past the later set's. All or nothing.
	[[nodiscard]] PdfResult<void> merge(const ObjectChangeSet& later) noexcept;

	[[nodiscard]] RevisionId built_on() const noexcept
	{
		return built_on_;
	}

	[[nodiscard]] uint32_t first_free() const noexcept
	{
		return first_free_;
	}

	[[nodiscard]] uint32_t next_free() const noexcept
	{
		return next_free_;
	}

	[[nodiscard]] const std::map<uint32_t, Entry>& entries() const noexcept
	{
		return entries_;
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return entries_.empty();
	}

private:
	RevisionId built_on_{};
	uint32_t first_free_{0};
	uint32_t next_free_{0};
	std::map<uint32_t, Entry> entries_{};

	friend class StagedRevisionChain;
};

// An exported squash: the base bytes and one revision holding exactly the current state's changes.
struct ExportedRevision {
	RevisionId revision{};
	std::shared_ptr<const std::vector<uint8_t>> bytes;
};

class StagedRevisionChain {
public:
	// A chain over `base` (copied), opened with `password` (empty for none), whose base state is `base_revision` and
	// whose pushes are numbered from `first_new_revision` up. A base the writer cannot extend is refused with the
	// writer's error (a recovered cross-reference is `DamagedFormat`, so the caller saves a full copy), and so is one
	// already at `kMaxXrefRevisions` revisions (`ResourceLimit`); a wrong password is the document's error.
	[[nodiscard]] static PdfResult<std::unique_ptr<StagedRevisionChain>> create(std::span<const uint8_t> base,
		std::string_view password, RevisionId base_revision, RevisionId first_new_revision) noexcept;

	~StagedRevisionChain() noexcept;
	StagedRevisionChain(const StagedRevisionChain&) = delete;
	StagedRevisionChain& operator=(const StagedRevisionChain&) = delete;

	// A change set for the current state, numbering new objects from `next_free_object()`.
	[[nodiscard]] ObjectChangeSet begin_change() const noexcept;

	// Appends `changes` as a revision and reopens the working document; a redo stack is discarded. A set built on
	// another state, an empty set, or one the writer refuses returns its error; past `kMaxChainStates` states or
	// `kMaxRetainedHistoryBytes` of history `ResourceLimit`. Every failure leaves the chain as it was.
	[[nodiscard]] PdfResult<void> push(ObjectChangeSet changes) noexcept;

	// Back one state, and forward again; nothing to undo or redo is `InvalidArgument`. All or nothing.
	[[nodiscard]] PdfResult<void> undo() noexcept;
	[[nodiscard]] PdfResult<void> redo() noexcept;

	[[nodiscard]] const Document& working() const noexcept
	{
		return *working_;
	}

	// The first number a change set on the current state allocates.
	[[nodiscard]] uint32_t next_free_object() const noexcept;

	// States applied above the base, and states waiting to be redone.
	[[nodiscard]] size_t depth() const noexcept
	{
		return current_;
	}

	[[nodiscard]] size_t redo_depth() const noexcept
	{
		return states_.size() - current_;
	}

	[[nodiscard]] RevisionId current_revision() const noexcept;

	// The history this chain retains, as `kMaxRetainedHistoryBytes` counts it.
	[[nodiscard]] uint64_t retained_bytes() const noexcept
	{
		return retained_;
	}

	// The base bytes plus one revision holding the current state's changes merged; at the base state, the base.
	[[nodiscard]] PdfResult<ExportedRevision> export_squashed() const noexcept;

private:
	struct State {
		RevisionId id{};
		ObjectChangeSet changes;
		uint64_t change_bytes{0};     // `changes` as the history budget counts it
		std::vector<uint8_t> revision; // its own revision, when it was written as one
		uint64_t revision_prefix{0};  // the working length it was appended at
		uint64_t revision_layout{0};  // the layout it was appended in
	};

	// What a transition builds before anything changes.
	struct Candidate {
		std::unique_ptr<Document> working;
		size_t squashed{0};
		uint64_t layout{0};
		std::vector<uint64_t> ends;
		std::optional<State> written; // the state appended or redone, with the revision it was written as
	};

	StagedRevisionChain() noexcept = default;

	[[nodiscard]] PdfResult<std::unique_ptr<Document>> reopen(std::vector<uint8_t> bytes) const noexcept;
	[[nodiscard]] PdfResult<ObjectChangeSet> merged(size_t through) const noexcept;
	[[nodiscard]] PdfResult<std::vector<uint8_t>> squash_bytes(size_t through) const noexcept;
	[[nodiscard]] PdfResult<std::vector<uint8_t>> append_bytes(const Document& on, const ObjectChangeSet& changes) const noexcept;
	[[nodiscard]] PdfResult<Candidate> forward(const State& next) const noexcept;
	[[nodiscard]] size_t used_slots() const noexcept;
	[[nodiscard]] uint64_t history_without_redo() const noexcept;
	// A stored revision written in another layout can never be appended again; it is dropped, and the history
	// recounted.
	void drop_stale_revisions() noexcept;

	std::unique_ptr<Document> base_;     // the opened bytes, never changed
	std::unique_ptr<Document> working_;  // the current state
	std::string password_;
	RevisionId base_revision_{};
	uint64_t next_id_{0};
	size_t room_{0};       // revisions the reader still follows past the base's own
	std::vector<State> states_;
	size_t current_{0};    // states applied
	size_t squashed_{0};   // states held by the compacted revision right after the base (0: none)
	uint64_t layout_{0};   // changes whenever the working bytes are rebuilt
	std::vector<uint64_t> ends_; // ends_[k] for k from squashed_ to current_: the working length after state k
	uint64_t retained_{0};
};

namespace detail {

// Tests only: while alive, the `after`-th reopen of a working document on this thread (counting from 1) and every
// later one fail with `OutOfMemory`, so a transition's atomicity can be checked at its last step.
class ScopedWorkingReopenFailure {
public:
	explicit ScopedWorkingReopenFailure(int after = 1) noexcept;
	~ScopedWorkingReopenFailure() noexcept;
	ScopedWorkingReopenFailure(const ScopedWorkingReopenFailure&) = delete;
	ScopedWorkingReopenFailure& operator=(const ScopedWorkingReopenFailure&) = delete;

private:
	int previous_;
};

// Tests only: while alive, chains on this thread use `limit` in place of `kMaxRetainedHistoryBytes`.
class ScopedRetainedHistoryLimit {
public:
	explicit ScopedRetainedHistoryLimit(uint64_t limit) noexcept;
	~ScopedRetainedHistoryLimit() noexcept;
	ScopedRetainedHistoryLimit(const ScopedRetainedHistoryLimit&) = delete;
	ScopedRetainedHistoryLimit& operator=(const ScopedRetainedHistoryLimit&) = delete;

private:
	uint64_t previous_;
};

} // namespace detail

} // namespace recto::pdf
