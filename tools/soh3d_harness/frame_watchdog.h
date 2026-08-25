#pragma once

namespace HarnessWatchdog {

void Install();

class Frame {
  public:
    explicit Frame(const char* operation);
    ~Frame();

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

} // namespace HarnessWatchdog
