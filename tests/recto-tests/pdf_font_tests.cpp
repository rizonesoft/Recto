// Unit tests for embedded font loading, `LoadedFont` and the `glyf` fallback (D01 T02 section 2). The fixtures in
// `tests/corpus/fonts/` are written by `scripts/build-corpus.py` from glyph definitions whose geometry is stated in
// that script (units per em 1000), so every expected advance, box and outline below is computed by hand from them.
#include "filters.h"
#include "font.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
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
// stated there. DirectWrite reads the same program: the advances agree on both routes (both apply USE_MY_METRICS),
// and the ink boxes agree except glyph 6's, since DirectWrite draws a USE_MY_METRICS composite at its own hmtx bearing
// where the OpenType glyf specification places it by the component's, which the glyf reader follows.
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
	// 8: the second copy's top phantom point (yMax 700 plus vmtx top side bearing 200) on the composite's point 0.
	const auto vphantom_result = glyf->outline(8);
	ASSERT_TRUE(vphantom_result.has_value()) << vphantom_result.error().message.view();
	expect_polygon(*vphantom_result, 4u, 3u, {{0.2, -0.9}, {0.6, -0.2}, {1.0, -0.9}});

	const auto dwrite = load_ok(font_bytes("truetype-composites.ttf"));
	ASSERT_NE(dwrite, nullptr);
	ASSERT_EQ(dwrite->kind(), LoadedFontKind::TrueType);
	for (uint16_t g = 1; g < glyf->glyph_count(); ++g) {
		EXPECT_NEAR(*glyf->advance(g), *dwrite->advance(g), 1e-12) << "glyph " << g;
		EXPECT_NEAR(dwrite->shape(g)->advance, *dwrite->advance(g), 1e-12) << "glyph " << g;
		if (g == 6u) {
			continue;
		}
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
	// A broken loca entry DirectWrite accepts costs its own glyph, not the checks: the last glyph's range runs past
	// glyf, the font still loads through DirectWrite, the other glyphs keep their budgets and that one is refused.
	{
		std::vector<uint8_t> bytes = *font_bytes("truetype-hostile-budgets.ttf");
		const size_t loca = table_of(bytes, "loca").first;
		const size_t last = loca + 4u * 19u;
		bytes[last] = 0x7F;
		auto broken = load_ok(std::make_shared<const std::vector<uint8_t>>(bytes));
		ASSERT_NE(broken, nullptr);
		EXPECT_EQ(broken->kind(), LoadedFontKind::TrueType);
		const auto tree = broken->check_glyph(16);
		ASSERT_FALSE(tree.has_value());
		EXPECT_EQ(tree.error().refusal_reason, RefusalReason::ComponentVisits);
		EXPECT_TRUE(broken->check_glyph(1).has_value());
		const auto last_glyph = broken->check_glyph(18);
		ASSERT_FALSE(last_glyph.has_value());
		EXPECT_EQ(last_glyph.error().code, ErrorCode::DamagedFormat);
	}
	// A metrics query is bounded too: 5,000 component records before the USE_MY_METRICS one.
	for (const auto& route : {font, load_through_glyf(font_bytes("truetype-hostile-budgets.ttf"))}) {
		const auto advance = route->advance(18);
		ASSERT_FALSE(advance.has_value());
		EXPECT_EQ(advance.error().refusal_reason, RefusalReason::ComponentVisits);
	}
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
	// loca ranges that run backwards or past glyf cost their own glyph, checked as it is read; the font loads.
	{
		std::vector<uint8_t> bad = font;
		const size_t loca = table_of(bad, "loca").first;
		put_u16(bad, loca + 2u * 2u, 0u); // glyph 1 now ends before it starts
		glyph_refused(bad, 1, ErrorCode::DamagedFormat, "loca backwards");
		std::vector<uint8_t> past = font;
		put_u16(past, table_of(past, "loca").first + 2u * 7u, 0xFFFFu); // the end of the last glyph, far past glyf
		glyph_refused(past, 6, ErrorCode::DamagedFormat, "loca past glyf");
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


// ---------------------------------------------------------------------------------------------------------------------
// Standard 14 fonts (D01 T02 section 27)
// ---------------------------------------------------------------------------------------------------------------------

constexpr std::array<Standard14Font, kStandard14FontCount> kAllStandard14 = {Standard14Font::Courier,
	Standard14Font::CourierBold, Standard14Font::CourierOblique, Standard14Font::CourierBoldOblique,
	Standard14Font::Helvetica, Standard14Font::HelveticaBold, Standard14Font::HelveticaOblique,
	Standard14Font::HelveticaBoldOblique, Standard14Font::TimesRoman, Standard14Font::TimesBold,
	Standard14Font::TimesItalic, Standard14Font::TimesBoldItalic, Standard14Font::Symbol, Standard14Font::ZapfDingbats};

// One AFM file read here, independently of scripts/build-font-metrics.py, so a generator that drops or misreads a
// field fails this test (Adobe Technical Note 5004).
struct AfmFile {
	std::string font_name;
	std::array<int, 4> font_box{};
	std::optional<int> ascender;
	std::optional<int> descender;
	std::optional<int> cap_height;
	double italic_angle = 0.0;
	bool fixed_pitch = false;
	struct Glyph {
		std::string name;
		int code = 0;
		int width = 0;
		std::array<int, 4> box{};
	};
	std::vector<Glyph> glyphs;
};

[[nodiscard]] AfmFile read_afm(std::string_view font_name)
{
	const auto bytes = recto::test::read_file_bytes(
		recto::test::repo_root() / "third_party" / "adobe-core14-afm" / (std::string(font_name) + ".afm"));
	AfmFile afm;
	std::string_view rest(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	bool in_chars = false;
	while (!rest.empty()) {
		const size_t end = rest.find('\n');
		std::string_view line = rest.substr(0, end);
		rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1u);
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1u);
		}
		std::istringstream in{std::string(line)};
		std::string key;
		in >> key;
		if (key == "StartCharMetrics") {
			in_chars = true;
		} else if (key == "EndCharMetrics") {
			in_chars = false;
		} else if (in_chars && key == "C") {
			AfmFile::Glyph g;
			std::string field;
			in >> g.code;
			while (in >> field) {
				if (field == "WX") {
					in >> g.width;
				} else if (field == "N") {
					in >> g.name;
				} else if (field == "B") {
					in >> g.box[0] >> g.box[1] >> g.box[2] >> g.box[3];
				} else if (field == "L") {
					std::string skip;
					in >> skip >> skip;
				}
			}
			afm.glyphs.push_back(g);
		} else if (key == "FontName") {
			in >> afm.font_name;
		} else if (key == "FontBBox") {
			in >> afm.font_box[0] >> afm.font_box[1] >> afm.font_box[2] >> afm.font_box[3];
		} else if (key == "Ascender") {
			afm.ascender.emplace();
			in >> *afm.ascender;
		} else if (key == "Descender") {
			afm.descender.emplace();
			in >> *afm.descender;
		} else if (key == "CapHeight") {
			afm.cap_height.emplace();
			in >> *afm.cap_height;
		} else if (key == "ItalicAngle") {
			in >> afm.italic_angle;
		} else if (key == "IsFixedPitch") {
			std::string value;
			in >> value;
			afm.fixed_pitch = value == "true";
		}
	}
	return afm;
}

[[nodiscard]] std::optional<int> as_int(std::optional<int16_t> v)
{
	return v.has_value() ? std::optional<int>(*v) : std::nullopt;
}

TEST(PdfFont, Standard14Metrics)
{
	for (const Standard14Font font : kAllStandard14) {
		const Standard14Metrics& m = standard14_metrics(font);
		SCOPED_TRACE(std::string(m.base_font));
		const AfmFile afm = read_afm(m.base_font);
		EXPECT_EQ(afm.font_name, m.base_font);
		EXPECT_EQ(afm.font_box, (std::array<int, 4>{m.font_box[0], m.font_box[1], m.font_box[2], m.font_box[3]}));
		EXPECT_EQ(afm.ascender, as_int(m.ascender));
		EXPECT_EQ(afm.descender, as_int(m.descender));
		EXPECT_EQ(afm.cap_height, as_int(m.cap_height));
		EXPECT_EQ(afm.italic_angle, m.italic_angle);
		EXPECT_EQ(afm.fixed_pitch, m.fixed_pitch);
		ASSERT_EQ(afm.glyphs.size(), m.glyphs.size());
		size_t encoded = 0;
		for (const AfmFile::Glyph& g : afm.glyphs) {
			const Standard14Glyph* table = standard14_glyph_by_name(font, g.name);
			ASSERT_NE(table, nullptr) << g.name;
			EXPECT_EQ(table->name, g.name);
			EXPECT_EQ(table->code, g.code) << g.name;
			EXPECT_EQ(table->width, g.width) << g.name;
			EXPECT_EQ((std::array<int, 4>{table->box[0], table->box[1], table->box[2], table->box[3]}), g.box) << g.name;
			if (g.code >= 0) {
				++encoded;
				EXPECT_EQ(standard14_glyph_by_code(font, static_cast<uint8_t>(g.code)), table) << g.name;
			}
		}
		// Every other code has no glyph, and the names are in byte order for the lookup.
		size_t coded = 0;
		for (int code = 0; code < 256; ++code) {
			coded += standard14_glyph_by_code(font, static_cast<uint8_t>(code)) != nullptr ? 1u : 0u;
		}
		EXPECT_EQ(coded, encoded);
		EXPECT_TRUE(std::is_sorted(m.glyphs.begin(), m.glyphs.end(),
			[](const Standard14Glyph& a, const Standard14Glyph& b) { return a.name < b.name; }));
		EXPECT_EQ(standard14_glyph_by_name(font, "no-such-glyph"), nullptr);
	}
	// Symbol and ZapfDingbats give no ascender, descender or cap height: none, not zero.
	EXPECT_FALSE(standard14_metrics(Standard14Font::Symbol).cap_height.has_value());
	EXPECT_FALSE(standard14_metrics(Standard14Font::ZapfDingbats).ascender.has_value());
	EXPECT_EQ(standard14_metrics(Standard14Font::TimesItalic).italic_angle, -15.5);
	static_assert(std::is_trivially_destructible_v<Standard14Metrics>);
}

TEST(PdfFont, Standard14Aliases)
{
	using F = Standard14Font;
	const std::array<std::pair<std::string_view, F>, 33> aliases = {{
		{"Helvetica", F::Helvetica}, {"ABCDEF+Helvetica-Bold", F::HelveticaBold}, {"QWERTY+Times-Italic", F::TimesItalic},
		{"Arial", F::Helvetica}, {"ArialMT", F::Helvetica}, {"Arial,Bold", F::HelveticaBold},
		{"Arial,Italic", F::HelveticaOblique}, {"Arial,BoldItalic", F::HelveticaBoldOblique},
		{"Arial-BoldMT", F::HelveticaBold}, {"Arial-ItalicMT", F::HelveticaOblique},
		{"Arial-BoldItalicMT", F::HelveticaBoldOblique}, {"Helvetica,Bold", F::HelveticaBold},
		{"TimesNewRoman", F::TimesRoman}, {"TimesNewRoman,Bold", F::TimesBold}, {"TimesNewRomanPSMT", F::TimesRoman},
		{"TimesNewRomanPS-BoldMT", F::TimesBold}, {"TimesNewRomanPS-ItalicMT", F::TimesItalic},
		{"TimesNewRomanPS-BoldItalicMT", F::TimesBoldItalic}, {"Times-Roman,Bold", F::TimesBold},
		{"CourierNew", F::Courier}, {"CourierNew,Bold", F::CourierBold}, {"CourierNewPSMT", F::Courier},
		{"CourierNewPS-BoldMT", F::CourierBold}, {"CourierNewPS-ItalicMT", F::CourierOblique},
		{"CourierNew,BoldItalic", F::CourierBoldOblique}, {"Courier-Oblique", F::CourierOblique},
		{"Symbol", F::Symbol}, {"SymbolMT", F::Symbol}, {"Symbol,Bold", F::Symbol}, {"ZapfDingbats", F::ZapfDingbats},
		{"ZYXWVU+ZapfDingbats", F::ZapfDingbats}, {"Times", F::TimesRoman},
		{"Arial,Oblique", F::HelveticaOblique},
	}};
	for (const auto& [name, font] : aliases) {
		EXPECT_EQ(standard14_from_base_font(name), std::optional<F>(font)) << name;
	}
	for (const std::string_view other : {"ArialNarrow", "Arial-Black", "Arial,Black", "Helvetica-Narrow",
			 "Helvetica-Light", "TimesNewRomanX", "Courier10", "Calibri", "abcdef+Helvetica", "ABCDE+Helvetica",
			 "ABCDEF+", "", "Symbol-Black", "Times New Roman"}) {
		// A name with spaces is not a PostScript name.
		EXPECT_EQ(standard14_from_base_font(other), std::nullopt) << other;
	}
}

// Adobe's zapfdingbats.txt (adobe-type-tools/agl-aglfn at 4036a9c) for the codes 0x21 to 0x7E: the Unicode scalar
// each glyph stands for. The test table only; the encoding itself is D01 T02 section 28's.
struct CodeUnicode {
	uint8_t code;
	char32_t unicode;
}; std::array<CodeUnicode, 94> kZapfDingbatsUnicode = {{{0x21, 0x2701}, {0x22, 0x2702},
	{0x23, 0x2703}, {0x24, 0x2704}, {0x25, 0x260E}, {0x26, 0x2706}, {0x27, 0x2707}, {0x28, 0x2708}, {0x29, 0x2709},
	{0x2A, 0x261B}, {0x2B, 0x261E}, {0x2C, 0x270C}, {0x2D, 0x270D}, {0x2E, 0x270E}, {0x2F, 0x270F}, {0x30, 0x2710},
	{0x31, 0x2711}, {0x32, 0x2712}, {0x33, 0x2713}, {0x34, 0x2714}, {0x35, 0x2715}, {0x36, 0x2716}, {0x37, 0x2717},
	{0x38, 0x2718}, {0x39, 0x2719}, {0x3A, 0x271A}, {0x3B, 0x271B}, {0x3C, 0x271C}, {0x3D, 0x271D}, {0x3E, 0x271E},
	{0x3F, 0x271F}, {0x40, 0x2720}, {0x41, 0x2721}, {0x42, 0x2722}, {0x43, 0x2723}, {0x44, 0x2724}, {0x45, 0x2725},
	{0x46, 0x2726}, {0x47, 0x2727}, {0x48, 0x2605}, {0x49, 0x2729}, {0x4A, 0x272A}, {0x4B, 0x272B}, {0x4C, 0x272C},
	{0x4D, 0x272D}, {0x4E, 0x272E}, {0x4F, 0x272F}, {0x50, 0x2730}, {0x51, 0x2731}, {0x52, 0x2732}, {0x53, 0x2733},
	{0x54, 0x2734}, {0x55, 0x2735}, {0x56, 0x2736}, {0x57, 0x2737}, {0x58, 0x2738}, {0x59, 0x2739}, {0x5A, 0x273A},
	{0x5B, 0x273B}, {0x5C, 0x273C}, {0x5D, 0x273D}, {0x5E, 0x273E}, {0x5F, 0x273F}, {0x60, 0x2740}, {0x61, 0x2741},
	{0x62, 0x2742}, {0x63, 0x2743}, {0x64, 0x2744}, {0x65, 0x2745}, {0x66, 0x2746}, {0x67, 0x2747}, {0x68, 0x2748},
	{0x69, 0x2749}, {0x6A, 0x274A}, {0x6B, 0x274B}, {0x6C, 0x25CF}, {0x6D, 0x274D}, {0x6E, 0x25A0}, {0x6F, 0x274F},
	{0x70, 0x2750}, {0x71, 0x2751}, {0x72, 0x2752}, {0x73, 0x25B2}, {0x74, 0x25BC}, {0x75, 0x25C6}, {0x76, 0x2756},
	{0x77, 0x25D7}, {0x78, 0x2758}, {0x79, 0x2759}, {0x7A, 0x275A}, {0x7B, 0x275B}, {0x7C, 0x275C}, {0x7D, 0x275D},
	{0x7E, 0x275E}}};

// A face glyph for `key` with an outline, beside the AFM glyph's advance.
void expect_drawn(Standard14Font font, char32_t key, const Standard14Glyph& afm_glyph)
{
	auto glyph = standard14_face_glyph(font, key);
	ASSERT_TRUE(glyph.has_value()) << glyph.error().message.view();
	ASSERT_TRUE(glyph->has_value()) << afm_glyph.name << " U+" << std::hex << static_cast<uint32_t>(key);
	ASSERT_NE((*glyph)->face, nullptr);
	auto outline = (*glyph)->face->outline((*glyph)->glyph);
	ASSERT_TRUE(outline.has_value()) << afm_glyph.name;
	EXPECT_FALSE(outline->points.empty()) << afm_glyph.name;
	EXPECT_GT(afm_glyph.width, 0) << afm_glyph.name;
}

TEST(PdfFont, Standard14Faces)
{
	for (const Standard14Font font : kAllStandard14) {
		const Standard14Metrics& m = standard14_metrics(font);
		SCOPED_TRACE(std::string(m.base_font));
		auto face = load_standard14_face(font);
		ASSERT_TRUE(face.has_value()) << face.error().message.view();
		EXPECT_GT((*face)->glyph_count(), 0u);
		EXPECT_NE((*face)->dwrite_face(), nullptr);
		auto again = load_standard14_face(font);
		ASSERT_TRUE(again.has_value());
		EXPECT_EQ(again->get(), face->get()); // one face per process, shared
		if (font == Standard14Font::Symbol) {
			for (int code = 0x21; code < 256; ++code) {
				if (const Standard14Glyph* g = standard14_glyph_by_code(font, static_cast<uint8_t>(code)); g != nullptr) {
					expect_drawn(font, static_cast<char32_t>(code), *g);
				}
			}
		} else if (font == Standard14Font::ZapfDingbats) {
			for (const auto& [code, unicode] : kZapfDingbatsUnicode) {
				const Standard14Glyph* g = standard14_glyph_by_code(font, code);
				ASSERT_NE(g, nullptr) << int{code};
				expect_drawn(font, unicode, *g);
			}
		} else {
			// StandardEncoding is ASCII from 0x21 to 0x7E but for its quotes (ISO 32000-1 Annex D).
			for (int code = 0x21; code <= 0x7E; ++code) {
				const Standard14Glyph* g = standard14_glyph_by_code(font, static_cast<uint8_t>(code));
				ASSERT_NE(g, nullptr) << code;
				const char32_t unicode = code == 0x27 ? U'’' : code == 0x60 ? U'‘' : static_cast<char32_t>(code);
				expect_drawn(font, unicode, *g);
			}
		}
	}
	// Threads that load a face at once all get the same one: the first face stored is the one every caller shares.
	{
		detail::reset_standard14_faces();
		constexpr size_t kThreads = 8u;
		std::array<std::shared_ptr<const LoadedFont>, kThreads> got;
		std::atomic<size_t> ready{0};
		std::vector<std::thread> threads;
		for (size_t i = 0; i < kThreads; ++i) {
			threads.emplace_back([&, i] {
				ready.fetch_add(1u);
				while (ready.load() < kThreads) {
					std::this_thread::yield();
				}
				auto face = load_standard14_face(Standard14Font::HelveticaBold);
				if (face.has_value()) {
					got[i] = *face;
				}
			});
		}
		for (std::thread& thread : threads) {
			thread.join();
		}
		for (const auto& face : got) {
			ASSERT_NE(face, nullptr);
			EXPECT_EQ(face.get(), got[0].get());
		}
		EXPECT_EQ(load_standard14_face(Standard14Font::HelveticaBold)->get(), got[0].get());
	}
	// Symbol's Euro, which the Windows Symbol face lacks, comes from Segoe UI Symbol; every other Symbol code from Symbol.
	{
		auto euro = standard14_face_glyph(Standard14Font::Symbol, U'\xA0');
		ASSERT_TRUE(euro.has_value() && euro->has_value());
		EXPECT_EQ((*euro)->face.get(), load_standard14_face(Standard14Font::ZapfDingbats)->get());
		auto alpha = standard14_face_glyph(Standard14Font::Symbol, U'a');
		ASSERT_TRUE(alpha.has_value() && alpha->has_value());
		EXPECT_EQ((*alpha)->face.get(), load_standard14_face(Standard14Font::Symbol)->get());
	}
	// A code point the face lacks is none, not glyph 0 drawn as a box.
	auto none = standard14_face_glyph(Standard14Font::Courier, U'\U0001F600');
	ASSERT_TRUE(none.has_value());
	EXPECT_FALSE(none->has_value());
	// The faces Windows draws with.
	EXPECT_EQ(standard14_face(Standard14Font::TimesBoldItalic).family, L"Times New Roman");
	EXPECT_TRUE(standard14_face(Standard14Font::TimesBoldItalic).bold && standard14_face(Standard14Font::TimesBoldItalic).italic);
	EXPECT_EQ(standard14_face(Standard14Font::HelveticaOblique).family, L"Arial");
	EXPECT_TRUE(standard14_face(Standard14Font::HelveticaOblique).italic);
	EXPECT_EQ(standard14_face(Standard14Font::Symbol).family, L"Symbol");
	EXPECT_EQ(standard14_face(Standard14Font::ZapfDingbats).family, L"Segoe UI Symbol");
}

TEST(PdfFont, Standard14FaceFailures)
{
	constexpr int32_t kOutOfMemory = static_cast<int32_t>(0x8007000Eu);
	constexpr int32_t kAccessDenied = static_cast<int32_t>(0x80070005u);
	// A family Windows lacks: UnsupportedFeature naming it, never another font.
	{
		const detail::ScopedStandard14Family missing(Standard14Font::Helvetica, L"Recto No Such Family");
		auto face = load_standard14_face(Standard14Font::Helvetica);
		ASSERT_FALSE(face.has_value());
		EXPECT_EQ(face.error().code, ErrorCode::UnsupportedFeature);
		EXPECT_EQ(face.error().decoder_step, DecoderStep::DWriteSystemFont);
		EXPECT_NE(face.error().message.view().find("Recto No Such Family"), std::string_view::npos)
			<< face.error().message.view();
		EXPECT_NE(face.error().message.view().find("Helvetica"), std::string_view::npos);
	}
	// Each DirectWrite step's failure keeps its step and HRESULT, and is not kept: the next call loads the face.
	const std::array<std::tuple<detail::FontLoaderStage, DecoderStep, int32_t, ErrorCode>, 4> cases = {{
		{detail::FontLoaderStage::Factory, DecoderStep::DWriteFactory, kAccessDenied, ErrorCode::InternalError},
		{detail::FontLoaderStage::SystemFont, DecoderStep::DWriteSystemFont, kAccessDenied, ErrorCode::InternalError},
		{detail::FontLoaderStage::SystemFontFile, DecoderStep::DWriteFontFile, kAccessDenied, ErrorCode::InternalError},
		{detail::FontLoaderStage::SystemFontFile, DecoderStep::DWriteFontFile, kOutOfMemory, ErrorCode::OutOfMemory},
	}};
	for (const auto& [stage, step, hr, code] : cases) {
		SCOPED_TRACE(static_cast<int>(stage));
		detail::reset_standard14_faces();
		{
			const detail::ScopedFontLoaderFailure fail(stage, hr);
			auto face = load_standard14_face(Standard14Font::TimesBold);
			ASSERT_FALSE(face.has_value());
			EXPECT_EQ(face.error().code, code);
			EXPECT_EQ(face.error().decoder_step, step);
			EXPECT_EQ(face.error().platform_status, std::optional<uint32_t>(static_cast<uint32_t>(hr)));
		}
		auto retried = load_standard14_face(Standard14Font::TimesBold);
		ASSERT_TRUE(retried.has_value()) << retried.error().message.view();
	}
	// A returned face stays usable once everything it was read through, and the shared slot, are gone.
	detail::reset_standard14_faces();
	auto face = load_standard14_face(Standard14Font::CourierBold);
	ASSERT_TRUE(face.has_value());
	const std::shared_ptr<const LoadedFont> held = *face;
	detail::reset_standard14_faces();
	ASSERT_NE(held->dwrite_face(), nullptr);
	UINT32 a = U'A';
	UINT16 index = 0;
	ASSERT_TRUE(SUCCEEDED(held->dwrite_face()->GetGlyphIndices(&a, 1u, &index)));
	ASSERT_NE(index, 0u);
	auto outline = held->outline(index);
	ASSERT_TRUE(outline.has_value()) << outline.error().message.view();
	EXPECT_FALSE(outline->points.empty());
}

} // namespace
} // namespace recto::pdf
