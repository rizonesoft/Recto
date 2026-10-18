#pragma once

// PDF cross-reference table and stream parser (classic `xref` tables, PDF 1.5+
// `/Type /XRef` streams, `/Type /ObjStm` compressed object streams, hybrid
// `/XRefStm`), trailer merger, incremental `/Prev` chain follower,
// damaged-file recovery scanner, and cycle-guarded indirect object resolver
// (ISO 32000-1 sections 7.5.4, 7.5.6, 7.5.7, and 7.5.8; D01 T01 sections 5-6).

#include "error.h"
#include "object.h"
#include "parser.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace recto::pdf {

inline constexpr size_t kHeaderSearchBytes = 1024;
inline constexpr size_t kTailSearchBytes = 1024;
inline constexpr size_t kMaxXrefRevisions = 256;
inline constexpr size_t kMaxIndirectRefDepth = 32;
inline constexpr uint32_t kMaxXrefSubsectionCount = 1'000'000;
inline constexpr uint64_t kMaxXrefTotalEntries = 1'000'000;
inline constexpr uint32_t kMaxObjStmEntries = 1'000'000;
inline constexpr size_t kMaxCachedObjStreams = 64;

enum class XrefEntryKind : uint8_t {
	Free = 0,
	Uncompressed = 1,
	Compressed = 2,
};

struct XrefEntry {
	XrefEntryKind kind{XrefEntryKind::Free};
	uint64_t byte_offset{0};
	uint32_t obj_stm_num{0};
	uint32_t obj_stm_index{0};
	uint16_t generation{0};

	constexpr bool is_free() const noexcept
	{
		return kind == XrefEntryKind::Free;
	}

	constexpr bool is_uncompressed() const noexcept
	{
		return kind == XrefEntryKind::Uncompressed;
	}

	constexpr bool is_compressed() const noexcept
	{
		return kind == XrefEntryKind::Compressed;
	}

	constexpr bool operator==(const XrefEntry&) const noexcept = default;
};

struct CachedObjStream {
	struct HeaderPair {
		uint32_t obj_num{0};
		uint64_t rel_offset{0};
	};

	uint64_t first_offset{0};
	std::vector<HeaderPair> entries{};
	std::vector<uint8_t> decoded_bytes{};
};

class XrefTable {
public:
	using EntryMap = std::unordered_map<uint32_t, XrefEntry>;

	XrefTable() noexcept = default;

	// Scans the first 1,024 bytes of `file_bytes` for `%PDF-<major>.<minor>`
	// and returns its byte offset. Returns `ErrorCode::NotAPdf` when `%PDF-` is
	// absent, or `ErrorCode::DamagedFormat` when the `%PDF-` header is
	// truncated or malformed.
	[[nodiscard]] static PdfResult<uint64_t> locate_header_offset(
		std::span<const uint8_t> file_bytes,
		std::string* out_version = nullptr) noexcept;

	// Scans the trailing 1,024 bytes of `file_bytes` for the last `startxref`
	// keyword and parses its cross-reference byte offset.
	[[nodiscard]] static PdfResult<uint64_t> locate_startxref_offset(
		std::span<const uint8_t> file_bytes) noexcept;

	// Unpacks a decoded `/Type /XRef` stream payload according to `/W [w1 w2 w3]`
	// (`0 <= wi <= 8`, `w1 + w2 + w3 > 0`, default type `1` when `w1 == 0`) and
	// `/Index [first_0 count_0 ...]` (default `[0 /Size]`), inserting first-seen
	// entries into `table` and merging trailer keys from `xref_dict`.
	[[nodiscard]] static PdfResult<void> unpack_xref_stream(
		const PdfDict& xref_dict,
		std::span<const uint8_t> decoded_bytes,
		uint64_t file_size,
		uint64_t header_offset,
		XrefTable& table,
		uint64_t& total_rows_parsed,
		uint64_t max_total_entries = kMaxXrefTotalEntries) noexcept;

	// Linearly scans `file_bytes` for `<N> <G> obj` definitions and surviving
	// `trailer << ... >>` dictionaries (or `/Type /Catalog` indirect objects when
	// no trailer survives) to reconstruct a damaged cross-reference table.
	[[nodiscard]] static PdfResult<XrefTable> recover_damaged_xref(
		std::span<const uint8_t> file_bytes,
		uint64_t max_total_entries = kMaxXrefTotalEntries) noexcept;

	// Parses the PDF header, `startxref`, classic `xref` subsections and/or
	// PDF 1.5+ `/Type /XRef` streams, `trailer` dictionaries, hybrid `/XRefStm`,
	// and `/Prev` incremental update chain (capped at 256 revisions with cycle
	// detection and cumulative entry budget). When `allow_recovery` is true,
	// falls back to `recover_damaged_xref` if `startxref` or the cross-reference
	// data is damaged.
	[[nodiscard]] static PdfResult<XrefTable> parse(
		std::span<const uint8_t> file_bytes,
		uint64_t max_total_entries = kMaxXrefTotalEntries,
		bool allow_recovery = false) noexcept;

	[[nodiscard]] uint64_t header_offset() const noexcept
	{
		return header_offset_;
	}

	[[nodiscard]] std::string_view pdf_version() const noexcept
	{
		return pdf_version_;
	}

	[[nodiscard]] size_t revision_count() const noexcept
	{
		return revision_count_;
	}

	[[nodiscard]] bool recovered() const noexcept
	{
		return recovered_;
	}

	[[nodiscard]] size_t cached_obj_stream_count() const noexcept
	{
		return obj_stm_cache_.size();
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return entries_.size();
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return entries_.empty();
	}

	[[nodiscard]] bool contains(uint32_t obj_num) const noexcept;

	[[nodiscard]] const XrefEntry* find(uint32_t obj_num) const noexcept;

	[[nodiscard]] const EntryMap& entries() const noexcept
	{
		return entries_;
	}

	[[nodiscard]] const PdfDict& trailer() const noexcept
	{
		return trailer_;
	}

	[[nodiscard]] std::optional<IndirectRef> root_ref() const noexcept;
	[[nodiscard]] std::optional<IndirectRef> info_ref() const noexcept;
	[[nodiscard]] std::optional<IndirectRef> encrypt_ref() const noexcept;

	void set_entry(uint32_t obj_num, XrefEntry entry);
	void set_header_offset(uint64_t offset) noexcept
	{
		header_offset_ = offset;
	}
	void set_pdf_version(std::string version) noexcept
	{
		pdf_version_ = std::move(version);
	}
	void set_trailer(PdfDict dict) noexcept
	{
		trailer_ = std::move(dict);
	}
	void clear_obj_stream_cache() const noexcept
	{
		obj_stm_cache_.clear();
	}

	// Reads and parses a single indirect object `N G obj ... endobj` (or extracts
	// a compressed object from its `/Type /ObjStm` stream) without chasing
	// top-level `IndirectRef` bodies.
	[[nodiscard]] PdfResult<IndirectObject> read_indirect_object(
		std::span<const uint8_t> file_bytes,
		IndirectRef ref) const noexcept;

	// Resolves `ref` to its concrete `PdfValue`, chasing indirect reference
	// chains (`1 0 obj 2 0 R endobj`) up to `kMaxIndirectRefDepth = 32` with a
	// per-resolution visited set (`std::vector<IndirectRef>`) that rejects
	// cycles with `ErrorCode::DamagedFormat`.
	[[nodiscard]] PdfResult<PdfValue> resolve_object(
		std::span<const uint8_t> file_bytes,
		IndirectRef ref) const noexcept;

	// If `value` is an `IndirectRef`, resolves it via `resolve_object`;
	// otherwise returns a clone of `value`.
	[[nodiscard]] PdfResult<PdfValue> resolve_value(
		std::span<const uint8_t> file_bytes,
		const PdfValue& value) const noexcept;

private:
	[[nodiscard]] PdfResult<IndirectObject> read_indirect_object_with_visited(
		std::span<const uint8_t> file_bytes,
		IndirectRef ref,
		std::vector<IndirectRef>& visited_refs) const;

	[[nodiscard]] PdfResult<IndirectObject> read_compressed_object_with_visited(
		std::span<const uint8_t> file_bytes,
		IndirectRef ref,
		const XrefEntry& entry,
		std::vector<IndirectRef>& visited_refs) const;

	[[nodiscard]] PdfResult<PdfValue> resolve_object_with_visited(
		std::span<const uint8_t> file_bytes,
		IndirectRef ref,
		std::vector<IndirectRef>& visited_refs) const;

	uint64_t header_offset_{0};
	std::string pdf_version_{"1.7"};
	size_t revision_count_{0};
	bool recovered_{false};
	EntryMap entries_{};
	PdfDict trailer_{};
	mutable std::unordered_map<uint32_t, CachedObjStream> obj_stm_cache_{};
};

} // namespace recto::pdf
