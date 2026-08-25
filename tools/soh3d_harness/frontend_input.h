#pragma once

#include <cstdint>
#include <sstream>

namespace HarnessFrontend {

void InputPoll();
int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id);

uint32_t InputMask();
void SetInputMask(uint32_t mask);
uint64_t InputPollCount();
uint32_t InputIdsSeen();
void SetAnalog(int16_t leftX, int16_t leftY, int16_t rightX, int16_t rightY);
void GetAnalog(int16_t* leftX, int16_t* leftY, int16_t* rightX, int16_t* rightY);

void HandleInput(std::istringstream& arguments);

} // namespace HarnessFrontend
