#include "Version.h"

#include "testrunner/VersionGenerated.h"

#include <sstream>

#ifndef TESTRUNNER_BUILD_CONFIG
#define TESTRUNNER_BUILD_CONFIG "unknown"
#endif

namespace TestRunner {

std::string VersionString() {
  return std::string(TESTRUNNER_PRODUCT_NAME) + " " +
         TESTRUNNER_VERSION_STRING;
}

std::string BuildInfoString() {
  std::ostringstream oss;
  oss << VersionString() << '\n'
      << "git: " << TESTRUNNER_GIT_BRANCH << " " << TESTRUNNER_GIT_COMMIT
      << " " << (TESTRUNNER_GIT_DIRTY ? "dirty" : "clean") << '\n'
      << "build: " << TESTRUNNER_BUILD_CONFIG << " "
      << TESTRUNNER_BUILD_TIMESTAMP;
  return oss.str();
}

} // namespace TestRunner
