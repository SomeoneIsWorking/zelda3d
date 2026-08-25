#include "headless_window_backend.h"

namespace Zelda3D::DlistHarness {

HeadlessWindowBackend::HeadlessWindowBackend(uint32_t width, uint32_t height) : mWidth(width), mHeight(height) {
}

void HeadlessWindowBackend::Init(const char*, const char*, bool, uint32_t, uint32_t, int32_t, int32_t) {
}
void HeadlessWindowBackend::Close() {
}
void HeadlessWindowBackend::SetKeyboardCallbacks(bool (*)(int), bool (*)(int), void (*)()) {
}
void HeadlessWindowBackend::SetMouseCallbacks(bool (*)(int), bool (*)(int)) {
}
void HeadlessWindowBackend::SetFullscreenChangedCallback(void (*)(bool)) {
}
void HeadlessWindowBackend::SetFullscreen(bool) {
}
void HeadlessWindowBackend::GetActiveWindowRefreshRate(uint32_t* refreshRate) {
    *refreshRate = 60;
}
void HeadlessWindowBackend::SetCursorVisibility(bool) {
}
void HeadlessWindowBackend::SetMousePos(int32_t, int32_t) {
}
void HeadlessWindowBackend::GetMousePos(int32_t* x, int32_t* y) {
    *x = 0;
    *y = 0;
}
void HeadlessWindowBackend::GetMouseDelta(int32_t* x, int32_t* y) {
    *x = 0;
    *y = 0;
}
void HeadlessWindowBackend::GetMouseWheel(float* x, float* y) {
    *x = 0;
    *y = 0;
}
bool HeadlessWindowBackend::GetMouseState(uint32_t) {
    return false;
}
void HeadlessWindowBackend::SetMouseCapture(bool) {
}
bool HeadlessWindowBackend::IsMouseCaptured() {
    return false;
}
void HeadlessWindowBackend::GetDimensions(uint32_t* width, uint32_t* height, int32_t* positionX, int32_t* positionY) {
    *width = mWidth;
    *height = mHeight;
    *positionX = 0;
    *positionY = 0;
}
void HeadlessWindowBackend::SetDimensions(uint32_t, uint32_t, int32_t, int32_t) {
}
Ship::WindowRect HeadlessWindowBackend::GetPrimaryMonitorRect() {
    return { 0, 0, static_cast<int32_t>(mWidth), static_cast<int32_t>(mHeight) };
}
void HeadlessWindowBackend::HandleEvents() {
}
bool HeadlessWindowBackend::IsFrameReady() {
    return true;
}
void HeadlessWindowBackend::SwapBuffersBegin() {
}
void HeadlessWindowBackend::SwapBuffersEnd() {
}
double HeadlessWindowBackend::GetTime() {
    return 0.0;
}
int HeadlessWindowBackend::GetTargetFps() {
    return 60;
}
void HeadlessWindowBackend::SetTargetFps(int) {
}
void HeadlessWindowBackend::SetMaxFrameLatency(int) {
}
const char* HeadlessWindowBackend::GetKeyName(int) {
    return "";
}
bool HeadlessWindowBackend::CanDisableVsync() {
    return true;
}
bool HeadlessWindowBackend::IsRunning() {
    return true;
}
void HeadlessWindowBackend::Destroy() {
}
bool HeadlessWindowBackend::IsFullscreen() {
    return false;
}

} // namespace Zelda3D::DlistHarness
