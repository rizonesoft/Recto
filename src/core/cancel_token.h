// `recto::pdf`'s cooperative cancellation for the runtime (D01 T02 section 7): defined in `src/pdf`, so that engine never
// includes upward.
#pragma once

#include "../pdf/cancel_token.h"

namespace recto::core {

using recto::pdf::CancelSource;
using recto::pdf::CancelToken;

} // namespace recto::core
