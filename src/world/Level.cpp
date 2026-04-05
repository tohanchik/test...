#include "Level.h"
#include "Random.h"
#include "WorldGen.h"
#include "TreeFeature.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

bool Level::isWaterBlock(uint8_t id) const {
  return id == BLOCK_WATER_STILL || id == BLOCK_WATER_FLOW;
}

bool Level::isLavaBlock(uint8_t id) const {
  return id == BLOCK_LAVA_STILL || id == BLOCK_LAVA_FLOW;
}

void Level::setSimulationFocus(int wx, int wy, int wz, int radius) {
  m_simFocusX = wx;
  m_simFocusY = wy;
  m_simFocusZ = wz;
  m_simFocusRadius = radius;
}

void Level::tick() {
  m_time += 1;
  if (m_waterDirty) tickWater();
  if (m_lavaDirty) tickLava();
  if (m_waterWakeTicks > 0) m_waterWakeTicks--;
  if (m_lavaWakeTicks > 0) m_lavaWakeTicks--;
}

void Level::tickWater() {
  // Budget per world tick (world ticks run at fixed 20 TPS from main loop).
  const int maxTicksPerWorldTick = 128;
  int processed = 0;
  while (processed < maxTicksPerWorldTick && !m_waterTicks.empty()) {
    WaterTickNode n = m_waterTicks.top();
    if (n.dueTick > (int)m_time) break;
    m_waterTicks.pop();
    if (n.idx < 0 || n.idx >= (int)m_waterDue.size()) continue;
    if (m_waterDue[n.idx] != n.dueTick) continue;
    m_waterDue[n.idx] = -1;

    int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
    int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
    int x = n.idx % maxX;
    int tmp = n.idx / maxX;
    int z = tmp % maxZ;
    int y = tmp / maxZ;
    processWaterCell(x, y, z);
    processed++;
  }
  m_waterDirty = !m_waterTicks.empty();
}

void Level::tickLava() {
  // Lava moves slower than water and has a lower per-tick processing budget.
  const int maxTicksPerWorldTick = 64;
  int processed = 0;
  while (processed < maxTicksPerWorldTick && !m_lavaTicks.empty()) {
    WaterTickNode n = m_lavaTicks.top();
    if (n.dueTick > (int)m_time) break;
    m_lavaTicks.pop();
    if (n.idx < 0 || n.idx >= (int)m_lavaDue.size()) continue;
    if (m_lavaDue[n.idx] != n.dueTick) continue;
    m_lavaDue[n.idx] = -1;

    int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
    int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
    int x = n.idx % maxX;
    int tmp = n.idx / maxX;
    int z = tmp % maxZ;
    int y = tmp / maxZ;
    processLavaCell(x, y, z);
    processed++;
  }
  m_lavaDirty = !m_lavaTicks.empty();
}

void Level::processWaterCell(int x, int y, int z) {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  auto inBounds = [&](int wx, int wy, int wz) {
    return wx >= 0 && wx < maxX && wz >= 0 && wz < maxZ && wy >= 0 && wy < CHUNK_SIZE_Y;
  };
  if (!inBounds(x, y, z)) return;
  uint8_t id = getBlock(x, y, z);
  if (!isWaterBlock(id)) return;

  auto getDepth = [&](int wx, int wy, int wz) -> int {
    if (!inBounds(wx, wy, wz)) return -1;
    uint8_t bid = getBlock(wx, wy, wz);
    if (!isWaterBlock(bid)) return -1;
    uint8_t d = getWaterDepth(wx, wy, wz);
    if (d == 0xFF) d = (bid == BLOCK_WATER_STILL) ? 0 : 1;
    return (int)d;
  };
  auto isWaterBlocking = [&](int wx, int wy, int wz) -> bool {
    if (!inBounds(wx, wy, wz)) return true;
    uint8_t bid = getBlock(wx, wy, wz);
    if (bid == BLOCK_AIR) return false;
    return g_blockProps[bid].isSolid();
  };
  auto canSpreadTo = [&](int wx, int wy, int wz) -> bool {
    if (!inBounds(wx, wy, wz)) return false;
    uint8_t bid = getBlock(wx, wy, wz);
    if (isWaterBlock(bid)) return false;
    return !isWaterBlocking(wx, wy, wz);
  };
  auto setWaterCell = [&](int wx, int wy, int wz, uint8_t bid, uint8_t d) {
    m_inWaterSimUpdate = true;
    setBlock(wx, wy, wz, bid);
    m_inWaterSimUpdate = false;
    setWaterDepth(wx, wy, wz, d);
  };

  int depth = getDepth(x, y, z);
  if (depth < 0) return;
  int dropOff = 1;
  bool becomeStatic = true;
  int maxCount = 0;

  if (depth > 0) {
    int highest = -100;
    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
      int nd = getDepth(x + dx[i], y, z + dz[i]);
      if (nd < 0) continue;
      if (nd == 0) maxCount++;
      if (nd >= 8) nd = 0;
      if (highest < 0 || nd < highest) highest = nd;
    }

    int above = getDepth(x, y + 1, z);
    int newDepth = highest + dropOff;
    if (newDepth >= 8 || highest < 0) newDepth = -1;
    if (above >= 0) newDepth = (above >= 8) ? above : (above + 8);

    if (maxCount >= 2) {
      if (isWaterBlocking(x, y - 1, z)) newDepth = 0;
      else if (isWaterBlock(getBlock(x, y - 1, z)) && getDepth(x, y - 1, z) == 0) newDepth = 0;
    }

    if (newDepth != depth) {
      if (newDepth < 0) {
        setWaterCell(x, y, z, BLOCK_AIR, 0xFF);
        wakeWaterNeighborhood(x, y, z, 5);
        return;
      }

      depth = newDepth;
      setWaterDepth(x, y, z, (uint8_t)depth);
      scheduleWaterTick(x, y, z, 5);
      wakeWaterNeighborhood(x, y, z, 5);
    } else {
      if (becomeStatic) setWaterCell(x, y, z, BLOCK_WATER_STILL, (uint8_t)depth);
    }
  } else {
    setWaterCell(x, y, z, BLOCK_WATER_STILL, 0);
  }

  if (canSpreadTo(x, y - 1, z)) {
    int d = (depth >= 8) ? depth : (depth + 8);
    if (d > 7) d = 7;
    setWaterCell(x, y - 1, z, BLOCK_WATER_FLOW, (uint8_t)d);
    scheduleWaterTick(x, y - 1, z, 5);
  } else if (depth >= 0 && (depth == 0 || isWaterBlocking(x, y - 1, z))) {
    int neighbor = depth + dropOff;
    if (depth >= 8) neighbor = 1;
    if (neighbor >= 8) return;

    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    int dist[4] = {1000, 1000, 1000, 1000};
    bool spread[4] = {false, false, false, false};
    static const int opposite[4] = {1, 0, 3, 2};
    const int maxSlopePass = 6; // expand pit detection by one more block
    std::function<int(int, int, int, int, int)> slopeDistance =
        [&](int wx, int wy, int wz, int pass, int from) -> int {
      int lowest = 1000;
      for (int d = 0; d < 4; ++d) {
        if (from >= 0 && d == opposite[from]) continue;
        int nx = wx + dx[d], nz = wz + dz[d];
        if (isWaterBlocking(nx, wy, nz)) continue;
        int nd = getDepth(nx, wy, nz);
        if (nd == 0) continue;
        if (!isWaterBlocking(nx, wy - 1, nz)) return pass;
        if (pass < maxSlopePass) {
          int v = slopeDistance(nx, wy, nz, pass + 1, d);
          if (v < lowest) lowest = v;
        }
      }
      return lowest;
    };

    for (int d = 0; d < 4; ++d) {
      int nx = x + dx[d], nz = z + dz[d];
      if (isWaterBlocking(nx, y, nz)) continue;
      int nd = getDepth(nx, y, nz);
      if (nd == 0) continue;
      if (!isWaterBlocking(nx, y - 1, nz)) dist[d] = 0;
      else dist[d] = slopeDistance(nx, y, nz, 1, d);
    }
    int lowest = dist[0];
    for (int d = 1; d < 4; ++d) if (dist[d] < lowest) lowest = dist[d];
    for (int d = 0; d < 4; ++d) spread[d] = (dist[d] == lowest);
    for (int d = 0; d < 4; ++d) {
      if (!spread[d]) continue;
      int nx = x + dx[d], nz = z + dz[d];
      if (!canSpreadTo(nx, y, nz)) continue;
      setWaterCell(nx, y, nz, BLOCK_WATER_FLOW, (uint8_t)neighbor);
      scheduleWaterTick(nx, y, nz, 5);
    }
  }
}

void Level::processLavaCell(int x, int y, int z) {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  auto inBounds = [&](int wx, int wy, int wz) {
    return wx >= 0 && wx < maxX && wz >= 0 && wz < maxZ && wy >= 0 && wy < CHUNK_SIZE_Y;
  };
  if (!inBounds(x, y, z)) return;
  uint8_t id = getBlock(x, y, z);
  if (!isLavaBlock(id)) return;

  auto getDepth = [&](int wx, int wy, int wz) -> int {
    if (!inBounds(wx, wy, wz)) return -1;
    uint8_t bid = getBlock(wx, wy, wz);
    if (!isLavaBlock(bid)) return -1;
    uint8_t d = m_lavaDepth[waterIndex(wx, wy, wz)];
    if (d == 0xFF) d = (bid == BLOCK_LAVA_STILL) ? 0 : 1;
    return (int)d;
  };
  auto isLavaBlocking = [&](int wx, int wy, int wz) -> bool {
    if (!inBounds(wx, wy, wz)) return true;
    uint8_t bid = getBlock(wx, wy, wz);
    if (bid == BLOCK_AIR) return false;
    return g_blockProps[bid].isSolid();
  };
  auto canSpreadTo = [&](int wx, int wy, int wz) -> bool {
    if (!inBounds(wx, wy, wz)) return false;
    uint8_t bid = getBlock(wx, wy, wz);
    if (isLavaBlock(bid)) return false;
    return !isLavaBlocking(wx, wy, wz);
  };
  auto setLavaCell = [&](int wx, int wy, int wz, uint8_t bid, uint8_t d) {
    m_inWaterSimUpdate = true;
    setBlock(wx, wy, wz, bid);
    m_inWaterSimUpdate = false;
    m_lavaDepth[waterIndex(wx, wy, wz)] = d;
  };

  int depth = getDepth(x, y, z);
  if (depth < 0) return;
  const int dropOff = 2;
  bool becomeStatic = true;
  int maxCount = 0;

  if (depth > 0) {
    int highest = -100;
    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    for (int i = 0; i < 4; ++i) {
      int nd = getDepth(x + dx[i], y, z + dz[i]);
      if (nd < 0) continue;
      if (nd == 0) maxCount++;
      if (nd >= 8) nd = 0;
      if (highest < 0 || nd < highest) highest = nd;
    }

    int above = getDepth(x, y + 1, z);
    int newDepth = highest + dropOff;
    if (newDepth >= 8 || highest < 0) newDepth = -1;
    if (above >= 0) newDepth = (above >= 8) ? above : (above + 8);

    if (maxCount >= 2) {
      if (isLavaBlocking(x, y - 1, z)) newDepth = 0;
      else if (isLavaBlock(getBlock(x, y - 1, z)) && getDepth(x, y - 1, z) == 0) newDepth = 0;
    }

    if (newDepth != depth) {
      if (newDepth < 0) {
        setLavaCell(x, y, z, BLOCK_AIR, 0xFF);
        wakeLavaNeighborhood(x, y, z, 12);
        return;
      }

      depth = newDepth;
      m_lavaDepth[waterIndex(x, y, z)] = (uint8_t)depth;
      scheduleLavaTick(x, y, z, 12);
      wakeLavaNeighborhood(x, y, z, 12);
    } else {
      if (becomeStatic) setLavaCell(x, y, z, BLOCK_LAVA_STILL, (uint8_t)depth);
    }
  } else {
    setLavaCell(x, y, z, BLOCK_LAVA_STILL, 0);
  }

  if (canSpreadTo(x, y - 1, z)) {
    int d = (depth >= 8) ? depth : (depth + 8);
    if (d > 7) d = 7;
    setLavaCell(x, y - 1, z, BLOCK_LAVA_FLOW, (uint8_t)d);
    scheduleLavaTick(x, y - 1, z, 12);
  } else if (depth >= 0 && (depth == 0 || isLavaBlocking(x, y - 1, z))) {
    int neighbor = depth + dropOff;
    if (depth >= 8) neighbor = 2;
    if (neighbor >= 8) return;

    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    int dist[4] = {1000, 1000, 1000, 1000};
    bool spread[4] = {false, false, false, false};
    static const int opposite[4] = {1, 0, 3, 2};
    const int maxSlopePass = 2;
    std::function<int(int, int, int, int, int)> slopeDistance =
        [&](int wx, int wy, int wz, int pass, int from) -> int {
      int lowest = 1000;
      for (int d = 0; d < 4; ++d) {
        if (from >= 0 && d == opposite[from]) continue;
        int nx = wx + dx[d], nz = wz + dz[d];
        if (isLavaBlocking(nx, wy, nz)) continue;
        int nd = getDepth(nx, wy, nz);
        if (nd == 0) continue;
        if (!isLavaBlocking(nx, wy - 1, nz)) return pass;
        if (pass < maxSlopePass) {
          int v = slopeDistance(nx, wy, nz, pass + 1, d);
          if (v < lowest) lowest = v;
        }
      }
      return lowest;
    };

    for (int d = 0; d < 4; ++d) {
      int nx = x + dx[d], nz = z + dz[d];
      if (isLavaBlocking(nx, y, nz)) continue;
      int nd = getDepth(nx, y, nz);
      if (nd == 0) continue;
      if (!isLavaBlocking(nx, y - 1, nz)) dist[d] = 0;
      else dist[d] = slopeDistance(nx, y, nz, 1, d);
    }
    int lowest = dist[0];
    for (int d = 1; d < 4; ++d) if (dist[d] < lowest) lowest = dist[d];
    for (int d = 0; d < 4; ++d) spread[d] = (dist[d] == lowest);
    for (int d = 0; d < 4; ++d) {
      if (!spread[d]) continue;
      int nx = x + dx[d], nz = z + dz[d];
      if (!canSpreadTo(nx, y, nz)) continue;
      setLavaCell(nx, y, nz, BLOCK_LAVA_FLOW, (uint8_t)neighbor);
      scheduleLavaTick(nx, y, nz, 12);
    }
  }
}

Level::Level() {
  memset(m_chunks, 0, sizeof(m_chunks));
  m_waterDepth.resize(WORLD_CHUNKS_X * CHUNK_SIZE_X * CHUNK_SIZE_Y * WORLD_CHUNKS_Z * CHUNK_SIZE_Z, 0xFF);
  m_waterDue.resize(m_waterDepth.size(), -1);
  m_lavaDepth.resize(m_waterDepth.size(), 0xFF);
  m_lavaDue.resize(m_waterDepth.size(), -1);
  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
      m_chunks[cx][cz] = new Chunk();
}

Level::~Level() {
  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++)
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
      delete m_chunks[cx][cz];
}

Chunk* Level::getChunk(int cx, int cz) const {
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z) return nullptr;
  return m_chunks[cx][cz];
}

void Level::markDirty(int wx, int wy, int wz, uint8_t priority, bool spreadNeighbors) {
  int cx = wx >> 4;
  int cz = wz >> 4;
  int sy = wy >> 4;
  const int subchunkCount = CHUNK_SIZE_Y / 16;
  if (cx >= 0 && cx < WORLD_CHUNKS_X && cz >= 0 && cz < WORLD_CHUNKS_Z && sy >= 0 && sy < subchunkCount) {
    if (priority > m_chunks[cx][cz]->priority[sy]) m_chunks[cx][cz]->priority[sy] = priority;
    m_chunks[cx][cz]->dirty[sy] = true;

    if (spreadNeighbors) {
      int lx = wx & 0xF;
      int lz = wz & 0xF;
      int ly = wy & 0xF;
      auto markNeighbor = [&](int ncx, int ncz, int nsy) {
        if (ncx >= 0 && ncx < WORLD_CHUNKS_X && ncz >= 0 && ncz < WORLD_CHUNKS_Z && nsy >= 0 && nsy < subchunkCount) {
          if (priority > m_chunks[ncx][ncz]->priority[nsy]) m_chunks[ncx][ncz]->priority[nsy] = priority;
          m_chunks[ncx][ncz]->dirty[nsy] = true;
        }
      };
      if (lx == 0) markNeighbor(cx - 1, cz, sy);
      if (lx == 15) markNeighbor(cx + 1, cz, sy);
      if (lz == 0) markNeighbor(cx, cz - 1, sy);
      if (lz == 15) markNeighbor(cx, cz + 1, sy);
      if (ly == 0) markNeighbor(cx, cz, sy - 1);
      if (ly == 15) markNeighbor(cx, cz, sy + 1);
    }
  }
}

uint8_t Level::getBlock(int wx, int wy, int wz) const {
  int cx = wx >> 4;
  int cz = wz >> 4;
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z || wy < 0 || wy >= CHUNK_SIZE_Y) return BLOCK_AIR;
  return m_chunks[cx][cz]->getBlock(wx & 0xF, wy, wz & 0xF);
}

int Level::waterIndex(int wx, int wy, int wz) const {
  return ((wy * (WORLD_CHUNKS_Z * CHUNK_SIZE_Z) + wz) * (WORLD_CHUNKS_X * CHUNK_SIZE_X) + wx);
}

uint8_t Level::getWaterDepth(int wx, int wy, int wz) const {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  if (wx < 0 || wx >= maxX || wz < 0 || wz >= maxZ || wy < 0 || wy >= CHUNK_SIZE_Y) return 0xFF;
  return m_waterDepth[waterIndex(wx, wy, wz)];
}

uint8_t Level::getLavaDepth(int wx, int wy, int wz) const {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  if (wx < 0 || wx >= maxX || wz < 0 || wz >= maxZ || wy < 0 || wy >= CHUNK_SIZE_Y) return 0xFF;
  return m_lavaDepth[waterIndex(wx, wy, wz)];
}

void Level::setWaterDepth(int wx, int wy, int wz, uint8_t depth) {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  if (wx < 0 || wx >= maxX || wz < 0 || wz >= maxZ || wy < 0 || wy >= CHUNK_SIZE_Y) return;
  m_waterDepth[waterIndex(wx, wy, wz)] = depth;
}

void Level::scheduleWaterTick(int wx, int wy, int wz, int delayTicks) {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  if (wx < 0 || wx >= maxX || wz < 0 || wz >= maxZ || wy < 0 || wy >= CHUNK_SIZE_Y) return;
  int idx = waterIndex(wx, wy, wz);
  int due = (int)m_time + delayTicks;
  if (idx < 0 || idx >= (int)m_waterDue.size()) return;
  if (m_waterDue[idx] != -1 && m_waterDue[idx] <= due) return;
  m_waterDue[idx] = due;
  m_waterTicks.push({due, idx});
  m_waterDirty = true;
}

void Level::scheduleLavaTick(int wx, int wy, int wz, int delayTicks) {
  int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;
  if (wx < 0 || wx >= maxX || wz < 0 || wz >= maxZ || wy < 0 || wy >= CHUNK_SIZE_Y) return;
  int idx = waterIndex(wx, wy, wz);
  int due = (int)m_time + delayTicks;
  if (idx < 0 || idx >= (int)m_lavaDue.size()) return;
  if (m_lavaDue[idx] != -1 && m_lavaDue[idx] <= due) return;
  m_lavaDue[idx] = due;
  m_lavaTicks.push({due, idx});
  m_lavaDirty = true;
}

void Level::wakeWaterNeighborhood(int wx, int wy, int wz, int delayTicks) {
  static const int dx[7] = {0, -1, 1, 0, 0, 0, 0};
  static const int dy[7] = {0, 0, 0, -1, 1, 0, 0};
  static const int dz[7] = {0, 0, 0, 0, 0, -1, 1};
  for (int i = 0; i < 7; ++i) {
    int nx = wx + dx[i], ny = wy + dy[i], nz = wz + dz[i];
    if (isWaterBlock(getBlock(nx, ny, nz))) scheduleWaterTick(nx, ny, nz, delayTicks);
  }
}

void Level::wakeLavaNeighborhood(int wx, int wy, int wz, int delayTicks) {
  static const int dx[7] = {0, -1, 1, 0, 0, 0, 0};
  static const int dy[7] = {0, 0, 0, -1, 1, 0, 0};
  static const int dz[7] = {0, 0, 0, 0, 0, -1, 1};
  for (int i = 0; i < 7; ++i) {
    int nx = wx + dx[i], ny = wy + dy[i], nz = wz + dz[i];
    if (isLavaBlock(getBlock(nx, ny, nz))) scheduleLavaTick(nx, ny, nz, delayTicks);
  }
}

void Level::setBlock(int wx, int wy, int wz, uint8_t id) {
  int cx = wx >> 4;
  int cz = wz >> 4;
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z || wy < 0 || wy >= CHUNK_SIZE_Y) return;
  uint8_t oldId = m_chunks[cx][cz]->getBlock(wx & 0xF, wy, wz & 0xF);
  if (oldId == id) return;
  
  m_chunks[cx][cz]->setBlock(wx & 0xF, wy, wz & 0xF, id);
  markDirty(wx, wy, wz);
  if (isWaterBlock(id)) {
    setWaterDepth(wx, wy, wz, (id == BLOCK_WATER_STILL) ? 0 : 1);
    m_lavaDepth[waterIndex(wx, wy, wz)] = 0xFF;
  } else if (isLavaBlock(id)) {
    m_lavaDepth[waterIndex(wx, wy, wz)] = (id == BLOCK_LAVA_STILL) ? 0 : 1;
    setWaterDepth(wx, wy, wz, 0xFF);
  } else {
    setWaterDepth(wx, wy, wz, 0xFF);
    m_lavaDepth[waterIndex(wx, wy, wz)] = 0xFF;
  }

  bool touchesFluid = isWaterBlock(oldId) || isWaterBlock(id) || isLavaBlock(oldId) || isLavaBlock(id);
  if (!touchesFluid) {
    static const int dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int dy[6] = {0, 0, -1, 1, 0, 0};
    static const int dz[6] = {0, 0, 0, 0, -1, 1};
    for (int i = 0; i < 6; ++i) {
      int nx = wx + dx[i], ny = wy + dy[i], nz = wz + dz[i];
      if (ny < 0 || ny >= CHUNK_SIZE_Y) continue;
      uint8_t n = getBlock(nx, ny, nz);
      if (isWaterBlock(n) || isLavaBlock(n)) {
        touchesFluid = true;
        break;
      }
    }
  }
  if (touchesFluid) {
    if (!m_inWaterSimUpdate) {
      m_waterDirty = true;
      m_lavaDirty = true;
      m_waterWakeX = wx;
      m_waterWakeY = wy;
      m_waterWakeZ = wz;
      m_waterWakeTicks = 16;
      m_lavaWakeX = wx;
      m_lavaWakeY = wy;
      m_lavaWakeZ = wz;
      m_lavaWakeTicks = 20;
      wakeWaterNeighborhood(wx, wy, wz, 5);
      wakeLavaNeighborhood(wx, wy, wz, 12);
    }
  }

  updateLight(wx, wy, wz);
}

std::vector<AABB> Level::getCubes(const AABB& box) const {
  std::vector<AABB> boxes;
  int x0 = (int)floorf(box.x0);
  int x1 = (int)floorf(box.x1 + 1.0f);
  int y0 = (int)floorf(box.y0);
  int y1 = (int)floorf(box.y1 + 1.0f);
  int z0 = (int)floorf(box.z0);
  int z1 = (int)floorf(box.z1 + 1.0f);

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (z0 < 0) z0 = 0;
  if (x1 > WORLD_CHUNKS_X * CHUNK_SIZE_X) x1 = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  if (y1 > CHUNK_SIZE_Y) y1 = CHUNK_SIZE_Y;
  if (z1 > WORLD_CHUNKS_Z * CHUNK_SIZE_Z) z1 = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;

  for (int x = x0; x < x1; x++) {
    for (int y = y0; y < y1; y++) {
      for (int z = z0; z < z1; z++) {
        uint8_t id = getBlock(x, y, z);
        if (id > 0 && g_blockProps[id].isSolid()) {
          // Create bounding box
          boxes.push_back(AABB((double)x, (double)y, (double)z,
                               (double)(x + 1), (double)(y + 1), (double)(z + 1)));
        }
      }
    }
  }
  return boxes;
}

uint8_t Level::getSkyLight(int wx, int wy, int wz) const {
  int cx = wx >> 4;
  int cz = wz >> 4;
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z || wy < 0 || wy >= CHUNK_SIZE_Y) return 15;
  return m_chunks[cx][cz]->getSkyLight(wx & 0xF, wy, wz & 0xF);
}

uint8_t Level::getBlockLight(int wx, int wy, int wz) const {
  int cx = wx >> 4;
  int cz = wz >> 4;
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z || wy < 0 || wy >= CHUNK_SIZE_Y) return 0;
  return m_chunks[cx][cz]->getBlockLight(wx & 0xF, wy, wz & 0xF);
}

void Level::setSkyLight(int wx, int wy, int wz, uint8_t val) {
  int cx = wx >> 4;
  int cz = wz >> 4;
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z || wy < 0 || wy >= CHUNK_SIZE_Y) return;
  uint8_t curBlock = m_chunks[cx][cz]->getBlockLight(wx & 0xF, wy, wz & 0xF);
  m_chunks[cx][cz]->setLight(wx & 0xF, wy, wz & 0xF, val, curBlock);
}

void Level::setBlockLight(int wx, int wy, int wz, uint8_t val) {
  int cx = wx >> 4;
  int cz = wz >> 4;
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cz < 0 || cz >= WORLD_CHUNKS_Z || wy < 0 || wy >= CHUNK_SIZE_Y) return;
  uint8_t curSky = m_chunks[cx][cz]->getSkyLight(wx & 0xF, wy, wz & 0xF);
  m_chunks[cx][cz]->setLight(wx & 0xF, wy, wz & 0xF, curSky, val);
}

bool Level::saveToFile(const char *path) const {
  if (!path) return false;
  FILE *f = fopen(path, "wb");
  if (!f) return false;

  struct SaveHeader {
    char magic[8];
    uint32_t version;
    uint32_t chunksX;
    uint32_t chunksZ;
    uint32_t chunkY;
    int64_t time;
    uint32_t waterSize;
  } hdr;

  memcpy(hdr.magic, "MCPSPWLD", 8);
  hdr.version = 3;
  hdr.chunksX = WORLD_CHUNKS_X;
  hdr.chunksZ = WORLD_CHUNKS_Z;
  hdr.chunkY = CHUNK_SIZE_Y;
  hdr.time = m_time;
  hdr.waterSize = (uint32_t)m_waterDepth.size();
  if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }

  for (int cx = 0; cx < WORLD_CHUNKS_X; ++cx) {
    for (int cz = 0; cz < WORLD_CHUNKS_Z; ++cz) {
      const Chunk *ch = m_chunks[cx][cz];
      if (fwrite(ch->blocks, sizeof(ch->blocks), 1, f) != 1) { fclose(f); return false; }
      if (fwrite(ch->light, sizeof(ch->light), 1, f) != 1) { fclose(f); return false; }
    }
  }

  if (!m_waterDepth.empty()) {
    if (fwrite(m_waterDepth.data(), 1, m_waterDepth.size(), f) != m_waterDepth.size()) {
      fclose(f);
      return false;
    }
  }
  if (!m_lavaDepth.empty()) {
    if (fwrite(m_lavaDepth.data(), 1, m_lavaDepth.size(), f) != m_lavaDepth.size()) {
      fclose(f);
      return false;
    }
  }

  fflush(f);
  fclose(f);
  return true;
}

bool Level::loadFromFile(const char *path) {
  if (!path) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;

  struct SaveHeader {
    char magic[8];
    uint32_t version;
    uint32_t chunksX;
    uint32_t chunksZ;
    uint32_t chunkY;
    int64_t time;
    uint32_t waterSize;
  } hdr;

  if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }
  if (memcmp(hdr.magic, "MCPSPWLD", 8) != 0 || (hdr.version != 1 && hdr.version != 2 && hdr.version != 3)) { fclose(f); return false; }
  if (hdr.chunksX != WORLD_CHUNKS_X || hdr.chunksZ != WORLD_CHUNKS_Z || hdr.chunkY != CHUNK_SIZE_Y) { fclose(f); return false; }
  if (hdr.waterSize != m_waterDepth.size()) { fclose(f); return false; }

  for (int cx = 0; cx < WORLD_CHUNKS_X; ++cx) {
    for (int cz = 0; cz < WORLD_CHUNKS_Z; ++cz) {
      Chunk *ch = m_chunks[cx][cz];
      ch->cx = cx;
      ch->cz = cz;
      if (fread(ch->blocks, sizeof(ch->blocks), 1, f) != 1) { fclose(f); return false; }
      if (fread(ch->light, sizeof(ch->light), 1, f) != 1) { fclose(f); return false; }
      for (int sy = 0; sy < (CHUNK_SIZE_Y / 16); ++sy) ch->dirty[sy] = true;
    }
  }

  if (!m_waterDepth.empty()) {
    if (fread(m_waterDepth.data(), 1, m_waterDepth.size(), f) != m_waterDepth.size()) {
      fclose(f);
      return false;
    }
  }
  if (hdr.version >= 3 && !m_lavaDepth.empty()) {
    if (fread(m_lavaDepth.data(), 1, m_lavaDepth.size(), f) != m_lavaDepth.size()) {
      fclose(f);
      return false;
    }
  }

  fclose(f);
  m_time = hdr.time;
  while (!m_waterTicks.empty()) m_waterTicks.pop();
  while (!m_lavaTicks.empty()) m_lavaTicks.pop();
  std::fill(m_waterDue.begin(), m_waterDue.end(), -1);
  std::fill(m_lavaDue.begin(), m_lavaDue.end(), -1);
  for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
    for (int z = 0; z < WORLD_CHUNKS_Z * CHUNK_SIZE_Z; ++z) {
      for (int x = 0; x < WORLD_CHUNKS_X * CHUNK_SIZE_X; ++x) {
        uint8_t id = getBlock(x, y, z);
        if (isWaterBlock(id)) scheduleWaterTick(x, y, z, 1);
        else if (isLavaBlock(id)) {
          if (hdr.version < 3) m_lavaDepth[waterIndex(x, y, z)] = (id == BLOCK_LAVA_STILL) ? 0 : 1;
          scheduleLavaTick(x, y, z, 4);
        } else if (hdr.version < 3) {
          m_lavaDepth[waterIndex(x, y, z)] = 0xFF;
        }
      }
    }
  }
  m_waterDirty = !m_waterTicks.empty();
  m_lavaDirty = !m_lavaTicks.empty();
  return true;
}

void Level::generate(Random *rng) {
  int64_t seed = rng->nextLong();

  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
      Chunk *c = m_chunks[cx][cz];
      c->cx = cx;
      c->cz = cz;
      WorldGen::generateChunk(c->blocks, cx, cz, seed);
      for(int i=0; i<(CHUNK_SIZE_Y / 16); i++) c->dirty[i] = true;
    }
  }

  m_deferLightUpdates = true;
  m_lightUpdatePending = false;

  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
      Random chunkRng(seed ^ ((int64_t)cx * 341873128712LL) ^ ((int64_t)cz * 132897987541LL));
      for (int i = 0; i < 3; i++) {
        int lx = chunkRng.nextInt(CHUNK_SIZE_X);
        int lz = chunkRng.nextInt(CHUNK_SIZE_Z);
        int wx = cx * CHUNK_SIZE_X + lx;
        int wz = cz * CHUNK_SIZE_Z + lz;

        int wy = CHUNK_SIZE_Y - 1;
        while (wy > 0 && getBlock(wx, wy, wz) == BLOCK_AIR) wy--;

        if (wy > 50 && getBlock(wx, wy, wz) == BLOCK_GRASS) {
          setBlock(wx, wy, wz, BLOCK_DIRT);
          TreeFeature::place(this, wx, wy + 1, wz, chunkRng);
        }
      }
    }
  }

  m_deferLightUpdates = false;

  for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
    for (int z = 0; z < WORLD_CHUNKS_Z * CHUNK_SIZE_Z; ++z) {
      for (int x = 0; x < WORLD_CHUNKS_X * CHUNK_SIZE_X; ++x) {
        uint8_t id = getBlock(x, y, z);
        if (id == BLOCK_WATER_STILL) setWaterDepth(x, y, z, 0);
        else if (id == BLOCK_WATER_FLOW) setWaterDepth(x, y, z, 1);
        else setWaterDepth(x, y, z, 0xFF);
        if (id == BLOCK_LAVA_STILL) m_lavaDepth[waterIndex(x, y, z)] = 0;
        else if (id == BLOCK_LAVA_FLOW) m_lavaDepth[waterIndex(x, y, z)] = 1;
        else m_lavaDepth[waterIndex(x, y, z)] = 0xFF;
      }
    }
  }
  while (!m_waterTicks.empty()) m_waterTicks.pop();
  while (!m_lavaTicks.empty()) m_lavaTicks.pop();
  std::fill(m_waterDue.begin(), m_waterDue.end(), -1);
  std::fill(m_lavaDue.begin(), m_lavaDue.end(), -1);

  if (m_lightUpdatePending) {
    m_lightUpdatePending = false;
  }
  computeLighting();
}

void Level::computeLighting() {
  m_lightUpdatePending = false;
  resetLight(LightLayer::Sky);
  resetLight(LightLayer::Block);
}

uint8_t Level::getData(LightLayer layer, int wx, int wy, int wz) const {
  return (layer == LightLayer::Sky) ? getSkyLight(wx, wy, wz) : getBlockLight(wx, wy, wz);
}

void Level::setData(LightLayer layer, int wx, int wy, int wz, uint8_t val) {
  if (layer == LightLayer::Sky) {
    setSkyLight(wx, wy, wz, val);
  } else {
    setBlockLight(wx, wy, wz, val);
  }
}

uint8_t Level::getTileLightOpacity(int wx, int wy, int wz) const {
  uint8_t id = getBlock(wx, wy, wz);
  const BlockProps &props = g_blockProps[id];
  if (props.isOpaque()) return 15;
  if (id == BLOCK_LEAVES) return 2;
  if (props.isLiquid()) return 3;
  return 1;
}

uint8_t Level::getTileBrightness(LightLayer layer, int wx, int wy, int wz) const {
  if (layer == LightLayer::Block) {
    return g_blockProps[getBlock(wx, wy, wz)].light_emit;
  }

  if (wy == CHUNK_SIZE_Y - 1) {
    return 15;
  }

  uint8_t aboveOpacity = getTileLightOpacity(wx, wy + 1, wz);
  uint8_t aboveLight = getSkyLight(wx, wy + 1, wz);
  if (aboveLight == 15 && aboveOpacity < 15) {
    return 15;
  }
  return 0;
}

void Level::resetLight(LightLayer layer) {
  const int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  const int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;

  std::vector<LightUpdate> lightUpdates;
  lightUpdates.reserve(maxX * maxZ * 4);

  for (int x = 0; x < maxX; ++x) {
    for (int z = 0; z < maxZ; ++z) {
      for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
        uint8_t base = getTileBrightness(layer, x, y, z);
        setData(layer, x, y, z, base);
        if (base > 0) {
          lightUpdates.push_back({(short)x, (short)y, (short)z, base});
        }
      }
    }
  }

  propagateLight(layer, lightUpdates);
}

void Level::propagateLight(LightLayer layer, std::vector<LightUpdate> &queue) {
  static const int xFaces[6] = {-1, 1, 0, 0, 0, 0};
  static const int yFaces[6] = {0, 0, -1, 1, 0, 0};
  static const int zFaces[6] = {0, 0, 0, 0, -1, 1};

  const int maxX = WORLD_CHUNKS_X * CHUNK_SIZE_X;
  const int maxZ = WORLD_CHUNKS_Z * CHUNK_SIZE_Z;

  size_t i = 0;
  while (i < queue.size()) {
    LightUpdate entry = queue[i++];
    uint8_t level = getData(layer, entry.x, entry.y, entry.z);
    if (level <= 1) continue;

    for (int face = 0; face < 6; ++face) {
      int nx = entry.x + xFaces[face];
      int ny = entry.y + yFaces[face];
      int nz = entry.z + zFaces[face];

      if (ny < 0 || ny >= CHUNK_SIZE_Y || nx < 0 || nx >= maxX || nz < 0 || nz >= maxZ) continue;

      int opacity = getTileLightOpacity(nx, ny, nz);
      if (opacity >= 15) continue;

      int falloff = opacity;
      if (layer == LightLayer::Sky && yFaces[face] == -1 && level == 15) {
        falloff = 0;
      }

      int candidate = (int)level - falloff;
      if (candidate <= 0) continue;

      uint8_t current = getData(layer, nx, ny, nz);
      if (candidate > current) {
        uint8_t next = (candidate > 15) ? 15 : (uint8_t)candidate;
        setData(layer, nx, ny, nz, next);
        queue.push_back({(short)nx, (short)ny, (short)nz, next});
      }
    }
  }
}

void Level::updateLight(int wx, int wy, int wz) {
  (void)wx;
  (void)wy;
  (void)wz;
  if (m_deferLightUpdates) {
    m_lightUpdatePending = true;
    return;
  }
  computeLighting();
}
