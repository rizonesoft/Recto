// A font resource's codes and widths for positioning text (ISO 32000-1 sections 9.2.4, 9.6 and 9.7; D01 T02 section 42).
#include "text_font.h"

#include "document.h"
#include "font.h"
#include "font_encoding.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <string>
#include <utility>

namespace recto::pdf {

namespace {

// A font this reader cannot use for one of these reasons is drawn with the fallback; anything else stops the page.
[[nodiscard]] bool recoverable(ErrorCode code) noexcept
{
	return code == ErrorCode::DamagedFormat || code == ErrorCode::UnsupportedFeature ||
		code == ErrorCode::ResourceLimit || code == ErrorCode::DecompressionLimitExceeded ||
		code == ErrorCode::RecursionLimitExceeded;
}

// `value` resolved, or none when it is missing or damaged; `fatal` takes any failure that must stop the page.
[[nodiscard]] std::optional<PdfValue> resolved(const Document& document, const PdfValue* value, std::optional<PdfError>& fatal)
{
	if (value == nullptr) {
		return std::nullopt;
	}
	auto r = document.resolve_value(*value);
	if (!r.has_value()) {
		if (!recoverable(r.error().code) && !fatal.has_value()) {
			fatal = r.error();
		}
		return std::nullopt;
	}
	return std::move(*r);
}

[[nodiscard]] std::optional<double> finite_number(const std::optional<PdfValue>& value) noexcept
{
	const std::optional<double> n = value.has_value() ? value->as_number() : std::nullopt;
	return n.has_value() && std::isfinite(*n) ? n : std::nullopt;
}

// Four finite numbers, normalised so the minimum comes first (9.8: a /FontBBox may be given in any corner order).
[[nodiscard]] std::optional<PdfRect> box_of(const Document& document, const PdfValue* value, std::optional<PdfError>& fatal)
{
	const std::optional<PdfValue> array_value = resolved(document, value, fatal);
	const PdfArray* array = array_value.has_value() ? array_value->as_array() : nullptr;
	if (array == nullptr || array->size() != 4u) {
		return std::nullopt;
	}
	double v[4]{};
	for (size_t i = 0; i < 4u; ++i) {
		const std::optional<double> n = finite_number(resolved(document, &(*array)[i], fatal));
		if (!n.has_value()) {
			return std::nullopt;
		}
		v[i] = *n;
	}
	return PdfRect{std::min(v[0], v[2]), std::min(v[1], v[3]), std::max(v[0], v[2]), std::max(v[1], v[3])};
}

[[nodiscard]] PdfRect scaled(const PdfRect& box, double factor) noexcept
{
	return PdfRect{box.x_min * factor, box.y_min * factor, box.x_max * factor, box.y_max * factor};
}

// A box with any extent: an empty or degenerate one is no use as a run's conservative bounds.
[[nodiscard]] bool has_extent(const PdfRect& box) noexcept
{
	return box.x_max > box.x_min && box.y_max > box.y_min;
}

// A view into `value`, which must outlive it (never a temporary).
[[nodiscard]] std::optional<std::string_view> name_of(const std::optional<PdfValue>& value) noexcept
{
	return value.has_value() ? value->as_name() : std::nullopt;
}
std::optional<std::string_view> name_of(std::optional<PdfValue>&&) = delete; // a view into a temporary dangles

} // namespace

struct TextFontBuilder {
	static std::shared_ptr<TextFont> helvetica()
	{
		auto font = std::make_shared<TextFont>();
		font->kind_ = TextFontKind::Fallback;
		const Standard14Metrics& metrics = standard14_metrics(Standard14Font::Helvetica);
		for (size_t code = 0; code < 256u; ++code) {
			const Standard14Glyph* glyph = standard14_glyph_by_code(Standard14Font::Helvetica, static_cast<uint8_t>(code));
			font->widths_[code] = glyph != nullptr ? glyph->width / 1000.0 : 0.0;
		}
		const PdfRect box{static_cast<double>(metrics.font_box[0]), static_cast<double>(metrics.font_box[1]),
			static_cast<double>(metrics.font_box[2]), static_cast<double>(metrics.font_box[3])};
		font->bbox_ = scaled(box, 0.001);
		return font;
	}

	// `/FirstChar` and `/Widths` over `missing` (each width times `factor`); false when the font gives no `/Widths`.
	static bool widths(const Document& document, const PdfDict& dict, double factor, double missing, TextFont& font,
		std::optional<PdfError>& fatal)
	{
		font.widths_.fill(missing);
		const std::optional<PdfValue> list = resolved(document, dict.get("Widths"), fatal);
		const PdfArray* array = list.has_value() ? list->as_array() : nullptr;
		if (array == nullptr) {
			return false;
		}
		const std::optional<double> first = finite_number(resolved(document, dict.get("FirstChar"), fatal));
		const int64_t start = first.has_value() && *first >= 0.0 && *first <= 255.0 ? static_cast<int64_t>(*first) : 0;
		for (size_t i = 0; i < array->size() && start + static_cast<int64_t>(i) <= 255; ++i) {
			const std::optional<double> w = finite_number(resolved(document, &(*array)[i], fatal));
			if (w.has_value()) {
				font.widths_[static_cast<size_t>(start) + i] = *w * factor;
			}
		}
		return true;
	}

	static PdfResult<std::shared_ptr<const TextFont>> simple(const Document& document, const PdfDict& dict,
		std::optional<PdfError>& fatal)
	{
		auto font = std::make_shared<TextFont>();
		font->kind_ = TextFontKind::Simple;
		const std::optional<PdfValue> descriptor_value = resolved(document, dict.get("FontDescriptor"), fatal);
		const PdfDict* descriptor = descriptor_value.has_value() ? descriptor_value->as_dict() : nullptr;
		const double missing = descriptor != nullptr
			? finite_number(resolved(document, descriptor->get("MissingWidth"), fatal)).value_or(0.0) / 1000.0
			: 0.0;
		const std::optional<PdfValue> base_font_value = resolved(document, dict.get("BaseFont"), fatal); // kept alive for the view
		const std::optional<std::string_view> base_font = name_of(base_font_value);
		const std::optional<Standard14Font> standard = base_font.has_value() ? standard14_from_base_font(*base_font)
																			  : std::nullopt;
		std::optional<PdfRect> box = descriptor != nullptr ? box_of(document, descriptor->get("FontBBox"), fatal)
														   : std::nullopt;
		if (box.has_value()) {
			box = scaled(*box, 0.001);
		}
		if (!widths(document, dict, 0.001, missing, *font, fatal)) {
			if (standard.has_value()) {
				// A Standard 14 font without /Widths: each code's AFM width by the effective encoding's glyph name
				// (9.6.2.2, 9.6.6), the built-in encoding being the AFM's own codes.
				const Standard14Metrics& metrics = standard14_metrics(*standard);
				GlyphNames built_in{};
				for (size_t code = 0; code < 256u; ++code) {
					const int16_t index = metrics.code_glyph[code];
					if (index >= 0) {
						built_in[code] = std::string(metrics.glyphs[static_cast<size_t>(index)].name);
					}
				}
				bool symbolic = *standard == Standard14Font::Symbol || *standard == Standard14Font::ZapfDingbats;
				if (descriptor != nullptr) {
					const std::optional<double> flags = finite_number(resolved(document, descriptor->get("Flags"), fatal));
					if (flags.has_value()) {
						symbolic = (static_cast<int64_t>(*flags) & 4) != 0;
					}
				}
				const std::optional<PdfValue> encoding_value = resolved(document, dict.get("Encoding"), fatal);
				RECTO_PDF_TRY(const SimpleEncoding encoding, SimpleEncoding::build(
					encoding_value.has_value() ? &*encoding_value : nullptr, symbolic, &built_in));
				for (size_t code = 0; code < 256u; ++code) {
					const Standard14Glyph* glyph = standard14_glyph_by_name(*standard, encoding.name(static_cast<uint8_t>(code)));
					font->widths_[code] = glyph != nullptr ? glyph->width / 1000.0 : missing;
				}
				if (!box.has_value()) {
					box = scaled(PdfRect{static_cast<double>(metrics.font_box[0]), static_cast<double>(metrics.font_box[1]),
									 static_cast<double>(metrics.font_box[2]), static_cast<double>(metrics.font_box[3])},
						0.001);
				}
			} else {
				// No /Widths and no standard metrics: Helvetica's widths position the text until section 43 reads
				// the program's own advances; the font's own box is kept.
				(void)make_pdf_error(ErrorCode::DamagedFormat, "Simple font has no /Widths; positioned with Helvetica's widths");
				const std::shared_ptr<TextFont> helvetica = TextFontBuilder::helvetica();
				font->widths_ = helvetica->widths_;
				if (!box.has_value()) {
					box = helvetica->bbox_;
				}
			}
		}
		if (box.has_value() && has_extent(*box)) {
			font->bbox_ = *box;
		}
		return std::shared_ptr<const TextFont>(std::move(font));
	}

	static PdfResult<std::shared_ptr<const TextFont>> type3(const Document& document, const PdfDict& dict,
		std::optional<PdfError>& fatal)
	{
		auto font = std::make_shared<TextFont>();
		font->kind_ = TextFontKind::Type3;
		double matrix[6] = {0.001, 0.0, 0.0, 0.001, 0.0, 0.0};
		const std::optional<PdfValue> matrix_value = resolved(document, dict.get("FontMatrix"), fatal);
		if (const PdfArray* array = matrix_value.has_value() ? matrix_value->as_array() : nullptr; array != nullptr &&
			array->size() == 6u) {
			double m[6]{};
			bool ok = true;
			for (size_t i = 0; i < 6u && ok; ++i) {
				const std::optional<double> n = finite_number(resolved(document, &(*array)[i], fatal));
				ok = n.has_value();
				m[i] = n.value_or(0.0);
			}
			if (ok) {
				std::copy(std::begin(m), std::end(m), std::begin(matrix));
			} else {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "Type 3 font /FontMatrix is not six numbers");
			}
		}
		// A glyph's displacement (w0, 0) in glyph space, through the font matrix (9.6.5).
		(void)widths(document, dict, matrix[0], 0.0, *font, fatal);
		if (const std::optional<PdfRect> box = box_of(document, dict.get("FontBBox"), fatal)) {
			const double xs[4] = {box->x_min, box->x_max, box->x_min, box->x_max};
			const double ys[4] = {box->y_min, box->y_min, box->y_max, box->y_max};
			// Plain extremes: PdfRect's constructor orders its corners, so it cannot hold an empty starting box.
			double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
			for (size_t i = 0; i < 4u; ++i) {
				const double x = matrix[0] * xs[i] + matrix[2] * ys[i] + matrix[4];
				const double y = matrix[1] * xs[i] + matrix[3] * ys[i] + matrix[5];
				x0 = std::min(x0, x);
				y0 = std::min(y0, y);
				x1 = std::max(x1, x);
				y1 = std::max(y1, y);
			}
			const PdfRect out{x0, y0, x1, y1};
			if (std::isfinite(out.x_min) && std::isfinite(out.y_min) && std::isfinite(out.x_max) &&
				std::isfinite(out.y_max) && has_extent(out)) {
				font->bbox_ = out;
			}
		}
		return std::shared_ptr<const TextFont>(std::move(font));
	}

	static PdfResult<std::shared_ptr<const TextFont>> composite(const Document& document, const PdfDict& dict,
		std::optional<PdfError>& fatal)
	{
		const IndirectObjectResolver resolver = [&document](IndirectRef ref) { return document.resolve_object(ref); };
		RECTO_PDF_TRY(std::shared_ptr<const Type0Font> type0, load_type0_font(dict, resolver));
		auto font = std::make_shared<TextFont>();
		font->kind_ = TextFontKind::Composite;
		font->vertical_ = type0->cmap().vertical();
		font->type0_ = std::move(type0);
		// The box of the one descendant's descriptor (9.7.4.1).
		const std::optional<PdfValue> descendants = resolved(document, dict.get("DescendantFonts"), fatal);
		const PdfArray* list = descendants.has_value() ? descendants->as_array() : nullptr;
		if (list != nullptr && !list->empty()) {
			const std::optional<PdfValue> descendant = resolved(document, &(*list)[0], fatal);
			const PdfDict* cid_font = descendant.has_value() ? descendant->as_dict() : nullptr;
			const std::optional<PdfValue> descriptor = cid_font != nullptr
				? resolved(document, cid_font->get("FontDescriptor"), fatal)
				: std::nullopt;
			const PdfDict* d = descriptor.has_value() ? descriptor->as_dict() : nullptr;
			if (const std::optional<PdfRect> box = d != nullptr ? box_of(document, d->get("FontBBox"), fatal) : std::nullopt;
				box.has_value() && has_extent(*box)) {
				font->bbox_ = scaled(*box, 0.001);
			}
		}
		return std::shared_ptr<const TextFont>(std::move(font));
	}
};

std::shared_ptr<const TextFont> TextFont::fallback() noexcept
{
	try {
		static const std::shared_ptr<const TextFont> helvetica = TextFontBuilder::helvetica();
		return helvetica;
	} catch (const std::bad_alloc&) {
		return std::make_shared<const TextFont>(); // the default widths of 0 and its default box; allocates less
	}
}

PdfResult<std::shared_ptr<const TextFont>> TextFont::resolve(const Document& document, const PdfValue& font) noexcept
{
	try {
		std::optional<PdfError> fatal;
		const auto finish = [&](PdfResult<std::shared_ptr<const TextFont>> made) -> PdfResult<std::shared_ptr<const TextFont>> {
			if (fatal.has_value()) {
				return *fatal;
			}
			if (!made.has_value()) {
				if (!recoverable(made.error().code)) {
					return made.error();
				}
				if (made.error().code == ErrorCode::UnsupportedFeature) {
					record_diagnostic(made.error()); // the other recoverable codes record themselves
				}
				return fallback();
			}
			return made;
		};
		const std::optional<PdfValue> value = resolved(document, &font, fatal);
		const PdfDict* dict = value.has_value() ? value->as_dict() : nullptr;
		if (dict == nullptr) {
			return finish(make_pdf_error(ErrorCode::DamagedFormat, "Font resource is not a font dictionary"));
		}
		const std::optional<PdfValue> subtype_value = resolved(document, dict->get("Subtype"), fatal); // kept alive for the view
		const std::optional<std::string_view> subtype = name_of(subtype_value);
		if (subtype == std::optional<std::string_view>("Type0")) {
			return finish(TextFontBuilder::composite(document, *dict, fatal));
		}
		if (subtype == std::optional<std::string_view>("Type3")) {
			return finish(TextFontBuilder::type3(document, *dict, fatal));
		}
		return finish(TextFontBuilder::simple(document, *dict, fatal));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading a font for text");
	}
}

TextCode TextFont::next(std::span<const uint8_t> bytes) const noexcept
{
	TextCode out;
	if (bytes.empty()) {
		return out;
	}
	if (kind_ == TextFontKind::Composite && type0_ != nullptr) {
		const Type0Glyph glyph = type0_->next(bytes);
		out.code = glyph.code;
		out.length = glyph.length;
		out.cid = glyph.cid;
		out.w0 = glyph.advance;
		out.w1 = glyph.vertical.w1y;
		out.vx = glyph.vertical.vx;
		out.vy = glyph.vertical.vy;
		out.word_space = glyph.length == 1u && glyph.code == 32u;
		return out;
	}
	out.code = bytes[0];
	out.length = 1u;
	out.cid = bytes[0];
	out.w0 = widths_[bytes[0]];
	out.word_space = bytes[0] == 32u;
	return out;
}

} // namespace recto::pdf
