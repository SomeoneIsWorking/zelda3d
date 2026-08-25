#include "first_div_reporter.h"

#include <cstdio>

namespace HarnessOracle {

bool FirstDivReporter::Reported() const {
    return reported_;
}

void FirstDivReporter::Report(const char* field, const std::string& details) {
    if (reported_) {
        return;
    }
    std::printf("  firstdiv: %s %s\n", field, details.c_str());
    reported_ = true;
}

} // namespace HarnessOracle
