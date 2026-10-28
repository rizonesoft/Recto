#pragma once

// PDF document model (`recto::pdf::Document`): page-tree resolver with
// inherited attributes and cycle protection, `/Info` and XMP `/Metadata`
// extractor, `/Outlines` and named-destination resolver, `/Annots` `/Link`
// extractor (with inert `/Launch` and `/JavaScript` refusal), and deduplicated
// document font inventory (ISO 32000-1:2008 section 7.7, 12.3, 12.5, 14.3;
// D01 T01 section 10).
//
// Freeze contract (foundation plan section 4.2):
// - `pdf::Document` has zero static mutable fields.
// - Every lazy object resolution and stream decode occurs on the owning
//   `Document` instance.
// - `/Launch` and `/JavaScript` actions are flagged as `LinkKind::RefusedAction`
//   (`InertAction`) and never executed.

#include "crypto.h"
#include "error.h"
#include "filters.h"
#include "object.h"
#include "xref.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace recto::pdf {

inline constexpr size_t kMaxPagesTreeDepth = 64;
inline constexpr size_t kMaxDocumentPages = 100'000;
inline constexpr size_t kMaxOutlineDepth = 64;
inline constexpr size_t kMaxOutlineNodes = 16'384;
inline constexpr size_t kMaxNameTreeDepth = 64;
inline constexpr size_t kMaxNameTreeEntries = 65'536;
inline constexpr size_t kMaxFormResourceDepth = 32;

// Structured PDF date parsed from `D:YYYYMMDDHHmmSSOHH'mm'` (ISO 32000-1
// section 7.9.4) or ISO-8601 XMP date strings (`YYYY-MM-DDTHH:MM:SSZ`).
struct PdfDate {
	int32_t year{0};
	int32_t month{1};
	int32_t day{1};
	int32_t hour{0};
	int32_t minute{0};
	int32_t second{0};
	int32_t utc_offset_minutes{0};
	bool has_timezone{false};
	std::string raw{};
	std::string iso8601{};

	[[nodiscard]] std::string to_iso8601() const;

	bool operator==(const PdfDate&) const noexcept = default;
};

// Parses a PDF date string (`D:YYYYMMDDHHmmSSOHH'mm'`) or ISO-8601 date string
// into a structured `PdfDate`. Returns `std::nullopt` if `raw` does not contain
// a valid 4-digit year or has out-of-range calendar fields.
[[nodiscard]] std::optional<PdfDate> parse_pdf_date(std::string_view raw) noexcept;

// Document-level metadata extracted from `/Info` with `/Catalog /Metadata` XMP
// stream fallback.
struct DocumentMetadata {
	std::string title{};
	std::string author{};
	std::string subject{};
	std::string keywords{};
	std::string creator{};
	std::string producer{};
	std::string creation_date{};
	std::string mod_date{};
	std::optional<PdfDate> parsed_creation_date{std::nullopt};
	std::optional<PdfDate> parsed_mod_date{std::nullopt};

	bool operator==(const DocumentMetadata&) const noexcept = default;
};

// Per-page geometry and rotation resolved from the `/Pages` tree.
struct PageDescription {
	uint32_t page_index{0};
	IndirectRef page_ref{};
	PdfRect media_box{0.0, 0.0, 612.0, 792.0};
	PdfRect crop_box{0.0, 0.0, 612.0, 792.0};
	std::optional<PdfRect> bleed_box{std::nullopt};
	std::optional<PdfRect> trim_box{std::nullopt};
	std::optional<PdfRect> art_box{std::nullopt};
	int32_t rotation{0};     // Normalized to 0, 90, 180, or 270 degrees
	int32_t rotation_deg{0}; // Alias matching `rotation`
	double width_pt{612.0};  // Unrotated crop_box.width() in points
	double height_pt{792.0}; // Unrotated crop_box.height() in points

	constexpr bool operator==(const PageDescription&) const noexcept = default;
};

using PageInfo = PageDescription;

// Hierarchical `/Outlines` bookmark item.
struct OutlineNode {
	std::string title{};
	int32_t page_index{-1}; // 0-based target page index, or -1 if unresolved/external
	double dest_x_pt{0.0};
	double dest_y_pt{0.0};
	double zoom{0.0};
	std::string fit_mode{};
	std::string uri{};
	bool has_destination{false};
	bool is_open{true};
	std::vector<OutlineNode> children{};

	bool operator==(const OutlineNode&) const noexcept = default;
};

// Classification of `/Subtype /Link` annotation destinations and actions.
enum class LinkKind : uint8_t {
	Internal = 0,      // Internal page jump (`/Dest` or `/A << /S /GoTo >>`)
	ExternalUri = 1,   // External safe URI (`/A << /S /URI >>` with http/https/mailto)
	RemoteGoTo = 2,    // Cross-document jump (`/A << /S /GoToR >>` or `/GoToE`)
	NamedAction = 3,   // Standard named navigation (`/A << /S /Named >>`)
	RefusedAction = 4, // Refused/inert action (`/Launch`, `/JavaScript`, or unsafe URI scheme)
	InertAction = RefusedAction,
	GoTo = Internal,
	Uri = ExternalUri,
};

// Extracted `/Subtype /Link` annotation on a page.
struct PageLink {
	PdfRect rect_pt{};
	LinkKind kind{LinkKind::RefusedAction};
	int32_t page_index{-1}; // 0-based target page index for `LinkKind::Internal`
	double x_pt{0.0};
	double y_pt{0.0};
	double dest_y_pt{0.0};
	double zoom{0.0};
	std::string uri{};
	std::string action_type{};
	bool is_inert{false};

	bool operator==(const PageLink&) const noexcept = default;
};

using LinkAnnotation = PageLink;

// Deduplicated font descriptor summary across page and Form XObject `/Font`
// resource dictionaries.
struct FontSummary {
	std::string name{};       // Raw `/BaseFont` name (including `ABCDEF+` subset prefix if present)
	std::string base_font{};  // Alias for `name`
	std::string clean_name{}; // `/BaseFont` with any leading `^[A-Z]{6}\+` subset prefix stripped
	std::string subtype{};    // `/Subtype` without leading slash (`Type1`, `TrueType`, `Type0`, `Type3`, ...)
	std::string encoding{};   // `/Encoding` name (`WinAnsiEncoding`, `Identity-H`, `Custom`, ...)
	bool is_embedded{false};  // True when `/FontDescriptor` contains `/FontFile`, `/FontFile2`, or `/FontFile3`
	bool is_subset{false};    // True when `name` starts with `^[A-Z]{6}\+`
	bool has_to_unicode{false};
	bool has_differences{false};

	bool operator==(const FontSummary&) const noexcept = default;
};

// Plain C++23 summary of an opened `pdf::Document`.
struct DocumentDescription {
	std::string pdf_version{"1.7"};
	uint32_t page_count{0};
	bool is_encrypted{false};
	bool is_authenticated{true};
	bool unlocked_as_owner{false};
	bool is_recovered{false};
	CryptFilterMethod encryption_method{CryptFilterMethod::None};
	int32_t encryption_v{0};
	int32_t encryption_r{0};
	int32_t encryption_key_bits{0};
	PdfPermissions permissions{};
	DocumentMetadata metadata{};
	std::vector<PageDescription> pages{};
	std::vector<OutlineNode> outline{};
	std::vector<FontSummary> fonts{};

	bool operator==(const DocumentDescription&) const noexcept = default;
};

class ColorProfileCache;

// The objects a document has parsed, kept so a shared object is not parsed again (D01 T01 section 11). Admission
// follows W-TinyLFU's shape: every newly parsed value enters a small window (`kMaxWindowEntries`, `kMaxWindowBytes`),
// and moves to the main cache (`kMaxObjectCacheEntries`, `kMaxObjectCacheBytes`) when it is asked for again, by a hit
// in the window or by a miss whose key the doorkeeper (an 8 KiB bit filter, cleared every `kDoorkeeperResetCount`
// sightings) has seen. An object read once, as a traversal reads each node, never costs more than the window. Each
// tier drops its least recently used entries first; each entry is charged its value's `heap_bytes()` and its
// bookkeeping; a value over a quarter of the main budget, or a stream whose body is over `kMaxCachedStreamBytes`
// (re-reading it is a copy, cheaper than the room it would take), is never kept. Values are immutable and shared:
// eviction drops only the cache's reference, so a value a caller holds stays valid. Owned, like every `Document`
// member, by the document's actor thread.
class ResolvedObjectCache {
public:
	static constexpr size_t kMaxObjectCacheEntries = 65'536u;
	static constexpr size_t kMaxObjectCacheBytes = size_t{64} << 20u;
	static constexpr size_t kMaxWindowEntries = 16u;
	static constexpr size_t kMaxWindowBytes = size_t{1} << 20u;
	static constexpr size_t kMaxCachedStreamBytes = size_t{64} << 10u;
	static constexpr size_t kDoorkeeperResetCount = 8'192u;

	// The value cached under `key` (object number and generation, `(obj << 16) | gen`), now the most recently used
	// (a window entry moves to the main cache); null when none is. Never allocates.
	[[nodiscard]] std::shared_ptr<const PdfValue> find(uint64_t key) noexcept;

	// Records a newly parsed `value` under `key`: into the main cache when the doorkeeper has seen `key`, else into
	// the window, evicting to stay within every bound; a value too large to keep is not kept. `OutOfMemory` leaves the
	// cache as it was.
	[[nodiscard]] PdfResult<void> insert(uint64_t key, std::shared_ptr<const PdfValue> value) noexcept;

	// What one entry for `value` is charged: its heap, the value itself and its shared control block, and the list
	// node and map slot that track it.
	[[nodiscard]] static size_t entry_bytes(const PdfValue& value) noexcept;

	[[nodiscard]] size_t size() const noexcept
	{
		return slots_.size();
	}
	[[nodiscard]] size_t bytes() const noexcept
	{
		return main_bytes_ + window_bytes_;
	}
	[[nodiscard]] size_t main_size() const noexcept
	{
		return main_.size();
	}

private:
	struct Entry {
		uint64_t key{0};
		std::shared_ptr<const PdfValue> value{};
		size_t bytes{0};
	};
	using List = std::list<Entry>;
	struct Slot {
		List::iterator entry{};
		bool main{false};
	};

	// Sets `key`'s two doorkeeper bits and says whether both were set already.
	[[nodiscard]] bool seen_before(uint64_t key) noexcept;
	void evict() noexcept;

	List main_{};   // most recently used first
	List window_{}; // likewise
	std::unordered_map<uint64_t, Slot> slots_{};
	size_t main_bytes_{0};
	size_t window_bytes_{0};
	std::array<uint64_t, 1'024> doorkeeper_{};
	size_t doorkeeper_count_{0};
};

class Document {
public:
	Document() = default;
	~Document() noexcept = default;

	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;
	Document(Document&&) noexcept = default;
	Document& operator=(Document&&) noexcept = default;

	// Opens a PDF document from disk (`path`), parsing its XRef table/stream
	// (with automatic damaged-file recovery fallback) and checking for
	// encryption (attempting automatic empty-password `""` unlock).
	// If the document requires a non-empty password, returns
	// `ErrorCode::NeedsPassword`.
	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open(
		const std::filesystem::path& path) noexcept;

	// Opens an encrypted (or unencrypted) PDF document from disk using
	// `password`. Returns `ErrorCode::WrongPassword` if `password` is rejected.
	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open(
		const std::filesystem::path& path,
		std::string_view password) noexcept;

	// Opens a PDF document from an in-memory byte span (`bytes`).
	// If the document requires a non-empty password, returns
	// `ErrorCode::NeedsPassword`.
	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open_memory(
		std::span<const uint8_t> bytes) noexcept;

	// Opens an encrypted (or unencrypted) PDF document from an in-memory byte
	// span using `password`. Returns `ErrorCode::WrongPassword` if `password`
	// is rejected.
	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open_memory(
		std::span<const uint8_t> bytes,
		std::string_view password) noexcept;

	// Opens and authenticates a PDF document from `path` or `bytes` with
	// `password`.
	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> authenticate(
		const std::filesystem::path& path,
		std::string_view password) noexcept;

	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> authenticate(
		std::span<const uint8_t> bytes,
		std::string_view password) noexcept;

	// Opens a PDF document from `path` or `bytes`, keeping the parsed XRef and
	// `SecurityHandler` in an unauthenticated state (`needs_password() == true`)
	// when a non-empty password is required so the caller can subsequently call
	// `doc->authenticate(password)` without re-reading or re-parsing XRef.
	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open_unauthenticated(
		const std::filesystem::path& path) noexcept;

	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open_memory_unauthenticated(
		std::span<const uint8_t> bytes) noexcept;

	// Authenticates this `Document` instance with `password` and loads the
	// page tree, metadata, outlines, and font inventory.
	[[nodiscard]] PdfResult<void> authenticate(std::string_view password) noexcept;

	[[nodiscard]] const DocumentDescription& description() const noexcept
	{
		return description_;
	}

	[[nodiscard]] const DocumentMetadata& metadata() const noexcept
	{
		return description_.metadata;
	}

	[[nodiscard]] const std::vector<OutlineNode>& outline() const noexcept
	{
		return description_.outline;
	}

	[[nodiscard]] const std::vector<FontSummary>& fonts() const noexcept
	{
		return description_.fonts;
	}

	[[nodiscard]] const std::vector<PageDescription>& pages() const noexcept
	{
		return description_.pages;
	}

	[[nodiscard]] size_t page_count() const noexcept
	{
		return description_.pages.size();
	}

	[[nodiscard]] std::string_view pdf_version() const noexcept
	{
		return description_.pdf_version;
	}

	[[nodiscard]] bool is_encrypted() const noexcept
	{
		return description_.is_encrypted;
	}

	[[nodiscard]] bool is_authenticated() const noexcept
	{
		return description_.is_authenticated;
	}

	[[nodiscard]] bool needs_password() const noexcept
	{
		return description_.is_encrypted && !description_.is_authenticated;
	}

	[[nodiscard]] bool is_recovered() const noexcept
	{
		return xref_.recovered();
	}

	[[nodiscard]] const PdfPermissions& permissions() const noexcept
	{
		return description_.permissions;
	}

	[[nodiscard]] PdfResult<PageDescription> page_description(size_t page_index) const noexcept;

	// Extracts `/Subtype /Link` annotations for `page_index` (0-based),
	// resolving internal destinations, external URIs, and marking `/Launch` and
	// `/JavaScript` actions as `LinkKind::RefusedAction` (`is_inert = true`).
	[[nodiscard]] PdfResult<std::vector<PageLink>> page_links(size_t page_index) const noexcept;

	// Concatenates and decodes `/Page /Contents` for `page_index` (0-based)
	// whether `/Contents` is a single stream or an array of streams (inserting a
	// single whitespace byte between array streams so adjacent tokens at stream
	// boundaries never fuse).
	[[nodiscard]] PdfResult<std::vector<uint8_t>> decoded_page_contents(
		size_t page_index) const noexcept;

	// Returns the effective (inherited + page-overridden) `/Resources`
	// dictionary for `page_index` (0-based).
	[[nodiscard]] PdfResult<PdfDict> page_resources(size_t page_index) const noexcept;

	// Returns the resolved `/Type /Page` dictionary for `page_index` (0-based).
	[[nodiscard]] PdfResult<PdfDict> page_dict(size_t page_index) const noexcept;

	// Resolves indirect object `ref` from `xref_` (decrypting strings and
	// streams when `is_encrypted()`). The value is a copy of the cached one, so
	// changing it never changes what the next resolve returns.
	[[nodiscard]] PdfResult<PdfValue> resolve_object(IndirectRef ref) const noexcept;

	// Resolves `ref` as `resolve_object` does, without copying: the immutable value
	// the document caches (parsed once while it stays cached).
	[[nodiscard]] PdfResult<std::shared_ptr<const PdfValue>> resolve_object_shared(IndirectRef ref) const noexcept;

	// The resolved-object cache's entries and charged bytes, and how many object bodies this document has parsed.
	[[nodiscard]] size_t cached_object_count() const noexcept
	{
		return object_cache_.size();
	}
	[[nodiscard]] size_t cached_object_bytes() const noexcept
	{
		return object_cache_.bytes();
	}
	[[nodiscard]] uint64_t parsed_object_count() const noexcept
	{
		return parsed_objects_;
	}

	// How many decrypted object streams this document holds decoded (encrypted documents only); siblings share one.
	[[nodiscard]] size_t cached_decrypted_obj_stream_count() const noexcept
	{
		return decrypted_obj_stm_cache_.size();
	}

	// If `value` is an `IndirectRef`, resolves it via `resolve_object`;
	// otherwise returns a deep clone of `value`.
	[[nodiscard]] PdfResult<PdfValue> resolve_value(const PdfValue& value) const noexcept;

	// Decodes `stream` using `recto::pdf::decode_stream` with this document's
	// indirect object resolver bound (for filters such as `JBIG2Decode` with
	// indirect `/JBIG2Globals`).
	[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_stream(
		const PdfStream& stream,
		size_t max_decoded_bytes = kMaxDecodedStreamBytes) const noexcept;

	[[nodiscard]] const XrefTable& xref() const noexcept
	{
		return xref_;
	}

	[[nodiscard]] std::span<const uint8_t> file_bytes() const noexcept
	{
		return file_bytes_;
	}

	[[nodiscard]] const std::optional<SecurityHandler>& security_handler() const noexcept
	{
		return security_handler_;
	}

	// The colour transforms built for this document, made on first use (D01 T02 section 32, `color.h`). Only the
	// DocumentActor thread that owns the document uses them.
	[[nodiscard]] ColorProfileCache& color_profiles() const;

	[[nodiscard]] const PdfDict& catalog() const noexcept
	{
		return catalog_;
	}

	[[nodiscard]] size_t xmp_entity_scan_steps() const noexcept
	{
		return xmp_entity_scan_steps_;
	}

private:
	struct ResolvedPageRecord {
		PageDescription description{};
		PdfDict page_dict{};
		std::shared_ptr<const PdfDict> resources{nullptr};
		std::vector<PageLink> links{};
		std::optional<std::vector<uint8_t>> cached_contents{std::nullopt};
	};

	struct ParsedDest {
		int32_t page_index{-1};
		double x_pt{0.0};
		double y_pt{0.0};
		double zoom{0.0};
		std::string_view fit_mode{};
		bool valid{false};
	};

	[[nodiscard]] static PdfResult<std::vector<uint8_t>> read_file_bytes(
		const std::filesystem::path& path) noexcept;

	[[nodiscard]] static PdfResult<std::unique_ptr<Document>> open_from_owned_bytes(
		std::vector<uint8_t>&& bytes,
		std::optional<std::string_view> password,
		bool allow_unauthenticated) noexcept;

	[[nodiscard]] PdfResult<void> initialize_xref_and_security(
		std::optional<std::string_view> password,
		bool allow_unauthenticated);

	[[nodiscard]] PdfResult<void> load_document_structure();

	struct SingleHopResult {
		std::optional<IndirectRef> next_ref{std::nullopt};
		PdfValue terminal_value{PdfNull{}};
	};

	// One hop read from the file: the next reference of an alias, or the object's parsed (and decrypted) body.
	[[nodiscard]] PdfResult<SingleHopResult> read_single_indirect_hop(
		IndirectRef current,
		const std::vector<IndirectRef>& visited_refs) const;

	struct SharedHop {
		std::optional<IndirectRef> next_ref{std::nullopt};
		std::shared_ptr<const PdfValue> terminal{};
	};

	// One hop through the resolved-object cache: a cached body without reading, else `read_single_indirect_hop`,
	// keeping a parsed body that is not null.
	[[nodiscard]] PdfResult<SharedHop> resolve_single_indirect_hop(
		IndirectRef current,
		const std::vector<IndirectRef>& visited_refs) const;

	// Walks `ref`'s chain (cycle and depth checks before every hop, cached or not) to its shared terminal value.
	[[nodiscard]] PdfResult<std::shared_ptr<const PdfValue>> resolve_shared_with_visited(
		IndirectRef ref,
		std::vector<IndirectRef>& visited_refs) const;

	[[nodiscard]] PdfResult<PdfValue> resolve_object_with_visited(
		IndirectRef ref,
		std::vector<IndirectRef>& visited_refs) const;

	// The name a dictionary entry holds, directly or through indirect references (ISO 32000-1 section 7.3.10: a value
	// may be indirect unless its entry says otherwise), or none when the entry is absent, does not resolve or is not
	// a name.
	[[nodiscard]] std::optional<std::string> entry_name(const PdfDict& dict, std::string_view key) const;

	[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_page_contents_from_dict(
		const PdfDict& page_dict) const;

	std::vector<uint8_t> file_bytes_{};
	XrefTable xref_{};
	std::optional<SecurityHandler> security_handler_{std::nullopt};
	// Decrypted, decoded object streams of an encrypted document, keyed by container number, so sibling objects
	// share one decode (bounded by count and decoded bytes). Owned, like every member, by the DocumentActor thread.
	mutable ObjStmCache decrypted_obj_stm_cache_{};
	// Parsed object bodies, so a shared object is parsed once (D01 T01 section 11); actor thread only.
	mutable ResolvedObjectCache object_cache_{};
	mutable uint64_t parsed_objects_{0};
	// The colour transforms built for this document (D01 T02 section 32), made on first use; actor thread only.
	mutable std::shared_ptr<ColorProfileCache> color_profiles_{};
	PdfDict catalog_{};
	DocumentDescription description_{};
	std::vector<ResolvedPageRecord> page_records_{};
	std::unordered_map<uint64_t, uint32_t> page_obj_to_index_{};
	std::unordered_map<std::string, ParsedDest> named_dests_{};
	size_t xmp_entity_scan_steps_{0u};
	bool structure_loaded_{false};
};

} // namespace recto::pdf
