// Raycast.cpp

#include "Raycast.h"
#include "Blocks.h"
#include "Level.h"
#include <math.h>

RayHit raycast(Level *level, float ox, float oy, float oz,
               float dx, float dy, float dz, float maxDist) {
  RayHit result;
  result.hit = false;
  result.x = result.y = result.z = 0;
  result.face = 0;
  result.nx = result.ny = result.nz = 0;

  // Normalize direction
  float len = sqrtf(dx * dx + dy * dy + dz * dz);
  if (len < 0.0001f) return result;
  dx /= len;
  dy /= len;
  dz /= len;

  // Current block position
  int mapX = (int)floorf(ox);
  int mapY = (int)floorf(oy);
  int mapZ = (int)floorf(oz);

  // Step direction (+1 or -1)
  int stepX = (dx >= 0) ? 1 : -1;
  int stepY = (dy >= 0) ? 1 : -1;
  int stepZ = (dz >= 0) ? 1 : -1;

  // Distance to next grid boundary along each axis
  float tMaxX = (dx != 0.0f) ? ((dx > 0 ? (mapX + 1.0f - ox) : (ox - mapX)) / fabsf(dx)) : 1e30f;
  float tMaxY = (dy != 0.0f) ? ((dy > 0 ? (mapY + 1.0f - oy) : (oy - mapY)) / fabsf(dy)) : 1e30f;
  float tMaxZ = (dz != 0.0f) ? ((dz > 0 ? (mapZ + 1.0f - oz) : (oz - mapZ)) / fabsf(dz)) : 1e30f;

  // How far along the ray we have to travel to cross one full block in each axis
  float tDeltaX = (dx != 0.0f) ? (1.0f / fabsf(dx)) : 1e30f;
  float tDeltaY = (dy != 0.0f) ? (1.0f / fabsf(dy)) : 1e30f;
  float tDeltaZ = (dz != 0.0f) ? (1.0f / fabsf(dz)) : 1e30f;

  float dist = 0.0f;
  int lastFace = 0;

  auto rayHitsBlockAABB = [&](int bx, int by, int bz, uint8_t id, int &outFace) -> bool {
    const BlockProps &bp = g_blockProps[id];
    float minX = (float)bx + bp.minX, maxX = (float)bx + bp.maxX;
    float minY = (float)by + bp.minY, maxY = (float)by + bp.maxY;
    float minZ = (float)bz + bp.minZ, maxZ = (float)bz + bp.maxZ;

    float tMin = 0.0f;
    float tMax = maxDist;
    int hitFace = lastFace;

    auto clipAxis = [&](float o, float d, float mn, float mx, int entryFacePosDir, int entryFaceNegDir) -> bool {
      if (fabsf(d) < 1e-6f) {
        return (o >= mn && o <= mx);
      }
      float inv = 1.0f / d;
      float t1 = (mn - o) * inv;
      float t2 = (mx - o) * inv;
      int f1 = (d > 0.0f) ? entryFacePosDir : entryFaceNegDir;
      int f2 = (d > 0.0f) ? entryFaceNegDir : entryFacePosDir;
      if (t1 > t2) {
        float tmpT = t1; t1 = t2; t2 = tmpT;
        int tmpF = f1; f1 = f2; f2 = tmpF;
      }
      if (t1 > tMin) {
        tMin = t1;
        hitFace = f1;
      }
      if (t2 < tMax) tMax = t2;
      return tMin <= tMax;
    };

    if (!clipAxis(ox, dx, minX, maxX, 4, 5)) return false; // west/east
    if (!clipAxis(oy, dy, minY, maxY, 0, 1)) return false; // bottom/top
    if (!clipAxis(oz, dz, minZ, maxZ, 2, 3)) return false; // north/south
    if (tMax < 0.0f || tMin > maxDist) return false;
    outFace = hitFace;
    return true;
  };

  // Step through grid
  for (int i = 0; i < 200 && dist < maxDist; i++) {
    // Check current block
    uint8_t id = level->getBlock(mapX, mapY, mapZ);
    if (id != BLOCK_AIR && !g_blockProps[id].isLiquid()) {
      const BlockProps &bp = g_blockProps[id];
      bool customBounds =
          (bp.minX > 0.0f || bp.minY > 0.0f || bp.minZ > 0.0f ||
           bp.maxX < 1.0f || bp.maxY < 1.0f || bp.maxZ < 1.0f);

      int hitFace = lastFace;
      if (customBounds && !rayHitsBlockAABB(mapX, mapY, mapZ, id, hitFace)) {
        // Ray entered this voxel cell but missed actual partial bounds
        // (e.g. slabs/cactus). Keep stepping.
      } else {
      result.hit = true;
      result.x = mapX;
      result.y = mapY;
      result.z = mapZ;
      result.id = id;
      result.face = hitFace;

      // Compute adjacent block for placement
      result.nx = mapX;
      result.ny = mapY;
      result.nz = mapZ;
      switch (hitFace) {
        case 0: result.ny--; break; // Y- (bottom)
        case 1: result.ny++; break; // Y+ (top)
        case 2: result.nz--; break; // Z- (north)
        case 3: result.nz++; break; // Z+ (south)
        case 4: result.nx--; break; // X- (west)
        case 5: result.nx++; break; // X+ (east)
      }
      return result;
      }
    }

    // Advance to next block boundary (DDA step)
    if (tMaxX < tMaxY) {
      if (tMaxX < tMaxZ) {
        dist = tMaxX;
        mapX += stepX;
        tMaxX += tDeltaX;
        lastFace = (stepX > 0) ? 4 : 5; // hit west or east face
      } else {
        dist = tMaxZ;
        mapZ += stepZ;
        tMaxZ += tDeltaZ;
        lastFace = (stepZ > 0) ? 2 : 3; // hit north or south face
      }
    } else {
      if (tMaxY < tMaxZ) {
        dist = tMaxY;
        mapY += stepY;
        tMaxY += tDeltaY;
        lastFace = (stepY > 0) ? 0 : 1; // hit bottom or top face
      } else {
        dist = tMaxZ;
        mapZ += stepZ;
        tMaxZ += tDeltaZ;
        lastFace = (stepZ > 0) ? 2 : 3;
      }
    }
  }

  return result;
}
