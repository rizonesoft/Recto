// PDF functions (ISO 32000-1 section 7.10; D01 T02 section 33).
#include "function.h"

#include "document.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace recto::pdf {

namespace {

thread_local size_t t_evaluations = 0; // test hook; per thread, so evaluation writes nothing another thread reads

// A PostScript calculator instruction (7.10.5, Table 42), with the pushes and jumps the compiler adds.
enum class CalcOp : uint8_t {
	PushInt,
	PushReal,
	PushBool,
	Jump,        // skip `jump` instructions
	JumpIfFalse, // pop a boolean; skip `jump` instructions when it is false
	Abs,
	Add,
	Atan,
	Ceiling,
	Cos,
	Cvi,
	Cvr,
	Div,
	Exp,
	Floor,
	Idiv,
	Ln,
	Log,
	Mod,
	Mul,
	Neg,
	Round,
	Sin,
	Sqrt,
	Sub,
	Truncate,
	And,
	Bitshift,
	Eq,
	Ge,
	Gt,
	Le,
	Lt,
	Ne,
	Not,
	Or,
	Xor,
	Copy,
	Dup,
	Exch,
	Index,
	Pop,
	Roll,
};

struct CalcInstr {
	CalcOp op = CalcOp::Pop;
	double value = 0.0;
	int32_t jump = 0;
};

struct NamedOp {
	std::string_view name;
	CalcOp op;
};

constexpr NamedOp kCalcOps[] = {
	{"abs", CalcOp::Abs}, {"add", CalcOp::Add}, {"atan", CalcOp::Atan}, {"ceiling", CalcOp::Ceiling},
	{"cos", CalcOp::Cos}, {"cvi", CalcOp::Cvi}, {"cvr", CalcOp::Cvr}, {"div", CalcOp::Div}, {"exp", CalcOp::Exp},
	{"floor", CalcOp::Floor}, {"idiv", CalcOp::Idiv}, {"ln", CalcOp::Ln}, {"log", CalcOp::Log}, {"mod", CalcOp::Mod},
	{"mul", CalcOp::Mul}, {"neg", CalcOp::Neg}, {"round", CalcOp::Round}, {"sin", CalcOp::Sin},
	{"sqrt", CalcOp::Sqrt}, {"sub", CalcOp::Sub}, {"truncate", CalcOp::Truncate}, {"and", CalcOp::And},
	{"bitshift", CalcOp::Bitshift}, {"eq", CalcOp::Eq}, {"ge", CalcOp::Ge}, {"gt", CalcOp::Gt}, {"le", CalcOp::Le},
	{"lt", CalcOp::Lt}, {"ne", CalcOp::Ne}, {"not", CalcOp::Not}, {"or", CalcOp::Or}, {"xor", CalcOp::Xor},
	{"copy", CalcOp::Copy}, {"dup", CalcOp::Dup}, {"exch", CalcOp::Exch}, {"index", CalcOp::Index},
	{"pop", CalcOp::Pop}, {"roll", CalcOp::Roll},
};

// 7.10's interpolation, the fraction taken first: with x within [x0, x1] and both spans finite (checked when parsed),
// no intermediate overflows, however large the endpoints.
[[nodiscard]] double interpolate(double x, double x0, double x1, double y0, double y1) noexcept
{
	return x1 == x0 ? y0 : y0 + ((x - x0) / (x1 - x0)) * (y1 - y0);
}

[[nodiscard]] PdfError damaged(std::string_view message) noexcept
{
	return make_pdf_error(ErrorCode::DamagedFormat, message);
}

[[nodiscard]] PdfError limited(std::string_view message, RefusalReason reason, uint64_t observed, uint64_t bound) noexcept
{
	return make_refusal_error(ErrorCode::ResourceLimit, reason, observed, bound, message);
}

} // namespace

struct Function::Node {
	enum class Type : uint8_t {
		Sampled,
		Exponential,
		Stitching,
		Calculator,
		Array,
	};

	Type type = Type::Exponential;
	size_t m = 0;
	size_t n = 0;
	size_t height = 0;   // levels of subfunctions below this one
	size_t expanded = 1; // functions in this subtree, a shared subfunction counted each time it is named
	std::vector<double> domain; // two per input
	std::vector<double> range;  // two per output, or empty (types 2 and 3 may omit it)

	// Type 0.
	std::vector<uint32_t> size;
	std::vector<uint64_t> stride; // samples between neighbours along each input, the first varying fastest
	std::vector<double> encode;   // type 0: two per input; type 3: two per subfunction
	std::vector<double> decode;   // two per output
	uint32_t bits = 0;
	std::vector<uint8_t> samples;
	bool cubic = false;               // `/Order 3` on a one-input function
	std::vector<double> spline_values; // decoded samples, `n` per sample, for the cubic spline
	std::vector<double> spline_second; // their natural spline second derivatives

	// Type 2.
	std::vector<double> c0;
	std::vector<double> c1;
	double exponent = 1.0;

	// Type 3 and function arrays.
	std::vector<std::shared_ptr<const Node>> children;
	std::vector<double> bounds;

	// Type 4.
	std::vector<CalcInstr> code;

	[[nodiscard]] bool evaluate(std::span<const double> in, std::span<double> out) const noexcept;
	[[nodiscard]] uint32_t sample(uint64_t index) const noexcept;
	void evaluate_sampled(std::span<const double> x, std::span<double> out) const noexcept;
	[[nodiscard]] bool run_calculator(std::span<const double> x, std::span<double> out) const noexcept;
	void fail(std::span<double> out) const noexcept;
};

namespace {

struct ParseContext {
	const Document& document;
	size_t nodes = 0;
	size_t bytes = 0;   // sample and spline tables parsed in this tree, each shared table once
	size_t decoded = 0; // bytes the tree's streams decoded, failed parses included
	std::vector<std::pair<uint64_t, std::shared_ptr<const Function::Node>>> parsed; // by `object_key`
};

[[nodiscard]] PdfResult<PdfValue> resolved(const Document& document, const PdfValue& value) noexcept
{
	return document.resolve_value(value);
}

// The numbers of an array entry: none when the key is absent, `DamagedFormat` when it is not an array of finite numbers.
[[nodiscard]] PdfResult<std::optional<std::vector<double>>> numbers(
	const Document& document, const PdfDict& dict, std::string_view key) noexcept
{
	try {
		const PdfValue* entry = dict.get(key);
		if (entry == nullptr) {
			return std::optional<std::vector<double>>{};
		}
		RECTO_PDF_TRY(const PdfValue value, resolved(document, *entry));
		const PdfArray* array = value.as_array();
		if (array == nullptr || array->size() > 2u * kMaxFunctionNodes + 2u * kMaxFunctionInputs) {
			return damaged("PDF function entry is not an array of numbers");
		}
		std::vector<double> out;
		out.reserve(array->size());
		for (const PdfValue& element : *array) {
			RECTO_PDF_TRY(const PdfValue number, resolved(document, element));
			const std::optional<double> x = number.as_number();
			if (!x.has_value() || !std::isfinite(*x)) {
				return damaged("PDF function entry holds something other than a finite number");
			}
			out.push_back(*x);
		}
		return std::optional<std::vector<double>>{std::move(out)};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a PDF function");
	}
}

// Every pair's span is finite, so interpolating across it never overflows to infinity or NaN.
[[nodiscard]] bool finite_spans(const std::vector<double>& pairs) noexcept
{
	for (size_t i = 0; i + 1u < pairs.size(); i += 2u) {
		if (!std::isfinite(pairs[i + 1u] - pairs[i])) {
			return false;
		}
	}
	return true;
}

// Pairs (`/Domain`, `/Range`): an even count from 2 to 2 * `limit`, each pair ordered with a finite span.
[[nodiscard]] bool ordered_pairs(const std::vector<double>& pairs, size_t limit) noexcept
{
	if (pairs.empty() || pairs.size() % 2u != 0u || pairs.size() > 2u * limit || !finite_spans(pairs)) {
		return false;
	}
	for (size_t i = 0; i < pairs.size(); i += 2u) {
		if (pairs[i] > pairs[i + 1u]) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] PdfResult<std::optional<int64_t>> integer(const Document& document, const PdfDict& dict, std::string_view key) noexcept
{
	const PdfValue* entry = dict.get(key);
	if (entry == nullptr) {
		return std::optional<int64_t>{};
	}
	RECTO_PDF_TRY(const PdfValue value, resolved(document, *entry));
	if (const std::optional<int64_t> i = value.as_int()) {
		return std::optional<int64_t>{*i};
	}
	const std::optional<double> x = value.as_number();
	if (x.has_value() && std::isfinite(*x) && *x == std::floor(*x) && std::fabs(*x) < 9.0e15) {
		return std::optional<int64_t>{static_cast<int64_t>(*x)};
	}
	return damaged("PDF function entry is not an integer");
}

PdfResult<std::shared_ptr<const Function::Node>> parse_node(ParseContext& context, const PdfValue& value, size_t depth) noexcept;

// The natural cubic spline's second derivatives through `y` at unit spacing, for one output (`stride` apart).
void natural_spline(const std::vector<double>& y, size_t count, size_t stride, size_t output, std::vector<double>& second,
	std::vector<double>& scratch)
{
	// Thomas algorithm on M[i-1] + 4 M[i] + M[i+1] = 6 (y[i+1] - 2 y[i] + y[i-1]), with M[0] = M[count-1] = 0.
	scratch.assign(count, 0.0);
	std::vector<double> rhs(count, 0.0);
	for (size_t i = 1; i + 1u < count; ++i) {
		rhs[i] = 6.0 * (y[(i + 1u) * stride + output] - 2.0 * y[i * stride + output] + y[(i - 1u) * stride + output]);
	}
	double previous_c = 0.0;
	double previous_d = 0.0;
	for (size_t i = 1; i + 1u < count; ++i) {
		const double denominator = 4.0 - previous_c;
		scratch[i] = 1.0 / denominator;
		const double d = (rhs[i] - previous_d) / denominator;
		rhs[i] = d;
		previous_c = scratch[i];
		previous_d = d;
	}
	second[(count - 1u) * stride + output] = 0.0;
	second[output] = 0.0;
	double next = 0.0;
	for (size_t i = count - 2u; i >= 1u; --i) {
		const double value = rhs[i] - scratch[i] * next;
		second[i * stride + output] = value;
		next = value;
		if (i == 1u) {
			break;
		}
	}
}

PdfResult<std::unique_ptr<Function::Node>> parse_sampled(
	ParseContext& context, const PdfStream& stream, std::unique_ptr<Function::Node> node)
{
	const Document& document = context.document;
	const PdfDict& dict = stream.dict;
	if (node->range.empty()) {
		return damaged("Sampled function has no /Range");
	}
	RECTO_PDF_TRY(const std::optional<std::vector<double>> size, numbers(document, dict, "Size"));
	if (!size.has_value() || size->size() != node->m) {
		return damaged("Sampled function /Size does not give one count per input");
	}
	uint64_t total = 1;
	for (const double s : *size) {
		if (s < 1.0 || s != std::floor(s) || s > 4.0e9) {
			return damaged("Sampled function /Size holds a count that is not a positive integer");
		}
		node->size.push_back(static_cast<uint32_t>(s));
		node->stride.push_back(total);
		total *= static_cast<uint64_t>(s);
		if (total > uint64_t{kMaxSampledFunctionBytes} * 8u) {
			return limited("Sampled function table is larger than its limit", RefusalReason::SampleTableBytes, total,
				kMaxSampledFunctionBytes);
		}
	}
	RECTO_PDF_TRY(const std::optional<int64_t> bits, integer(document, dict, "BitsPerSample"));
	if (!bits.has_value() ||
		(*bits != 1 && *bits != 2 && *bits != 4 && *bits != 8 && *bits != 12 && *bits != 16 && *bits != 24 && *bits != 32)) {
		return damaged("Sampled function /BitsPerSample is not 1, 2, 4, 8, 12, 16, 24 or 32");
	}
	node->bits = static_cast<uint32_t>(*bits);
	const uint64_t table_bits = total * node->n * node->bits;
	const uint64_t table_bytes = (table_bits + 7u) / 8u;
	if (table_bytes > kMaxSampledFunctionBytes) {
		return limited("Sampled function table is larger than its limit", RefusalReason::SampleTableBytes, table_bytes,
			kMaxSampledFunctionBytes);
	}
	if (context.bytes + table_bytes > kMaxFunctionTreeBytes) {
		return limited("Sample tables of one function are larger together than their limit",
			RefusalReason::SampleTableBytes, context.bytes + table_bytes, kMaxFunctionTreeBytes);
	}
	RECTO_PDF_TRY(const std::optional<std::vector<double>> encode, numbers(document, dict, "Encode"));
	if (encode.has_value()) {
		if (encode->size() != 2u * node->m || !finite_spans(*encode)) {
			return damaged("Sampled function /Encode does not give two numbers per input with a finite span");
		}
		node->encode = *encode;
	} else {
		for (const uint32_t s : node->size) {
			node->encode.push_back(0.0);
			node->encode.push_back(static_cast<double>(s - 1u));
		}
	}
	RECTO_PDF_TRY(const std::optional<std::vector<double>> decode, numbers(document, dict, "Decode"));
	if (decode.has_value()) {
		if (decode->size() != 2u * node->n || !finite_spans(*decode)) {
			return damaged("Sampled function /Decode does not give two numbers per output with a finite span");
		}
		node->decode = *decode;
	} else {
		node->decode = node->range;
	}
	RECTO_PDF_TRY(const std::optional<int64_t> order, integer(document, dict, "Order"));
	auto bytes = document.decode_stream(stream, kMaxSampledFunctionBytes);
	if (bytes.has_value()) {
		context.decoded += bytes->size();
	}
	if (!bytes.has_value()) {
		const ErrorCode code = bytes.error().code;
		if (code == ErrorCode::DecompressionLimitExceeded || code == ErrorCode::ResourceLimit) {
			return limited("Sampled function table decodes past its limit", RefusalReason::SampleTableBytes,
				kMaxSampledFunctionBytes + 1u, kMaxSampledFunctionBytes);
		}
		return bytes.error();
	}
	if (bytes->size() < table_bytes) {
		return damaged("Sampled function table is shorter than /Size and /BitsPerSample need");
	}
	// An exact copy, so the node never keeps a longer decoded buffer's capacity it was not charged for.
	node->samples.assign(bytes->begin(), bytes->begin() + static_cast<std::ptrdiff_t>(table_bytes));
	bytes = std::vector<uint8_t>{};
	context.bytes += node->samples.size();
	if (order.has_value() && *order != 1 && *order != 3) {
		// 7.10.2: only 1 and 3 are valid; any other value is drawn linearly, as the default.
		(void)make_pdf_error(ErrorCode::DamagedFormat, "Sampled function /Order is not 1 or 3; drawn linearly");
	}
	// `/Order 3`: a natural cubic spline for one input (7.10.2 names cubic spline interpolation without a method; one
	// input is the common Separation tint transform), linear with a diagnostic otherwise.
	const uint64_t spline_bytes = node->size[0] * node->n * 2u * sizeof(double);
	if (order == 3 && node->m == 1u &&
		(node->size[0] > kMaxSplineSamples || context.bytes + spline_bytes > kMaxFunctionTreeBytes)) {
		record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::SampleTableBytes, node->size[0],
			kMaxSplineSamples, "Sampled function /Order 3 table is past the spline limit; drawn linearly"));
	} else if (order == 3) {
		// Table 39: Order 3 with fewer than 4 samples in a dimension is drawn as Order 1.
		if (node->m == 1u && node->size[0] >= 4u) {
			context.bytes += static_cast<size_t>(spline_bytes);
			node->cubic = true;
			const size_t count = node->size[0];
			node->spline_values.resize(count * node->n);
			node->spline_second.resize(count * node->n);
			const double max_sample = std::ldexp(1.0, static_cast<int>(node->bits)) - 1.0;
			for (size_t i = 0; i < count; ++i) {
				for (size_t j = 0; j < node->n; ++j) {
					node->spline_values[i * node->n + j] = interpolate(
						static_cast<double>(node->sample(i * node->n + j)), 0.0, max_sample, node->decode[2u * j],
						node->decode[2u * j + 1u]);
				}
			}
			std::vector<double> scratch;
			for (size_t j = 0; j < node->n; ++j) {
				natural_spline(node->spline_values, count, node->n, j, node->spline_second, scratch);
			}
			// A /Decode so wide that the spline's derivatives overflow is drawn linearly, never as NaN.
			const auto finite = [](double v) { return std::isfinite(v); };
			if (!std::all_of(node->spline_values.begin(), node->spline_values.end(), finite) ||
				!std::all_of(node->spline_second.begin(), node->spline_second.end(), finite)) {
				node->cubic = false;
				node->spline_values = {};
				node->spline_second = {};
				(void)make_pdf_error(ErrorCode::DamagedFormat, "Sampled function spline overflows; drawn linearly");
			}
		} else if (node->m != 1u) {
			record_diagnostic(make_pdf_error(ErrorCode::UnsupportedFeature,
				"Sampled function /Order 3 with more than one input is drawn linearly"));
		}
	}
	return node;
}

[[nodiscard]] bool regular_char(char c) noexcept
{
	return c != '{' && c != '}' && c != '(' && c != ')' && c != '<' && c != '>' && c != '[' && c != ']' && c != '/' &&
		c != '%' && c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\0';
}

class CalculatorCompiler {
public:
	explicit CalculatorCompiler(std::string_view text) noexcept
		: text_(text)
	{
	}

	PdfResult<std::vector<CalcInstr>> compile()
	{
		skip_space();
		if (at_ >= text_.size() || text_[at_] != '{') {
			return damaged("Calculator function does not start with {");
		}
		++at_;
		RECTO_PDF_TRY(std::vector<CalcInstr> code, procedure(1u));
		skip_space();
		if (at_ != text_.size()) {
			return damaged("Calculator function has text after its closing }");
		}
		return code;
	}

private:
	static constexpr size_t kMaxNesting = 100u;

	void skip_space() noexcept
	{
		while (at_ < text_.size()) {
			const char c = text_[at_];
			if (c == '%') {
				while (at_ < text_.size() && text_[at_] != '\n' && text_[at_] != '\r') {
					++at_;
				}
			} else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0') {
				++at_;
			} else {
				return;
			}
		}
	}

	[[nodiscard]] PdfResult<void> count_token() noexcept
	{
		if (++tokens_ > kMaxCalculatorOperators) {
			return limited("Calculator function is longer than its limit", RefusalReason::CalculatorOperators, tokens_,
				kMaxCalculatorOperators);
		}
		return {};
	}

	// The body of a procedure whose `{` has been read, through its `}`.
	PdfResult<std::vector<CalcInstr>> procedure(size_t nesting)
	{
		if (nesting > kMaxNesting) {
			return damaged("Calculator function nests its procedures too deeply");
		}
		std::vector<CalcInstr> code;
		std::vector<std::vector<CalcInstr>> pending; // procedures read, waiting for their `if` or `ifelse`
		for (;;) {
			skip_space();
			if (at_ >= text_.size()) {
				return damaged("Calculator function procedure has no closing }");
			}
			const char c = text_[at_];
			if (c == '}') {
				++at_;
				if (!pending.empty()) {
					return damaged("Calculator function has a procedure without if or ifelse");
				}
				return code;
			}
			if (c == '{') {
				++at_;
				RECTO_PDF_RETURN_IF_ERROR(count_token());
				RECTO_PDF_TRY(std::vector<CalcInstr> body, procedure(nesting + 1u));
				pending.push_back(std::move(body));
				continue;
			}
			const size_t start = at_;
			while (at_ < text_.size() && regular_char(text_[at_])) {
				++at_;
			}
			if (at_ == start) {
				return damaged("Calculator function holds a character it cannot read");
			}
			RECTO_PDF_RETURN_IF_ERROR(count_token());
			const std::string_view token = text_.substr(start, at_ - start);
			if (token == "if" || token == "ifelse") {
				const size_t need = token == "if" ? 1u : 2u;
				if (pending.size() != need) {
					return damaged("Calculator function has if or ifelse without its procedures");
				}
				const auto jump = [](size_t n) { return static_cast<int32_t>(n); };
				if (need == 1u) {
					code.push_back({CalcOp::JumpIfFalse, 0.0, jump(pending[0].size())});
					code.insert(code.end(), pending[0].begin(), pending[0].end());
				} else {
					code.push_back({CalcOp::JumpIfFalse, 0.0, jump(pending[0].size() + 1u)});
					code.insert(code.end(), pending[0].begin(), pending[0].end());
					code.push_back({CalcOp::Jump, 0.0, jump(pending[1].size())});
					code.insert(code.end(), pending[1].begin(), pending[1].end());
				}
				pending.clear();
				continue;
			}
			if (!pending.empty()) {
				return damaged("Calculator function has a procedure without if or ifelse");
			}
			if (token == "true" || token == "false") {
				code.push_back({CalcOp::PushBool, token == "true" ? 1.0 : 0.0, 0});
				continue;
			}
			const auto named = std::find_if(std::begin(kCalcOps), std::end(kCalcOps),
				[&](const NamedOp& op) { return op.name == token; });
			if (named != std::end(kCalcOps)) {
				code.push_back({named->op, 0.0, 0});
				continue;
			}
			RECTO_PDF_TRY(const CalcInstr number, parse_number(token));
			code.push_back(number);
		}
	}

	[[nodiscard]] static PdfResult<CalcInstr> parse_number(std::string_view token) noexcept
	{
		std::string_view digits = token;
		if (!digits.empty() && digits.front() == '+') {
			digits.remove_prefix(1);
		}
		const bool real = digits.find_first_of(".eE") != std::string_view::npos;
		double value = 0.0;
		const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
		if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size() || !std::isfinite(value)) {
			return damaged("Calculator function holds an unknown operator or a malformed number");
		}
		// An integer past 32 bits is a real, as in PostScript.
		const bool integral = !real && value >= -2147483648.0 && value <= 2147483647.0;
		return CalcInstr{integral ? CalcOp::PushInt : CalcOp::PushReal, value, 0};
	}

	std::string_view text_;
	size_t at_ = 0;
	size_t tokens_ = 0;
};

PdfResult<std::unique_ptr<Function::Node>> parse_calculator(
	ParseContext& context, const PdfStream& stream, std::unique_ptr<Function::Node> node)
{
	if (node->range.empty()) {
		return damaged("Calculator function has no /Range");
	}
	auto bytes = context.document.decode_stream(stream, kMaxCalculatorProgramBytes);
	if (bytes.has_value()) {
		context.decoded += bytes->size();
	}
	if (!bytes.has_value()) {
		const ErrorCode code = bytes.error().code;
		if (code == ErrorCode::DecompressionLimitExceeded || code == ErrorCode::ResourceLimit) {
			return limited("Calculator function decodes past its limit", RefusalReason::CalculatorOperators,
				kMaxCalculatorProgramBytes + 1u, kMaxCalculatorProgramBytes);
		}
		return bytes.error();
	}
	CalculatorCompiler compiler(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
	RECTO_PDF_TRY(node->code, compiler.compile());
	node->code.shrink_to_fit();
	const size_t code_bytes = node->code.capacity() * sizeof(CalcInstr);
	if (context.bytes + code_bytes > kMaxFunctionTreeBytes) {
		return limited("Calculator programs of one function are larger together than their limit",
			RefusalReason::CalculatorOperators, context.bytes + code_bytes, kMaxFunctionTreeBytes);
	}
	context.bytes += code_bytes;
	return node;
}

PdfResult<std::unique_ptr<Function::Node>> parse_array(ParseContext& context, const PdfArray& array, size_t depth)
{
	if (array.empty() || array.size() > kMaxFunctionOutputs) {
		return damaged("Function array does not hold 1 to 32 functions");
	}
	auto node = std::make_unique<Function::Node>();
	node->type = Function::Node::Type::Array;
	for (const PdfValue& element : array) {
		RECTO_PDF_TRY(std::shared_ptr<const Function::Node> child, parse_node(context, element, depth + 1u));
		if (child->n != 1u || (!node->children.empty() && child->m != node->m)) {
			return damaged("Function array holds a function that is not one output of the same inputs");
		}
		node->m = child->m;
		node->children.push_back(std::move(child));
	}
	node->n = node->children.size();
	node->domain = node->children.front()->domain;
	return node;
}

PdfResult<std::unique_ptr<Function::Node>> parse_fresh(ParseContext& context, const PdfValue& value, size_t depth)
{
	{
		const Document& document = context.document;
		RECTO_PDF_TRY(const PdfValue object, resolved(document, value));
		if (const PdfArray* array = object.as_array()) {
			return parse_array(context, *array, depth);
		}
		const PdfStream* stream = object.as_stream();
		const PdfDict* dict = stream != nullptr ? &stream->dict : object.as_dict();
		if (dict == nullptr) {
			return damaged("Function is not a dictionary, a stream or an array");
		}
		RECTO_PDF_TRY(const std::optional<int64_t> type, integer(document, *dict, "FunctionType"));
		auto node = std::make_unique<Function::Node>();
		RECTO_PDF_TRY(const std::optional<std::vector<double>> domain, numbers(document, *dict, "Domain"));
		if (!domain.has_value() || !ordered_pairs(*domain, kMaxFunctionInputs)) {
			return damaged("Function /Domain is not ordered pairs, one per input");
		}
		node->domain = *domain;
		node->m = domain->size() / 2u;
		RECTO_PDF_TRY(const std::optional<std::vector<double>> range, numbers(document, *dict, "Range"));
		if (range.has_value()) {
			if (!ordered_pairs(*range, kMaxFunctionOutputs)) {
				return damaged("Function /Range is not ordered pairs, one per output");
			}
			node->range = *range;
			node->n = range->size() / 2u;
		}
		switch (type.value_or(-1)) {
		case 0:
			if (stream == nullptr) {
				return damaged("Sampled function is not a stream");
			}
			node->type = Function::Node::Type::Sampled;
			return parse_sampled(context, *stream, std::move(node));
		case 2: {
			node->type = Function::Node::Type::Exponential;
			if (node->m != 1u) {
				return damaged("Exponential function does not have one input");
			}
			RECTO_PDF_TRY(const std::optional<std::vector<double>> c0, numbers(document, *dict, "C0"));
			RECTO_PDF_TRY(const std::optional<std::vector<double>> c1, numbers(document, *dict, "C1"));
			node->c0 = c0.value_or(std::vector<double>{0.0});
			node->c1 = c1.value_or(std::vector<double>{1.0});
			if (node->c0.empty() || node->c0.size() != node->c1.size() || node->c0.size() > kMaxFunctionOutputs ||
				(!node->range.empty() && node->range.size() != 2u * node->c0.size())) {
				return damaged("Exponential function /C0, /C1 and /Range disagree on the outputs");
			}
			node->n = node->c0.size();
			for (size_t j = 0; j < node->n; ++j) {
				if (!std::isfinite(node->c1[j] - node->c0[j])) {
					return damaged("Exponential function /C0 and /C1 are too far apart");
				}
			}
			const PdfValue* n_entry = dict->get("N");
			if (n_entry == nullptr) {
				return damaged("Exponential function has no /N");
			}
			RECTO_PDF_TRY(const PdfValue n_value, resolved(document, *n_entry));
			const std::optional<double> exponent = n_value.as_number();
			if (!exponent.has_value() || !std::isfinite(*exponent)) {
				return damaged("Exponential function /N is not a number");
			}
			node->exponent = *exponent;
			return node;
		}
		case 3: {
			node->type = Function::Node::Type::Stitching;
			if (node->m != 1u) {
				return damaged("Stitching function does not have one input");
			}
			const PdfValue* functions_entry = dict->get("Functions");
			if (functions_entry == nullptr) {
				return damaged("Stitching function has no /Functions");
			}
			RECTO_PDF_TRY(const PdfValue functions_value, resolved(document, *functions_entry));
			const PdfArray* functions = functions_value.as_array();
			if (functions == nullptr || functions->empty()) {
				return damaged("Stitching function /Functions is not a non-empty array");
			}
			for (const PdfValue& element : *functions) {
				RECTO_PDF_TRY(std::shared_ptr<const Function::Node> child, parse_node(context, element, depth + 1u));
				if (child->m != 1u || (!node->children.empty() && child->n != node->children.front()->n)) {
					return damaged("Stitching function holds a function of another shape");
				}
				node->children.push_back(std::move(child));
			}
			const size_t k = node->children.size();
			const size_t outputs = node->children.front()->n;
			if (!node->range.empty() && node->n != outputs) {
				return damaged("Stitching function /Range disagrees with its functions' outputs");
			}
			node->n = outputs;
			RECTO_PDF_TRY(const std::optional<std::vector<double>> bounds, numbers(document, *dict, "Bounds"));
			RECTO_PDF_TRY(const std::optional<std::vector<double>> encode, numbers(document, *dict, "Encode"));
			if (!bounds.has_value() || bounds->size() != k - 1u || !encode.has_value() || encode->size() != 2u * k ||
				!finite_spans(*encode)) {
				return damaged("Stitching function /Bounds or /Encode does not match its functions");
			}
			double previous = node->domain[0];
			for (const double b : *bounds) {
				if (b < previous || b > node->domain[1]) {
					return damaged("Stitching function /Bounds are not in order within /Domain");
				}
				previous = b;
			}
			node->bounds = *bounds;
			node->encode = *encode;
			return node;
		}
		case 4:
			if (stream == nullptr) {
				return damaged("Calculator function is not a stream");
			}
			node->type = Function::Node::Type::Calculator;
			return parse_calculator(context, *stream, std::move(node));
		default:
			return damaged("Function /FunctionType is not 0, 2, 3 or 4");
		}
	}
}

// Every function the tree names is counted against the depth and node bounds, each time it is named; one an indirect
// reference names again is parsed once and shared, so its tables are held and charged once.
PdfResult<std::shared_ptr<const Function::Node>> parse_node(ParseContext& context, const PdfValue& value, size_t depth) noexcept
{
	try {
		if (depth > kMaxFunctionDepth) {
			return limited("Functions nest deeper than their limit", RefusalReason::FunctionDepth, depth, kMaxFunctionDepth);
		}
		if (++context.nodes > kMaxFunctionNodes) {
			return limited("A function tree holds more functions than its limit", RefusalReason::FunctionNodes,
				context.nodes, kMaxFunctionNodes);
		}
		const std::optional<IndirectRef> ref = value.as_ref();
		if (ref.has_value()) {
			for (const auto& [number, node] : context.parsed) {
				if (number == object_key(*ref)) {
					// A shared subtree named again counts with its depth below this point and every function in it.
					if (depth + node->height > kMaxFunctionDepth) {
						return limited("Functions nest deeper than their limit", RefusalReason::FunctionDepth,
							depth + node->height, kMaxFunctionDepth);
					}
					context.nodes += node->expanded - 1u;
					if (context.nodes > kMaxFunctionNodes) {
						return limited("A function tree holds more functions than its limit", RefusalReason::FunctionNodes,
							context.nodes, kMaxFunctionNodes);
					}
					return node;
				}
			}
		}
		RECTO_PDF_TRY(std::unique_ptr<Function::Node> fresh, parse_fresh(context, value, depth));
		for (const std::shared_ptr<const Function::Node>& child : fresh->children) {
			fresh->height = std::max(fresh->height, child->height + 1u);
			fresh->expanded += child->expanded;
		}
		// Each function's own bookkeeping counts too, so a tree of many small functions is bounded like one large one.
		if (context.bytes + sizeof(Function::Node) > kMaxFunctionTreeBytes) {
			return limited("A function tree is larger than its limit", RefusalReason::FunctionNodes, context.nodes,
				kMaxFunctionNodes);
		}
		context.bytes += sizeof(Function::Node);
		std::shared_ptr<const Function::Node> node(std::move(fresh));
		if (ref.has_value()) {
			context.parsed.emplace_back(object_key(*ref), node);
		}
		return node;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a PDF function");
	}
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------------------------------------------------

uint32_t Function::Node::sample(uint64_t index) const noexcept
{
	const uint64_t bit = index * bits;
	const size_t byte = static_cast<size_t>(bit / 8u);
	const uint32_t shift = static_cast<uint32_t>(bit % 8u);
	const uint32_t span = (shift + bits + 7u) / 8u; // at most 5 bytes for 32 bits at an odd offset
	uint64_t acc = 0;
	for (uint32_t k = 0; k < span; ++k) {
		acc = (acc << 8u) | samples[byte + k];
	}
	acc >>= span * 8u - shift - bits;
	return static_cast<uint32_t>(acc & ((uint64_t{1} << bits) - 1u));
}

void Function::Node::evaluate_sampled(std::span<const double> x, std::span<double> out) const noexcept
{
	std::array<double, kMaxFunctionOutputs> acc{};
	std::array<uint64_t, kMaxFunctionInputs> base_index{};
	std::array<double, kMaxFunctionInputs> fraction{};
	std::array<size_t, kMaxFunctionInputs> active{}; // the inputs between two samples
	size_t active_count = 0;
	uint64_t base = 0;
	double e0 = 0.0;
	for (size_t i = 0; i < m; ++i) {
		double e = interpolate(x[i], domain[2u * i], domain[2u * i + 1u], encode[2u * i], encode[2u * i + 1u]);
		e = std::isfinite(e) ? std::clamp(e, 0.0, static_cast<double>(size[i] - 1u)) : 0.0; // never an index from NaN
		if (i == 0u) {
			e0 = e;
		}
		double whole = std::floor(e);
		double f = e - whole;
		if (whole >= static_cast<double>(size[i] - 1u)) {
			whole = static_cast<double>(size[i] - 1u);
			f = 0.0;
		}
		base_index[i] = static_cast<uint64_t>(whole);
		fraction[i] = f;
		base += base_index[i] * stride[i];
		if (f > 0.0) {
			active[active_count++] = i;
		}
	}
	const double max_sample = std::ldexp(1.0, static_cast<int>(bits)) - 1.0;
	if (cubic) {
		const size_t count = size[0];
		const size_t i = std::min(static_cast<size_t>(base_index[0]), count - 2u);
		const double t = e0 - static_cast<double>(i);
		const double u = 1.0 - t;
		for (size_t j = 0; j < n; ++j) {
			const double y0 = spline_values[i * n + j];
			const double y1 = spline_values[(i + 1u) * n + j];
			const double m0 = spline_second[i * n + j];
			const double m1 = spline_second[(i + 1u) * n + j];
			double v = u * y0 + t * y1 + ((u * u * u - u) * m0 + (t * t * t - t) * m1) / 6.0;
			if (!std::isfinite(v)) {
				v = u * y0 + t * y1; // the curve term overflowed: the line through the two samples
			}
			out[j] = std::clamp(v, range[2u * j], range[2u * j + 1u]);
		}
		return;
	}
	if (m <= kMaxMultilinearInputs) {
		// Multilinear: every corner of the cell around the point, weighted by the product of its fractions. Chosen by
		// the input count, never by how many inputs fall between samples, so the result is continuous everywhere.
		const uint32_t corners = uint32_t{1} << active_count;
		for (uint32_t corner = 0; corner < corners; ++corner) {
			double weight = 1.0;
			uint64_t index = base;
			for (size_t a = 0; a < active_count; ++a) {
				const size_t i = active[a];
				if ((corner >> a) & 1u) {
					weight *= fraction[i];
					index += stride[i];
				} else {
					weight *= 1.0 - fraction[i];
				}
			}
			if (weight == 0.0) {
				continue;
			}
			for (size_t j = 0; j < n; ++j) {
				acc[j] += weight * static_cast<double>(sample(index * n + j));
			}
		}
	} else {
		// Simplex (Kuhn) interpolation: walk from the cell's base corner along the inputs in falling order of their
		// fractions; active_count + 1 corners, exact at the samples and linear along each edge walked.
		std::sort(active.begin(), active.begin() + static_cast<std::ptrdiff_t>(active_count),
			[&](size_t a, size_t b) { return fraction[a] > fraction[b]; });
		uint64_t index = base;
		double previous = 1.0;
		for (size_t k = 0; k <= active_count; ++k) {
			const double f = k < active_count ? fraction[active[k]] : 0.0;
			const double weight = previous - f;
			for (size_t j = 0; j < n; ++j) {
				acc[j] += weight * static_cast<double>(sample(index * n + j));
			}
			if (k < active_count) {
				index += stride[active[k]];
				previous = f;
			}
		}
	}
	for (size_t j = 0; j < n; ++j) {
		const double v = interpolate(acc[j], 0.0, max_sample, decode[2u * j], decode[2u * j + 1u]);
		out[j] = std::clamp(v, range[2u * j], range[2u * j + 1u]);
	}
}

namespace {

// A calculator operand: a number (integer or real) or a boolean.
struct CalcValue {
	double v = 0.0;
	uint8_t kind = 0; // 0 integer, 1 real, 2 boolean
};

constexpr uint8_t kInt = 0u;
constexpr uint8_t kReal = 1u;
constexpr uint8_t kBool = 2u;

[[nodiscard]] CalcValue number_of(double v, bool integral) noexcept
{
	// An integer result past the 32-bit range becomes a real, as in PostScript.
	if (integral && v >= -2147483648.0 && v <= 2147483647.0) {
		return {v, kInt};
	}
	return {v, kReal};
}

constexpr double kDegrees = 180.0 / 3.14159265358979323846;

} // namespace

bool Function::Node::run_calculator(std::span<const double> x, std::span<double> out) const noexcept
{
	std::array<CalcValue, kCalculatorStackDepth> stack{};
	size_t top = 0; // the number of operands
	for (size_t i = 0; i < m; ++i) {
		stack[top++] = {std::clamp(x[i], domain[2u * i], domain[2u * i + 1u]), kReal};
	}
	const auto need = [&](size_t count) { return top >= count; };
	const auto is_number = [&](size_t depth) { return stack[top - depth].kind != kBool; };
	const auto is_int = [&](size_t depth) { return stack[top - depth].kind == kInt; };
	size_t executed = 0;
	for (size_t pc = 0; pc < code.size(); ++pc) {
		if (++executed > kMaxCalculatorOperators) {
			return false;
		}
		const CalcInstr& instr = code[pc];
		switch (instr.op) {
		case CalcOp::PushInt:
		case CalcOp::PushReal:
		case CalcOp::PushBool:
			if (top >= kCalculatorStackDepth) {
				return false; // stack overflow
			}
			stack[top++] = {instr.value, instr.op == CalcOp::PushInt ? kInt : instr.op == CalcOp::PushReal ? kReal : kBool};
			break;
		case CalcOp::Jump:
			pc += static_cast<size_t>(instr.jump);
			break;
		case CalcOp::JumpIfFalse:
			if (!need(1u) || stack[top - 1u].kind != kBool) {
				return false;
			}
			--top;
			if (stack[top].v == 0.0) {
				pc += static_cast<size_t>(instr.jump);
			}
			break;
		case CalcOp::Abs:
		case CalcOp::Neg:
		case CalcOp::Ceiling:
		case CalcOp::Floor:
		case CalcOp::Round:
		case CalcOp::Truncate:
		case CalcOp::Cvi:
		case CalcOp::Cvr:
		case CalcOp::Sqrt:
		case CalcOp::Sin:
		case CalcOp::Cos:
		case CalcOp::Ln:
		case CalcOp::Log: {
			if (!need(1u) || !is_number(1u)) {
				return false;
			}
			CalcValue& a = stack[top - 1u];
			const bool integral = a.kind == kInt;
			switch (instr.op) {
			case CalcOp::Abs:
				a = number_of(std::fabs(a.v), integral);
				break;
			case CalcOp::Neg:
				a = number_of(-a.v, integral);
				break;
			case CalcOp::Ceiling:
				a = {std::ceil(a.v), a.kind};
				break;
			case CalcOp::Floor:
				a = {std::floor(a.v), a.kind};
				break;
			case CalcOp::Round:
				a = {std::floor(a.v + 0.5), a.kind}; // halfway rounds up, as in PostScript
				break;
			case CalcOp::Truncate:
				a = {std::trunc(a.v), a.kind};
				break;
			case CalcOp::Cvi:
				if (std::trunc(a.v) < -2147483648.0 || std::trunc(a.v) > 2147483647.0) {
					return false; // out of the integer range
				}
				a = {std::trunc(a.v), kInt};
				break;
			case CalcOp::Cvr:
				a = {a.v, kReal};
				break;
			case CalcOp::Sqrt:
				if (a.v < 0.0) {
					return false;
				}
				a = {std::sqrt(a.v), kReal};
				break;
			case CalcOp::Sin:
				a = {std::sin(a.v / kDegrees), kReal};
				break;
			case CalcOp::Cos:
				a = {std::cos(a.v / kDegrees), kReal};
				break;
			case CalcOp::Ln:
			case CalcOp::Log:
				if (a.v <= 0.0) {
					return false;
				}
				a = {instr.op == CalcOp::Ln ? std::log(a.v) : std::log10(a.v), kReal};
				break;
			default:
				break;
			}
			if (!std::isfinite(a.v)) {
				return false;
			}
			break;
		}
		case CalcOp::Add:
		case CalcOp::Sub:
		case CalcOp::Mul:
		case CalcOp::Div:
		case CalcOp::Idiv:
		case CalcOp::Mod:
		case CalcOp::Atan:
		case CalcOp::Exp:
		case CalcOp::Ge:
		case CalcOp::Gt:
		case CalcOp::Le:
		case CalcOp::Lt: {
			if (!need(2u) || !is_number(1u) || !is_number(2u)) {
				return false;
			}
			const bool ints = is_int(1u) && is_int(2u);
			const CalcValue b = stack[--top];
			CalcValue& a = stack[top - 1u];
			switch (instr.op) {
			case CalcOp::Add:
				a = number_of(a.v + b.v, ints);
				break;
			case CalcOp::Sub:
				a = number_of(a.v - b.v, ints);
				break;
			case CalcOp::Mul:
				a = number_of(a.v * b.v, ints);
				break;
			case CalcOp::Div:
				if (b.v == 0.0) {
					return false;
				}
				a = {a.v / b.v, kReal};
				break;
			case CalcOp::Idiv:
			case CalcOp::Mod: {
				if (!ints || b.v == 0.0) {
					return false;
				}
				const auto ia = static_cast<int64_t>(a.v);
				const auto ib = static_cast<int64_t>(b.v);
				a = number_of(static_cast<double>(instr.op == CalcOp::Idiv ? ia / ib : ia % ib), true);
				break;
			}
			case CalcOp::Atan: {
				if (a.v == 0.0 && b.v == 0.0) {
					return false; // undefined
				}
				double angle = std::atan2(a.v, b.v) * kDegrees;
				if (angle < 0.0) {
					angle += 360.0;
				}
				a = {angle, kReal};
				break;
			}
			case CalcOp::Exp:
				a = {std::pow(a.v, b.v), kReal};
				break;
			case CalcOp::Ge:
				a = {a.v >= b.v ? 1.0 : 0.0, kBool};
				break;
			case CalcOp::Gt:
				a = {a.v > b.v ? 1.0 : 0.0, kBool};
				break;
			case CalcOp::Le:
				a = {a.v <= b.v ? 1.0 : 0.0, kBool};
				break;
			case CalcOp::Lt:
				a = {a.v < b.v ? 1.0 : 0.0, kBool};
				break;
			default:
				break;
			}
			if (!std::isfinite(a.v)) {
				return false;
			}
			break;
		}
		case CalcOp::Eq:
		case CalcOp::Ne: {
			if (!need(2u)) {
				return false;
			}
			const CalcValue b = stack[--top];
			CalcValue& a = stack[top - 1u];
			// Numbers compare by value whatever their kind; a number never equals a boolean.
			const bool same = (a.kind == kBool) == (b.kind == kBool) && a.v == b.v;
			a = {(instr.op == CalcOp::Eq) == same ? 1.0 : 0.0, kBool};
			break;
		}
		case CalcOp::And:
		case CalcOp::Or:
		case CalcOp::Xor: {
			if (!need(2u)) {
				return false;
			}
			const CalcValue b = stack[--top];
			CalcValue& a = stack[top - 1u];
			if (a.kind == kBool && b.kind == kBool) {
				const bool x0 = a.v != 0.0;
				const bool x1 = b.v != 0.0;
				const bool r = instr.op == CalcOp::And ? (x0 && x1) : instr.op == CalcOp::Or ? (x0 || x1) : (x0 != x1);
				a = {r ? 1.0 : 0.0, kBool};
			} else if (a.kind == kInt && b.kind == kInt) {
				const auto x0 = static_cast<uint32_t>(static_cast<int32_t>(a.v));
				const auto x1 = static_cast<uint32_t>(static_cast<int32_t>(b.v));
				const uint32_t r = instr.op == CalcOp::And ? (x0 & x1) : instr.op == CalcOp::Or ? (x0 | x1) : (x0 ^ x1);
				a = {static_cast<double>(static_cast<int32_t>(r)), kInt};
			} else {
				return false;
			}
			break;
		}
		case CalcOp::Not: {
			if (!need(1u)) {
				return false;
			}
			CalcValue& a = stack[top - 1u];
			if (a.kind == kBool) {
				a.v = a.v != 0.0 ? 0.0 : 1.0;
			} else if (a.kind == kInt) {
				a.v = static_cast<double>(~static_cast<int32_t>(a.v));
			} else {
				return false;
			}
			break;
		}
		case CalcOp::Bitshift: {
			if (!need(2u) || !is_int(1u) || !is_int(2u)) {
				return false;
			}
			const auto shift = static_cast<int32_t>(stack[--top].v);
			CalcValue& a = stack[top - 1u];
			const auto bits_value = static_cast<uint32_t>(static_cast<int32_t>(a.v));
			uint32_t r = 0;
			if (shift >= 0) {
				r = shift >= 32 ? 0u : bits_value << static_cast<uint32_t>(shift);
			} else {
				r = shift <= -32 ? 0u : bits_value >> static_cast<uint32_t>(-shift);
			}
			a = {static_cast<double>(static_cast<int32_t>(r)), kInt};
			break;
		}
		case CalcOp::Dup:
			if (!need(1u) || top >= kCalculatorStackDepth) {
				return false;
			}
			stack[top] = stack[top - 1u];
			++top;
			break;
		case CalcOp::Exch:
			if (!need(2u)) {
				return false;
			}
			std::swap(stack[top - 1u], stack[top - 2u]);
			break;
		case CalcOp::Pop:
			if (!need(1u)) {
				return false;
			}
			--top;
			break;
		case CalcOp::Copy: {
			if (!need(1u) || !is_int(1u) || stack[top - 1u].v < 0.0) {
				return false;
			}
			const auto count = static_cast<size_t>(stack[--top].v);
			if (!need(count) || top + count > kCalculatorStackDepth) {
				return false;
			}
			for (size_t k = 0; k < count; ++k) {
				stack[top + k] = stack[top - count + k];
			}
			top += count;
			break;
		}
		case CalcOp::Index: {
			if (!need(1u) || !is_int(1u) || stack[top - 1u].v < 0.0) {
				return false;
			}
			const auto depth = static_cast<size_t>(stack[top - 1u].v);
			if (depth + 1u >= top) {
				return false;
			}
			stack[top - 1u] = stack[top - 2u - depth];
			break;
		}
		case CalcOp::Roll: {
			if (!need(2u) || !is_int(1u) || !is_int(2u) || stack[top - 2u].v < 0.0) {
				return false;
			}
			const auto shift = static_cast<int64_t>(stack[top - 1u].v);
			const auto count = static_cast<size_t>(stack[top - 2u].v);
			top -= 2u;
			if (!need(count)) {
				return false;
			}
			if (count > 0u) {
				const int64_t c = static_cast<int64_t>(count);
				const auto by = static_cast<size_t>(((shift % c) + c) % c);
				std::rotate(stack.begin() + static_cast<std::ptrdiff_t>(top - count),
					stack.begin() + static_cast<std::ptrdiff_t>(top - by), stack.begin() + static_cast<std::ptrdiff_t>(top));
			}
			break;
		}
		}
	}
	if (top < n) {
		return false;
	}
	for (size_t j = 0; j < n; ++j) {
		const CalcValue& v = stack[top - n + j];
		if (v.kind == kBool) {
			return false;
		}
		out[j] = std::clamp(v.v, range[2u * j], range[2u * j + 1u]);
	}
	return true;
}

void Function::Node::fail(std::span<double> out) const noexcept
{
	for (size_t j = 0; j < n; ++j) {
		out[j] = range.empty() ? 0.0 : range[2u * j];
	}
}

bool Function::Node::evaluate(std::span<const double> in, std::span<double> out) const noexcept
{
	std::array<double, kMaxFunctionInputs> x{};
	for (size_t i = 0; i < m; ++i) {
		x[i] = std::clamp(in[i], domain[2u * i], domain[2u * i + 1u]);
	}
	switch (type) {
	case Type::Sampled:
		evaluate_sampled(x, out);
		return true;
	case Type::Exponential: {
		const double v = std::pow(x[0], exponent);
		if (!std::isfinite(v)) {
			fail(out); // outside 7.10.3's domain: a negative input to a fractional /N, or 0 to a negative one
			return false;
		}
		for (size_t j = 0; j < n; ++j) {
			const double y = c0[j] + v * (c1[j] - c0[j]);
			out[j] = range.empty() ? y : std::clamp(y, range[2u * j], range[2u * j + 1u]);
		}
		return true;
	}
	case Type::Stitching: {
		const size_t k = children.size();
		size_t piece = k - 1u;
		for (size_t i = 0; i < bounds.size(); ++i) {
			if (x[0] < bounds[i]) {
				piece = i;
				break;
			}
		}
		const double lo = piece == 0u ? domain[0] : bounds[piece - 1u];
		const double hi = piece == k - 1u ? domain[1] : bounds[piece];
		const double e = interpolate(x[0], lo, hi, encode[2u * piece], encode[2u * piece + 1u]);
		const std::array<double, 1> sub_in{e};
		// A failing piece has already written its own failure values (its /Range lows); they stand, clipped to this
		// function's /Range like any output, and the failure is reported.
		const bool ok = children[piece]->evaluate(sub_in, out);
		if (!range.empty()) {
			for (size_t j = 0; j < n; ++j) {
				out[j] = std::clamp(out[j], range[2u * j], range[2u * j + 1u]);
			}
		}
		return ok;
	}
	case Type::Calculator:
		if (!run_calculator(x, out)) {
			fail(out);
			return false;
		}
		return true;
	case Type::Array: {
		// Each function of an array takes the input as given and clips it to its own /Domain.
		bool ok = true;
		for (size_t j = 0; j < n && ok; ++j) {
			ok = children[j]->evaluate(in, out.subspan(j, 1u));
		}
		if (!ok) {
			for (size_t j = 0; j < n; ++j) {
				children[j]->fail(out.subspan(j, 1u)); // every output its failure value, never a partial result
			}
		}
		return ok;
	}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Function
// ---------------------------------------------------------------------------------------------------------------------

Function::Function(std::shared_ptr<const Node> root, size_t table_bytes) noexcept
	: root_(std::move(root))
	, table_bytes_(table_bytes)
{
}

Function::~Function() = default;

PdfResult<std::shared_ptr<const Function>> Function::parse(
	const Document& document, const PdfValue& value, size_t* decoded) noexcept
{
	try {
		ParseContext context{document};
		auto root = parse_node(context, value, 0u);
		if (decoded != nullptr) {
			*decoded += context.decoded;
		}
		if (!root.has_value()) {
			return root.error();
		}
		if ((*root)->n == 0u) {
			return damaged("Function gives no outputs");
		}
		return std::make_shared<const Function>(std::move(*root), context.bytes);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a PDF function");
	}
}

size_t Function::inputs() const noexcept
{
	return root_->m;
}

size_t Function::outputs() const noexcept
{
	return root_->n;
}

size_t Function::table_bytes() const noexcept
{
	return table_bytes_;
}

bool Function::evaluate(std::span<const double> in, std::span<double> out) const noexcept
{
	++t_evaluations;
	return root_->evaluate(in, out);
}

namespace detail {

size_t function_evaluations_for_testing() noexcept
{
	return t_evaluations;
}

} // namespace detail

} // namespace recto::pdf
