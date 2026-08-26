#pragma once

namespace HarnessWatchdog {

void Install();
void Pulse();

class Frame {
  public:
    explicit Frame(const char* operation);
    ~Frame();

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

} // namespace HarnessWatchdog
