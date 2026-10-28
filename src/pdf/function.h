// PDF functions (ISO 32000-1 section 7.10; D01 T02 section 33): sampled (type 0), exponential (type 2), stitching
// (type 3) and PostScript calculator (type 4) functions, the tint transforms of Separation and DeviceN colour spaces and
// later the colour functions of shadings. A `Function` is immutable once parsed: evaluating one allocates nothing and
// touches no shared state, so any thread may evaluate it.
#pragma once

#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace recto::pdf {

class Document;

// Bounds on what a hostile document may ask of a function.
inline constexpr size_t kMaxFunctionInputs = 32u;  // a DeviceN tint transform has one input per colorant (Annex C.2)
inline constexpr size_t kMaxFunctionOutputs = 32u;
inline constexpr size_t kMaxFunctionDepth = 8u;    // nested stitching functions
inline constexpr size_t kMaxFunctionNodes = 256u;  // functions in one tree, shared subfunctions counted each time
inline constexpr size_t kMaxSampledFunctionBytes = size_t{16} << 20u; // a type 0 sample table, decoded
// All sample tables and spline tables of one function tree together: a subfunction an indirect reference names again is
// parsed once and shared, so repeating a reference costs nothing, while distinct tables add up to this.
inline constexpr size_t kMaxFunctionTreeBytes = kMaxSampledFunctionBytes;
inline constexpr size_t kMaxSplineSamples = 65'536u; // a one-input `/Order 3` table past this is drawn linearly
inline constexpr size_t kMaxCalculatorOperators = 65'536u;          // tokens in one type 4 program
inline constexpr size_t kMaxCalculatorProgramBytes = size_t{1} << 20u; // a type 4 program's stream, decoded
inline constexpr size_t kCalculatorStackDepth = 100u;               // 7.10.5: the operand stack
inline constexpr size_t kMaxMultilinearInputs = 8u; // past this a sampled function interpolates on a simplex

// An indirect object's identity for a cache: its object number and generation together (ISO 32000-1 7.3.10), so a
// reference to a generation the file does not hold never finds another generation's parsed object.
[[nodiscard]] constexpr uint64_t object_key(const IndirectRef& ref) noexcept
{
	return (uint64_t{ref.obj_num} << 16u) | ref.gen_num;
}

class Function {
public:
	struct Node;

	// A function given as a dictionary, a stream or an array of one-output functions (read as one function of their
	// common inputs), direct or indirect. A malformed function is `DamagedFormat`; one past a bound above
	// `ResourceLimit`; allocation failure `OutOfMemory`. `decoded`, when given, is increased by the bytes its streams
	// decoded, whether the parse succeeded or not, so a caller can bound the decoding work of many functions.
	[[nodiscard]] static PdfResult<std::shared_ptr<const Function>> parse(
		const Document& document, const PdfValue& value, size_t* decoded = nullptr) noexcept;

	~Function();
	Function(const Function&) = delete;
	Function& operator=(const Function&) = delete;

	[[nodiscard]] size_t inputs() const noexcept;
	[[nodiscard]] size_t outputs() const noexcept;
	// The bytes its sample and spline tables hold, each shared table once, for a caller's own budget.
	[[nodiscard]] size_t table_bytes() const noexcept;

	// Evaluates the function at `in` (at least `inputs()` values) into `out` (at least `outputs()` values). False when a
	// calculator program failed (stack overflow or underflow, a type error, division by zero, a domain error or a
	// non-finite result): `out` then holds each output's `/Range` low value (0 without a `/Range`), never a partial
	// result. The caller records the diagnostic, once per conversion rather than once per pixel.
	[[nodiscard]] bool evaluate(std::span<const double> in, std::span<double> out) const noexcept;

	Function(std::shared_ptr<const Node> root, size_t table_bytes) noexcept;

private:
	std::shared_ptr<const Node> root_;
	size_t table_bytes_ = 0;
};

namespace detail {

// Test hook: the number of function evaluations made on the calling thread (a thread-local count, so evaluation shares
// no state between threads), for checking that an image row evaluates a tint transform once per distinct sample value
// rather than once per pixel.
[[nodiscard]] size_t function_evaluations_for_testing() noexcept;

} // namespace detail

} // namespace recto::pdf
