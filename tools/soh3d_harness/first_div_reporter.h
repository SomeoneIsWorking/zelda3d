#pragma once

#include <string>

namespace HarnessOracle {

class FirstDivReporter {
  public:
    bool Reported() const;
    void Report(const char* field, const std::string& details);

  private:
    bool reported_ = false;
};

} // namespace HarnessOracle
