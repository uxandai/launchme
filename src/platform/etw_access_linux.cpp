// Linux: there is no ETW; the FPS widget is Windows-only, so nothing is ever missing or grantable.
#include "etw_access.hpp"

namespace etw_access {
State state() { return State::unknown; }
bool grant() { return false; }
}
