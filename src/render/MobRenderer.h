#pragma once

#include "render/Texture.h"
#include "world/MobSystem.h"

class Player;

class MobRenderer {
public:
  MobRenderer();

  bool loadPigTexture(const char *path);
  void render(const MobSystem &mobs, const Player *player, float sunBrightness);

private:
  Texture m_pigTexture;
  bool m_hasPigTexture;
};
