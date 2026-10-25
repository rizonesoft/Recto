// Incremental PDF revisions (D01 T02 section 5): see incremental_writer.h.

#include "incremental_writer.h"

#include "crypto.h"
#include "filters.h"
#include "serialize.h"
#include "xref.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace recto::pdf {

// A call returning `PdfResult<void>` whose error is returned as is.
#define RECTO_WRITER_TRY_VOID(expr)                \
	do {                                           \
		if (auto _writer_r = (expr); !_writer_r) { \
			return std::move(_writer_r).error();   \
		}                                          \
	} while (false)

namespace {

// Each `/ByteRange` integer is reserved at this many digits, as a classic row's offset is written (7.5.4): a base
// with its revision must stay under `kMaxTenDigitOffset` bytes.
constexpr size_t kByteRangeDigits = 10u;
constexpr uint64_t kMaxTenDigitOffset = 10'000'000'000ull;

// Trailer entries the new section writes itself or that belong to the old section only (ISO 32000-1 sections 7.5.5,
// 7.5.8.2 and 7.5.8.4): everything else in the newest trailer is carried forward (7.5.6).
constexpr std::array<std::string_view, 14> kSectionOwnKeys = {
	"Prev", "XRefStm", "Size", "ID", "Type", "W", "Index", "Length", "Filter", "DecodeParms", "F", "FFilter",
	"FDecodeParms", "DL"};

thread_local uint64_t t_revision_limit = kMaxRevisionBytes;
thread_local uint64_t t_allocation_probes = 0;

[[nodiscard]] uint64_t revision_limit() noexcept
{
	return t_revision_limit;
}

// What a staged value holds, as `kMaxRevisionBytes` counts it: its string, name, key and stream bytes, and a fixed
// share for every value and container entry, walked without recursion (a value can nest as deep as its caller made it).
[[nodiscard]] uint64_t value_bytes(const PdfValue& root)
{
	constexpr uint64_t kPerValue = 16u;
	uint64_t total = 0;
	std::vector<const PdfValue*> pending{&root};
	while (!pending.empty()) {
		const PdfValue* v = pending.back();
		pending.pop_back();
		total += kPerValue;
		if (const PdfString* s = v->as_string()) {
			total += s->size();
		} else if (const PdfName* n = v->as_name_obj()) {
			total += n->value.size();
		} else if (const PdfArray* a = v->as_array()) {
			for (const PdfValue& e : *a) {
				pending.push_back(&e);
			}
		} else if (const PdfDict* d = v->as_dict()) {
			for (const auto& [key, value] : *d) {
				total += key.value.size();
				pending.push_back(&value);
			}
		} else if (const PdfStream* st = v->as_stream()) {
			total += st->raw_bytes.size();
			for (const auto& [key, value] : st->dict) {
				total += key.value.size();
				pending.push_back(&value);
			}
		}
	}
	return total;
}

[[nodiscard]] PdfError invalid_argument(std::string_view message, std::optional<uint32_t> obj = std::nullopt)
{
	PdfError err = make_pdf_error(ErrorCode::InvalidArgument, message, std::nullopt, obj);
	record_diagnostic(err);
	return err;
}

[[nodiscard]] PdfError out_of_memory()
{
	return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory writing an incremental revision");
}

[[nodiscard]] bool is_carried_key(std::string_view key) noexcept
{
	return std::find(kSectionOwnKeys.begin(), kSectionOwnKeys.end(), key) == kSectionOwnKeys.end();
}

// Bytes needed to hold `v` big-endian, at least 1.
[[nodiscard]] uint8_t byte_width(uint64_t v) noexcept
{
	uint8_t w = 1;
	while (w < 8u && (v >> (8u * w)) != 0u) {
		++w;
	}
	return w;
}

void put_big_endian(std::vector<uint8_t>& out, uint64_t v, uint8_t width)
{
	for (uint8_t i = width; i > 0u; --i) {
		out.push_back(static_cast<uint8_t>(v >> (8u * (i - 1u))));
	}
}

void put_decimal(std::string& out, uint64_t v, size_t min_digits = 0)
{
	std::array<char, 24> buf{};
	size_t n = 0;
	do {
		buf[n++] = static_cast<char>('0' + v % 10u);
		v /= 10u;
	} while (v != 0u);
	for (size_t i = n; i < min_digits; ++i) {
		out.push_back('0');
	}
	while (n > 0u) {
		out.push_back(buf[--n]);
	}
}

// One cross-reference row as the new section lists it.
struct Row {
	uint32_t number{0};
	bool in_use{false};
	uint64_t field{0}; // the offset (in use) or the next free number (free)
	uint16_t generation{0};
};

// A stream with neither `/Filter` nor `/DecodeParms` is compressed when that makes it smaller; XMP metadata stays as
// it is so tools that scan for XMP find it (ISO 32000-1 section 14.3.2).
[[nodiscard]] PdfResult<void> compress_if_unfiltered(PdfValue& value)
{
	PdfStream* stream = value.as_stream_mut();
	if (stream == nullptr || stream->dict.contains("Filter") || stream->dict.contains("DecodeParms") ||
		stream->dict.get_name("Type") == std::optional<std::string_view>("Metadata")) {
		return {};
	}
	RECTO_PDF_TRY(std::vector<uint8_t> packed, flate_compress(stream->raw_bytes, FlateLevel::Default));
	if (packed.size() < stream->raw_bytes.size()) {
		stream->raw_bytes = std::move(packed);
		stream->raw_length = stream->raw_bytes.size();
		stream->decoded_bytes.reset();
		stream->dict.set("Filter", PdfValue(PdfName("FlateDecode")));
	}
	return {};
}

} // namespace

PdfResult<IncrementalWriter> IncrementalWriter::create(const Document& base) noexcept
{
	try {
		const XrefTable& xref = base.xref();
		if (base.is_recovered() || xref.newest_section() == XrefSectionKind::None) {
			return make_pdf_error(ErrorCode::DamagedFormat,
				"The cross-reference was rebuilt from a damaged file, so a revision cannot be chained to it; write a "
				"full copy instead");
		}
		if (base.is_encrypted() && (!base.is_authenticated() || !base.security_handler().has_value() ||
									   !base.security_handler()->is_authenticated())) {
			return make_pdf_error(ErrorCode::NeedsPassword, "An encrypted document must be unlocked before it is saved");
		}
		if (xref.revision_count() >= kMaxXrefRevisions) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::XrefRevisions, xref.revision_count(),
				kMaxXrefRevisions - 1u, "The file already holds as many revisions as a reader follows");
		}
		// The smallest revision adds object 0's row and, after a stream, the stream's own.
		if (xref.total_rows() + 2u > kMaxXrefTotalEntries) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::XrefRows, xref.total_rows() + 2u,
				kMaxXrefTotalEntries, "A revision would take the cross-reference past what a reader reads");
		}
		// A classic row and a reserved /ByteRange hold ten-digit offsets.
		if (base.file_bytes().size() + kMaxRevisionBytes >= kMaxTenDigitOffset) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, base.file_bytes().size(),
				kMaxTenDigitOffset - kMaxRevisionBytes, "The file is too large for an incremental revision");
		}
		const auto size = xref.trailer().get_int("Size");
		if (!size.has_value() || *size <= 0) {
			return make_pdf_error(ErrorCode::DamagedFormat, "The trailer has no valid /Size");
		}
		IncrementalWriter writer(base);
		writer.base_size_ = static_cast<uint64_t>(*size);
		writer.next_mark_ = writer.base_size_;
		if (const auto root = xref.root_ref()) {
			writer.catalog_ = root->obj_num;
		}
		if (const auto encrypt = xref.encrypt_ref()) {
			writer.encrypt_ = encrypt->obj_num;
		} else if (base.security_handler().has_value() && base.security_handler()->encrypt_ref().has_value()) {
			writer.encrypt_ = base.security_handler()->encrypt_ref()->obj_num;
		}
		return writer;
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<uint16_t> IncrementalWriter::check_existing(IndirectRef ref) const noexcept
{
	if (ref.obj_num == 0u) {
		return invalid_argument("Object 0 is the head of the free list and cannot be written");
	}
	if (encrypt_.has_value() && ref.obj_num == *encrypt_) {
		return invalid_argument("The /Encrypt dictionary is not changed by an incremental revision", ref.obj_num);
	}
	if (const auto it = staged_.find(ref.obj_num); it != staged_.end() && it->second.kind == StagedKind::Free) {
		return invalid_argument("The object was freed in this revision", ref.obj_num);
	}
	const XrefEntry* entry = base_->xref().find(ref.obj_num);
	if (entry == nullptr || entry->is_free()) {
		return invalid_argument("The object is free or does not exist; allocate a new one instead", ref.obj_num);
	}
	const uint16_t generation = entry->is_compressed() ? uint16_t{0} : entry->generation;
	if (ref.gen_num != generation) {
		return invalid_argument("The generation does not match the object's current one", ref.obj_num);
	}
	return generation;
}

PdfResult<uint32_t> IncrementalWriter::next_free_number() const noexcept
{
	// Every number from the base /Size up to the mark is staged, so the search starts at the mark.
	uint64_t number = std::max(base_size_, next_mark_);
	for (auto it = staged_.lower_bound(static_cast<uint32_t>(std::min<uint64_t>(number, kMaxPdfObjectNumber + 1u)));
		 it != staged_.end() && it->first == number; ++it) {
		++number;
		++t_allocation_probes;
	}
	++t_allocation_probes;
	if (number > kMaxPdfObjectNumber) {
		return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::ObjectNumber, number, kMaxPdfObjectNumber,
			"No object number a reader accepts is left");
	}
	return static_cast<uint32_t>(number);
}

PdfResult<void> IncrementalWriter::check_rows(size_t staged_after) const noexcept
{
	// The rows a revision adds: one per staged number, object 0, and a cross-reference stream's own.
	const uint64_t rows = base_->xref().total_rows() + staged_after + 2u;
	if (rows > kMaxXrefTotalEntries) {
		return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::XrefRows, rows, kMaxXrefTotalEntries,
			"The revision would take the cross-reference past what a reader reads");
	}
	return {};
}

PdfResult<void> IncrementalWriter::stage(uint32_t number, Staged staged) noexcept
{
	try {
		const auto it = staged_.find(number);
		RECTO_WRITER_TRY_VOID(check_rows(staged_.size() + (it == staged_.end() ? 1u : 0u)));
		const uint64_t old_bytes = it != staged_.end() ? staged_size(it->second) : 0u;
		const uint64_t new_bytes = staged_size(staged);
		if (staged_bytes_ - old_bytes + new_bytes > revision_limit()) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes,
				staged_bytes_ - old_bytes + new_bytes, revision_limit(), "The revision would be larger than its limit");
		}
		if (it != staged_.end()) {
			it->second = std::move(staged);
		} else {
			staged_.emplace(number, std::move(staged));
		}
		staged_bytes_ = staged_bytes_ - old_bytes + new_bytes;
		if (signature_.has_value() && *signature_ == number && staged_.at(number).kind != StagedKind::Signature) {
			signature_.reset();
		}
		return {};
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<void> IncrementalWriter::set_object(IndirectRef ref, PdfValue value) noexcept
{
	uint16_t generation = 0;
	if (ref.obj_num < base_size_) {
		RECTO_PDF_TRY(generation, check_existing(ref));
	} else {
		if (ref.obj_num > kMaxPdfObjectNumber) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::ObjectNumber, ref.obj_num,
				kMaxPdfObjectNumber, "The object number is past the highest a reader accepts");
		}
		if (ref.gen_num != 0u) {
			return invalid_argument("A new object is written at generation 0", ref.obj_num);
		}
	}
	return stage(ref.obj_num, Staged{StagedKind::Value, generation, std::move(value), 0u});
}

PdfResult<IndirectRef> IncrementalWriter::allocate_object(PdfValue value) noexcept
{
	RECTO_PDF_TRY(const uint32_t number, next_free_number());
	RECTO_WRITER_TRY_VOID(stage(number, Staged{StagedKind::Value, 0u, std::move(value), 0u}));
	next_mark_ = static_cast<uint64_t>(number) + 1u;
	return IndirectRef(number, 0u);
}

PdfResult<void> IncrementalWriter::free_object(IndirectRef ref) noexcept
{
	if (catalog_.has_value() && ref.obj_num == *catalog_) {
		return invalid_argument("The document catalog cannot be freed", ref.obj_num);
	}
	if (ref.obj_num >= base_size_) {
		const auto it = staged_.find(ref.obj_num);
		if (it == staged_.end() || ref.gen_num != 0u) {
			return invalid_argument("The object is not staged in this revision", ref.obj_num);
		}
		try {
			staged_bytes_ -= staged_size(it->second);
		} catch (const std::bad_alloc&) {
			return out_of_memory();
		}
		if (signature_.has_value() && *signature_ == ref.obj_num) {
			signature_.reset();
		}
		staged_.erase(it);
		next_mark_ = std::min<uint64_t>(next_mark_, ref.obj_num); // the number is free again
		return {};
	}
	RECTO_PDF_TRY(const uint16_t generation, check_existing(ref));
	const uint16_t next = generation == std::numeric_limits<uint16_t>::max() ? generation : static_cast<uint16_t>(generation + 1u);
	return stage(ref.obj_num, Staged{StagedKind::Free, next, PdfValue{}, 0u});
}

PdfResult<IndirectRef> IncrementalWriter::add_signature(PdfDict sig, size_t contents_bytes) noexcept
{
	try {
		if (signature_.has_value()) {
			return invalid_argument("A revision holds one signature");
		}
		const auto type = sig.get_name("Type");
		if (!type.has_value() || (*type != "Sig" && *type != "DocTimeStamp")) {
			return invalid_argument("A signature dictionary is /Type /Sig or /DocTimeStamp");
		}
		if (contents_bytes == 0u || contents_bytes > kMaxSignatureContentsBytes) {
			return invalid_argument("The reserved signature size is 0 or past its limit");
		}
		sig.erase("ByteRange");
		sig.erase("Contents");
		RECTO_PDF_TRY(const uint32_t number, next_free_number());
		RECTO_WRITER_TRY_VOID(stage(number, Staged{StagedKind::Signature, 0u, PdfValue(std::move(sig)), contents_bytes}));
		signature_ = number;
		next_mark_ = static_cast<uint64_t>(number) + 1u;
		return IndirectRef(number, 0u);
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<IncrementalRevision> IncrementalWriter::revision() const noexcept
{
	try {
		const XrefTable& xref = base_->xref();
		const std::span<const uint8_t> base_bytes = base_->file_bytes();
		const uint64_t base_size = base_bytes.size();
		const uint64_t header = xref.header_offset();
		const SecurityHandler* handler =
			base_->is_encrypted() && base_->security_handler().has_value() ? &*base_->security_handler() : nullptr;

		// Lookups made while encrypting (an indirect /Type or /Filter) see this revision's objects first.
		const IndirectObjectResolver resolver = [this](IndirectRef ref) -> PdfResult<PdfValue> {
			if (const auto it = staged_.find(ref.obj_num); it != staged_.end()) {
				if (it->second.kind == StagedKind::Free || it->second.generation != ref.gen_num) {
					return PdfValue{};
				}
				return it->second.value.deep_clone();
			}
			return base_->resolve_object(ref);
		};

		IncrementalRevision rev;
		rev.base_size = base_size;
		std::string out;
		if (base_size > 0u && base_bytes.back() != '\n' && base_bytes.back() != '\r') {
			out.push_back('\n');
		}
		const auto remaining = [&out]() -> size_t {
			return static_cast<size_t>(std::min<uint64_t>(kMaxSerializedBytes, revision_limit() - std::min<uint64_t>(out.size(), revision_limit())));
		};

		std::vector<Row> rows;
		rows.reserve(staged_.size() + 2u);
		std::vector<std::array<uint8_t, 16>> digests; // of each object before encryption, for /ID
		digests.reserve(staged_.size());
		std::vector<uint32_t> freed;
		std::string plain; // an object serialized before encryption, when the document is encrypted

		for (const auto& [number, staged] : staged_) {
			const IndirectRef ref(number, staged.generation);
			if (staged.kind == StagedKind::Free) {
				freed.push_back(number);
				rows.push_back(Row{number, false, 0u, staged.generation});
				continue;
			}
			RECTO_PDF_TRY(PdfValue value, staged.value.deep_clone());
			RECTO_WRITER_TRY_VOID(compress_if_unfiltered(value));
			const uint64_t offset = base_size + out.size();
			if (staged.kind == StagedKind::Value) {
				plain.clear();
				if (handler != nullptr) {
					RECTO_WRITER_TRY_VOID(serialize_indirect(ref, value, plain, kMaxSerializedBytes));
					RECTO_WRITER_TRY_VOID(handler->encrypt_object_values(value, ref, &resolver));
				}
				const size_t start = out.size();
				RECTO_WRITER_TRY_VOID(serialize_indirect(ref, value, out, remaining()));
				const std::string_view hashed = handler != nullptr ? std::string_view(plain) : std::string_view(out).substr(start);
				const std::array<std::span<const uint8_t>, 1> chunk = {
					std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(hashed.data()), hashed.size())};
				RECTO_PDF_TRY(const auto digest, md5_digest(chunk));
				digests.push_back(digest);
			} else {
				// A signature: the dictionary with its reserved fields, encrypted as a whole (its /Contents exempt),
				// then written with those two fields placed by hand right after `<<`, so their offsets are known.
				std::string byte_range = "[0";
				for (int i = 0; i < 3; ++i) {
					byte_range.push_back(' ');
					byte_range.append(kByteRangeDigits, '0');
				}
				byte_range.push_back(']');
				std::string contents = "<";
				contents.append(staged.contents_bytes * 2u, '0');
				contents.push_back('>');
				const auto frame = [&](const PdfDict& rest, std::string& into) -> PdfResult<std::pair<size_t, size_t>> {
					std::string body;
					RECTO_WRITER_TRY_VOID(serialize_value(PdfValue(PdfDict(rest)), body, kMaxSerializedBytes));
					if (body.size() < 4u || body.compare(0, 2, "<<") != 0) {
						return make_pdf_error(ErrorCode::InternalError, "A dictionary was not written as << >>");
					}
					put_decimal(into, number);
					into.append(" 0 obj\n<</ByteRange ");
					const size_t br = into.size();
					into.append(byte_range);
					into.append("/Contents ");
					const size_t ct = into.size();
					into.append(contents);
					into.append(body, 2u);
					into.append("\nendobj\n");
					return std::pair<size_t, size_t>{br, ct};
				};
				PdfDict rest = *value.as_dict();
				plain.clear();
				RECTO_PDF_TRY(const auto plain_at, frame(rest, plain));
				static_cast<void>(plain_at);
				if (handler != nullptr) {
					PdfValue whole{PdfDict(rest)};
					whole.as_dict_mut()->set("ByteRange", PdfValue(PdfArray{PdfValue(int64_t{0}), PdfValue(int64_t{0}),
						PdfValue(int64_t{0}), PdfValue(int64_t{0})}));
					whole.as_dict_mut()->set("Contents",
						PdfValue(PdfString(std::vector<uint8_t>(staged.contents_bytes, uint8_t{0}), true)));
					RECTO_WRITER_TRY_VOID(handler->encrypt_object_values(whole, ref, &resolver));
					rest = *whole.as_dict();
					rest.erase("ByteRange");
					rest.erase("Contents");
				}
				if (out.size() + plain.size() > revision_limit()) {
					return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes,
						out.size() + plain.size(), revision_limit(), "The revision would be larger than its limit");
				}
				RECTO_PDF_TRY(const auto at, frame(rest, out));
				rev.signature = SignaturePlaceholder{ref, base_size + at.first, byte_range.size(),
					base_size + at.second, contents.size()};
				const std::array<std::span<const uint8_t>, 1> chunk = {
					std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(plain.data()), plain.size())};
				RECTO_PDF_TRY(const auto digest, md5_digest(chunk));
				digests.push_back(digest);
			}
			rev.objects.push_back(WrittenObject{ref, offset});
			rows.push_back(Row{number, true, offset - header, staged.generation});
		}

		// The free list: object 0 names the lowest number freed here, each the next, the last the base's old head.
		uint64_t old_head = 0;
		if (const XrefEntry* zero = xref.find(0u); zero != nullptr && zero->is_free()) {
			old_head = zero->byte_offset;
		}
		for (Row& row : rows) {
			if (!row.in_use) {
				const auto it = std::upper_bound(freed.begin(), freed.end(), row.number);
				row.field = it != freed.end() ? *it : old_head;
			}
		}
		rows.insert(rows.begin(), Row{0u, false, freed.empty() ? old_head : freed.front(), uint16_t{65'535}});

		// /Size: one past the highest number used, the cross-reference stream's included.
		uint64_t size = base_size_;
		if (!staged_.empty()) {
			size = std::max<uint64_t>(size, static_cast<uint64_t>(staged_.rbegin()->first) + 1u);
		}

		// The trailer: the newest one's entries carried forward (7.5.6), the catalog, info and security from the
		// merged trailer when the newest lacks them, and the file identifier (14.4).
		PdfDict trailer;
		for (const auto& [key, value] : xref.newest_trailer()) {
			if (is_carried_key(key.view())) {
				RECTO_PDF_TRY(PdfValue copy, value.deep_clone());
				trailer.set(key, std::move(copy));
			}
		}
		for (const std::string_view key : {std::string_view("Root"), std::string_view("Info"), std::string_view("Encrypt")}) {
			if (!trailer.contains(key)) {
				if (const PdfValue* merged = xref.trailer().get(key)) {
					RECTO_PDF_TRY(PdfValue copy, merged->deep_clone());
					trailer.set(key, std::move(copy));
				}
			}
		}
		std::vector<uint8_t> id0;
		std::vector<uint8_t> old_id1;
		bool had_id = false;
		if (const PdfValue* id = xref.trailer().get("ID"); id != nullptr && id->as_array() != nullptr &&
			id->as_array()->size() == 2u && (*id->as_array())[0].as_string() != nullptr &&
			(*id->as_array())[1].as_string() != nullptr) {
			had_id = true;
			const auto& parts = *id->as_array();
			id0 = parts[0].as_string()->bytes;
			old_id1 = parts[1].as_string()->bytes;
		}
		if (handler != nullptr) {
			const auto handler_id0 = handler->file_id_0();
			id0.assign(handler_id0.begin(), handler_id0.end());
		}
		std::array<uint8_t, 8> size_le{};
		for (size_t i = 0; i < size_le.size(); ++i) {
			size_le[i] = static_cast<uint8_t>(base_size >> (8u * i));
		}
		std::vector<std::span<const uint8_t>> chunks;
		chunks.reserve(digests.size() + 3u);
		chunks.emplace_back(id0);
		chunks.emplace_back(old_id1);
		chunks.emplace_back(size_le);
		for (const auto& d : digests) {
			chunks.emplace_back(d);
		}
		std::vector<uint8_t> freed_bytes;
		for (const Row& row : rows) {
			if (!row.in_use && row.number != 0u) {
				put_big_endian(freed_bytes, row.number, 4u);
				put_big_endian(freed_bytes, row.generation, 2u);
			}
		}
		chunks.emplace_back(freed_bytes);
		RECTO_PDF_TRY(const auto id1, md5_digest(chunks));
		const bool keyed_without_id = handler != nullptr && handler->params().r <= 4 && id0.empty();
		if (!keyed_without_id) {
			const std::vector<uint8_t> first = (had_id || handler != nullptr) && !id0.empty()
				? id0 : std::vector<uint8_t>(id1.begin(), id1.end());
			trailer.set("ID", PdfValue(PdfArray{PdfValue(PdfString(first, true)),
				PdfValue(PdfString(std::vector<uint8_t>(id1.begin(), id1.end()), true))}));
		}
		const uint64_t prev = xref.startxref_offset();
		trailer.set("Prev", PdfValue(static_cast<int64_t>(prev)));

		uint64_t section_at = 0;
		if (xref.newest_section() == XrefSectionKind::Stream) {
			// A cross-reference stream (7.5.8) with its own number, listing itself.
			RECTO_PDF_TRY(const uint32_t self, next_free_number());
			size = std::max<uint64_t>(size, static_cast<uint64_t>(self) + 1u);
			section_at = base_size + out.size();
			if (section_at < header) {
				return make_pdf_error(ErrorCode::InternalError, "Section offset before the header");
			}
			rows.push_back(Row{self, true, section_at - header, 0u});
			std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.number < b.number; });
			uint64_t widest = 0;
			uint16_t widest_gen = 0;
			for (const Row& row : rows) {
				widest = std::max(widest, row.field);
				widest_gen = std::max(widest_gen, row.generation);
			}
			const uint8_t w2 = byte_width(widest);
			const uint8_t w3 = byte_width(widest_gen);
			std::vector<uint8_t> data;
			data.reserve(rows.size() * (1u + w2 + w3));
			PdfArray index;
			for (size_t i = 0; i < rows.size();) {
				size_t j = i + 1u;
				while (j < rows.size() && rows[j].number == rows[j - 1u].number + 1u) {
					++j;
				}
				index.emplace_back(static_cast<int64_t>(rows[i].number));
				index.emplace_back(static_cast<int64_t>(j - i));
				i = j;
			}
			for (const Row& row : rows) {
				data.push_back(row.in_use ? uint8_t{1} : uint8_t{0});
				put_big_endian(data, row.field, w2);
				put_big_endian(data, row.generation, w3);
			}
			PdfDict dict = std::move(trailer);
			dict.set("Type", PdfValue(PdfName("XRef")));
			dict.set("Size", PdfValue(static_cast<int64_t>(size)));
			dict.set("Index", PdfValue(std::move(index)));
			dict.set("W", PdfValue(PdfArray{PdfValue(int64_t{1}), PdfValue(static_cast<int64_t>(w2)),
				PdfValue(static_cast<int64_t>(w3))}));
			PdfValue stream_value(PdfStream(std::move(dict), std::move(data)));
			RECTO_WRITER_TRY_VOID(compress_if_unfiltered(stream_value));
			RECTO_WRITER_TRY_VOID(serialize_indirect(IndirectRef(self, 0u), stream_value, out, remaining()));
			rev.objects.push_back(WrittenObject{IndirectRef(self, 0u), section_at});
			std::sort(rev.objects.begin(), rev.objects.end(),
				[](const WrittenObject& a, const WrittenObject& b) { return a.ref.obj_num < b.ref.obj_num; });
		} else {
			// A classic table (7.5.4) of 20-byte rows in subsections of consecutive numbers, then the trailer (7.5.5).
			section_at = base_size + out.size();
			if (section_at < header) {
				return make_pdf_error(ErrorCode::InternalError, "Section offset before the header");
			}
			out.append("xref\n");
			for (size_t i = 0; i < rows.size();) {
				size_t j = i + 1u;
				while (j < rows.size() && rows[j].number == rows[j - 1u].number + 1u) {
					++j;
				}
				put_decimal(out, rows[i].number);
				out.push_back(' ');
				put_decimal(out, j - i);
				out.push_back('\n');
				for (size_t k = i; k < j; ++k) {
					put_decimal(out, rows[k].field, 10u);
					out.push_back(' ');
					put_decimal(out, rows[k].generation, 5u);
					out.append(rows[k].in_use ? " n\r\n" : " f\r\n");
				}
				i = j;
			}
			trailer.set("Size", PdfValue(static_cast<int64_t>(size)));
			out.append("trailer\n");
			RECTO_WRITER_TRY_VOID(serialize_value(PdfValue(std::move(trailer)), out, remaining()));
			out.push_back('\n');
		}
		out.append("startxref\n");
		put_decimal(out, section_at - header);
		out.append("\n%%EOF\n");
		if (out.size() > revision_limit()) {
			return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, out.size(),
				revision_limit(), "The revision would be larger than its limit");
		}
		rev.bytes.assign(out.begin(), out.end());
		return rev;
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

PdfResult<std::vector<uint8_t>> IncrementalWriter::write() const noexcept
{
	try {
		RECTO_PDF_TRY(IncrementalRevision rev, revision());
		const std::span<const uint8_t> base_bytes = base_->file_bytes();
		std::vector<uint8_t> file;
		file.reserve(base_bytes.size() + rev.bytes.size());
		file.insert(file.end(), base_bytes.begin(), base_bytes.end());
		file.insert(file.end(), rev.bytes.begin(), rev.bytes.end());
		return file;
	} catch (const std::bad_alloc&) {
		return out_of_memory();
	}
}

uint64_t IncrementalWriter::staged_size(const Staged& staged)
{
	// A signature also holds its reserved /Contents, written as two hex digits per byte.
	return value_bytes(staged.value) + static_cast<uint64_t>(staged.contents_bytes) * 2u;
}

namespace detail {

ScopedRevisionByteLimit::ScopedRevisionByteLimit(uint64_t limit) noexcept
	: previous_(t_revision_limit)
{
	t_revision_limit = limit;
}

ScopedRevisionByteLimit::~ScopedRevisionByteLimit() noexcept
{
	t_revision_limit = previous_;
}

uint64_t allocation_probes() noexcept
{
	return t_allocation_probes;
}

} // namespace detail

#undef RECTO_WRITER_TRY_VOID

} // namespace recto::pdf
