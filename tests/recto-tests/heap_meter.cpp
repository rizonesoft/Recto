// Replaces the global operator new and delete for the test binary so ScopedHeapMeter can measure what a call holds
// live on its thread, except under AddressSanitizer, whose own operators check allocation types and sized deletes
// (a replacement would disable that for every test; Microsoft Learn, AddressSanitizer known issues). Every form
// forwards to malloc and free, the pair the MSVC runtime's own operators use, so a block freed here was allocated
// here or by the runtime's malloc; the aligned forms are left to the runtime.

#include "test_support.h"

#include <malloc.h>

#include <cstdlib>
#include <new>

#if !defined(__SANITIZE_ADDRESS__)

namespace {

struct HeapMeterState {
	bool active = false;
	int64_t live = 0;
	int64_t peak = 0;
	uint64_t allocated = 0;
};

thread_local HeapMeterState t_meter;

void note_allocated(void* p) noexcept
{
	if (t_meter.active && p != nullptr) {
		t_meter.live += static_cast<int64_t>(_msize(p));
		t_meter.allocated += _msize(p);
		if (t_meter.live > t_meter.peak) {
			t_meter.peak = t_meter.live;
		}
	}
}

void note_freed(void* p) noexcept
{
	if (t_meter.active && p != nullptr) {
		t_meter.live -= static_cast<int64_t>(_msize(p));
	}
}

void* allocate(std::size_t size)
{
	if (size == 0u) {
		size = 1u;
	}
	while (true) {
		if (void* p = std::malloc(size)) {
			note_allocated(p);
			return p;
		}
		const std::new_handler handler = std::get_new_handler();
		if (handler == nullptr) {
			throw std::bad_alloc();
		}
		handler();
	}
}

void* allocate_nothrow(std::size_t size) noexcept
{
	try {
		return allocate(size);
	} catch (...) {
		return nullptr;
	}
}

void release(void* p) noexcept
{
	note_freed(p);
	std::free(p);
}

} // namespace

namespace recto::test {

ScopedHeapMeter::ScopedHeapMeter() noexcept
{
	t_meter = HeapMeterState{true, 0, 0, 0};
}

ScopedHeapMeter::~ScopedHeapMeter() noexcept
{
	t_meter.active = false;
}

size_t ScopedHeapMeter::peak_bytes() const noexcept
{
	return static_cast<size_t>(t_meter.peak);
}

size_t ScopedHeapMeter::allocated_bytes() const noexcept
{
	return static_cast<size_t>(t_meter.allocated);
}

bool ScopedHeapMeter::available() noexcept
{
	return true;
}

} // namespace recto::test

void* operator new(std::size_t size)
{
	return allocate(size);
}

void* operator new[](std::size_t size)
{
	return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	return allocate_nothrow(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
	return allocate_nothrow(size);
}

void operator delete(void* p) noexcept
{
	release(p);
}

void operator delete[](void* p) noexcept
{
	release(p);
}

void operator delete(void* p, std::size_t) noexcept
{
	release(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
	release(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
	release(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
	release(p);
}

#else // __SANITIZE_ADDRESS__

namespace recto::test {

ScopedHeapMeter::ScopedHeapMeter() noexcept = default;

ScopedHeapMeter::~ScopedHeapMeter() noexcept = default;

size_t ScopedHeapMeter::peak_bytes() const noexcept
{
	return 0;
}

size_t ScopedHeapMeter::allocated_bytes() const noexcept
{
	return 0;
}

bool ScopedHeapMeter::available() noexcept
{
	return false;
}

} // namespace recto::test

#endif // __SANITIZE_ADDRESS__
