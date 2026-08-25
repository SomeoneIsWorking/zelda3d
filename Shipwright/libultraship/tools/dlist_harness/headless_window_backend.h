#pragma once

#include <cstdint>

#include <fast/backends/gfx_window_manager_api.h>

namespace Zelda3D::DlistHarness {

class HeadlessWindowBackend final : public Fast::GfxWindowBackend {
  public:
    HeadlessWindowBackend(uint32_t width, uint32_t height);

    void Init(const char*, const char*, bool, uint32_t, uint32_t, int32_t, int32_t) override;
    void Close() override;
    void SetKeyboardCallbacks(bool (*)(int), bool (*)(int), void (*)()) override;
    void SetMouseCallbacks(bool (*)(int), bool (*)(int)) override;
    void SetFullscreenChangedCallback(void (*)(bool)) override;
    void SetFullscreen(bool) override;
    void GetActiveWindowRefreshRate(uint32_t* refreshRate) override;
    void SetCursorVisibility(bool) override;
    void SetMousePos(int32_t, int32_t) override;
    void GetMousePos(int32_t* x, int32_t* y) override;
    void GetMouseDelta(int32_t* x, int32_t* y) override;
    void GetMouseWheel(float* x, float* y) override;
    bool GetMouseState(uint32_t) override;
    void SetMouseCapture(bool) override;
    bool IsMouseCaptured() override;
    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* positionX, int32_t* positionY) override;
    void SetDimensions(uint32_t, uint32_t, int32_t, int32_t) override;
    Ship::WindowRect GetPrimaryMonitorRect() override;
    void HandleEvents() override;
    bool IsFrameReady() override;
    void SwapBuffersBegin() override;
    void SwapBuffersEnd() override;
    double GetTime() override;
    int GetTargetFps() override;
    void SetTargetFps(int) override;
    void SetMaxFrameLatency(int) override;
    const char* GetKeyName(int) override;
    bool CanDisableVsync() override;
    bool IsRunning() override;
    void Destroy() override;
    bool IsFullscreen() override;

  private:
    uint32_t mWidth;
    uint32_t mHeight;
};

} // namespace Zelda3D::DlistHarness
