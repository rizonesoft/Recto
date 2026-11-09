// Temp-file-then-replace in the destination folder, the only way Recto writes a user's file (D01 T02 section 8; AGENTS.md).
#pragma once

#include "cancel_token.h"

#include "error.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace recto::core {

// Writes `bytes` to `destination`: a temporary file beside it, flushed, read back and compared, then `ReplaceFileW` over
// an existing file (which keeps its ACL, or the save fails with the destination unchanged) or `MoveFileExW` for a new
// one, and read back once more. A sharing violation on the replace is retried, since a scanner often holds a new file
// briefly.
//
// By publication state: a failure before the replace leaves the destination unchanged and removes the temporary file; a
// failed replace with the destination still there, or a failed move to a path that had no file, does the same; a failed
// replace after which a destination that was there is gone keeps the temporary file, then the only copy, and names it in
// the error; a read-back after the replace that differs is an error naming the destination. Cancelled before the replace
// (the read-back is polled by chunk, and each retry checks), nothing changed; past it, the file is saved and the call
// succeeds.
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
	ReadBackChunk, // between chunks of the read-back before the replace
	Retry,         // before each retry of a replace a sharing violation refused
	Cleanup,       // just before a temporary file that was not published is deleted
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

// Called on the calling thread just before `ReadBack`, each later `ReadBackChunk`, `Replace` (ahead of the last cancel
// check), each `Retry` (ahead of its cancel check), `Verify` and `Cleanup`, with the temporary file's path, so a test can change the
// files on disk or cancel at that point.
using AtomicWriteHook = void (*)(AtomicWriteStage stage, const std::filesystem::path& temporary, void* context) noexcept;

class ScopedAtomicWriteHookForTesting {
public:
	ScopedAtomicWriteHookForTesting(AtomicWriteHook hook, void* context) noexcept;
	~ScopedAtomicWriteHookForTesting() noexcept;
	ScopedAtomicWriteHookForTesting(const ScopedAtomicWriteHookForTesting&) = delete;
	ScopedAtomicWriteHookForTesting& operator=(const ScopedAtomicWriteHookForTesting&) = delete;

private:
	AtomicWriteHook previous_hook_;
	void* previous_context_;
};

} // namespace detail

} // namespace recto::core
