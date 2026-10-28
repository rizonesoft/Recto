// Unit tests for `recto::pdf::DisplayList` and `recto::pdf::DisplayListBuilder`
// (D01 T02 section 1).
//
// Bounds contract under test (the contract a bounding-box rendering device
// keeps): `content_bounds_pt` contains every painted point and is
// the union of each painted primitive's exact page-space box, intersected
// with the bounding boxes of the active clips and with `page_box_pt`. It is
// tight for unclipped primitives and under rectangular clips, and a superset
// under curved or non-rectangular clips and dashes.
#include "display_list.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <latch>
#include <limits>
#include <memory>
#include <numbers>
#include <thread>
#include <type_traits>
#include <vector>

namespace recto::pdf {
namespace {

static_assert(!std::is_copy_constructible_v<DisplayList>);
static_assert(!std::is_move_constructible_v<DisplayList>);
static_assert(!std::is_copy_assignable_v<DisplayList>);
static_assert(!std::is_move_assignable_v<DisplayList>);
static_assert(!std::is_default_constructible_v<DecodedBitmap>);
static_assert(!std::is_copy_constructible_v<DecodedBitmap>);
static_assert(!std::is_move_constructible_v<DecodedBitmap>);
static_assert(!std::is_constructible_v<
	DecodedBitmap,
	uint32_t,
	uint32_t,
	uint32_t,
	std::vector<uint8_t>,
	bool>);

void expect_rect_near(const PdfRect& actual, const PdfRect& expected, double tolerance = 1e-9)
{
	EXPECT_NEAR(actual.x_min, expected.x_min, tolerance);
	EXPECT_NEAR(actual.y_min, expected.y_min, tolerance);
	EXPECT_NEAR(actual.x_max, expected.x_max, tolerance);
	EXPECT_NEAR(actual.y_max, expected.y_max, tolerance);
}

// The never-cut-ink check: `outer` contains every point of `inner`.
void expect_contains(const PdfRect& outer, const PdfRect& inner)
{
	EXPECT_LE(outer.x_min, inner.x_min);
	EXPECT_LE(outer.y_min, inner.y_min);
	EXPECT_GE(outer.x_max, inner.x_max);
	EXPECT_GE(outer.y_max, inner.y_max);
}

// Builds a BGRA bitmap whose pixel alphas are `alphas` in row-major order
// (row 0 first), every colour channel set to the alpha (premultiplied white).
std::shared_ptr<const DecodedBitmap> make_bitmap(
	uint32_t width,
	uint32_t height,
	std::initializer_list<uint8_t> alphas)
{
	std::vector<uint8_t> px;
	px.reserve(alphas.size() * 4u);
	for (const uint8_t a : alphas) {
		px.insert(px.end(), {a, a, a, a});
	}
	auto res = DecodedBitmap::create(width, height, width * 4u, std::move(px));
	EXPECT_TRUE(res.has_value());
	return res.has_value() ? *res : nullptr;
}

std::shared_ptr<const DisplayList> finish_ok(DisplayListBuilder& builder)
{
	auto res = builder.finish();
	EXPECT_TRUE(res.has_value()) << (res.has_value() ? "" : res.error().message.view());
	return res.has_value() ? *res : nullptr;
}

// Strokes `pb` once under `ctm` and returns the list's content bounds.
PdfRect stroke_bounds(
	const PathBuilder& pb,
	float width,
	LineCap cap,
	LineJoin join,
	float miter_limit,
	const PdfMatrix& ctm = PdfMatrix::identity(),
	std::span<const float> dashes = {})
{
	DisplayListBuilder builder(PdfRect{-1000.0, -1000.0, 1000.0, 1000.0});
	const auto pid = builder.add_path(pb);
	EXPECT_TRUE(pid.has_value());
	EXPECT_TRUE(builder.concat_matrix(ctm).has_value());
	EXPECT_TRUE(builder.set_stroke_style(width, cap, join, miter_limit, dashes).has_value());
	EXPECT_TRUE(builder.draw_path(*pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
	const auto dl = finish_ok(builder);
	EXPECT_TRUE(dl != nullptr && dl->has_content_bounds());
	return dl != nullptr ? dl->content_bounds_pt() : PdfRect{};
}

TEST(PdfDisplayList, CommandRecordingAndContiguousStorage)
{
	EXPECT_EQ(debug_live_display_lists(), 0);

	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
	ASSERT_TRUE(builder.reserve_commands(16u).has_value());

	PathBuilder pb1;
	pb1.rect(50.0, 60.0, 100.0, 80.0);
	const auto path0 = builder.add_path(pb1);
	ASSERT_TRUE(path0.has_value());
	EXPECT_EQ(*path0, 0u);

	PathBuilder pb2;
	pb2.move_to(10.0, 20.0);
	pb2.line_to(40.0, 60.0);
	pb2.close_subpath();
	const auto path1 = builder.add_path(pb2);
	ASSERT_TRUE(path1.has_value());
	EXPECT_EQ(*path1, 1u);

	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 10.0, 20.0}).has_value());
	const float dashes[] = {4.0f, 2.0f};
	ASSERT_TRUE(builder.set_stroke_style(
		2.5f,
		LineCap::Round,
		LineJoin::Bevel,
		8.0f,
		dashes,
		1.0f).has_value());
	ASSERT_TRUE(builder.set_alpha_and_blend(0.8f, 0.5f, BlendMode::Multiply).has_value());
	ASSERT_TRUE(builder.push_clip_path(*path0, FillRule::EvenOdd).has_value());
	ASSERT_TRUE(builder.draw_path(
		*path1,
		ColorRgba{0.1f, 0.2f, 0.3f, 1.0f},
		ColorRgba{0.4f, 0.5f, 0.6f, 1.0f},
		true,
		true,
		FillRule::NonZero).has_value());

	const PositionedGlyph run_glyphs[] = {
		PositionedGlyph{42u, U'A', PdfPoint{100.0, 200.0}, 7.5, PdfRect{0.0, 0.0, 7.5, 14.0}},
		PositionedGlyph{43u, U'B', PdfPoint{107.5, 200.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 14.0}},
	};
	ASSERT_TRUE(builder.draw_glyph_run(
		run_glyphs,
		14.0,
		TextRenderMode::Fill,
		ColorRgba::black(),
		ColorRgba::white()).has_value());

	std::vector<uint8_t> pixels(16u, 0xFFu);
	auto bmp_res = DecodedBitmap::create(2u, 2u, 8u, std::move(pixels), false);
	ASSERT_TRUE(bmp_res.has_value()) << bmp_res.error().message.view();
	ASSERT_TRUE(builder.draw_image(
		*bmp_res,
		PdfMatrix{32.0, 0.0, 0.0, 24.0, 70.0, 80.0},
		true).has_value());
	ASSERT_TRUE(builder.restore_state().has_value());

	auto dl_res = builder.finish();
	ASSERT_TRUE(dl_res.has_value()) << dl_res.error().message.view();
	const auto& dl = *dl_res;
	ASSERT_NE(dl, nullptr);
	EXPECT_TRUE(builder.is_finished());
	EXPECT_EQ(debug_live_display_lists(), 1);

	// Subsequent mutations or repeated `finish()` calls after sealing fail with
	// `ErrorCode::InternalError` and do not mutate `dl`:
	EXPECT_FALSE(builder.save_state().has_value());
	EXPECT_FALSE(builder.restore_state().has_value());
	const auto dl_again = builder.finish();
	ASSERT_FALSE(dl_again.has_value());
	EXPECT_EQ(dl_again.error().code, ErrorCode::InternalError);
	EXPECT_EQ(debug_live_display_lists(), 1);

	ASSERT_EQ(dl->command_count(), 9u);
	EXPECT_EQ(dl->command_bounds().size(), dl->command_count());
	EXPECT_EQ(dl->commands()[0].kind(), DisplayCommandKind::SaveState);
	EXPECT_EQ(dl->commands()[1].kind(), DisplayCommandKind::ConcatMatrix);
	EXPECT_EQ(dl->commands()[2].kind(), DisplayCommandKind::SetStrokeStyle);
	EXPECT_EQ(dl->commands()[3].kind(), DisplayCommandKind::SetAlphaAndBlend);
	EXPECT_EQ(dl->commands()[4].kind(), DisplayCommandKind::PushClipPath);
	EXPECT_EQ(dl->commands()[5].kind(), DisplayCommandKind::DrawPath);
	EXPECT_EQ(dl->commands()[6].kind(), DisplayCommandKind::DrawGlyphRun);
	EXPECT_EQ(dl->commands()[7].kind(), DisplayCommandKind::DrawImage);
	EXPECT_EQ(dl->commands()[8].kind(), DisplayCommandKind::RestoreState);

	const auto* stroke_cmd = dl->commands()[2].as<SetStrokeStyleCommand>();
	ASSERT_NE(stroke_cmd, nullptr);
	EXPECT_FLOAT_EQ(stroke_cmd->line_width, 2.5f);
	EXPECT_EQ(stroke_cmd->line_cap, LineCap::Round);
	EXPECT_EQ(stroke_cmd->line_join, LineJoin::Bevel);
	const auto dash_span = dl->dash_values(*stroke_cmd);
	ASSERT_TRUE(dash_span.has_value());
	ASSERT_EQ(dash_span->size(), 2u);
	EXPECT_FLOAT_EQ((*dash_span)[0], 4.0f);
	EXPECT_FLOAT_EQ((*dash_span)[1], 2.0f);

	const auto* glyph_cmd = dl->commands()[6].as<DrawGlyphRunCommand>();
	ASSERT_NE(glyph_cmd, nullptr);
	const auto glyph_span = dl->glyphs(*glyph_cmd);
	ASSERT_TRUE(glyph_span.has_value());
	ASSERT_EQ(glyph_span->size(), 2u);
	EXPECT_EQ((*glyph_span)[0].glyph_index, 42u);
	EXPECT_EQ((*glyph_span)[0].unicode_codepoint, static_cast<uint32_t>(U'A'));
	EXPECT_EQ((*glyph_span)[1].glyph_index, 43u);
	EXPECT_EQ((*glyph_span)[1].unicode_codepoint, static_cast<uint32_t>(U'B'));
}

TEST(PdfDisplayList, PathRecording)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

	// Path 0: two disjoint subpaths (`MoveTo` + `LineTo` + `CloseSubpath` and `Rect`):
	PathBuilder pb0;
	pb0.move_to(10.0, 15.0);
	pb0.line_to(50.0, 15.0);
	pb0.line_to(30.0, 45.0);
	pb0.close_subpath();
	pb0.rect(100.0, 200.0, -20.0, 30.0); // negative width normalizes to [80, 100]
	const auto idx0 = builder.add_path(pb0);
	ASSERT_TRUE(idx0.has_value());

	// Path 1: cubic Bezier arch from (0, 0) to (100, 0) with control points
	// (0, 100) and (100, 100). Analytic peak at t = 0.5 is y = 75.0 (strictly
	// tighter than the control-point hull y = 100.0):
	PathBuilder pb1;
	pb1.move_to(0.0, 0.0);
	pb1.cubic_bezier_to(PdfPoint{0.0, 100.0}, PdfPoint{100.0, 100.0}, PdfPoint{100.0, 0.0});
	const auto idx1 = builder.add_path(pb1);
	ASSERT_TRUE(idx1.has_value());

	const auto dl_res = builder.finish();
	ASSERT_TRUE(dl_res.has_value());
	const auto& dl = *dl_res;
	ASSERT_EQ(dl->paths().size(), 2u);

	const auto p0_res = dl->path(*idx0);
	ASSERT_TRUE(p0_res.has_value());
	const RecordedPath& p0 = *p0_res;
	EXPECT_TRUE(p0.has_bounds);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.x_min, 10.0);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.y_min, 15.0);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.x_max, 100.0);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.y_max, 230.0);
	const auto p0_verbs = dl->path_verbs(p0);
	const auto p0_points = dl->path_points(p0);
	ASSERT_TRUE(p0_verbs.has_value());
	ASSERT_TRUE(p0_points.has_value());
	EXPECT_EQ(p0_verbs->size(), 5u);
	EXPECT_EQ(p0_points->size(), 5u);

	const auto p1_res = dl->path(*idx1);
	ASSERT_TRUE(p1_res.has_value());
	const RecordedPath& p1 = *p1_res;
	EXPECT_TRUE(p1.has_bounds);
	EXPECT_DOUBLE_EQ(p1.bounds_pt.x_min, 0.0);
	EXPECT_DOUBLE_EQ(p1.bounds_pt.y_min, 0.0);
	EXPECT_DOUBLE_EQ(p1.bounds_pt.x_max, 100.0);
	EXPECT_NEAR(p1.bounds_pt.y_max, 75.0, 1e-9);
	const auto p1_verbs = dl->path_verbs(p1);
	const auto p1_points = dl->path_points(p1);
	ASSERT_TRUE(p1_verbs.has_value());
	ASSERT_TRUE(p1_points.has_value());
	EXPECT_EQ(p1_verbs->size(), 2u);
	EXPECT_EQ(p1_points->size(), 4u);

	// Shared pools hold the concatenated verbs and points of both paths:
	EXPECT_EQ(dl->path_verbs().size(), 7u);
	EXPECT_EQ(dl->path_points().size(), 9u);
}

TEST(PdfDisplayList, DecodedBitmapZeroCopySharing)
{
	// Invalid dimensions, strides, or buffer sizes are rejected with `ErrorCode::DamagedFormat`:
	EXPECT_FALSE(DecodedBitmap::create(0u, 10u, 40u, std::vector<uint8_t>(400u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(10u, 0u, 40u, std::vector<uint8_t>(400u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(65537u, 1u, 65537u * 4u, std::vector<uint8_t>(65537u * 4u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(1u, 65537u, 4u, std::vector<uint8_t>(65537u * 4u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(10u, 10u, 36u, std::vector<uint8_t>(360u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(10u, 10u, 40u, std::vector<uint8_t>(399u)).has_value());

	std::vector<uint8_t> raw_pixels(64u * 4u, 0xABu);
	const uint8_t* original_ptr = raw_pixels.data();
	auto bmp_res = DecodedBitmap::create(8u, 8u, 32u, std::move(raw_pixels), true);
	ASSERT_TRUE(bmp_res.has_value()) << bmp_res.error().message.view();
	const auto& bmp = *bmp_res;
	EXPECT_EQ(bmp->span().data(), original_ptr);
	EXPECT_TRUE(bmp->is_monochrome_mask);
	EXPECT_TRUE(bmp->has_non_transparent_pixels);
	ASSERT_TRUE(bmp->opaque_pixel_box.has_value());
	EXPECT_EQ(*bmp->opaque_pixel_box, (PixelBox{0u, 0u, 8u, 8u}));

	DisplayListBuilder builder;
	ASSERT_TRUE(builder.draw_image(bmp, PdfMatrix{100.0, 0.0, 0.0, 100.0, 10.0, 20.0}).has_value());
	ASSERT_TRUE(builder.draw_image(bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 200.0, 300.0}).has_value());
	auto dl_res = builder.finish();
	ASSERT_TRUE(dl_res.has_value());
	const auto& dl = *dl_res;

	ASSERT_EQ(dl->command_count(), 2u);
	const auto* img0 = dl->commands()[0].as<DrawImageCommand>();
	const auto* img1 = dl->commands()[1].as<DrawImageCommand>();
	ASSERT_NE(img0, nullptr);
	ASSERT_NE(img1, nullptr);
	EXPECT_EQ(img0->bitmap.get(), bmp.get());
	EXPECT_EQ(img1->bitmap.get(), bmp.get());
	EXPECT_EQ(img0->bitmap->span().data(), original_ptr);
	EXPECT_EQ(img1->bitmap->span().data(), original_ptr);
}

TEST(PdfDisplayList, ContentBounds)
{
	// 1. Mixed page: an unpainted path, a path under a rectangular clip, an
	//    image, an invisible run and a visible run.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

		// A non-stroked, non-filled path (`n` operator) does not contribute:
		PathBuilder ignored_pb;
		ignored_pb.rect(5.0, 5.0, 500.0, 700.0);
		const auto ignored_path = builder.add_path(ignored_pb);
		ASSERT_TRUE(ignored_path.has_value());
		ASSERT_TRUE(builder.draw_path(
			*ignored_path,
			ColorRgba::black(),
			ColorRgba::black(),
			false,
			false).has_value());

		// A path under a rectangular clip inside a saved state is clipped exactly:
		ASSERT_TRUE(builder.save_state().has_value());
		PathBuilder clip_pb;
		clip_pb.rect(50.0, 60.0, 200.0, 200.0); // clip window [50, 60, 250, 260]
		const auto clip_path = builder.add_path(clip_pb);
		ASSERT_TRUE(clip_path.has_value());
		ASSERT_TRUE(builder.push_clip_path(*clip_path).has_value());

		PathBuilder wide_pb;
		wide_pb.rect(20.0, 40.0, 300.0, 100.0); // [20, 40, 320, 140] -> clipped [50, 60, 250, 140]
		const auto wide_path = builder.add_path(wide_pb);
		ASSERT_TRUE(wide_path.has_value());
		ASSERT_TRUE(builder.draw_path(
			*wide_path,
			ColorRgba::black(),
			ColorRgba::black(),
			false,
			true).has_value());
		ASSERT_TRUE(builder.restore_state().has_value());

		// A transformed image outside the restored clip:
		std::vector<uint8_t> px(16u, 0xFFu);
		auto bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(px));
		ASSERT_TRUE(builder.save_state().has_value());
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 300.0, 400.0}).has_value());
		ASSERT_TRUE(builder.draw_image(bmp, PdfMatrix{80.0, 0.0, 0.0, 50.0, 0.0, 0.0}).has_value()); // [300, 400, 380, 450]
		ASSERT_TRUE(builder.restore_state().has_value());

		// An invisible (`Tr 3`) run does not expand content bounds; a visible
		// (`Tr 0`) run with explicit glyph ink bounds does:
		const PositionedGlyph inv_glyphs[] = {
			PositionedGlyph{1u, U'X', PdfPoint{10.0, 10.0}, 20.0, PdfRect{0.0, 0.0, 20.0, 24.0}},
		};
		ASSERT_TRUE(builder.draw_glyph_run(
			inv_glyphs,
			24.0,
			TextRenderMode::Invisible,
			ColorRgba::black(),
			ColorRgba::black()).has_value());

		const PositionedGlyph vis_glyphs[] = {
			PositionedGlyph{2u, U'H', PdfPoint{100.0, 500.0}, 15.0, PdfRect{0.0, 0.0, 15.0, 20.0}},
		};
		ASSERT_TRUE(builder.draw_glyph_run(
			vis_glyphs,
			20.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black()).has_value()); // [100, 500, 115, 520]

		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{50.0, 60.0, 380.0, 520.0}));
	}

	// 2. Unclipped fills: a rotated cubic arch's box comes from the extrema of
	//    the transformed curve, not from the transformed local box.
	{
		constexpr double kC = std::numbers::sqrt2 / 2.0;
		const PdfMatrix rot45{kC, kC, -kC, kC, 300.0, 0.0};
		DisplayListBuilder builder(PdfRect{-1000.0, -1000.0, 1000.0, 1000.0});
		PathBuilder arch;
		arch.move_to(100.0, 100.0);
		arch.cubic_bezier_to(PdfPoint{100.0, 200.0}, PdfPoint{200.0, 200.0}, PdfPoint{200.0, 100.0});
		arch.close_subpath();
		const uint32_t pid = *builder.add_path(arch);
		ASSERT_TRUE(builder.concat_matrix(rot45).has_value());
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());

		// Dense sampling of the transformed curve gives the true box to well
		// within the tolerance (the closing line's ends are the curve's ends).
		PdfRect sampled{};
		bool first = true;
		constexpr int kSamples = 200000;
		for (int i = 0; i <= kSamples; ++i) {
			const double t = static_cast<double>(i) / kSamples;
			const double u = 1.0 - t;
			const PdfPoint user{
				u * u * u * 100.0 + 3.0 * u * u * t * 100.0 + 3.0 * u * t * t * 200.0 + t * t * t * 200.0,
				u * u * u * 100.0 + 3.0 * u * u * t * 200.0 + 3.0 * u * t * t * 200.0 + t * t * t * 100.0};
			const PdfPoint q = transform_point(rot45, user);
			sampled = first ? PdfRect{q.x, q.y, q.x, q.y} : union_rect(sampled, PdfRect{q.x, q.y, q.x, q.y});
			first = false;
		}
		expect_rect_near(dl->content_bounds_pt(), sampled, 1e-6);
		const PdfRect local_box_transformed = transform_rect(rot45, PdfRect{100.0, 100.0, 200.0, 175.0});
		EXPECT_GT(dl->content_bounds_pt().x_min, local_box_transformed.x_min + 1.0);
	}

	// 3. Strokes, exact in user space and mapped exactly by the CTM. Width 20
	//    (`r = 10`) unless stated.
	{
		PathBuilder horizontal;
		horizontal.move_to(100.0, 100.0);
		horizontal.line_to(200.0, 100.0);
		expect_rect_near(
			stroke_bounds(horizontal, 20.0f, LineCap::Butt, LineJoin::Bevel, 10.0f),
			PdfRect{100.0, 90.0, 200.0, 110.0});
		expect_rect_near(
			stroke_bounds(horizontal, 20.0f, LineCap::Round, LineJoin::Bevel, 10.0f),
			PdfRect{90.0, 90.0, 210.0, 110.0});
		expect_rect_near(
			stroke_bounds(horizontal, 20.0f, LineCap::ProjectingSquare, LineJoin::Bevel, 10.0f),
			PdfRect{90.0, 90.0, 210.0, 110.0});

		// A diagonal tells the three caps apart: butt reaches `r / sqrt(2)`
		// past the ends in x and y, round `r`, projecting `r * sqrt(2)`.
		PathBuilder diagonal;
		diagonal.move_to(100.0, 100.0);
		diagonal.line_to(200.0, 200.0);
		const double s = 10.0 / std::numbers::sqrt2;
		expect_rect_near(
			stroke_bounds(diagonal, 20.0f, LineCap::Butt, LineJoin::Miter, 10.0f),
			PdfRect{100.0 - s, 100.0 - s, 200.0 + s, 200.0 + s});
		expect_rect_near(
			stroke_bounds(diagonal, 20.0f, LineCap::Round, LineJoin::Miter, 10.0f),
			PdfRect{90.0, 90.0, 210.0, 210.0});
		expect_rect_near(
			stroke_bounds(diagonal, 20.0f, LineCap::ProjectingSquare, LineJoin::Miter, 10.0f),
			PdfRect{100.0 - 2.0 * s, 100.0 - 2.0 * s, 200.0 + 2.0 * s, 200.0 + 2.0 * s});

		// Joins on the V `(100, 90) -> (200, 100) -> (100, 110)`: the miter
		// ratio is `sqrt(101) ~= 10.05`, its apex `200 + sqrt(10100)` in x.
		PathBuilder vee;
		vee.move_to(100.0, 90.0);
		vee.line_to(200.0, 100.0);
		vee.line_to(100.0, 110.0);
		const double len = std::sqrt(10100.0);
		const double body_x = 100.0 / len; // r * |n.x|
		const double body_y = 1000.0 / len; // r * |n.y|
		const PdfRect bevel_box{100.0 - body_x, 90.0 - body_y, 200.0 + body_x, 110.0 + body_y};
		expect_rect_near(
			stroke_bounds(vee, 20.0f, LineCap::Butt, LineJoin::Bevel, 10.0f),
			bevel_box);
		expect_rect_near(
			stroke_bounds(vee, 20.0f, LineCap::Butt, LineJoin::Round, 10.0f),
			PdfRect{bevel_box.x_min, bevel_box.y_min, 210.0, bevel_box.y_max});
		expect_rect_near(
			stroke_bounds(vee, 20.0f, LineCap::Butt, LineJoin::Miter, 10.1f),
			PdfRect{bevel_box.x_min, bevel_box.y_min, 200.0 + len, bevel_box.y_max},
			1e-6);
		// A miter limit below the ratio falls back to bevel:
		expect_rect_near(
			stroke_bounds(vee, 20.0f, LineCap::Butt, LineJoin::Miter, 10.0f),
			bevel_box);

		// A sheared butt stroke (`x' = x + 0.5 y`), width 10:
		expect_rect_near(
			stroke_bounds(horizontal, 10.0f, LineCap::Butt, LineJoin::Miter, 10.0f, PdfMatrix{1.0, 0.0, 0.5, 1.0, 0.0, 0.0}),
			PdfRect{147.5, 95.0, 252.5, 105.0});

		// A rotated, scaled round-capped stroke (`(x, y) -> (400 - 2y, 2x)`), width 10:
		PathBuilder short_line;
		short_line.move_to(10.0, 10.0);
		short_line.line_to(110.0, 10.0);
		expect_rect_near(
			stroke_bounds(short_line, 10.0f, LineCap::Round, LineJoin::Round, 10.0f, PdfMatrix{0.0, 2.0, -2.0, 0.0, 400.0, 0.0}),
			PdfRect{370.0, 10.0, 390.0, 230.0});

		// Non-uniform scaling `scale(3, 5)` of a round cap expands x by `3 r` and y by `5 r`:
		PathBuilder scaled;
		scaled.move_to(50.0, 40.0);
		scaled.line_to(100.0, 40.0);
		expect_rect_near(
			stroke_bounds(scaled, 10.0f, LineCap::Round, LineJoin::Round, 10.0f, PdfMatrix{3.0, 0.0, 0.0, 5.0, 0.0, 0.0}),
			PdfRect{135.0, 175.0, 315.0, 225.0});

		// A round-capped cubic arch, width 10: curve box `[100, 100, 200, 175]` grown by 5.
		PathBuilder arch;
		arch.move_to(100.0, 100.0);
		arch.cubic_bezier_to(PdfPoint{100.0, 200.0}, PdfPoint{200.0, 200.0}, PdfPoint{200.0, 100.0});
		expect_rect_near(
			stroke_bounds(arch, 10.0f, LineCap::Round, LineJoin::Round, 10.0f),
			PdfRect{95.0, 95.0, 205.0, 180.0});

		// A hairline (width 0) is the path's own box:
		expect_rect_near(
			stroke_bounds(diagonal, 0.0f, LineCap::ProjectingSquare, LineJoin::Miter, 10.0f),
			PdfRect{100.0, 100.0, 200.0, 200.0});
	}

	// 4. Glyph runs: the ink box through the text matrix and the CTM, and a
	//    stroked run widened in user space.
	{
		const PositionedGlyph glyphs[] = {
			PositionedGlyph{5u, U'g', PdfPoint{0.0, 0.0}, 8.0, PdfRect{0.0, -2.0, 8.0, 10.0}},
		};
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{2.0, 0.0, 0.0, 2.0, 0.0, 0.0}).has_value());
		ASSERT_TRUE(builder.draw_glyph_run(
			glyphs,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black(),
			PdfMatrix{1.0, 0.0, 0.0, 1.0, 50.0, 100.0}).has_value());
		ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Round, 10.0f).has_value());
		ASSERT_TRUE(builder.draw_glyph_run(
			glyphs,
			12.0,
			TextRenderMode::Stroke,
			ColorRgba::black(),
			ColorRgba::black(),
			PdfMatrix{1.0, 0.0, 0.0, 1.0, 150.0, 100.0}).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_EQ(dl->command_bounds().size(), 4u);
		EXPECT_EQ(dl->command_bounds()[1].bounds_pt, (PdfRect{100.0, 196.0, 116.0, 220.0}));
		EXPECT_EQ(dl->command_bounds()[3].bounds_pt, (PdfRect{298.0, 194.0, 318.0, 222.0}));
	}
}

TEST(PdfDisplayList, NonRectangularClipsUseTheirBoxes)
{
	// The contract narrows bounds by a clip's exact bounding box only, so a
	// non-rectangular or even-odd clip yields a superset of the painted ink
	// (the consumer's padding absorbs it). Each case states the box it promises.

	// 1. Triangle clip `x + y <= 100`: a square inside the triangle's box but
	//    outside the triangle keeps its own box.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder tri_pb;
		tri_pb.move_to(0.0, 0.0);
		tri_pb.line_to(100.0, 0.0);
		tri_pb.line_to(0.0, 100.0);
		tri_pb.close_subpath();
		const uint32_t tri_clip = *builder.add_path(tri_pb);
		ASSERT_TRUE(builder.push_clip_path(tri_clip, FillRule::NonZero).has_value());

		PathBuilder outside_pb;
		outside_pb.rect(80.0, 80.0, 40.0, 40.0);
		const uint32_t outside_rect = *builder.add_path(outside_pb);
		ASSERT_TRUE(builder.draw_path(outside_rect, ColorRgba::black(), ColorRgba::black(), false, true).has_value());

		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{80.0, 80.0, 100.0, 100.0}));
	}

	// 2. An even-odd ring clip and a non-zero one give the same box for a
	//    square drawn in the hole.
	{
		PathBuilder ring_pb;
		ring_pb.rect(0.0, 0.0, 100.0, 100.0);
		ring_pb.rect(20.0, 20.0, 60.0, 60.0);

		PathBuilder inner_pb;
		inner_pb.rect(30.0, 30.0, 40.0, 40.0);

		for (const FillRule rule : {FillRule::EvenOdd, FillRule::NonZero}) {
			DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
			const uint32_t clip = *builder.add_path(ring_pb);
			const uint32_t draw = *builder.add_path(inner_pb);
			ASSERT_TRUE(builder.push_clip_path(clip, rule).has_value());
			ASSERT_TRUE(builder.draw_path(draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
			const auto dl = finish_ok(builder);
			ASSERT_TRUE(dl->has_content_bounds());
			EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{30.0, 30.0, 70.0, 70.0}));
		}
	}

	// 3. Nested clips intersect their boxes: triangle `[0, 0, 100, 100]` and
	//    band `[0, 60, 100, 100]` give `[0, 60, 100, 100]`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder tri_pb;
		tri_pb.move_to(0.0, 0.0);
		tri_pb.line_to(100.0, 0.0);
		tri_pb.line_to(0.0, 100.0);
		tri_pb.close_subpath();
		const uint32_t c1 = *builder.add_path(tri_pb);

		PathBuilder band_pb;
		band_pb.rect(0.0, 60.0, 100.0, 40.0);
		const uint32_t c2 = *builder.add_path(band_pb);

		PathBuilder full_pb;
		full_pb.rect(0.0, 0.0, 150.0, 150.0);
		const uint32_t draw_id = *builder.add_path(full_pb);

		ASSERT_TRUE(builder.push_clip_path(c1, FillRule::NonZero).has_value());
		ASSERT_TRUE(builder.push_clip_path(c2, FillRule::NonZero).has_value());
		ASSERT_TRUE(builder.draw_path(draw_id, ColorRgba::black(), ColorRgba::black(), false, true).has_value());

		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{0.0, 60.0, 100.0, 100.0}));
	}

	// 4. A clip box survives `restore_state()` only inside its saved state:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(0.0, 0.0, 10.0, 10.0);
		const uint32_t clip = *builder.add_path(clip_pb);
		PathBuilder draw_pb;
		draw_pb.rect(50.0, 50.0, 10.0, 10.0);
		const uint32_t draw = *builder.add_path(draw_pb);
		ASSERT_TRUE(builder.save_state().has_value());
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		ASSERT_TRUE(builder.draw_path(draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		ASSERT_TRUE(builder.restore_state().has_value());
		ASSERT_TRUE(builder.draw_path(draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_EQ(dl->command_bounds().size(), 5u);
		EXPECT_FALSE(dl->command_bounds()[2].has_bounds);
		EXPECT_TRUE(dl->command_bounds()[4].has_bounds);
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{50.0, 50.0, 60.0, 60.0}));
	}
}

TEST(PdfDisplayList, GlyphInkBoundsDescendersOverhangsAndSpaces)
{
	// 1. Space-only glyph run paints no ink (`has_content_bounds() == false`):
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
		const PositionedGlyph spaces[] = {
			PositionedGlyph::blank(3u, U' ', PdfPoint{72.0, 700.0}, 4.5),
			PositionedGlyph::blank(3u, U' ', PdfPoint{76.5, 700.0}, 4.5),
		};
		ASSERT_TRUE(builder.draw_glyph_run(
			spaces,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black()).has_value());
		const auto dl = *builder.finish();
		EXPECT_FALSE(dl->has_content_bounds());
	}

	// 2. Glyph with negative left bearing (-2.5), descender (-4.0), and right overhang (+11.0 > advance 8.0):
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
		const PositionedGlyph glyphs[] = {
			PositionedGlyph{10u, U'j', PdfPoint{100.0, 200.0}, 8.0, PdfRect{-2.5, -4.0, 11.0, 9.5}},
			PositionedGlyph::blank(3u, U' ', PdfPoint{108.0, 200.0}, 5.0),
		};
		ASSERT_TRUE(builder.draw_glyph_run(
			glyphs,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black()).has_value());
		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_min, 97.5);
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_min, 196.0);
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_max, 111.0);
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_max, 209.5);
	}

	// 3. Visible glyph run without per-glyph ink bounds, local_bounds_pt, or Type 3 sub-list is rejected:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
		const PositionedGlyph missing_bounds[] = {
			PositionedGlyph{10u, U'A', PdfPoint{100.0, 200.0}, 8.0, std::nullopt, false},
		};
		const auto res = builder.draw_glyph_run(
			missing_bounds,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black());
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat);
		EXPECT_EQ(builder.command_count(), 0u);
	}

	// 4. A Type 3 run places the sub-list's content bounds at each glyph origin:
	{
		DisplayListBuilder glyph_builder(PdfRect{0.0, 0.0, 10.0, 10.0});
		PathBuilder glyph_pb;
		glyph_pb.rect(1.0, 0.0, 6.0, 8.0);
		const uint32_t glyph_path = *glyph_builder.add_path(glyph_pb);
		ASSERT_TRUE(glyph_builder.draw_path(glyph_path, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto sub_list = finish_ok(glyph_builder);

		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
		const PositionedGlyph glyphs[] = {
			PositionedGlyph{1u, U'a', PdfPoint{100.0, 200.0}, 8.0},
			PositionedGlyph{2u, U'b', PdfPoint{108.0, 200.0}, 8.0},
		};
		ASSERT_TRUE(builder.draw_glyph_run(
			glyphs,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black(),
			PdfMatrix::identity(),
			std::nullopt,
			nullptr,
			sub_list).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{101.0, 200.0, 115.0, 208.0}));
	}
}

TEST(PdfDisplayList, InvisibleContentHasNoBounds)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

	PathBuilder pb;
	pb.rect(10.0, 10.0, 100.0, 100.0);
	const uint32_t pid = *builder.add_path(pb);
	const PositionedGlyph glyphs[] = {
		PositionedGlyph{1u, U'A', PdfPoint{50.0, 50.0}, 10.0, PdfRect{0.0, 0.0, 10.0, 12.0}},
	};
	std::vector<uint8_t> opaque_px(16u, 0xFFu);
	const auto opaque_bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(opaque_px));

	// 1. State alpha 0 for both stroke and fill, inside a saved state:
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.set_alpha_and_blend(0.0f, 0.0f, BlendMode::Normal).has_value());
	ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, true).has_value());
	ASSERT_TRUE(builder.draw_glyph_run(
		glyphs,
		12.0,
		TextRenderMode::FillAndStroke,
		ColorRgba::black(),
		ColorRgba::black()).has_value());
	ASSERT_TRUE(builder.draw_image(opaque_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 20.0, 20.0}).has_value());
	ASSERT_TRUE(builder.restore_state().has_value());

	// 2. Alpha 1 state, but colours, pixels, a soft mask or a stencil tint with alpha 0:
	ASSERT_TRUE(builder.draw_path(
		pid,
		ColorRgba{0.0f, 0.0f, 0.0f, 0.0f},
		ColorRgba{1.0f, 0.0f, 0.0f, 0.0f},
		true,
		true).has_value());
	ASSERT_TRUE(builder.draw_glyph_run(
		glyphs,
		12.0,
		TextRenderMode::Fill,
		ColorRgba{0.0f, 0.0f, 0.0f, 0.0f},
		ColorRgba::black()).has_value());
	std::vector<uint8_t> transparent_px(16u, 0x00u);
	const auto trans_bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(transparent_px));
	EXPECT_FALSE(trans_bmp->has_non_transparent_pixels);
	EXPECT_FALSE(trans_bmp->opaque_pixel_box.has_value());
	ASSERT_TRUE(builder.draw_image(trans_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 200.0, 200.0}).has_value());
	ASSERT_TRUE(builder.draw_image(opaque_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 200.0, 200.0}, true, trans_bmp).has_value());
	std::vector<uint8_t> stencil_px(16u, 0xFFu);
	const auto stencil = *DecodedBitmap::create(2u, 2u, 8u, std::move(stencil_px), true);
	ASSERT_TRUE(builder.draw_image(
		stencil,
		PdfMatrix{50.0, 0.0, 0.0, 50.0, 300.0, 300.0},
		true,
		nullptr,
		ColorRgba{0.0f, 0.0f, 0.0f, 0.0f}).has_value());

	// 3. `Tr 3` (invisible) and `Tr 7` (clip only) runs at full alpha:
	ASSERT_TRUE(builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Invisible, ColorRgba::black(), ColorRgba::black()).has_value());
	ASSERT_TRUE(builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Clip, ColorRgba::black(), ColorRgba::black()).has_value());

	// 4. Fully clipped primitives: a clip box disjoint from the draws, a clip
	//    touching the drawn square only along its edge, and a clip path that
	//    encloses no area.
	PathBuilder corner_pb;
	corner_pb.rect(500.0, 600.0, 10.0, 10.0);
	const uint32_t corner_clip = *builder.add_path(corner_pb);
	PathBuilder edge_pb;
	edge_pb.rect(110.0, 10.0, 50.0, 100.0); // shares the edge x = 110 with `pb`
	const uint32_t edge_clip = *builder.add_path(edge_pb);
	PathBuilder empty_pb;
	empty_pb.move_to(30.0, 30.0);
	const uint32_t empty_clip = *builder.add_path(empty_pb);
	for (const uint32_t clip : {corner_clip, edge_clip, empty_clip}) {
		ASSERT_TRUE(builder.save_state().has_value());
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		// Fill only: a stroke's half width would paint past the shared edge.
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		ASSERT_TRUE(builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()).has_value());
		ASSERT_TRUE(builder.draw_image(opaque_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 20.0, 20.0}).has_value());
		ASSERT_TRUE(builder.restore_state().has_value());
	}

	const auto dl = finish_ok(builder);
	EXPECT_FALSE(dl->has_content_bounds());
	EXPECT_EQ(dl->content_bounds_pt(), PdfRect{});
	ASSERT_EQ(dl->command_bounds().size(), dl->command_count());
	for (const CommandBounds& entry : dl->command_bounds()) {
		EXPECT_FALSE(entry.has_bounds);
	}

	// The same draws with alpha, mode and clip left alone do paint, so each
	// case above is excluded by its own condition:
	DisplayListBuilder control(PdfRect{0.0, 0.0, 612.0, 792.0});
	const uint32_t control_pid = *control.add_path(pb);
	ASSERT_TRUE(control.draw_path(control_pid, ColorRgba::black(), ColorRgba::black(), true, true).has_value());
	ASSERT_TRUE(control.draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()).has_value());
	ASSERT_TRUE(control.draw_image(opaque_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 200.0, 200.0}).has_value());
	ASSERT_TRUE(control.draw_image(stencil, PdfMatrix{50.0, 0.0, 0.0, 50.0, 300.0, 300.0}).has_value());
	const auto control_dl = finish_ok(control);
	for (const CommandBounds& entry : control_dl->command_bounds()) {
		EXPECT_TRUE(entry.has_bounds);
	}
}

TEST(PdfDisplayList, ConfigurableBudgetsAndStatePreservation)
{
	DisplayListLimits limits{};
	limits.max_commands = 3u;
	limits.max_paths = 2u;
	limits.max_path_verbs = 4u;
	limits.max_path_points = 5u;
	limits.max_glyphs = 2u;
	limits.max_dash_values = 2u;
	limits.max_total_bytes = 4096u;

	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);

	// Exceeding reserve_commands budget fails without mutating builder:
	const auto reserve_res = builder.reserve_commands(4u);
	ASSERT_FALSE(reserve_res.has_value());
	EXPECT_EQ(reserve_res.error().code, ErrorCode::ResourceLimit);

	PathBuilder pb1;
	pb1.rect(10.0, 20.0, 30.0, 40.0); // 1 verb, 2 points
	const auto p0 = builder.add_path(pb1);
	ASSERT_TRUE(p0.has_value());
	EXPECT_EQ(*p0, 0u);

	// Path exceeding verb/point limits is rejected while preserving path 0:
	PathBuilder huge_pb;
	huge_pb.move_to(0.0, 0.0);
	huge_pb.line_to(1.0, 1.0);
	huge_pb.line_to(2.0, 2.0);
	huge_pb.line_to(3.0, 3.0); // 4 verbs -> total 5 > max_path_verbs (4)
	const auto huge_res = builder.add_path(huge_pb);
	ASSERT_FALSE(huge_res.has_value());
	EXPECT_EQ(huge_res.error().code, ErrorCode::ResourceLimit);
	EXPECT_EQ(builder.path_count(), 1u);

	// Dash array exceeding max_dash_values (2) is rejected without adding a command:
	const float too_many_dashes[] = {1.0f, 2.0f, 3.0f};
	EXPECT_FALSE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, too_many_dashes).has_value());
	EXPECT_EQ(builder.command_count(), 0u);

	// Glyph run exceeding max_glyphs (2) is rejected without adding a command:
	const PositionedGlyph three_glyphs[] = {
		PositionedGlyph::blank(1u, U' ', PdfPoint{0.0, 0.0}, 4.0),
		PositionedGlyph::blank(1u, U' ', PdfPoint{4.0, 0.0}, 4.0),
		PositionedGlyph::blank(1u, U' ', PdfPoint{8.0, 0.0}, 4.0),
	};
	EXPECT_FALSE(builder.draw_glyph_run(
		three_glyphs,
		12.0,
		TextRenderMode::Fill,
		ColorRgba::black(),
		ColorRgba::black()).has_value());
	EXPECT_EQ(builder.command_count(), 0u);

	// Record 3 valid commands up to `max_commands`:
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.draw_path(*p0, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	ASSERT_TRUE(builder.restore_state().has_value());
	EXPECT_EQ(builder.command_count(), 3u);

	// 4th command exceeds `max_commands`:
	const auto fourth = builder.draw_path(*p0, ColorRgba::black(), ColorRgba::black(), false, true);
	ASSERT_FALSE(fourth.has_value());
	EXPECT_EQ(fourth.error().code, ErrorCode::ResourceLimit);
	EXPECT_EQ(builder.command_count(), 3u);
	EXPECT_LE(builder.retained_bytes(), limits.max_total_bytes);

	// Builder still seals cleanly with the 3 valid commands and exact bounds:
	const auto dl_res = builder.finish();
	ASSERT_TRUE(dl_res.has_value());
	EXPECT_EQ((*dl_res)->command_count(), 3u);
	ASSERT_TRUE((*dl_res)->has_content_bounds());
	EXPECT_EQ((*dl_res)->content_bounds_pt(), (PdfRect{10.0, 20.0, 40.0, 60.0}));
}

TEST(PdfDisplayList, MalformedRecordsRefused)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	const float nan_f = std::numeric_limits<float>::quiet_NaN();
	const float inf_f = std::numeric_limits<float>::infinity();

	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
	PathBuilder good_pb;
	good_pb.rect(10.0, 10.0, 20.0, 20.0);
	const uint32_t good_path = *builder.add_path(good_pb);
	const PositionedGlyph good_glyphs[] = {
		PositionedGlyph{1u, U'A', PdfPoint{10.0, 10.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 12.0}},
	};
	const auto good_bmp = make_bitmap(1u, 1u, {255u});
	const size_t paths_before = builder.path_count();

	const auto expect_refused = [&](const PdfResult<void>& res, const char* what) {
		ASSERT_FALSE(res.has_value()) << what;
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat) << what;
		EXPECT_EQ(builder.command_count(), 0u) << what;
		EXPECT_EQ(builder.path_count(), paths_before) << what;
	};
	const auto expect_path_refused = [&](const PdfResult<uint32_t>& res, const char* what) {
		ASSERT_FALSE(res.has_value()) << what;
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat) << what;
		EXPECT_EQ(builder.path_count(), paths_before) << what;
	};

	// Verb and point counts that disagree, in either direction, and bad sequencing:
	{
		const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::CubicBezierTo};
		const PdfPoint pts[] = {PdfPoint{10.0, 10.0}};
		expect_path_refused(builder.add_path(verbs, pts), "truncated cubic");
	}
	{
		const PathVerb verbs[] = {PathVerb::MoveTo};
		const PdfPoint pts[] = {PdfPoint{10.0, 10.0}, PdfPoint{20.0, 20.0}};
		expect_path_refused(builder.add_path(verbs, pts), "surplus point");
	}
	{
		const PathVerb verbs[] = {PathVerb::LineTo};
		const PdfPoint pts[] = {PdfPoint{10.0, 10.0}};
		expect_path_refused(builder.add_path(verbs, pts), "line without move");
	}
	{
		const PathVerb verbs[] = {PathVerb::CloseSubpath};
		expect_path_refused(builder.add_path(verbs, {}), "close without move");
	}
	{
		const PathVerb verbs[] = {static_cast<PathVerb>(99u)};
		expect_path_refused(builder.add_path(verbs, {}), "out-of-range verb");
	}

	// Non-finite points, and a rectangle whose corner overflows:
	{
		PathBuilder pb;
		pb.move_to(nan, 0.0);
		expect_path_refused(builder.add_path(pb), "NaN point");
	}
	{
		PathBuilder pb;
		pb.rect(0.0, 0.0, inf, 10.0);
		expect_path_refused(builder.add_path(pb), "infinite rect size");
	}
	{
		PathBuilder pb;
		pb.rect(1e308, 0.0, 1e308, 10.0);
		expect_path_refused(builder.add_path(pb), "overflowing rect corner");
	}

	// Non-finite matrices, including a finite one whose product with the CTM overflows:
	expect_refused(builder.concat_matrix(PdfMatrix{nan, 0.0, 0.0, 1.0, 0.0, 0.0}), "NaN matrix");
	expect_refused(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, inf, 0.0}), "infinite matrix");
	{
		DisplayListBuilder scaled(PdfRect{0.0, 0.0, 200.0, 200.0});
		ASSERT_TRUE(scaled.concat_matrix(PdfMatrix{1e200, 0.0, 0.0, 1e200, 0.0, 0.0}).has_value());
		const auto res = scaled.concat_matrix(PdfMatrix{1e200, 0.0, 0.0, 1e200, 0.0, 0.0});
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat);
		EXPECT_EQ(scaled.command_count(), 1u);
	}
	expect_refused(builder.draw_glyph_run(
		good_glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black(),
		PdfMatrix{1.0, 0.0, 0.0, nan, 0.0, 0.0}), "NaN text matrix");
	expect_refused(builder.draw_image(good_bmp, PdfMatrix{1.0, 0.0, 0.0, 1.0, nan, 0.0}), "NaN image matrix");

	// Non-finite or out-of-domain widths, miter limits, dash values and phases:
	expect_refused(builder.set_stroke_style(nan_f), "NaN width");
	expect_refused(builder.set_stroke_style(inf_f), "infinite width");
	expect_refused(builder.set_stroke_style(-1.0f), "negative width");
	expect_refused(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 0.5f), "miter limit below 1");
	expect_refused(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, nan_f), "NaN miter limit");
	expect_refused(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, {}, nan_f), "NaN phase");

	// Non-finite or out-of-range alphas and colours:
	expect_refused(builder.set_alpha_and_blend(1.5f, 1.0f), "stroke alpha above 1");
	expect_refused(builder.set_alpha_and_blend(1.0f, -0.1f), "fill alpha below 0");
	expect_refused(builder.set_alpha_and_blend(nan_f, 1.0f), "NaN alpha");
	expect_refused(builder.draw_path(good_path, ColorRgba{nan_f, 0.0f, 0.0f, 1.0f}, ColorRgba::black(), true, false), "NaN colour");
	expect_refused(builder.draw_path(good_path, ColorRgba::black(), ColorRgba{0.0f, 0.0f, 0.0f, 2.0f}, false, true), "colour alpha above 1");
	expect_refused(builder.draw_glyph_run(good_glyphs, 12.0, TextRenderMode::Fill, ColorRgba{2.0f, 0.0f, 0.0f, 1.0f}, ColorRgba::black()), "glyph colour above 1");
	expect_refused(builder.draw_image(good_bmp, PdfMatrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0}, true, nullptr, ColorRgba{0.0f, 0.0f, 0.0f, nan_f}), "NaN tint");

	// Out-of-range enum values:
	expect_refused(builder.set_stroke_style(1.0f, static_cast<LineCap>(7u)), "line cap");
	expect_refused(builder.set_stroke_style(1.0f, LineCap::Butt, static_cast<LineJoin>(9u)), "line join");
	expect_refused(builder.set_alpha_and_blend(1.0f, 1.0f, static_cast<BlendMode>(99u)), "blend mode");
	expect_refused(builder.push_clip_path(good_path, static_cast<FillRule>(5u)), "clip fill rule");
	expect_refused(builder.draw_path(good_path, ColorRgba::black(), ColorRgba::black(), false, true, static_cast<FillRule>(5u)), "draw fill rule");
	expect_refused(builder.draw_glyph_run(good_glyphs, 12.0, static_cast<TextRenderMode>(12u), ColorRgba::black(), ColorRgba::black()), "render mode");

	// Out-of-range path indices:
	expect_refused(builder.push_clip_path(good_path + 1u), "clip path index");
	expect_refused(builder.draw_path(std::numeric_limits<uint32_t>::max(), ColorRgba::black(), ColorRgba::black(), false, true), "draw path index");

	// Malformed glyph records:
	{
		const PositionedGlyph nan_origin[] = {PositionedGlyph{1u, U'A', PdfPoint{nan, 0.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 12.0}}};
		expect_refused(builder.draw_glyph_run(nan_origin, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()), "NaN origin");
		const PositionedGlyph nan_advance[] = {PositionedGlyph{1u, U'A', PdfPoint{0.0, 0.0}, nan, PdfRect{0.0, 0.0, 8.0, 12.0}}};
		expect_refused(builder.draw_glyph_run(nan_advance, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()), "NaN advance");
		PdfRect inverted{};
		inverted.x_min = 8.0;
		inverted.x_max = 0.0;
		inverted.y_max = 12.0;
		const PositionedGlyph inverted_ink[] = {PositionedGlyph{1u, U'A', PdfPoint{0.0, 0.0}, 8.0, inverted}};
		expect_refused(builder.draw_glyph_run(inverted_ink, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()), "inverted ink box");
		expect_refused(builder.draw_glyph_run(good_glyphs, nan, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()), "NaN font size");
		expect_refused(builder.draw_glyph_run(good_glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black(), PdfMatrix::identity(), inverted), "inverted run box");
	}

	// A null bitmap:
	expect_refused(builder.draw_image(nullptr, PdfMatrix::identity()), "null bitmap");

	// `restore_state()` with no saved state is refused, records nothing, and
	// leaves the builder usable:
	expect_refused(builder.restore_state(), "restore without save");
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.restore_state().has_value());
	EXPECT_EQ(builder.command_count(), 2u);

	// Bitmap dimensions, strides and buffer lengths that overflow or disagree:
	const auto expect_bitmap_refused = [](uint32_t w, uint32_t h, uint32_t stride, size_t bytes, const char* what) {
		const auto res = DecodedBitmap::create(w, h, stride, std::vector<uint8_t>(bytes));
		ASSERT_FALSE(res.has_value()) << what;
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat) << what;
	};
	expect_bitmap_refused(0u, 1u, 4u, 4u, "zero width");
	expect_bitmap_refused(1u, 0u, 4u, 4u, "zero height");
	expect_bitmap_refused(65537u, 1u, 65537u * 4u, 65537u * 4u, "width above 65536");
	expect_bitmap_refused(2u, 2u, 7u, 16u, "stride below width * 4");
	expect_bitmap_refused(65536u, 65536u, 0xFFFFFFFFu, 16u, "stride * height overflows the byte limit");
	expect_bitmap_refused(2u, 2u, 8u, 15u, "buffer one byte short");
	expect_bitmap_refused(2u, 2u, 8u, 17u, "buffer one byte long");
	expect_bitmap_refused(2u, 2u, 12u, 22u, "padded buffer neither unpadded nor fully padded");
	// Padded rows are accepted with the last row padded or unpadded:
	EXPECT_TRUE(DecodedBitmap::create(2u, 2u, 12u, std::vector<uint8_t>(20u)).has_value());
	EXPECT_TRUE(DecodedBitmap::create(2u, 2u, 12u, std::vector<uint8_t>(24u)).has_value());
}

TEST(PdfDisplayList, AccessorBoundaryValidation)
{
	DisplayListBuilder builder;
	PathBuilder pb;
	pb.rect(0.0, 0.0, 10.0, 10.0);
	const uint32_t pid = *builder.add_path(pb);
	const float dashes[] = {2.0f, 2.0f};
	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, dashes).has_value());
	const PositionedGlyph glyphs[] = {
		PositionedGlyph::blank(1u, U' ', PdfPoint{0.0, 0.0}, 5.0),
	};
	ASSERT_TRUE(builder.draw_glyph_run(
		glyphs,
		12.0,
		TextRenderMode::Fill,
		ColorRgba::black(),
		ColorRgba::black()).has_value());

	const auto dl = *builder.finish();
	EXPECT_TRUE(dl->path(pid).has_value());
	EXPECT_FALSE(dl->path(pid + 1u).has_value());
	EXPECT_FALSE(dl->path(std::numeric_limits<uint32_t>::max()).has_value());

	RecordedPath bad_path{};
	bad_path.verb_offset = 10u;
	bad_path.verb_count = 1u;
	EXPECT_FALSE(dl->path_verbs(bad_path).has_value());

	bad_path.verb_offset = 0u;
	bad_path.verb_count = std::numeric_limits<uint32_t>::max();
	EXPECT_FALSE(dl->path_verbs(bad_path).has_value());

	bad_path.point_offset = std::numeric_limits<uint32_t>::max();
	bad_path.point_count = 2u;
	EXPECT_FALSE(dl->path_points(bad_path).has_value());

	RecordedGlyphRun bad_run{};
	bad_run.glyph_offset = 1u;
	bad_run.glyph_count = 1u;
	EXPECT_FALSE(dl->glyphs(bad_run).has_value());

	SetStrokeStyleCommand bad_style{};
	bad_style.dash_offset = 1u;
	bad_style.dash_count = 2u;
	EXPECT_FALSE(dl->dash_values(bad_style).has_value());
}

TEST(PdfDisplayList, SaveDepthOverflowPreventsPublication)
{
	DisplayListLimits limits{};
	limits.max_save_depth = 2u;
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);

	PathBuilder pb;
	pb.rect(10.0, 10.0, 20.0, 20.0);
	const uint32_t pid = *builder.add_path(pb);

	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.save_state().has_value());

	// 3rd `save_state()` exceeds `max_save_depth == 2`:
	const auto overflow_res = builder.save_state();
	ASSERT_FALSE(overflow_res.has_value());
	EXPECT_EQ(overflow_res.error().code, ErrorCode::ResourceLimit);

	// Subsequent state changes inside the overflowing frame and `finish()` are rejected:
	EXPECT_FALSE(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 50.0, 50.0}).has_value());
	EXPECT_FALSE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	EXPECT_FALSE(builder.restore_state().has_value());
	const auto finish_res = builder.finish();
	ASSERT_FALSE(finish_res.has_value());
	EXPECT_EQ(finish_res.error().code, ErrorCode::ResourceLimit);
}

TEST(PdfDisplayList, OneShotFinishSemantics)
{
	PathBuilder pb;
	pb.rect(10.0, 10.0, 50.0, 50.0);
	const PositionedGlyph glyphs[] = {
		PositionedGlyph{7u, U'Q', PdfPoint{20.0, 20.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 12.0}},
	};

	// 1. A second `finish()` and every mutation after sealing return
	//    `ErrorCode::InternalError`, leaving the published list unchanged:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());

		{
			auto first_dl = builder.finish();
			ASSERT_TRUE(first_dl.has_value());
			EXPECT_EQ((*first_dl)->command_count(), 1u);
			EXPECT_EQ(debug_live_display_lists(), 1);

			const auto bmp = make_bitmap(1u, 1u, {255u});
			const auto expect_sealed = [](const auto& res) {
				ASSERT_FALSE(res.has_value());
				EXPECT_EQ(res.error().code, ErrorCode::InternalError);
			};
			expect_sealed(builder.reserve_commands(4u));
			expect_sealed(builder.add_path(pb));
			expect_sealed(builder.add_path(pb.verbs(), pb.points()));
			expect_sealed(builder.save_state());
			expect_sealed(builder.restore_state());
			expect_sealed(builder.concat_matrix(PdfMatrix::identity()));
			expect_sealed(builder.set_stroke_style(1.0f));
			expect_sealed(builder.set_alpha_and_blend(1.0f, 1.0f));
			expect_sealed(builder.push_clip_path(pid));
			expect_sealed(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true));
			expect_sealed(builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()));
			expect_sealed(builder.draw_image(bmp, PdfMatrix::identity()));
			expect_sealed(builder.finish());
			EXPECT_EQ((*first_dl)->command_count(), 1u);
			EXPECT_EQ((*first_dl)->content_bounds_pt(), (PdfRect{10.0, 10.0, 60.0, 60.0}));
		}
		// First result has been released; calling `finish()` again must fail rather
		// than returning an empty DisplayList with stale bounds:
		EXPECT_EQ(debug_live_display_lists(), 0);
		const auto second_dl = builder.finish();
		ASSERT_FALSE(second_dl.has_value());
		EXPECT_EQ(second_dl.error().code, ErrorCode::InternalError);
		EXPECT_EQ(debug_live_display_lists(), 0);
	}

	// 2. The builder can be destroyed while readers walk the list: the list
	//    owns its storage (AddressSanitizer reports any read of freed builder
	//    memory).
	{
		auto builder = std::make_unique<DisplayListBuilder>(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t pid = *builder->add_path(pb);
		for (int i = 0; i < 64; ++i) {
			ASSERT_TRUE(builder->draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
			ASSERT_TRUE(builder->draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()).has_value());
		}
		const auto dl = finish_ok(*builder);
		ASSERT_NE(dl, nullptr);

		std::latch start(2);
		std::atomic<uint64_t> reader_sum{0u};
		std::thread reader([&]() {
			start.arrive_and_wait();
			uint64_t sum = 0u;
			for (int pass = 0; pass < 200; ++pass) {
				for (const DisplayCommand& cmd : dl->commands()) {
					if (const auto* dp = cmd.as<DrawPathCommand>()) {
						const RecordedPath p = *dl->path(dp->path_index);
						sum += dl->path_points(p)->size();
					} else if (const auto* gr = cmd.as<DrawGlyphRunCommand>()) {
						sum += (*dl->glyphs(*gr))[0].glyph_index;
					}
				}
				for (const CommandBounds& entry : dl->command_bounds()) {
					sum += entry.has_bounds ? 1u : 0u;
				}
			}
			reader_sum.store(sum);
		});
		start.arrive_and_wait();
		builder.reset();
		reader.join();
		// Per pass: 64 paths x 2 points, 64 runs x glyph 7, 128 painted entries.
		EXPECT_EQ(reader_sum.load(), 200u * (64u * 2u + 64u * 7u + 128u));
	}
}

TEST(PdfDisplayList, LiveDisplayListsCounter)
{
	EXPECT_EQ(debug_live_display_lists(), 0);
	{
		DisplayListBuilder b1;
		DisplayListBuilder b2;
		auto dl1 = *b1.finish();
		EXPECT_EQ(debug_live_display_lists(), 1);
		{
			auto dl2 = *b2.finish();
			EXPECT_EQ(debug_live_display_lists(), 2);
		}
		// `dl2` dropped while `b2` is still on the stack:
		EXPECT_EQ(debug_live_display_lists(), 1);
	}
	EXPECT_EQ(debug_live_display_lists(), 0);
}

TEST(PdfDisplayList, ConcurrentRead)
{
	// One entry per command, built from what was recorded, never from the list:
	// the command kind, its pool range and the shared payload it carries.
	struct WalkEntry {
		DisplayCommandKind kind{DisplayCommandKind::SaveState};
		uint64_t offset{0u};
		uint64_t count{0u};
		uint64_t payload{0u};
		const void* identity{nullptr};

		bool operator==(const WalkEntry&) const noexcept = default;
	};

	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
	constexpr size_t kTotalCommands = 10000u;
	ASSERT_TRUE(builder.reserve_commands(kTotalCommands).has_value());

	PathBuilder triangle;
	triangle.move_to(10.0, 10.0);
	triangle.line_to(100.0, 10.0);
	triangle.line_to(100.0, 80.0);
	triangle.close_subpath();
	PathBuilder square;
	square.rect(200.0, 200.0, 50.0, 50.0);
	const uint32_t paths[] = {*builder.add_path(triangle), *builder.add_path(square)};
	const uint64_t verb_offsets[] = {0u, 4u};
	const uint64_t verb_counts[] = {4u, 1u};
	const uint64_t point_ranges[] = {(0ull << 32) | 3u, (3ull << 32) | 2u};

	const auto bitmap = make_bitmap(2u, 1u, {255u, 128u});
	const auto mask = make_bitmap(1u, 1u, {255u});

	std::vector<WalkEntry> expected;
	expected.reserve(kTotalCommands);
	uint64_t dash_offset = 0u;
	uint64_t glyph_offset = 0u;
	// Record 10,000 commands (2,000 groups of 5 commands):
	for (size_t i = 0u; i < kTotalCommands / 5u; ++i) {
		const float dashes[] = {30.0f, static_cast<float>(10u + i % 7u)};
		const PositionedGlyph glyphs[] = {
			PositionedGlyph{static_cast<uint16_t>(10u + i % 50u), U'R', PdfPoint{72.0, 700.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 12.0}},
			PositionedGlyph{static_cast<uint16_t>(11u + i % 50u), U'e', PdfPoint{80.0, 700.0}, 6.0, PdfRect{0.0, 0.0, 6.0, 9.0}},
		};

		ASSERT_TRUE(builder.save_state().has_value());
		expected.push_back(WalkEntry{DisplayCommandKind::SaveState});

		ASSERT_TRUE(builder.set_stroke_style(1.5f, LineCap::Butt, LineJoin::Miter, 10.0f, dashes, 0.0f).has_value());
		expected.push_back(WalkEntry{DisplayCommandKind::SetStrokeStyle, dash_offset, 2u, static_cast<uint64_t>(dashes[1])});
		dash_offset += 2u;

		const size_t which = i % 2u;
		ASSERT_TRUE(builder.draw_path(paths[which], ColorRgba::black(), ColorRgba::white(), true, true).has_value());
		expected.push_back(WalkEntry{DisplayCommandKind::DrawPath, verb_offsets[which], verb_counts[which], point_ranges[which]});

		if (i % 2u == 0u) {
			ASSERT_TRUE(builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()).has_value());
			expected.push_back(WalkEntry{
				DisplayCommandKind::DrawGlyphRun,
				glyph_offset,
				2u,
				static_cast<uint64_t>(glyphs[0].glyph_index) * 1000u + glyphs[1].glyph_index});
			glyph_offset += 2u;
		} else {
			ASSERT_TRUE(builder.draw_image(bitmap, PdfMatrix{20.0, 0.0, 0.0, 10.0, 300.0, 300.0}, false, mask).has_value());
			expected.push_back(WalkEntry{
				DisplayCommandKind::DrawImage,
				0u,
				0u,
				reinterpret_cast<uintptr_t>(mask.get()),
				bitmap.get()});
		}

		ASSERT_TRUE(builder.restore_state().has_value());
		expected.push_back(WalkEntry{DisplayCommandKind::RestoreState});
	}

	auto dl = *builder.finish();
	ASSERT_EQ(dl->command_count(), kTotalCommands);
	ASSERT_EQ(expected.size(), kTotalCommands);

	const auto walk = [](const DisplayList& list, const DisplayCommand& cmd) {
		WalkEntry e{cmd.kind()};
		if (const auto* ss = cmd.as<SetStrokeStyleCommand>()) {
			const auto values = *list.dash_values(*ss);
			e.offset = ss->dash_offset;
			e.count = values.size();
			e.payload = static_cast<uint64_t>(values[1]);
		} else if (const auto* dp = cmd.as<DrawPathCommand>()) {
			const RecordedPath p = *list.path(dp->path_index);
			e.offset = p.verb_offset;
			e.count = list.path_verbs(p)->size();
			e.payload = (static_cast<uint64_t>(p.point_offset) << 32) | list.path_points(p)->size();
		} else if (const auto* gr = cmd.as<DrawGlyphRunCommand>()) {
			const auto glyph_span = *list.glyphs(*gr);
			e.offset = gr->glyph_offset;
			e.count = glyph_span.size();
			e.payload = static_cast<uint64_t>(glyph_span[0].glyph_index) * 1000u + glyph_span[1].glyph_index;
		} else if (const auto* img = cmd.as<DrawImageCommand>()) {
			e.payload = reinterpret_cast<uintptr_t>(img->soft_mask.get());
			e.identity = img->bitmap.get();
		}
		return e;
	};

	constexpr size_t kThreadCount = 8u;
	std::latch start_latch(kThreadCount);
	std::vector<size_t> mismatches(kThreadCount, 0u);
	std::vector<size_t> walked(kThreadCount, 0u);
	std::vector<std::thread> threads;
	threads.reserve(kThreadCount);

	for (size_t t = 0u; t < kThreadCount; ++t) {
		threads.emplace_back([&, t, shared_dl = dl]() {
			start_latch.arrive_and_wait();
			size_t bad = 0u;
			size_t k = 0u;
			for (const DisplayCommand& cmd : shared_dl->commands()) {
				if (k >= expected.size() || !(walk(*shared_dl, cmd) == expected[k])) {
					++bad;
				}
				++k;
			}
			mismatches[t] = bad;
			walked[t] = k;
		});
	}

	for (std::thread& th : threads) {
		th.join();
	}

	for (size_t t = 0u; t < kThreadCount; ++t) {
		EXPECT_EQ(walked[t], kTotalCommands) << "reader " << t;
		EXPECT_EQ(mismatches[t], 0u) << "reader " << t;
	}
}

TEST(PdfDisplayList, BoundsNeverCutInk)
{
	// Each case reproduces an ink loss of the earlier exact-clip candidate and
	// checks the box contains the analytic ink box.

	// 1. R3-3: a sliver inside the closed cubic clip `(0,0) (0,100) (100,100)
	//    (100,0)` lies below the curve and must keep its ink.
	PathBuilder cubic_clip_pb;
	cubic_clip_pb.move_to(0.0, 0.0);
	cubic_clip_pb.cubic_bezier_to(PdfPoint{0.0, 100.0}, PdfPoint{100.0, 100.0}, PdfPoint{100.0, 0.0});
	cubic_clip_pb.close_subpath();
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t clip = *builder.add_path(cubic_clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		const PdfRect sliver{11.19999, 49.45722, 11.2, 49.45723};
		PathBuilder sliver_pb;
		sliver_pb.rect(sliver.x_min, sliver.y_min, sliver.width(), sliver.height());
		const uint32_t draw = *builder.add_path(sliver_pb);
		ASSERT_TRUE(builder.draw_path(draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(dl->content_bounds_pt(), sliver);
	}
	// R3-3 again: `[10.5, 0, 11.2, 100]` under the same clip reaches the true
	// boundary `y ~= 49.4572565` at `x = 11.2`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t clip = *builder.add_path(cubic_clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		PathBuilder strip_pb;
		strip_pb.rect(10.5, 0.0, 0.7, 100.0);
		const uint32_t draw = *builder.add_path(strip_pb);
		ASSERT_TRUE(builder.draw_path(draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(dl->content_bounds_pt(), PdfRect{10.5, 0.0, 11.2, 49.4572565});
		// What the contract promises: the strip's box within the clip's exact box.
		expect_rect_near(dl->content_bounds_pt(), PdfRect{10.5, 0.0, 11.2, 75.0});
	}

	// 2. A fill under a circular clip (four cubic arcs, radius 50 around
	//    `(100, 100)`): the ink box is `[140, 90, 150, 110]`.
	{
		constexpr double k = 0.5522847498307936 * 50.0;
		PathBuilder circle;
		circle.move_to(150.0, 100.0);
		circle.cubic_bezier_to(PdfPoint{150.0, 100.0 + k}, PdfPoint{100.0 + k, 150.0}, PdfPoint{100.0, 150.0});
		circle.cubic_bezier_to(PdfPoint{100.0 - k, 150.0}, PdfPoint{50.0, 100.0 + k}, PdfPoint{50.0, 100.0});
		circle.cubic_bezier_to(PdfPoint{50.0, 100.0 - k}, PdfPoint{100.0 - k, 50.0}, PdfPoint{100.0, 50.0});
		circle.cubic_bezier_to(PdfPoint{100.0 + k, 50.0}, PdfPoint{150.0, 100.0 - k}, PdfPoint{150.0, 100.0});
		circle.close_subpath();
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 300.0, 300.0});
		const uint32_t clip = *builder.add_path(circle);
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		PathBuilder bar;
		bar.rect(140.0, 90.0, 60.0, 20.0);
		const uint32_t draw = *builder.add_path(bar);
		ASSERT_TRUE(builder.draw_path(draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(dl->content_bounds_pt(), PdfRect{140.0, 90.0, 150.0, 110.0});
	}

	// 3. A round cap of radius 100 under the strip clip `[299.98, 299.995]`
	//    keeps the cap's ink `y = 100 +- sqrt(100^2 - 99.98^2)`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 400.0, 400.0});
		PathBuilder clip_pb;
		clip_pb.rect(299.98, 0.0, 0.015, 200.0);
		const uint32_t clip = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		PathBuilder line_pb;
		line_pb.move_to(100.0, 100.0);
		line_pb.line_to(200.0, 100.0);
		const uint32_t line = *builder.add_path(line_pb);
		ASSERT_TRUE(builder.set_stroke_style(200.0f, LineCap::Round, LineJoin::Round, 10.0f).has_value());
		ASSERT_TRUE(builder.draw_path(line, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		const double dy = std::sqrt(100.0 * 100.0 - 99.98 * 99.98);
		expect_contains(dl->content_bounds_pt(), PdfRect{299.98, 100.0 - dy, 299.995, 100.0 + dy});
	}

	// 4. R3-4: dash `[0, 10]` with round caps paints dots at x = 10, 20 and
	//    30, the first one included: ink `[9, 9, 31, 11]`. The box is the
	//    solid stroke's `[9, 9, 36, 11]`.
	{
		PathBuilder line_pb;
		line_pb.move_to(10.0, 10.0);
		line_pb.line_to(35.0, 10.0);
		const float dots[] = {0.0f, 10.0f};
		const PdfRect box = stroke_bounds(line_pb, 2.0f, LineCap::Round, LineJoin::Miter, 10.0f, PdfMatrix::identity(), dots);
		expect_contains(box, PdfRect{9.0, 9.0, 31.0, 11.0});
		expect_rect_near(box, PdfRect{9.0, 9.0, 36.0, 11.0});
	}

	// 5. Odd dash `[10]` with phase 5 along `(0, 50) -> (25, 50)` under the
	//    clip `[6, 100]`: ink is the dash `[15, 25]`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(6.0, 0.0, 94.0, 100.0);
		const uint32_t clip = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip).has_value());
		PathBuilder line_pb;
		line_pb.move_to(0.0, 50.0);
		line_pb.line_to(25.0, 50.0);
		const uint32_t line = *builder.add_path(line_pb);
		const float odd_dash[] = {10.0f};
		ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Miter, 10.0f, odd_dash, 5.0f).has_value());
		ASSERT_TRUE(builder.draw_path(line, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl = finish_ok(builder);
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(dl->content_bounds_pt(), PdfRect{15.0, 49.0, 25.0, 51.0});
	}

	// 6. R3-5: a run without per-glyph ink boxes falls back to its run box
	//    `[10, 50, 90, 62]`; the glyphs' true ink is `[10, 20]` and `[80, 90]`.
	const PositionedGlyph no_ink[] = {
		PositionedGlyph{1u, U'A', PdfPoint{10.0, 50.0}, 10.0},
		PositionedGlyph{2u, U'B', PdfPoint{80.0, 50.0}, 10.0},
	};
	const PdfRect run_box{10.0, 50.0, 90.0, 62.0};
	const auto run_bounds = [&](std::optional<PdfRect> clip_rect) {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		if (clip_rect.has_value()) {
			PathBuilder clip_pb;
			clip_pb.rect(clip_rect->x_min, clip_rect->y_min, clip_rect->width(), clip_rect->height());
			EXPECT_TRUE(builder.push_clip_path(*builder.add_path(clip_pb)).has_value());
		}
		EXPECT_TRUE(builder.draw_glyph_run(
			no_ink,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black(),
			PdfMatrix::identity(),
			run_box).has_value());
		return finish_ok(builder);
	};
	{
		const auto dl = run_bounds(std::nullopt);
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(dl->content_bounds_pt(), PdfRect{10.0, 50.0, 90.0, 62.0});
	}
	{
		const auto dl = run_bounds(PdfRect{15.0, 0.0, 85.0, 100.0});
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(dl->content_bounds_pt(), PdfRect{15.0, 50.0, 85.0, 62.0});
	}
	{
		// Clipped to the gap the glyphs paint nothing; the conservative run
		// box stays within the clip and the run box.
		const auto dl = run_bounds(PdfRect{35.0, 0.0, 65.0, 100.0});
		ASSERT_TRUE(dl->has_content_bounds());
		expect_contains(PdfRect{35.0, 50.0, 65.0, 62.0}, dl->content_bounds_pt());
	}

	// 7. With per-glyph ink boxes each glyph is clipped on its own, so the
	//    gap clip leaves no ink:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(35.0, 0.0, 30.0, 100.0);
		ASSERT_TRUE(builder.push_clip_path(*builder.add_path(clip_pb)).has_value());
		const PositionedGlyph glyphs[] = {
			PositionedGlyph{1u, U'A', PdfPoint{10.0, 50.0}, 10.0, PdfRect{0.0, 0.0, 10.0, 12.0}},
			PositionedGlyph{2u, U'B', PdfPoint{80.0, 50.0}, 10.0, PdfRect{0.0, 0.0, 10.0, 12.0}},
		};
		ASSERT_TRUE(builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()).has_value());
		const auto dl = finish_ok(builder);
		EXPECT_FALSE(dl->has_content_bounds());
	}
}

TEST(PdfDisplayList, ImageCoverageUsesMaskCells)
{
	const PdfMatrix unit_100{100.0, 0.0, 0.0, 100.0, 0.0, 0.0};
	const auto image_bounds = [](
		const std::shared_ptr<const DecodedBitmap>& bmp,
		const PdfMatrix& dest,
		bool interpolate,
		const std::shared_ptr<const DecodedBitmap>& mask) -> std::shared_ptr<const DisplayList> {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		EXPECT_TRUE(builder.draw_image(bmp, dest, interpolate, mask).has_value());
		return finish_ok(builder);
	};

	// 1. R3-6: a fully opaque 1x1 image under a 2x1 mask with alpha `[0, 255]`
	//    paints only the mask's right cell.
	const auto opaque_1x1 = make_bitmap(1u, 1u, {255u});
	const auto right_half_mask = make_bitmap(2u, 1u, {0u, 255u});
	ASSERT_TRUE(right_half_mask->opaque_pixel_box.has_value());
	EXPECT_EQ(*right_half_mask->opaque_pixel_box, (PixelBox{1u, 0u, 2u, 1u}));
	{
		const auto dl = image_bounds(opaque_1x1, unit_100, false, right_half_mask);
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{50.0, 0.0, 100.0, 100.0}));
	}
	// Interpolated, bilinear sampling reaches half a mask cell into the
	// transparent left cell:
	{
		const auto dl = image_bounds(opaque_1x1, unit_100, true, right_half_mask);
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{25.0, 0.0, 100.0, 100.0}));
	}

	// 2. A fully transparent image or a fully transparent mask contributes nothing:
	const auto clear_1x1 = make_bitmap(1u, 1u, {0u});
	EXPECT_FALSE(clear_1x1->opaque_pixel_box.has_value());
	EXPECT_FALSE(image_bounds(clear_1x1, unit_100, false, nullptr)->has_content_bounds());
	EXPECT_FALSE(image_bounds(opaque_1x1, unit_100, true, clear_1x1)->has_content_bounds());

	// 3. A transparent border: a 4x4 image whose central 2x2 cells are opaque
	//    covers `[25, 25, 75, 75]`, its opaque box computed once at creation.
	{
		std::vector<uint8_t> px(4u * 4u * 4u, 0u);
		for (uint32_t y = 1u; y < 3u; ++y) {
			for (uint32_t x = 1u; x < 3u; ++x) {
				const size_t off = (static_cast<size_t>(y) * 4u + x) * 4u;
				px[off + 0u] = 0xFFu;
				px[off + 1u] = 0xFFu;
				px[off + 2u] = 0xFFu;
				px[off + 3u] = 0xFFu;
			}
		}
		const auto bmp = *DecodedBitmap::create(4u, 4u, 16u, std::move(px));
		EXPECT_EQ(*bmp->opaque_pixel_box, (PixelBox{1u, 1u, 3u, 3u}));
		EXPECT_EQ(image_bounds(bmp, unit_100, false, nullptr)->content_bounds_pt(), (PdfRect{25.0, 25.0, 75.0, 75.0}));
		EXPECT_EQ(image_bounds(bmp, unit_100, true, nullptr)->content_bounds_pt(), (PdfRect{12.5, 12.5, 87.5, 87.5}));
	}

	// 4. Equal-size disjoint alpha: the image's left cell and the mask's right
	//    cell meet only along `x = 0.5`, which paints nothing without
	//    interpolation and the blended middle with it.
	{
		const auto left = make_bitmap(2u, 1u, {255u, 0u});
		const PdfMatrix dest{100.0, 0.0, 0.0, 50.0, 10.0, 20.0};
		EXPECT_FALSE(image_bounds(left, dest, false, right_half_mask)->has_content_bounds());
		const auto blended = image_bounds(left, dest, true, right_half_mask);
		ASSERT_TRUE(blended->has_content_bounds());
		EXPECT_EQ(blended->content_bounds_pt(), (PdfRect{35.0, 20.0, 85.0, 70.0}));
	}

	// 5. Masks of a different resolution in both directions: a 2x2 image with
	//    its right column opaque under a 4x1 mask whose last cell is opaque.
	{
		const auto right_column = make_bitmap(2u, 2u, {0u, 255u, 0u, 255u});
		const auto last_quarter = make_bitmap(4u, 1u, {0u, 0u, 0u, 255u});
		EXPECT_EQ(image_bounds(right_column, unit_100, false, last_quarter)->content_bounds_pt(), (PdfRect{75.0, 0.0, 100.0, 100.0}));
	}

	// 6. Row orientation: row 0 is the image's first row, at the top of the unit square (ISO 32000-1 section 8.9.4),
	//    so cell row `r` of `h` covers unit `y` in `[1 - (r + 1) / h, 1 - r / h]`.
	{
		const auto first_row = make_bitmap(1u, 2u, {255u, 0u});
		EXPECT_EQ(*first_row->opaque_pixel_box, (PixelBox{0u, 0u, 1u, 1u}));
		EXPECT_EQ(image_bounds(first_row, unit_100, false, nullptr)->content_bounds_pt(), (PdfRect{0.0, 50.0, 100.0, 100.0}));
		const auto last_row = make_bitmap(1u, 2u, {0u, 255u});
		EXPECT_EQ(image_bounds(last_row, unit_100, false, nullptr)->content_bounds_pt(), (PdfRect{0.0, 0.0, 100.0, 50.0}));
	}
}

TEST(PdfDisplayList, TinyDashPatternsRecordInBoundedTime)
{
	// R3-1: sub-epsilon positive dash entries recorded and drawn at once. The
	// earlier candidate expanded dashes for bounds and looped forever on
	// `[1e-13, 1e-13]`; with dashes ignored for bounds each draw walks the
	// path once.
	const std::vector<std::vector<float>> patterns = {
		{1e-13f, 1e-13f},
		{1e-30f},
		{0.0f, 1e-13f},
		{1e-13f, 0.0f},
		{std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::denorm_min()},
		{1e-13f, 1e-13f, 1e-13f},
	};
	const float phases[] = {0.0f, 1e30f, 0.5f, 0.0f, 0.0f, 7.0f};

	PathBuilder pb;
	pb.move_to(10.0, 100.0);
	pb.line_to(510.0, 100.0);
	pb.cubic_bezier_to(PdfPoint{600.0, 100.0}, PdfPoint{600.0, 300.0}, PdfPoint{510.0, 300.0});

	const auto started = std::chrono::steady_clock::now();
	for (size_t i = 0u; i < patterns.size(); ++i) {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 1000.0, 1000.0});
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Round, LineJoin::Round, 10.0f, patterns[i], phases[i]).has_value()) << i;
		for (int repeat = 0; repeat < 100; ++repeat) {
			ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value()) << i;
		}
		const auto dl = finish_ok(builder);
		const auto* style = dl->commands()[0].as<SetStrokeStyleCommand>();
		ASSERT_NE(style, nullptr);
		EXPECT_EQ(style->dash_count, patterns[i].size()) << i;
		// The dashed stroke's box is the solid stroke's.
		EXPECT_EQ(dl->content_bounds_pt(), stroke_bounds(pb, 2.0f, LineCap::Round, LineJoin::Round, 10.0f)) << i;
	}
	const auto elapsed = std::chrono::steady_clock::now() - started;
	EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(PdfDisplayList, SmallBudgetsRefuseBeforeAllocating)
{
	// R3-2: every pool is charged by capacity before it grows, so a refusal
	// leaves the builder's heap under `max_total_bytes` and allocates nothing
	// beyond an error message. The heap meter is unavailable under
	// AddressSanitizer; there only the codes and `retained_bytes()` are checked.
	using recto::test::ScopedHeapMeter;
	constexpr size_t kErrorSlack = 1024u; // an error's message and diagnostic record
	(void)make_pdf_error(ErrorCode::ResourceLimit, "warm-up for the diagnostic record");

	const auto polygon = [](size_t vertices) {
		PathBuilder pb;
		pb.move_to(100.0, 50.0);
		for (size_t i = 1u; i < vertices; ++i) {
			const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(vertices);
			pb.line_to(100.0 + 50.0 * std::cos(a), 100.0 + 50.0 * std::sin(a));
		}
		pb.close_subpath();
		return pb;
	};

	// 1. Clips: the budget holds the clip path and one command but not the
	//    command's bounds entry. The earlier candidate built the clip's
	//    polygon region (a block per edge) before checking.
	{
		const PathBuilder pb = polygon(200u);
		const size_t path_bytes =
			pb.verbs().size() * sizeof(PathVerb) +
			pb.points().size() * sizeof(PdfPoint) +
			sizeof(RecordedPath);
		DisplayListLimits limits{};
		limits.max_total_bytes = path_bytes + sizeof(DisplayCommand);
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
		const auto pid = builder.add_path(pb);
		ASSERT_TRUE(pid.has_value());
		const size_t before = builder.retained_bytes();
		EXPECT_LE(before, limits.max_total_bytes);

		ScopedHeapMeter meter;
		const auto res = builder.push_clip_path(*pid);
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(builder.command_count(), 0u);
		EXPECT_EQ(builder.retained_bytes(), before);
		if (ScopedHeapMeter::available()) {
			EXPECT_LE(before + meter.peak_bytes(), limits.max_total_bytes + kErrorSlack);
			EXPECT_LE(meter.allocated_bytes(), kErrorSlack);
		}
	}

	// 2. Saved states: saving until the budget refuses stops with
	//    `ResourceLimit` (not the depth limit), the peak heap included.
	{
		DisplayListLimits limits{};
		limits.max_total_bytes = 4096u;
		limits.max_save_depth = 1000u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
		ScopedHeapMeter meter;
		std::optional<PdfError> refusal;
		for (int i = 0; i < 1000 && !refusal.has_value(); ++i) {
			const auto res = builder.save_state();
			if (!res.has_value()) {
				refusal = res.error();
			}
		}
		ASSERT_TRUE(refusal.has_value());
		EXPECT_EQ(refusal->code, ErrorCode::ResourceLimit);
		EXPECT_GT(builder.command_count(), 0u);
		EXPECT_LE(builder.retained_bytes(), limits.max_total_bytes);
		if (ScopedHeapMeter::available()) {
			EXPECT_LE(meter.peak_bytes(), limits.max_total_bytes + kErrorSlack);
		}
		const size_t recorded = builder.command_count();
		const auto dl = finish_ok(builder);
		EXPECT_EQ(dl->command_count(), recorded);
	}

	// 3. Glyph runs: a 1,000-glyph run under a 4 KiB budget, and under a
	//    10-glyph limit with the default budget, is refused before anything
	//    is reserved. The earlier candidate reserved one rectangle per glyph
	//    first.
	std::vector<PositionedGlyph> many_glyphs;
	for (int i = 0; i < 1000; ++i) {
		many_glyphs.push_back(PositionedGlyph{1u, U'A', PdfPoint{static_cast<double>(i % 50), 10.0}, 1.0, PdfRect{0.0, 0.0, 1.0, 2.0}});
	}
	{
		DisplayListLimits limits{};
		limits.max_total_bytes = 4096u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
		ScopedHeapMeter meter;
		const auto res = builder.draw_glyph_run(many_glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black());
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(builder.command_count(), 0u);
		EXPECT_EQ(builder.retained_bytes(), 0u);
		if (ScopedHeapMeter::available()) {
			EXPECT_LE(meter.allocated_bytes(), kErrorSlack);
		}
	}
	{
		DisplayListLimits limits{};
		limits.max_glyphs = 10u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
		ScopedHeapMeter meter;
		const auto res = builder.draw_glyph_run(many_glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black());
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(builder.retained_bytes(), 0u);
		if (ScopedHeapMeter::available()) {
			EXPECT_LE(meter.allocated_bytes(), kErrorSlack);
		}
	}

	// 4. Command reservations count what the builder already holds: with a
	//    path taking half the budget, reserving a budget's worth of commands
	//    is refused. The earlier candidate compared the count with the whole
	//    budget alone and reserved it.
	{
		const PathBuilder pb = polygon(500u);
		DisplayListLimits limits{};
		limits.max_total_bytes = 16384u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
		ASSERT_TRUE(builder.add_path(pb).has_value());
		const size_t before = builder.retained_bytes();
		ASSERT_GT(before, limits.max_total_bytes / 2u);
		{
			ScopedHeapMeter meter;
			const auto res = builder.reserve_commands(limits.max_total_bytes / sizeof(DisplayCommand));
			ASSERT_FALSE(res.has_value());
			EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
			EXPECT_EQ(builder.retained_bytes(), before);
			if (ScopedHeapMeter::available()) {
				EXPECT_LE(meter.allocated_bytes(), kErrorSlack);
			}
		}
		ASSERT_TRUE(builder.reserve_commands(2u).has_value());
		EXPECT_LE(builder.retained_bytes(), limits.max_total_bytes);
	}

	// 5. Reserved but unused capacity counts: after reserving 40 empty command
	//    slots, a path that would fit beside 40 recorded-size-zero pools is
	//    refused. Accounting by size instead of capacity would accept it and
	//    hold more heap than the budget.
	{
		const PathBuilder pb = polygon(500u);
		DisplayListLimits limits{};
		limits.max_total_bytes = 16384u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
		ASSERT_TRUE(builder.reserve_commands(40u).has_value());
		ASSERT_EQ(builder.command_count(), 0u);
		const size_t before = builder.retained_bytes();
		ASSERT_GE(before, 40u * (sizeof(DisplayCommand) + sizeof(CommandBounds)));
		ScopedHeapMeter meter;
		const auto res = builder.add_path(pb);
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(builder.path_count(), 0u);
		if (ScopedHeapMeter::available()) {
			EXPECT_LE(before + meter.peak_bytes(), limits.max_total_bytes + kErrorSlack);
		}
	}
}

TEST(PdfDisplayList, CommandBoundsPerDraw)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

	PathBuilder page_pb;
	page_pb.rect(0.0, 0.0, 612.0, 792.0);
	const uint32_t page_path = *builder.add_path(page_pb);
	PathBuilder inset_pb;
	inset_pb.rect(72.0, 100.0, 468.0, 600.0);
	const uint32_t inset_path = *builder.add_path(inset_pb);
	PathBuilder half_pb;
	half_pb.rect(0.0, 0.0, 306.0, 792.0);
	const uint32_t half_path = *builder.add_path(half_pb);
	PathBuilder rule_pb;
	rule_pb.move_to(200.0, 50.0);
	rule_pb.line_to(400.0, 50.0);
	const uint32_t rule_path = *builder.add_path(rule_pb);
	const PositionedGlyph glyphs[] = {
		PositionedGlyph{2u, U'H', PdfPoint{100.0, 720.0}, 15.0, PdfRect{0.0, 0.0, 15.0, 20.0}},
	};

	ASSERT_TRUE(builder.draw_path(page_path, ColorRgba::white(), ColorRgba::white(), false, true).has_value()); // 0
	ASSERT_TRUE(builder.save_state().has_value());                                                               // 1
	ASSERT_TRUE(builder.set_stroke_style(2.0f).has_value());                                                    // 2
	ASSERT_TRUE(builder.draw_path(inset_path, ColorRgba::black(), ColorRgba::black(), false, true).has_value()); // 3
	ASSERT_TRUE(builder.draw_glyph_run(glyphs, 20.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black()).has_value()); // 4
	ASSERT_TRUE(builder.push_clip_path(half_path).has_value());                                                 // 5
	ASSERT_TRUE(builder.draw_path(rule_path, ColorRgba::black(), ColorRgba::black(), true, false).has_value()); // 6
	ASSERT_TRUE(builder.restore_state().has_value());                                                           // 7
	ASSERT_TRUE(builder.draw_path(inset_path, ColorRgba::black(), ColorRgba{0.0f, 0.0f, 0.0f, 0.0f}, false, true).has_value()); // 8

	const auto dl = finish_ok(builder);
	const auto entries = dl->command_bounds();
	ASSERT_EQ(entries.size(), dl->commands().size());
	ASSERT_EQ(entries.size(), 9u);

	// The white full-page background is its own entry, so a margin detector
	// can leave it out:
	ASSERT_TRUE(entries[0].has_bounds);
	EXPECT_EQ(entries[0].bounds_pt, (PdfRect{0.0, 0.0, 612.0, 792.0}));
	for (const size_t state_index : {1u, 2u, 5u, 7u}) {
		EXPECT_FALSE(entries[state_index].has_bounds) << state_index;
	}
	ASSERT_TRUE(entries[3].has_bounds);
	EXPECT_EQ(entries[3].bounds_pt, (PdfRect{72.0, 100.0, 540.0, 700.0}));
	ASSERT_TRUE(entries[4].has_bounds);
	EXPECT_EQ(entries[4].bounds_pt, (PdfRect{100.0, 720.0, 115.0, 740.0}));
	ASSERT_TRUE(entries[6].has_bounds);
	EXPECT_EQ(entries[6].bounds_pt, (PdfRect{200.0, 49.0, 306.0, 51.0})); // clipped by the half-page clip
	EXPECT_FALSE(entries[8].has_bounds); // alpha 0 fill

	// The union of every entry is the content bounds; without the background
	// it is the inset content.
	EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{0.0, 0.0, 612.0, 792.0}));
	std::optional<PdfRect> inset;
	for (size_t i = 1u; i < entries.size(); ++i) {
		if (entries[i].has_bounds) {
			inset = inset.has_value() ? union_rect(*inset, entries[i].bounds_pt) : entries[i].bounds_pt;
		}
	}
	ASSERT_TRUE(inset.has_value());
	EXPECT_EQ(*inset, (PdfRect{72.0, 49.0, 540.0, 740.0}));
}

TEST(PdfDisplayList, PageBoxConvention)
{
	// `page_box_pt` is the crop box in unrotated default user space exactly as
	// passed (non-zero origins kept), and `content_bounds_pt` is in that space.

	// 1. A blank page has a page box and no content bounds:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
		const auto dl = finish_ok(builder);
		EXPECT_EQ(dl->page_box_pt(), (PdfRect{0.0, 0.0, 612.0, 792.0}));
		EXPECT_FALSE(dl->has_content_bounds());
		EXPECT_EQ(dl->content_bounds_pt(), PdfRect{});
	}

	// 2. A shifted page box keeps its origin; content inside it keeps its box,
	//    content partly outside is cut to it, content wholly outside is none.
	{
		const PdfRect shifted{100.0, 200.0, 712.0, 992.0};
		DisplayListBuilder builder(shifted);
		PathBuilder inside_pb;
		inside_pb.rect(150.0, 250.0, 100.0, 100.0);
		PathBuilder straddle_pb;
		straddle_pb.rect(50.0, 150.0, 150.0, 150.0);
		PathBuilder outside_pb;
		outside_pb.rect(0.0, 0.0, 50.0, 50.0);
		const uint32_t inside = *builder.add_path(inside_pb);
		const uint32_t straddle = *builder.add_path(straddle_pb);
		const uint32_t outside = *builder.add_path(outside_pb);
		ASSERT_TRUE(builder.draw_path(inside, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		ASSERT_TRUE(builder.draw_path(straddle, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		ASSERT_TRUE(builder.draw_path(outside, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto dl = finish_ok(builder);
		EXPECT_EQ(dl->page_box_pt(), shifted);
		const auto entries = dl->command_bounds();
		ASSERT_EQ(entries.size(), 3u);
		EXPECT_EQ(entries[0].bounds_pt, (PdfRect{150.0, 250.0, 250.0, 350.0}));
		EXPECT_EQ(entries[1].bounds_pt, (PdfRect{100.0, 200.0, 200.0, 300.0}));
		EXPECT_FALSE(entries[2].has_bounds);
		EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{100.0, 200.0, 250.0, 350.0}));
	}
}

TEST(PdfDisplayList, DashPatternRecording)
{
	// ISO 32000-1 8.4.3.6. Each accepted call records one SetStrokeStyle.
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
	const float all_zero[] = {0.0f, 0.0f};
	const float single_zero[] = {0.0f};
	const float odd_one[] = {3.0f};
	const float odd_three[] = {1.0f, 2.0f, 3.0f};
	const float mixed_zero[] = {2.0f, 0.0f};
	const float negative[] = {-1.0f, 2.0f};
	const float not_finite[] = {std::numeric_limits<float>::infinity(), 1.0f};
	const float nan_entry[] = {std::numeric_limits<float>::quiet_NaN()};

	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, {}, 2.0f).has_value());          // empty: solid
	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, all_zero, 3.0f).has_value());    // all zero: solid
	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, single_zero, 0.0f).has_value()); // all zero: solid
	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, odd_one, 0.0f).has_value());     // odd: unchanged
	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, odd_three, 7.5f).has_value());   // odd: unchanged
	ASSERT_TRUE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, mixed_zero, -4.0f).has_value()); // zero entries kept
	for (const std::span<const float> refused : {std::span<const float>(negative), std::span<const float>(not_finite), std::span<const float>(nan_entry)}) {
		const auto res = builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, refused, 0.0f);
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat);
	}
	EXPECT_EQ(builder.command_count(), 6u);

	const auto dl = finish_ok(builder);
	const auto style = [&](size_t i) { return *dl->commands()[i].as<SetStrokeStyleCommand>(); };
	const auto values = [&](size_t i) {
		const auto span = *dl->dash_values(style(i));
		return std::vector<float>(span.begin(), span.end());
	};
	EXPECT_EQ(style(0).dash_count, 0u);
	EXPECT_FLOAT_EQ(style(0).dash_phase, 2.0f);
	EXPECT_EQ(style(1).dash_count, 0u);
	EXPECT_FLOAT_EQ(style(1).dash_phase, 3.0f);
	EXPECT_EQ(style(2).dash_count, 0u);
	EXPECT_EQ(values(3), (std::vector<float>{3.0f}));
	EXPECT_EQ(values(4), (std::vector<float>{1.0f, 2.0f, 3.0f}));
	EXPECT_FLOAT_EQ(style(4).dash_phase, 7.5f);
	EXPECT_EQ(values(5), (std::vector<float>{2.0f, 0.0f}));
	EXPECT_FLOAT_EQ(style(5).dash_phase, -4.0f);
	EXPECT_EQ(dl->dash_values().size(), 6u);

	// With butt caps a dashed stroke's box is the solid stroke's (dash caps:
	// `DashCapsStayInsideBounds`).
	PathBuilder line;
	line.move_to(10.0, 10.0);
	line.line_to(100.0, 10.0);
	const float dashes[] = {5.0f, 50.0f};
	EXPECT_EQ(
		stroke_bounds(line, 4.0f, LineCap::Butt, LineJoin::Miter, 10.0f, PdfMatrix::identity(), dashes),
		stroke_bounds(line, 4.0f, LineCap::Butt, LineJoin::Miter, 10.0f));
}

TEST(PdfDisplayList, FiniteInputTransformationOverflowRejected)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

	PathBuilder pb;
	pb.rect(10.0, 10.0, 100.0, 100.0);
	const uint32_t pid = *builder.add_path(pb);

	// Push a large finite scale matrix (1e308) inside a saved state:
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1e308, 0.0, 0.0, 1e308, 0.0, 0.0}).has_value());

	// Transformed coordinates `10.0 * 1e308` overflow `double` to `+Inf`, so
	// `push_clip_path`, `draw_path`, `draw_glyph_run`, and `draw_image` must
	// reject them with `ErrorCode::DamagedFormat` without recording commands:
	const size_t cmds_before = builder.command_count();

	const auto clip_res = builder.push_clip_path(pid);
	ASSERT_FALSE(clip_res.has_value());
	EXPECT_EQ(clip_res.error().code, ErrorCode::DamagedFormat);

	const auto path_res = builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, true);
	ASSERT_FALSE(path_res.has_value());
	EXPECT_EQ(path_res.error().code, ErrorCode::DamagedFormat);

	const PositionedGlyph glyphs[] = {
		PositionedGlyph{1u, U'A', PdfPoint{10.0, 10.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 12.0}},
	};
	const auto glyph_res = builder.draw_glyph_run(
		glyphs,
		12.0,
		TextRenderMode::Fill,
		ColorRgba::black(),
		ColorRgba::black());
	ASSERT_FALSE(glyph_res.has_value());
	EXPECT_EQ(glyph_res.error().code, ErrorCode::DamagedFormat);

	std::vector<uint8_t> px(4u, 0xFFu);
	auto bmp = *DecodedBitmap::create(1u, 1u, 4u, std::move(px));
	const auto img_res = builder.draw_image(
		bmp,
		PdfMatrix{10.0, 0.0, 0.0, 10.0, 0.0, 0.0});
	ASSERT_FALSE(img_res.has_value());
	EXPECT_EQ(img_res.error().code, ErrorCode::DamagedFormat);

	EXPECT_EQ(builder.command_count(), cmds_before);

	// A stroke whose widening overflows is refused too: the vertices are
	// finite under the CTM, their offsets by half the line width are not.
	DisplayListBuilder wide(PdfRect{0.0, 0.0, 612.0, 792.0});
	const uint32_t wide_pid = *wide.add_path(pb);
	ASSERT_TRUE(wide.concat_matrix(PdfMatrix{1e300, 0.0, 0.0, 1e300, 0.0, 0.0}).has_value());
	ASSERT_TRUE(wide.set_stroke_style(std::numeric_limits<float>::max(), LineCap::Round, LineJoin::Round).has_value());
	const auto wide_res = wide.draw_path(wide_pid, ColorRgba::black(), ColorRgba::black(), true, false);
	ASSERT_FALSE(wide_res.has_value());
	EXPECT_EQ(wide_res.error().code, ErrorCode::DamagedFormat);
	EXPECT_EQ(wide.command_count(), 2u);
}

TEST(PdfDisplayList, ClipDepthGeometryWorkAndFaultInjection)
{
	// 1. `max_clip_depth` bounds repeated `push_clip_path()` calls:
	{
		DisplayListLimits limits{};
		limits.max_clip_depth = 2u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);

		PathBuilder pb;
		pb.rect(10.0, 10.0, 100.0, 100.0);
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.push_clip_path(pid).has_value());
		ASSERT_TRUE(builder.push_clip_path(pid).has_value());
		const auto third = builder.push_clip_path(pid);
		ASSERT_FALSE(third.has_value());
		EXPECT_EQ(third.error().code, ErrorCode::ResourceLimit);
		// A saved state keeps the clip depth it was saved with:
		ASSERT_TRUE(builder.save_state().has_value());
		EXPECT_FALSE(builder.push_clip_path(pid).has_value());
	}

	// 2. `max_geometry_work_ops` bounds the verbs and points walked over every
	//    draw: a rectangle costs 4 (1 verb, 2 points, 1 per call), so a budget
	//    of 5 allows one draw of it and refuses the second.
	{
		DisplayListLimits limits{};
		limits.max_geometry_work_ops = 5u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);

		PathBuilder pb;
		pb.rect(10.0, 10.0, 100.0, 100.0);
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto res = builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true);
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
		EXPECT_EQ(builder.command_count(), 1u);
	}

	// 3. Deterministic allocation failure during `draw_path()` leaves command count
	//    and content bounds unchanged, and failure during `finish()` latches the
	//    builder so a retry cannot publish an empty DisplayList with stale bounds:
	{
		DisplayListLimits draw_fail_limits{};
		draw_fail_limits.fail_allocation_after_ops = 2u; // op 1 = add_path, op 2 = draw_path
		DisplayListBuilder b1(PdfRect{0.0, 0.0, 200.0, 200.0}, draw_fail_limits);
		PathBuilder pb;
		pb.rect(10.0, 10.0, 50.0, 50.0);
		const uint32_t pid = *b1.add_path(pb);
		const auto draw_res = b1.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true);
		ASSERT_FALSE(draw_res.has_value());
		EXPECT_EQ(draw_res.error().code, ErrorCode::OutOfMemory);
		EXPECT_EQ(b1.command_count(), 0u);

		DisplayListLimits finish_fail_limits{};
		finish_fail_limits.fail_allocation_after_ops = 3u; // 1 = add_path, 2 = draw_path, 3 = finish
		DisplayListBuilder b2(PdfRect{0.0, 0.0, 200.0, 200.0}, finish_fail_limits);
		const uint32_t pid2 = *b2.add_path(pb);
		ASSERT_TRUE(b2.draw_path(pid2, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto fin1 = b2.finish();
		ASSERT_FALSE(fin1.has_value());
		EXPECT_EQ(fin1.error().code, ErrorCode::OutOfMemory);
		EXPECT_TRUE(b2.is_finished());

		const auto fin2 = b2.finish();
		ASSERT_FALSE(fin2.has_value());
		EXPECT_EQ(fin2.error().code, ErrorCode::InternalError);
		EXPECT_EQ(debug_live_display_lists(), 0);
	}
}

TEST(PdfDisplayList, FinishControlBlockFailurePublishesNothing)
{
	PathBuilder pb;
	pb.rect(10.0, 10.0, 50.0, 50.0);

	// Control: without the injection the same recording publishes.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		EXPECT_NE(finish_ok(builder), nullptr);
	}

	DisplayListLimits limits{};
	limits.fail_finish_control_block_allocation = true;
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);
	const uint32_t pid = *builder.add_path(pb);
	ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	ASSERT_EQ(builder.command_count(), 1u);
	ASSERT_EQ(builder.path_count(), 1u);
	EXPECT_EQ(debug_live_display_lists(), 0);

	const auto fin = builder.finish();
	ASSERT_FALSE(fin.has_value());
	EXPECT_EQ(fin.error().code, ErrorCode::OutOfMemory);
	EXPECT_TRUE(builder.is_finished());
	// The failure struck after the pools moved into the constructed list (an
	// injection before the move would leave them here), and the list it built
	// was destroyed rather than published:
	EXPECT_EQ(builder.command_count(), 0u);
	EXPECT_EQ(builder.path_count(), 0u);
	EXPECT_EQ(debug_live_display_lists(), 0);

	const auto retry = builder.finish();
	ASSERT_FALSE(retry.has_value());
	EXPECT_EQ(retry.error().code, ErrorCode::InternalError);
	const auto mutate = builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true);
	ASSERT_FALSE(mutate.has_value());
	EXPECT_EQ(mutate.error().code, ErrorCode::InternalError);
}

TEST(PdfDisplayList, DashCapsStayInsideBounds)
{
	// Completion review G1: a dash end inside a segment carries a cap (ISO
	// 32000-1 8.4.3.6). The first dash of `[140 1000]` on the polyline
	// (100,100) -> (200,200) -> (100,300), width 100, ends 1.4 before the
	// corner, and its projecting cap reaches x = 100 + 190 / sqrt(2) +
	// 50 / sqrt(2) = 269.706, past the bevel the solid stroke would have there.
	PathBuilder corner;
	corner.move_to(100.0, 100.0);
	corner.line_to(200.0, 200.0);
	corner.line_to(100.0, 300.0);
	const float dashes[] = {140.0f, 1000.0f};
	const double reach_x = 100.0 + 190.0 / std::numbers::sqrt2 + 50.0 / std::numbers::sqrt2;
	const double reach_y = 100.0 + 190.0 / std::numbers::sqrt2 - 50.0 / std::numbers::sqrt2;
	const PdfRect solid = stroke_bounds(corner, 100.0f, LineCap::ProjectingSquare, LineJoin::Bevel, 10.0f);
	EXPECT_LT(solid.x_max, reach_x - 1.0); // the solid stroke alone would cut the cap
	const PdfRect dashed =
		stroke_bounds(corner, 100.0f, LineCap::ProjectingSquare, LineJoin::Bevel, 10.0f, PdfMatrix::identity(), dashes);
	EXPECT_GE(dashed.x_max, reach_x - 1e-9);

	// The same under a rotation and shear: the cap corner's image stays inside.
	const PdfMatrix ctm{0.8, 0.6, -0.3, 0.9, 50.0, -20.0};
	const PdfRect transformed =
		stroke_bounds(corner, 100.0f, LineCap::ProjectingSquare, LineJoin::Bevel, 10.0f, ctm, dashes);
	const PdfPoint cap_corner = transform_point(ctm, PdfPoint{reach_x, reach_y});
	EXPECT_GE(transformed.x_max, cap_corner.x - 1e-9);
	EXPECT_LE(transformed.y_min, cap_corner.y + 1e-9);

	// Round caps: the dash end's disk at (198.995, 198.995) reaches x = 248.995.
	const PdfRect round =
		stroke_bounds(corner, 100.0f, LineCap::Round, LineJoin::Bevel, 10.0f, PdfMatrix::identity(), dashes);
	EXPECT_GE(round.x_max, 100.0 + 140.0 / std::numbers::sqrt2 + 50.0 - 1e-9);

	// Butt caps add nothing beyond the solid stroke.
	EXPECT_EQ(
		stroke_bounds(corner, 100.0f, LineCap::Butt, LineJoin::Bevel, 10.0f, PdfMatrix::identity(), dashes),
		stroke_bounds(corner, 100.0f, LineCap::Butt, LineJoin::Bevel, 10.0f));
}

TEST(PdfDisplayList, CubicExtremaOverflowKeepsTheHull)
{
	// Completion review G2: finite control points whose extremum arithmetic
	// overflows (x = [100, 1e200, -1e200, 100]) still give a box containing the
	// curve, which crosses the whole page, instead of the endpoints' box.
	PathBuilder wide;
	wide.move_to(100.0, 50.0);
	wide.cubic_bezier_to(PdfPoint{1e200, 50.0}, PdfPoint{-1e200, 60.0}, PdfPoint{100.0, 60.0});
	wide.close_subpath();
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
	const auto pid = builder.add_path(wide);
	ASSERT_TRUE(pid.has_value());
	ASSERT_TRUE(builder.draw_path(*pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	const auto dl = finish_ok(builder);
	ASSERT_TRUE(dl->has_content_bounds());
	EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{0.0, 50.0, 200.0, 60.0}));
}

TEST(PdfDisplayList, GrowthNearTheBudgetStaysAmortized)
{
	// Completion review G4: once doubling no longer fits `max_total_bytes`,
	// the pools grow to the largest capacity that fits instead of one slot at
	// a time. With room for 5 x 1024 command slots (a command and its bounds),
	// doubling from 2048 to 4096 no longer fits but growing does: the earlier
	// code then grew one slot per command up to 2560 (524 reallocations); now
	// one reallocation reaches 3072 and the next command is refused.
	const size_t slot = sizeof(DisplayCommand) + sizeof(CommandBounds);
	DisplayListLimits limits{};
	limits.max_total_bytes = 5u * 1024u * slot;
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 100.0, 100.0}, limits);
	size_t recorded = 0u;
	size_t growths = 0u;
	size_t last_bytes = builder.retained_bytes();
	for (;;) {
		const auto res = builder.concat_matrix(PdfMatrix::identity());
		if (!res.has_value()) {
			EXPECT_EQ(res.error().code, ErrorCode::ResourceLimit);
			break;
		}
		++recorded;
		if (builder.retained_bytes() != last_bytes) {
			++growths;
			last_bytes = builder.retained_bytes();
		}
		ASSERT_LT(recorded, 1'000'000u);
	}
	EXPECT_EQ(recorded, 3072u);
	EXPECT_LE(growths, 14u) << recorded << " commands";
	EXPECT_LE(builder.retained_bytes(), limits.max_total_bytes);
}

TEST(PdfDisplayList, HugeFiniteSegmentKeepsItsStroke)
{
	// Round 1 finding 1: a finite segment whose length overflows `hypot`
	// ((-8e307, -8e307) to (8e307, 8e307)) keeps its direction, so under a
	// 1e-306 scale its hairline stays the page line (70, 70) to (230, 230).
	PathBuilder line;
	line.move_to(-8e307, -8e307);
	line.line_to(8e307, 8e307);
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 300.0, 300.0});
	const auto pid = builder.add_path(line);
	ASSERT_TRUE(pid.has_value());
	ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1e-306, 0.0, 0.0, 1e-306, 150.0, 150.0}).has_value());
	ASSERT_TRUE(builder.set_stroke_style(0.0f).has_value());
	ASSERT_TRUE(builder.draw_path(*pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
	const auto dl = finish_ok(builder);
	ASSERT_TRUE(dl->has_content_bounds());
	const PdfRect& box = dl->content_bounds_pt();
	EXPECT_NEAR(box.x_min, 70.0, 1e-9);
	EXPECT_NEAR(box.y_min, 70.0, 1e-9);
	EXPECT_NEAR(box.x_max, 230.0, 1e-9);
	EXPECT_NEAR(box.y_max, 230.0, 1e-9);
}

TEST(PdfDisplayList, Type3GlyphsPaintByTheirOwnProcedure)
{
	// Round 1 finding 2: a Type 3 glyph's procedure sets its own colours and
	// state (ISO 32000-1 9.6.5), so its sub-list counts under `Tr 1` with the
	// run's stroke alpha at 0; only `Tr 3` paints nothing.
	DisplayListBuilder glyph_builder(PdfRect{0.0, 0.0, 100.0, 100.0});
	PathBuilder square;
	square.rect(10.0, 10.0, 80.0, 80.0);
	const auto sid = glyph_builder.add_path(square);
	ASSERT_TRUE(sid.has_value());
	ASSERT_TRUE(glyph_builder.draw_path(*sid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	const auto glyph = finish_ok(glyph_builder);
	ASSERT_EQ(glyph->content_bounds_pt(), (PdfRect{10.0, 10.0, 90.0, 90.0}));

	const auto run_bounds = [&](TextRenderMode mode) -> std::optional<PdfRect> {
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		EXPECT_TRUE(builder.set_alpha_and_blend(0.0f, 1.0f).has_value());
		const PositionedGlyph glyphs[] = {PositionedGlyph{1u, 0x41u, PdfPoint{0.0, 0.0}, 100.0}};
		EXPECT_TRUE(builder
						.draw_glyph_run(glyphs, 1.0, mode, ColorRgba::black(), ColorRgba::black(), PdfMatrix::identity(),
							std::nullopt, nullptr, glyph)
						.has_value());
		const auto dl = finish_ok(builder);
		return dl->has_content_bounds() ? std::optional<PdfRect>(dl->content_bounds_pt()) : std::nullopt;
	};
	EXPECT_EQ(run_bounds(TextRenderMode::Stroke), (std::optional<PdfRect>(PdfRect{10.0, 10.0, 90.0, 90.0})));
	EXPECT_EQ(run_bounds(TextRenderMode::Fill), (std::optional<PdfRect>(PdfRect{10.0, 10.0, 90.0, 90.0})));
	EXPECT_EQ(run_bounds(TextRenderMode::Invisible), std::nullopt);
}

TEST(PdfDisplayList, CurveStrokesWidenOnlyByTheirNormals)
{
	// Round 1 finding 3: `100 w 0 J 100 100 m 125 100 175 100 200 100 c S`, a
	// straight cubic with butt caps, paints exactly [100, 50, 200, 150], as
	// the same line would.
	PathBuilder straight;
	straight.move_to(100.0, 100.0);
	straight.cubic_bezier_to(PdfPoint{125.0, 100.0}, PdfPoint{175.0, 100.0}, PdfPoint{200.0, 100.0});
	EXPECT_EQ(stroke_bounds(straight, 100.0f, LineCap::Butt, LineJoin::Miter, 10.0f), (PdfRect{100.0, 50.0, 200.0, 150.0}));

	// A curved stroke under a rotation and shear contains every point of its
	// swept normals and is within 1% of their box.
	PathBuilder arch;
	arch.move_to(0.0, 0.0);
	arch.cubic_bezier_to(PdfPoint{30.0, 80.0}, PdfPoint{120.0, 90.0}, PdfPoint{150.0, 10.0});
	const PdfMatrix ctm{0.8, 0.6, -0.3, 0.9, 50.0, -20.0};
	const double r = 10.0;
	double x_min = 1e300;
	double y_min = 1e300;
	double x_max = -1e300;
	double y_max = -1e300;
	for (int i = 0; i <= 4000; ++i) {
		const double t = static_cast<double>(i) / 4000.0;
		const double u = 1.0 - t;
		const PdfPoint c{
			u * u * u * 0.0 + 3.0 * u * u * t * 30.0 + 3.0 * u * t * t * 120.0 + t * t * t * 150.0,
			u * u * u * 0.0 + 3.0 * u * u * t * 80.0 + 3.0 * u * t * t * 90.0 + t * t * t * 10.0};
		const double dx = 3.0 * u * u * 30.0 + 6.0 * u * t * (120.0 - 30.0) + 3.0 * t * t * (150.0 - 120.0);
		const double dy = 3.0 * u * u * 80.0 + 6.0 * u * t * (90.0 - 80.0) + 3.0 * t * t * (10.0 - 90.0);
		const double len = std::hypot(dx, dy);
		for (const double s : {-r, r}) {
			const PdfPoint q = transform_point(ctm, PdfPoint{c.x - s * dy / len, c.y + s * dx / len});
			x_min = std::min(x_min, q.x);
			y_min = std::min(y_min, q.y);
			x_max = std::max(x_max, q.x);
			y_max = std::max(y_max, q.y);
		}
	}
	const PdfRect box = stroke_bounds(arch, static_cast<float>(2.0 * r), LineCap::Butt, LineJoin::Miter, 10.0f, ctm);
	EXPECT_LE(box.x_min, x_min + 1e-9);
	EXPECT_LE(box.y_min, y_min + 1e-9);
	EXPECT_GE(box.x_max, x_max - 1e-9);
	EXPECT_GE(box.y_max, y_max - 1e-9);
	EXPECT_LT(box.width(), 1.01 * (x_max - x_min));
	EXPECT_LT(box.height(), 1.01 * (y_max - y_min));
}

TEST(PdfDisplayList, CubicRootsSurviveCancellation)
{
	// Round 2 finding 1: `0 100 m 1e16 100 1e16 200 1 200 c h f`. The textbook
	// root formula cancels to t = 0 and t = 2e16 and loses the extremum at
	// t = 0.5 (x = 7.5e15); the stable form keeps it, so the fill reaches the
	// page's right edge.
	PathBuilder loop;
	loop.move_to(0.0, 100.0);
	loop.cubic_bezier_to(PdfPoint{1e16, 100.0}, PdfPoint{1e16, 200.0}, PdfPoint{1.0, 200.0});
	loop.close_subpath();
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 300.0, 300.0});
	const auto pid = builder.add_path(loop);
	ASSERT_TRUE(pid.has_value());
	ASSERT_TRUE(builder.draw_path(*pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	const auto dl = finish_ok(builder);
	ASSERT_TRUE(dl->has_content_bounds());
	EXPECT_EQ(dl->content_bounds_pt(), (PdfRect{0.0, 100.0, 300.0, 200.0}));
}

// A glyph's horizontal fitting scale (D01 T02 section 43) is recorded and read back; one that is not finite and
// positive is refused.
TEST(PdfDisplayList, GlyphWidthScale)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
	PositionedGlyph fitted{7u, U'm', PdfPoint{10.0, 10.0}, 5.0, PdfRect{0.0, 0.0, 5.0, 6.0}};
	fitted.width_scale = 0.6;
	const PositionedGlyph glyphs[] = {PositionedGlyph{6u, U'a', PdfPoint{0.0, 10.0}, 10.0, PdfRect{0.0, 0.0, 9.0, 6.0}}, fitted};
	ASSERT_TRUE(builder.draw_glyph_run(glyphs, 10.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::white()).has_value());
	for (const double bad : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
		PositionedGlyph refused = fitted;
		refused.width_scale = bad;
		const PositionedGlyph one[] = {refused};
		const auto r = builder.draw_glyph_run(one, 10.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::white());
		ASSERT_FALSE(r.has_value()) << bad;
		EXPECT_EQ(r.error().code, ErrorCode::DamagedFormat) << bad;
	}
	auto dl = builder.finish();
	ASSERT_TRUE(dl.has_value());
	const auto* run = (*dl)->commands().back().as<DrawGlyphRunCommand>();
	ASSERT_NE(run, nullptr);
	const auto read = (*dl)->glyphs(*run);
	ASSERT_TRUE(read.has_value());
	ASSERT_EQ(read->size(), 2u);
	EXPECT_DOUBLE_EQ((*read)[0].width_scale, 1.0);
	EXPECT_DOUBLE_EQ((*read)[1].width_scale, 0.6);
}

TEST(PdfDisplayList, NearReversingMiterKeepsItsApex)
{
	// Round 2 finding 2: `2 w 0 J 0 j 1e9 M 100 150 m 150 150 l 100 150.0000005 l S`.
	// The turn's miter ratio is about 2e8, inside the 1e9 limit, so the apex
	// reaches x = 2e8 + 150; `1 + dot` rounded to 0 had dropped it.
	PathBuilder spike;
	spike.move_to(100.0, 150.0);
	spike.line_to(150.0, 150.0);
	spike.line_to(100.0, 150.0000005);
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 300.0, 300.0});
	const auto pid = builder.add_path(spike);
	ASSERT_TRUE(pid.has_value());
	ASSERT_TRUE(builder.set_stroke_style(2.0f, LineCap::Butt, LineJoin::Miter, 1e9f).has_value());
	ASSERT_TRUE(builder.draw_path(*pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
	const auto dl = finish_ok(builder);
	ASSERT_TRUE(dl->has_content_bounds());
	EXPECT_EQ(dl->content_bounds_pt().x_max, 300.0);
}

} // namespace
} // namespace recto::pdf
