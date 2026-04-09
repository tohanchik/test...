#include "world/MobSystem.h"

#include "world/AABB.h"
#include "world/Blocks.h"
#include <math.h>

MobSystem::MobSystem(Level *level) : m_level(level) {}

void MobSystem::spawnPig(float x, float y, float z) {
  Mob pig;
  pig.type = TYPE_PIG;
  pig.x = x;
  pig.y = y;
  pig.z = z;
  pig.velX = 0.0f;
  pig.velY = 0.0f;
  pig.velZ = 0.0f;
  pig.onGround = false;
  pig.removed = false;
  m_mobs.push_back(pig);
}

void MobSystem::update(float dt) {
  if (!m_level || m_mobs.empty()) return;

  float ticksLeft = dt * 20.0f;
  if (ticksLeft < 0.0f) ticksLeft = 0.0f;

  while (ticksLeft > 0.0f) {
    float step = (ticksLeft > 1.0f) ? 1.0f : ticksLeft;
    ticksLeft -= step;

    for (size_t i = 0; i < m_mobs.size(); ++i) {
      if (!m_mobs[i].removed) tickMobPhysics(m_mobs[i], step);
    }
  }
}

void MobSystem::tickMobPhysics(Mob &mob, float step) {
  const float R = 0.45f; // MC pig width 0.9
  const float H = 0.9f;  // MC pig height 0.9

  uint8_t feetBlock = m_level->getBlock((int)floorf(mob.x), (int)floorf(mob.y), (int)floorf(mob.z));
  uint8_t headBlock = m_level->getBlock((int)floorf(mob.x), (int)floorf(mob.y + H), (int)floorf(mob.z));
  bool inWater = g_blockProps[feetBlock].isLiquid() || g_blockProps[headBlock].isLiquid();

  AABB waterCheckBox(mob.x - R, mob.y, mob.z - R, mob.x + R, mob.y + H, mob.z + R);
  inWater = m_level->applyWaterCurrent(waterCheckBox, mob.velX, mob.velY, mob.velZ) || inWater;

  if (inWater) {
    mob.velY -= 0.02f * step;
  } else {
    mob.velY -= 0.08f * step;
    mob.velY *= powf(0.98f, step);
  }

  float dx = mob.velX * step;
  float dy = mob.velY * step;
  float dz = mob.velZ * step;
  float expectedDy = dy;

  AABB mobAabb(mob.x - R, mob.y, mob.z - R, mob.x + R, mob.y + H, mob.z + R);
  AABB *expanded = mobAabb.expand(dx, dy, dz);
  std::vector<AABB> cubes = m_level->getCubes(*expanded);
  delete expanded;

  for (size_t i = 0; i < cubes.size(); ++i) dy = cubes[i].clipYCollide(&mobAabb, dy);
  mobAabb.move(0.0f, dy, 0.0f);
  for (size_t i = 0; i < cubes.size(); ++i) dx = cubes[i].clipXCollide(&mobAabb, dx);
  mobAabb.move(dx, 0.0f, 0.0f);
  for (size_t i = 0; i < cubes.size(); ++i) dz = cubes[i].clipZCollide(&mobAabb, dz);
  mobAabb.move(0.0f, 0.0f, dz);

  mob.onGround = (expectedDy != dy && expectedDy < 0.0f);
  if (dy != expectedDy) mob.velY = 0.0f;
  if (dx != mob.velX * step) mob.velX = 0.0f;
  if (dz != mob.velZ * step) mob.velZ = 0.0f;

  mob.x = (mobAabb.x0 + mobAabb.x1) * 0.5f;
  mob.y = mobAabb.y0;
  mob.z = (mobAabb.z0 + mobAabb.z1) * 0.5f;

  if (inWater) {
    mob.velX *= powf(0.80f, step);
    mob.velY *= powf(0.80f, step);
    mob.velZ *= powf(0.80f, step);
  } else {
    const float friction = mob.onGround ? (0.6f * 0.91f) : 0.91f;
    mob.velX *= powf(friction, step);
    mob.velZ *= powf(friction, step);
  }

  const float WORLD_MAX_X = (float)(WORLD_CHUNKS_X * CHUNK_SIZE_X - 1);
  const float WORLD_MAX_Z = (float)(WORLD_CHUNKS_Z * CHUNK_SIZE_Z - 1);
  if (mob.x < 0.5f) mob.x = 0.5f;
  if (mob.x > WORLD_MAX_X) mob.x = WORLD_MAX_X;
  if (mob.z < 0.5f) mob.z = 0.5f;
  if (mob.z > WORLD_MAX_Z) mob.z = WORLD_MAX_Z;
}
