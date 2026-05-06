#pragma once

#include <string>

struct RunOptions {
    std::string runId;
    std::string resultDir = "Results";
    std::string resultJson;
    std::string resultPipe;
};
