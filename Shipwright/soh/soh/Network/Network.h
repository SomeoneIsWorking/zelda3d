#ifndef NETWORK_H
#define NETWORK_H
#ifdef __cplusplus

#include <thread>
// SDL3-MIGRATION: SDL2_net is ABI-incompatible with SDL3 (shared SDL_h_ guard); use the no-op
// shim so the networking code compiles/links with the feature disabled at runtime. See SDLNetShim.h.
#include "soh/Network/SDLNetShim.h"
#include <nlohmann/json.hpp>

class Network {
  private:
    IPaddress networkAddress;
    TCPsocket networkSocket;
    std::thread receiveThread;
    std::string receivedData;

    void ReceiveFromServer();
    void HandleRemoteData(char payload[512]);
    void HandleRemoteJson(std::string payload);

  public:
    // Initialised, because nothing else does. There is no Network constructor and no subclass sets
    // these, so `new CrowdControl()` left both indeterminate -- and they gate `Enable()`'s early
    // return, the menu's Enable/Disable label, and `Disable()`'s `receiveThread.join()`. A garbage
    // `true` there joins a thread that was never started, which is std::terminate, not a leak.
    bool isEnabled = false;
    bool isConnected = false;

    void Enable(const char* host, uint16_t port);
    void Disable();
    /**
     * Raw data handler
     *
     * If you are developing a new remote, you should probably use the json methods instead. This
     * method requires you to parse the data and ensure packets are complete manually, we cannot
     * gaurentee that the data will be complete, or that it will only contain one packet with this
     */
    virtual void OnIncomingData(char payload[512]);
    /**
     * Json handler
     *
     * This method will be called when a complete json packet is received. All json packets must
     * be delimited by a null terminator (\0).
     */
    virtual void OnIncomingJson(nlohmann::json payload);
    virtual void OnConnected();
    virtual void OnDisconnected();
    virtual void ProcessOutgoingPackets();
    void SendDataToRemote(const char* payload);
    virtual void SendJsonToRemote(nlohmann::json packet);
};

#endif // __cplusplus
#endif // NETWORK_H
