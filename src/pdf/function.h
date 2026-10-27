// PDF functions (ISO 32000-1 section 7.10; D01 T02 section 33): sampled (type 0), exponential (type 2), stitching
// (type 3) and PostScript calculator (type 4) functions, the tint transforms of Separation and DeviceN colour spaces and
// later the colour functions of shadings. A `Function` is immutable once parsed: evaluating one allocates nothing and
// touches no shared state, so any thread may evaluate it.
#pragma once

#include "error.h"
#include "object.h"

#include <cstddef>
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
inline constexpr size_t kMaxCalculatorOperators = 65'536u;          // tokens in one type 4 program
inline constexpr size_t kMaxCalculatorProgramBytes = size_t{1} << 20u; // a type 4 program's stream, decoded
inline constexpr size_t kCalculatorStackDepth = 100u;               // 7.10.5: the operand stack
inline constexpr size_t kMaxMultilinearInputs = 8u; // past this a sampled function interpolates on a simplex

class Function {
public:
	struct Node;

	// A function given as a dictionary, a stream or an array of one-output functions (read as one function of their
	// common inputs), direct or indirect. A malformed function is `DamagedFormat`; one past a bound above
	// `ResourceLimit`; allocation failure `OutOfMemory`.
	[[nodiscard]] static PdfResult<std::shared_ptr<const Function>> parse(
		const Document& document, const PdfValue& value) noexcept;

	~Function();
	Function(const Function&) = delete;
	Function& operator=(const Function&) = delete;

	[[nodiscard]] size_t inputs() const noexcept;
	[[nodiscard]] size_t outputs() const noexcept;

	// Evaluates the function at `in` (at least `inputs()` values) into `out` (at least `outputs()` values). False when a
	// calculator program failed (stack overflow or underflow, a type error, division by zero, a domain error or a
	// non-finite result): `out` then holds each output's `/Range` low value (0 without a `/Range`), never a partial
	// result. The caller records the diagnostic, once per conversion rather than once per pixel.
	[[nodiscard]] bool evaluate(std::span<const double> in, std::span<double> out) const noexcept;

	explicit Function(std::unique_ptr<const Node> root) noexcept;

private:
	std::unique_ptr<const Node> root_;
};

namespace detail {

// Test hook: the number of function evaluations made, for checking that an image row evaluates a tint transform once
// per distinct sample value rather than once per pixel.
[[nodiscard]] size_t function_evaluations_for_testing() noexcept;

} // namespace detail

} // namespace recto::pdf
