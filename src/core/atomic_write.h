// Temp-file-then-replace in the destination folder, the only way Recto writes a user's file (D01 T02 section 8; AGENTS.md).
#pragma once

#include "cancel_token.h"

#include "error.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace recto::core {

// Writes `bytes` to `destination`: a temporary file beside it, flushed, read back and compared, then `ReplaceFileW` over
// an existing file (which keeps its ACL) or `MoveFileExW` for a new one, and read back once more. A sharing violation on
// the replace is retried, since a scanner often holds a new file briefly.
//
// By publication state: a failure before the replace leaves the destination unchanged and removes the temporary file; a
// failed replace with the destination still there does the same; a failed replace after which the destination is gone
// keeps the temporary file, then the only copy, and names it in the error; a read-back after the replace that differs is
// an error naming the destination. Cancelled before the replace, nothing changed; past it, the file is saved and the
// call succeeds.
[[nodiscard]] pdf::PdfResult<void> atomic_write_file(
	const std::filesystem::path& destination, std::span<const uint8_t> bytes, const pdf::CancelToken& cancel = {}) noexcept;

namespace detail {

enum class AtomicWriteStage : uint8_t {
	None = 0,
	Write,
	Flush,
	ReadBack,
	Replace,
	Verify,
};

// Makes the calling thread's next atomic write fail at `stage` with `win32_error`, for a test.
class ScopedAtomicWriteFailureForTesting {
public:
	ScopedAtomicWriteFailureForTesting(AtomicWriteStage stage, uint32_t win32_error) noexcept;
	~ScopedAtomicWriteFailureForTesting() noexcept;
	ScopedAtomicWriteFailureForTesting(const ScopedAtomicWriteFailureForTesting&) = delete;
	ScopedAtomicWriteFailureForTesting& operator=(const ScopedAtomicWriteFailureForTesting&) = delete;

private:
	AtomicWriteStage previous_stage_;
	uint32_t previous_error_;
};

} // namespace detail

} // namespace recto::core
