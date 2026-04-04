#include "TileRenderer.h"
#include "../world/Blocks.h"
#include <cmath>

// Fake ambient occlusion lighting per face direction
#define LIGHT_TOP 0xFFFFFFFF
#define LIGHT_SIDE 0xFFCCCCCC
#define LIGHT_BOT 0xFF999999

TileRenderer::TileRenderer(Level *level, Tesselator *opaqueTess, Tesselator *transTess,
                           Tesselator *fancyTess, Tesselator *emitTess)
    : m_level(level), m_opaqueTess(opaqueTess), m_transTess(transTess),
      m_fancyTess(fancyTess), m_emitTess(emitTess) {}

TileRenderer::~TileRenderer() {}

// Render plants (cross sprites)
bool TileRenderer::tesselateCrossInWorld(uint8_t id, int lx, int ly, int lz, int cx, int cz) {
  const BlockUV &uv = g_blockUV[id];

  int   wX = cx * CHUNK_SIZE_X + lx;
  int   wY = ly;
  int   wZ = cz * CHUNK_SIZE_Z + lz;

  // Vertex positions in absolute world space (bypass VFPU translation precision issues)
  float xt = (float)(cx * CHUNK_SIZE_X + lx);
  float yt = (float)ly;
  float zt = (float)(cz * CHUNK_SIZE_Z + lz);

  // Random offset constraint
  if (id == BLOCK_TALLGRASS) {
    int64_t seed = ((int64_t)wX * 3129871LL) ^ ((int64_t)wZ * 116129781LL) ^ ((int64_t)wY);
    seed = seed * seed * 42317861LL + seed * 11LL;

    xt += ((((seed >> 16) & 0xf) / 15.0f) - 0.5f) * 0.5f;
    yt += ((((seed >> 20) & 0xf) / 15.0f) - 1.0f) * 0.2f;
    zt += ((((seed >> 24) & 0xf) / 15.0f) - 0.5f) * 0.5f;
  }

  const float ts  = 1.0f / 16.0f;
  const float eps = 0.125f / 256.0f;

  // Sample light from the block position
  float skyL, blkL;
  {
    // 4J brightness ramp: (1-v)/(v*3+1)
    static const float lightTable[16] = {
      0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f,
      0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 1.0f
    };
    static bool inited = false;
    if (!inited) {
      for (int i = 0; i <= 15; i++) {
        float v = 1.0f - i / 15.0f;
        const_cast<float*>(lightTable)[i] = (1.0f - v) / (v * 3.0f + 1.0f);
      }
      inited = true;
    }
    uint8_t sl = (wY + 1 < CHUNK_SIZE_Y) ? m_level->getSkyLight(wX, wY + 1, wZ)
                                          : 15;
    uint8_t bl = m_level->getBlockLight(wX, wY, wZ);
    skyL = lightTable[sl];
    blkL = lightTable[bl];
  }
  float brightness = (blkL > skyL + 0.05f) ? blkL : skyL;
  
  uint32_t baseColor = 0xFFFFFFFF;
  // Apply biome green tint for tall grass (vanilla FoliageColor::getDefaultColor)
  if (id == BLOCK_TALLGRASS) {
      baseColor = 0xFF44a065; // PSP ABGR: R=0x44, G=0xa0, B=0x65 (vanilla ~0x65a044)
  }
  uint32_t col = applyLightToFace(baseColor, brightness);

  float u0 = uv.top_x * ts + eps;
  float v0 = uv.top_y * ts + eps;
  float u1 = (uv.top_x + 1) * ts - eps;
  float v1 = (uv.top_y + 1) * ts - eps;

  // Cross width
  const float width = 0.45f;
  float x0 = xt + 0.5f - width;
  float x1 = xt + 0.5f + width;
  float z0 = zt + 0.5f - width;
  float z1 = zt + 0.5f + width;

  Tesselator *t = m_transTess;

  // Diagonal 1: (x0,z0) -> (x1,z1)
  t->addQuad(u0, v0, u1, v1, col, col, col, col,
             x0, yt + 1.0f, z0,
             x1, yt + 1.0f, z1,
             x0, yt,        z0,
             x1, yt,        z1);

  // Diagonal 2: (x0,z1) -> (x1,z0)
  t->addQuad(u0, v0, u1, v1, col, col, col, col,
             x0, yt + 1.0f, z1,
             x1, yt + 1.0f, z0,
             x0, yt,        z1,
             x1, yt,        z0);

  return true;
}

bool TileRenderer::needFace(int lx, int ly, int lz, int cx, int cz, uint8_t id, int dx, int dy, int dz, bool &outIsFancy) {
  int nx = lx + dx, ny = ly + dy, nz = lz + dz;
  int wNx = cx * CHUNK_SIZE_X + nx;
  int wNy = ny;
  int wNz = cz * CHUNK_SIZE_Z + nz;

  // Cull faces at the bottom of the world (Y=0)
  if (wNy < 0) return false;
  if (wNy >= CHUNK_SIZE_Y) return true;

  // Cull faces at the world boundaries (X and Z)
  if (wNx < 0 || wNx >= WORLD_CHUNKS_X * CHUNK_SIZE_X ||
      wNz < 0 || wNz >= WORLD_CHUNKS_Z * CHUNK_SIZE_Z) {
    return false;
  }

  uint8_t nb = m_level->getBlock(wNx, wNy, wNz);

  const BlockProps &bp = g_blockProps[id];

  if (g_blockProps[nb].isOpaque())
    return false;

  outIsFancy = false;

  if (nb == id && id == BLOCK_LEAVES) {
    outIsFancy = true;
    return true;
  }

  if (bp.isLiquid() && g_blockProps[nb].isLiquid())
    return false;

  if (nb == id && (bp.isTransparent()))
    return false;

  return true;
}

// Returns raw sky light (0-15) at the face-adjacent voxel, no sun multiplier
float TileRenderer::getSkyLightRaw(int lx, int ly, int lz, int cx, int cz, int dx, int dy, int dz) {
  int nx = lx + dx, ny = ly + dy, nz = lz + dz;
  int wNx = cx * CHUNK_SIZE_X + nx;
  int wNy = ny;
  int wNz = cz * CHUNK_SIZE_Z + nz;

  uint8_t skyL;
  if (ny < 0 || ny >= CHUNK_SIZE_Y) {
    skyL = 15;
  } else {
    skyL = m_level->getSkyLight(wNx, wNy, wNz);
  }

  // 4J brightness ramp: (1-v)/(v*3+1)
  static const float lightTable[16] = {
    0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f,
    0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 1.0f
  };
  static bool inited = false;
  if (!inited) {
    for (int i = 0; i <= 15; i++) {
      float v = 1.0f - i / 15.0f;
      const_cast<float*>(lightTable)[i] = (1.0f - v) / (v * 3.0f + 1.0f);
    }
    inited = true;
  }
  return lightTable[skyL];
}

// Smooth vertex sky light (4-sample average, no sun multiplier)
float TileRenderer::getVertexSkyLight(int wx, int wy, int wz,
                                      int dx1, int dy1, int dz1,
                                      int dx2, int dy2, int dz2) {
  // 4J brightness ramp: (1-v)/(v*3+1)
  static const float lightTable[16] = {
    0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f,
    0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 1.0f
  };
  static bool inited = false;
  if (!inited) {
    for (int i = 0; i <= 15; i++) {
      float v = 1.0f - i / 15.0f;
      const_cast<float*>(lightTable)[i] = (1.0f - v) / (v * 3.0f + 1.0f);
    }
    inited = true;
  }

  auto getS = [&](int x, int y, int z) -> float {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 1.0f;
    uint8_t skyL = m_level->getSkyLight(x, y, z);
    return lightTable[skyL];
  };

  float lCenter = getS(wx, wy, wz);
  float lE1 = getS(wx + dx1, wy + dy1, wz + dz1);
  float lE2 = getS(wx + dx2, wy + dy2, wz + dz2);
  bool oq1 = g_blockProps[m_level->getBlock(wx+dx1, wy+dy1, wz+dz1)].isOpaque();
  bool oq2 = g_blockProps[m_level->getBlock(wx+dx2, wy+dy2, wz+dz2)].isOpaque();
  float lC = getS(wx + dx1 + dx2, wy + dy1 + dy2, wz + dz1 + dz2);
  if (oq1 && oq2) lC = (lE1 + lE2) / 2.0f;
  return (lCenter + lE1 + lE2 + lC) / 4.0f;
}

// Returns block light (torch) brightness at a vertex, NOT multiplied by sun
float TileRenderer::getVertexBlockLight(int wx, int wy, int wz,
                                        int dx1, int dy1, int dz1,
                                        int dx2, int dy2, int dz2) {
  // 4J brightness ramp: (1-v)/(v*3+1)
  static const float lightTable[16] = {
    0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f,
    0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 1.0f
  };
  static bool inited = false;
  if (!inited) {
    for (int i = 0; i <= 15; i++) {
      float v = 1.0f - i / 15.0f;
      const_cast<float*>(lightTable)[i] = (1.0f - v) / (v * 3.0f + 1.0f);
    }
    inited = true;
  }

  auto getB = [&](int x, int y, int z) -> float {
    if (y < 0 || y >= CHUNK_SIZE_Y) return 0.0f;
    uint8_t blkL = m_level->getBlockLight(x, y, z);
    return lightTable[blkL];
  };

  float lCenter = getB(wx, wy, wz);
  float lE1 = getB(wx + dx1, wy + dy1, wz + dz1);
  float lE2 = getB(wx + dx2, wy + dy2, wz + dz2);
  float lC = getB(wx + dx1 + dx2, wy + dy1 + dy2, wz + dz1 + dz2);
  return (lCenter + lE1 + lE2 + lC) / 4.0f;
}

uint32_t TileRenderer::applyLightToFace(uint32_t baseColor, float brightness) {
  uint8_t a = (baseColor >> 24) & 0xFF;
  uint8_t b = (baseColor >> 16) & 0xFF;
  uint8_t g = (baseColor >> 8) & 0xFF;
  uint8_t r = baseColor & 0xFF;
  b = (uint8_t)(b * brightness);
  g = (uint8_t)(g * brightness);
  r = (uint8_t)(r * brightness);
  return (a << 24) | (b << 16) | (g << 8) | r;
}

bool TileRenderer::tesselateBlockInWorld(uint8_t id, int lx, int ly, int lz, int cx, int cz) {
  if (id == BLOCK_TALLGRASS || id == BLOCK_FLOWER || id == BLOCK_ROSE || id == BLOCK_SAPLING || id == BLOCK_REEDS) {
    return tesselateCrossInWorld(id, lx, ly, lz, cx, cz);
  }

  // Fluids use a dedicated MCPE 0.6.1-style path:
  // smooth corner heights, directional top UV for flow, and seam-free side joins.
  if (id == BLOCK_WATER_STILL || id == BLOCK_WATER_FLOW ||
      id == BLOCK_LAVA_STILL || id == BLOCK_LAVA_FLOW) {
    const BlockUV &uv = g_blockUV[id];
    const bool isWater = (id == BLOCK_WATER_STILL || id == BLOCK_WATER_FLOW);
    float wx = (float)(cx * CHUNK_SIZE_X + lx);
    float wy = (float)ly;
    float wz = (float)(cz * CHUNK_SIZE_Z + lz);
    int wX = cx * CHUNK_SIZE_X + lx;
    int wY = ly;
    int wZ = cz * CHUNK_SIZE_Z + lz;

    const float ts = 1.0f / 16.0f;
    const float eps = 0.125f / 256.0f;

    auto isFluidId = [&](uint8_t b) {
      if (isWater) return b == BLOCK_WATER_STILL || b == BLOCK_WATER_FLOW;
      return b == BLOCK_LAVA_STILL || b == BLOCK_LAVA_FLOW;
    };
    Tesselator *fluidTess = isWater ? m_transTess : m_emitTess;
    uint32_t topColor = isWater ? 0xFFFFFFFF : 0xFF88CCFF;
    uint32_t bottomColor = isWater ? 0xFFB0B0B0 : 0xFF4477AA;
    uint32_t sideColor = isWater ? 0xFFDDDDDD : 0xFF66AADD;

    auto getLiquidDepth = [&](int x, int y, int z) -> int {
      uint8_t bid = m_level->getBlock(x, y, z);
      if (!isFluidId(bid)) return -1;
      uint8_t d = isWater ? m_level->getWaterDepth(x, y, z)
                          : m_level->getLavaDepth(x, y, z);
      if (d == 0xFF) d = (bid == BLOCK_WATER_STILL || bid == BLOCK_LAVA_STILL) ? 0 : 1;
      if (d >= 8) d = 0;
      return (int)d;
    };

    auto getLiquidHeight = [&](int x, int y, int z) -> float {
      int count = 0;
      float h = 0.0f;
      for (int i = 0; i < 4; i++) {
        int xx = x - (i & 1);
        int zz = z - ((i >> 1) & 1);
        if (isFluidId(m_level->getBlock(xx, y + 1, zz))) return 1.0f;
        int d = getLiquidDepth(xx, y, zz);
        if (d >= 0) {
          float fh = ((float)d + 1.0f) / 9.0f;
          if (d == 0) { h += fh * 10.0f; count += 10; }
          h += fh;
          count++;
        } else if (!g_blockProps[m_level->getBlock(xx, y, zz)].isSolid()) {
          h += 1.0f;
          count++;
        }
      }
      if (count <= 0) return 0.0f;
      return 1.0f - h / (float)count;
    };

    // Approximation of MCPE 0.6.1 LiquidTile::getSlopeAngle().
    auto getSlopeAngle = [&]() -> float {
      int mid = getLiquidDepth(wX, wY, wZ);
      if (mid < 0) return -1000.0f;
      float fx = 0.0f, fz = 0.0f;
      for (int d = 0; d < 4; d++) {
        int xt = wX + (d == 0 ? -1 : d == 2 ? 1 : 0);
        int zt = wZ + (d == 1 ? -1 : d == 3 ? 1 : 0);
        int t = getLiquidDepth(xt, wY, zt);
        if (t < 0) {
          if (!g_blockProps[m_level->getBlock(xt, wY, zt)].isSolid()) {
            t = getLiquidDepth(xt, wY - 1, zt);
            if (t >= 0) {
              int dir = t - (mid - 8);
              fx += (xt - wX) * dir;
              fz += (zt - wZ) * dir;
            }
          }
        } else {
          int dir = t - mid;
          fx += (xt - wX) * dir;
          fz += (zt - wZ) * dir;
        }
      }
      if (std::abs(fx) < 1e-5f && std::abs(fz) < 1e-5f) return -1000.0f;
      return std::atan2(fz, fx) - 3.14159265f * 0.5f;
    };

    auto getMixedBrightness = [&](int x, int y, int z) -> float {
      static float lightTable[16] = {};
      static bool inited = false;
      if (!inited) {
        for (int i = 0; i <= 15; i++) {
          float v = 1.0f - i / 15.0f;
          lightTable[i] = (1.0f - v) / (v * 3.0f + 1.0f);
        }
        inited = true;
      }
      if (y < 0) return 0.0f;
      if (y >= CHUNK_SIZE_Y) return 1.0f;
      float sky = lightTable[m_level->getSkyLight(x, y, z)];
      float blk = lightTable[m_level->getBlockLight(x, y, z)];
      return (blk > sky) ? blk : sky;
    };

    auto emitVertex = [&](float u, float v, uint32_t c, float x, float y, float z) {
      fluidTess->color(c);
      fluidTess->tex(u, v);
      fluidTess->vertex(x, y, z);
    };
    auto emitQuad = [&](float u0, float v0, float u1, float v1, float u2, float v2, float u3, float v3,
                        uint32_t c, float x0, float y0, float z0, float x1, float y1, float z1,
                        float x2, float y2, float z2, float x3, float y3, float z3) {
      emitVertex(u0, v0, c, x0, y0, z0);
      emitVertex(u2, v2, c, x2, y2, z2);
      emitVertex(u1, v1, c, x1, y1, z1);
      emitVertex(u1, v1, c, x1, y1, z1);
      emitVertex(u2, v2, c, x2, y2, z2);
      emitVertex(u3, v3, c, x3, y3, z3);
    };

    auto cornerHeight = [&](int cx0, int cz0) -> float {
      return getLiquidHeight(cx0, wY, cz0);
    };

    float h00 = cornerHeight(wX, wZ);
    float h01 = cornerHeight(wX, wZ + 1);
    float h11 = cornerHeight(wX + 1, wZ + 1);
    float h10 = cornerHeight(wX + 1, wZ);
    bool drawn = false;

    bool isFancy = false;
    bool up = needFace(lx, ly, lz, cx, cz, id, 0, 1, 0, isFancy);
    bool down = needFace(lx, ly, lz, cx, cz, id, 0, -1, 0, isFancy);
    bool dirs[4];
    dirs[0] = needFace(lx, ly, lz, cx, cz, id, 0, 0, -1, isFancy);
    dirs[1] = needFace(lx, ly, lz, cx, cz, id, 0, 0, 1, isFancy);
    dirs[2] = needFace(lx, ly, lz, cx, cz, id, -1, 0, 0, isFancy);
    dirs[3] = needFace(lx, ly, lz, cx, cz, id, 1, 0, 0, isFancy);

    if (!up && !down && !dirs[0] && !dirs[1] && !dirs[2] && !dirs[3]) return false;

    // Top (with flow angle UV like MCPE 0.6.1)
    if (up) {
      float br = getMixedBrightness(wX, wY, wZ);
      uint32_t c = applyLightToFace(topColor, br);
      float angle = getSlopeAngle();

      int topTX = uv.top_x;
      int topTY = uv.top_y;
      if (angle > -999.0f) {
        topTX = uv.side_x;
        topTY = uv.side_y;
      }
      float uc = (topTX * 16.0f + 8.0f) / 256.0f;
      float vc = (topTY * 16.0f + 8.0f) / 256.0f;
      if (angle < -999.0f) {
        angle = 0.0f;
      } else {
        uc = (topTX * 16.0f + 16.0f) / 256.0f;
        vc = (topTY * 16.0f + 16.0f) / 256.0f;
      }
      float s = (std::sin(angle) * 8.0f) / 256.5f;
      float co = (std::cos(angle) * 8.0f) / 256.5f;

      emitQuad(uc - co - s, vc - co + s, uc - co + s, vc + co + s,
               uc + co + s, vc + co - s, uc + co - s, vc - co - s,
               c,
               wx, wy + h00, wz,
               wx, wy + h01, wz + 1,
               wx + 1, wy + h11, wz + 1,
               wx + 1, wy + h10, wz);
      drawn = true;
    }

    // Bottom
    if (down) {
      float br = getMixedBrightness(wX, wY - 1, wZ);
      uint32_t c = applyLightToFace(bottomColor, br * 0.5f);
      float u0 = uv.bot_x * ts + eps, v0 = uv.bot_y * ts + eps;
      float u1 = (uv.bot_x + 1) * ts - eps, v1 = (uv.bot_y + 1) * ts - eps;
      emitQuad(u0, v0, u1, v0, u1, v1, u0, v1, c,
               wx, wy, wz + 1,
               wx + 1, wy, wz + 1,
               wx + 1, wy, wz,
               wx, wy, wz);
      drawn = true;
    }

    for (int face = 0; face < 4; face++) {
      if (!dirs[face]) continue;
      float hh0, hh1, x0, z0, x1, z1;
      int xt = wX, zt = wZ;
      if (face == 0) { // north
        hh0 = h00; hh1 = h10; x0 = wx; x1 = wx + 1; z0 = wz; z1 = wz; zt--;
      } else if (face == 1) { // south
        hh0 = h11; hh1 = h01; x0 = wx + 1; x1 = wx; z0 = wz + 1; z1 = wz + 1; zt++;
      } else if (face == 2) { // west
        hh0 = h01; hh1 = h00; x0 = wx; x1 = wx; z0 = wz + 1; z1 = wz; xt--;
      } else { // east
        hh0 = h10; hh1 = h11; x0 = wx + 1; x1 = wx + 1; z0 = wz; z1 = wz + 1; xt++;
      }

      float u0 = (uv.side_x * 16.0f + 0.0f) / 256.0f;
      float u1 = (uv.side_x * 16.0f + 16.0f - 0.01f) / 256.0f;
      float v01 = (uv.side_y * 16.0f + (1.0f - hh0) * 16.0f) / 256.0f;
      float v02 = (uv.side_y * 16.0f + (1.0f - hh1) * 16.0f) / 256.0f;
      float v1 = (uv.side_y * 16.0f + 16.0f - 0.01f) / 256.0f;

      float br = getMixedBrightness(xt, wY, zt) * ((face < 2) ? 0.8f : 0.6f);
      uint32_t c = applyLightToFace(sideColor, br);
      emitQuad(u0, v01, u1, v02, u1, v1, u0, v1, c,
               x0, wy + hh0, z0,
               x1, wy + hh1, z1,
               x1, wy, z1,
               x0, wy, z0);
      drawn = true;
    }
    return drawn;
  }

  const BlockUV &uv = g_blockUV[id];

  // Vertex positions in absolute world space
  float wx = (float)(cx * CHUNK_SIZE_X + lx);
  float wy = (float)ly;
  float wz = (float)(cz * CHUNK_SIZE_Z + lz);

  // World coords for light lookups
  int wX = cx * CHUNK_SIZE_X + lx;
  int wY = ly;
  int wZ = cz * CHUNK_SIZE_Z + lz;

  const float ts = 1.0f / 16.0f;
  const float eps = 0.125f / 256.0f;
  bool drawn = false;
  bool isFancy = false;

  // Select tesselator based on lighting
  auto pickTess = [&](Tesselator *skyTess, Tesselator *fncTess,
                      float skyL, float blkL, bool fancy) -> Tesselator * {
    if (g_blockProps[id].isTransparent()) {
      return fancy ? fncTess : m_transTess;
    }
    // If block light is dominant, route to emitTess
    if (blkL > skyL + 0.05f) return m_emitTess;
    return skyTess;
  };

  // 4J logic to avoid Z-fighting on inner leaves is handled in per-face code

  // TOP (+Y)
  if (needFace(lx, ly, lz, cx, cz, id, 0, 1, 0, isFancy)) {
    float sl00 = getVertexSkyLight(wX, wY+1, wZ, -1,0,0, 0,0,-1);
    float sl10 = getVertexSkyLight(wX, wY+1, wZ,  1,0,0, 0,0,-1);
    float sl01 = getVertexSkyLight(wX, wY+1, wZ, -1,0,0, 0,0, 1);
    float sl11 = getVertexSkyLight(wX, wY+1, wZ,  1,0,0, 0,0, 1);
    float bl00 = getVertexBlockLight(wX, wY+1, wZ, -1,0,0, 0,0,-1);
    float bl10 = getVertexBlockLight(wX, wY+1, wZ,  1,0,0, 0,0,-1);
    float bl01 = getVertexBlockLight(wX, wY+1, wZ, -1,0,0, 0,0, 1);
    float bl11 = getVertexBlockLight(wX, wY+1, wZ,  1,0,0, 0,0, 1);
    float avgBlk = (bl00+bl10+bl01+bl11)*0.25f;
    float avgSky = (sl00+sl10+sl01+sl11)*0.25f;

    Tesselator *t = pickTess(m_opaqueTess, m_fancyTess, avgSky, avgBlk, isFancy);
    bool useBlk = (avgBlk > avgSky + 0.05f);
    uint32_t c00 = applyLightToFace(LIGHT_TOP, useBlk ? bl00 : sl00);
    uint32_t c10 = applyLightToFace(LIGHT_TOP, useBlk ? bl10 : sl10);
    uint32_t c01 = applyLightToFace(LIGHT_TOP, useBlk ? bl01 : sl01);
    uint32_t c11 = applyLightToFace(LIGHT_TOP, useBlk ? bl11 : sl11);

    float u0 = uv.top_x*ts+eps, v0 = uv.top_y*ts+eps;
    float u1 = (uv.top_x+1)*ts-eps, v1 = (uv.top_y+1)*ts-eps;
    float off = isFancy ? 0.005f : 0.0f;
    t->addQuad(u0,v0,u1,v1, c00,c10,c01,c11,
               wx+off,wy+1-off,wz+off, wx+1-off,wy+1-off,wz+off, wx+off,wy+1-off,wz+1-off, wx+1-off,wy+1-off,wz+1-off);
    drawn = true;
  }

  // BOTTOM (-Y)
  if (needFace(lx, ly, lz, cx, cz, id, 0, -1, 0, isFancy)) {
    float sl00 = getVertexSkyLight(wX, wY-1, wZ, -1,0,0, 0,0,-1);
    float sl10 = getVertexSkyLight(wX, wY-1, wZ,  1,0,0, 0,0,-1);
    float sl01 = getVertexSkyLight(wX, wY-1, wZ, -1,0,0, 0,0, 1);
    float sl11 = getVertexSkyLight(wX, wY-1, wZ,  1,0,0, 0,0, 1);
    float bl00 = getVertexBlockLight(wX, wY-1, wZ, -1,0,0, 0,0,-1);
    float bl10 = getVertexBlockLight(wX, wY-1, wZ,  1,0,0, 0,0,-1);
    float bl01 = getVertexBlockLight(wX, wY-1, wZ, -1,0,0, 0,0, 1);
    float bl11 = getVertexBlockLight(wX, wY-1, wZ,  1,0,0, 0,0, 1);
    float avgBlk=(bl00+bl10+bl01+bl11)*0.25f, avgSky=(sl00+sl10+sl01+sl11)*0.25f;

    Tesselator *t = pickTess(m_opaqueTess, m_fancyTess, avgSky, avgBlk, isFancy);
    bool useBlk = (avgBlk > avgSky + 0.05f);
    uint32_t c00=applyLightToFace(LIGHT_BOT, useBlk?bl00:sl00);
    uint32_t c10=applyLightToFace(LIGHT_BOT, useBlk?bl10:sl10);
    uint32_t c01=applyLightToFace(LIGHT_BOT, useBlk?bl01:sl01);
    uint32_t c11=applyLightToFace(LIGHT_BOT, useBlk?bl11:sl11);

    float u0=uv.bot_x*ts+eps, v0=uv.bot_y*ts+eps;
    float u1=(uv.bot_x+1)*ts-eps, v1=(uv.bot_y+1)*ts-eps;
    float off = isFancy ? 0.005f : 0.0f;
    t->addQuad(u0,v0,u1,v1, c01,c11,c00,c10,
               wx+off,wy+off,wz+1-off, wx+1-off,wy+off,wz+1-off, wx+off,wy+off,wz+off, wx+1-off,wy+off,wz+off);
    drawn = true;
  }

  // NORTH (-Z)
  if (needFace(lx, ly, lz, cx, cz, id, 0, 0, -1, isFancy)) {
    float sl11=getVertexSkyLight(wX,wY,wZ-1, 1,0,0, 0,1,0);
    float sl01=getVertexSkyLight(wX,wY,wZ-1,-1,0,0, 0,1,0);
    float sl10=getVertexSkyLight(wX,wY,wZ-1, 1,0,0, 0,-1,0);
    float sl00=getVertexSkyLight(wX,wY,wZ-1,-1,0,0, 0,-1,0);
    float bl11=getVertexBlockLight(wX,wY,wZ-1, 1,0,0, 0,1,0);
    float bl01=getVertexBlockLight(wX,wY,wZ-1,-1,0,0, 0,1,0);
    float bl10=getVertexBlockLight(wX,wY,wZ-1, 1,0,0, 0,-1,0);
    float bl00=getVertexBlockLight(wX,wY,wZ-1,-1,0,0, 0,-1,0);
    float avgBlk=(bl11+bl01+bl10+bl00)*0.25f, avgSky=(sl11+sl01+sl10+sl00)*0.25f;

    Tesselator *t=pickTess(m_opaqueTess,m_fancyTess,avgSky,avgBlk,isFancy);
    bool useBlk=(avgBlk>avgSky+0.05f);
    uint32_t c11=applyLightToFace(LIGHT_SIDE,useBlk?bl11:sl11);
    uint32_t c01=applyLightToFace(LIGHT_SIDE,useBlk?bl01:sl01);
    uint32_t c10=applyLightToFace(LIGHT_SIDE,useBlk?bl10:sl10);
    uint32_t c00=applyLightToFace(LIGHT_SIDE,useBlk?bl00:sl00);

    float u0=uv.side_x*ts+eps, v0=uv.side_y*ts+eps;
    float u1=(uv.side_x+1)*ts-eps, v1=(uv.side_y+1)*ts-eps;
    float off = isFancy ? 0.005f : 0.0f;
    t->addQuad(u0,v0,u1,v1, c11,c01,c10,c00,
               wx+1-off,wy+1-off,wz+off, wx+off,wy+1-off,wz+off, wx+1-off,wy+off,wz+off, wx+off,wy+off,wz+off);
    drawn = true;
  }

  // SOUTH (+Z)
  if (needFace(lx, ly, lz, cx, cz, id, 0, 0, 1, isFancy)) {
    float sl01=getVertexSkyLight(wX,wY,wZ+1,-1,0,0, 0,1,0);
    float sl11=getVertexSkyLight(wX,wY,wZ+1, 1,0,0, 0,1,0);
    float sl00=getVertexSkyLight(wX,wY,wZ+1,-1,0,0, 0,-1,0);
    float sl10=getVertexSkyLight(wX,wY,wZ+1, 1,0,0, 0,-1,0);
    float bl01=getVertexBlockLight(wX,wY,wZ+1,-1,0,0, 0,1,0);
    float bl11=getVertexBlockLight(wX,wY,wZ+1, 1,0,0, 0,1,0);
    float bl00=getVertexBlockLight(wX,wY,wZ+1,-1,0,0, 0,-1,0);
    float bl10=getVertexBlockLight(wX,wY,wZ+1, 1,0,0, 0,-1,0);
    float avgBlk=(bl01+bl11+bl00+bl10)*0.25f, avgSky=(sl01+sl11+sl00+sl10)*0.25f;

    Tesselator *t=pickTess(m_opaqueTess,m_fancyTess,avgSky,avgBlk,isFancy);
    bool useBlk=(avgBlk>avgSky+0.05f);
    uint32_t c01=applyLightToFace(LIGHT_SIDE,useBlk?bl01:sl01);
    uint32_t c11=applyLightToFace(LIGHT_SIDE,useBlk?bl11:sl11);
    uint32_t c00=applyLightToFace(LIGHT_SIDE,useBlk?bl00:sl00);
    uint32_t c10=applyLightToFace(LIGHT_SIDE,useBlk?bl10:sl10);

    float u0=uv.side_x*ts+eps, v0=uv.side_y*ts+eps;
    float u1=(uv.side_x+1)*ts-eps, v1=(uv.side_y+1)*ts-eps;
    float off = isFancy ? 0.005f : 0.0f;
    t->addQuad(u0,v0,u1,v1, c01,c11,c00,c10,
               wx+off,wy+1-off,wz+1-off, wx+1-off,wy+1-off,wz+1-off, wx+off,wy+off,wz+1-off, wx+1-off,wy+off,wz+1-off);
    drawn = true;
  }

  // WEST (-X)
  if (needFace(lx, ly, lz, cx, cz, id, -1, 0, 0, isFancy)) {
    float sl01=getVertexSkyLight(wX-1,wY,wZ, 0,1,0, 0,0,-1);
    float sl11=getVertexSkyLight(wX-1,wY,wZ, 0,1,0, 0,0, 1);
    float sl00=getVertexSkyLight(wX-1,wY,wZ, 0,-1,0, 0,0,-1);
    float sl10=getVertexSkyLight(wX-1,wY,wZ, 0,-1,0, 0,0, 1);
    float bl01=getVertexBlockLight(wX-1,wY,wZ, 0,1,0, 0,0,-1);
    float bl11=getVertexBlockLight(wX-1,wY,wZ, 0,1,0, 0,0, 1);
    float bl00=getVertexBlockLight(wX-1,wY,wZ, 0,-1,0, 0,0,-1);
    float bl10=getVertexBlockLight(wX-1,wY,wZ, 0,-1,0, 0,0, 1);
    float avgBlk=(bl01+bl11+bl00+bl10)*0.25f, avgSky=(sl01+sl11+sl00+sl10)*0.25f;

    Tesselator *t=pickTess(m_opaqueTess,m_fancyTess,avgSky,avgBlk,isFancy);
    bool useBlk=(avgBlk>avgSky+0.05f);
    uint32_t c01=applyLightToFace(LIGHT_SIDE,useBlk?bl01:sl01);
    uint32_t c11=applyLightToFace(LIGHT_SIDE,useBlk?bl11:sl11);
    uint32_t c00=applyLightToFace(LIGHT_SIDE,useBlk?bl00:sl00);
    uint32_t c10=applyLightToFace(LIGHT_SIDE,useBlk?bl10:sl10);

    float u0=uv.side_x*ts+eps, v0=uv.side_y*ts+eps;
    float u1=(uv.side_x+1)*ts-eps, v1=(uv.side_y+1)*ts-eps;
    float off = isFancy ? 0.005f : 0.0f;
    t->addQuad(u0,v0,u1,v1, c01,c11,c00,c10,
               wx+off,wy+1-off,wz+off, wx+off,wy+1-off,wz+1-off, wx+off,wy+off,wz+off, wx+off,wy+off,wz+1-off);
    drawn = true;
  }

  // EAST (+X)
  if (needFace(lx, ly, lz, cx, cz, id, 1, 0, 0, isFancy)) {
    float sl11=getVertexSkyLight(wX+1,wY,wZ, 0,1,0, 0,0, 1);
    float sl01=getVertexSkyLight(wX+1,wY,wZ, 0,1,0, 0,0,-1);
    float sl10=getVertexSkyLight(wX+1,wY,wZ, 0,-1,0, 0,0, 1);
    float sl00=getVertexSkyLight(wX+1,wY,wZ, 0,-1,0, 0,0,-1);
    float bl11=getVertexBlockLight(wX+1,wY,wZ, 0,1,0, 0,0, 1);
    float bl01=getVertexBlockLight(wX+1,wY,wZ, 0,1,0, 0,0,-1);
    float bl10=getVertexBlockLight(wX+1,wY,wZ, 0,-1,0, 0,0, 1);
    float bl00=getVertexBlockLight(wX+1,wY,wZ, 0,-1,0, 0,0,-1);
    float avgBlk=(bl11+bl01+bl10+bl00)*0.25f, avgSky=(sl11+sl01+sl10+sl00)*0.25f;

    Tesselator *t=pickTess(m_opaqueTess,m_fancyTess,avgSky,avgBlk,isFancy);
    bool useBlk=(avgBlk>avgSky+0.05f);
    uint32_t c11=applyLightToFace(LIGHT_SIDE,useBlk?bl11:sl11);
    uint32_t c01=applyLightToFace(LIGHT_SIDE,useBlk?bl01:sl01);
    uint32_t c10=applyLightToFace(LIGHT_SIDE,useBlk?bl10:sl10);
    uint32_t c00=applyLightToFace(LIGHT_SIDE,useBlk?bl00:sl00);

    float u0=uv.side_x*ts+eps, v0=uv.side_y*ts+eps;
    float u1=(uv.side_x+1)*ts-eps, v1=(uv.side_y+1)*ts-eps;
    float off = isFancy ? 0.005f : 0.0f;
    t->addQuad(u0,v0,u1,v1, c11,c01,c10,c00,
               wx+1-off,wy+1-off,wz+1-off, wx+1-off,wy+1-off,wz+off, wx+1-off,wy+off,wz+1-off, wx+1-off,wy+off,wz+off);
    drawn = true;
  }

  return drawn;
}
