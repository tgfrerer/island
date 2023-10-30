#ifndef LE_TIMEBASE_TICK_TYPE_H
#define LE_TIMEBASE_TICK_TYPE_H

namespace le {
using Ticks = std::chrono::duration<uint64_t, std::ratio<1, LE_TIME_TICKS_PER_SECOND>>; /// LE_TIME_TICKS_PER_SECOND ticks per second.
}

#endif // LE_TIMEBASE_TICK_TYPE_H
