#pragma once
#include <pspgu.h>
#include <pspmath.h>
#include <stdint.h>

// Basic sceGu wrapper

bool PSPRenderer_Init();
void PSPRenderer_BeginFrame(uint32_t skyColor, float fogNear, float fogFar, uint32_t fogColor, float fov);
void PSPRenderer_SetCamera(const ScePspFVector3 *eye,
                           const ScePspFVector3 *center,
                           const ScePspFVector3 *up = nullptr);

void PSPRenderer_GetViewProjMatrix(ScePspFMatrix4 *outVP);

void PSPRenderer_EndFrame();
void PSPRenderer_Shutdown();

// Returns pointer to the most recently completed 8888 frame (before the last swap),
// with its buffer width (stride in pixels).
const uint32_t *PSPRenderer_GetLastFrameBuffer8888(int *outBufferWidth);
