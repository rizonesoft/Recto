// Unit tests for embedded font loading, `LoadedFont` and the `glyf` fallback (D01 T02 section 2). The fixtures in
// `tests/corpus/fonts/` are written by `scripts/build-corpus.py` from glyph definitions whose geometry is stated in
// that script (units per em 1000), so every expected advance, box and outline below is computed by hand from them.
#include "filters.h"
#include "font.h"
#include "font_encoding.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include <wrl/client.h>

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
		recto::test::repo_root() / "src" / "pdf" / "adobe-core14-afm" / (std::string(font_name) + ".afm"));
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

// The two tables below are taken from Adobe's zapfdingbats.txt and glyphlist.txt (the Adobe Glyph List,
// adobe-type-tools/agl-aglfn at 4036a9c), whose notice they carry:
//
// Copyright 2002-2019 Adobe (http://www.adobe.com/).
//
// Redistribution and use in source and binary forms, with or
// without modification, are permitted provided that the
// following conditions are met:
//
// Redistributions of source code must retain the above
// copyright notice, this list of conditions and the following
// disclaimer.
//
// Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following
// disclaimer in the documentation and/or other materials
// provided with the distribution.
//
// Neither the name of Adobe nor the names of its contributors
// may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Adobe's zapfdingbats.txt (adobe-type-tools/agl-aglfn at 4036a9c): the Unicode scalar each ZapfDingbats glyph
// stands for, by code, every glyph but space. The test table only; the encoding itself is D01 T02 section 28's.
struct CodeUnicode {
	uint8_t code;
	char32_t unicode;
};
constexpr std::array<CodeUnicode, 201> kZapfDingbatsUnicode = {{
	{0x21, 0x2701}, {0x22, 0x2702}, {0x23, 0x2703}, {0x24, 0x2704}, {0x25, 0x260E}, {0x26, 0x2706}, {0x27, 0x2707},
	{0x28, 0x2708}, {0x29, 0x2709}, {0x2A, 0x261B}, {0x2B, 0x261E}, {0x2C, 0x270C}, {0x2D, 0x270D}, {0x2E, 0x270E},
	{0x2F, 0x270F}, {0x30, 0x2710}, {0x31, 0x2711}, {0x32, 0x2712}, {0x33, 0x2713}, {0x34, 0x2714}, {0x35, 0x2715},
	{0x36, 0x2716}, {0x37, 0x2717}, {0x38, 0x2718}, {0x39, 0x2719}, {0x3A, 0x271A}, {0x3B, 0x271B}, {0x3C, 0x271C},
	{0x3D, 0x271D}, {0x3E, 0x271E}, {0x3F, 0x271F}, {0x40, 0x2720}, {0x41, 0x2721}, {0x42, 0x2722}, {0x43, 0x2723},
	{0x44, 0x2724}, {0x45, 0x2725}, {0x46, 0x2726}, {0x47, 0x2727}, {0x48, 0x2605}, {0x49, 0x2729}, {0x4A, 0x272A},
	{0x4B, 0x272B}, {0x4C, 0x272C}, {0x4D, 0x272D}, {0x4E, 0x272E}, {0x4F, 0x272F}, {0x50, 0x2730}, {0x51, 0x2731},
	{0x52, 0x2732}, {0x53, 0x2733}, {0x54, 0x2734}, {0x55, 0x2735}, {0x56, 0x2736}, {0x57, 0x2737}, {0x58, 0x2738},
	{0x59, 0x2739}, {0x5A, 0x273A}, {0x5B, 0x273B}, {0x5C, 0x273C}, {0x5D, 0x273D}, {0x5E, 0x273E}, {0x5F, 0x273F},
	{0x60, 0x2740}, {0x61, 0x2741}, {0x62, 0x2742}, {0x63, 0x2743}, {0x64, 0x2744}, {0x65, 0x2745}, {0x66, 0x2746},
	{0x67, 0x2747}, {0x68, 0x2748}, {0x69, 0x2749}, {0x6A, 0x274A}, {0x6B, 0x274B}, {0x6C, 0x25CF}, {0x6D, 0x274D},
	{0x6E, 0x25A0}, {0x6F, 0x274F}, {0x70, 0x2750}, {0x71, 0x2751}, {0x72, 0x2752}, {0x73, 0x25B2}, {0x74, 0x25BC},
	{0x75, 0x25C6}, {0x76, 0x2756}, {0x77, 0x25D7}, {0x78, 0x2758}, {0x79, 0x2759}, {0x7A, 0x275A}, {0x7B, 0x275B},
	{0x7C, 0x275C}, {0x7D, 0x275D}, {0x7E, 0x275E}, {0x80, 0x2768}, {0x81, 0x2769}, {0x82, 0x276A}, {0x83, 0x276B},
	{0x84, 0x276C}, {0x85, 0x276D}, {0x86, 0x276E}, {0x87, 0x276F}, {0x88, 0x2770}, {0x89, 0x2771}, {0x8A, 0x2772},
	{0x8B, 0x2773}, {0x8C, 0x2774}, {0x8D, 0x2775}, {0xA1, 0x2761}, {0xA2, 0x2762}, {0xA3, 0x2763}, {0xA4, 0x2764},
	{0xA5, 0x2765}, {0xA6, 0x2766}, {0xA7, 0x2767}, {0xA8, 0x2663}, {0xA9, 0x2666}, {0xAA, 0x2665}, {0xAB, 0x2660},
	{0xAC, 0x2460}, {0xAD, 0x2461}, {0xAE, 0x2462}, {0xAF, 0x2463}, {0xB0, 0x2464}, {0xB1, 0x2465}, {0xB2, 0x2466},
	{0xB3, 0x2467}, {0xB4, 0x2468}, {0xB5, 0x2469}, {0xB6, 0x2776}, {0xB7, 0x2777}, {0xB8, 0x2778}, {0xB9, 0x2779},
	{0xBA, 0x277A}, {0xBB, 0x277B}, {0xBC, 0x277C}, {0xBD, 0x277D}, {0xBE, 0x277E}, {0xBF, 0x277F}, {0xC0, 0x2780},
	{0xC1, 0x2781}, {0xC2, 0x2782}, {0xC3, 0x2783}, {0xC4, 0x2784}, {0xC5, 0x2785}, {0xC6, 0x2786}, {0xC7, 0x2787},
	{0xC8, 0x2788}, {0xC9, 0x2789}, {0xCA, 0x278A}, {0xCB, 0x278B}, {0xCC, 0x278C}, {0xCD, 0x278D}, {0xCE, 0x278E},
	{0xCF, 0x278F}, {0xD0, 0x2790}, {0xD1, 0x2791}, {0xD2, 0x2792}, {0xD3, 0x2793}, {0xD4, 0x2794}, {0xD5, 0x2192},
	{0xD6, 0x2194}, {0xD7, 0x2195}, {0xD8, 0x2798}, {0xD9, 0x2799}, {0xDA, 0x279A}, {0xDB, 0x279B}, {0xDC, 0x279C},
	{0xDD, 0x279D}, {0xDE, 0x279E}, {0xDF, 0x279F}, {0xE0, 0x27A0}, {0xE1, 0x27A1}, {0xE2, 0x27A2}, {0xE3, 0x27A3},
	{0xE4, 0x27A4}, {0xE5, 0x27A5}, {0xE6, 0x27A6}, {0xE7, 0x27A7}, {0xE8, 0x27A8}, {0xE9, 0x27A9}, {0xEA, 0x27AA},
	{0xEB, 0x27AB}, {0xEC, 0x27AC}, {0xED, 0x27AD}, {0xEE, 0x27AE}, {0xEF, 0x27AF}, {0xF1, 0x27B1}, {0xF2, 0x27B2},
	{0xF3, 0x27B3}, {0xF4, 0x27B4}, {0xF5, 0x27B5}, {0xF6, 0x27B6}, {0xF7, 0x27B7}, {0xF8, 0x27B8}, {0xF9, 0x27B9},
	{0xFA, 0x27BA}, {0xFB, 0x27BB}, {0xFC, 0x27BC}, {0xFD, 0x27BD}, {0xFE, 0x27BE}
}};

// Adobe's glyphlist.txt (the Adobe Glyph List, adobe-type-tools/agl-aglfn at 4036a9c): the Unicode scalar of each of
// the 315 glyph names in the twelve Latin Core14 AFM files, sorted by name, but for `commaaccent`, which the list
// maps to Adobe's private-use U+F6C3 that no Windows face has: here its Unicode character, U+0326 COMBINING COMMA
// BELOW. The test table only (D01 T02 section 28 owns the list, and that mapping).
struct NameUnicode {
	std::string_view name;
	char32_t unicode;
};
constexpr std::array<NameUnicode, 315> kLatinUnicode = {{
	{"A", 0x0041}, {"AE", 0x00C6}, {"Aacute", 0x00C1}, {"Abreve", 0x0102}, {"Acircumflex", 0x00C2},
	{"Adieresis", 0x00C4}, {"Agrave", 0x00C0}, {"Amacron", 0x0100}, {"Aogonek", 0x0104}, {"Aring", 0x00C5},
	{"Atilde", 0x00C3}, {"B", 0x0042}, {"C", 0x0043}, {"Cacute", 0x0106}, {"Ccaron", 0x010C}, {"Ccedilla", 0x00C7},
	{"D", 0x0044}, {"Dcaron", 0x010E}, {"Dcroat", 0x0110}, {"Delta", 0x2206}, {"E", 0x0045}, {"Eacute", 0x00C9},
	{"Ecaron", 0x011A}, {"Ecircumflex", 0x00CA}, {"Edieresis", 0x00CB}, {"Edotaccent", 0x0116}, {"Egrave", 0x00C8},
	{"Emacron", 0x0112}, {"Eogonek", 0x0118}, {"Eth", 0x00D0}, {"Euro", 0x20AC}, {"F", 0x0046}, {"G", 0x0047},
	{"Gbreve", 0x011E}, {"Gcommaaccent", 0x0122}, {"H", 0x0048}, {"I", 0x0049}, {"Iacute", 0x00CD},
	{"Icircumflex", 0x00CE}, {"Idieresis", 0x00CF}, {"Idotaccent", 0x0130}, {"Igrave", 0x00CC}, {"Imacron", 0x012A},
	{"Iogonek", 0x012E}, {"J", 0x004A}, {"K", 0x004B}, {"Kcommaaccent", 0x0136}, {"L", 0x004C}, {"Lacute", 0x0139},
	{"Lcaron", 0x013D}, {"Lcommaaccent", 0x013B}, {"Lslash", 0x0141}, {"M", 0x004D}, {"N", 0x004E},
	{"Nacute", 0x0143}, {"Ncaron", 0x0147}, {"Ncommaaccent", 0x0145}, {"Ntilde", 0x00D1}, {"O", 0x004F},
	{"OE", 0x0152}, {"Oacute", 0x00D3}, {"Ocircumflex", 0x00D4}, {"Odieresis", 0x00D6}, {"Ograve", 0x00D2},
	{"Ohungarumlaut", 0x0150}, {"Omacron", 0x014C}, {"Oslash", 0x00D8}, {"Otilde", 0x00D5}, {"P", 0x0050},
	{"Q", 0x0051}, {"R", 0x0052}, {"Racute", 0x0154}, {"Rcaron", 0x0158}, {"Rcommaaccent", 0x0156}, {"S", 0x0053},
	{"Sacute", 0x015A}, {"Scaron", 0x0160}, {"Scedilla", 0x015E}, {"Scommaaccent", 0x0218}, {"T", 0x0054},
	{"Tcaron", 0x0164}, {"Tcommaaccent", 0x0162}, {"Thorn", 0x00DE}, {"U", 0x0055}, {"Uacute", 0x00DA},
	{"Ucircumflex", 0x00DB}, {"Udieresis", 0x00DC}, {"Ugrave", 0x00D9}, {"Uhungarumlaut", 0x0170},
	{"Umacron", 0x016A}, {"Uogonek", 0x0172}, {"Uring", 0x016E}, {"V", 0x0056}, {"W", 0x0057}, {"X", 0x0058},
	{"Y", 0x0059}, {"Yacute", 0x00DD}, {"Ydieresis", 0x0178}, {"Z", 0x005A}, {"Zacute", 0x0179}, {"Zcaron", 0x017D},
	{"Zdotaccent", 0x017B}, {"a", 0x0061}, {"aacute", 0x00E1}, {"abreve", 0x0103}, {"acircumflex", 0x00E2},
	{"acute", 0x00B4}, {"adieresis", 0x00E4}, {"ae", 0x00E6}, {"agrave", 0x00E0}, {"amacron", 0x0101},
	{"ampersand", 0x0026}, {"aogonek", 0x0105}, {"aring", 0x00E5}, {"asciicircum", 0x005E}, {"asciitilde", 0x007E},
	{"asterisk", 0x002A}, {"at", 0x0040}, {"atilde", 0x00E3}, {"b", 0x0062}, {"backslash", 0x005C}, {"bar", 0x007C},
	{"braceleft", 0x007B}, {"braceright", 0x007D}, {"bracketleft", 0x005B}, {"bracketright", 0x005D},
	{"breve", 0x02D8}, {"brokenbar", 0x00A6}, {"bullet", 0x2022}, {"c", 0x0063}, {"cacute", 0x0107},
	{"caron", 0x02C7}, {"ccaron", 0x010D}, {"ccedilla", 0x00E7}, {"cedilla", 0x00B8}, {"cent", 0x00A2},
	{"circumflex", 0x02C6}, {"colon", 0x003A}, {"comma", 0x002C}, {"commaaccent", 0x0326}, {"copyright", 0x00A9},
	{"currency", 0x00A4}, {"d", 0x0064}, {"dagger", 0x2020}, {"daggerdbl", 0x2021}, {"dcaron", 0x010F},
	{"dcroat", 0x0111}, {"degree", 0x00B0}, {"dieresis", 0x00A8}, {"divide", 0x00F7}, {"dollar", 0x0024},
	{"dotaccent", 0x02D9}, {"dotlessi", 0x0131}, {"e", 0x0065}, {"eacute", 0x00E9}, {"ecaron", 0x011B},
	{"ecircumflex", 0x00EA}, {"edieresis", 0x00EB}, {"edotaccent", 0x0117}, {"egrave", 0x00E8}, {"eight", 0x0038},
	{"ellipsis", 0x2026}, {"emacron", 0x0113}, {"emdash", 0x2014}, {"endash", 0x2013}, {"eogonek", 0x0119},
	{"equal", 0x003D}, {"eth", 0x00F0}, {"exclam", 0x0021}, {"exclamdown", 0x00A1}, {"f", 0x0066}, {"fi", 0xFB01},
	{"five", 0x0035}, {"fl", 0xFB02}, {"florin", 0x0192}, {"four", 0x0034}, {"fraction", 0x2044}, {"g", 0x0067},
	{"gbreve", 0x011F}, {"gcommaaccent", 0x0123}, {"germandbls", 0x00DF}, {"grave", 0x0060}, {"greater", 0x003E},
	{"greaterequal", 0x2265}, {"guillemotleft", 0x00AB}, {"guillemotright", 0x00BB}, {"guilsinglleft", 0x2039},
	{"guilsinglright", 0x203A}, {"h", 0x0068}, {"hungarumlaut", 0x02DD}, {"hyphen", 0x002D}, {"i", 0x0069},
	{"iacute", 0x00ED}, {"icircumflex", 0x00EE}, {"idieresis", 0x00EF}, {"igrave", 0x00EC}, {"imacron", 0x012B},
	{"iogonek", 0x012F}, {"j", 0x006A}, {"k", 0x006B}, {"kcommaaccent", 0x0137}, {"l", 0x006C}, {"lacute", 0x013A},
	{"lcaron", 0x013E}, {"lcommaaccent", 0x013C}, {"less", 0x003C}, {"lessequal", 0x2264}, {"logicalnot", 0x00AC},
	{"lozenge", 0x25CA}, {"lslash", 0x0142}, {"m", 0x006D}, {"macron", 0x00AF}, {"minus", 0x2212}, {"mu", 0x00B5},
	{"multiply", 0x00D7}, {"n", 0x006E}, {"nacute", 0x0144}, {"ncaron", 0x0148}, {"ncommaaccent", 0x0146},
	{"nine", 0x0039}, {"notequal", 0x2260}, {"ntilde", 0x00F1}, {"numbersign", 0x0023}, {"o", 0x006F},
	{"oacute", 0x00F3}, {"ocircumflex", 0x00F4}, {"odieresis", 0x00F6}, {"oe", 0x0153}, {"ogonek", 0x02DB},
	{"ograve", 0x00F2}, {"ohungarumlaut", 0x0151}, {"omacron", 0x014D}, {"one", 0x0031}, {"onehalf", 0x00BD},
	{"onequarter", 0x00BC}, {"onesuperior", 0x00B9}, {"ordfeminine", 0x00AA}, {"ordmasculine", 0x00BA},
	{"oslash", 0x00F8}, {"otilde", 0x00F5}, {"p", 0x0070}, {"paragraph", 0x00B6}, {"parenleft", 0x0028},
	{"parenright", 0x0029}, {"partialdiff", 0x2202}, {"percent", 0x0025}, {"period", 0x002E},
	{"periodcentered", 0x00B7}, {"perthousand", 0x2030}, {"plus", 0x002B}, {"plusminus", 0x00B1}, {"q", 0x0071},
	{"question", 0x003F}, {"questiondown", 0x00BF}, {"quotedbl", 0x0022}, {"quotedblbase", 0x201E},
	{"quotedblleft", 0x201C}, {"quotedblright", 0x201D}, {"quoteleft", 0x2018}, {"quoteright", 0x2019},
	{"quotesinglbase", 0x201A}, {"quotesingle", 0x0027}, {"r", 0x0072}, {"racute", 0x0155}, {"radical", 0x221A},
	{"rcaron", 0x0159}, {"rcommaaccent", 0x0157}, {"registered", 0x00AE}, {"ring", 0x02DA}, {"s", 0x0073},
	{"sacute", 0x015B}, {"scaron", 0x0161}, {"scedilla", 0x015F}, {"scommaaccent", 0x0219}, {"section", 0x00A7},
	{"semicolon", 0x003B}, {"seven", 0x0037}, {"six", 0x0036}, {"slash", 0x002F}, {"space", 0x0020},
	{"sterling", 0x00A3}, {"summation", 0x2211}, {"t", 0x0074}, {"tcaron", 0x0165}, {"tcommaaccent", 0x0163},
	{"thorn", 0x00FE}, {"three", 0x0033}, {"threequarters", 0x00BE}, {"threesuperior", 0x00B3}, {"tilde", 0x02DC},
	{"trademark", 0x2122}, {"two", 0x0032}, {"twosuperior", 0x00B2}, {"u", 0x0075}, {"uacute", 0x00FA},
	{"ucircumflex", 0x00FB}, {"udieresis", 0x00FC}, {"ugrave", 0x00F9}, {"uhungarumlaut", 0x0171},
	{"umacron", 0x016B}, {"underscore", 0x005F}, {"uogonek", 0x0173}, {"uring", 0x016F}, {"v", 0x0076}, {"w", 0x0077},
	{"x", 0x0078}, {"y", 0x0079}, {"yacute", 0x00FD}, {"ydieresis", 0x00FF}, {"yen", 0x00A5}, {"z", 0x007A},
	{"zacute", 0x017A}, {"zcaron", 0x017E}, {"zdotaccent", 0x017C}, {"zero", 0x0030}
}};

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
			for (const auto& [code, unicode] : kZapfDingbatsUnicode) { // every glyph but space
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
	// Threads that load a face at once all get the same one, read once: the others wait for it (completion review gap 1).
	{
		detail::reset_standard14_faces();
		const uint64_t reads_before = detail::standard14_face_reads();
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
		EXPECT_EQ(detail::standard14_face_reads(), reads_before + 1u);
		const auto warm_started = std::chrono::steady_clock::now();
		EXPECT_EQ(load_standard14_face(Standard14Font::HelveticaBold)->get(), got[0].get());
		const auto warm = std::chrono::steady_clock::now() - warm_started;
		EXPECT_EQ(detail::standard14_face_reads(), reads_before + 1u); // a warm lookup reads nothing
		detail::reset_standard14_faces();
		const auto cold_started = std::chrono::steady_clock::now();
		ASSERT_TRUE(load_standard14_face(Standard14Font::TimesRoman).has_value());
		const auto cold = std::chrono::steady_clock::now() - cold_started;
		const auto us = [](auto d) { return std::chrono::duration_cast<std::chrono::microseconds>(d).count(); };
		::testing::Test::RecordProperty("cold_load_us", std::to_string(us(cold)));
		::testing::Test::RecordProperty("warm_load_us", std::to_string(us(warm)));
		std::printf("Standard 14 face load: cold %lld us, warm %lld us\n", static_cast<long long>(us(cold)),
			static_cast<long long>(us(warm)));
		EXPECT_LT(us(warm), 1000);
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

// Completion review gap 5: every glyph of every standard font draws, and draws the right glyph. Arial, Times New Roman
// and Courier New are metric-compatible with Helvetica, Times and Courier, as Windows' Symbol is with Adobe's, so each
// face glyph's advance matches the AFM width (within 2/1000 em of rounding) wherever the face is the font's own; a wrong
// glyph would show a different width. ZapfDingbats is drawn from Segoe UI Symbol, a different design, so its glyphs are
// checked for presence and outline only. Space has an advance and no outline.
// The glyphs whose Arial or Times New Roman design is not as wide as Adobe's (reviewed 2026-10-26 on Windows 11): the
// accents, signs and caron letters Adobe added to the Core14 set in 1997, which Monotype did not match. Layout keeps the
// AFM width; drawing fits the face's glyph to it (D01 T02 section 4). And `commaaccent`, a spacing accent in the AFM,
// drawn from the combining U+0326, which has no advance. Courier New and Symbol match everywhere.
constexpr std::array<std::string_view, 15> kKnownWidthDifferences = {"Lcaron", "commaaccent", "dcaron", "divide", "imacron", "lcaron",
	"lozenge", "macron", "mu", "partialdiff", "periodcentered", "plusminus", "radical", "summation", "tcaron"};

TEST(PdfFont, Standard14Repertoire)
{
	const auto unicode_of = [](std::string_view name) -> std::optional<char32_t> {
		const auto it = std::lower_bound(kLatinUnicode.begin(), kLatinUnicode.end(), name,
			[](const NameUnicode& row, std::string_view key) { return row.name < key; });
		return it != kLatinUnicode.end() && it->name == name ? std::optional<char32_t>(it->unicode) : std::nullopt;
	};
	for (const Standard14Font font : kAllStandard14) {
		const Standard14Metrics& m = standard14_metrics(font);
		SCOPED_TRACE(std::string(m.base_font));
		std::vector<std::string> missing;
		std::vector<std::string> wrong_width;
		size_t known_differences = 0;
		size_t checked = 0;
		size_t unencoded = 0;
		const auto check = [&](const Standard14Glyph& g, char32_t key, bool own_face) {
			auto glyph = standard14_face_glyph(font, key);
			ASSERT_TRUE(glyph.has_value()) << glyph.error().message.view();
			if (!glyph->has_value()) {
				missing.emplace_back(g.name);
				return;
			}
			const LoadedFont& face = *(*glyph)->face;
			auto advance = face.advance((*glyph)->glyph);
			ASSERT_TRUE(advance.has_value());
			if (own_face && std::abs(*advance * 1000.0 - g.width) > 2.0) {
				if (std::find(kKnownWidthDifferences.begin(), kKnownWidthDifferences.end(), g.name) !=
					kKnownWidthDifferences.end()) {
					++known_differences;
				} else {
					wrong_width.push_back(std::string(g.name) + " " + std::to_string(*advance * 1000.0) + " vs " +
						std::to_string(g.width));
				}
			}
			auto outline = face.outline((*glyph)->glyph);
			ASSERT_TRUE(outline.has_value()) << g.name;
			EXPECT_EQ(outline->points.empty(), g.name == "space") << g.name;
			++checked;
		};
		if (font == Standard14Font::Symbol) {
			for (const Standard14Glyph& g : m.glyphs) {
				if (g.code < 0) {
					// Adobe's apple: no code reaches it, and the Glyph List names it only by private-use U+F8FF, which no
					// Windows face draws, so it is the one glyph with nothing to draw: recorded, not counted as drawn.
					ASSERT_EQ(g.name, "apple");
					auto none = standard14_face_glyph(font, U'\xF8FF');
					ASSERT_TRUE(none.has_value());
					EXPECT_FALSE(none->has_value());
					++unencoded;
					continue;
				}
				check(g, static_cast<char32_t>(g.code), g.name != "Euro");
			}
		} else if (font == Standard14Font::ZapfDingbats) {
			for (const auto& [code, unicode] : kZapfDingbatsUnicode) {
				const Standard14Glyph* g = standard14_glyph_by_code(font, code);
				ASSERT_NE(g, nullptr) << int{code};
				check(*g, unicode, false);
			}
			check(*standard14_glyph_by_name(font, "space"), U' ', false);
		} else {
			for (const Standard14Glyph& g : m.glyphs) {
				const std::optional<char32_t> unicode = unicode_of(g.name);
				ASSERT_TRUE(unicode.has_value()) << g.name;
				check(g, *unicode, true);
			}
		}
		EXPECT_EQ(checked + unencoded, m.glyphs.size());
		EXPECT_EQ(unencoded, font == Standard14Font::Symbol ? 1u : 0u);
		EXPECT_TRUE(missing.empty()) << "no face glyph: " << ::testing::PrintToString(missing);
		EXPECT_TRUE(wrong_width.empty()) << "advance differs from the AFM: " << ::testing::PrintToString(wrong_width);
		EXPECT_LE(known_differences, kKnownWidthDifferences.size());
	}
}

// The first system family whose closest match to bold (700) is a real face of another weight, not a simulation.
[[nodiscard]] std::optional<std::wstring> partial_bold_family()
{
	Microsoft::WRL::ComPtr<IDWriteFactory> factory;
	Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) ||
		FAILED(factory->GetSystemFontCollection(collection.GetAddressOf(), FALSE))) {
		return std::nullopt;
	}
	for (UINT32 i = 0; i < collection->GetFontFamilyCount(); ++i) {
		Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
		Microsoft::WRL::ComPtr<IDWriteFont> font;
		Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
		if (FAILED(collection->GetFontFamily(i, family.GetAddressOf())) ||
			FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STRETCH_NORMAL,
				DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf())) ||
			font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE || font->GetWeight() == DWRITE_FONT_WEIGHT_BOLD ||
			font->GetStretch() != DWRITE_FONT_STRETCH_NORMAL || FAILED(family->GetFamilyNames(names.GetAddressOf()))) {
			continue;
		}
		UINT32 length = 0;
		if (FAILED(names->GetStringLength(0, &length))) {
			continue;
		}
		std::wstring name(length + 1u, L'\0');
		if (SUCCEEDED(names->GetString(0, name.data(), length + 1u))) {
			name.resize(length);
			return name;
		}
	}
	return std::nullopt;
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
	// A family whose closest face to bold is a real face of another weight (no simulation) is refused, not drawn in
	// that weight (completion review gap 2). Which families are partial depends on the fonts installed, so the test finds
	// one in the system collection, the first whose closest match to bold is real and not 700.
	if (const std::optional<std::wstring> family = partial_bold_family(); !family.has_value()) {
		std::printf("Standard 14 weight check not run: no installed family has a real face nearest bold that is not bold\n");
	} else {
		const detail::ScopedStandard14Family partial(Standard14Font::HelveticaBold, *family);
		auto face = load_standard14_face(Standard14Font::HelveticaBold);
		ASSERT_FALSE(face.has_value());
		EXPECT_EQ(face.error().code, ErrorCode::UnsupportedFeature);
		EXPECT_NE(face.error().message.view().find("weight, width and style"), std::string_view::npos)
			<< face.error().message.view();
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



// ---------------------------------------------------------------------------------------------------------------------
// Simple-font encodings, glyph names, TrueType selection and ToUnicode (D01 T02 section 28)
// ---------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::u32string u32(std::u32string_view text)
{
	return std::u32string(text);
}

// The character a Windows code page gives a byte, or none.
[[nodiscard]] std::optional<char32_t> code_page_char(UINT code_page, uint8_t byte)
{
	wchar_t wide[2]{};
	const char narrow = static_cast<char>(byte);
	const int n = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, &narrow, 1, wide, 2);
	if (n != 1) {
		return std::nullopt;
	}
	return static_cast<char32_t>(wide[0]);
}

TEST(PdfFont, BaseEncodings)
{
	// StandardEncoding, Symbol and ZapfDingbats: every code against Adobe's Core14 AFM files, read here.
	for (const auto& [encoding, font] : {std::pair{BaseEncoding::Standard, std::string_view("Times-Roman")},
			 std::pair{BaseEncoding::Symbol, std::string_view("Symbol")},
			 std::pair{BaseEncoding::ZapfDingbats, std::string_view("ZapfDingbats")}}) {
		SCOPED_TRACE(std::string(font));
		std::array<std::string, 256> want{};
		for (const AfmFile::Glyph& g : read_afm(font).glyphs) {
			if (g.code >= 0) {
				want[static_cast<size_t>(g.code)] = g.name;
			}
		}
		for (int code = 0; code < 256; ++code) {
			EXPECT_EQ(base_encoding_name(encoding, static_cast<uint8_t>(code)), want[static_cast<size_t>(code)]) << code;
		}
	}
	// WinAnsiEncoding and MacRomanEncoding: every code against Windows' own code pages 1252 and 10000, through the
	// glyph name's Unicode, but for what Annex D's notes make different (space and hyphen also at 0240, 0255 and
	// 0312, bullet at WinAnsi's unused codes, currency where code page 10000 now has the euro).
	const auto check = [](BaseEncoding encoding, UINT code_page, std::initializer_list<std::pair<int, char32_t>> notes) {
		size_t encoded = 0;
		for (int code = 0x20; code < 256; ++code) {
			const std::string_view name = base_encoding_name(encoding, static_cast<uint8_t>(code));
			char32_t want = U'\0';
			bool noted = false;
			for (const auto& [note_code, note_char] : notes) {
				if (note_code == code) {
					want = note_char;
					noted = true;
				}
			}
			if (!noted) {
				const std::optional<char32_t> page = code_page_char(code_page, static_cast<uint8_t>(code));
				if (!page.has_value()) {
					ADD_FAILURE() << code << " has no character in the code page";
					continue;
				}
				want = *page;
			}
			if (name.empty()) {
				EXPECT_TRUE(noted || want < 0x20u || (want >= 0x7Fu && want < 0xA0u)) << code << " has no name";
				continue;
			}
			++encoded;
			auto unicode = glyph_name_unicode(name);
			if (!unicode.has_value()) {
				ADD_FAILURE() << name;
				continue;
			}
			EXPECT_EQ(*unicode, std::u32string(1u, want)) << code << " " << name;
		}
		return encoded;
	};
	EXPECT_EQ(check(BaseEncoding::WinAnsi, 1252u,
				  {{0x7F, U'•'}, {0x81, U'•'}, {0x8D, U'•'}, {0x8F, U'•'}, {0x90, U'•'},
					  {0x9D, U'•'}, {0xA0, U' '}, {0xAD, U'-'}}),
		224u - 0u); // codes 0x20 to 0xFF: 224 names
	const size_t mac_encoded = check(BaseEncoding::MacRoman, 10000u,
		{{0xCA, U' '}, {0xDB, U'¤'}, {0x7F, U'\0'}, {0xAD, U'\0'}, {0xB0, U'\0'}, {0xB2, U'\0'}, {0xB3, U'\0'},
			{0xB6, U'\0'}, {0xB7, U'\0'}, {0xB8, U'\0'}, {0xB9, U'\0'}, {0xBA, U'\0'}, {0xBD, U'\0'}, {0xC3, U'\0'},
			{0xC5, U'\0'}, {0xC6, U'\0'}, {0xD7, U'\0'}, {0xF0, U'\0'}});
	EXPECT_EQ(mac_encoded, 208u);
	// The Mac OS standard Roman set has those fifteen too, at code page 10000's places.
	for (const auto& [name, code] : {std::pair{std::string_view("notequal"), 0xADu}, std::pair{std::string_view("apple"), 0xF0u},
			 std::pair{std::string_view("Omega"), 0xBDu}, std::pair{std::string_view("Adieresis"), 0x80u}}) {
		EXPECT_EQ(mac_os_roman_code(name), std::optional<uint8_t>(static_cast<uint8_t>(code))) << name;
	}
	EXPECT_EQ(mac_os_roman_code("space"), std::optional<uint8_t>(uint8_t{0x20u})); // the first of its two codes
	EXPECT_EQ(mac_os_roman_code("Euro"), std::optional<uint8_t>(uint8_t{0xDBu})); // Table 115: Euro for currency
	EXPECT_EQ(mac_os_roman_code("currency"), std::nullopt);
	// MacExpertEncoding: Annex D's 165 codes, every name one the Glyph List knows.
	size_t expert = 0;
	for (int code = 0; code < 256; ++code) {
		if (const std::string_view name = base_encoding_name(BaseEncoding::MacExpert, static_cast<uint8_t>(code)); !name.empty()) {
			++expert;
			EXPECT_FALSE(glyph_name_unicode(name)->empty()) << name;
		}
	}
	EXPECT_EQ(expert, 165u);
	EXPECT_EQ(base_encoding_name(BaseEncoding::MacExpert, 041u), "exclamsmall");
	EXPECT_EQ(base_encoding_from_name("WinAnsiEncoding"), std::optional<BaseEncoding>(BaseEncoding::WinAnsi));
	EXPECT_EQ(base_encoding_from_name("StandardEncoding"), std::optional<BaseEncoding>(BaseEncoding::Standard));
	EXPECT_EQ(base_encoding_from_name("Identity-H"), std::nullopt);
}

TEST(PdfFont, EncodingAndDifferences)
{
	// Glyph names by the AGL specification.
	const std::array<std::pair<std::string_view, std::u32string_view>, 16> names = {{
		{"A", U"A"}, {"a.sc", U"a"}, {"f_i", U"fi"}, {"uni00410042", U"AB"}, {"u1F600", U"\U0001F600"},
		{"uni00E9", U"é"}, {"uni00e9", U""}, {"uniD800", U""}, {"u110000", U""}, {"uni004", U""},
		{"g123", U""}, {"Euro", U"€"}, {"dalethatafpatah", U"דֲ"}, {"", U""}, {".notdef", U""},
		{"T_h.alt", U"Th"},
	}};
	for (const auto& [name, want] : names) {
		auto got = glyph_name_unicode(name);
		ASSERT_TRUE(got.has_value());
		EXPECT_EQ(*got, u32(want)) << name;
	}
	EXPECT_EQ(*glyph_name_unicode("a1", true), U"✁"); // zapfdingbats.txt for a ZapfDingbats font
	EXPECT_EQ(*glyph_name_unicode("a1", false), U"");
	EXPECT_EQ(*glyph_name_unicode("space", true), U" "); // not in zapfdingbats.txt: the AGL, as for any font

	// /Differences over WinAnsiEncoding: runs of codes after each number, unknown names kept, bad entries skipped.
	PdfArray differences;
	for (const PdfValue& v : {PdfValue(int64_t{32}), PdfValue(PdfName("a")), PdfValue(PdfName("b")), PdfValue(int64_t{128}),
			 PdfValue(PdfName("Euro")), PdfValue(PdfName("zz")), PdfValue(PdfName("uni00410042")), PdfValue(int64_t{300}),
			 PdfValue(PdfName("lost")), PdfValue(int64_t{140}), PdfValue(true), PdfValue(PdfName("f_i"))}) {
		differences.push_back(v);
	}
	PdfDict dict;
	dict.set("BaseEncoding", PdfValue(PdfName("WinAnsiEncoding")));
	dict.set("Differences", PdfValue(std::move(differences)));
	const PdfValue encoding{std::move(dict)};
	auto built = SimpleEncoding::build(&encoding, false, nullptr);
	ASSERT_TRUE(built.has_value()) << built.error().message.view();
	EXPECT_EQ(built->name(32), "a");
	EXPECT_EQ(built->name(33), "b");
	EXPECT_EQ(built->name(34), "quotedbl"); // past the run: WinAnsi again
	EXPECT_EQ(built->name(128), "Euro");
	EXPECT_EQ(built->name(129), "zz");
	EXPECT_EQ(built->name(130), "uni00410042");
	EXPECT_EQ(built->name(131), "florin"); // the name after 300 had no code to go to
	EXPECT_EQ(built->name(140), "f_i");    // a boolean in the array is skipped, the run goes on
	EXPECT_TRUE(built->has_differences());
	EXPECT_EQ(built->named_base(), std::optional<BaseEncoding>(BaseEncoding::WinAnsi));
	EXPECT_EQ(*glyph_name_unicode(built->name(130)), U"AB");
	EXPECT_EQ(*glyph_name_unicode(built->name(129)), U"");
}

TEST(PdfFont, EffectiveEncoding)
{
	const auto named = [](std::string_view name) { return PdfValue(PdfName(name)); };
	GlyphNames custom{};
	custom[0x41] = "alpha";
	custom[0x42] = "beta";
	// A named encoding.
	{
		const PdfValue value = named("WinAnsiEncoding");
		auto e = SimpleEncoding::build(&value, false, &custom);
		ASSERT_TRUE(e.has_value());
		EXPECT_EQ(e->name(0x80), "Euro");
		EXPECT_EQ(e->named_base(), std::optional<BaseEncoding>(BaseEncoding::WinAnsi));
		EXPECT_FALSE(e->has_differences());
	}
	// A dictionary with /BaseEncoding.
	{
		PdfDict dict;
		dict.set("BaseEncoding", named("MacRomanEncoding"));
		const PdfValue value{std::move(dict)};
		auto e = SimpleEncoding::build(&value, true, &custom);
		ASSERT_TRUE(e.has_value());
		EXPECT_EQ(e->name(0x80), "Adieresis");
		EXPECT_EQ(e->named_base(), std::optional<BaseEncoding>(BaseEncoding::MacRoman));
	}
	// A dictionary without /BaseEncoding: StandardEncoding for a non-symbolic font, the built-in for a symbolic one.
	{
		PdfArray differences;
		differences.push_back(PdfValue(int64_t{0x42}));
		differences.push_back(named("gamma"));
		PdfDict dict;
		dict.set("Differences", PdfValue(std::move(differences)));
		const PdfValue value{std::move(dict)};
		auto plain = SimpleEncoding::build(&value, false, &custom);
		ASSERT_TRUE(plain.has_value());
		EXPECT_EQ(plain->name(0x27), "quoteright");
		EXPECT_EQ(plain->name(0x41), "A");
		EXPECT_EQ(plain->name(0x42), "gamma");
		EXPECT_EQ(plain->named_base(), std::nullopt);
		auto symbolic = SimpleEncoding::build(&value, true, &custom);
		ASSERT_TRUE(symbolic.has_value());
		EXPECT_EQ(symbolic->name(0x41), "alpha"); // the custom built-in encoding
		EXPECT_EQ(symbolic->name(0x42), "gamma"); // with /Differences over it
		EXPECT_EQ(symbolic->name(0x27), "");
	}
	// No /Encoding: the built-in encoding; StandardEncoding for a non-symbolic font without one; none for a symbolic.
	{
		auto with = SimpleEncoding::build(nullptr, true, &custom);
		EXPECT_EQ(with->name(0x41), "alpha");
		auto standard = SimpleEncoding::build(nullptr, false, nullptr);
		EXPECT_EQ(standard->name(0x41), "A");
		auto none = SimpleEncoding::build(nullptr, true, nullptr);
		EXPECT_EQ(none->name(0x41), "");
		const PdfValue unknown = named("FooEncoding"); // an unknown name: as if absent
		EXPECT_EQ(SimpleEncoding::build(&unknown, true, &custom)->name(0x42), "beta");
	}
	// A /Differences array past its bound.
	{
		PdfArray big;
		for (size_t i = 0; i <= kMaxDifferencesEntries; ++i) {
			big.push_back(PdfValue(int64_t{1}));
		}
		PdfDict dict;
		dict.set("Differences", PdfValue(std::move(big)));
		const PdfValue value{std::move(dict)};
		RECTO_EXPECT_ERR(SimpleEncoding::build(&value, false, nullptr), ErrorCode::ResourceLimit);
	}
}

// Minimal sfnt programs for the TrueType tests: a table directory holding only `cmap` and `post` tables.
void be16(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(static_cast<uint8_t>(v >> 8u));
	out.push_back(static_cast<uint8_t>(v));
}
void be32(std::vector<uint8_t>& out, uint32_t v)
{
	be16(out, v >> 16u);
	be16(out, v & 0xFFFFu);
}

struct CmapPart {
	uint16_t platform;
	uint16_t encoding;
	std::vector<uint8_t> bytes;
};

[[nodiscard]] std::vector<uint8_t> cmap_table(const std::vector<CmapPart>& parts)
{
	std::vector<uint8_t> out;
	be16(out, 0u);
	be16(out, static_cast<uint32_t>(parts.size()));
	uint32_t offset = 4u + 8u * static_cast<uint32_t>(parts.size());
	for (const CmapPart& part : parts) {
		be16(out, part.platform);
		be16(out, part.encoding);
		be32(out, offset);
		offset += static_cast<uint32_t>(part.bytes.size());
	}
	for (const CmapPart& part : parts) {
		out.insert(out.end(), part.bytes.begin(), part.bytes.end());
	}
	return out;
}

// Format 4, one segment per code (idDelta, no range offsets), then the closing segment.
[[nodiscard]] std::vector<uint8_t> format4(const std::vector<std::pair<uint32_t, uint32_t>>& codes)
{
	const uint32_t segments = static_cast<uint32_t>(codes.size()) + 1u;
	std::vector<uint8_t> out;
	be16(out, 4u);
	be16(out, 16u + segments * 8u);
	be16(out, 0u);
	be16(out, segments * 2u);
	be16(out, 0u);
	be16(out, 0u);
	be16(out, 0u);
	for (const auto& [code, glyph] : codes) {
		be16(out, code);
	}
	be16(out, 0xFFFFu);
	be16(out, 0u);
	for (const auto& [code, glyph] : codes) {
		be16(out, code);
	}
	be16(out, 0xFFFFu);
	for (const auto& [code, glyph] : codes) {
		be16(out, (glyph - code) & 0xFFFFu);
	}
	be16(out, 1u);
	for (size_t i = 0; i < segments; ++i) {
		be16(out, 0u);
	}
	return out;
}

[[nodiscard]] std::vector<uint8_t> format0(const std::vector<std::pair<uint32_t, uint32_t>>& codes)
{
	std::vector<uint8_t> out;
	be16(out, 0u);
	be16(out, 262u);
	be16(out, 0u);
	std::array<uint8_t, 256> glyphs{};
	for (const auto& [code, glyph] : codes) {
		glyphs[code & 0xFFu] = static_cast<uint8_t>(glyph);
	}
	out.insert(out.end(), glyphs.begin(), glyphs.end());
	return out;
}

[[nodiscard]] std::vector<uint8_t> format6(uint16_t first, const std::vector<uint16_t>& glyphs)
{
	std::vector<uint8_t> out;
	be16(out, 6u);
	be16(out, 10u + 2u * static_cast<uint32_t>(glyphs.size()));
	be16(out, 0u);
	be16(out, first);
	be16(out, static_cast<uint32_t>(glyphs.size()));
	for (const uint16_t g : glyphs) {
		be16(out, g);
	}
	return out;
}

[[nodiscard]] std::vector<uint8_t> format12(const std::vector<std::array<uint32_t, 3>>& groups)
{
	std::vector<uint8_t> out;
	be16(out, 12u);
	be16(out, 0u);
	be32(out, 16u + 12u * static_cast<uint32_t>(groups.size()));
	be32(out, 0u);
	be32(out, static_cast<uint32_t>(groups.size()));
	for (const auto& g : groups) {
		be32(out, g[0]);
		be32(out, g[1]);
		be32(out, g[2]);
	}
	return out;
}

// A `post` format 2 table naming glyphs 0 and up, each name a Macintosh name or a string of its own.
[[nodiscard]] std::vector<uint8_t> post2(const std::vector<std::string>& names)
{
	std::vector<uint8_t> out;
	be32(out, 0x00020000u);
	for (int i = 0; i < 7; ++i) {
		be32(out, 0u);
	}
	be16(out, static_cast<uint32_t>(names.size()));
	std::vector<std::string> own;
	for (const std::string& name : names) {
		if (name == ".notdef") {
			be16(out, 0u);
		} else {
			be16(out, 258u + static_cast<uint32_t>(own.size()));
			own.push_back(name);
		}
	}
	for (const std::string& name : own) {
		out.push_back(static_cast<uint8_t>(name.size()));
		out.insert(out.end(), name.begin(), name.end());
	}
	return out;
}

[[nodiscard]] std::vector<uint8_t> sfnt(const std::vector<uint8_t>& cmap, const std::vector<uint8_t>& post = {})
{
	std::vector<std::pair<uint32_t, const std::vector<uint8_t>*>> tables;
	if (!cmap.empty()) {
		tables.emplace_back(0x636D6170u, &cmap);
	}
	if (!post.empty()) {
		tables.emplace_back(0x706F7374u, &post);
	}
	std::vector<uint8_t> out;
	be32(out, 0x00010000u);
	be16(out, static_cast<uint32_t>(tables.size()));
	be16(out, 0u);
	be16(out, 0u);
	be16(out, 0u);
	uint32_t offset = 12u + 16u * static_cast<uint32_t>(tables.size());
	for (const auto& [tag, bytes] : tables) {
		be32(out, tag);
		be32(out, 0u);
		be32(out, offset);
		be32(out, static_cast<uint32_t>(bytes->size()));
		offset += static_cast<uint32_t>(bytes->size());
	}
	for (const auto& [tag, bytes] : tables) {
		out.insert(out.end(), bytes->begin(), bytes->end());
	}
	return out;
}

[[nodiscard]] SimpleEncoding encoding_of(std::optional<std::string_view> name, bool symbolic, const PdfArray* differences = nullptr)
{
	if (differences != nullptr) {
		PdfDict dict;
		if (name.has_value()) {
			dict.set("BaseEncoding", PdfValue(PdfName(*name)));
		}
		dict.set("Differences", PdfValue(*differences));
		const PdfValue value{std::move(dict)};
		return *SimpleEncoding::build(&value, symbolic, nullptr);
	}
	if (name.has_value()) {
		const PdfValue value{PdfName(*name)};
		return *SimpleEncoding::build(&value, symbolic, nullptr);
	}
	return *SimpleEncoding::build(nullptr, symbolic, nullptr);
}

TEST(PdfFont, TrueTypeCmapSelection)
{
	const auto tables = [](const std::vector<uint8_t>& program) {
		auto parsed = TrueTypeTables::parse(program);
		EXPECT_TRUE(parsed.has_value()) << parsed.error().message.view();
		return parsed.has_value() ? std::move(*parsed) : TrueTypeTables{};
	};
	// The name path through (3,1): WinAnsi's 0x80 is Euro, U+20AC.
	{
		const auto t = tables(sfnt(cmap_table({{3, 1, format4({{0x41, 5}, {0x20AC, 9}})}})));
		const SimpleEncoding win = encoding_of("WinAnsiEncoding", true);
		EXPECT_EQ(simple_truetype_glyph(t, win, true, false, 0x41), 5u);
		EXPECT_EQ(simple_truetype_glyph(t, win, true, false, 0x80), 9u);
		// Non-symbolic with no encoding: StandardEncoding names, the same path.
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of(std::nullopt, false), false, true, 0x41), 5u);
	}
	// The name path through (1,0): WinAnsi's 0xC4 is Adieresis, Mac OS Roman 0x80; WinAnsi's 0x80 is Euro, 0xDB there.
	{
		const auto t = tables(sfnt(cmap_table({{1, 0, format0({{0x80, 7}, {0xDB, 9}, {0x41, 3}})}})));
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of("WinAnsiEncoding", false), false, true, 0xC4), 7u);
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of("WinAnsiEncoding", false), false, true, 0x80), 9u);
	}
	// With a (3,1) subtable the (1,0) one is not consulted for a name: a name (3,1) lacks goes to the post names.
	{
		const auto t = tables(sfnt(cmap_table({{3, 1, format4({{0x42, 2}})}, {1, 0, format0({{0x41, 7}})}}),
			post2({".notdef", "x", "y", "A"})));
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of("WinAnsiEncoding", false), false, true, 0x41), 3u);
	}
	// The name path through the post names, a (3,1) subtable lacking the name.
	{
		const auto t = tables(sfnt(cmap_table({{3, 1, format4({{0x42, 2}})}}), post2({".notdef", "x", "y", "A"})));
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of("WinAnsiEncoding", false), false, true, 0x41), 3u);
		EXPECT_EQ(t.glyph_for_name("y"), std::optional<uint16_t>(uint16_t{2u}));
		EXPECT_EQ(t.glyph_for_name(".notdef"), std::optional<uint16_t>(uint16_t{0u}));
	}
	// /Differences selects the name path in a symbolic font too.
	{
		PdfArray differences;
		differences.push_back(PdfValue(int64_t{0x41}));
		differences.push_back(PdfValue(PdfName("B")));
		const auto t = tables(sfnt(cmap_table({{3, 1, format4({{0x42, 4}})}, {3, 0, format4({{0xF041, 8}})}})));
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of(std::nullopt, true, &differences), true, false, 0x41), 4u);
		// Without /Differences the symbolic font takes the code path.
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of(std::nullopt, true), true, false, 0x41), 8u);
	}
	// The code path through (3,0) at each base, the first that maps it winning.
	{
		const SimpleEncoding none = encoding_of(std::nullopt, true);
		EXPECT_EQ(simple_truetype_glyph(tables(sfnt(cmap_table({{3, 0, format4({{0x41, 11}})}}))), none, true, false, 0x41), 11u);
		EXPECT_EQ(simple_truetype_glyph(tables(sfnt(cmap_table({{3, 0, format4({{0xF041, 12}})}}))), none, true, false, 0x41), 12u);
		EXPECT_EQ(simple_truetype_glyph(tables(sfnt(cmap_table({{3, 0, format4({{0xF141, 13}})}}))), none, true, false, 0x41), 13u);
		EXPECT_EQ(simple_truetype_glyph(tables(sfnt(cmap_table({{3, 0, format4({{0xF241, 14}})}}))), none, true, false, 0x41), 14u);
		// (1,0) at the code, through format 6.
		EXPECT_EQ(simple_truetype_glyph(tables(sfnt(cmap_table({{1, 0, format6(0x40, {20, 21, 22})}}))), none, true, false, 0x41), 21u);
		// No subtable at all: the code as a glyph index.
		EXPECT_EQ(simple_truetype_glyph(tables(sfnt({})), none, true, false, 0x41), 0x41u);
		// A subtable that does not map the code: missing, not the glyph at that index (whose Unicode is unrelated).
		const auto miss = tables(sfnt(cmap_table({{3, 0, format4({{0xF042, 7}})}, {3, 1, format4({{0x5A, 0x41}})}})));
		EXPECT_EQ(simple_truetype_glyph(miss, none, true, false, 0x41), 0u);
		const SimpleFontText text{nullptr, &none, false, &miss, true, false};
		EXPECT_EQ(*simple_font_unicode(text, 0x41), U""); // not Z
	}
	// A name the name path cannot find falls to the code path.
	{
		const auto t = tables(sfnt(cmap_table({{3, 1, format4({{0x42, 4}})}, {3, 0, format4({{0xF041, 8}})}})));
		EXPECT_EQ(simple_truetype_glyph(t, encoding_of("WinAnsiEncoding", false), false, true, 0x41), 8u);
	}
	// The Unicode subtable read backwards: the lowest scalar of a glyph, and (3,10) over (3,1).
	{
		const auto t = tables(sfnt(cmap_table({{3, 1, format4({{0x41, 5}, {0x42, 6}, {0x391, 5}})}})));
		EXPECT_EQ(t.unicode_for_glyph(5u), std::optional<char32_t>(U'A'));
		EXPECT_EQ(t.unicode_for_glyph(6u), std::optional<char32_t>(U'B'));
		EXPECT_EQ(t.unicode_for_glyph(7u), std::nullopt);
		const auto full = tables(sfnt(cmap_table({{3, 1, format4({{0x41, 5}})}, {3, 10, format12({{{0x1F600, 0x1F601, 5}}})}})));
		EXPECT_EQ(full.unicode_for_glyph(5u), std::optional<char32_t>(U'\U0001F600'));
		EXPECT_EQ(full.unicode_for_glyph(6u), std::optional<char32_t>(U'\U0001F601'));
	}
}

TEST(PdfFont, TrueTypeCmapMalformed)
{
	// The font program cannot be read at all: refused.
	const auto refused = [](const std::vector<uint8_t>& program, std::string_view what) {
		auto parsed = TrueTypeTables::parse(program);
		ASSERT_FALSE(parsed.has_value()) << what;
		EXPECT_EQ(parsed.error().code, ErrorCode::DamagedFormat) << what;
	};
	// One component is damaged: it is left out with its typed error, and the rest stays usable (completion review G4).
	const auto damaged = [](const std::vector<uint8_t>& program, std::string_view what) {
		auto parsed = TrueTypeTables::parse(program);
		EXPECT_TRUE(parsed.has_value()) << what;
		if (!parsed.has_value()) {
			return TrueTypeTables{};
		}
		EXPECT_EQ(parsed->damage().size(), 1u) << what;
		if (!parsed->damage().empty()) {
			EXPECT_EQ(parsed->damage()[0].code, ErrorCode::DamagedFormat) << what;
		}
		return std::move(*parsed);
	};
	refused({0x00, 0x01, 0x00, 0x00, 0x00, 0x05}, "a truncated table directory");
	// A table lying outside the program is damage to that table only (review round 3): the other one stays usable.
	{
		std::vector<uint8_t> program = sfnt(cmap_table({{3, 1, format4({{0x41, 5}})}}), post2({".notdef", "A"}));
		program[12u + 8u + 3u] = 0xF0; // the cmap record's offset, now past the program
		const TrueTypeTables no_cmap = damaged(program, "a cmap outside the program");
		EXPECT_FALSE(no_cmap.has(3u, 1u));
		EXPECT_EQ(no_cmap.glyph_for_name("A"), std::optional<uint16_t>(uint16_t{1u}));
		std::vector<uint8_t> post_out = sfnt(cmap_table({{3, 1, format4({{0x41, 5}})}}), post2({".notdef", "A"}));
		post_out[12u + 16u + 8u + 3u] = 0xF0; // the post record's offset, now past the program
		const TrueTypeTables no_post = damaged(post_out, "a post outside the program");
		EXPECT_EQ(no_post.lookup(3u, 1u, 0x41), std::optional<uint16_t>(uint16_t{5u}));
		EXPECT_EQ(no_post.glyph_for_name("A"), std::nullopt);
	}
	refused(sfnt({0x00, 0x00}), "a truncated cmap header");
	// Each damaged subtable beside a good (3,0) one: the (3,1) is gone, the (3,0) still selects.
	const std::vector<uint8_t> good = format4({{0xF041, 8}});
	const auto beside = [&](std::vector<uint8_t> bad, uint16_t encoding = 1u) {
		return sfnt(cmap_table({{3, encoding, std::move(bad)}, {3, 0, good}}));
	};
	const SimpleEncoding symbolic = *SimpleEncoding::build(nullptr, true, nullptr);
	const auto still_selects = [&](const TrueTypeTables& tables, std::string_view what) {
		EXPECT_FALSE(tables.has(3u, 1u) || tables.has(3u, 10u)) << what;
		EXPECT_EQ(simple_truetype_glyph(tables, symbolic, true, false, 0x41), 8u) << what;
	};
	{
		std::vector<uint8_t> cmap = cmap_table({{3, 1, format4({{0x41, 5}})}, {3, 0, good}});
		cmap[4u + 4u + 3u] = 0xF0; // the (3,1) subtable offset, past the cmap table
		still_selects(damaged(sfnt(cmap), "a subtable offset past the table"), "offset");
	}
	{
		std::vector<uint8_t> sub = format4({{0x41, 5}});
		sub[6] = 0x7F; // segCountX2 far larger than the subtable
		still_selects(damaged(beside(sub), "format 4 segment counts"), "segments");
	}
	{
		std::vector<uint8_t> sub = format4({{0x41, 5}});
		const size_t offsets = 14u + 4u + 2u + 4u + 4u; // two segments: ends, pad, starts, deltas
		sub[offsets] = 0x40; // the first segment's idRangeOffset, outside the glyph array
		still_selects(damaged(beside(sub), "a format 4 idRangeOffset outside the glyph array"), "range offset");
	}
	{
		std::vector<uint8_t> sub = format4({{0x41, 5}});
		sub[14u + 4u + 2u] = 0x00;
		sub[14u + 4u + 2u + 1u] = 0x42; // the first segment starts after it ends
		still_selects(damaged(beside(sub), "a reversed format 4 segment"), "reversed");
	}
	{
		std::vector<uint8_t> sub = format12({{{0x41, 0x42, 5}}});
		sub[12] = 0x00;
		sub[13] = 0x10; // numGroups far larger than the table
		still_selects(damaged(beside(sub, 10u), "format 12 group counts"), "groups");
	}
	still_selects(damaged(beside(format12({{{0x50, 0x40, 5}}}), 10u), "a reversed format 12 group"), "reversed 12");
	still_selects(damaged(beside(format12({{{0x40, 0x50, 5}}, {{0x45, 0x60, 9}}}), 10u), "overlapping format 12 groups"),
		"overlapping");
	still_selects(damaged(beside(format12({{{0x40, 0x110000, 5}}}), 10u), "a format 12 group past U+10FFFF"), "past");
	// Review round 2: glyph IDs that wrap or pass 0xFFFF are damage, not small glyph IDs.
	still_selects(damaged(beside(format12({{{0x40, 0x42, 0xFFFFFFFFu}}}), 10u), "format 12 glyph IDs wrapping"), "wrap");
	still_selects(damaged(beside(format12({{{0x40, 0x42, 0xFFFEu}}}), 10u), "format 12 glyph IDs past 0xFFFF"), "past glyphs");
	// A damaged post table: its names are gone, the cmap stays (G4), and every truncation is typed (G5).
	{
		const std::vector<uint8_t> cmap = cmap_table({{3, 1, format4({{0x41, 1}})}});
		std::vector<uint8_t> counts = post2({".notdef", "A"});
		counts[32] = 0x7F; // numGlyphs far larger than the table
		std::vector<uint8_t> runs = post2({".notdef", "A"});
		runs[runs.size() - 2u] = 0x40; // the name's length, past the table
		std::vector<uint8_t> missing = post2({".notdef", "A"});
		missing.resize(missing.size() - 2u); // the string glyph 1 names is gone
		const std::vector<uint8_t> short_header(20u, 0u);
		for (const auto& [post, what] : {std::pair{counts, "post glyph indices larger than the table"},
				 std::pair{runs, "a post name past the table"}, std::pair{missing, "a post index naming no string"},
				 std::pair{short_header, "a post table shorter than its header"}}) {
			const TrueTypeTables tables = damaged(sfnt(cmap, post), what);
			EXPECT_EQ(tables.glyph_for_name("A"), std::nullopt) << what;
			EXPECT_EQ(tables.lookup(3u, 1u, 0x41), std::optional<uint16_t>(uint16_t{1u})) << what;
		}
	}
	// Review round 1: a format 4 idRangeOffset into the segment arrays or not word-aligned reads no glyph from them.
	{
		std::vector<uint8_t> sub = format4({{0x41, 5}});
		const size_t offsets = 14u + 4u + 2u + 4u + 4u;
		sub[offsets + 1u] = 2u; // the first segment's offset points at the second segment's offset word
		still_selects(damaged(beside(sub), "an idRangeOffset into the segment arrays"), "metadata");
		// With a glyph array after the offsets, an odd offset would read across two of its words.
		std::vector<uint8_t> odd = format4({{0x41, 5}});
		odd.insert(odd.end(), {0x00, 0x09, 0x00, 0x0A});
		odd[3] = static_cast<uint8_t>(odd[3] + 4u); // the subtable's length
		odd[offsets + 1u] = 5u;
		still_selects(damaged(beside(odd), "an unaligned idRangeOffset"), "unaligned");
	}
	// Review round 2: bytes past the strings a post table's names refer to are not read as names, so a large padded
	// table costs nothing beyond its one name.
	{
		std::vector<uint8_t> post = post2({".notdef", "A"});
		post.resize(post.size() + (size_t{8} << 20u), 0u);
		const std::vector<uint8_t> program = sfnt(cmap_table({{3, 1, format4({{0x41, 1}})}}), post);
		const auto started = std::chrono::steady_clock::now();
		const recto::test::ScopedHeapMeter meter;
		auto parsed = TrueTypeTables::parse(program);
		const size_t peak = meter.peak_bytes();
		const auto elapsed = std::chrono::steady_clock::now() - started;
		ASSERT_TRUE(parsed.has_value()) << parsed.error().message.view();
		EXPECT_TRUE(parsed->damage().empty());
		EXPECT_EQ(parsed->glyph_for_name("A"), std::optional<uint16_t>(uint16_t{1u}));
		EXPECT_LT(peak, size_t{1} << 20u); // not a string record per padding byte (0 under ASan, which has no meter)
		EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 3000);
	}
	// Review round 1: records repeating one damaged subtable read it once, not once each.
	{
		std::vector<std::array<uint32_t, 3>> groups;
		for (uint32_t i = 0; i < 20000u; ++i) {
			groups.push_back({i * 2u, i * 2u, 1u});
		}
		groups.push_back({0u, 1u, 1u}); // overlapping the first: damaged
		const std::vector<uint8_t> bad = format12(groups);
		std::vector<uint8_t> cmap;
		be16(cmap, 0u);
		be16(cmap, 0xFFFFu);
		const uint32_t at = 4u + 8u * 0xFFFFu;
		for (uint32_t i = 0; i < 0xFFFFu; ++i) {
			be16(cmap, 3u);
			be16(cmap, 10u);
			be32(cmap, at);
		}
		cmap.insert(cmap.end(), bad.begin(), bad.end());
		const auto started = std::chrono::steady_clock::now();
		auto parsed = TrueTypeTables::parse(sfnt(cmap));
		const auto elapsed = std::chrono::steady_clock::now() - started;
		ASSERT_TRUE(parsed.has_value()) << parsed.error().message.view();
		EXPECT_EQ(parsed->damage().size(), 1u);
		EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 3000);
	}
	// The work stays linear: format 12 groups over every Unicode scalar (each within the 16-bit glyph IDs) read back
	// quickly.
	{
		std::vector<std::array<uint32_t, 3>> groups;
		for (uint32_t start = 0x20; start <= 0x10FFFFu; start += 0xFFFFu) {
			groups.push_back({start, std::min<uint32_t>(start + 0xFFFEu, 0x10FFFFu), 1u});
		}
		const std::vector<uint8_t> program = sfnt(cmap_table({{3, 10, format12(groups)}}));
		const auto started = std::chrono::steady_clock::now();
		auto big = TrueTypeTables::parse(program);
		const auto elapsed = std::chrono::steady_clock::now() - started;
		ASSERT_TRUE(big.has_value()) << big.error().message.view();
		EXPECT_TRUE(big->damage().empty());
		EXPECT_EQ(big->unicode_for_glyph(1u), std::optional<char32_t>(U' '));
		EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 3000);
	}
}

[[nodiscard]] std::vector<uint8_t> bytes_of(std::string_view text)
{
	return std::vector<uint8_t>(text.begin(), text.end());
}

TEST(PdfFont, ToUnicodeCMap)
{
	const auto cmap_text = bytes_of(R"(/CIDInit /ProcSet findresource begin 12 dict begin begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def /CMapName /Test def % a comment
1 begincodespacerange <00> <FF> endcodespacerange
2 beginbfchar <41> <0041> <02> <00660069> endbfchar
1 beginbfchar <03> <D83DDE00> endbfchar
2 beginbfrange <10> <12> <0061> <20> <21> [<0058> <0059>] endbfrange
1 beginbfchar <04> /space endbfchar
1 beginbfchar <05> <D800> endbfchar
1 beginbfchar <06> <D83D0041> endbfchar
2 beginbfrange <30> <39> <0030> <3A> <3B> <004>endbfrange
1 beginbfchar <35> <0058> endbfchar
endcmap CMapName currentdict /CMap defineresource pop end end)");
	auto cmap = ToUnicodeCMap::parse(cmap_text);
	ASSERT_TRUE(cmap.has_value()) << cmap.error().message.view();
	const auto at = [&](uint32_t code) {
		auto found = cmap->lookup(code, 1u);
		return found.has_value() && found->has_value() ? **found : std::u32string(U"<none>");
	};
	EXPECT_EQ(at(0x41), U"A");
	EXPECT_EQ(at(0x02), U"fi"); // a multi-character target
	EXPECT_EQ(at(0x03), U"\U0001F600"); // a surrogate pair
	EXPECT_EQ(at(0x10), U"a");
	EXPECT_EQ(at(0x12), U"c"); // the range's last character incremented
	EXPECT_EQ(at(0x20), U"X");
	EXPECT_EQ(at(0x21), U"Y"); // the array form
	EXPECT_EQ(at(0x04), U" "); // a glyph name target
	EXPECT_EQ(at(0x05), U"<none>"); // an unpaired surrogate is skipped
	EXPECT_EQ(at(0x06), U"<none>"); // and a high surrogate followed by anything but a low one
	EXPECT_EQ(at(0x34), U"4");
	EXPECT_EQ(at(0x35), U"X"); // the later bfchar wins inside the earlier range
	EXPECT_EQ(at(0x36), U"6");
	EXPECT_EQ(at(0x3A), U"@"); // an odd digit count is padded: <004> is <0040>
	EXPECT_EQ(at(0x60), U"<none>");
	EXPECT_EQ(*cmap->lookup(0x41, 2u), std::nullopt); // another code length is another code
	// Two-byte and mixed codespaces.
	{
		auto two = ToUnicodeCMap::parse(bytes_of(
			"1 begincodespacerange <0000> <FFFF> endcodespacerange 1 beginbfrange <0100> <01FF> <0100> endbfrange"));
		ASSERT_TRUE(two.has_value());
		EXPECT_EQ(*two->lookup(0x0150, 2u), std::optional<std::u32string>(U"Ő"));
		const std::array<uint8_t, 2> code = {0x01, 0x50};
		EXPECT_EQ(two->code_length(code), 2u);
		auto mixed = ToUnicodeCMap::parse(bytes_of("2 begincodespacerange <00> <80> <8140> <9FFC> endcodespacerange"));
		ASSERT_TRUE(mixed.has_value());
		const std::array<uint8_t, 2> sjis = {0x81, 0x40};
		const std::array<uint8_t, 1> ascii = {0x41};
		EXPECT_EQ(mixed->code_length(sjis), 2u);
		EXPECT_EQ(mixed->code_length(ascii), 1u);
		auto edge = ToUnicodeCMap::parse(bytes_of("1 beginbfrange <FFFFFFFE> <FFFFFFFF> <0041> endbfrange"));
		ASSERT_TRUE(edge.has_value());
		EXPECT_EQ(*edge->lookup(0xFFFFFFFFu, 4u), std::optional<std::u32string>(U"B"));
	}
	// Completion review G1: a range's increment never makes a surrogate, also after a later mapping splits it.
	{
		auto ranged = ToUnicodeCMap::parse(bytes_of(
			"1 beginbfrange <00> <02> <D7FE> endbfrange 1 beginbfchar <00> <0041> endbfchar"));
		ASSERT_TRUE(ranged.has_value()) << ranged.error().message.view();
		EXPECT_EQ(*ranged->lookup(0x00, 1u), std::optional<std::u32string>(U"A"));
		EXPECT_EQ(*ranged->lookup(0x01, 1u), std::optional<std::u32string>(U"\uD7FF"));
		EXPECT_EQ(*ranged->lookup(0x02, 1u), std::nullopt); // U+D800 is not a character
	}
	// Refusals, each typed.
	const auto refuses = [](std::string_view text, ErrorCode code) {
		auto parsed = ToUnicodeCMap::parse(bytes_of(text));
		ASSERT_FALSE(parsed.has_value()) << text.substr(0, 60);
		EXPECT_EQ(parsed.error().code, code) << text.substr(0, 60);
	};
	refuses("/Identity-H usecmap", ErrorCode::UnsupportedFeature);
	refuses("1 beginbfrange <20> <10> <0041> endbfrange", ErrorCode::DamagedFormat);
	refuses("1 beginbfchar <4G> <0041> endbfchar", ErrorCode::DamagedFormat);
	refuses("1 beginbfchar <41> <0041", ErrorCode::DamagedFormat);
	refuses("1 begincodespacerange <00> <FFFF> endcodespacerange", ErrorCode::DamagedFormat);
	refuses("1 beginbfrange <00> <10> 42 endbfrange", ErrorCode::DamagedFormat);
	{
		std::string many = "101 begincodespacerange";
		for (int i = 0; i < 101; ++i) {
			many += " <00> <FF>";
		}
		refuses(many + " endcodespacerange", ErrorCode::ResourceLimit);
	}
	refuses("2 beginbfrange <0000> <FFFF> <0041> <0000> <FFFF> <0041> endbfrange", ErrorCode::ResourceLimit);
	refuses("1 beginbfchar <41> <" + std::string(1026u, '0') + "> endbfchar", ErrorCode::ResourceLimit);
	// Completion review G2 and G3: an array range gives one string per code, without wrapping past 0xFFFFFFFF, and a
	// glyph name target is held to the same budget as a string.
	refuses("1 beginbfrange <FFFFFFFF> <FFFFFFFF> [<0041> <0042>] endbfrange", ErrorCode::DamagedFormat);
	refuses("1 beginbfrange <10> <12> [<0041>] endbfrange", ErrorCode::DamagedFormat);
	{
		std::string many = "1 beginbfrange <00> <00> [";
		for (int i = 0; i < 70000; ++i) {
			many += "<0041>";
		}
		refuses(many + "] endbfrange", ErrorCode::DamagedFormat); // refused for its count, before any limit
	}
	{
		std::string name = "/uni";
		for (int i = 0; i < 257; ++i) {
			name += "0041";
		}
		refuses("1 beginbfchar <41> " + name + " endbfchar", ErrorCode::ResourceLimit);
	}
	{
		std::vector<uint8_t> huge(kMaxToUnicodeStreamBytes + 1u, ' ');
		auto parsed = ToUnicodeCMap::parse(huge);
		ASSERT_FALSE(parsed.has_value());
		EXPECT_EQ(parsed.error().code, ErrorCode::ResourceLimit);
	}
}

TEST(PdfFont, UnicodeFallbackOrder)
{
	auto cmap = ToUnicodeCMap::parse(bytes_of("1 beginbfchar <41> <005A> endbfchar"));
	ASSERT_TRUE(cmap.has_value());
	GlyphNames names{};
	names[0x41] = "A";
	names[0x42] = "g123";
	names[0x43] = "a1";
	auto encoding = SimpleEncoding::build(nullptr, true, &names);
	ASSERT_TRUE(encoding.has_value());
	auto tables = TrueTypeTables::parse(sfnt(cmap_table({{3, 0, format4({{0xF042, 7}})}, {3, 1, format4({{0x00C5, 7}})}})));
	ASSERT_TRUE(tables.has_value());
	SimpleFontText font{&*cmap, &*encoding, false, &*tables, true, false};
	EXPECT_EQ(*simple_font_unicode(font, 0x41), U"Z"); // ToUnicode first
	font.to_unicode = nullptr;
	EXPECT_EQ(*simple_font_unicode(font, 0x41), U"A"); // then the glyph name through the AGL
	EXPECT_EQ(*simple_font_unicode(font, 0x42), U"Å"); // then the cmap: glyph 7 back through (3,1)
	EXPECT_EQ(*simple_font_unicode(font, 0x44), U""); // nothing: never a guessed character
	font.truetype = nullptr;
	EXPECT_EQ(*simple_font_unicode(font, 0x42), U""); // g123 names nothing without the cmap
	font.zapf_dingbats = true;
	EXPECT_EQ(*simple_font_unicode(font, 0x43), U"✁"); // a ZapfDingbats font reads zapfdingbats.txt
}

// Completion review G6: when the mapped characters cannot be held, the lookup says so; the encoding's name is never
// used instead, which would turn the CMap's Z into the encoding's A.
TEST(PdfFont, ToUnicodeOutOfMemory)
{
	if (!recto::test::ScopedAllocationFailure::available()) {
		GTEST_SKIP() << "allocation failure injection is unavailable under AddressSanitizer";
	}
	std::string target = "<";
	for (int i = 0; i < 40; ++i) {
		target += "005A";
	}
	auto cmap = ToUnicodeCMap::parse(bytes_of("1 beginbfchar <41> " + target + "> endbfchar"));
	ASSERT_TRUE(cmap.has_value()) << cmap.error().message.view();
	auto encoding = SimpleEncoding::build(nullptr, false, nullptr);
	ASSERT_TRUE(encoding.has_value());
	const SimpleFontText font{&*cmap, &*encoding, false, nullptr, false, true};
	{
		const recto::test::ScopedAllocationFailure fail(1u, 100u);
		auto text = simple_font_unicode(font, 0x41);
		ASSERT_FALSE(text.has_value());
		EXPECT_EQ(text.error().code, ErrorCode::OutOfMemory);
	}
	auto text = simple_font_unicode(font, 0x41);
	ASSERT_TRUE(text.has_value());
	EXPECT_EQ(*text, std::u32string(40u, U'Z'));
}

TEST(PdfFont, DrawableUnicode)
{
	// Expected scalars of the test's own, not read through the AGL.
	const std::array<std::pair<std::string_view, char32_t>, 10> expected = {{{"A", U'A'}, {"Euro", U'€'},
		{"fi", U'ﬁ'}, {"commaaccent", U'̦'}, {"Lslash", U'Ł'}, {"quoteright", U'’'},
		{"space", U' '}, {"dotlessi", U'ı'}, {"partialdiff", U'∂'}, {"florin", U'ƒ'}}};
	for (const auto& [name, scalar] : expected) {
		EXPECT_EQ(drawable_unicode(name), std::optional<char32_t>(scalar)) << name;
	}
	EXPECT_EQ(drawable_unicode("apple"), std::nullopt); // Symbol's apple, private use in the AGL
	// Every Latin Core14 name against kLatinUnicode, the test's table from the pinned glyphlist.txt (U+0326 for
	// commaaccent), and every ZapfDingbats code against kZapfDingbatsUnicode: scalar by scalar, so a digit drawn as
	// another digit of the same width cannot pass.
	for (const auto& [name, scalar] : kLatinUnicode) {
		EXPECT_EQ(drawable_unicode(name), std::optional<char32_t>(scalar)) << name;
	}
	for (const auto& [code, scalar] : kZapfDingbatsUnicode) {
		EXPECT_EQ(drawable_unicode(base_encoding_name(BaseEncoding::ZapfDingbats, code), true), std::optional<char32_t>(scalar))
			<< int{code};
	}
	EXPECT_EQ(drawable_unicode("f_i"), std::nullopt);   // two characters: no single glyph
	// Every name of the twelve Latin fonts draws in its own face, and every ZapfDingbats name in its face.
	for (const Standard14Font font : kAllStandard14) {
		if (font == Standard14Font::Symbol) {
			continue; // Symbol draws by its built-in code
		}
		const Standard14Metrics& m = standard14_metrics(font);
		SCOPED_TRACE(std::string(m.base_font));
		const bool zapf = font == Standard14Font::ZapfDingbats;
		std::vector<std::string> missing;
		std::vector<std::string> wrong_width;
		for (const Standard14Glyph& g : m.glyphs) {
			const std::optional<char32_t> scalar = drawable_unicode(g.name, zapf);
			if (!scalar.has_value()) {
				missing.push_back(std::string(g.name) + " (no scalar)");
				continue;
			}
			auto glyph = standard14_face_glyph(font, *scalar);
			ASSERT_TRUE(glyph.has_value());
			if (!glyph->has_value()) {
				missing.emplace_back(g.name);
				continue;
			}
			// Completion review G7: the glyph a name's scalar draws is that name's glyph, shown by the metric-compatible
			// faces' advances matching Adobe's AFM widths (the reviewed design differences apart; ZapfDingbats is drawn
			// from Segoe UI Symbol, another design).
			if (!zapf && std::find(kKnownWidthDifferences.begin(), kKnownWidthDifferences.end(), g.name) ==
					kKnownWidthDifferences.end()) {
				const double advance = *(*glyph)->face->advance((*glyph)->glyph) * 1000.0;
				if (std::abs(advance - g.width) > 2.0) {
					wrong_width.push_back(std::string(g.name) + " " + std::to_string(advance));
				}
			}
		}
		EXPECT_TRUE(missing.empty()) << ::testing::PrintToString(missing);
		EXPECT_TRUE(wrong_width.empty()) << ::testing::PrintToString(wrong_width);
	}
	// commaaccent draws as a comma below the baseline in each Latin face.
	for (const Standard14Font font : kAllStandard14) {
		if (font == Standard14Font::Symbol || font == Standard14Font::ZapfDingbats) {
			continue;
		}
		const std::optional<char32_t> comma = drawable_unicode("commaaccent");
		ASSERT_TRUE(comma.has_value());
		auto glyph = standard14_face_glyph(font, *comma);
		ASSERT_TRUE(glyph.has_value() && glyph->has_value());
		auto box = (*glyph)->face->ink_box((*glyph)->glyph);
		ASSERT_TRUE(box.has_value() && box->has_value()) << standard14_metrics(font).base_font;
		EXPECT_LT((*box)->y_max, 0.0) << standard14_metrics(font).base_font;
		// And it is a comma: the size of the face's own comma (U+002C), give or take a half, moved below the baseline.
		auto plain_comma = standard14_face_glyph(font, U',');
		ASSERT_TRUE(plain_comma.has_value() && plain_comma->has_value());
		auto comma_box = (*plain_comma)->face->ink_box((*plain_comma)->glyph);
		ASSERT_TRUE(comma_box.has_value() && comma_box->has_value());
		const double height = (*box)->y_max - (*box)->y_min;
		const double comma_height = (*comma_box)->y_max - (*comma_box)->y_min;
		EXPECT_GT(height, comma_height * 0.5) << standard14_metrics(font).base_font;
		EXPECT_LT(height, comma_height * 1.5) << standard14_metrics(font).base_font;
	}
}

} // namespace
} // namespace recto::pdf
