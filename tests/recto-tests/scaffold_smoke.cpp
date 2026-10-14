// Scaffold smoke tests (D00 T01 section 3): the first occupants of the
// suite every later domain joins. They prove the harness can fail, that
// GoogleTest arrives only through vcpkg manifest mode, and that the
// module link chain services -> core -> bridge is real. Suite naming
// follows the plan's checkpoints (Bridge.*, Actor.*, Render.*, ... as
// those domains land).
#include "recto_bridge.h"
#include "recto_core.h"
#include "recto_services.h"

#include <gtest/gtest.h>

TEST(Scaffold, BridgeReportsAbiVersionOne)
{
	EXPECT_EQ(rc_bridge_abi_version(), 1);
}

TEST(Scaffold, CoreReportsTheBridgeAbi)
{
	EXPECT_EQ(recto::core::abi_version(), rc_bridge_abi_version());
}

TEST(Scaffold, ServicesReportTheCoreAbi)
{
	EXPECT_EQ(recto::services::abi_version(), recto::core::abi_version());
}
