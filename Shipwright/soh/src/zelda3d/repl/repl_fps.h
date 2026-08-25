#ifndef ZELDA3D_REPL_FPS_H
#define ZELDA3D_REPL_FPS_H

namespace Zelda3D::Repl {

void TickFps();
void ResetFps();

} // namespace Zelda3D::Repl

extern "C" {

double Zelda3D_ReplLogicFps(void);
int Zelda3D_ReplLogicFpsSamples(void);
double Zelda3D_ReplLogicFpsWindow(void);
}

#endif // ZELDA3D_REPL_FPS_H
