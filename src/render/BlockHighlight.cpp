// BlockHighlight.cpp - wireframe cube around the targeted block
// Uses PSP GU_LINES to draw 12 edges of the block's AABB

#include "BlockHighlight.h"
#include "../world/Blocks.h"
#include <pspgu.h>
#include <pspgum.h>

// Simple vertex: just position (no texture, no color per-vertex)
struct HLVertex {
  float x, y, z;
};

void BlockHighlight_Draw(int bx, int by, int bz, uint8_t blockId) {
  // Slight expansion to prevent z-fighting with block faces
  const float e = 0.002f;

  // Stair outline needs up to 18 edges (36 verts), keep headroom.
  static HLVertex lines[64];
  int vcount = 0;
  auto appendBoxEdges = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
    // Bottom
    lines[vcount++] = {x0, y0, z0}; lines[vcount++] = {x1, y0, z0};
    lines[vcount++] = {x1, y0, z0}; lines[vcount++] = {x1, y0, z1};
    lines[vcount++] = {x1, y0, z1}; lines[vcount++] = {x0, y0, z1};
    lines[vcount++] = {x0, y0, z1}; lines[vcount++] = {x0, y0, z0};
    // Top
    lines[vcount++] = {x0, y1, z0}; lines[vcount++] = {x1, y1, z0};
    lines[vcount++] = {x1, y1, z0}; lines[vcount++] = {x1, y1, z1};
    lines[vcount++] = {x1, y1, z1}; lines[vcount++] = {x0, y1, z1};
    lines[vcount++] = {x0, y1, z1}; lines[vcount++] = {x0, y1, z0};
    // Vertical
    lines[vcount++] = {x0, y0, z0}; lines[vcount++] = {x0, y1, z0};
    lines[vcount++] = {x1, y0, z0}; lines[vcount++] = {x1, y1, z0};
    lines[vcount++] = {x1, y0, z1}; lines[vcount++] = {x1, y1, z1};
    lines[vcount++] = {x0, y0, z1}; lines[vcount++] = {x0, y1, z1};
  };

  if (isStairId(blockId)) {
    const float x0 = 0.0f - e, x1 = 1.0f + e;
    const float y0 = 0.0f - e, yM = 0.5f, y1 = 1.0f + e;
    const float z0 = 0.0f - e, zM = 0.5f, z1 = 1.0f + e;
    appendBoxEdges(x0, y0, z0, x1, yM, z1);
    int facing = stairFacing(blockId);
    if (facing == 0) appendBoxEdges(x0, yM, zM, x1, y1, z1);
    else if (facing == 1) appendBoxEdges(x0, yM, z0, 0.5f, y1, z1);
    else if (facing == 2) appendBoxEdges(x0, yM, z0, x1, y1, 0.5f);
    else appendBoxEdges(0.5f, yM, z0, x1, y1, z1);
  } else {
    const BlockProps& props = g_blockProps[blockId];
    appendBoxEdges(props.minX - e, props.minY - e, props.minZ - e,
                   props.maxX + e, props.maxY + e, props.maxZ + e);
  }

  // Disable texturing for wireframe
  sceGuDisable(GU_TEXTURE_2D);

  // Semi-transparent black lines
  sceGuColor(0x88000000);
  sceGuDepthMask(GU_TRUE); // no depth writes

  sceGumMatrixMode(GU_MODEL);
  sceGumLoadIdentity();
  
  // Translate to block position to keep coordinate values small
  ScePspFVector3 blockPos = { (float)bx, (float)by, (float)bz };
  sceGumTranslate(&blockPos);

  sceGumDrawArray(GU_LINES,
                  GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                  vcount, NULL, lines);

  sceGuDepthMask(GU_FALSE); // restore depth writes
  sceGuEnable(GU_TEXTURE_2D);
}
