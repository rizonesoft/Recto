// Unit tests for `recto::pdf::DisplayList` and `recto::pdf::DisplayListBuilder`
// (D01 T02 section 1).
#include "display_list.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <latch>
#include <limits>
#include <memory>
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
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

	// 1. Non-stroked, non-filled path (`n` operator) does not contribute to content bounds:
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

	// 2. Clipped path inside a saved state:
	ASSERT_TRUE(builder.save_state().has_value());
	PathBuilder clip_pb;
	clip_pb.rect(50.0, 60.0, 200.0, 200.0); // clip window [50, 60, 250, 260]
	const auto clip_path = builder.add_path(clip_pb);
	ASSERT_TRUE(clip_path.has_value());
	ASSERT_TRUE(builder.push_clip_path(*clip_path).has_value());

	PathBuilder wide_pb;
	wide_pb.rect(20.0, 40.0, 300.0, 100.0); // unclipped [20, 40, 320, 140] -> clipped [50, 60, 250, 140]
	const auto wide_path = builder.add_path(wide_pb);
	ASSERT_TRUE(wide_path.has_value());
	ASSERT_TRUE(builder.draw_path(
		*wide_path,
		ColorRgba::black(),
		ColorRgba::black(),
		false,
		true).has_value());
	ASSERT_TRUE(builder.restore_state().has_value());

	// 3. Transformed image outside the restored clip:
	std::vector<uint8_t> px(16u, 0xFFu);
	auto bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(px));
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 300.0, 400.0}).has_value());
	ASSERT_TRUE(builder.draw_image(bmp, PdfMatrix{80.0, 0.0, 0.0, 50.0, 0.0, 0.0}).has_value()); // [300, 400, 380, 450]
	ASSERT_TRUE(builder.restore_state().has_value());

	// 4. Invisible (`Tr=3`) glyph run does not expand content bounds, while
	//    visible (`Tr=0`) glyph run with explicit glyph ink bounds does:
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

	auto dl_res = builder.finish();
	ASSERT_TRUE(dl_res.has_value());
	const auto& dl = *dl_res;
	ASSERT_TRUE(dl->has_content_bounds());
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_min, 50.0);
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_min, 60.0);
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_max, 380.0);
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_max, 520.0);
}

TEST(PdfDisplayList, StrokeBoundsAndCapsMitersTransforms)
{
	// 1. Horizontal stroke with width 20 and Butt cap from (100, 100) to (200, 100)
	//    expands Y to [90, 110] and keeps X at [100, 200]:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 500.0, 500.0});
		PathBuilder pb;
		pb.move_to(100.0, 100.0);
		pb.line_to(200.0, 100.0);
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.set_stroke_style(20.0f, LineCap::Butt, LineJoin::Bevel, 10.0f).has_value());
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 100.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 90.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 200.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 110.0, 1e-6);
	}

	// 2. ProjectingSquare cap extends endpoints by half stroke width (10 pt):
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 500.0, 500.0});
		PathBuilder pb;
		pb.move_to(100.0, 100.0);
		pb.line_to(200.0, 100.0);
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.set_stroke_style(20.0f, LineCap::ProjectingSquare, LineJoin::Bevel, 10.0f).has_value());
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 90.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 90.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 210.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 110.0, 1e-6);
	}

	// 3. Round cap under nonuniform scaling `scale(3, 5)` expands X by 3*r and Y by 5*r:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 1000.0, 1000.0});
		PathBuilder pb;
		pb.move_to(50.0, 40.0);
		pb.line_to(100.0, 40.0);
		const uint32_t pid = *builder.add_path(pb);
		ASSERT_TRUE(builder.concat_matrix(PdfMatrix{3.0, 0.0, 0.0, 5.0, 0.0, 0.0}).has_value());
		ASSERT_TRUE(builder.set_stroke_style(10.0f, LineCap::Round, LineJoin::Round, 10.0f).has_value());
		ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		// User-space X in [50 - 5, 100 + 5] = [45, 105] -> scaled by 3 = [135, 315]
		// User-space Y in [40 - 5, 40 + 5] = [35, 45]   -> scaled by 5 = [175, 225]
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 135.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 175.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 315.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 225.0, 1e-6);
	}

	// 4. Miter join spike vs Bevel fallback when miter_limit is exceeded:
	//    Symmetric V-shape `(100, 90) -> (200, 100) -> (100, 110)` has a sharp turn at (200, 100).
	{
		PathBuilder pb;
		pb.move_to(100.0, 90.0);
		pb.line_to(200.0, 100.0);
		pb.line_to(100.0, 110.0);

		DisplayListBuilder miter_builder(PdfRect{0.0, 0.0, 500.0, 500.0});
		const uint32_t p_miter = *miter_builder.add_path(pb);
		ASSERT_TRUE(miter_builder.set_stroke_style(20.0f, LineCap::Butt, LineJoin::Miter, 25.0f).has_value());
		ASSERT_TRUE(miter_builder.draw_path(p_miter, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl_miter = *miter_builder.finish();

		DisplayListBuilder bevel_builder(PdfRect{0.0, 0.0, 500.0, 500.0});
		const uint32_t p_bevel = *bevel_builder.add_path(pb);
		ASSERT_TRUE(bevel_builder.set_stroke_style(20.0f, LineCap::Butt, LineJoin::Miter, 2.0f).has_value());
		ASSERT_TRUE(bevel_builder.draw_path(p_bevel, ColorRgba::black(), ColorRgba::black(), true, false).has_value());
		const auto dl_bevel = *bevel_builder.finish();

		// With miter_limit=25, the miter apex projects ~100.5 pt past x=200 (~300.5).
		// With miter_limit=2, it falls back to bevel (~201.0).
		EXPECT_GT(dl_miter->content_bounds_pt().x_max, 290.0);
		EXPECT_LT(dl_bevel->content_bounds_pt().x_max, 205.0);
	}
}

TEST(PdfDisplayList, PolygonAndEvenOddClippingBounds)
{
	// 1. Triangle clip `(0,0) -> (100,0) -> (0,100)` (`x + y <= 100`):
	//    A rectangle drawn at `(80, 80) -> (100, 100)` lies inside the triangle's
	//    bounding box `[0, 0, 100, 100]` but strictly outside the triangle itself.
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
		outside_pb.rect(80.0, 80.0, 20.0, 20.0);
		const uint32_t outside_rect = *builder.add_path(outside_pb);
		ASSERT_TRUE(builder.draw_path(
			outside_rect,
			ColorRgba::black(),
			ColorRgba::black(),
			false,
			true).has_value());

		const auto dl = *builder.finish();
		EXPECT_FALSE(dl->has_content_bounds());
	}

	// 2. EvenOdd clip with a central hole vs NonZero clip without hole:
	//    Outer rect `[0, 0, 100, 100]` + inner rect `[20, 20, 80, 80]` with same winding.
	//    Drawing a square at `[30, 30, 70, 70]` inside the hole produces NO ink
	//    under `FillRule::EvenOdd`, and `[30, 30, 70, 70]` under `FillRule::NonZero`.
	{
		PathBuilder ring_pb;
		ring_pb.rect(0.0, 0.0, 100.0, 100.0);
		ring_pb.rect(20.0, 20.0, 60.0, 60.0);

		PathBuilder inner_pb;
		inner_pb.rect(30.0, 30.0, 40.0, 40.0);

		DisplayListBuilder eo_builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t eo_clip = *eo_builder.add_path(ring_pb);
		const uint32_t eo_draw = *eo_builder.add_path(inner_pb);
		ASSERT_TRUE(eo_builder.push_clip_path(eo_clip, FillRule::EvenOdd).has_value());
		ASSERT_TRUE(eo_builder.draw_path(eo_draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto eo_dl = *eo_builder.finish();
		EXPECT_FALSE(eo_dl->has_content_bounds());

		DisplayListBuilder nz_builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		const uint32_t nz_clip = *nz_builder.add_path(ring_pb);
		const uint32_t nz_draw = *nz_builder.add_path(inner_pb);
		ASSERT_TRUE(nz_builder.push_clip_path(nz_clip, FillRule::NonZero).has_value());
		ASSERT_TRUE(nz_builder.draw_path(nz_draw, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
		const auto nz_dl = *nz_builder.finish();
		ASSERT_TRUE(nz_dl->has_content_bounds());
		EXPECT_DOUBLE_EQ(nz_dl->content_bounds_pt().x_min, 30.0);
		EXPECT_DOUBLE_EQ(nz_dl->content_bounds_pt().y_min, 30.0);
		EXPECT_DOUBLE_EQ(nz_dl->content_bounds_pt().x_max, 70.0);
		EXPECT_DOUBLE_EQ(nz_dl->content_bounds_pt().y_max, 70.0);
	}

	// 3. Nested nonrectangular clips:
	//    Clip 1: triangle `(0,0) -> (100,0) -> (0,100)` (`x + y <= 100`)
	//    Clip 2: horizontal band `[0, 60, 100, 100]` (`y >= 60`)
	//    Intersection is the smaller triangle `(0,60) -> (40,60) -> (0,100)`.
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
		full_pb.rect(0.0, 0.0, 100.0, 100.0);
		const uint32_t draw_id = *builder.add_path(full_pb);

		ASSERT_TRUE(builder.push_clip_path(c1, FillRule::NonZero).has_value());
		ASSERT_TRUE(builder.push_clip_path(c2, FillRule::NonZero).has_value());
		ASSERT_TRUE(builder.draw_path(draw_id, ColorRgba::black(), ColorRgba::black(), false, true).has_value());

		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 0.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 60.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 40.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 100.0, 1e-6);
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
}

TEST(PdfDisplayList, TransparentOperationsExcludedFromContentBounds)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});

	PathBuilder pb;
	pb.rect(10.0, 10.0, 100.0, 100.0);
	const uint32_t pid = *builder.add_path(pb);

	// 1. State alpha set to (0, 0) inside saved state:
	ASSERT_TRUE(builder.save_state().has_value());
	ASSERT_TRUE(builder.set_alpha_and_blend(0.0f, 0.0f, BlendMode::Normal).has_value());
	ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), true, true).has_value());

	const PositionedGlyph glyphs[] = {
		PositionedGlyph{1u, U'A', PdfPoint{50.0, 50.0}, 10.0, PdfRect{0.0, 0.0, 10.0, 12.0}},
	};
	ASSERT_TRUE(builder.draw_glyph_run(
		glyphs,
		12.0,
		TextRenderMode::FillAndStroke,
		ColorRgba::black(),
		ColorRgba::black()).has_value());

	std::vector<uint8_t> opaque_px(16u, 0xFFu);
	auto opaque_bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(opaque_px));
	ASSERT_TRUE(builder.draw_image(opaque_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 20.0, 20.0}).has_value());
	ASSERT_TRUE(builder.restore_state().has_value());

	// 2. Restored state has alpha 1.0, but command colours or bitmap pixels have alpha 0:
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
	auto trans_bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(transparent_px));
	EXPECT_FALSE(trans_bmp->has_non_transparent_pixels);
	ASSERT_TRUE(builder.draw_image(trans_bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 200.0, 200.0}).has_value());

	const auto dl = *builder.finish();
	EXPECT_FALSE(dl->has_content_bounds());
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
	EXPECT_FALSE(builder.reserve_commands(4u).has_value());

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
	EXPECT_FALSE(builder.add_path(huge_pb).has_value());
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
	EXPECT_FALSE(builder.draw_path(*p0, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	EXPECT_EQ(builder.command_count(), 3u);

	// Builder still seals cleanly with the 3 valid commands and exact bounds:
	const auto dl_res = builder.finish();
	ASSERT_TRUE(dl_res.has_value());
	EXPECT_EQ((*dl_res)->command_count(), 3u);
	ASSERT_TRUE((*dl_res)->has_content_bounds());
	EXPECT_DOUBLE_EQ((*dl_res)->content_bounds_pt().x_min, 10.0);
	EXPECT_DOUBLE_EQ((*dl_res)->content_bounds_pt().y_min, 20.0);
	EXPECT_DOUBLE_EQ((*dl_res)->content_bounds_pt().x_max, 40.0);
	EXPECT_DOUBLE_EQ((*dl_res)->content_bounds_pt().y_max, 60.0);
}

TEST(PdfDisplayList, RejectsMalformedAndNonFiniteInputs)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();

	// 1. Truncated CubicBezierTo with 0 points:
	const PathVerb trunc_verbs[] = {PathVerb::MoveTo, PathVerb::CubicBezierTo};
	const PdfPoint trunc_pts[] = {PdfPoint{10.0, 10.0}};
	EXPECT_FALSE(builder.add_path(trunc_verbs, trunc_pts).has_value());

	// 2. LineTo without preceding MoveTo:
	const PathVerb no_move_verbs[] = {PathVerb::LineTo};
	const PdfPoint no_move_pts[] = {PdfPoint{10.0, 10.0}};
	EXPECT_FALSE(builder.add_path(no_move_verbs, no_move_pts).has_value());

	// 3. Invalid PathVerb enum value:
	const PathVerb bad_verb[] = {static_cast<PathVerb>(99u)};
	EXPECT_FALSE(builder.add_path(bad_verb, {}).has_value());

	// 4. NaN / Infinity in path points:
	PathBuilder nan_pb;
	nan_pb.move_to(nan, 0.0);
	EXPECT_FALSE(builder.add_path(nan_pb).has_value());

	PathBuilder inf_pb;
	inf_pb.rect(0.0, 0.0, inf, 10.0);
	EXPECT_FALSE(builder.add_path(inf_pb).has_value());
	EXPECT_EQ(builder.path_count(), 0u);

	// 5. NaN / Infinity in matrices, stroke style, and alpha:
	EXPECT_FALSE(builder.concat_matrix(PdfMatrix{nan, 0.0, 0.0, 1.0, 0.0, 0.0}).has_value());
	EXPECT_FALSE(builder.set_stroke_style(-1.0f).has_value());
	EXPECT_FALSE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 0.5f).has_value());
	const float zero_dashes[] = {0.0f, 0.0f};
	EXPECT_FALSE(builder.set_stroke_style(1.0f, LineCap::Butt, LineJoin::Miter, 10.0f, zero_dashes).has_value());
	EXPECT_FALSE(builder.set_alpha_and_blend(1.5f, 1.0f).has_value());
	EXPECT_FALSE(builder.set_alpha_and_blend(-0.1f, 1.0f).has_value());
	EXPECT_EQ(builder.command_count(), 0u);
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
	EXPECT_EQ(overflow_res.error().code, ErrorCode::DamagedFormat);

	// Subsequent state changes inside the overflowing frame and `finish()` are rejected:
	EXPECT_FALSE(builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 50.0, 50.0}).has_value());
	EXPECT_FALSE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());
	EXPECT_FALSE(builder.restore_state().has_value());
	const auto finish_res = builder.finish();
	ASSERT_FALSE(finish_res.has_value());
	EXPECT_EQ(finish_res.error().code, ErrorCode::DamagedFormat);
}

TEST(PdfDisplayList, OneShotFinishSemantics)
{
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
	PathBuilder pb;
	pb.rect(10.0, 10.0, 50.0, 50.0);
	const uint32_t pid = *builder.add_path(pb);
	ASSERT_TRUE(builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true).has_value());

	{
		auto first_dl = builder.finish();
		ASSERT_TRUE(first_dl.has_value());
		EXPECT_EQ((*first_dl)->command_count(), 1u);
		EXPECT_EQ(debug_live_display_lists(), 1);
	}
	// First result has been released; calling `finish()` again must fail rather
	// than returning an empty DisplayList with stale bounds:
	EXPECT_EQ(debug_live_display_lists(), 0);
	const auto second_dl = builder.finish();
	ASSERT_FALSE(second_dl.has_value());
	EXPECT_EQ(second_dl.error().code, ErrorCode::InternalError);
	EXPECT_EQ(debug_live_display_lists(), 0);
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
	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
	constexpr size_t kTotalCommands = 10000u;
	ASSERT_TRUE(builder.reserve_commands(kTotalCommands).has_value());

	PathBuilder pb;
	pb.move_to(10.0, 10.0);
	pb.line_to(100.0, 10.0);
	pb.line_to(100.0, 80.0);
	pb.close_subpath();
	const uint32_t path_id = *builder.add_path(pb);

	const PositionedGlyph glyphs[] = {
		PositionedGlyph{10u, U'R', PdfPoint{72.0, 700.0}, 8.0, PdfRect{0.0, 0.0, 8.0, 12.0}},
		PositionedGlyph{11u, U'e', PdfPoint{80.0, 700.0}, 6.0, PdfRect{0.0, 0.0, 6.0, 9.0}},
	};
	const float dashes[] = {30.0f, 10.0f};

	// Record 10,000 commands (2,000 groups of 5 commands):
	for (size_t i = 0u; i < kTotalCommands / 5u; ++i) {
		ASSERT_TRUE(builder.save_state().has_value());
		ASSERT_TRUE(builder.set_stroke_style(
			1.5f,
			LineCap::Butt,
			LineJoin::Miter,
			10.0f,
			dashes,
			0.0f).has_value());
		ASSERT_TRUE(builder.draw_path(
			path_id,
			ColorRgba::black(),
			ColorRgba::white(),
			true,
			true).has_value());
		ASSERT_TRUE(builder.draw_glyph_run(
			glyphs,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black()).has_value());
		ASSERT_TRUE(builder.restore_state().has_value());
	}

	auto dl = *builder.finish();
	ASSERT_EQ(dl->command_count(), kTotalCommands);

	constexpr size_t kThreadCount = 8u;
	std::latch start_latch(kThreadCount);
	std::vector<uint64_t> checksums(kThreadCount, 0u);
	std::vector<std::thread> threads;
	threads.reserve(kThreadCount);

	for (size_t t = 0u; t < kThreadCount; ++t) {
		threads.emplace_back([&, t, shared_dl = dl]() {
			start_latch.arrive_and_wait();
			uint64_t acc = 0u;
			for (const DisplayCommand& cmd : shared_dl->commands()) {
				acc = acc * 131u + static_cast<uint64_t>(cmd.kind()) + 1u;
				if (const auto* dp = cmd.as<DrawPathCommand>()) {
					const RecordedPath p = *shared_dl->path(dp->path_index);
					acc += shared_dl->path_verbs(p)->size() +
						shared_dl->path_points(p)->size();
				} else if (const auto* gr = cmd.as<DrawGlyphRunCommand>()) {
					const auto glyph_span = *shared_dl->glyphs(*gr);
					for (const PositionedGlyph& g : glyph_span) {
						acc += g.glyph_index + g.unicode_codepoint;
					}
				} else if (const auto* ss = cmd.as<SetStrokeStyleCommand>()) {
					acc += shared_dl->dash_values(*ss)->size();
				}
			}
			checksums[t] = acc;
		});
	}

	for (std::thread& th : threads) {
		th.join();
	}

	EXPECT_GT(checksums[0], 0u);
	for (size_t t = 1u; t < kThreadCount; ++t) {
		EXPECT_EQ(checksums[t], checksums[0]);
	}
}

TEST(PdfDisplayList, CurvedClipAndRoundCapExactBoundsBetweenSamplePoints)
{
	// 1. Cubic Bezier `(0, 0), (0, 100), (100, 100), (100, 0)` clipped to a
	//    narrow vertical strip `[10.5, 11.2] x [0, 100]` strictly between the
	//    coarse 32-step sample points (`t = 6/32 = 0.1875 -> x = 9.2285` and
	//    `t = 7/32 = 0.21875 -> x = 12.2375`).
	//    At `x(t) = 300 t^2 - 200 t^3`:
	//    - `x(t0) = 10.5` -> `t0 ~= 0.2016359676`, `y(t0) = 300 t0 (1 - t0) ~= 48.2943125`
	//    - `x(t1) = 11.2` -> `t1 ~= 0.2089268455`, `y(t1) = 300 t1 (1 - t1) ~= 49.4572565`
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(10.5, 0.0, 0.7, 100.0);
		const uint32_t clip_id = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip_id).has_value());

		PathBuilder curve_pb;
		curve_pb.move_to(0.0, 0.0);
		curve_pb.cubic_bezier_to(
			PdfPoint{0.0, 100.0},
			PdfPoint{100.0, 100.0},
			PdfPoint{100.0, 0.0});
		curve_pb.close_subpath();
		const uint32_t curve_id = *builder.add_path(curve_pb);
		ASSERT_TRUE(builder.draw_path(
			curve_id,
			ColorRgba::black(),
			ColorRgba::black(),
			false,
			true).has_value());

		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 10.5, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 11.2, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 0.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 49.4572565, 1e-4);
	}

	// 2. Round cap on a horizontal stroke `(100, 100) -> (200, 100)` with width
	//    `200.0` (`r = 100.0`) clipped to a thin strip `[299.98, 299.995] x [0, 200]`
	//    near the rightmost tip `(300, 100)` where fixed 48-step arc chords would
	//    miss the true circular boundary `y = 100 +- sqrt(100^2 - (299.98 - 200)^2)`.
	//    Exact `dy = sqrt(10000 - 99.98^2) = sqrt(3.9996) ~= 1.9999000`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 400.0, 400.0});
		PathBuilder clip_pb;
		clip_pb.rect(299.98, 0.0, 0.015, 200.0);
		const uint32_t clip_id = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip_id).has_value());

		PathBuilder line_pb;
		line_pb.move_to(100.0, 100.0);
		line_pb.line_to(200.0, 100.0);
		const uint32_t line_id = *builder.add_path(line_pb);
		ASSERT_TRUE(builder.set_stroke_style(
			200.0f,
			LineCap::Round,
			LineJoin::Round,
			10.0f).has_value());
		ASSERT_TRUE(builder.draw_path(
			line_id,
			ColorRgba::black(),
			ColorRgba::black(),
			true,
			false).has_value());

		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		const double exact_dy = std::sqrt(100.0 * 100.0 - 99.98 * 99.98);
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 299.98, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 299.995, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 100.0 - exact_dy, 1e-4);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 100.0 + exact_dy, 1e-4);
	}
}

TEST(PdfDisplayList, DashPatternPhaseOddLengthAndGapClipping)
{
	// 1. Dash pattern `[20, 30]` with phase `0` along `(0, 100) -> (100, 100)`:
	//    Painted dashes are `[0, 20]` and `[50, 70]`; `[20, 50]` and `[70, 100]`
	//    are unpainted gaps. A clip restricted to `[25, 45] x [80, 120]` inside
	//    the gap must report `has_content_bounds() == false`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(25.0, 80.0, 20.0, 40.0);
		const uint32_t clip_id = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip_id).has_value());

		PathBuilder line_pb;
		line_pb.move_to(0.0, 100.0);
		line_pb.line_to(100.0, 100.0);
		const uint32_t line_id = *builder.add_path(line_pb);
		const float dashes[] = {20.0f, 30.0f};
		ASSERT_TRUE(builder.set_stroke_style(
			4.0f,
			LineCap::Butt,
			LineJoin::Miter,
			10.0f,
			dashes,
			0.0f).has_value());
		ASSERT_TRUE(builder.draw_path(
			line_id,
			ColorRgba::black(),
			ColorRgba::black(),
			true,
			false).has_value());

		const auto dl = *builder.finish();
		EXPECT_FALSE(dl->has_content_bounds());
	}

	// 2. Odd-length dash pattern `[10]` (doubled to `[10, 10]` per ISO 32000-1
	//    §8.4.3.6) with `dash_phase = 5` along `(0, 50) -> (25, 50)`:
	//    - `[0, 5]` is on (remaining 5 of first 10-on)
	//    - `[5, 15]` is off (10-off)
	//    - `[15, 25]` is on (second 10-on)
	//    Clipping to `[6, 99] x [0, 100]` excludes `[0, 5]` so X bounds are `[15, 25]`.
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(6.0, 0.0, 94.0, 100.0);
		const uint32_t clip_id = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip_id).has_value());

		PathBuilder line_pb;
		line_pb.move_to(0.0, 50.0);
		line_pb.line_to(25.0, 50.0);
		const uint32_t line_id = *builder.add_path(line_pb);
		const float odd_dash[] = {10.0f};
		ASSERT_TRUE(builder.set_stroke_style(
			2.0f,
			LineCap::Butt,
			LineJoin::Miter,
			10.0f,
			odd_dash,
			5.0f).has_value());
		ASSERT_TRUE(builder.draw_path(
			line_id,
			ColorRgba::black(),
			ColorRgba::black(),
			true,
			false).has_value());

		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_NEAR(dl->content_bounds_pt().x_min, 15.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().x_max, 25.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_min, 49.0, 1e-6);
		EXPECT_NEAR(dl->content_bounds_pt().y_max, 51.0, 1e-6);
	}
}

TEST(PdfDisplayList, GlyphRunInterGlyphClipAndImageMaskCoverage)
{
	// 1. Two-glyph run with glyph 0 at `[10, 20]` and glyph 1 at `[80, 90]`,
	//    clipped to the inter-glyph gap `[35, 65] x [0, 100]` produces no ink:
	{
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		PathBuilder clip_pb;
		clip_pb.rect(35.0, 0.0, 30.0, 100.0);
		const uint32_t clip_id = *builder.add_path(clip_pb);
		ASSERT_TRUE(builder.push_clip_path(clip_id).has_value());

		const PositionedGlyph glyphs[] = {
			PositionedGlyph{1u, U'A', PdfPoint{10.0, 50.0}, 10.0, PdfRect{0.0, 0.0, 10.0, 12.0}},
			PositionedGlyph{2u, U'B', PdfPoint{80.0, 50.0}, 10.0, PdfRect{0.0, 0.0, 10.0, 12.0}},
		};
		ASSERT_TRUE(builder.draw_glyph_run(
			glyphs,
			12.0,
			TextRenderMode::Fill,
			ColorRgba::black(),
			ColorRgba::black()).has_value());

		const auto dl = *builder.finish();
		EXPECT_FALSE(dl->has_content_bounds());
	}

	// 2. Image with disjoint non-transparent alpha against soft_mask:
	//    2x1 bitmap has alpha `[255, 0]`, 2x1 soft_mask has alpha `[0, 255]`.
	//    Combined effective alpha is `[0, 0]`, so `has_content_bounds()` is false:
	{
		std::vector<uint8_t> bmp_px = {
			0xFFu, 0xFFu, 0xFFu, 0xFFu, // pixel 0: alpha 255
			0x00u, 0x00u, 0x00u, 0x00u, // pixel 1: alpha 0
		};
		std::vector<uint8_t> mask_px = {
			0x00u, 0x00u, 0x00u, 0x00u, // pixel 0: alpha 0
			0xFFu, 0xFFu, 0xFFu, 0xFFu, // pixel 1: alpha 255
		};
		auto bmp = *DecodedBitmap::create(2u, 1u, 8u, std::move(bmp_px));
		auto mask = *DecodedBitmap::create(2u, 1u, 8u, std::move(mask_px));

		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		ASSERT_TRUE(builder.draw_image(
			bmp,
			PdfMatrix{100.0, 0.0, 0.0, 50.0, 10.0, 20.0},
			true,
			mask).has_value());
		const auto dl = *builder.finish();
		EXPECT_FALSE(dl->has_content_bounds());
	}

	// 3. Image with transparent outer border (4x4 bitmap where only the central
	//    2x2 pixels `[1..3, 1..3]` are opaque) mapped to `[0, 0, 100, 100]`
	//    reports tight bounds `[25, 25, 75, 75]`:
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
		auto bmp = *DecodedBitmap::create(4u, 4u, 16u, std::move(px));
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0});
		ASSERT_TRUE(builder.draw_image(
			bmp,
			PdfMatrix{100.0, 0.0, 0.0, 100.0, 0.0, 0.0}).has_value());
		const auto dl = *builder.finish();
		ASSERT_TRUE(dl->has_content_bounds());
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_min, 25.0);
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_min, 25.0);
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_max, 75.0);
		EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_max, 75.0);
	}
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
		EXPECT_EQ(third.error().code, ErrorCode::DamagedFormat);
	}

	// 2. `max_geometry_work_ops` bounds scanbeam / intersection work:
	{
		DisplayListLimits limits{};
		limits.max_geometry_work_ops = 10u;
		DisplayListBuilder builder(PdfRect{0.0, 0.0, 200.0, 200.0}, limits);

		PathBuilder pb;
		pb.rect(10.0, 10.0, 100.0, 100.0);
		const uint32_t pid = *builder.add_path(pb);
		const auto res = builder.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true);
		ASSERT_FALSE(res.has_value());
		EXPECT_EQ(res.error().code, ErrorCode::DamagedFormat);
		EXPECT_EQ(builder.command_count(), 0u);
	}

	// 3. Deterministic allocation failure during `draw_path()` leaves command count
	//    and content bounds unchanged, and failure during `finish()` latches the
	//    builder so a retry cannot publish an empty DisplayList with stale bounds:
	{
		DisplayListLimits draw_fail_limits{};
		draw_fail_limits.fail_allocation_after_ops = 2u; // op 1 = add_path, op 2 = draw_path region clip
		DisplayListBuilder b1(PdfRect{0.0, 0.0, 200.0, 200.0}, draw_fail_limits);
		PathBuilder pb;
		pb.rect(10.0, 10.0, 50.0, 50.0);
		const uint32_t pid = *b1.add_path(pb);
		const auto draw_res = b1.draw_path(pid, ColorRgba::black(), ColorRgba::black(), false, true);
		ASSERT_FALSE(draw_res.has_value());
		EXPECT_EQ(draw_res.error().code, ErrorCode::OutOfMemory);
		EXPECT_EQ(b1.command_count(), 0u);

		DisplayListLimits finish_fail_limits{};
		finish_fail_limits.fail_allocation_after_ops = 4u; // 1=add_path, 2=clip_region, 3=draw_path cmd, 4=finish
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

} // namespace
} // namespace recto::pdf
