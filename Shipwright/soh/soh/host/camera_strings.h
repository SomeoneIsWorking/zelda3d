#pragma once

// db_camera mutates this table in place, so each game run receives a fresh heap copy.
void Zelda3D_InitializeCameraStrings();
void Zelda3D_FreeCameraStrings();
