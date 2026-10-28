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

// A font this reader cannot use for one of these reasons is drawn with the fallback; anything else (memory, a decoder
// or recursion limit, an internal error) stops the page, as every other resolver failure in the interpreter does.
[[nodiscard]] bool recoverable(ErrorCode code) noexcept
{
	return code == ErrorCode::DamagedFormat || code == ErrorCode::UnsupportedFeature || code == ErrorCode::ResourceLimit;
}

// A resolved entry: the document's cached value for a reference, shared without a copy (D01 T01 section 11), else the
// entry itself, which its container must outlive.
struct Resolved {
	std::shared_ptr<const PdfValue> owner{};
	const PdfValue* value{nullptr};

	[[nodiscard]] bool has_value() const noexcept
	{
		return value != nullptr;
	}
	explicit operator bool() const noexcept
	{
		return value != nullptr;
	}
	const PdfValue& operator*() const noexcept
	{
		return *value;
	}
	const PdfValue* operator->() const noexcept
	{
		return value;
	}
};

[[nodiscard]] Resolved resolved(const Document& document, const PdfValue* value, std::optional<PdfError>& fatal);

inline constexpr size_t kMaxIndirectDifferences = 1'024u;

// A direct copy of an encoding dictionary with its `/BaseEncoding` and `/Differences` (and the array's elements)
// resolved, since `SimpleEncoding::build` reads only direct values (9.6.6.1).
[[nodiscard]] PdfValue direct_encoding(const Document& document, const PdfValue& encoding, std::optional<PdfError>& fatal)
{
	const PdfDict* dict = encoding.as_dict();
	if (dict == nullptr) {
		return encoding;
	}
	PdfDict out = *dict;
	if (const Resolved base = resolved(document, dict->get("BaseEncoding"), fatal)) {
		out.set("BaseEncoding", *base);
	}
	if (const Resolved differences = resolved(document, dict->get("Differences"), fatal)) {
		if (const PdfArray* array = differences->as_array()) {
			PdfArray direct;
			direct.reserve(std::min(array->size(), kMaxDifferencesEntries + 1u));
			size_t references = 0;
			for (const PdfValue& element : *array) {
				if (direct.size() > kMaxDifferencesEntries) {
					break; // past the limit: `SimpleEncoding::build` refuses the array by its length
				}
				if (!element.is_ref()) {
					direct.push_back(element);
					continue;
				}
				// Only a number or a name means anything here (Table 114); a reference to anything else is kept as
				// null, and past kMaxIndirectDifferences references (a real array has at most 512 entries that
				// matter) the rest are not resolved at all.
				Resolved value;
				if (++references <= kMaxIndirectDifferences) {
					value = resolved(document, &element, fatal);
				} else if (references == kMaxIndirectDifferences + 1u) {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "/Differences holds more indirect entries than its limit");
				}
				const std::optional<std::string_view> name = value.has_value() ? value->as_name() : std::nullopt;
				const bool meaningful = value.has_value() &&
					(value->as_number().has_value() || (name.has_value() && name->size() <= kMaxGlyphNameBytes));
				direct.push_back(meaningful ? PdfValue(*value) : PdfValue(PdfNull{}));
			}
			out.set("Differences", PdfValue(std::move(direct)));
		}
	}
	return PdfValue(std::move(out));
}

// `value` resolved, or none when it is missing or damaged; `fatal` takes any failure that must stop the page.
[[nodiscard]] Resolved resolved(const Document& document, const PdfValue* value, std::optional<PdfError>& fatal)
{
	if (value == nullptr) {
		return {};
	}
	if (!value->is_ref()) {
		return Resolved{nullptr, value};
	}
	auto r = document.resolve_object_shared(*value->as_ref());
	if (!r.has_value()) {
		if (!recoverable(r.error().code) && !fatal.has_value()) {
			fatal = r.error();
		}
		return {};
	}
	Resolved out{std::move(*r), nullptr};
	out.value = out.owner.get();
	return out;
}

[[nodiscard]] std::optional<double> finite_number(const Resolved& value) noexcept
{
	const std::optional<double> n = value.has_value() ? value->as_number() : std::nullopt;
	return n.has_value() && std::isfinite(*n) ? n : std::nullopt;
}

// Four finite numbers, normalised so the minimum comes first (9.8: a /FontBBox may be given in any corner order).
[[nodiscard]] std::optional<PdfRect> box_of(const Document& document, const PdfValue* value, std::optional<PdfError>& fatal)
{
	const Resolved array_value = resolved(document, value, fatal);
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
[[nodiscard]] std::optional<std::string_view> name_of(const Resolved& value) noexcept
{
	return value.has_value() ? value->as_name() : std::nullopt;
}
std::optional<std::string_view> name_of(Resolved&&) = delete; // a view into a temporary dangles

} // namespace

struct TextFontBuilder {
	// The face's index in `font.faces_`, added on first use; kNoFace past 255 faces.
	static uint8_t face_index(TextFont& font, const std::shared_ptr<const LoadedFont>& face)
	{
		for (size_t i = 0; i < font.faces_.size(); ++i) {
			if (font.faces_[i] == face) {
				return static_cast<uint8_t>(i);
			}
		}
		if (font.faces_.size() >= TextGlyph::kNoFace) {
			return TextGlyph::kNoFace;
		}
		font.faces_.push_back(face);
		return static_cast<uint8_t>(font.faces_.size() - 1u);
	}

	// `glyph` of `face`, fitted by `width_scale`, with its ink box: none and blank for an outline that paints nothing,
	// none for one the face refuses to read (the run's conservative box then holds it). OutOfMemory is passed on.
	static PdfResult<TextGlyph> face_glyph(TextFont& font, const std::shared_ptr<const LoadedFont>& face, uint16_t glyph,
		double width_scale)
	{
		TextGlyph out;
		out.face = face_index(font, face);
		out.glyph = glyph;
		out.width_scale = width_scale;
		if (out.face == TextGlyph::kNoFace) {
			out.blank = true;
			return out;
		}
		auto ink = face->ink_box(glyph);
		if (!ink.has_value()) {
			if (ink.error().code == ErrorCode::OutOfMemory) {
				return ink.error();
			}
			return out;
		}
		if (!ink->has_value()) {
			out.blank = true;
			return out;
		}
		const PdfRect& box = **ink;
		out.ink = PdfRect{box.x_min * width_scale, box.y_min, box.x_max * width_scale, box.y_max};
		return out;
	}

	// Each code drawn through a standard font's Windows face (section 27): by its effective glyph name's Unicode
	// scalar, or for Symbol by the name's code in Symbol's built-in encoding (what its symbol `cmap` is keyed by),
	// fitted horizontally to the code's advance wherever the face's own advance differs. A code no face draws is
	// advance-only; the face's absence is recorded once.
	static PdfResult<void> standard_glyphs(TextFont& font, Standard14Font standard, const SimpleEncoding& encoding)
	{
		bool absence_recorded = false;
		for (size_t code = 0; code < 256u; ++code) {
			TextGlyph& slot = font.glyphs_[code];
			slot = TextGlyph{};
			slot.blank = true;
			const std::string_view name = encoding.name(static_cast<uint8_t>(code));
			if (name.empty() || name == ".notdef") {
				continue;
			}
			std::optional<char32_t> point;
			if (standard == Standard14Font::Symbol) {
				if (const Standard14Glyph* g = standard14_glyph_by_name(standard, name); g != nullptr && g->code >= 0) {
					point = static_cast<char32_t>(g->code);
				}
			} else {
				point = drawable_unicode(name, standard == Standard14Font::ZapfDingbats);
			}
			if (!point.has_value()) {
				continue;
			}
			auto drawn = standard14_face_glyph(standard, *point);
			if (!drawn.has_value()) {
				if (drawn.error().code == ErrorCode::OutOfMemory) {
					return drawn.error();
				}
				if (!absence_recorded) {
					record_diagnostic(drawn.error()); // no face loads: the text is positioned, not painted
					absence_recorded = true;
				}
				continue;
			}
			if (!drawn->has_value()) {
				continue;
			}
			const Standard14FaceGlyph& face_glyph_found = **drawn;
			double scale = 1.0;
			auto advance = face_glyph_found.face->advance(face_glyph_found.glyph);
			if (!advance.has_value()) {
				if (advance.error().code == ErrorCode::OutOfMemory) {
					return advance.error();
				}
			} else if (*advance > 0.0 && font.widths_[code] > 0.0) {
				const double fitted = font.widths_[code] / *advance;
				if (std::abs(fitted - 1.0) > 1e-3) {
					scale = fitted; // the substitute's glyph stretched or narrowed to the advance the PDF gives it
				}
			}
			RECTO_PDF_TRY(slot, face_glyph(font, face_glyph_found.face, face_glyph_found.glyph, scale));
		}
		return {};
	}

	// A standard font's AFM codes as its built-in encoding (9.6.6.1).
	static GlyphNames afm_encoding(Standard14Font standard)
	{
		const Standard14Metrics& metrics = standard14_metrics(standard);
		GlyphNames built_in{};
		for (size_t code = 0; code < 256u; ++code) {
			const int16_t index = metrics.code_glyph[code];
			if (index >= 0) {
				built_in[code] = std::string(metrics.glyphs[static_cast<size_t>(index)].name);
			}
		}
		return built_in;
	}

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
		// Drawn through Helvetica's face by its AFM codes; a face that cannot load leaves the text positioned only.
		const GlyphNames built_in = afm_encoding(Standard14Font::Helvetica);
		auto encoding = SimpleEncoding::build(nullptr, false, &built_in);
		if (encoding.has_value()) {
			if (auto drawn = standard_glyphs(*font, Standard14Font::Helvetica, *encoding);
				!drawn.has_value() && drawn.error().code == ErrorCode::OutOfMemory) {
				throw std::bad_alloc();
			}
		} else if (encoding.error().code == ErrorCode::OutOfMemory) {
			throw std::bad_alloc();
		}
		return font;
	}

	// `/FirstChar` and `/Widths` over `missing` (each width times `factor`); false when the font gives no `/Widths`.
	static bool widths(const Document& document, const PdfDict& dict, double factor, double missing, TextFont& font,
		std::optional<PdfError>& fatal)
	{
		font.widths_.fill(missing);
		const Resolved list = resolved(document, dict.get("Widths"), fatal);
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

	// A simple font's embedded program (ISO 32000-1 9.9, Table 126): the face it draws from, the program its glyph
	// names and built-in encoding come from (a Type 1 or CFF program, or an OpenType program's wrapped `CFF ` table),
	// and a TrueType or OpenType program's `cmap`.
	struct Program {
		std::shared_ptr<const LoadedFont> face;
		std::shared_ptr<const LoadedFont> names;
		std::optional<TrueTypeTables> tables;
	};

	// The descriptor's program by its key (section 29's loaders), none when it embeds none, a typed error when the
	// loaders refuse it.
	static PdfResult<std::optional<Program>> program(const Document& document, const PdfDict& descriptor,
		std::optional<PdfError>& fatal)
	{
		for (const std::string_view key : {std::string_view("FontFile"), std::string_view("FontFile2"), std::string_view("FontFile3")}) {
			const Resolved file = resolved(document, descriptor.get(key), fatal);
			if (!file.has_value() || file->is_null()) {
				continue;
			}
			const PdfStream* stream = file->as_stream();
			if (stream == nullptr) {
				return make_pdf_error(ErrorCode::DamagedFormat, "Font program is not a stream");
			}
			RECTO_PDF_TRY(std::vector<uint8_t> data, document.decode_stream(*stream, kMaxDecodedStreamBytes));
			const auto bytes = std::make_shared<const std::vector<uint8_t>>(std::move(data));
			Program out;
			if (key == "FontFile") {
				const auto length = [&](std::string_view entry) {
					const std::optional<double> n = finite_number(resolved(document, stream->dict.get(entry), fatal));
					return n.has_value() && *n >= 0.0 && *n <= static_cast<double>(bytes->size()) ? static_cast<size_t>(*n) : size_t{0};
				};
				RECTO_PDF_TRY(out.face, load_charstring_font(bytes, CharstringFormat::Type1,
					Type1Lengths{length("Length1"), length("Length2"), length("Length3")}));
				out.names = out.face;
				return std::optional<Program>(std::move(out));
			}
			if (key == "FontFile2") {
				RECTO_PDF_TRY(out.face, load_embedded_font(bytes, FontProgramFormat::TrueType));
				RECTO_PDF_TRY_REF(tables, TrueTypeTables::parse(*bytes));
				out.tables = std::move(tables);
				return std::optional<Program>(std::move(out));
			}
			const Resolved subtype_value = resolved(document, stream->dict.get("Subtype"), fatal);
			const std::optional<std::string_view> subtype = name_of(subtype_value);
			if (subtype == std::optional<std::string_view>("Type1C")) {
				RECTO_PDF_TRY(out.face, load_charstring_font(bytes, CharstringFormat::Type1C));
				out.names = out.face;
				return std::optional<Program>(std::move(out));
			}
			if (subtype == std::optional<std::string_view>("OpenType")) {
				RECTO_PDF_TRY(out.face, load_embedded_font(bytes, FontProgramFormat::OpenType));
				RECTO_PDF_TRY_REF(tables, TrueTypeTables::parse(*bytes));
				out.tables = std::move(tables);
				// The wrapped CFF's charset and encoding name the glyphs when the program has no `cmap` to read.
				if (const auto cff = opentype_cff_table(*bytes)) {
					auto names = load_charstring_font(std::make_shared<const std::vector<uint8_t>>(cff->begin(), cff->end()),
						CharstringFormat::Type1C);
					if (names.has_value()) {
						out.names = std::move(*names);
					} else if (names.error().code == ErrorCode::OutOfMemory) {
						return names.error();
					}
				}
				return std::optional<Program>(std::move(out));
			}
			return make_pdf_error(ErrorCode::UnsupportedFeature, "Simple font's /FontFile3 /Subtype is not one this reader reads");
		}
		return std::optional<Program>{};
	}

	// The standard font drawn for a simple font without a usable program: by its descriptor's fixed-pitch and serif
	// flags, its weight (`/FontWeight` 600 or more, or ForceBold) and slant (`/ItalicAngle`, or the Italic flag). The
	// symbolic flag selects the encoding, not the face: Symbol's face would draw Greek for a Latin name.
	static Standard14Font substitute(uint32_t flags, double weight, double italic_angle) noexcept
	{
		const bool fixed = (flags & 1u) != 0u;
		const bool serif = (flags & 2u) != 0u;
		const bool italic = (flags & 64u) != 0u || italic_angle != 0.0;
		const bool bold = (flags & (1u << 18u)) != 0u || weight >= 600.0;
		if (fixed) {
			return bold ? (italic ? Standard14Font::CourierBoldOblique : Standard14Font::CourierBold)
						: (italic ? Standard14Font::CourierOblique : Standard14Font::Courier);
		}
		if (serif) {
			return bold ? (italic ? Standard14Font::TimesBoldItalic : Standard14Font::TimesBold)
						: (italic ? Standard14Font::TimesItalic : Standard14Font::TimesRoman);
		}
		return bold ? (italic ? Standard14Font::HelveticaBoldOblique : Standard14Font::HelveticaBold)
					: (italic ? Standard14Font::HelveticaOblique : Standard14Font::Helvetica);
	}

	static PdfResult<std::shared_ptr<const TextFont>> simple(const Document& document, const PdfDict& dict, bool truetype,
		std::optional<PdfError>& fatal)
	{
		auto font = std::make_shared<TextFont>();
		font->kind_ = TextFontKind::Simple;
		const Resolved descriptor_value = resolved(document, dict.get("FontDescriptor"), fatal);
		const PdfDict* descriptor = descriptor_value.has_value() ? descriptor_value->as_dict() : nullptr;
		const double missing = descriptor != nullptr
			? finite_number(resolved(document, descriptor->get("MissingWidth"), fatal)).value_or(0.0) / 1000.0
			: 0.0;
		const Resolved base_font_value = resolved(document, dict.get("BaseFont"), fatal); // kept alive for the view
		const std::optional<std::string_view> base_font = name_of(base_font_value);
		const std::optional<Standard14Font> standard = base_font.has_value() ? standard14_from_base_font(*base_font)
																			  : std::nullopt;
		std::optional<PdfRect> box = descriptor != nullptr ? box_of(document, descriptor->get("FontBBox"), fatal)
														   : std::nullopt;
		if (box.has_value()) {
			box = scaled(*box, 0.001);
		}
		// The descriptor's flags (Table 121): a 32-bit integer, else ignored with a diagnostic.
		std::optional<uint32_t> flags;
		double weight = 400.0;
		double italic_angle = 0.0;
		if (descriptor != nullptr) {
			const std::optional<double> value = finite_number(resolved(document, descriptor->get("Flags"), fatal));
			if (value.has_value() && *value == std::floor(*value) && *value >= 0.0 && *value <= 4294967295.0) {
				flags = static_cast<uint32_t>(*value);
			} else if (value.has_value()) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "Font descriptor /Flags is not a 32-bit integer");
			}
			weight = finite_number(resolved(document, descriptor->get("FontWeight"), fatal)).value_or(400.0);
			italic_angle = finite_number(resolved(document, descriptor->get("ItalicAngle"), fatal)).value_or(0.0);
		}
		const bool symbolic = flags.has_value() ? (*flags & 4u) != 0u
												: standard == Standard14Font::Symbol || standard == Standard14Font::ZapfDingbats;
		const bool nonsymbolic = flags.has_value() && (*flags & 32u) != 0u;
		// The embedded program; one the loaders refuse is drawn in a substitute face, its error recorded.
		std::optional<Program> program;
		if (descriptor != nullptr) {
			auto loaded = TextFontBuilder::program(document, *descriptor, fatal);
			if (!loaded.has_value()) {
				if (!recoverable(loaded.error().code)) {
					return loaded.error();
				}
				record_diagnostic(loaded.error());
			} else {
				program = std::move(*loaded);
			}
		}
		// The effective encoding over the built-in one: the program's, or for a standard font its AFM codes.
		GlyphNames afm{};
		const GlyphNames* built_in = nullptr;
		if (program.has_value() && program->names != nullptr) {
			built_in = program->names->built_in_encoding();
		} else if (!program.has_value() && standard.has_value()) {
			afm = afm_encoding(*standard);
			built_in = &afm;
		}
		std::optional<PdfValue> encoding_value;
		if (const Resolved encoding_entry = resolved(document, dict.get("Encoding"), fatal)) {
			encoding_value = direct_encoding(document, *encoding_entry, fatal);
		}
		RECTO_PDF_TRY(const SimpleEncoding encoding,
			SimpleEncoding::build(encoding_value.has_value() ? &*encoding_value : nullptr, symbolic, built_in));
		const bool has_widths = widths(document, dict, 0.001, missing, *font, fatal);
		if (program.has_value()) {
			// Each code's glyph in the program: a TrueType font through its program's `cmap` (9.6.6.4); a Type 1 font,
			// including one embedding an OpenType CFF program, by glyph name under the Type 1 encoding rules (9.9),
			// its `cmap` only when the program names no glyphs.
			const bool by_cmap = program->tables.has_value() && (truetype || program->names == nullptr);
			font->owned_face_bytes_ = program->face->memory_bytes() +
				(program->names != nullptr && program->names != program->face ? program->names->memory_bytes() : 0u);
			for (size_t code = 0; code < 256u; ++code) {
				const uint8_t c = static_cast<uint8_t>(code);
				uint16_t glyph = 0;
				if (by_cmap) {
					glyph = simple_truetype_glyph(*program->tables, encoding, symbolic, nonsymbolic, c);
				} else if (program->names != nullptr) {
					glyph = program->names->glyph_for_name(encoding.name(c)).value_or(0u);
				}
				if (!has_widths) {
					// No /Widths: the program's own advances (9.6.2.1 makes /Widths required, readers fall back).
					auto advance = program->face->advance(glyph);
					if (advance.has_value()) {
						font->widths_[code] = *advance;
					} else if (advance.error().code == ErrorCode::OutOfMemory) {
						return advance.error();
					}
				}
				RECTO_PDF_TRY(font->glyphs_[code], face_glyph(*font, program->face, glyph, 1.0));
			}
			if (!has_widths) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "Simple font has no /Widths; positioned by its program's advances");
			}
		} else {
			// No usable program: the standard font it names, else the substitute its descriptor matches.
			const Standard14Font face_font = standard.has_value() ? *standard
				: substitute(flags.value_or(0u), weight, italic_angle);
			if (!has_widths) {
				// The face font's AFM widths by the effective encoding's glyph names (9.6.2.2, 9.6.6).
				for (size_t code = 0; code < 256u; ++code) {
					const Standard14Glyph* glyph = standard14_glyph_by_name(face_font, encoding.name(static_cast<uint8_t>(code)));
					font->widths_[code] = glyph != nullptr ? glyph->width / 1000.0 : missing;
				}
				if (!standard.has_value()) {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "Simple font has no /Widths; positioned with its substitute's widths");
				}
			}
			RECTO_PDF_RETURN_IF_ERROR(standard_glyphs(*font, face_font, encoding));
			if (!box.has_value() || !has_extent(*box)) {
				const Standard14Metrics& metrics = standard14_metrics(face_font);
				box = scaled(PdfRect{static_cast<double>(metrics.font_box[0]), static_cast<double>(metrics.font_box[1]),
								 static_cast<double>(metrics.font_box[2]), static_cast<double>(metrics.font_box[3])},
					0.001);
			}
		}
		if ((!box.has_value() || !has_extent(*box)) && standard.has_value()) {
			// A Standard 14 font whose descriptor gives no box, /Widths or not: the AFM's FontBBox.
			const Standard14Metrics& metrics = standard14_metrics(*standard);
			box = scaled(PdfRect{static_cast<double>(metrics.font_box[0]), static_cast<double>(metrics.font_box[1]),
							 static_cast<double>(metrics.font_box[2]), static_cast<double>(metrics.font_box[3])},
				0.001);
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
		const Resolved matrix_value = resolved(document, dict.get("FontMatrix"), fatal);
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
		const std::optional<PdfRect> declared = box_of(document, dict.get("FontBBox"), fatal);
		if (declared.has_value() && declared->x_min == 0.0 && declared->y_min == 0.0 && declared->x_max == 0.0 &&
			declared->y_max == 0.0) {
			font->bbox_known_ = false; // all zeros: no assumption about the glyphs (Table 112)
		}
		if (const std::optional<PdfRect> box = declared; box.has_value() && font->bbox_known_) {
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
		const SharedObjectResolver shared = [&document](IndirectRef ref) { return document.resolve_object_shared(ref); };
		std::optional<PdfError> program_error;
		RECTO_PDF_TRY(std::shared_ptr<const Type0Font> type0, load_type0_font(dict, resolver, &shared, &program_error));
		auto font = std::make_shared<TextFont>();
		font->kind_ = TextFontKind::Composite;
		font->vertical_ = type0->cmap().vertical();
		if (type0->font() != nullptr) {
			font->faces_.push_back(type0->font()); // counted in the composite font's memory
		} else if (program_error.has_value()) {
			// Only the program failed: the codes, CIDs and widths still position the text, drawn advance-only.
			record_diagnostic(*program_error);
		} else {
			// A CIDFont that embeds no program: positioned, drawn once section 17 substitutes it.
			record_diagnostic(make_pdf_error(ErrorCode::UnsupportedFeature, "CIDFont embeds no program; its text is positioned but not drawn"));
		}
		font->type0_ = std::move(type0);
		// The box of the one descendant's descriptor (9.7.4.1).
		const Resolved descendants = resolved(document, dict.get("DescendantFonts"), fatal);
		const PdfArray* list = descendants.has_value() ? descendants->as_array() : nullptr;
		if (list != nullptr && !list->empty()) {
			const Resolved descendant = resolved(document, &(*list)[0], fatal);
			const PdfDict* cid_font = descendant.has_value() ? descendant->as_dict() : nullptr;
			const Resolved descriptor = cid_font != nullptr
				? resolved(document, cid_font->get("FontDescriptor"), fatal)
				: Resolved{};
			const PdfDict* d = descriptor.has_value() ? descriptor->as_dict() : nullptr;
			if (const std::optional<PdfRect> box = d != nullptr ? box_of(document, d->get("FontBBox"), fatal) : std::nullopt;
				box.has_value() && has_extent(*box)) {
				font->bbox_ = scaled(*box, 0.001);
			}
		}
		return std::shared_ptr<const TextFont>(std::move(font));
	}
};

PdfResult<std::shared_ptr<const TextFont>> TextFont::fallback() noexcept
{
	try {
		// Built per call (256 table lookups, only on a fallback path), so memory running out is reported every time.
		return std::shared_ptr<const TextFont>(TextFontBuilder::helvetica());
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory building the fallback font");
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
				return fallback(); // OutOfMemory building it is passed on
			}
			return made;
		};
		const Resolved value = resolved(document, &font, fatal);
		const PdfDict* dict = value.has_value() ? value->as_dict() : nullptr;
		if (dict == nullptr) {
			return finish(make_pdf_error(ErrorCode::DamagedFormat, "Font resource is not a font dictionary"));
		}
		const Resolved subtype_value = resolved(document, dict->get("Subtype"), fatal); // kept alive for the view
		const std::optional<std::string_view> subtype = name_of(subtype_value);
		if (subtype == std::optional<std::string_view>("Type0")) {
			return finish(TextFontBuilder::composite(document, *dict, fatal));
		}
		if (subtype == std::optional<std::string_view>("Type3")) {
			return finish(TextFontBuilder::type3(document, *dict, fatal));
		}
		if (subtype == std::optional<std::string_view>("Type1") || subtype == std::optional<std::string_view>("MMType1") ||
			subtype == std::optional<std::string_view>("TrueType")) {
			return finish(TextFontBuilder::simple(document, *dict, subtype == std::optional<std::string_view>("TrueType"), fatal));
		}
		// Any other /Subtype, or none, is not a font this reader positions (9.5, Table 110): the fallback, recorded.
		return finish(subtype.has_value()
				? make_pdf_error(ErrorCode::UnsupportedFeature, "Font /Subtype is not one this reader reads")
				: make_pdf_error(ErrorCode::DamagedFormat, "Font has no /Subtype"));
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
		out.glyph = glyph.glyph;
		return out;
	}
	out.code = bytes[0];
	out.length = 1u;
	out.cid = bytes[0];
	out.w0 = widths_[bytes[0]];
	out.word_space = bytes[0] == 32u;
	return out;
}

TextGlyph TextFont::glyph(const TextCode& code) const noexcept
{
	if (kind_ == TextFontKind::Composite) {
		TextGlyph out;
		if (faces_.empty()) {
			out.blank = true; // no program to draw it: advance-only
			return out;
		}
		out.face = 0u;
		out.glyph = code.glyph;
		out.ink_known = false; // read from the face by the interpreter, once per glyph on the page
		return out;
	}
	return glyphs_[code.code & 0xFFu];
}

} // namespace recto::pdf
