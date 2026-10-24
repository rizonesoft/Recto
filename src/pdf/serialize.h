// The canonical serializer of the object model (ISO 32000-1 sections 7.3 and 7.5; D01 T02 section 35): every PDF
// Recto writes, incremental revisions (section 5) and whole documents (`D04 T02 section 2`), goes through it, so a
// value written is always the value the parser reads back.
#pragma once

#include "error.h"
#include "object.h"

#include <cstddef>
#include <string>

namespace recto::pdf {

// The most bytes one call writes: a stream at the largest decoded size and its dictionary.
inline constexpr size_t kMaxSerializedBytes = 512u * 1024u * 1024u;

// The deepest nesting of arrays and dictionaries written, the parser's own limit (`kMaxParserRecursionDepth`).
inline constexpr size_t kMaxSerializedDepth = 64u;

// Appends `value` to `out`: null, booleans, integers, reals in the shortest form that reads back exactly and never
// with an exponent (7.3.3), names with `#XX` escapes (7.3.5), strings literal with escapes or hex when they were hex
// or are mostly binary (7.3.4), arrays, dictionaries and references. A stream is not a direct value (7.3.8) and is
// `InvalidArgument` here; `serialize_indirect` writes it. Nesting past `kMaxSerializedDepth` is
// `RecursionLimitExceeded`, a non-finite real `InvalidArgument`, output past `max_bytes` `ResourceLimit`
// (`RefusalReason::WorkingBytes`) and an allocation failure `OutOfMemory`; on every failure `out` is cut back to
// its length before the call. `value` is never changed.
[[nodiscard]] PdfResult<void> serialize_value(const PdfValue& value, std::string& out,
	size_t max_bytes = kMaxSerializedBytes) noexcept;

// Appends `N G obj`, `value` and `endobj` to `out`. A stream is written from its encoded `raw_bytes` with its
// dictionary as given (filters and their parameters kept) and `/Length` the number of bytes written, never from its
// decoded cache, framed `stream` LF bytes LF `endstream` (7.3.8.1). Failures as `serialize_value`, `out` cut back.
[[nodiscard]] PdfResult<void> serialize_indirect(IndirectRef ref, const PdfValue& value, std::string& out,
	size_t max_bytes = kMaxSerializedBytes) noexcept;

namespace detail {

// Tests only: while alive, a serialization on this thread fails with `OutOfMemory` once its output has grown by
// `after_bytes`, so the rollback after partial output can be checked.
class ScopedSerializeAllocFailure {
public:
	explicit ScopedSerializeAllocFailure(size_t after_bytes) noexcept;
	~ScopedSerializeAllocFailure() noexcept;
	ScopedSerializeAllocFailure(const ScopedSerializeAllocFailure&) = delete;
	ScopedSerializeAllocFailure& operator=(const ScopedSerializeAllocFailure&) = delete;

private:
	size_t previous_;
};

} // namespace detail

} // namespace recto::pdf
