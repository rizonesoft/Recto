// Unit tests for embedded font loading, `LoadedFont` and the `glyf` fallback (D01 T02 section 2). The fixtures in
// `tests/corpus/fonts/` are written by `scripts/build-corpus.py` from glyph definitions whose geometry is stated in
// that script (units per em 1000), so every expected advance, box and outline below is computed by hand from them.
#include "filters.h"
#include "font.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>

namespace recto::pdf {
namespace {

std::shared_ptr<const std::vector<uint8_t>> font_bytes(std::string_view name)
{
	std::ifstream in(recto::test::corpus_path(std::string("fonts/") + std::string(name)), std::ios::binary);
	EXPECT_TRUE(in.good()) << name;
	return std::make_shared<const std::vector<uint8_t>>(
		std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::shared_ptr<const LoadedFont> load_ok(std::shared_ptr<const std::vector<uint8_t>> bytes,
	FontProgramFormat format = FontProgramFormat::TrueType)
{
	auto font = load_embedded_font(std::move(bytes), format);
	EXPECT_TRUE(font.has_value()) << (font.has_value() ? "" : std::string(font.error().message.view()));
	return font.has_value() ? *font : nullptr;
}

// The font read by the glyf reader alone, as when DirectWrite rejects the program.
std::shared_ptr<const LoadedFont> load_through_glyf(std::shared_ptr<const std::vector<uint8_t>> bytes)
{
	const detail::ScopedFontLoaderFailure rejected(detail::FontLoaderStage::Face, DWRITE_E_FILEFORMAT);
	auto font = load_ok(std::move(bytes));
	EXPECT_TRUE(font == nullptr || font->kind() == LoadedFontKind::OutlineOnly);
	return font;
}

// The table directory entry for `tag` in an sfnt program: (offset, length).
std::pair<size_t, size_t> table_of(const std::vector<uint8_t>& font, std::string_view tag)
{
	const auto u16 = [&](size_t at) { return static_cast<size_t>((font[at] << 8) | font[at + 1u]); };
	const auto u32 = [&](size_t at) { return (u16(at) << 16) | u16(at + 2u); };
	for (size_t i = 0; i < u16(4u); ++i) {
		const size_t entry = 12u + 16u * i;
		if (std::memcmp(font.data() + entry, tag.data(), 4u) == 0) {
			return {u32(entry + 8u), u32(entry + 12u)};
		}
	}
	ADD_FAILURE() << "no table " << tag;
	return {0u, 0u};
}

void put_u16(std::vector<uint8_t>& font, size_t at, uint16_t value)
{
	font[at] = static_cast<uint8_t>(value >> 8);
	font[at + 1u] = static_cast<uint8_t>(value & 0xFFu);
}

// The offset of glyph `index`'s data inside the font program, through a short `loca`.
size_t glyph_offset_short_loca(const std::vector<uint8_t>& font, uint16_t index)
{
	const size_t loca = table_of(font, "loca").first;
	const size_t glyf = table_of(font, "glyf").first;
	return glyf + 2u * static_cast<size_t>((font[loca + 2u * index] << 8) | font[loca + 2u * index + 1u]);
}

// The exact cubic of the quadratic (p0, c, p1), as the glyf reader must emit it.
void expect_quad(const GlyphOutline& o, size_t verb, size_t point, PdfPoint p0, PdfPoint c, PdfPoint p1)
{
	ASSERT_LT(verb, o.verbs.size());
	EXPECT_EQ(o.verbs[verb], PathVerb::CubicBezierTo) << "verb " << verb;
	ASSERT_LE(point + 3u, o.points.size());
	const PdfPoint c1{p0.x + 2.0 / 3.0 * (c.x - p0.x), p0.y + 2.0 / 3.0 * (c.y - p0.y)};
	const PdfPoint c2{p1.x + 2.0 / 3.0 * (c.x - p1.x), p1.y + 2.0 / 3.0 * (c.y - p1.y)};
	EXPECT_NEAR(o.points[point].x, c1.x, 1e-12);
	EXPECT_NEAR(o.points[point].y, c1.y, 1e-12);
	EXPECT_NEAR(o.points[point + 1u].x, c2.x, 1e-12);
	EXPECT_NEAR(o.points[point + 1u].y, c2.y, 1e-12);
	EXPECT_NEAR(o.points[point + 2u].x, p1.x, 1e-12);
	EXPECT_NEAR(o.points[point + 2u].y, p1.y, 1e-12);
}

// A closed polygon outline: MoveTo, LineTo for each later point, CloseSubpath.
void expect_polygon(const GlyphOutline& o, size_t verb, size_t point, std::initializer_list<PdfPoint> corners)
{
	size_t i = 0;
	for (const PdfPoint& p : corners) {
		ASSERT_LT(verb + i, o.verbs.size());
		EXPECT_EQ(o.verbs[verb + i], i == 0u ? PathVerb::MoveTo : PathVerb::LineTo) << "verb " << verb + i;
		ASSERT_LT(point + i, o.points.size());
		EXPECT_NEAR(o.points[point + i].x, p.x, 1e-12) << "point " << point + i;
		EXPECT_NEAR(o.points[point + i].y, p.y, 1e-12) << "point " << point + i;
		++i;
	}
	ASSERT_LT(verb + i, o.verbs.size());
	EXPECT_EQ(o.verbs[verb + i], PathVerb::CloseSubpath);
}

void expect_box(const std::optional<PdfRect>& box, PdfRect expected)
{
	ASSERT_TRUE(box.has_value());
	EXPECT_NEAR(box->x_min, expected.x_min, 1e-9);
	EXPECT_NEAR(box->y_min, expected.y_min, 1e-9);
	EXPECT_NEAR(box->x_max, expected.x_max, 1e-9);
	EXPECT_NEAR(box->y_max, expected.y_max, 1e-9);
}

// Advances in text space for glyphs 0 to 6 (FONT_GLYPHS in scripts/build-corpus.py).
constexpr std::array<double, 7> kAdvances = {0.5, 1.0, 0.25, 1.0, 0.6, 0.6, 0.6};

// The glyf reader's outlines and metrics for the seven fixture glyphs, from either loca format.
void expect_fixture_glyphs(const LoadedFont& font)
{
	ASSERT_EQ(font.units_per_em(), 1000u);
	ASSERT_EQ(font.glyph_count(), 7u);
	for (uint16_t g = 0; g < 7u; ++g) {
		EXPECT_NEAR(*font.advance(g), kAdvances[g], 1e-12) << "glyph " << g;
	}
	// Glyph 1: the triangle (100, 0), (500, 700), (900, 0).
	const GlyphOutline a = *font.outline(1);
	ASSERT_EQ(a.verbs.size(), 4u);
	expect_polygon(a, 0u, 0u, {{0.1, 0.0}, {0.5, 0.7}, {0.9, 0.0}});
	expect_box(*font.ink_box(1), PdfRect{0.1, 0.0, 0.9, 0.7});
	// Glyph 2: empty, with an advance.
	EXPECT_TRUE(font.outline(2)->verbs.empty());
	EXPECT_FALSE(font.ink_box(2)->has_value());
	// Glyph 3: off-curve points (500, 0), (1000, 500), (500, 1000), (0, 500); the start is the implied point between
	// the first two, (750, 250), and four quadratics run through the implied midpoints back to it.
	const GlyphOutline o = *font.outline(3);
	ASSERT_EQ(o.verbs.size(), 6u);
	EXPECT_EQ(o.verbs[0], PathVerb::MoveTo);
	EXPECT_NEAR(o.points[0].x, 0.75, 1e-12);
	EXPECT_NEAR(o.points[0].y, 0.25, 1e-12);
	expect_quad(o, 1u, 1u, {0.75, 0.25}, {1.0, 0.5}, {0.75, 0.75});
	expect_quad(o, 2u, 4u, {0.75, 0.75}, {0.5, 1.0}, {0.25, 0.75});
	expect_quad(o, 3u, 7u, {0.25, 0.75}, {0.0, 0.5}, {0.25, 0.25});
	expect_quad(o, 4u, 10u, {0.25, 0.25}, {0.5, 0.0}, {0.75, 0.25});
	EXPECT_EQ(o.verbs[5], PathVerb::CloseSubpath);
	expect_box(*font.ink_box(3), PdfRect{0.0, 0.0, 1.0, 1.0});
	// Glyph 4: glyph 1 at half scale moved by (50, 100).
	expect_polygon(*font.outline(4), 0u, 0u, {{0.1, 0.1}, {0.3, 0.45}, {0.5, 0.1}});
	expect_box(*font.ink_box(4), PdfRect{0.1, 0.1, 0.5, 0.45});
	// Glyph 5: glyph 1, and a copy whose point 0 sits on the first copy's point 2: moved by (800, 0).
	const GlyphOutline c = *font.outline(5);
	ASSERT_EQ(c.verbs.size(), 8u);
	expect_polygon(c, 0u, 0u, {{0.1, 0.0}, {0.5, 0.7}, {0.9, 0.0}});
	expect_polygon(c, 4u, 3u, {{0.9, 0.0}, {1.3, 0.7}, {1.7, 0.0}});
	// Glyph 6: a negative left side bearing, and ink past the advance (0.6).
	expect_box(*font.ink_box(6), PdfRect{-0.05, 0.0, 0.7, 0.5});
}

TEST(PdfFont, LoadedFontIsImmutable)
{
	static_assert(!std::is_copy_constructible_v<LoadedFont> && !std::is_move_constructible_v<LoadedFont>);
	static_assert(!std::is_copy_assignable_v<LoadedFont> && !std::is_move_assignable_v<LoadedFont>);
	static_assert(requires(const LoadedFont& f) {
		f.advance(0);
		f.ink_box(0);
		f.outline(0);
		f.kind();
		f.dwrite_face();
	});
	for (const auto& font : {load_ok(font_bytes("truetype-full-short-loca.ttf")),
			 load_through_glyf(font_bytes("truetype-full-short-loca.ttf"))}) {
		ASSERT_NE(font, nullptr);
		// Two threads query every glyph of one font at once and see the same answers.
		std::array<std::vector<double>, 2> seen;
		const auto walk = [&](std::vector<double>& out) {
			for (int round = 0; round < 50; ++round) {
				for (uint16_t g = 0; g < font->glyph_count(); ++g) {
					out.push_back(*font->advance(g));
					const auto box = *font->ink_box(g);
					out.push_back(box.has_value() ? box->x_max : -1.0);
					const GlyphOutline o = *font->outline(g);
					out.push_back(static_cast<double>(o.points.size()));
					for (const PdfPoint& p : o.points) {
						out.push_back(p.x + 2.0 * p.y);
					}
				}
			}
		};
		std::thread first(walk, std::ref(seen[0]));
		std::thread second(walk, std::ref(seen[1]));
		first.join();
		second.join();
		EXPECT_FALSE(seen[0].empty());
		EXPECT_EQ(seen[0], seen[1]);
	}
}

TEST(PdfFont, InMemoryDirectWriteLoader)
{
	const int64_t owners_before = detail::live_font_program_owners();
	const size_t registered_before = detail::registered_font_programs();
	{
		auto bytes = font_bytes("truetype-full-short-loca.ttf");
		const std::weak_ptr<const std::vector<uint8_t>> watch = bytes;
		auto font = load_ok(std::move(bytes));
		ASSERT_NE(font, nullptr);
		EXPECT_EQ(font->kind(), LoadedFontKind::TrueType);
		ASSERT_NE(font->dwrite_face(), nullptr);
		EXPECT_EQ(font->units_per_em(), 1000u);
		EXPECT_EQ(font->glyph_count(), 7u);
		// The cmap maps 'A' to glyph 1 through DirectWrite.
		const UINT32 code = 'A';
		UINT16 glyph = 0;
		ASSERT_TRUE(SUCCEEDED(font->dwrite_face()->GetGlyphIndices(&code, 1u, &glyph)));
		EXPECT_EQ(glyph, 1u);
		// DirectWrite's owner holds the bytes: the caller's copy is gone, the font still draws.
		EXPECT_FALSE(watch.expired());
		EXPECT_EQ(detail::live_font_program_owners(), owners_before + 1);
		EXPECT_EQ(detail::registered_font_programs(), registered_before + 1u);
		EXPECT_NEAR(*font->advance(1), 1.0, 1e-12);
		expect_box(*font->ink_box(1), PdfRect{0.1, 0.0, 0.9, 0.7});
		const GlyphOutline a = *font->outline(1);
		EXPECT_FALSE(a.verbs.empty());
		EXPECT_EQ(a.verbs.front(), PathVerb::MoveTo);

		// OpenType with CFF outlines: glyph 1 is a 500 by 700 box at (100, 0) with advance 600.
		auto cff = load_ok(font_bytes("opentype-cff.otf"), FontProgramFormat::OpenType);
		ASSERT_NE(cff, nullptr);
		EXPECT_EQ(cff->kind(), LoadedFontKind::OpenType);
		EXPECT_EQ(cff->glyph_count(), 2u);
		EXPECT_NEAR(*cff->advance(1), 0.6, 1e-12);
		expect_box(*cff->ink_box(1), PdfRect{0.1, 0.0, 0.6, 0.7});
		const GlyphOutline box = *cff->outline(1);
		ASSERT_FALSE(box.points.empty());
		double x_min = 1e9;
		double x_max = -1e9;
		double y_min = 1e9;
		double y_max = -1e9;
		for (const PdfPoint& p : box.points) {
			x_min = std::min(x_min, p.x);
			x_max = std::max(x_max, p.x);
			y_min = std::min(y_min, p.y);
			y_max = std::max(y_max, p.y);
		}
		EXPECT_NEAR(x_min, 0.1, 1e-6);
		EXPECT_NEAR(x_max, 0.6, 1e-6);
		EXPECT_NEAR(y_min, 0.0, 1e-6);
		EXPECT_NEAR(y_max, 0.7, 1e-6);
	}
	// The stream is released with the last face, and the closed fonts leave the loader's registry: nothing of them stays.
	EXPECT_EQ(detail::live_font_program_owners(), owners_before);
	EXPECT_EQ(detail::registered_font_programs(), registered_before);
}

TEST(PdfFont, LoaderFailuresAreTyped)
{
	struct Case {
		detail::FontLoaderStage stage;
		DecoderStep step;
	};
	const Case stages[] = {
		{detail::FontLoaderStage::Factory, DecoderStep::DWriteFactory},
		{detail::FontLoaderStage::Loader, DecoderStep::DWriteFontLoader},
		{detail::FontLoaderStage::Reference, DecoderStep::DWriteFontReference},
		{detail::FontLoaderStage::Face, DecoderStep::DWriteFontFace},
	};
	for (const Case& c : stages) {
		// A resource failure is typed, keeps its step and HRESULT, and tries no fallback (this font has glyf outlines).
		for (const auto& [hr, code] : {std::pair{E_OUTOFMEMORY, ErrorCode::OutOfMemory}, std::pair{E_FAIL, ErrorCode::InternalError}}) {
			const detail::ScopedFontLoaderFailure failure(c.stage, hr);
			auto font = load_embedded_font(font_bytes("truetype-full-short-loca.ttf"), FontProgramFormat::TrueType);
			ASSERT_FALSE(font.has_value()) << static_cast<int>(c.stage);
			EXPECT_EQ(font.error().code, code) << static_cast<int>(c.stage);
			EXPECT_EQ(font.error().decoder_step, c.step);
			EXPECT_EQ(font.error().platform_status, std::optional<uint32_t>(static_cast<uint32_t>(hr)));
		}
	}
	// A rejection only at the face goes to the glyf reader; before it, it is a resource failure like any other.
	{
		const detail::ScopedFontLoaderFailure rejected(detail::FontLoaderStage::Reference, DWRITE_E_FILEFORMAT);
		auto font = load_embedded_font(font_bytes("truetype-full-short-loca.ttf"), FontProgramFormat::TrueType);
		ASSERT_FALSE(font.has_value());
		EXPECT_EQ(font.error().code, ErrorCode::InternalError);
	}
	{
		const detail::ScopedFontLoaderFailure rejected(detail::FontLoaderStage::Face, DWRITE_E_FILEFORMAT);
		auto font = load_embedded_font(font_bytes("truetype-full-short-loca.ttf"), FontProgramFormat::TrueType);
		ASSERT_TRUE(font.has_value());
		EXPECT_EQ((*font)->kind(), LoadedFontKind::OutlineOnly);
	}
	// A rejected program with no glyf outlines is refused, with DirectWrite's status kept as the prior step.
	{
		const detail::ScopedFontLoaderFailure rejected(detail::FontLoaderStage::Face, DWRITE_E_FILEFORMAT);
		auto font = load_embedded_font(font_bytes("opentype-cff.otf"), FontProgramFormat::OpenType);
		ASSERT_FALSE(font.has_value());
		EXPECT_EQ(font.error().code, ErrorCode::DamagedFormat);
		EXPECT_EQ(font.error().decoder_step, DecoderStep::NativeGlyf);
		EXPECT_EQ(font.error().prior_step, DecoderStep::DWriteFontFace);
		EXPECT_EQ(font.error().prior_status, std::optional<uint32_t>(static_cast<uint32_t>(DWRITE_E_FILEFORMAT)));
	}
}

TEST(PdfFont, RefusedTrueTypeFallsBackToGlyfOutlines)
{
	// The subset (a cmap header claiming 40 subtables, no OS/2, name or post; five hmtx records for seven glyphs) is
	// refused by DirectWrite and read by the glyf reader, without a damaged-font entry in the ring.
	clear_diagnostics();
	auto subset = load_ok(font_bytes("truetype-subset-short-loca.ttf"));
	ASSERT_NE(subset, nullptr);
	EXPECT_EQ(subset->kind(), LoadedFontKind::OutlineOnly);
	EXPECT_EQ(subset->dwrite_face(), nullptr);
	EXPECT_TRUE(diagnostics_tail().empty());
	expect_fixture_glyphs(*subset);
	// An hhea numberOfHMetrics past the glyph count, which DirectWrite refuses: the reader reads the first seven.
	{
		std::vector<uint8_t> past = *font_bytes("truetype-full-short-loca.ttf");
		put_u16(past, table_of(past, "hhea").first + 34u, 9u);
		auto font = load_ok(std::make_shared<const std::vector<uint8_t>>(past));
		ASSERT_NE(font, nullptr);
		EXPECT_EQ(font->kind(), LoadedFontKind::OutlineOnly);
		for (uint16_t g = 0; g < 7u; ++g) {
			EXPECT_NEAR(*font->advance(g), kAdvances[g], 1e-12) << "glyph " << g;
		}
	}
	// The same glyphs through both loca formats.
	expect_fixture_glyphs(*load_through_glyf(font_bytes("truetype-full-short-loca.ttf")));
	expect_fixture_glyphs(*load_through_glyf(font_bytes("truetype-full-long-loca.ttf")));
}

TEST(PdfFont, GlyphMetricsInTextSpace)
{
	// Advances and ink boxes in text space (glyph units over units per em), the same through DirectWrite and glyf.
	for (const auto& font : {load_ok(font_bytes("truetype-full-short-loca.ttf")),
			 load_through_glyf(font_bytes("truetype-full-short-loca.ttf"))}) {
		ASSERT_NE(font, nullptr);
		for (uint16_t g = 0; g < 7u; ++g) {
			EXPECT_NEAR(*font->advance(g), kAdvances[g], 1e-12) << "glyph " << g;
		}
		expect_box(*font->ink_box(1), PdfRect{0.1, 0.0, 0.9, 0.7});
		// A negative left side bearing and ink past the advance of 0.6.
		expect_box(*font->ink_box(6), PdfRect{-0.05, 0.0, 0.7, 0.5});
		EXPECT_GT(font->ink_box(6)->value().x_max, *font->advance(6));
		EXPECT_FALSE(font->ink_box(2)->has_value());
		auto past = font->advance(7);
		ASSERT_FALSE(past.has_value());
		EXPECT_EQ(past.error().code, ErrorCode::DamagedFormat);
	}
}

// Every component encoding the glyf reader reads, and placement by the glyph's origin (its first phantom point): the
// glyphs of `fonts/truetype-composites.ttf` (COMPOSITE_FONT_GLYPHS in scripts/build-corpus.py), whose placements are
// stated there. DirectWrite reads the same program, and its advances and ink boxes agree, except for glyph 6:
// DirectWrite keeps a USE_MY_METRICS composite's own hmtx advance and bearing, where the OpenType glyf specification
// says the component's metrics apply and any other hmtx value is ignored, which the glyf reader follows.
TEST(PdfFont, CompositeEncodingsAndPlacement)
{
	const auto glyf = load_through_glyf(font_bytes("truetype-composites.ttf"));
	ASSERT_NE(glyf, nullptr);
	// 2: lsb 150 against xMin 200, drawn 50 to the left.
	expect_polygon(*glyf->outline(2), 0u, 0u, {{0.15, 0.0}, {0.35, 0.5}, {0.55, 0.0}});
	// 3: byte offsets (-10, -20) and x/y scales (0.5, 1.5).
	expect_polygon(*glyf->outline(3), 0u, 0u, {{0.04, -0.02}, {0.24, 1.03}, {0.44, -0.02}});
	// 4: x' = -y, y' = x, then (800, 0).
	expect_polygon(*glyf->outline(4), 0u, 0u, {{0.8, 0.1}, {0.1, 0.5}, {0.8, 0.9}});
	// 5: half scale with the offset (100, 100) scaled to (50, 50).
	expect_polygon(*glyf->outline(5), 0u, 0u, {{0.1, 0.05}, {0.3, 0.4}, {0.5, 0.05}});
	// 6: USE_MY_METRICS: glyph 2's advance and placement, not its own advance of 999.
	EXPECT_NEAR(*glyf->advance(6), 0.7, 1e-12);
	expect_polygon(*glyf->outline(6), 0u, 0u, {{0.15, 0.0}, {0.35, 0.5}, {0.55, 0.0}});
	// 7: the second copy's advance point on the composite's origin moves it by (-1000, 0).
	const auto phantom_result = glyf->outline(7);
	ASSERT_TRUE(phantom_result.has_value()) << phantom_result.error().message.view();
	const GlyphOutline& phantom = *phantom_result;
	ASSERT_EQ(phantom.verbs.size(), 8u);
	expect_polygon(phantom, 0u, 0u, {{0.1, 0.0}, {0.5, 0.7}, {0.9, 0.0}});
	expect_polygon(phantom, 4u, 3u, {{-0.9, 0.0}, {-0.5, 0.7}, {-0.1, 0.0}});

	const auto dwrite = load_ok(font_bytes("truetype-composites.ttf"));
	ASSERT_NE(dwrite, nullptr);
	ASSERT_EQ(dwrite->kind(), LoadedFontKind::TrueType);
	for (uint16_t g = 1; g < glyf->glyph_count(); ++g) {
		if (g == 6u) {
			EXPECT_NEAR(*dwrite->advance(g), 0.999, 1e-12);
			continue;
		}
		EXPECT_NEAR(*glyf->advance(g), *dwrite->advance(g), 1e-12) << "glyph " << g;
		const auto ours = *glyf->ink_box(g);
		const auto theirs = *dwrite->ink_box(g);
		ASSERT_TRUE(ours.has_value() && theirs.has_value()) << "glyph " << g;
		EXPECT_NEAR(ours->x_min, theirs->x_min, 1e-9) << "glyph " << g;
		EXPECT_NEAR(ours->y_min, theirs->y_min, 1e-9) << "glyph " << g;
		EXPECT_NEAR(ours->x_max, theirs->x_max, 1e-9) << "glyph " << g;
		EXPECT_NEAR(ours->y_max, theirs->y_max, 1e-9) << "glyph " << g;
	}
}

// One expansion answers what the three queries answer apart, on both routes.
TEST(PdfFont, GlyphShapeMatchesSeparateQueries)
{
	for (const auto& font : {load_ok(font_bytes("truetype-full-short-loca.ttf")),
			 load_through_glyf(font_bytes("truetype-composites.ttf")), load_ok(font_bytes("opentype-cff.otf"), FontProgramFormat::OpenType)}) {
		ASSERT_NE(font, nullptr);
		for (uint16_t g = 0; g < font->glyph_count(); ++g) {
			const GlyphShape s = *font->shape(g);
			EXPECT_EQ(s.advance, *font->advance(g)) << "glyph " << g;
			EXPECT_EQ(s.ink_box, *font->ink_box(g)) << "glyph " << g;
			const GlyphOutline o = *font->outline(g);
			EXPECT_EQ(s.outline.verbs, o.verbs) << "glyph " << g;
			EXPECT_EQ(s.outline.points, o.points) << "glyph " << g;
		}
	}
}

// A font DirectWrite accepts is still bounded: its glyf outlines are checked before DirectWrite expands a glyph.
TEST(PdfFont, DirectWriteRouteKeepsTheBudgets)
{
	const auto font = load_ok(font_bytes("truetype-hostile-budgets.ttf"));
	ASSERT_NE(font, nullptr);
	ASSERT_EQ(font->kind(), LoadedFontKind::TrueType);
	for (const auto& [glyph, reason] : {std::pair{uint16_t{2}, RefusalReason::GlyphPoints}, std::pair{uint16_t{16}, RefusalReason::ComponentVisits}}) {
		const auto check = font->check_glyph(glyph);
		ASSERT_FALSE(check.has_value()) << "glyph " << glyph;
		EXPECT_EQ(check.error().code, ErrorCode::ResourceLimit) << "glyph " << glyph;
		EXPECT_EQ(check.error().refusal_reason, reason) << "glyph " << glyph;
		const auto outline = font->outline(glyph);
		ASSERT_FALSE(outline.has_value()) << "glyph " << glyph;
		EXPECT_EQ(outline.error().code, ErrorCode::ResourceLimit) << "glyph " << glyph;
		EXPECT_EQ(outline.error().refusal_reason, reason) << "glyph " << glyph;
	}
	EXPECT_TRUE(font->check_glyph(1).has_value());
	EXPECT_TRUE(font->outline(1).has_value());
}

TEST(PdfFont, HostileFontProgramsRefused)
{
	const auto refused = [](const std::vector<uint8_t>& bytes, ErrorCode code, std::string_view what) {
		const detail::ScopedFontLoaderFailure rejected(detail::FontLoaderStage::Face, DWRITE_E_FILEFORMAT);
		auto font = load_embedded_font(std::make_shared<const std::vector<uint8_t>>(bytes), FontProgramFormat::TrueType);
		ASSERT_FALSE(font.has_value()) << what;
		EXPECT_EQ(font.error().code, code) << what;
	};
	const auto glyph_refused = [](const std::vector<uint8_t>& bytes, uint16_t glyph, ErrorCode code, std::string_view what,
								   RefusalReason reason = RefusalReason::None) {
		auto font = load_through_glyf(std::make_shared<const std::vector<uint8_t>>(bytes));
		ASSERT_NE(font, nullptr) << what;
		auto outline = font->outline(glyph);
		ASSERT_FALSE(outline.has_value()) << what;
		EXPECT_EQ(outline.error().code, code) << what;
		EXPECT_EQ(outline.error().refusal_reason, reason) << what;
		auto box = font->ink_box(glyph);
		ASSERT_FALSE(box.has_value()) << what;
		EXPECT_EQ(box.error().code, code) << what;
	};
	const std::vector<uint8_t> font = *font_bytes("truetype-subset-short-loca.ttf");

	// A program past kMaxDecodedStreamBytes, before DirectWrite sees it.
	{
		auto huge = load_embedded_font(
			std::make_shared<const std::vector<uint8_t>>(kMaxDecodedStreamBytes + 1u, uint8_t{0}), FontProgramFormat::TrueType);
		ASSERT_FALSE(huge.has_value());
		EXPECT_EQ(huge.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(huge.error().refusal_reason, RefusalReason::FontProgramBytes);
		EXPECT_EQ(huge.error().observed, std::optional<uint64_t>(kMaxDecodedStreamBytes + 1u));
	}
	// A table outside the program.
	{
		std::vector<uint8_t> bad = font;
		const size_t tables = static_cast<size_t>((bad[4] << 8) | bad[5]);
		for (size_t i = 0; i < tables; ++i) {
			if (std::memcmp(bad.data() + 12u + 16u * i, "glyf", 4u) == 0) {
				put_u16(bad, 12u + 16u * i + 8u, 0x7FFFu); // offset high half: far past the end
			}
		}
		refused(bad, ErrorCode::DamagedFormat, "table outside the program");
	}
	// loca offsets that run backwards.
	{
		std::vector<uint8_t> bad = font;
		const size_t loca = table_of(bad, "loca").first;
		put_u16(bad, loca + 2u * 2u, 0u); // glyph 2 starts before glyph 1 ends
		refused(bad, ErrorCode::DamagedFormat, "loca backwards");
	}
	// loca offsets past the end of glyf.
	{
		std::vector<uint8_t> bad = font;
		const size_t loca = table_of(bad, "loca").first;
		put_u16(bad, loca + 2u * 7u, 0xFFFFu); // the end of the last glyph, far past glyf
		refused(bad, ErrorCode::DamagedFormat, "loca past glyf");
	}
	// A component naming a glyph past numGlyphs, and one naming its own composite (nesting past 16 levels).
	{
		std::vector<uint8_t> bad = font;
		put_u16(bad, glyph_offset_short_loca(bad, 4) + 12u, 99u);
		glyph_refused(bad, 4, ErrorCode::DamagedFormat, "component past numGlyphs");
		std::vector<uint8_t> self = font;
		put_u16(self, glyph_offset_short_loca(self, 4) + 12u, 4u);
		glyph_refused(self, 4, ErrorCode::ResourceLimit, "composite nesting past 16 levels", RefusalReason::CompositeDepth);
	}
	// A simple glyph past 65,535 points: its last end point says 65,535, so it would hold 65,536.
	{
		std::vector<uint8_t> bad = font;
		put_u16(bad, glyph_offset_short_loca(bad, 1) + 10u, 0xFFFFu);
		glyph_refused(bad, 1, ErrorCode::ResourceLimit, "simple glyph past 65,535 points", RefusalReason::GlyphPoints);
	}
	// Budgets in a compact font: four copies of a 20,000-point glyph (80,000 points), and a 12-level binary tree of
	// composites (8,190 component visits) whose nesting stays within 16 levels.
	{
		const std::vector<uint8_t> budgets = *font_bytes("truetype-hostile-budgets.ttf");
		glyph_refused(budgets, 2, ErrorCode::ResourceLimit, "expanded points", RefusalReason::GlyphPoints);
		glyph_refused(budgets, 16, ErrorCode::ResourceLimit, "component visits", RefusalReason::ComponentVisits);
		// Contour ends that repeat (2, 2, 2) in an otherwise well-formed glyph: empty contours would grow storage
		// without spending points.
		glyph_refused(budgets, 17, ErrorCode::DamagedFormat, "contour ends do not increase");
		auto ok = load_through_glyf(std::make_shared<const std::vector<uint8_t>>(budgets));
		ASSERT_NE(ok, nullptr);
		EXPECT_EQ(ok->outline(1)->points.size() > 0u, true); // the 20,000-point glyph alone fits
		EXPECT_TRUE(ok->outline(15).has_value());             // 4,094 visits at 11 levels fit
	}
}

} // namespace
} // namespace recto::pdf
