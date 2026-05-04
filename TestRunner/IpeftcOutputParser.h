#pragma once

#include "Message.h"

#include <string>

namespace TestRunner {

class IpeftcOutputParser {
public:
  static TestResult Parse(const std::string &output, const std::string &role,
                          int port);
};

} // namespace TestRunner
