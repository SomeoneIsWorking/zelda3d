#pragma once

#include <sstream>

namespace HarnessOracle {

void HandlePlayState(std::istringstream& arguments);
void HandleGameplay(std::istringstream& arguments);
void HandleScene(std::istringstream& arguments);
void HandleWarp(std::istringstream& arguments);
void HandleSohWarp(std::istringstream& arguments, bool sohBooted);
void HandleSohSetAge(std::istringstream& arguments, bool sohBooted);
void HandleSohGetAge(std::istringstream& arguments, bool sohBooted);

} // namespace HarnessOracle
