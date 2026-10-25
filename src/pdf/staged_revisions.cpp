// Staged revisions (D01 T02 section 13): see staged_revisions.h.

#include "staged_revisions.h"

#include "incremental_writer.h"
#include "xref.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace recto::pdf {

// A call returning `PdfResult<void>` whose error is returned as is.
#define RECTO_CHAIN_TRY_VOID(expr)                \
	do {                                          \
		if (auto _chain_r = (expr); !_chain_r) {  \
			return std::move(_chain_r).error();   \
		}                                         \
	} while (false)

namespace {

thread_local int t_reopen_fail_after = 0; // 0: off
thread_local int t_reopen_count = 0;
thread_local uint64_t t_history_limit = kMaxRetainedHistoryBytes;

// Each entry's share of the history budget beyond its value.
constexpr uint64_t kPerEntryBytes = 32u;

[[nodiscard]] PdfError invalid_argument(std::string_view message)
{
	PdfError err = make_pdf_error(ErrorCode::InvalidArgument, message);
	record_diagnostic(err);
	return err;
}

[[nodiscard]] PdfError out_of_memory()
{
	return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory staging a revision");
}

[[nodiscard]] PdfError too_many_objects(size_t count)
{
	return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::ChangeSetSize, count, kMaxChangeSetObjects,
		"One change holds more objects than its limit");
}

[[nodiscard]] uint64_t change_bytes(const ObjectChangeSet& changes)
{
	uint64_t total = 0;
	for (const auto& [number, entry] : changes.entries()) {
		static_cast<void>(number);
		total += kPerEntryBytes + staged_value_bytes(entry.value);
	}
	return total;
}

} // namespace

PdfResult<void> ObjectChangeSet::replace(IndirectRef ref, PdfValue value) noexcept
{
	try {
		if (ref.obj_num == 0u) {
			return invalid_argument("Object 0 is the head of the free list and cannot be replaced");
		}
		const auto it = entries_.find(ref.obj_num);
		const bool allocated_here = it != entries_.end() && it->second.allocated;
		if (ref.obj_num >= first_free_ && !allocated_here) {
			return invalid_argument("A new object is allocated, not replaced");
		}
		if (allocated_here && ref.gen_num != 0u) {
			return invalid_argument("An allocated object is generation 0");
		}
		if (it != entries_.end()) {
			it->second.value = std::move(value);
			it->second.generation = ref.gen_num;
			return {};
		}
		if (entries_.size() >= kMaxChangeSetObjects) {
			return too_many_objects(entries_.size() + 1u);
		}
		entries_.emplace(ref.obj_num, Entry{ref.gen_num, std::move(value), false});
		return {};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<IndirectRef> ObjectChangeSet::allocate(PdfValue value) noexcept
{
	try {
		if (next_free_ > kMaxPdfObjectNumber) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::ObjectNumber, next_free_,
				kMaxPdfObjectNumber, "No object number a reader accepts is left");
		}
		if (entries_.size() >= kMaxChangeSetObjects) {
			return too_many_objects(entries_.size() + 1u);
		}
		const uint32_t number = next_free_;
		entries_.emplace(number, Entry{0u, std::move(value), true});
		++next_free_;
		return IndirectRef(number, 0u);
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<void> ObjectChangeSet::merge(const ObjectChangeSet& later) noexcept
{
	try {
		size_t union_size = entries_.size();
		for (const auto& [number, entry] : later.entries_) {
			static_cast<void>(entry);
			union_size += entries_.contains(number) ? 0u : 1u;
		}
		if (union_size > kMaxChangeSetObjects) {
			return too_many_objects(union_size);
		}
		std::map<uint32_t, Entry> merged = entries_;
		for (const auto& [number, entry] : later.entries_) {
			if (auto it = merged.find(number); it != merged.end()) {
				const bool allocated = it->second.allocated || entry.allocated;
				it->second = entry;
				it->second.allocated = allocated;
			} else {
				merged.emplace(number, entry);
			}
		}
		entries_ = std::move(merged);
		next_free_ = std::max(next_free_, later.next_free_);
		return {};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<std::unique_ptr<StagedRevisionChain>> StagedRevisionChain::create(std::span<const uint8_t> base,
	std::string_view password, RevisionId base_revision, RevisionId first_new_revision) noexcept
{
	try {
		// Push ids come after the base's, so a change built on the base can never match a push.
		if (first_new_revision <= base_revision) {
			return invalid_argument("The first new revision id must come after the base's");
		}
		std::unique_ptr<StagedRevisionChain> chain(new StagedRevisionChain());
		chain->password_.assign(password.begin(), password.end());
		chain->base_revision_ = base_revision;
		chain->next_id_ = first_new_revision.value;
		RECTO_PDF_TRY(chain->base_, password.empty() ? Document::open_memory(base) : Document::open_memory(base, password));
		// The writer's own refusals (a recovered cross-reference, a locked document, the reader's limits) apply to the
		// base before any edit.
		RECTO_PDF_TRY(const IncrementalWriter probe, IncrementalWriter::create(*chain->base_));
		static_cast<void>(probe);
		const size_t revisions = chain->base_->xref().revision_count();
		if (revisions >= kMaxXrefRevisions) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::XrefRevisions, revisions,
				kMaxXrefRevisions - 1u, "The file already holds as many revisions as a reader follows");
		}
		chain->room_ = kMaxXrefRevisions - revisions;
		RECTO_PDF_TRY(chain->working_, chain->reopen(std::vector<uint8_t>(base.begin(), base.end())));
		chain->ends_.assign(1u, base.size());
		return chain;
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

StagedRevisionChain::~StagedRevisionChain() noexcept
{
	if (!password_.empty()) {
		SecureZeroMemory(password_.data(), password_.size());
	}
}

ObjectChangeSet StagedRevisionChain::begin_change() const noexcept
{
	return ObjectChangeSet(current_revision(), next_free_object());
}

uint32_t StagedRevisionChain::next_free_object() const noexcept
{
	const uint64_t first = IncrementalWriter::first_new_number(*working_);
	return static_cast<uint32_t>(std::min<uint64_t>(first, std::numeric_limits<uint32_t>::max()));
}

RevisionId StagedRevisionChain::current_revision() const noexcept
{
	return current_ == 0u ? base_revision_ : states_[current_ - 1u].id;
}

size_t StagedRevisionChain::used_slots() const noexcept
{
	return (squashed_ > 0u ? 1u : 0u) + (current_ - squashed_);
}

uint64_t StagedRevisionChain::history_without_redo() const noexcept
{
	uint64_t total = working_->file_bytes().size() - base_->file_bytes().size();
	for (size_t i = 0; i < current_; ++i) {
		total += states_[i].change_bytes + states_[i].revision.size();
	}
	return total;
}

uint64_t StagedRevisionChain::projected_history(uint64_t working_size, size_t current, size_t squashed, uint64_t layout,
	const State* replaced, size_t replaced_index) const noexcept
{
	uint64_t total = working_size - base_->file_bytes().size();
	for (size_t i = 0; i < states_.size(); ++i) {
		const State& s = (replaced != nullptr && i == replaced_index) ? *replaced : states_[i];
		const bool reusable = s.revision_layout == layout && (i >= current || i >= squashed);
		total += s.change_bytes + (reusable ? s.revision.size() : 0u);
	}
	return total;
}

PdfResult<void> StagedRevisionChain::check_history(uint64_t projected) const noexcept
{
	if (projected > t_history_limit) {
		return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, projected, t_history_limit,
			"The unsaved edits would hold more memory than their limit");
	}
	return {};
}

void StagedRevisionChain::drop_stale_revisions() noexcept
{
	uint64_t total = working_->file_bytes().size() - base_->file_bytes().size();
	for (size_t i = 0; i < states_.size(); ++i) {
		State& s = states_[i];
		// An applied state's own revision is reusable only above the compacted part and in this layout; a state
		// waiting to be redone only in this layout.
		const bool reusable = s.revision_layout == layout_ && (i >= current_ || i >= squashed_);
		if (!reusable && !s.revision.empty()) {
			std::vector<uint8_t>().swap(s.revision);
		}
		total += s.change_bytes + s.revision.size();
	}
	retained_ = total;
}

PdfResult<std::unique_ptr<Document>> StagedRevisionChain::reopen(std::vector<uint8_t> bytes) const noexcept
{
	if (t_reopen_fail_after > 0 && ++t_reopen_count >= t_reopen_fail_after) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Injected failure reopening a working document");
	}
	RECTO_PDF_TRY(std::unique_ptr<Document> doc,
		password_.empty() ? Document::open_memory(bytes) : Document::open_memory(bytes, password_));
	if (doc->is_recovered()) {
		return make_pdf_error(ErrorCode::InternalError, "A staged revision did not read back without recovery");
	}
	return doc;
}

void StagedRevisionChain::fold(ObjectChangeSet& into, const ObjectChangeSet& later)
{
	for (const auto& [number, entry] : later.entries_) {
		auto [it, inserted] = into.entries_.try_emplace(number, entry);
		if (!inserted) {
			const bool allocated = it->second.allocated || entry.allocated;
			it->second = entry;
			it->second.allocated = allocated;
		}
	}
	into.next_free_ = std::max(into.next_free_, later.next_free_);
}

PdfResult<ObjectChangeSet> StagedRevisionChain::merged(size_t through) const noexcept
{
	try {
		// Folded in place into one local copy (a `merge` per state would copy the whole set each time).
		ObjectChangeSet all = states_[0].changes;
		for (size_t i = 1; i < through; ++i) {
			fold(all, states_[i].changes);
		}
		// No per-edit ceiling here: every set was accepted on its own, and the writer bounds the union by the rows a
		// reader reads.
		return all;
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<std::vector<uint8_t>> StagedRevisionChain::append_bytes(const Document& on,
	const ObjectChangeSet& changes) const noexcept
{
	try {
		RECTO_PDF_TRY(IncrementalWriter writer, IncrementalWriter::create(on));
		for (const auto& [number, entry] : changes.entries()) {
			PdfValue value = entry.value; // a copy can run out of memory like any other
			RECTO_CHAIN_TRY_VOID(writer.set_object(IndirectRef(number, entry.generation), std::move(value)));
		}
		return writer.write();
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<std::vector<uint8_t>> StagedRevisionChain::squash_bytes(size_t through) const noexcept
{
	try {
		if (through == 0u) {
			const auto bytes = base_->file_bytes();
			return std::vector<uint8_t>(bytes.begin(), bytes.end());
		}
		RECTO_PDF_TRY(const ObjectChangeSet all, merged(through));
		return append_bytes(*base_, all);
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<StagedRevisionChain::Candidate> StagedRevisionChain::forward(const State& next) const noexcept
{
	try {
		Candidate c;
		State written = next;
		const size_t k = current_ + 1u;
		std::vector<uint8_t> bytes;
		if (used_slots() + 1u <= room_) {
			// Room for one more revision: append it, reusing its bytes when the buffer still ends where they were
			// written.
			const auto now = working_->file_bytes();
			if (!next.revision.empty() && next.revision_layout == layout_ && next.revision_prefix == now.size()) {
				bytes.reserve(now.size() + next.revision.size());
				bytes.assign(now.begin(), now.end());
				bytes.insert(bytes.end(), next.revision.begin(), next.revision.end());
			} else {
				RECTO_PDF_TRY(bytes, append_bytes(*working_, next.changes));
				written.revision.assign(bytes.begin() + static_cast<ptrdiff_t>(now.size()), bytes.end());
				written.revision_prefix = now.size();
				written.revision_layout = layout_;
			}
			c.squashed = squashed_;
			c.layout = layout_;
			c.ends = ends_;
		} else if (room_ >= 2u) {
			// Compact: the states so far become one revision after the base, and this one follows as its own.
			RECTO_PDF_TRY(std::vector<uint8_t> block, squash_bytes(current_));
			const size_t block_size = block.size();
			RECTO_PDF_TRY(std::unique_ptr<Document> on, reopen(std::move(block)));
			RECTO_PDF_TRY(bytes, append_bytes(*on, next.changes));
			c.squashed = current_;
			c.layout = layout_ + 1u;
			c.ends.assign(k, 0u);
			c.ends[current_] = block_size;
			written.revision.assign(bytes.begin() + static_cast<ptrdiff_t>(block_size), bytes.end());
			written.revision_prefix = block_size;
			written.revision_layout = c.layout;
		} else {
			// Room for one revision only: every state is the base plus its changes merged.
			RECTO_PDF_TRY(ObjectChangeSet all, current_ == 0u ? PdfResult<ObjectChangeSet>(next.changes) : merged(current_));
			if (current_ != 0u) {
				fold(all, next.changes);
			}
			RECTO_PDF_TRY(bytes, append_bytes(*base_, all));
			c.squashed = k;
			c.layout = layout_ + 1u;
			c.ends.assign(k, 0u);
			written.revision.clear();
		}
		c.ends.resize(k + 1u, 0u);
		c.ends[k] = bytes.size();
		RECTO_PDF_TRY(c.working, reopen(std::move(bytes)));
		c.written = std::move(written);
		return c;
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<void> StagedRevisionChain::push(ObjectChangeSet changes) noexcept
{
	try {
		if (changes.built_on() != current_revision()) {
			return invalid_argument("The change was built on another state of the document");
		}
		if (changes.empty()) {
			return invalid_argument("An empty change is not a revision");
		}
		if (next_id_ == std::numeric_limits<uint64_t>::max()) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::HistoryStates, next_id_, next_id_,
				"No revision id is left for another edit");
		}
		if (current_ + 1u > kMaxChainStates) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::HistoryStates, current_ + 1u,
				kMaxChainStates, "The document holds as many unsaved edits as undo keeps");
		}
		const uint64_t bytes = change_bytes(changes);
		// Checked before anything is written, then again with the revision the write produced.
		const uint64_t before = history_without_redo();
		if (before + bytes > t_history_limit) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, before + bytes,
				t_history_limit, "The unsaved edits would hold more memory than their limit");
		}
		State next{RevisionId{next_id_}, std::move(changes), bytes, {}, 0u, 0u};
		RECTO_PDF_TRY(Candidate c, forward(next));
		// What stays after the commit: the new working bytes, every kept state's changes, and only the stored
		// revisions still in the new layout (the others are dropped).
		uint64_t after = c.working->file_bytes().size() - base_->file_bytes().size() + c.written->change_bytes +
			c.written->revision.size();
		for (size_t i = 0; i < current_; ++i) {
			after += states_[i].change_bytes;
			if (i >= c.squashed && states_[i].revision_layout == c.layout) {
				after += states_[i].revision.size();
			}
		}
		if (after > t_history_limit) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, after,
				t_history_limit, "The unsaved edits would hold more memory than their limit");
		}
		// Commit: nothing below can fail but an allocation for the state list, made first.
		states_.reserve(current_ + 1u);
		states_.erase(states_.begin() + static_cast<ptrdiff_t>(current_), states_.end());
		states_.push_back(std::move(*c.written));
		++next_id_;
		++current_;
		working_ = std::move(c.working);
		squashed_ = c.squashed;
		layout_ = c.layout;
		ends_ = std::move(c.ends);
		drop_stale_revisions();
		return {};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<void> StagedRevisionChain::undo() noexcept
{
	try {
		if (current_ == 0u) {
			return invalid_argument("There is nothing to undo");
		}
		const size_t target = current_ - 1u;
		std::vector<uint8_t> bytes;
		size_t squashed = squashed_;
		uint64_t layout = layout_;
		std::vector<uint64_t> ends;
		if (target >= squashed_) {
			// The state's revision is the last one: cut it off.
			const auto now = working_->file_bytes();
			bytes.assign(now.begin(), now.begin() + static_cast<ptrdiff_t>(ends_[target]));
			ends.assign(ends_.begin(), ends_.begin() + static_cast<ptrdiff_t>(target + 1u));
		} else {
			// Inside the compacted part: rebuild the base plus the target's changes.
			RECTO_PDF_TRY(bytes, squash_bytes(target));
			squashed = target;
			layout = layout_ + 1u;
			ends.assign(target + 1u, 0u);
			ends[target] = bytes.size();
		}
		RECTO_PDF_TRY(std::unique_ptr<Document> doc, reopen(std::move(bytes)));
		RECTO_CHAIN_TRY_VOID(check_history(projected_history(doc->file_bytes().size(), target, squashed, layout, nullptr, 0u)));
		working_ = std::move(doc);
		current_ = target;
		squashed_ = squashed;
		layout_ = layout;
		ends_ = std::move(ends);
		drop_stale_revisions();
		return {};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<void> StagedRevisionChain::redo() noexcept
{
	try {
		if (current_ == states_.size()) {
			return invalid_argument("There is nothing to redo");
		}
		RECTO_PDF_TRY(Candidate c, forward(states_[current_]));
		RECTO_CHAIN_TRY_VOID(check_history(projected_history(c.working->file_bytes().size(), current_ + 1u, c.squashed,
			c.layout, &*c.written, current_)));
		states_[current_] = std::move(*c.written);
		++current_;
		working_ = std::move(c.working);
		squashed_ = c.squashed;
		layout_ = c.layout;
		ends_ = std::move(c.ends);
		drop_stale_revisions();
		return {};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<ExportedRevision> StagedRevisionChain::export_squashed() const noexcept
{
	try {
		RECTO_PDF_TRY(std::vector<uint8_t> bytes, squash_bytes(current_));
		return ExportedRevision{current_revision(), std::make_shared<const std::vector<uint8_t>>(std::move(bytes))};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

namespace detail {

ScopedWorkingReopenFailure::ScopedWorkingReopenFailure(int after) noexcept
	: previous_(t_reopen_fail_after)
{
	t_reopen_fail_after = after;
	t_reopen_count = 0;
}

ScopedWorkingReopenFailure::~ScopedWorkingReopenFailure() noexcept
{
	t_reopen_fail_after = previous_;
	t_reopen_count = 0;
}

ScopedRetainedHistoryLimit::ScopedRetainedHistoryLimit(uint64_t limit) noexcept
	: previous_(t_history_limit)
{
	t_history_limit = limit;
}

ScopedRetainedHistoryLimit::~ScopedRetainedHistoryLimit() noexcept
{
	t_history_limit = previous_;
}

} // namespace detail

#undef RECTO_CHAIN_TRY_VOID

} // namespace recto::pdf
