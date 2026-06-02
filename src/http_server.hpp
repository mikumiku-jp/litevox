#pragma once

#include "runtime.hpp"

#include <vector>

struct RuntimeWorker {
    RuntimeState *runtimeState;
};

void serve(RuntimeState &runtimeState, int port);
void serveRuntimePool(std::vector<RuntimeWorker> runtimeWorkers, int port);
