#pragma once

#include "../../../config/macros.h"

namespace lcf::async::details {

template <typename Handle>
struct ScheduleSender;

template <typename Handle, typename Receiver>
class ScheduleOperation;

} // namespace lcf::async::details
