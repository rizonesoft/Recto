// Scaffold smoke tests (D00 T01 section 3): the first occupants of the
// suite every later domain joins. They prove the harness can fail, that
// GoogleTest arrives only through vcpkg manifest mode, and that the
// module link chain services -> core -> pdf is real. Suite naming
// follows the plan's checkpoints (Pdf.*, Actor.*, Render.*, ... as
// those domains land).
#include "recto_core.h"
#include "recto_pdf.h"
#include "recto_services.h"

#include <gtest/gtest.h>

// D14 (revised 2026-10-22): every Recto project compiles past C++20, through
// /std:c++23preview until MSVC ships a stable C++23 or C++26 switch.
static_assert(_MSVC_LANG > 202002L, "Recto projects compile as C++23 or later (D14)");

TEST(Toolchain, CompilesPastCpp20)
{
	EXPECT_GT(_MSVC_LANG, 202002L);
}

TEST(Scaffold, PdfEngineReportsAbiVersionOne)
{
	EXPECT_EQ(recto::pdf::abi_version(), 1);
}

TEST(Scaffold, CoreReportsThePdfEngineAbi)
{
	EXPECT_EQ(recto::core::abi_version(), recto::pdf::abi_version());
}

TEST(Scaffold, ServicesReportTheCoreAbi)
{
	EXPECT_EQ(recto::services::abi_version(), recto::core::abi_version());
}
