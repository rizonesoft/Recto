// Unit tests for `recto::pdf::DisplayList` and `recto::pdf::DisplayListBuilder`
// (D01 T02 section 1).
#include "display_list.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <latch>
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
static_assert(!std::is_copy_constructible_v<DecodedBitmap>);
static_assert(!std::is_move_constructible_v<DecodedBitmap>);

TEST(PdfDisplayList, CommandRecordingAndContiguousStorage)
{
	EXPECT_EQ(debug_live_display_lists(), 0);

	DisplayListBuilder builder(PdfRect{0.0, 0.0, 612.0, 792.0});
	builder.reserve_commands(16u);

	PathBuilder pb1;
	pb1.rect(50.0, 60.0, 100.0, 80.0);
	const uint32_t path0 = builder.add_path(pb1);
	EXPECT_EQ(path0, 0u);

	PathBuilder pb2;
	pb2.move_to(10.0, 20.0);
	pb2.line_to(40.0, 60.0);
	pb2.close_subpath();
	const uint32_t path1 = builder.add_path(pb2);
	EXPECT_EQ(path1, 1u);

	builder.save_state();
	builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 10.0, 20.0});
	const float dashes[] = {4.0f, 2.0f};
	builder.set_stroke_style(2.5f, LineCap::Round, LineJoin::Bevel, 8.0f, dashes, 1.0f);
	builder.set_alpha_and_blend(0.8f, 0.5f, BlendMode::Multiply);
	builder.push_clip_path(path0, FillRule::EvenOdd);
	builder.draw_path(
		path1,
		ColorRgba{0.1f, 0.2f, 0.3f, 1.0f},
		ColorRgba{0.4f, 0.5f, 0.6f, 1.0f},
		true,
		true,
		FillRule::NonZero);

	const PositionedGlyph run_glyphs[] = {
		PositionedGlyph{42u, U'A', PdfPoint{100.0, 200.0}, 7.5},
		PositionedGlyph{43u, U'B', PdfPoint{107.5, 200.0}, 8.0},
	};
	builder.draw_glyph_run(
		run_glyphs,
		14.0,
		TextRenderMode::Fill,
		ColorRgba::black(),
		ColorRgba::white());

	std::vector<uint8_t> pixels(16u, 0xFFu);
	auto bmp_res = DecodedBitmap::create(2u, 2u, 8u, std::move(pixels), false);
	ASSERT_TRUE(bmp_res.has_value()) << bmp_res.error().message.view();
	builder.draw_image(
		*bmp_res,
		PdfMatrix{32.0, 0.0, 0.0, 24.0, 70.0, 80.0},
		true);
	builder.restore_state();

	auto dl = builder.finish();
	ASSERT_NE(dl, nullptr);
	EXPECT_TRUE(builder.is_finished());
	EXPECT_EQ(debug_live_display_lists(), 1);

	// Subsequent calls after `finish()` do not mutate `dl`, and calling
	// `finish()` again while `dl` is alive returns the same shared instance:
	builder.save_state();
	builder.restore_state();
	auto dl_again = builder.finish();
	EXPECT_EQ(dl.get(), dl_again.get());
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
	ASSERT_EQ(dash_span.size(), 2u);
	EXPECT_FLOAT_EQ(dash_span[0], 4.0f);
	EXPECT_FLOAT_EQ(dash_span[1], 2.0f);

	const auto* glyph_cmd = dl->commands()[6].as<DrawGlyphRunCommand>();
	ASSERT_NE(glyph_cmd, nullptr);
	const auto glyph_span = dl->glyphs(*glyph_cmd);
	ASSERT_EQ(glyph_span.size(), 2u);
	EXPECT_EQ(glyph_span[0].glyph_index, 42u);
	EXPECT_EQ(glyph_span[0].unicode_codepoint, static_cast<uint32_t>(U'A'));
	EXPECT_EQ(glyph_span[1].glyph_index, 43u);
	EXPECT_EQ(glyph_span[1].unicode_codepoint, static_cast<uint32_t>(U'B'));
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
	const uint32_t idx0 = builder.add_path(pb0);

	// Path 1: cubic Bezier arch from (0, 0) to (100, 0) with control points
	// (0, 100) and (100, 100). Analytic peak at t = 0.5 is y = 75.0 (strictly
	// tighter than the control-point hull y = 100.0):
	PathBuilder pb1;
	pb1.move_to(0.0, 0.0);
	pb1.cubic_bezier_to(PdfPoint{0.0, 100.0}, PdfPoint{100.0, 100.0}, PdfPoint{100.0, 0.0});
	const uint32_t idx1 = builder.add_path(pb1);

	auto dl = builder.finish();
	ASSERT_EQ(dl->paths().size(), 2u);

	const RecordedPath& p0 = dl->path(idx0);
	EXPECT_TRUE(p0.has_bounds);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.x_min, 10.0);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.y_min, 15.0);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.x_max, 100.0);
	EXPECT_DOUBLE_EQ(p0.bounds_pt.y_max, 230.0);
	EXPECT_EQ(dl->path_verbs(p0).size(), 5u);
	EXPECT_EQ(dl->path_points(p0).size(), 5u);

	const RecordedPath& p1 = dl->path(idx1);
	EXPECT_TRUE(p1.has_bounds);
	EXPECT_DOUBLE_EQ(p1.bounds_pt.x_min, 0.0);
	EXPECT_DOUBLE_EQ(p1.bounds_pt.y_min, 0.0);
	EXPECT_DOUBLE_EQ(p1.bounds_pt.x_max, 100.0);
	EXPECT_NEAR(p1.bounds_pt.y_max, 75.0, 1e-9);
	EXPECT_EQ(dl->path_verbs(p1).size(), 2u);
	EXPECT_EQ(dl->path_points(p1).size(), 4u);

	// Shared pools hold the concatenated verbs and points of both paths:
	EXPECT_EQ(dl->path_verbs().size(), 7u);
	EXPECT_EQ(dl->path_points().size(), 9u);
}

TEST(PdfDisplayList, DecodedBitmapZeroCopySharing)
{
	// Invalid dimensions or strides are rejected with `ErrorCode::DamagedFormat`:
	EXPECT_FALSE(DecodedBitmap::create(0u, 10u, 40u, std::vector<uint8_t>(400u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(10u, 10u, 36u, std::vector<uint8_t>(360u)).has_value());
	EXPECT_FALSE(DecodedBitmap::create(10u, 10u, 40u, std::vector<uint8_t>(399u)).has_value());

	std::vector<uint8_t> raw_pixels(64u * 4u, 0xABu);
	const uint8_t* original_ptr = raw_pixels.data();
	auto bmp_res = DecodedBitmap::create(8u, 8u, 32u, std::move(raw_pixels), true);
	ASSERT_TRUE(bmp_res.has_value()) << bmp_res.error().message.view();
	const auto& bmp = *bmp_res;
	EXPECT_EQ(bmp->span().data(), original_ptr);
	EXPECT_TRUE(bmp->is_monochrome_mask);

	DisplayListBuilder builder;
	builder.draw_image(bmp, PdfMatrix{100.0, 0.0, 0.0, 100.0, 10.0, 20.0});
	builder.draw_image(bmp, PdfMatrix{50.0, 0.0, 0.0, 50.0, 200.0, 300.0});
	auto dl = builder.finish();

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
	const uint32_t ignored_path = builder.add_path(ignored_pb);
	builder.draw_path(ignored_path, ColorRgba::black(), ColorRgba::black(), false, false);

	// 2. Clipped path inside a saved state:
	builder.save_state();
	PathBuilder clip_pb;
	clip_pb.rect(50.0, 60.0, 200.0, 200.0); // clip window [50, 60, 250, 260]
	const uint32_t clip_path = builder.add_path(clip_pb);
	builder.push_clip_path(clip_path);

	PathBuilder wide_pb;
	wide_pb.rect(20.0, 40.0, 300.0, 100.0); // unclipped [20, 40, 320, 140] -> clipped [50, 60, 250, 140]
	const uint32_t wide_path = builder.add_path(wide_pb);
	builder.draw_path(wide_path, ColorRgba::black(), ColorRgba::black(), false, true);
	builder.restore_state();

	// 3. Transformed image outside the restored clip:
	std::vector<uint8_t> px(16u, 0xFFu);
	auto bmp = *DecodedBitmap::create(2u, 2u, 8u, std::move(px));
	builder.save_state();
	builder.concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, 300.0, 400.0});
	builder.draw_image(bmp, PdfMatrix{80.0, 0.0, 0.0, 50.0, 0.0, 0.0}); // [300, 400, 380, 450]
	builder.restore_state();

	// 4. Invisible (`Tr=3`) glyph run does not expand content bounds, while
	//    visible (`Tr=0`) glyph run does:
	const PositionedGlyph inv_glyphs[] = {
		PositionedGlyph{1u, U'X', PdfPoint{10.0, 10.0}, 20.0},
	};
	builder.draw_glyph_run(
		inv_glyphs,
		24.0,
		TextRenderMode::Invisible,
		ColorRgba::black(),
		ColorRgba::black());

	const PositionedGlyph vis_glyphs[] = {
		PositionedGlyph{2u, U'H', PdfPoint{100.0, 500.0}, 15.0},
	};
	builder.draw_glyph_run(
		vis_glyphs,
		20.0,
		TextRenderMode::Fill,
		ColorRgba::black(),
		ColorRgba::black()); // [100, 500, 115, 520]

	auto dl = builder.finish();
	ASSERT_TRUE(dl->has_content_bounds());
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_min, 50.0);
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_min, 60.0);
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().x_max, 380.0);
	EXPECT_DOUBLE_EQ(dl->content_bounds_pt().y_max, 520.0);
}

TEST(PdfDisplayList, LiveDisplayListsCounter)
{
	EXPECT_EQ(debug_live_display_lists(), 0);
	{
		DisplayListBuilder b1;
		DisplayListBuilder b2;
		auto dl1 = b1.finish();
		EXPECT_EQ(debug_live_display_lists(), 1);
		{
			auto dl2 = b2.finish();
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
	builder.reserve_commands(kTotalCommands);

	PathBuilder pb;
	pb.move_to(10.0, 10.0);
	pb.line_to(100.0, 10.0);
	pb.line_to(100.0, 80.0);
	pb.close_subpath();
	const uint32_t path_id = builder.add_path(pb);

	const PositionedGlyph glyphs[] = {
		PositionedGlyph{10u, U'R', PdfPoint{72.0, 700.0}, 8.0},
		PositionedGlyph{11u, U'e', PdfPoint{80.0, 700.0}, 6.0},
	};
	const float dashes[] = {3.0f, 1.0f};

	// Record 10,000 commands (2,000 groups of 5 commands):
	for (size_t i = 0u; i < kTotalCommands / 5u; ++i) {
		builder.save_state();
		builder.set_stroke_style(1.5f, LineCap::Butt, LineJoin::Miter, 10.0f, dashes, 0.0f);
		builder.draw_path(path_id, ColorRgba::black(), ColorRgba::white(), true, true);
		builder.draw_glyph_run(glyphs, 12.0, TextRenderMode::Fill, ColorRgba::black(), ColorRgba::black());
		builder.restore_state();
	}

	auto dl = builder.finish();
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
					const RecordedPath& p = shared_dl->path(dp->path_index);
					acc += shared_dl->path_verbs(p).size() + shared_dl->path_points(p).size();
				} else if (const auto* gr = cmd.as<DrawGlyphRunCommand>()) {
					for (const PositionedGlyph& g : shared_dl->glyphs(*gr)) {
						acc += g.glyph_index + g.unicode_codepoint;
					}
				} else if (const auto* ss = cmd.as<SetStrokeStyleCommand>()) {
					acc += shared_dl->dash_values(*ss).size();
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

} // namespace
} // namespace recto::pdf
