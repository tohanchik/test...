#pragma once

#include "world/Level.h"
#include <vector>

class MobSystem {
public:
  enum Type {
    TYPE_PIG = 0,
  };

  struct Mob {
    Type type;
    float x, y, z;
    float velX, velY, velZ;
    bool onGround;
    bool removed;
  };

  explicit MobSystem(Level *level);

  void spawnPig(float x, float y, float z);
  void update(float dt);

  const std::vector<Mob> &getMobs() const { return m_mobs; }

private:
  void tickMobPhysics(Mob &mob, float step);

  Level *m_level;
  std::vector<Mob> m_mobs;
};
