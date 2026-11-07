// recto::pdf::Rasterizer (D01 T02 section 6): every expected pixel below follows by hand from the list it replays.
#include "../../src/pdf/display_list.h"
#include "../../src/pdf/document.h"
#include "../../src/pdf/error.h"
#include "../../src/pdf/font.h"
#include "../../src/pdf/interpreter.h"
#include "../../src/pdf/rasterizer.h"

#include "test_support.h"

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace recto::pdf {
namespace {

using Microsoft::WRL::ComPtr;

struct Bgra {
	int b;
	int g;
	int r;
	int a;
};

struct Image {
	uint32_t width{0};
	uint32_t height{0};
	std::vector<uint8_t> bytes;

	[[nodiscard]] Bgra at(uint32_t x, uint32_t y) const
	{
		const uint8_t* p = bytes.data() + (size_t{y} * width + x) * 4u;
		return Bgra{p[0], p[1], p[2], p[3]};
	}
};

Rasterizer& worker()
{
	thread_local std::unique_ptr<Rasterizer> made = [] {
		auto r = Rasterizer::create();
		EXPECT_TRUE(r.has_value());
		return r.has_value() ? std::move(*r) : nullptr;
	}();
	return *made;
}

Image render(const std::shared_ptr<const DisplayList>& list, const TileSpec& spec, Rasterizer& r = worker())
{
	Image out{spec.tile_width_px, spec.tile_height_px, {}};
	out.bytes.resize(size_t{spec.tile_width_px} * spec.tile_height_px * 4u);
	const auto done = r.render_tile(list, spec, out.bytes, size_t{spec.tile_width_px} * 4u);
	EXPECT_TRUE(done.has_value()) << (done.has_value() ? "" : std::string(done.error().message.view()));
	return out;
}

// The whole page in one bitmap.
Image render_page(const std::shared_ptr<const DisplayList>& list, double scale = 1.0, int32_t rotation = 0,
	ColorRgba background = ColorRgba::white(), Rasterizer& r = worker())
{
	const PagePixelSize size = page_pixel_size(list->page_box_pt(), scale, rotation);
	TileSpec spec;
	spec.dpi_scale = scale;
	spec.rotation_deg = rotation;
	spec.tile_width_px = size.width;
	spec.tile_height_px = size.height;
	spec.background_rgba = background;
	return render(list, spec, r);
}

std::vector<uint8_t> corpus_bytes(std::string_view relative)
{
	std::ifstream in(recto::test::corpus_path(relative), std::ios::binary);
	EXPECT_TRUE(in.good()) << relative;
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::shared_ptr<const DisplayList>> corpus_pages(std::string_view relative, std::string_view password = {})
{
	const std::vector<uint8_t> bytes = corpus_bytes(relative);
	auto doc = password.empty() ? Document::open_memory(bytes) : Document::open_memory(bytes, password);
	EXPECT_TRUE(doc.has_value()) << relative;
	std::vector<std::shared_ptr<const DisplayList>> pages;
	if (!doc.has_value()) {
		return pages;
	}
	for (size_t page = 0; page < (*doc)->page_count(); ++page) {
		auto list = interpret_page(**doc, page);
		EXPECT_TRUE(list.has_value()) << relative << " page " << page;
		if (list.has_value()) {
			pages.push_back(std::move(*list));
		}
	}
	return pages;
}

// The whole page again, from `edge`-pixel tiles on one worker.
Image stitched(const std::shared_ptr<const DisplayList>& list, double scale, int32_t rotation, uint32_t edge = kEngineTileEdgePx)
{
	const PagePixelSize size = page_pixel_size(list->page_box_pt(), scale, rotation);
	Image out{size.width, size.height, {}};
	out.bytes.resize(size_t{size.width} * size.height * 4u);
	std::vector<uint8_t> tile(size_t{edge} * edge * 4u);
	for (uint32_t y = 0; y < size.height; y += edge) {
		for (uint32_t x = 0; x < size.width; x += edge) {
			TileSpec spec;
			spec.dpi_scale = scale;
			spec.rotation_deg = rotation;
			spec.tile_x_px = x;
			spec.tile_y_px = y;
			spec.tile_width_px = edge;
			spec.tile_height_px = edge;
			const auto done = worker().render_tile(list, spec, tile, size_t{edge} * 4u);
			EXPECT_TRUE(done.has_value());
			const uint32_t w = std::min(edge, size.width - x);
			const uint32_t h = std::min(edge, size.height - y);
			for (uint32_t row = 0; row < h; ++row) {
				std::copy_n(tile.data() + size_t{row} * edge * 4u, size_t{w} * 4u,
					out.bytes.data() + (size_t{y + row} * size.width + x) * 4u);
			}
		}
	}
	return out;
}

[[nodiscard]] bool blank(const Image& image)
{
	for (uint32_t y = 0; y < image.height; ++y) {
		for (uint32_t x = 0; x < image.width; ++x) {
			const Bgra p = image.at(x, y);
			if (p.r != 255 || p.g != 255 || p.b != 255) {
				return false;
			}
		}
	}
	return true;
}

TEST(PdfRasterizer, RotationTransforms)
{
	// A box off the origin, so a matrix that forgets it fails: 100 wide, 200 tall, from (10, 20).
	const PdfRect box{10.0, 20.0, 110.0, 220.0};
	const PdfPoint bottom_left{10.0, 20.0};
	const PdfPoint bottom_right{110.0, 20.0};
	const PdfPoint top_left{10.0, 220.0};
	const PdfPoint top_right{110.0, 220.0};
	struct Expected {
		int32_t rotation;
		PdfPoint top_left, top_right, bottom_left, bottom_right;
	};
	// At scale 2: 200 by 400 upright, 400 by 200 on its side. A clockwise turn takes the page's top left to the right.
	const std::array<Expected, 4> table{{
		{0, {0.0, 0.0}, {200.0, 0.0}, {0.0, 400.0}, {200.0, 400.0}},
		{90, {400.0, 0.0}, {400.0, 200.0}, {0.0, 0.0}, {0.0, 200.0}},
		{180, {200.0, 400.0}, {0.0, 400.0}, {200.0, 0.0}, {0.0, 0.0}},
		{270, {0.0, 200.0}, {0.0, 0.0}, {400.0, 200.0}, {400.0, 0.0}},
	}};
	for (const Expected& e : table) {
		SCOPED_TRACE(e.rotation);
		TileSpec spec;
		spec.dpi_scale = 2.0;
		spec.rotation_deg = e.rotation;
		const PdfMatrix m = page_to_tile_matrix(box, spec);
		EXPECT_EQ(transform_point(m, top_left), e.top_left);
		EXPECT_EQ(transform_point(m, top_right), e.top_right);
		EXPECT_EQ(transform_point(m, bottom_left), e.bottom_left);
		EXPECT_EQ(transform_point(m, bottom_right), e.bottom_right);
		// The tile's origin moves every point by exactly itself.
		spec.tile_x_px = 512u;
		spec.tile_y_px = 1024u;
		const PdfMatrix moved = page_to_tile_matrix(box, spec);
		EXPECT_EQ(transform_point(moved, top_left), (PdfPoint{e.top_left.x - 512.0, e.top_left.y - 1024.0}));
		const PagePixelSize size = page_pixel_size(box, 2.0, e.rotation);
		EXPECT_EQ(size, e.rotation % 180 == 0 ? (PagePixelSize{200u, 400u}) : (PagePixelSize{400u, 200u}));
	}
}

TEST(PdfRasterizer, TileSpecRefusals)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 10.0, 10.0});
	auto list = builder.finish();
	ASSERT_TRUE(list.has_value());
	const auto refused = [&](TileSpec spec, size_t buffer_bytes, size_t stride) {
		std::vector<uint8_t> buffer(buffer_bytes, 0x5Au);
		const auto done = worker().render_tile(*list, spec, buffer, stride);
		EXPECT_FALSE(done.has_value());
		if (!done.has_value()) {
			EXPECT_EQ(done.error().code, ErrorCode::InvalidArgument);
		}
		EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == 0x5Au; })) << "the buffer was written";
	};
	TileSpec ok;
	ok.tile_width_px = 4u;
	ok.tile_height_px = 4u;
	TileSpec spec = ok;
	spec.rotation_deg = 45;
	refused(spec, 64u, 16u);
	spec = ok;
	spec.dpi_scale = 0.0;
	refused(spec, 64u, 16u);
	spec.dpi_scale = std::numeric_limits<double>::infinity();
	refused(spec, 64u, 16u);
	spec = ok;
	spec.tile_width_px = 0u;
	refused(spec, 64u, 16u);
	spec = ok;
	spec.tile_height_px = kMaxTileEdgePx + 1u;
	refused(spec, 64u, 16u);
	refused(ok, 64u, 15u); // a stride under a row
	refused(ok, 63u, 16u); // a buffer a byte short
	spec = ok;
	spec.background_rgba = ColorRgba{2.0f, 0.0f, 0.0f, 1.0f};
	refused(spec, 64u, 16u);
	const auto no_list = worker().render_tile(nullptr, ok, std::span<uint8_t>(), 16u);
	ASSERT_FALSE(no_list.has_value());
	EXPECT_EQ(no_list.error().code, ErrorCode::InvalidArgument);
	// The last row needs no padding, so exactly that many bytes is enough.
	std::vector<uint8_t> exact(3u * 20u + 16u);
	EXPECT_TRUE(worker().render_tile(*list, ok, exact, 20u).has_value());
}

TEST(PdfRasterizer, StrideHonoured)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 10.0, 10.0});
	PathBuilder square;
	square.rect(2.0, 2.0, 6.0, 6.0);
	auto path = builder.add_path(square);
	ASSERT_TRUE(path.has_value());
	ASSERT_TRUE(builder.draw_path(*path, ColorRgba::black(), ColorRgba{1.0f, 0.0f, 0.0f}, false, true).has_value());
	auto list = builder.finish();
	ASSERT_TRUE(list.has_value());
	TileSpec spec;
	spec.tile_width_px = 10u;
	spec.tile_height_px = 10u;
	const Image plain = render(*list, spec);
	constexpr size_t kStride = 40u + 12u;
	std::vector<uint8_t> padded(kStride * 10u, 0x77u);
	ASSERT_TRUE(worker().render_tile(*list, spec, padded, kStride).has_value());
	for (size_t row = 0; row < 10u; ++row) {
		EXPECT_TRUE(std::equal(plain.bytes.begin() + static_cast<ptrdiff_t>(row * 40u),
			plain.bytes.begin() + static_cast<ptrdiff_t>((row + 1u) * 40u), padded.begin() + static_cast<ptrdiff_t>(row * kStride)))
			<< "row " << row;
		for (size_t pad = 40u; pad < kStride; ++pad) {
			EXPECT_EQ(padded[row * kStride + pad], 0x77u) << "row " << row << " padding byte " << pad;
		}
	}
	// A red square, inset two pixels: y up in the list, y down in the bitmap, equal here by symmetry.
	EXPECT_EQ(plain.at(5u, 5u).r, 255);
	EXPECT_EQ(plain.at(5u, 5u).g, 0);
	EXPECT_EQ(plain.at(1u, 5u).g, 255);
}

TEST(PdfRasterizer, CorpusPagesNotBlank)
{
	struct File {
		std::string_view name;
		std::string_view password;
	};
	const std::array<File, 8> files{{
		{"render/simple-1p.pdf", {}},
		{"render/compressed-xref-2p.pdf", {}},
		{"render/incremental-2rev.pdf", {}},
		{"render/rotated-pages.pdf", {}},
		{"render/images-and-vectors.pdf", {}},
		{"render/encrypted-rc4-128.pdf", "recto-test"},
		{"render/encrypted-aes-128.pdf", "recto-test"},
		{"render/encrypted-aes-256.pdf", "recto-test"},
	}};
	for (const File& file : files) {
		SCOPED_TRACE(file.name);
		const auto pages = corpus_pages(file.name, file.password);
		ASSERT_FALSE(pages.empty());
		for (size_t i = 0; i < pages.size(); ++i) {
			SCOPED_TRACE(i);
			EXPECT_FALSE(blank(render_page(pages[i]))) << "the page rendered blank";
		}
	}
}

TEST(PdfRasterizer, TileStitchIdentity)
{
	for (const std::string_view file : {std::string_view("render/simple-1p.pdf"), std::string_view("render/images-and-vectors.pdf")}) {
		const auto pages = corpus_pages(file);
		ASSERT_FALSE(pages.empty());
		for (const int32_t rotation : {0, 90, 180, 270}) {
			for (const double scale : {1.0, 1.5, 2.0}) {
				SCOPED_TRACE(std::string(file) + " rotation " + std::to_string(rotation) + " scale " + std::to_string(scale));
				const Image whole = render_page(pages[0], scale, rotation);
				const Image tiled = stitched(pages[0], scale, rotation);
				ASSERT_EQ(whole.width, tiled.width);
				ASSERT_EQ(whole.height, tiled.height);
				recto::test::expect_rgba_equal(tiled.bytes, whole.bytes, whole.width, whole.height, 0u, 0u, 0u);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Lists built by hand, on a 100 by 100 page: a PDF y of `v` is the row `100 - v` going down.
// ---------------------------------------------------------------------------------------------------------------------

constexpr PdfRect kPage100{0.0, 0.0, 100.0, 100.0};
const ColorRgba kBlack = ColorRgba::black();
const ColorRgba kRed{1.0f, 0.0f, 0.0f};
const ColorRgba kBlue{0.0f, 0.0f, 1.0f};

[[nodiscard]] bool painted(const Bgra& p)
{
	return p.r < 128 || p.g < 128 || p.b < 128;
}

[[nodiscard]] bool white(const Bgra& p)
{
	return p.r == 255 && p.g == 255 && p.b == 255;
}

std::shared_ptr<const DisplayList> finished(DisplayListBuilder& builder)
{
	auto list = builder.finish();
	EXPECT_TRUE(list.has_value());
	return list.has_value() ? *list : nullptr;
}

void fill_rect(DisplayListBuilder& builder, double x, double y, double w, double h, ColorRgba color)
{
	PathBuilder path;
	path.rect(x, y, w, h);
	const auto id = builder.add_path(path);
	ASSERT_TRUE(id.has_value());
	ASSERT_TRUE(builder.draw_path(*id, kBlack, color, false, true).has_value());
}

// A polyline stroked in black.
void stroke_line(DisplayListBuilder& builder, std::initializer_list<PdfPoint> points)
{
	PathBuilder path;
	bool first = true;
	for (const PdfPoint p : points) {
		if (first) {
			path.move_to(p);
			first = false;
		} else {
			path.line_to(p);
		}
	}
	const auto id = builder.add_path(path);
	ASSERT_TRUE(id.has_value());
	ASSERT_TRUE(builder.draw_path(*id, kBlack, kBlack, true, false).has_value());
}

std::shared_ptr<const DecodedBitmap> bitmap_of(uint32_t width, uint32_t height, std::vector<uint8_t> bgra, bool mask = false)
{
	auto made = DecodedBitmap::create(width, height, width * 4u, std::move(bgra), mask);
	EXPECT_TRUE(made.has_value());
	return made.has_value() ? *made : nullptr;
}

std::shared_ptr<const LoadedFont> truetype(std::string_view name)
{
	auto font = load_embedded_font(std::make_shared<const std::vector<uint8_t>>(corpus_bytes("fonts/" + std::string(name))), FontProgramFormat::TrueType);
	EXPECT_TRUE(font.has_value()) << name;
	return font.has_value() ? *font : nullptr;
}

std::shared_ptr<const LoadedFont> charstring(std::string_view name, CharstringFormat format)
{
	auto font = load_charstring_font(std::make_shared<const std::vector<uint8_t>>(corpus_bytes("fonts/" + std::string(name))), format);
	EXPECT_TRUE(font.has_value()) << name;
	return font.has_value() ? *font : nullptr;
}

// A run of `glyphs` from `font` placed left to right on a baseline, each its own advance in ems.
void draw_glyphs(DisplayListBuilder& builder, const std::shared_ptr<const LoadedFont>& font, std::span<const uint16_t> glyphs,
	double x, double y, double size, TextRenderMode mode = TextRenderMode::Fill, ColorRgba fill = kBlack, ColorRgba stroke = kBlack,
	double width_scale = 1.0)
{
	std::vector<PositionedGlyph> run;
	double at = 0.0;
	for (const uint16_t glyph : glyphs) {
		const auto advance = font->advance(glyph);
		ASSERT_TRUE(advance.has_value());
		const auto ink = font->ink_box(glyph);
		ASSERT_TRUE(ink.has_value());
		PositionedGlyph placed;
		placed.glyph_index = glyph;
		placed.origin_pt = PdfPoint{at, 0.0};
		placed.advance_pt = *advance * size * width_scale;
		placed.width_scale = width_scale;
		if (ink->has_value()) {
			placed.ink_bounds_pt = PdfRect{(*ink)->x_min * size * width_scale, (*ink)->y_min * size, (*ink)->x_max * size * width_scale, (*ink)->y_max * size};
		} else {
			placed.is_blank = true;
		}
		run.push_back(placed);
		at += placed.advance_pt;
	}
	ASSERT_TRUE(builder
			.draw_glyph_run(run, size, mode, fill, stroke, PdfMatrix{1.0, 0.0, 0.0, 1.0, x, y}, std::nullopt,
				std::shared_ptr<const void>(font))
			.has_value());
}

struct InkBox {
	uint32_t x_min{UINT32_MAX};
	uint32_t y_min{UINT32_MAX};
	uint32_t x_max{0};
	uint32_t y_max{0};
	size_t count{0};
};

[[nodiscard]] InkBox ink_of(const Image& image)
{
	InkBox box;
	for (uint32_t y = 0; y < image.height; ++y) {
		for (uint32_t x = 0; x < image.width; ++x) {
			if (painted(image.at(x, y))) {
				box.x_min = std::min(box.x_min, x);
				box.y_min = std::min(box.y_min, y);
				box.x_max = std::max(box.x_max, x);
				box.y_max = std::max(box.y_max, y);
				++box.count;
			}
		}
	}
	return box;
}

[[nodiscard]] bool recorded(ErrorCode code, RefusalReason reason = RefusalReason::None)
{
	const auto tail = diagnostics_tail(64u);
	return std::any_of(tail.begin(), tail.end(), [&](const DiagnosticEntry& e) {
		return e.code == code && (reason == RefusalReason::None || e.refusal_reason == reason);
	});
}

TEST(PdfRasterizer, VectorFixtures)
{
	// A pentagram, whose centre winds twice: filled under nonzero, empty under even-odd.
	for (const FillRule rule : {FillRule::NonZero, FillRule::EvenOdd}) {
		DisplayListBuilder builder(kPage100);
		PathBuilder star;
		for (const int k : {0, 2, 4, 1, 3}) {
			const double angle = std::numbers::pi / 2.0 + k * 2.0 * std::numbers::pi / 5.0;
			const PdfPoint p{50.0 + 40.0 * std::cos(angle), 50.0 + 40.0 * std::sin(angle)};
			k == 0 ? star.move_to(p) : star.line_to(p);
		}
		star.close_subpath();
		const auto id = builder.add_path(star);
		ASSERT_TRUE(id.has_value());
		ASSERT_TRUE(builder.draw_path(*id, kBlack, kBlack, false, true, rule).has_value());
		const Image image = render_page(finished(builder));
		EXPECT_EQ(painted(image.at(50u, 50u)), rule == FillRule::NonZero);
		EXPECT_TRUE(painted(image.at(50u, 15u))) << "a point of the star, one winding under either rule";
	}
	// Caps: a line from x 20 to x 80 at y 50, 10 wide. Past its end a butt shows nothing, a square its half width, and a
	// round one the half width on the axis and not at the square's corner.
	const auto capped = [](LineCap cap) {
		DisplayListBuilder builder(kPage100);
		EXPECT_TRUE(builder.set_stroke_style(10.0f, cap).has_value());
		stroke_line(builder, {PdfPoint{20.0, 50.0}, PdfPoint{80.0, 50.0}});
		return render_page(finished(builder));
	};
	EXPECT_TRUE(white(capped(LineCap::Butt).at(16u, 49u)));
	EXPECT_TRUE(painted(capped(LineCap::ProjectingSquare).at(16u, 49u)));
	EXPECT_TRUE(painted(capped(LineCap::ProjectingSquare).at(16u, 45u)));
	EXPECT_TRUE(painted(capped(LineCap::Round).at(16u, 49u)));
	EXPECT_TRUE(white(capped(LineCap::Round).at(15u, 45u)));
	// Joins: a right angle at (80, 20), 20 wide, whose outer corner is (90, 10). A miter fills to the corner (row 88,
	// 12 from the join), a round join reaches 10 from it, a bevel stops at the line x - y = 70.
	const auto joined = [](LineJoin join, float limit) {
		DisplayListBuilder builder(kPage100);
		EXPECT_TRUE(builder.set_stroke_style(20.0f, LineCap::Butt, join, limit).has_value());
		stroke_line(builder, {PdfPoint{20.0, 20.0}, PdfPoint{80.0, 20.0}, PdfPoint{80.0, 80.0}});
		return render_page(finished(builder));
	};
	const Image miter = joined(LineJoin::Miter, 10.0f);
	const Image round = joined(LineJoin::Round, 10.0f);
	const Image bevel = joined(LineJoin::Bevel, 10.0f);
	EXPECT_TRUE(painted(miter.at(88u, 88u)));
	EXPECT_TRUE(white(round.at(88u, 88u)));
	EXPECT_TRUE(white(bevel.at(88u, 88u)));
	EXPECT_TRUE(painted(round.at(86u, 86u)));
	EXPECT_TRUE(white(bevel.at(86u, 86u)));
	// The same miter past its limit: a right angle's ratio is sqrt 2, so a limit of 1.2 bevels it.
	EXPECT_TRUE(white(joined(LineJoin::Miter, 1.2f).at(88u, 88u)));
	EXPECT_TRUE(white(joined(LineJoin::Miter, 1.2f).at(86u, 86u)));
	// An arbitrary clip: the triangle (10, 10), (90, 10), (10, 90) over a full fill.
	{
		DisplayListBuilder builder(kPage100);
		PathBuilder triangle;
		triangle.move_to(10.0, 10.0);
		triangle.line_to(90.0, 10.0);
		triangle.line_to(10.0, 90.0);
		triangle.close_subpath();
		const auto id = builder.add_path(triangle);
		ASSERT_TRUE(id.has_value());
		ASSERT_TRUE(builder.push_clip_path(*id).has_value());
		fill_rect(builder, 0.0, 0.0, 100.0, 100.0, kBlack);
		const Image image = render_page(finished(builder));
		EXPECT_TRUE(painted(image.at(20u, 80u))) << "inside the triangle";
		EXPECT_TRUE(white(image.at(80u, 20u))) << "the corner it excludes";
	}
	// A rectangular clip under a 45 degree turn is a diamond, whose bounding box's corners stay unpainted.
	{
		DisplayListBuilder builder(kPage100);
		const double c = std::cos(std::numbers::pi / 4.0);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{c, c, -c, c, 50.0, 50.0}).has_value());
		PathBuilder rect;
		rect.rect(-20.0, -20.0, 40.0, 40.0);
		const auto id = builder.add_path(rect);
		ASSERT_TRUE(id.has_value());
		ASSERT_TRUE(builder.push_clip_path(*id).has_value());
		fill_rect(builder, -100.0, -100.0, 200.0, 200.0, kBlack);
		const Image image = render_page(finished(builder));
		EXPECT_TRUE(painted(image.at(50u, 50u)));
		EXPECT_TRUE(painted(image.at(74u, 50u))) << "inside the diamond, whose half diagonal is 28.3";
		EXPECT_TRUE(white(image.at(75u, 25u))) << "a corner of the box around the diamond";
	}
	// The corpus's vector block, inside its clip and not outside: 72 560 468 160 re W n.
	{
		const auto pages = corpus_pages("render/images-and-vectors.pdf");
		ASSERT_FALSE(pages.empty());
		const Image image = render_page(pages[0]);
		EXPECT_TRUE(painted(image.at(100u, 792u - 640u))) << "the blue block";
		// The red block's left edge is 220 and its right 500, so its right part stands alone: 75% over white.
		const Bgra red = image.at(450u, 792u - 640u);
		EXPECT_GT(red.r, red.b);
		EXPECT_TRUE(white(image.at(100u, 792u - 725u))) << "just above the clip, where nothing else is drawn";
		EXPECT_TRUE(white(image.at(100u, 792u - 555u))) << "just below it";
	}
}

TEST(PdfRasterizer, StrokeAndAlpha)
{
	// Under a 1 by 4 scale a width of 2 is 8 pixels across a horizontal line and 2 across a vertical one: an elliptical pen.
	{
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 4.0, 0.0, 0.0}).has_value());
		ASSERT_TRUE(builder.set_stroke_style(2.0f).has_value());
		stroke_line(builder, {PdfPoint{10.0, 10.0}, PdfPoint{40.0, 10.0}});
		stroke_line(builder, {PdfPoint{70.0, 5.0}, PdfPoint{70.0, 20.0}});
		const Image image = render_page(finished(builder));
		EXPECT_TRUE(painted(image.at(25u, 56u)));
		EXPECT_TRUE(painted(image.at(25u, 63u)));
		EXPECT_TRUE(white(image.at(25u, 55u)));
		EXPECT_TRUE(white(image.at(25u, 64u)));
		EXPECT_TRUE(painted(image.at(69u, 40u)));
		EXPECT_TRUE(painted(image.at(70u, 40u)));
		EXPECT_TRUE(white(image.at(68u, 40u)));
		EXPECT_TRUE(white(image.at(71u, 40u)));
	}
	// A width of 0 is one pixel at any scale: the coverage across a column sums to one row.
	for (const double scale : {1.0, 1.5, 2.0, 3.0}) {
		SCOPED_TRACE(scale);
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.set_stroke_style(0.0f).has_value());
		stroke_line(builder, {PdfPoint{10.0, 50.25}, PdfPoint{90.0, 50.25}});
		const Image image = render_page(finished(builder), scale);
		double coverage = 0.0;
		const uint32_t column = static_cast<uint32_t>(50.0 * scale);
		for (uint32_t y = 0; y < image.height; ++y) {
			coverage += (255.0 - image.at(column, y).g) / 255.0;
		}
		EXPECT_NEAR(coverage, 1.0, 0.1);
	}
	// Under any CTM too: the horizontal line a 1 by 4 scale would thicken, the vertical one a 4 by 1 scale would, a shear.
	for (const PdfMatrix ctm : {PdfMatrix{1.0, 0.0, 0.0, 4.0, 0.0, 0.0}, PdfMatrix{4.0, 0.0, 0.0, 1.0, 0.0, 0.0}, PdfMatrix{1.0, 0.0, 0.8, 1.0, 0.0, 0.0}}) {
		SCOPED_TRACE(ctm.a);
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.concat_matrix(ctm).has_value());
		ASSERT_TRUE(builder.set_stroke_style(0.0f).has_value());
		// A horizontal line at device row 50.
		const double user_y = 50.25 / ctm.d;
		stroke_line(builder, {PdfPoint{1.0, user_y}, PdfPoint{20.0, user_y}});
		const Image image = render_page(finished(builder));
		double coverage = 0.0;
		const uint32_t column = static_cast<uint32_t>(transform_point(ctm, PdfPoint{10.0, user_y}).x);
		for (uint32_t y = 0; y < image.height; ++y) {
			coverage += (255.0 - image.at(column, y).g) / 255.0;
		}
		EXPECT_NEAR(coverage, 1.0, 0.1);
	}
	{
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{4.0, 0.0, 0.0, 1.0, 0.0, 0.0}).has_value());
		ASSERT_TRUE(builder.set_stroke_style(0.0f).has_value());
		stroke_line(builder, {PdfPoint{12.5625, 10.0}, PdfPoint{12.5625, 90.0}}); // device x 50.25
		const Image image = render_page(finished(builder));
		double coverage = 0.0;
		for (uint32_t x = 0; x < image.width; ++x) {
			coverage += (255.0 - image.at(x, 50u).g) / 255.0;
		}
		EXPECT_NEAR(coverage, 1.0, 0.1);
	}
	// Dashes in user space: on and off 10 from x 10, at each scale.
	for (const double scale : {1.0, 2.0}) {
		SCOPED_TRACE(scale);
		DisplayListBuilder builder(kPage100);
		const std::array<float, 2> dash{10.0f, 10.0f};
		ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Miter, 10.0f, dash).has_value());
		stroke_line(builder, {PdfPoint{10.0, 50.0}, PdfPoint{90.0, 50.0}});
		const Image image = render_page(finished(builder), scale);
		const uint32_t row = static_cast<uint32_t>(50.0 * scale) - 1u;
		const auto at = [&](double x) { return image.at(static_cast<uint32_t>(x * scale), row); };
		EXPECT_TRUE(painted(at(15.0)));
		EXPECT_TRUE(white(at(25.0)));
		EXPECT_TRUE(painted(at(35.0)));
		EXPECT_TRUE(white(at(45.0)));
	}
	// An odd dash array repeats with its dashes and gaps exchanged, a phase starting into it (8.4.3.6).
	{
		DisplayListBuilder builder(kPage100);
		const std::array<float, 1> dash{10.0f};
		ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Miter, 10.0f, dash, 5.0f).has_value());
		stroke_line(builder, {PdfPoint{10.0, 50.0}, PdfPoint{90.0, 50.0}});
		const Image image = render_page(finished(builder));
		EXPECT_TRUE(painted(image.at(12u, 49u))) << "5 of the first dash remain";
		EXPECT_TRUE(white(image.at(17u, 49u)));
		EXPECT_TRUE(painted(image.at(27u, 49u)));
	}
	// Three entries: on 5, off 10, on 15, off 5, on 10, off 15. Read as a plain cycle, 40 to 45 would be on-and-off wrong.
	{
		DisplayListBuilder builder(kPage100);
		const std::array<float, 3> dash{5.0f, 10.0f, 15.0f};
		ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Miter, 10.0f, dash).has_value());
		stroke_line(builder, {PdfPoint{10.0, 50.0}, PdfPoint{90.0, 50.0}});
		const Image image = render_page(finished(builder));
		EXPECT_TRUE(painted(image.at(12u, 49u)));
		EXPECT_TRUE(white(image.at(20u, 49u)));
		EXPECT_TRUE(painted(image.at(32u, 49u)));
		EXPECT_TRUE(white(image.at(42u, 49u)));
		EXPECT_TRUE(painted(image.at(50u, 49u)));
		EXPECT_TRUE(white(image.at(62u, 49u)));
		EXPECT_TRUE(painted(image.at(72u, 49u)));
	}
	// Fill and stroke alpha, each its own.
	{
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.set_alpha_and_blend(0.25f, 0.5f).has_value());
		fill_rect(builder, 10.0, 10.0, 30.0, 30.0, kBlack);
		ASSERT_TRUE(builder.set_stroke_style(10.0f).has_value());
		stroke_line(builder, {PdfPoint{60.0, 50.0}, PdfPoint{90.0, 50.0}});
		const Image image = render_page(finished(builder));
		EXPECT_NEAR(image.at(25u, 75u).g, 128, 1);
		EXPECT_NEAR(image.at(75u, 50u).g, 191, 1);
	}
}

TEST(PdfRasterizer, ImageFixtures)
{
	// Four cells, each its own colour: row 0 is the top of the unit square. BGRA.
	const auto asymmetric = bitmap_of(2u, 2u, {0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 255, 255});
	ASSERT_NE(asymmetric, nullptr);
	for (const int32_t rotation : {0, 90, 180, 270}) {
		SCOPED_TRACE(rotation);
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{40.0, 0.0, 0.0, 40.0, 30.0, 30.0}).has_value());
		ASSERT_TRUE(builder.draw_image(asymmetric, PdfMatrix::identity(), false).has_value());
		const Image image = render_page(finished(builder), 1.0, rotation);
		TileSpec spec;
		spec.rotation_deg = rotation;
		const PdfMatrix m = page_to_tile_matrix(kPage100, spec);
		const auto cell = [&](double x, double y) {
			const PdfPoint p = transform_point(m, PdfPoint{x, y});
			return image.at(static_cast<uint32_t>(p.x), static_cast<uint32_t>(p.y));
		};
		EXPECT_EQ(cell(40.0, 60.0).r, 255) << "row 0, column 0 is red, at the top left of the unit square";
		EXPECT_EQ(cell(40.0, 60.0).g, 0);
		EXPECT_EQ(cell(60.0, 60.0).g, 255) << "row 0, column 1 is green";
		EXPECT_EQ(cell(60.0, 60.0).r, 0);
		EXPECT_EQ(cell(40.0, 40.0).b, 255) << "row 1, column 0 is blue";
		EXPECT_EQ(cell(40.0, 40.0).r, 0);
		EXPECT_EQ(cell(60.0, 40.0).r, 255) << "row 1, column 1 is yellow";
		EXPECT_EQ(cell(60.0, 40.0).g, 255);
		EXPECT_EQ(cell(60.0, 40.0).b, 0);
		EXPECT_TRUE(white(cell(25.0, 50.0))) << "outside the image";
	}
	// A black image 2 cells wide under a mask of its own resolution, 4 cells wide: opaque, clear, opaque, clear.
	const auto black = bitmap_of(2u, 1u, {0, 0, 0, 255, 0, 0, 0, 255});
	const auto stripes = bitmap_of(4u, 1u, {0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0});
	ASSERT_NE(black, nullptr);
	ASSERT_NE(stripes, nullptr);
	{
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{80.0, 0.0, 0.0, 40.0, 10.0, 30.0}).has_value());
		ASSERT_TRUE(builder.draw_image(black, PdfMatrix::identity(), false, stripes, kBlack, false).has_value());
		const Image image = render_page(finished(builder));
		for (const auto& [x, opaque] : {std::pair{20u, true}, std::pair{40u, false}, std::pair{60u, true}, std::pair{80u, false}}) {
			EXPECT_EQ(image.at(x, 50u).g, opaque ? 0 : 255) << "column " << x;
		}
	}
	// /Interpolate on the image and on the mask, each by its own flag. An image black then white, 2 cells over 80 pixels:
	// nearest is hard at the middle, linear passes through gray.
	const auto ramp = bitmap_of(2u, 1u, {0, 0, 0, 255, 255, 255, 255, 255});
	const auto gray_at_middle = [](const Image& image) { return image.at(49u, 50u).g > 40 && image.at(49u, 50u).g < 215; };
	const auto list_with = [&](bool image_linear, const std::shared_ptr<const DecodedBitmap>& mask, bool mask_linear) {
		DisplayListBuilder builder(kPage100);
		EXPECT_TRUE(builder.concat_matrix(PdfMatrix{80.0, 0.0, 0.0, 40.0, 10.0, 30.0}).has_value());
		EXPECT_TRUE(builder.draw_image(ramp, PdfMatrix::identity(), image_linear, mask, kBlack, mask_linear).has_value());
		return render_page(finished(builder));
	};
	EXPECT_FALSE(gray_at_middle(list_with(false, nullptr, false)));
	EXPECT_TRUE(gray_at_middle(list_with(true, nullptr, false)));
	// A solid black image, so only the mask (opaque then clear) can make gray.
	const auto half = bitmap_of(2u, 1u, {0, 0, 0, 255, 0, 0, 0, 0});
	const auto masked = [&](bool mask_linear) {
		DisplayListBuilder builder(kPage100);
		EXPECT_TRUE(builder.concat_matrix(PdfMatrix{80.0, 0.0, 0.0, 40.0, 10.0, 30.0}).has_value());
		EXPECT_TRUE(builder.draw_image(black, PdfMatrix::identity(), false, half, kBlack, mask_linear).has_value());
		return render_page(finished(builder));
	};
	EXPECT_FALSE(gray_at_middle(masked(false)));
	EXPECT_TRUE(gray_at_middle(masked(true)));
	// A shrunken image is filtered whatever its flag says: alternating cells at a quarter size average out.
	{
		std::vector<uint8_t> checks;
		for (int i = 0; i < 40 * 40; ++i) {
			const uint8_t v = ((i % 40) + (i / 40)) % 2 == 0 ? 0u : 255u;
			checks.insert(checks.end(), {v, v, v, 255});
		}
		const auto fine = bitmap_of(40u, 40u, std::move(checks));
		DisplayListBuilder builder(kPage100);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{20.0, 0.0, 0.0, 20.0, 40.0, 40.0}).has_value());
		ASSERT_TRUE(builder.draw_image(fine, PdfMatrix::identity(), false).has_value());
		const Image image = render_page(finished(builder));
		EXPECT_GT(image.at(50u, 50u).g, 40);
		EXPECT_LT(image.at(50u, 50u).g, 215);
	}
	// A coloured stencil at 50% fill alpha over red: where it marks, half blue over red; where it does not, red.
	{
		DisplayListBuilder builder(kPage100);
		fill_rect(builder, 0.0, 0.0, 100.0, 100.0, kRed);
		ASSERT_TRUE(builder.set_alpha_and_blend(1.0f, 0.5f).has_value());
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{80.0, 0.0, 0.0, 40.0, 10.0, 30.0}).has_value());
		const auto stencil = bitmap_of(2u, 1u, {0, 0, 0, 255, 0, 0, 0, 0}, true);
		ASSERT_TRUE(builder.draw_image(stencil, PdfMatrix::identity(), false, nullptr, kBlue).has_value());
		const Image image = render_page(finished(builder));
		EXPECT_NEAR(image.at(30u, 50u).r, 128, 2);
		EXPECT_NEAR(image.at(30u, 50u).b, 128, 2);
		EXPECT_EQ(image.at(70u, 50u).r, 255);
		EXPECT_EQ(image.at(70u, 50u).b, 0);
	}
	// The corpus's three images at their rectangles: 64 points square at x 72, 168 and 264, y 440.
	{
		const auto pages = corpus_pages("render/images-and-vectors.pdf");
		ASSERT_FALSE(pages.empty());
		const Image image = render_page(pages[0]);
		const uint32_t row = 792u - 440u - 32u;
		for (const uint32_t left : {72u, 168u, 264u}) {
			SCOPED_TRACE(left);
			EXPECT_TRUE(white(image.at(left - 2u, row)));
			EXPECT_TRUE(white(image.at(left + 66u, row)));
			EXPECT_TRUE(white(image.at(left + 32u, 792u - 505u - 2u))) << "above it";
			EXPECT_TRUE(white(image.at(left + 32u, 792u - 438u))) << "below it";
		}
		EXPECT_NEAR(image.at(72u + 32u, row).g, 128, 2) << "the JPEG's mid gray, by its DC";
		// The /SMask at (x + y) * 16 over the image's x * 32: its bottom right is opaque and its top left clear.
		EXPECT_TRUE(white(image.at(168u + 2u, 792u - 504u + 2u))) << "the clear corner";
		EXPECT_FALSE(white(image.at(168u + 60u, 792u - 440u - 4u))) << "the opaque corner";
		// CCITT's second row is 5 white, 8 black, 3 white, in cells 4 points wide: black at cell 8, white at cell 2.
		const uint32_t ccitt_row = 792u - 504u + 6u;
		EXPECT_LT(image.at(264u + 8u * 4u + 2u, ccitt_row).g, 40);
		EXPECT_GT(image.at(264u + 2u * 4u + 2u, ccitt_row).g, 215);
	}
}

TEST(PdfRasterizer, ScanReduction)
{
	// A scan's thin rule, one row in 16, shrunk 16 times: every device row holds its share of ink, where a bilinear sample
	// steps over 15 rows in 16 and shows white. The X axis keeps full size, so the reduction is per axis.
	std::vector<uint8_t> rules;
	for (uint32_t y = 0; y < 1600u; ++y) {
		for (uint32_t x = 0; x < 80u; ++x) {
			const uint8_t v = y % 16u == 0u ? 0u : 255u;
			rules.insert(rules.end(), {v, v, v, 255});
		}
	}
	const auto scan = bitmap_of(80u, 1600u, std::move(rules));
	ASSERT_NE(scan, nullptr);
	DisplayListBuilder builder(kPage100);
	ASSERT_TRUE(builder.concat_matrix(PdfMatrix{80.0, 0.0, 0.0, 100.0, 10.0, 0.0}).has_value());
	ASSERT_TRUE(builder.draw_image(scan, PdfMatrix::identity(), false).has_value());
	const Image image = render_page(finished(builder));
	for (uint32_t y = 2u; y < 98u; ++y) {
		EXPECT_NEAR(image.at(50u, y).g, 239, 4) << "row " << y << ": 15 cells in 16 white";
	}
	// Turned, it is antialiased only at its edge, and tiles still agree.
	const auto turned_list = [&] {
		DisplayListBuilder turned(kPage100);
		EXPECT_TRUE(turned.concat_matrix(PdfMatrix{40.0, 25.0, -25.0, 40.0, 45.0, 15.0}).has_value());
		EXPECT_TRUE(turned.draw_image(scan, PdfMatrix::identity(), true).has_value());
		return finished(turned);
	}();
	const Image turned_whole = render_page(turned_list, 1.5);
	EXPECT_FALSE(blank(turned_whole));
	// Its unit square's corners are (45, 15), (85, 40), (20, 55), (60, 80) in points: its box's corner stays white.
	EXPECT_TRUE(white(turned_whole.at(static_cast<uint32_t>(82.0 * 1.5), static_cast<uint32_t>((100.0 - 75.0) * 1.5))));
	EXPECT_FALSE(white(turned_whole.at(static_cast<uint32_t>(52.0 * 1.5), static_cast<uint32_t>((100.0 - 48.0) * 1.5)))) << "its middle";
	const Image turned_tiled = stitched(turned_list, 1.5, 0, 32u);
	recto::test::expect_rgba_equal(turned_tiled.bytes, turned_whole.bytes, turned_whole.width, turned_whole.height);
	// Tiles agree when they reduce it.
	const auto list = [&] {
		DisplayListBuilder again(kPage100);
		EXPECT_TRUE(again.concat_matrix(PdfMatrix{61.3, 0.0, 0.0, 77.7, 10.3, 5.1}).has_value());
		EXPECT_TRUE(again.draw_image(scan, PdfMatrix::identity(), false).has_value());
		return finished(again);
	}();
	const Image whole = render_page(list, 1.5);
	const Image tiled = stitched(list, 1.5, 0, 32u);
	recto::test::expect_rgba_equal(tiled.bytes, whole.bytes, whole.width, whole.height);
}

TEST(PdfRasterizer, ImageCacheBudget)
{
	// Shrunken copies stay within the budget, and an image and its mask both shrunk under one the second overflows are
	// both still read: the sampler keeps what it reads alive past the eviction.
	const auto square = [](uint8_t value, uint8_t alpha) {
		std::vector<uint8_t> pixels;
		pixels.reserve(400u * 400u * 4u);
		for (int i = 0; i < 400 * 400; ++i) {
			pixels.insert(pixels.end(), {value, value, value, alpha});
		}
		return bitmap_of(400u, 400u, std::move(pixels));
	};
	const auto image = square(40u, 255u);
	const auto mask = square(0u, 255u);
	const auto list = [&] {
		DisplayListBuilder builder(kPage100);
		EXPECT_TRUE(builder.concat_matrix(PdfMatrix{50.0, 0.0, 0.0, 50.0, 25.0, 25.0}).has_value());
		EXPECT_TRUE(builder.draw_image(image, PdfMatrix::identity(), true, mask, kBlack, true).has_value());
		return finished(builder);
	}();
	{
		// Each copy is 50 by 50 cells, 10,000 bytes: a budget of 12,000 holds one.
		detail::ScopedRasterImageCacheForTesting budget(12'000u);
		auto made = Rasterizer::create();
		ASSERT_TRUE(made.has_value());
		const Image tight = render_page(list, 1.0, 0, ColorRgba::white(), **made);
		EXPECT_EQ(tight.at(50u, 50u).g, 40);
		EXPECT_EQ((*made)->stats().image_cache_bytes, 10'000u) << "the older copy was evicted";
		EXPECT_EQ(render_page(list, 1.0, 0, ColorRgba::white(), **made).at(50u, 50u).g, 40);
	}
	auto made = Rasterizer::create();
	ASSERT_TRUE(made.has_value());
	(void)render_page(list, 1.0, 0, ColorRgba::white(), **made);
	EXPECT_EQ((*made)->stats().image_cache_bytes, 20'000u) << "with room, both are kept";
}

TEST(PdfRasterizer, ImagePastBitmapLimit)
{
	// As wide as a DecodedBitmap can be, past what a Direct2D bitmap holds on many machines: each cell tells its column.
	constexpr uint32_t kWidth = 65'536u;
	std::vector<uint8_t> cells;
	cells.reserve(size_t{kWidth} * 2u * 4u);
	for (uint32_t y = 0; y < 2u; ++y) {
		for (uint32_t x = 0; x < kWidth; ++x) {
			cells.insert(cells.end(), {uint8_t{0}, static_cast<uint8_t>(x >> 8u), static_cast<uint8_t>(x & 0xFFu), uint8_t{255}});
		}
	}
	const auto wide = bitmap_of(kWidth, 2u, std::move(cells));
	ASSERT_NE(wide, nullptr);
	std::vector<uint8_t> edge;
	for (uint32_t y = 0; y < 2u; ++y) {
		for (uint32_t x = 0; x < kWidth; ++x) {
			edge.insert(edge.end(), {uint8_t{0}, uint8_t{0}, uint8_t{0}, x < 60'000u ? uint8_t{255} : uint8_t{0}});
		}
	}
	const auto soft = bitmap_of(kWidth, 2u, std::move(edge));
	ASSERT_NE(soft, nullptr);
	const auto page = [&](bool masked) {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, static_cast<double>(kWidth), 20.0});
		EXPECT_TRUE(builder.concat_matrix(PdfMatrix{static_cast<double>(kWidth), 0.0, 0.0, 20.0, 0.0, 0.0}).has_value());
		EXPECT_TRUE(builder.draw_image(wide, PdfMatrix::identity(), false, masked ? soft : nullptr, kBlack, false).has_value());
		return finished(builder);
	};
	const auto plain = page(false);
	for (const uint32_t x : {0u, 30'000u, 60'000u, kWidth - 512u}) {
		SCOPED_TRACE(x);
		TileSpec spec;
		spec.tile_x_px = x;
		spec.tile_width_px = 512u;
		spec.tile_height_px = 20u;
		const Image tile = render(plain, spec);
		for (uint32_t column = 0; column < 512u; column += 37u) {
			const uint32_t cell = x + column;
			ASSERT_EQ(tile.at(column, 10u).r, static_cast<int>(cell & 0xFFu)) << column;
			ASSERT_EQ(tile.at(column, 10u).g, static_cast<int>(cell >> 8u)) << column;
		}
	}
	// Its mask, at its own cells: opaque to column 60,000 and clear past it, across this tile.
	TileSpec spec;
	spec.tile_x_px = 59'900u;
	spec.tile_width_px = 512u;
	spec.tile_height_px = 20u;
	const Image tile = render(page(true), spec);
	EXPECT_FALSE(white(tile.at(50u, 10u)));
	EXPECT_TRUE(white(tile.at(150u, 10u))) << "past the mask's edge";
}

TEST(PdfRasterizer, AbuttingWithoutSeam)
{
	// Strips of one scan meeting at fractional zoom: each edge lands on a whole pixel, so no row between them is partly
	// covered and shows the paper through. Independent of any tile: every row of the strips is exactly the strip's colour.
	const auto gray = bitmap_of(1u, 1u, {60, 60, 60, 255});
	ASSERT_NE(gray, nullptr);
	for (const double scale : {1.0, 1.1, 1.333, 1.5, 2.25}) {
		SCOPED_TRACE(scale);
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 100.0, 100.0});
		for (int strip = 0; strip < 9; ++strip) {
			ASSERT_TRUE(builder.save_state().has_value());
			ASSERT_TRUE(builder.concat_matrix(PdfMatrix{60.0, 0.0, 0.0, 10.3, 20.0, 5.0 + strip * 10.3}).has_value());
			ASSERT_TRUE(builder.draw_image(gray, PdfMatrix::identity(), false).has_value());
			ASSERT_TRUE(builder.restore_state().has_value());
		}
		const auto list = finished(builder);
		const Image image = render_page(list, scale);
		const uint32_t column = static_cast<uint32_t>(50.0 * scale);
		const uint32_t top = static_cast<uint32_t>(std::ceil((100.0 - 5.0 - 9.0 * 10.3) * scale)) + 1u;
		const uint32_t bottom = static_cast<uint32_t>((100.0 - 5.0) * scale) - 1u;
		for (uint32_t y = top; y < bottom; ++y) {
			EXPECT_EQ(image.at(column, y).g, 60) << "row " << y;
		}
		// Translucent strips show a row painted twice as darker, so the strips must meet and not overlap.
		{
			DisplayListBuilder translucent(PdfRect{0.0, 0.0, 100.0, 100.0});
			ASSERT_TRUE(translucent.set_alpha_and_blend(1.0f, 0.5f).has_value());
			const auto ink = bitmap_of(1u, 1u, {0, 0, 0, 255});
			for (int strip = 0; strip < 9; ++strip) {
				ASSERT_TRUE(translucent.save_state().has_value());
				ASSERT_TRUE(translucent.concat_matrix(PdfMatrix{60.0, 0.0, 0.0, 10.3, 20.0, 5.0 + strip * 10.3}).has_value());
				ASSERT_TRUE(translucent.draw_image(ink, PdfMatrix::identity(), false).has_value());
				ASSERT_TRUE(translucent.restore_state().has_value());
			}
			const Image half = render_page(finished(translucent), scale);
			for (uint32_t y = top; y < bottom; ++y) {
				EXPECT_NEAR(half.at(column, y).g, 128, 1) << "row " << y;
			}
		}
		// Abutting strips crossing an engine tile's edge, tile against whole.
		const Image large = render_page(list, scale * 8.0);
		const Image tiled = stitched(list, scale * 8.0, 0);
		recto::test::expect_rgba_equal(tiled.bytes, large.bytes, large.width, large.height);
	}
	// Abutting vector fills whose shared edge is a tile's edge, on a whole pixel: solid across it, by its own colour.
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 800.0, 100.0});
	const double edge = 512.0 / 1.5; // the first engine tile's right edge at 150%
	fill_rect(builder, 100.0, 10.0, edge - 100.0, 80.0, kBlack);
	fill_rect(builder, edge, 10.0, 200.0, 80.0, kBlack);
	const Image tiled = stitched(finished(builder), 1.5, 0);
	for (uint32_t x = 505u; x < 520u; ++x) {
		EXPECT_EQ(tiled.at(x, 75u).g, 0) << "column " << x;
	}
}

TEST(PdfRasterizer, ClipsStitch)
{
	// An arbitrary clip and a turned rectangular one, each across tile edges, at a fractional zoom: tiles equal the whole.
	const auto curved = [] {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 400.0, 400.0});
		PathBuilder blob;
		blob.move_to(20.0, 200.0);
		blob.cubic_bezier_to(PdfPoint{100.0, 420.0}, PdfPoint{300.0, 420.0}, PdfPoint{380.0, 200.0});
		blob.cubic_bezier_to(PdfPoint{300.0, -20.0}, PdfPoint{100.0, -20.0}, PdfPoint{20.0, 200.0});
		blob.close_subpath();
		const auto id = builder.add_path(blob);
		EXPECT_TRUE(id.has_value());
		EXPECT_TRUE(builder.push_clip_path(*id).has_value());
		fill_rect(builder, 0.0, 0.0, 400.0, 400.0, ColorRgba{0.2f, 0.4f, 0.8f});
		return finished(builder);
	}();
	const auto turned = [] {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 400.0, 400.0});
		const double c = std::cos(0.5);
		const double s = std::sin(0.5);
		EXPECT_TRUE(builder.concat_matrix(PdfMatrix{c, s, -s, c, 200.0, 40.0}).has_value());
		PathBuilder rect;
		rect.rect(0.0, 0.0, 200.0, 200.0);
		const auto id = builder.add_path(rect);
		EXPECT_TRUE(id.has_value());
		EXPECT_TRUE(builder.push_clip_path(*id).has_value());
		fill_rect(builder, -400.0, -400.0, 1200.0, 1200.0, ColorRgba{0.8f, 0.3f, 0.2f});
		return finished(builder);
	}();
	for (const auto& list : {curved, turned}) {
		for (const double scale : {1.0, 1.37, 2.0}) {
			SCOPED_TRACE(scale);
			const Image whole = render_page(list, scale);
			const Image tiled = stitched(list, scale, 0, 128u);
			recto::test::expect_rgba_equal(tiled.bytes, whole.bytes, whole.width, whole.height);
		}
	}
}

TEST(PdfRasterizer, TextOnPage)
{
	// Each glyph of the corpus page's dark line has ink inside its own box, and nothing around the run.
	const auto pages = corpus_pages("render/simple-1p.pdf");
	ASSERT_FALSE(pages.empty());
	const Image image = render_page(pages[0]);
	size_t runs = 0;
	for (const DisplayCommand& command : pages[0]->commands()) {
		const auto* run = command.as<DrawGlyphRunCommand>();
		if (run == nullptr || run->font_size != 12.0) {
			continue;
		}
		++runs;
		const auto glyphs = pages[0]->glyphs(*run);
		ASSERT_TRUE(glyphs.has_value());
		const PdfMatrix to_page = concat_matrix(run->text_matrix, run->ctm);
		for (const PositionedGlyph& glyph : *glyphs) {
			if (glyph.is_blank || !glyph.ink_bounds_pt.has_value()) {
				continue;
			}
			const PdfRect box = transform_rect(concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, glyph.origin_pt.x, glyph.origin_pt.y}, to_page), *glyph.ink_bounds_pt);
			bool ink = false;
			for (uint32_t y = static_cast<uint32_t>(792.0 - box.y_max); y <= static_cast<uint32_t>(792.0 - box.y_min); ++y) {
				for (uint32_t x = static_cast<uint32_t>(box.x_min); x <= static_cast<uint32_t>(box.x_max); ++x) {
					ink = ink || image.at(x, y).g < 128;
				}
			}
			EXPECT_TRUE(ink) << "glyph " << glyph.glyph_index << " at " << glyph.origin_pt.x;
		}
		// The baseline is at 636: 20 points clear of it on either side holds nothing of this line.
		EXPECT_TRUE(white(image.at(100u, 792u - 660u + 8u)));
	}
	EXPECT_EQ(runs, 1u);
	// A run moved one point moves its ink exactly `dpi_scale` pixels.
	const auto font = truetype("truetype-full-short-loca.ttf");
	ASSERT_NE(font, nullptr);
	const std::array<uint16_t, 2> glyphs{1u, 3u};
	const auto at = [&](double x) {
		DisplayListBuilder builder(kPage100);
		draw_glyphs(builder, font, glyphs, x, 40.0, 24.0);
		return render_page(finished(builder), 2.0);
	};
	const Image left = at(10.0);
	const Image right = at(11.0);
	const InkBox a = ink_of(left);
	const InkBox b = ink_of(right);
	ASSERT_GT(a.count, 0u);
	EXPECT_EQ(b.x_min, a.x_min + 2u);
	EXPECT_EQ(b.x_max, a.x_max + 2u);
	EXPECT_EQ(b.y_min, a.y_min);
	EXPECT_EQ(a.count, b.count);
}

TEST(PdfRasterizer, TextRenderModes)
{
	const auto directwrite = truetype("truetype-full-short-loca.ttf");
	const auto outline_only = truetype("truetype-subset-short-loca.ttf");
	ASSERT_NE(directwrite, nullptr);
	ASSERT_NE(outline_only, nullptr);
	ASSERT_NE(directwrite->dwrite_face(), nullptr);
	ASSERT_EQ(outline_only->kind(), LoadedFontKind::OutlineOnly);
	for (const auto& font : {directwrite, outline_only}) {
		SCOPED_TRACE(font->kind() == LoadedFontKind::OutlineOnly ? "outline only" : "DirectWrite");
		const std::array<uint16_t, 1> glyph{1u};
		const auto draw = [&](TextRenderMode mode) {
			DisplayListBuilder builder(kPage100);
			EXPECT_TRUE(builder.set_stroke_style(1.0f).has_value());
			draw_glyphs(builder, font, glyph, 10.0, 20.0, 80.0, mode, kBlue, kRed);
			return render_page(finished(builder));
		};
		const auto count = [](const Image& image, auto&& test) {
			size_t n = 0;
			for (uint32_t y = 0; y < image.height; ++y) {
				for (uint32_t x = 0; x < image.width; ++x) {
					n += test(image.at(x, y)) ? 1u : 0u;
				}
			}
			return n;
		};
		const auto is_blue = [](const Bgra& p) { return p.b > 200 && p.r < 60; };
		const auto is_red = [](const Bgra& p) { return p.r > 200 && p.b < 60; };
		const Image fill = draw(TextRenderMode::Fill);
		const Image stroke = draw(TextRenderMode::Stroke);
		const Image both = draw(TextRenderMode::FillAndStroke);
		EXPECT_GT(count(fill, is_blue), 100u);
		EXPECT_EQ(count(fill, is_red), 0u);
		EXPECT_GT(count(stroke, is_red), 20u);
		EXPECT_EQ(count(stroke, is_blue), 0u);
		EXPECT_LT(count(stroke, is_red), count(fill, is_blue)) << "a stroke at 1 point is hollow";
		EXPECT_GT(count(both, is_blue), 100u);
		EXPECT_GT(count(both, is_red), 20u);
		EXPECT_TRUE(blank(draw(TextRenderMode::Invisible)));
		EXPECT_TRUE(blank(draw(TextRenderMode::Clip))) << "its clip is D01 T02 section 16's; it paints nothing";
	}
}

TEST(PdfRasterizer, GlyphBudgetsBeforeDirectWrite)
{
	const auto font = truetype("truetype-hostile-budgets.ttf");
	ASSERT_NE(font, nullptr);
	ASSERT_EQ(font->kind(), LoadedFontKind::TrueType) << "the path this proves is the DirectWrite one";
	clear_diagnostics();
	DisplayListBuilder builder(kPage100);
	std::vector<PositionedGlyph> run;
	for (const uint16_t glyph : {uint16_t{2}, uint16_t{16}}) {
		PositionedGlyph placed;
		placed.glyph_index = glyph;
		placed.origin_pt = PdfPoint{10.0, 10.0};
		placed.advance_pt = 50.0;
		placed.ink_bounds_pt = PdfRect{0.0, 0.0, 50.0, 50.0};
		run.push_back(placed);
	}
	ASSERT_TRUE(builder.draw_glyph_run(run, 50.0, TextRenderMode::Fill, kBlack, kBlack, PdfMatrix::identity(), std::nullopt, std::shared_ptr<const void>(font)).has_value());
	fill_rect(builder, 80.0, 80.0, 10.0, 10.0, kRed);
	const Image image = render_page(finished(builder));
	EXPECT_TRUE(recorded(ErrorCode::ResourceLimit, RefusalReason::GlyphPoints));
	EXPECT_TRUE(recorded(ErrorCode::ResourceLimit, RefusalReason::ComponentVisits));
	EXPECT_EQ(ink_of(image).x_min, 80u) << "the page goes on, and neither glyph paints";
	EXPECT_EQ(image.at(85u, 15u).r, 255);
}

TEST(PdfRasterizer, GlyphWidthScale)
{
	// Each scale fits a glyph about its origin: its ink's left and right follow the scaled outline, its origin does not move.
	const auto check = [](const std::shared_ptr<const LoadedFont>& font, uint16_t glyph, double scale) {
		SCOPED_TRACE(scale);
		const auto ink = font->ink_box(glyph);
		ASSERT_TRUE(ink.has_value());
		ASSERT_TRUE(ink->has_value());
		const std::array<uint16_t, 1> one{glyph};
		constexpr double kSize = 60.0;
		constexpr double kOrigin = 20.0;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 100.0});
		draw_glyphs(builder, font, one, kOrigin, 20.0, kSize, TextRenderMode::Fill, kBlack, kBlack, scale);
		const InkBox box = ink_of(render_page(finished(builder)));
		ASSERT_GT(box.count, 0u);
		EXPECT_NEAR(static_cast<double>(box.x_min), kOrigin + (*ink)->x_min * kSize * scale, 1.5);
		EXPECT_NEAR(static_cast<double>(box.x_max) + 1.0, kOrigin + (*ink)->x_max * kSize * scale, 1.5);
	};
	const auto directwrite = truetype("truetype-full-short-loca.ttf");
	const auto outline_only = truetype("truetype-subset-short-loca.ttf");
	ASSERT_NE(directwrite, nullptr);
	ASSERT_NE(outline_only, nullptr);
	for (const double scale : {0.6, 1.0, 1.4}) {
		check(directwrite, 1u, scale);
		check(outline_only, 1u, scale);
	}
	// A Times line whose substitute macron and summation §43 fits, scales below and above 1 (`kKnownWidthDifferences`:
	// macron 552 on the face against 333 in the AFM; the same seen the other way for a wider advance).
	const auto macron = standard14_face_glyph(Standard14Font::TimesRoman, U'¯');
	ASSERT_TRUE(macron.has_value());
	ASSERT_TRUE(macron->has_value());
	check((*macron)->face, (*macron)->glyph, 333.0 / 552.0);
	const auto summation = standard14_face_glyph(Standard14Font::TimesRoman, U'∑');
	ASSERT_TRUE(summation.has_value());
	if (summation->has_value()) {
		check((*summation)->face, (*summation)->glyph, 1.25);
	}
}

TEST(PdfRasterizer, NullFontRun)
{
	// §43's advance-only glyphs: no font, so nothing painted, and what follows stands where it was put.
	DisplayListBuilder builder(kPage100);
	PositionedGlyph glyph;
	glyph.glyph_index = 1u;
	glyph.origin_pt = PdfPoint{10.0, 10.0};
	glyph.advance_pt = 20.0;
	glyph.ink_bounds_pt = PdfRect{0.0, 0.0, 20.0, 20.0};
	const std::array<PositionedGlyph, 1> run{glyph};
	ASSERT_TRUE(builder.draw_glyph_run(run, 20.0, TextRenderMode::Fill, kBlack, kBlack).has_value());
	fill_rect(builder, 70.0, 70.0, 10.0, 10.0, kBlack);
	const InkBox box = ink_of(render_page(finished(builder)));
	EXPECT_EQ(box.x_min, 70u);
	EXPECT_EQ(box.y_min, 20u);
	EXPECT_EQ(box.count, 100u);
}

TEST(PdfRasterizer, Type3SubList)
{
	// A 5-point square in the sub-list's text space, at each of two glyph origins, under the run's matrix.
	DisplayListBuilder glyph_builder(PdfRect{0.0, 0.0, 5.0, 5.0});
	fill_rect(glyph_builder, 0.0, 0.0, 5.0, 5.0, kBlack);
	const auto square = finished(glyph_builder);
	const auto run_of = [](PdfPoint origin) {
		PositionedGlyph placed;
		placed.glyph_index = 1u;
		placed.origin_pt = origin;
		placed.advance_pt = 10.0;
		return placed;
	};
	{
		DisplayListBuilder builder(kPage100);
		const std::array<PositionedGlyph, 2> run{run_of(PdfPoint{0.0, 0.0}), run_of(PdfPoint{30.0, 0.0})};
		ASSERT_TRUE(builder.draw_glyph_run(run, 1.0, TextRenderMode::Fill, kBlack, kBlack, PdfMatrix{1.0, 0.0, 0.0, 1.0, 20.0, 50.0},
			PdfRect{0.0, 0.0, 40.0, 10.0}, nullptr, square).has_value());
		const Image image = render_page(finished(builder));
		EXPECT_TRUE(painted(image.at(22u, 47u)));
		EXPECT_TRUE(painted(image.at(52u, 47u)));
		EXPECT_TRUE(white(image.at(37u, 47u)));
		EXPECT_EQ(ink_of(image).count, 50u);
	}
	// A chain of nine sub-lists, each marking a pixel column of its own and showing the next: the ninth is past the bound.
	std::shared_ptr<const DisplayList> chain;
	for (int level = 9; level >= 1; --level) {
		DisplayListBuilder link(kPage100);
		fill_rect(link, level * 10.0, 50.0, 5.0, 5.0, kBlack);
		if (chain != nullptr) {
			const std::array<PositionedGlyph, 1> next{run_of(PdfPoint{0.0, 0.0})};
			ASSERT_TRUE(link.draw_glyph_run(next, 1.0, TextRenderMode::Fill, kBlack, kBlack, PdfMatrix::identity(), PdfRect{0.0, 0.0, 100.0, 100.0}, nullptr, chain).has_value());
		}
		chain = finished(link);
	}
	clear_diagnostics();
	DisplayListBuilder builder(kPage100);
	const std::array<PositionedGlyph, 1> first{run_of(PdfPoint{0.0, 0.0})};
	ASSERT_TRUE(builder.draw_glyph_run(first, 1.0, TextRenderMode::Fill, kBlack, kBlack, PdfMatrix::identity(), PdfRect{0.0, 0.0, 100.0, 100.0}, nullptr, chain).has_value());
	const Image image = render_page(finished(builder));
	for (int level = 1; level <= 8; ++level) {
		EXPECT_TRUE(painted(image.at(static_cast<uint32_t>(level * 10 + 2), 47u))) << "level " << level;
	}
	EXPECT_TRUE(white(image.at(92u, 47u))) << "the ninth level";
	EXPECT_TRUE(recorded(ErrorCode::ResourceLimit));
}

TEST(PdfRasterizer, HostileGeometry)
{
	clear_diagnostics();
	DisplayListBuilder builder(kPage100);
	fill_rect(builder, 40.0, 40.0, 20.0, 20.0, kBlack);
	// A coordinate far past any tile, kept finite; its fill reaches the whole page.
	fill_rect(builder, -1e30, 90.0, 2e30, 5.0, kBlack);
	// A width past anything a pen can be, then a dash that would cut one line into ten million pieces.
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.set_stroke_style(1e30f).has_value());
	stroke_line(builder, {PdfPoint{0.0, 200.0}, PdfPoint{10.0, 200.0}});
	ASSERT_TRUE(builder.restore_state().has_value());
	const std::array<float, 2> dash{0.001f, 0.001f};
	ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Miter, 10.0f, dash).has_value());
	stroke_line(builder, {PdfPoint{0.0, 10.0}, PdfPoint{10000.0, 10.0}});
	// A finite matrix that takes every point past a float; the builder refuses one that goes past a double.
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1e200, 0.0, 0.0, 1e200, 0.0, 0.0}).has_value());
	PathBuilder lost;
	lost.rect(1.0, 1.0, 1.0, 1.0);
	const auto lost_id = builder.add_path(lost);
	ASSERT_TRUE(lost_id.has_value());
	(void)builder.draw_path(*lost_id, kBlack, kBlack, true, true);
	ASSERT_TRUE(builder.restore_state().has_value());
	const auto list = finished(builder);
	ASSERT_NE(list, nullptr);
	const auto started = std::chrono::steady_clock::now();
	const Image image = render_page(list);
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(20));
	EXPECT_TRUE(painted(image.at(50u, 50u))) << "the rectangle beside them";
	EXPECT_TRUE(painted(image.at(50u, 89u))) << "the dash refused draws solid";
	EXPECT_TRUE(painted(image.at(52u, 7u))) << "the huge fill, clamped";
	const auto tail = diagnostics_tail(32u);
	const auto dash_refusal = std::find_if(tail.begin(), tail.end(), [](const DiagnosticEntry& e) { return e.refusal_reason == RefusalReason::DashSegments; });
	ASSERT_NE(dash_refusal, tail.end());
	EXPECT_EQ(dash_refusal->code, ErrorCode::ResourceLimit);
	EXPECT_EQ(dash_refusal->bound, kMaxDashSegments);
	EXPECT_GT(dash_refusal->observed.value_or(0u), kMaxDashSegments);
}

TEST(PdfRasterizer, RenderTargetReuse)
{
	// 100 engine tiles on one worker make one target.
	auto made = Rasterizer::create();
	ASSERT_TRUE(made.has_value());
	Rasterizer& fresh = **made;
	const auto pages = corpus_pages("render/images-and-vectors.pdf");
	ASSERT_FALSE(pages.empty());
	std::vector<uint8_t> tile(size_t{kEngineTileEdgePx} * kEngineTileEdgePx * 4u);
	for (uint32_t i = 0; i < 100u; ++i) {
		TileSpec spec;
		spec.tile_x_px = (i % 10u) * 64u;
		spec.tile_y_px = (i / 10u) * 64u;
		ASSERT_TRUE(fresh.render_tile(pages[0], spec, tile, size_t{kEngineTileEdgePx} * 4u).has_value());
	}
	EXPECT_EQ(fresh.stats().target_creations, 1u);
}

TEST(PdfRasterizer, ReuseMatchesFresh)
{
	// Lists that leave every kind of state behind, in turn on one worker, each with both backgrounds.
	DisplayListBuilder busy(PdfRect{0.0, 0.0, 700.0, 700.0});
	PathBuilder triangle;
	triangle.move_to(0.0, 0.0);
	triangle.line_to(600.0, 0.0);
	triangle.line_to(0.0, 600.0);
	triangle.close_subpath();
	const auto id = busy.add_path(triangle);
	ASSERT_TRUE(id.has_value());
	ASSERT_TRUE(busy.save_state().has_value());
	ASSERT_TRUE(busy.concat_matrix(PdfMatrix{1.2, 0.3, -0.3, 1.2, 10.0, 10.0}).has_value());
	ASSERT_TRUE(busy.push_clip_path(*id).has_value());
	ASSERT_TRUE(busy.set_alpha_and_blend(0.5f, 0.25f).has_value());
	ASSERT_TRUE(busy.set_stroke_style(9.0f).has_value());
	fill_rect(busy, 0.0, 0.0, 500.0, 500.0, kRed);
	stroke_line(busy, {PdfPoint{0.0, 0.0}, PdfPoint{400.0, 400.0}});
	// The list ends with its state still open: the next tile must not see it.
	const auto busy_list = finished(busy);
	DisplayListBuilder empty_builder(PdfRect{0.0, 0.0, 700.0, 700.0});
	const auto empty_list = finished(empty_builder);
	const auto corpus = corpus_pages("render/simple-1p.pdf");
	ASSERT_FALSE(corpus.empty());
	const ColorRgba clear{0.0f, 0.0f, 0.0f, 0.0f};

	auto made = Rasterizer::create();
	ASSERT_TRUE(made.has_value());
	Rasterizer& reused = **made;
	for (const auto& list : {busy_list, empty_list, corpus[0], empty_list, busy_list}) {
		for (const ColorRgba background : {ColorRgba::white(), clear}) {
			TileSpec spec;
			spec.background_rgba = background;
			// A tile crossing the page's edge: 700 by 700 at scale 1, so 188 of its rows and columns are past it.
			spec.tile_x_px = 512u;
			spec.tile_y_px = 512u;
			const Image a = render(list, spec, reused);
			auto other = Rasterizer::create();
			ASSERT_TRUE(other.has_value());
			const Image b = render(list, spec, **other);
			recto::test::expect_rgba_equal(a.bytes, b.bytes, a.width, a.height);
			if (list != corpus[0]) {
				const Bgra past = a.at(511u, 511u);
				EXPECT_EQ(past.a, background.a == 0.0f ? 0 : 255) << "past the page is the background";
			}
		}
	}
	// Nothing paints past the page's edge, on any list: a fill wider than the page stops at it.
	DisplayListBuilder wide(PdfRect{0.0, 0.0, 100.0, 100.0});
	fill_rect(wide, -50.0, -50.0, 1000.0, 1000.0, kBlack);
	TileSpec spec;
	spec.tile_width_px = 150u;
	spec.tile_height_px = 150u;
	const Image image = render(finished(wide), spec);
	EXPECT_TRUE(painted(image.at(99u, 99u)));
	EXPECT_TRUE(white(image.at(100u, 50u)));
	EXPECT_TRUE(white(image.at(50u, 100u)));
}

TEST(PdfRasterizer, CullsOffTileCommands)
{
	// 1,000 rectangles in rows, and a tile that shows exactly one.
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 1000.0, 1000.0});
	for (int i = 0; i < 1000; ++i) {
		fill_rect(builder, (i % 40) * 25.0, (i / 40) * 25.0, 4.0, 4.0, kBlack);
	}
	const auto list = finished(builder);
	auto made = Rasterizer::create();
	ASSERT_TRUE(made.has_value());
	TileSpec spec;
	spec.tile_width_px = 16u;
	spec.tile_height_px = 16u;
	spec.tile_x_px = 100u + 6u; // the column at x 100 ends at 104; the next begins at 125
	spec.tile_y_px = 1000u - 104u - 6u; // the row at y 100, upward
	spec.tile_x_px = 96u;
	spec.tile_y_px = 892u;
	const Image image = render(list, spec, **made);
	EXPECT_EQ((*made)->stats().draws_replayed, 1u);
	EXPECT_GT(ink_of(image).count, 0u);
	// A stroked glyph whose outline stops short of the tile and whose stroke reaches into it.
	const auto font = truetype("truetype-full-short-loca.ttf");
	ASSERT_NE(font, nullptr);
	const std::array<uint16_t, 1> glyph{1u};
	DisplayListBuilder stroked(PdfRect{0.0, 0.0, 600.0, 200.0});
	ASSERT_TRUE(stroked.set_stroke_style(40.0f, LineCap::Butt, LineJoin::Round).has_value());
	draw_glyphs(stroked, font, glyph, 100.0, 50.0, 100.0, TextRenderMode::Stroke);
	const auto stroked_list = finished(stroked);
	const Image whole = render_page(stroked_list);
	const auto ink = font->ink_box(1u);
	ASSERT_TRUE(ink.has_value());
	ASSERT_TRUE(ink->has_value());
	const uint32_t outline_right = static_cast<uint32_t>(100.0 + (*ink)->x_max * 100.0);
	TileSpec past;
	past.tile_x_px = outline_right + 5u; // past the outline, within the stroke's 20
	past.tile_y_px = 0u;
	past.tile_width_px = 64u;
	past.tile_height_px = 200u;
	const Image tile = render(stroked_list, past, **made);
	EXPECT_GT(ink_of(tile).count, 0u) << "the stroke's reach was culled";
	for (uint32_t y = 0; y < 200u; ++y) {
		for (uint32_t x = 0; x < 64u; ++x) {
			const Bgra from_tile = tile.at(x, y);
			const Bgra from_whole = whole.at(x + past.tile_x_px, y);
			ASSERT_EQ(from_tile.g, from_whole.g) << x << ", " << y;
		}
	}
}

TEST(PdfRasterizer, OwnerThreadOnly)
{
	const auto pages = corpus_pages("render/images-and-vectors.pdf");
	ASSERT_FALSE(pages.empty());
	auto made = Rasterizer::create();
	ASSERT_TRUE(made.has_value());
	std::thread other([&] {
		std::vector<uint8_t> tile(16u * 16u * 4u);
		TileSpec spec;
		spec.tile_width_px = 16u;
		spec.tile_height_px = 16u;
		const auto done = (*made)->render_tile(pages[0], spec, tile, 64u);
		EXPECT_FALSE(done.has_value());
		if (!done.has_value()) {
			EXPECT_EQ(done.error().code, ErrorCode::InternalError);
		}
	});
	other.join();
}

TEST(PdfRasterizer, SharedListTwoWorkers)
{
	const auto pages = corpus_pages("render/images-and-vectors.pdf");
	ASSERT_FALSE(pages.empty());
	const Image expected = render_page(pages[0], 1.5);
	std::array<Image, 2> results;
	std::array<std::thread, 2> workers;
	for (size_t i = 0; i < workers.size(); ++i) {
		workers[i] = std::thread([&, i] {
			auto made = Rasterizer::create();
			ASSERT_TRUE(made.has_value());
			for (int round = 0; round < 5; ++round) {
				results[i] = render_page(pages[0], 1.5, 0, ColorRgba::white(), **made);
			}
		});
	}
	for (std::thread& worker_thread : workers) {
		worker_thread.join();
	}
	for (const Image& result : results) {
		recto::test::expect_rgba_equal(result.bytes, expected.bytes, expected.width, expected.height);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Goldens.
// ---------------------------------------------------------------------------------------------------------------------

constexpr double kGoldenSsim = 0.98;
constexpr uint32_t kGoldenWindow = 32u;
constexpr double kGoldenWindowSsim = 0.90;

std::filesystem::path golden_path(std::string_view name)
{
	return recto::test::corpus_path().parent_path() / "goldens" / "rasterizer" / (std::string(name) + ".png");
}

ComPtr<IWICImagingFactory> wic_factory()
{
	(void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	ComPtr<IWICImagingFactory> factory;
	EXPECT_TRUE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))));
	return factory;
}

bool write_png(const std::filesystem::path& path, const Image& image)
{
	std::filesystem::create_directories(path.parent_path());
	const std::filesystem::path temporary = path.wstring() + L".tmp";
	const ComPtr<IWICImagingFactory> factory = wic_factory();
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	bool ok = SUCCEEDED(factory->CreateStream(stream.GetAddressOf())) &&
		SUCCEEDED(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE)) &&
		SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
		SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
		SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
		SUCCEEDED(frame->SetSize(image.width, image.height));
	if (ok) {
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		ok = SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA &&
			SUCCEEDED(frame->WritePixels(image.height, image.width * 4u, static_cast<UINT>(image.bytes.size()), const_cast<BYTE*>(image.bytes.data()))) &&
			SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
	}
	frame.Reset();
	encoder.Reset();
	stream.Reset();
	std::error_code error;
	if (ok) {
		std::filesystem::rename(temporary, path, error);
		ok = !error;
	}
	if (!ok) {
		std::filesystem::remove(temporary, error);
	}
	return ok;
}

std::optional<Image> read_png(const std::filesystem::path& path)
{
	const ComPtr<IWICImagingFactory> factory = wic_factory();
	ComPtr<IWICBitmapDecoder> decoder;
	if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()))) {
		return std::nullopt;
	}
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter> converter;
	UINT width = 0;
	UINT height = 0;
	if (FAILED(decoder->GetFrame(0u, frame.GetAddressOf())) || FAILED(frame->GetSize(&width, &height)) ||
		FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
		FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
		return std::nullopt;
	}
	Image image{width, height, std::vector<uint8_t>(size_t{width} * height * 4u)};
	if (FAILED(converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(image.bytes.size()), image.bytes.data()))) {
		return std::nullopt;
	}
	return image;
}

enum class GoldenResult { Match, Missing, WrongSize, Different };

struct Similarity {
	double ink{1.0}; // SSIM over the box holding both images' ink
	double worst{1.0}; // the lowest SSIM of the windows holding ink
};

[[nodiscard]] Image cropped(const Image& image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
	Image out{x1 - x0, y1 - y0, std::vector<uint8_t>(size_t{x1 - x0} * (y1 - y0) * 4u)};
	for (uint32_t y = y0; y < y1; ++y) {
		std::copy_n(image.bytes.data() + (size_t{y} * image.width + x0) * 4u, size_t{out.width} * 4u,
			out.bytes.data() + size_t{y - y0} * out.width * 4u);
	}
	return out;
}

// SSIM over a whole page is held up by its white paper: a page missing a glyph still scores above any threshold. So it
// is measured where the ink is, and a window of `kGoldenWindow` pixels that lost its ink fails on its own.
[[nodiscard]] Similarity similarity(const Image& a, const Image& b)
{
	const InkBox ia = ink_of(a);
	const InkBox ib = ink_of(b);
	Similarity out;
	if (ia.count == 0u && ib.count == 0u) {
		return out;
	}
	if (ia.count == 0u || ib.count == 0u) {
		return Similarity{0.0, 0.0};
	}
	const uint32_t x0 = std::min(ia.x_min, ib.x_min);
	const uint32_t y0 = std::min(ia.y_min, ib.y_min);
	const uint32_t x1 = std::max(ia.x_max, ib.x_max) + 1u;
	const uint32_t y1 = std::max(ia.y_max, ib.y_max) + 1u;
	const Image ca = cropped(a, x0, y0, x1, y1);
	const Image cb = cropped(b, x0, y0, x1, y1);
	out.ink = recto::test::compute_ssim(ca.bytes, cb.bytes, ca.width, ca.height);
	for (uint32_t y = 0; y < ca.height; y += kGoldenWindow) {
		for (uint32_t x = 0; x < ca.width; x += kGoldenWindow) {
			const uint32_t right = std::min(x + kGoldenWindow, ca.width);
			const uint32_t bottom = std::min(y + kGoldenWindow, ca.height);
			const Image wa = cropped(ca, x, y, right, bottom);
			const Image wb = cropped(cb, x, y, right, bottom);
			if (ink_of(wa).count != 0u || ink_of(wb).count != 0u) {
				out.worst = std::min(out.worst, recto::test::compute_ssim(wa.bytes, wb.bytes, wa.width, wa.height));
			}
		}
	}
	return out;
}

[[nodiscard]] bool similar(const Similarity& s)
{
	return s.ink >= kGoldenSsim && s.worst >= kGoldenWindowSsim;
}

GoldenResult compare_golden(const std::filesystem::path& path, const Image& image, Similarity* measured = nullptr)
{
	const auto golden = read_png(path);
	if (!golden.has_value()) {
		return GoldenResult::Missing;
	}
	if (golden->width != image.width || golden->height != image.height) {
		return GoldenResult::WrongSize;
	}
	const Similarity s = similarity(image, *golden);
	if (measured != nullptr) {
		*measured = s;
	}
	return similar(s) ? GoldenResult::Match : GoldenResult::Different;
}

// A name's golden, written first when `RECTO_UPDATE_GOLDENS=1`.
void expect_golden(std::string_view name, const Image& image)
{
	SCOPED_TRACE(name);
	const std::filesystem::path path = golden_path(name);
	char* update = nullptr;
	size_t length = 0;
	const bool updating = _dupenv_s(&update, &length, "RECTO_UPDATE_GOLDENS") == 0 && update != nullptr && std::string_view(update) == "1";
	free(update);
	if (updating) {
		ASSERT_TRUE(write_png(path, image));
	}
	Similarity measured;
	const GoldenResult result = compare_golden(path, image, &measured);
	EXPECT_EQ(result, GoldenResult::Match) << path.string() << " SSIM over the ink " << measured.ink << ", worst window " << measured.worst
										   << " (missing is written with RECTO_UPDATE_GOLDENS=1)";
}

// Every glyph of `font`, from `first` to at most `count` of them, a line at each of 8, 10 and 12 points.
std::shared_ptr<const DisplayList> small_text_page(const std::shared_ptr<const LoadedFont>& font, uint16_t count)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 400.0, 90.0});
	std::vector<uint16_t> glyphs;
	for (uint16_t glyph = 0; glyph < std::min(font->glyph_count(), count); ++glyph) {
		glyphs.push_back(glyph);
	}
	double y = 66.0;
	for (const double size : {8.0, 10.0, 12.0}) {
		draw_glyphs(builder, font, glyphs, 8.0, y, size);
		y -= 24.0;
	}
	return finished(builder);
}

TEST(PdfRasterizer, GoldenSeam)
{
	const auto font = charstring("cff-outlines.cff", CharstringFormat::Type1C);
	ASSERT_NE(font, nullptr);
	const Image image = render_page(small_text_page(font, 24u), 2.0);
	const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "recto-golden-seam.png";
	std::error_code error;
	std::filesystem::remove(scratch, error);
	EXPECT_EQ(compare_golden(scratch, image), GoldenResult::Missing);
	ASSERT_TRUE(write_png(scratch, image));
	EXPECT_EQ(compare_golden(scratch, image), GoldenResult::Match);
	// One glyph painted out of the render fails it.
	Image altered = image;
	for (uint32_t y = 0; y < altered.height; ++y) {
		for (uint32_t x = 100u; x < 124u; ++x) {
			uint8_t* p = altered.bytes.data() + (size_t{y} * altered.width + x) * 4u;
			p[0] = 255;
			p[1] = 255;
			p[2] = 255;
		}
	}
	EXPECT_EQ(compare_golden(scratch, altered), GoldenResult::Different);
	TileSpec other;
	other.tile_width_px = 10u;
	other.tile_height_px = 10u;
	DisplayListBuilder empty(kPage100);
	EXPECT_EQ(compare_golden(scratch, render(finished(empty), other)), GoldenResult::WrongSize);
	std::filesystem::remove(scratch, error);
}

TEST(PdfRasterizer, OutlineOnlyGlyphRuns)
{
	// The same `glyf` program through DirectWrite and through its own outlines.
	const auto directwrite = truetype("truetype-full-short-loca.ttf");
	const auto outline_only = truetype("truetype-subset-short-loca.ttf");
	ASSERT_NE(directwrite, nullptr);
	ASSERT_NE(outline_only, nullptr);
	ASSERT_EQ(outline_only->kind(), LoadedFontKind::OutlineOnly);
	const std::array<uint16_t, 4> glyphs{1u, 3u, 4u, 5u};
	const auto page = [&](const std::shared_ptr<const LoadedFont>& font) {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 300.0, 80.0});
		draw_glyphs(builder, font, glyphs, 10.0, 20.0, 40.0);
		return render_page(finished(builder), 2.0);
	};
	const Image through_directwrite = page(directwrite);
	const Image through_outlines = page(outline_only);
	EXPECT_GT(ink_of(through_outlines).count, 500u);
	const Similarity measured = similarity(through_outlines, through_directwrite);
	EXPECT_TRUE(similar(measured)) << "SSIM over the ink " << measured.ink << ", worst window " << measured.worst;
	// A Type 1 run has no DirectWrite rendering; its own golden holds it.
	const auto type1 = charstring("type1-outlines.pfa", CharstringFormat::Type1);
	ASSERT_NE(type1, nullptr);
	expect_golden("outline-only-type1", render_page(small_text_page(type1, 32u), 2.0));
}

TEST(PdfRasterizer, CharstringSmallText)
{
	struct Fixture {
		std::string_view file;
		CharstringFormat format;
		std::string_view name;
	};
	const std::array<Fixture, 3> fixtures{{
		{"type1-outlines.pfa", CharstringFormat::Type1, "type1"},
		{"cff-outlines.cff", CharstringFormat::Type1C, "cff-name-keyed"},
		{"cff-cid.cff", CharstringFormat::CIDFontType0C, "cff-cid"},
	}};
	for (const Fixture& fixture : fixtures) {
		SCOPED_TRACE(fixture.name);
		const auto font = charstring(fixture.file, fixture.format);
		ASSERT_NE(font, nullptr);
		const auto list = small_text_page(font, 32u);
		for (const double scale : {1.0, 2.0}) {
			const Image image = render_page(list, scale);
			EXPECT_GT(ink_of(image).count, 0u);
			expect_golden("charstring-" + std::string(fixture.name) + (scale == 1.0 ? "-100" : "-200"), image);
		}
		// No glyph missing: every glyph with an outline has ink inside its own box on the largest line.
		const Image image = render_page(list, 2.0);
		for (const DisplayCommand& command : list->commands()) {
			const auto* run = command.as<DrawGlyphRunCommand>();
			if (run == nullptr || run->font_size != 12.0) {
				continue;
			}
			const auto glyphs = list->glyphs(*run);
			ASSERT_TRUE(glyphs.has_value());
			for (const PositionedGlyph& glyph : *glyphs) {
				if (glyph.is_blank || !glyph.ink_bounds_pt.has_value()) {
					continue;
				}
				const PdfRect box = transform_rect(concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, glyph.origin_pt.x, glyph.origin_pt.y}, run->text_matrix), *glyph.ink_bounds_pt);
				if (box.x_max >= 396.0) {
					continue; // past the page's right edge
				}
				bool ink = false;
				for (uint32_t y = static_cast<uint32_t>((90.0 - box.y_max) * 2.0); y <= static_cast<uint32_t>((90.0 - box.y_min) * 2.0) && y < image.height; ++y) {
					for (uint32_t x = static_cast<uint32_t>(box.x_min * 2.0); x <= static_cast<uint32_t>(box.x_max * 2.0); ++x) {
						ink = ink || image.at(x, y).g < 200;
					}
				}
				EXPECT_TRUE(ink) << "glyph " << glyph.glyph_index;
			}
		}
	}
}

TEST(PdfRasterizer, Type0Text)
{
	for (const std::string_view name : {std::string_view("type0-cid0"), std::string_view("type0-cid2"), std::string_view("type0-vertical")}) {
		SCOPED_TRACE(name);
		clear_diagnostics();
		const auto pages = corpus_pages("text/" + std::string(name) + ".pdf");
		ASSERT_EQ(pages.size(), 1u);
		// Every glyph present, each at the advance its font's /W or /W2 gives, with ink at its place.
		size_t glyph_count = 0;
		const Image image = render_page(pages[0], 2.0);
		for (const DisplayCommand& command : pages[0]->commands()) {
			const auto* run = command.as<DrawGlyphRunCommand>();
			if (run == nullptr) {
				continue;
			}
			const auto glyphs = pages[0]->glyphs(*run);
			ASSERT_TRUE(glyphs.has_value());
			EXPECT_NE(run->font_handle, nullptr) << "a glyph with no face would be positioned and not drawn";
			for (const PositionedGlyph& glyph : *glyphs) {
				++glyph_count;
				ASSERT_FALSE(glyph.is_blank) << "glyph " << glyph.glyph_index;
				ASSERT_TRUE(glyph.ink_bounds_pt.has_value());
				const PdfRect box = transform_rect(concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, glyph.origin_pt.x, glyph.origin_pt.y}, concat_matrix(run->text_matrix, run->ctm)), *glyph.ink_bounds_pt);
				bool ink = false;
				for (uint32_t y = static_cast<uint32_t>((200.0 - box.y_max) * 2.0); y <= static_cast<uint32_t>((200.0 - box.y_min) * 2.0); ++y) {
					for (uint32_t x = static_cast<uint32_t>(box.x_min * 2.0); x <= static_cast<uint32_t>(box.x_max * 2.0); ++x) {
						ink = ink || image.at(x, y).g < 200;
					}
				}
				EXPECT_TRUE(ink) << "glyph " << glyph.glyph_index;
			}
			if (name == "type0-cid0" && run->font_size == 36.0) {
				// /W: CID 100 is 700 wide, 101 is 800, 102 is 900, 500 is 600; so origins at 0, 25.2, 54 and 86.4.
				ASSERT_EQ(glyphs->size(), 4u);
				EXPECT_NEAR((*glyphs)[1].origin_pt.x, 25.2, 1e-6);
				EXPECT_NEAR((*glyphs)[2].origin_pt.x, 54.0, 1e-6);
				EXPECT_NEAR((*glyphs)[3].origin_pt.x, 86.4, 1e-6);
			}
			if (name == "type0-vertical") {
				// /DW2 [880 -1000]: a down step of 24 points; /W2 gives glyph 3 -1500, 36 points, and its own vector (300, 880).
				ASSERT_EQ(glyphs->size(), 4u);
				const double vy = -0.88 * 24.0;
				EXPECT_NEAR((*glyphs)[0].origin_pt.y, vy, 1e-6);
				EXPECT_NEAR((*glyphs)[1].origin_pt.y, -24.0 + vy, 1e-6);
				EXPECT_NEAR((*glyphs)[1].origin_pt.x, -0.3 * 24.0, 1e-6);
				EXPECT_NEAR((*glyphs)[2].origin_pt.y, -24.0 - 36.0 + vy, 1e-6);
			}
		}
		EXPECT_EQ(glyph_count, name == "type0-vertical" ? 4u : 8u);
		expect_golden(std::string(name) + "-100", render_page(pages[0], 1.0));
		expect_golden(std::string(name) + "-200", image);
	}
}

} // namespace
} // namespace recto::pdf
