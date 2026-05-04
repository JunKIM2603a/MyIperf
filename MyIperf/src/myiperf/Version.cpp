#include "myiperf/Version.h"

#include "myiperf/VersionGenerated.h"

#include <sstream>

#ifndef MYIPERF_BUILD_CONFIG
#define MYIPERF_BUILD_CONFIG "unknown"
#endif

namespace myiperf {

std::string versionString() {
    return std::string(MYIPERF_PRODUCT_NAME) + " " + MYIPERF_VERSION_STRING;
}

std::string buildInfoString() {
    std::ostringstream oss;
    oss << versionString() << '\n'
        << "git: " << MYIPERF_GIT_BRANCH << " " << MYIPERF_GIT_COMMIT
        << " " << (MYIPERF_GIT_DIRTY ? "dirty" : "clean") << '\n'
        << "build: " << MYIPERF_BUILD_CONFIG << " " << MYIPERF_BUILD_TIMESTAMP;
    return oss.str();
}

} // namespace myiperf
