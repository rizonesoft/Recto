#pragma once

// Incremental PDF revisions (ISO 32000-1 section 7.5.6; foundation plan section 4.1 item 6; D01 T02 section 5): the
// objects an edit changes are appended after the original bytes with a cross-reference section chained to the old one
// through `/Prev`, so the original stays byte for byte (and any signature over it valid). Objects go through the
// serializer (`serialize.h`), streams without a filter through the Flate compressor (`filters.h`), and an encrypted
// document's objects through its own security handler (`crypto.h`). The section matches the base's newest one: a
// `/Type /XRef` stream after a stream, a classic table after a classic or hybrid one.
//
// A writer reads its base `Document` and never changes it; it must not outlive it, and like the document it belongs to
// the one thread that owns that document (a `DocumentActor` or a job's own instance). It does not judge `/P`
// permissions or a certification signature's `/DocMDP` level: the save commands (D02 T01 section 8) and the signature
// validator (D05 T01 section 8) own those.

#include "document.h"
#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace recto::pdf {

// The largest revision written: every staged value (its strings, names, containers and stream payloads, and a reserved
// signature) counted as it is staged, and the output counted as it is written.
inline constexpr uint64_t kMaxRevisionBytes = 1ull << 30u;

// The highest object number a conforming reader must accept (ISO 32000-1 Annex C.2).
inline constexpr uint32_t kMaxPdfObjectNumber = 8'388'607u;

// The largest signature value `add_signature` reserves room for.
inline constexpr size_t kMaxSignatureContentsBytes = 16u * 1024u * 1024u;

// What a staged value holds as `kMaxRevisionBytes` counts it: its string, name, key and stream bytes, and a fixed share
// for every value and container entry, walked without recursion. Shared with the staged revision chain's history
// budget (D01 T02 section 13). May throw `std::bad_alloc`.
[[nodiscard]] uint64_t staged_value_bytes(const PdfValue& value);

// Where a reserved signature's `/ByteRange` and `/Contents` sit in the written file (offsets from the start of the
// whole file). `/ByteRange` is an array of four ten-digit integers, `[0 0000000000 0000000000 0000000000]`, which a
// signer rewrites in place (padding with spaces before `]`); `/Contents` is `<` followed by two zero hex digits per
// reserved byte and `>`. Neither field moves any other byte when it is patched at its own length.
struct SignaturePlaceholder {
	IndirectRef ref{};
	uint64_t byte_range_offset{0};
	uint64_t byte_range_length{0};
	uint64_t contents_offset{0};
	uint64_t contents_length{0};
};

// One object the revision wrote: where its `N G obj` starts, from the start of the whole file.
struct WrittenObject {
	IndirectRef ref{};
	uint64_t offset{0};
};

// A written revision: the bytes that follow the base, the base's size, every object written (ascending by number,
// the cross-reference stream included) and the signature reserved, if any.
struct IncrementalRevision {
	std::vector<uint8_t> bytes;
	uint64_t base_size{0};
	std::vector<WrittenObject> objects;
	std::optional<SignaturePlaceholder> signature;
};

// "The base /Size" below means the trailer's /Size, or one past the highest number the cross-reference holds when
// that is greater (a reader keeps an entry numbered /Size itself), so a new number is never a live one.
class IncrementalWriter {
public:
	// A writer for `base`, refused before any work when the base cannot be extended safely: a cross-reference rebuilt
	// by recovery is `DamagedFormat` (the save path then writes a full copy), an encrypted document not yet
	// authenticated `NeedsPassword`, and a base already at `kMaxXrefRevisions` revisions or `kMaxXrefTotalEntries`
	// rows, or too large for the ten-digit offsets a classic row holds (10 GB with its revision), `ResourceLimit`. A
	// section listing rows past /Size, which a reader ignores unread, limits the new /Size to below the lowest of them:
	// a base already there is `DamagedFormat`, and so is a new object that would reach it.
	[[nodiscard]] static PdfResult<IncrementalWriter> create(const Document& base) noexcept;

	// The first number a writer on `base` would allocate: one past the trailer's /Size or the highest number its
	// cross-reference holds, whichever is greater. Needs no writer, so it answers for a base `create` would refuse.
	[[nodiscard]] static uint64_t first_new_number(const Document& base) noexcept;

	IncrementalWriter(IncrementalWriter&&) noexcept = default;
	IncrementalWriter& operator=(IncrementalWriter&&) noexcept = default;
	IncrementalWriter(const IncrementalWriter&) = delete;
	IncrementalWriter& operator=(const IncrementalWriter&) = delete;

	// Writes `value` as object `ref`: an in-use object at its current generation (one stored in an object stream is
	// generation 0 and is written uncompressed), or a number at or past the base `/Size` at generation 0. Object 0, a
	// free or unknown number below `/Size`, a wrong generation, a number freed in this revision and the `/Encrypt`
	// dictionary are `InvalidArgument`; a number past `kMaxPdfObjectNumber` or a write past the reader's row limit or
	// `kMaxRevisionBytes` `ResourceLimit`. Writing a number already staged replaces it.
	[[nodiscard]] PdfResult<void> set_object(IndirectRef ref, PdfValue value) noexcept;

	// Writes `value` at the lowest number at or past the base `/Size` that nothing staged holds, and returns it, in
	// amortized constant probes: numbers below a kept mark are all staged, so the search starts there.
	[[nodiscard]] PdfResult<IndirectRef> allocate_object(PdfValue value) noexcept;

	// Frees an in-use object: a free entry at the next generation (65,535 at most, then never reused; ISO 32000-1
	// section 7.5.4), linked into the free list after object 0 in ascending order ahead of the base's old chain. A
	// number staged at or past `/Size` is simply dropped. The catalog and `/Encrypt` are `InvalidArgument`, as are the
	// numbers `set_object` refuses.
	[[nodiscard]] PdfResult<void> free_object(IndirectRef ref) noexcept;

	// Stages one signature dictionary (`/Type /Sig` or `/DocTimeStamp`) at a new number with a reserved `/ByteRange`
	// and a `/Contents` of `contents_bytes` zero bytes, written by the writer after the rest of the dictionary, so
	// `IncrementalRevision::signature` can say where both are. A second signature, another `/Type`, or a size of 0 or
	// past `kMaxSignatureContentsBytes` is `InvalidArgument`. Any `/ByteRange` or `/Contents` in `sig` is replaced.
	[[nodiscard]] PdfResult<IndirectRef> add_signature(PdfDict sig, size_t contents_bytes) noexcept;

	// The revision for everything staged, built in its own buffers: any failure (compression, encryption,
	// serialization, an allocation, a limit) returns its error and leaves the writer and the base as they were.
	[[nodiscard]] PdfResult<IncrementalRevision> revision() const noexcept;

	// The base bytes followed by `revision().bytes`.
	[[nodiscard]] PdfResult<std::vector<uint8_t>> write() const noexcept;

private:
	enum class StagedKind : uint8_t {
		Value,
		Free,
		Signature,
	};

	struct Staged {
		StagedKind kind{StagedKind::Value};
		uint16_t generation{0}; // as written: the object's own, or the next one for a free entry
		PdfValue value{};
		size_t contents_bytes{0}; // signatures only
	};

	explicit IncrementalWriter(const Document& base) noexcept
		: base_(&base)
	{
	}

	[[nodiscard]] PdfResult<uint16_t> check_existing(IndirectRef ref) const noexcept;
	[[nodiscard]] PdfResult<uint32_t> next_free_number(bool for_section = false) const noexcept;
	[[nodiscard]] PdfResult<void> check_rows(size_t staged_after) const noexcept;
	[[nodiscard]] PdfResult<void> check_size_reach(uint64_t number, bool leave_section_room) const noexcept;
	[[nodiscard]] PdfResult<void> stage(uint32_t number, Staged staged) noexcept;
	[[nodiscard]] static uint64_t staged_size(const Staged& staged); // may throw std::bad_alloc

	const Document* base_;
	uint64_t base_size_{0}; // the merged trailer's /Size, or past the highest number the table holds
	uint64_t next_mark_{0}; // every number from the base /Size up to this one is staged
	bool stream_{false};    // the new section is a cross-reference stream, which takes a number of its own
	std::optional<uint32_t> ignored_from_{}; // the lowest number a base section lists past /Size, which /Size must not reach
	std::optional<uint32_t> catalog_{};
	std::optional<uint32_t> encrypt_{};
	std::map<uint32_t, Staged> staged_{};
	uint64_t staged_bytes_{0}; // what every staged value holds, as `kMaxRevisionBytes` counts it
	std::optional<uint32_t> signature_{};
};

namespace detail {

// Tests only: while alive, writers on this thread use `limit` in place of `kMaxRevisionBytes`, so the refusal at the
// limit can be checked without staging a gigabyte.
class ScopedRevisionByteLimit {
public:
	explicit ScopedRevisionByteLimit(uint64_t limit) noexcept;
	~ScopedRevisionByteLimit() noexcept;
	ScopedRevisionByteLimit(const ScopedRevisionByteLimit&) = delete;
	ScopedRevisionByteLimit& operator=(const ScopedRevisionByteLimit&) = delete;

private:
	uint64_t previous_;
};

// Tests only: numbers this thread's writers have probed while allocating, so a test can show allocation does not
// rescan what it has already handed out.
[[nodiscard]] uint64_t allocation_probes() noexcept;

} // namespace detail

} // namespace recto::pdf
