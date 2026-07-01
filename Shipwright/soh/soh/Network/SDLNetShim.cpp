// SDL3-MIGRATION (Phase 1): no-op SDL_net stubs. See SDLNetShim.h for the why. Every entry point
// reports failure so the networking code (Network::Enable, etc.) cleanly aborts and the feature
// stays inert at runtime. No SDL2 headers are pulled, so the SDL3 build is not poisoned.
//
// STOPGAP: replace with a real native/SDL3 networking transport to restore Anchor/CrowdControl/Sail.

#include "soh/Network/SDLNetShim.h"

extern "C" {

static const char* kSohNetDisabled = "networking disabled in Zelda3D SDL3 build (no SDL3_net)";

int SDLNet_Init(void) {
    return 0; // 0 = "success" so SDLNet_Init() at startup is a harmless no-op
}

void SDLNet_Quit(void) {
}

const char* SDLNet_GetError(void) {
    return kSohNetDisabled;
}

int SDLNet_ResolveHost(IPaddress* address, const char* /*host*/, uint16_t port) {
    if (address != nullptr) {
        address->host = 0;
        address->port = port;
    }
    return -1; // -1 = failure -> callers bail out of connecting
}

TCPsocket SDLNet_TCP_Open(IPaddress* /*ip*/) {
    return nullptr; // never connects
}

void SDLNet_TCP_Close(TCPsocket /*sock*/) {
}

int SDLNet_TCP_Send(TCPsocket /*sock*/, const void* /*data*/, int /*len*/) {
    return 0; // 0 bytes sent (< len) -> treated as send failure
}

int SDLNet_TCP_Recv(TCPsocket /*sock*/, void* /*data*/, int /*maxlen*/) {
    return -1; // <= 0 -> connection closed/error
}

SDLNet_SocketSet SDLNet_AllocSocketSet(int /*maxsockets*/) {
    return nullptr;
}

void SDLNet_FreeSocketSet(SDLNet_SocketSet /*set*/) {
}

int SDLNet_TCP_AddSocket(SDLNet_SocketSet /*set*/, TCPsocket /*sock*/) {
    return -1;
}

int SDLNet_CheckSockets(SDLNet_SocketSet /*set*/, uint32_t /*timeout*/) {
    return -1; // error
}

} // extern "C"
