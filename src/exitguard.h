#pragma once
#include <functional>

namespace sc {

void installExitHook(std::function<void()> fn);

}
