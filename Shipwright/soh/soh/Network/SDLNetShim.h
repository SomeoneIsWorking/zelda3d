#ifndef SOH3D_SDLNET_SHIM_H
#define SOH3D_SDLNET_SHIM_H

// SDL3-MIGRATION (Phase 1): SoH's networking (Anchor co-op / CrowdControl / Sail) was built on
// SDL2_net. There is no SDL3_net on this system, and SDL2_net cannot be linked into an SDL3 binary
// (SDL2 and SDL3 both define the `SDL_h_` master include guard, so pulling SDL2_net's <SDL2/SDL.h>
// silently neutralizes every later <SDL3/SDL.h> — which is exactly how the whole soh tree failed to
// see SDL_Gamepad). Rather than #ifdef-out the ~30 Network/Anchor translation units and their many
// call sites in OTRGlobals/SohGui, we replace the SDL2_net dependency with this tiny shim: it
// declares the exact SDL_net surface the code uses, backed by no-op stubs (see SDLNetShim.cpp) that
// report failure. The networking feature is therefore compiled and link-clean but DISABLED at
// runtime. Restoring it is a follow-up: port the sockets to a native/SDL3 transport (or an
// eventual SDL3_net) and delete this shim.
//
// STOPGAP: networking disabled because SDL2_net is ABI-incompatible with SDL3; proper fix is a
// native/SDL3 networking transport.

#include <cstdint>

extern "C" {

typedef struct {
    uint32_t host; // 32-bit IPv4 host address (network byte order in real SDL_net)
    uint16_t port; // 16-bit protocol port (network byte order in real SDL_net)
} IPaddress;

typedef struct SoH3dNetTCPsocket* TCPsocket;
typedef struct SoH3dNetSocketSet* SDLNet_SocketSet;

int SDLNet_Init(void);
void SDLNet_Quit(void);
const char* SDLNet_GetError(void);

int SDLNet_ResolveHost(IPaddress* address, const char* host, uint16_t port);

TCPsocket SDLNet_TCP_Open(IPaddress* ip);
void SDLNet_TCP_Close(TCPsocket sock);
int SDLNet_TCP_Send(TCPsocket sock, const void* data, int len);
int SDLNet_TCP_Recv(TCPsocket sock, void* data, int maxlen);

SDLNet_SocketSet SDLNet_AllocSocketSet(int maxsockets);
void SDLNet_FreeSocketSet(SDLNet_SocketSet set);
int SDLNet_TCP_AddSocket(SDLNet_SocketSet set, TCPsocket sock);
int SDLNet_CheckSockets(SDLNet_SocketSet set, uint32_t timeout);

} // extern "C"

#endif // SOH3D_SDLNET_SHIM_H
