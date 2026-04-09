#include "render/MobRenderer.h"

#include "world/Player.h"
#include "world/Mth.h"
#include <pspgu.h>
#include <vector>

struct MobVertex {
  float u, v;
  uint32_t color;
  float x, y, z;
};

struct UVRect {
  float u0, v0, u1, v1;
};

struct BoxDef {
  float x0, y0, z0;
  float x1, y1, z1;
  int texX, texY;
  int w, h, d;
};

MobRenderer::MobRenderer() : m_hasPigTexture(false) {}

bool MobRenderer::loadPigTexture(const char *path) {
  m_hasPigTexture = m_pigTexture.load(path);
  return m_hasPigTexture;
}

static inline UVRect uvRect(const BoxDef &box, int face) {
  const float eps = 0.5f;
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
  if (face == 0) { // right
    x0 = (float)(box.texX + box.d + box.w);
    y0 = (float)(box.texY + box.d);
    x1 = (float)(box.texX + box.d + box.w + box.d);
    y1 = (float)(box.texY + box.d + box.h);
  } else if (face == 1) { // left
    x0 = (float)(box.texX + 0);
    y0 = (float)(box.texY + box.d);
    x1 = (float)(box.texX + box.d);
    y1 = (float)(box.texY + box.d + box.h);
  } else if (face == 2) { // top
    x0 = (float)(box.texX + box.d);
    y0 = (float)(box.texY + 0);
    x1 = (float)(box.texX + box.d + box.w);
    y1 = (float)(box.texY + box.d);
  } else if (face == 3) { // bottom
    x0 = (float)(box.texX + box.d + box.w);
    y0 = (float)(box.texY + box.d);
    x1 = (float)(box.texX + box.d + box.w + box.w);
    y1 = (float)(box.texY + 0);
  } else if (face == 4) { // front
    x0 = (float)(box.texX + box.d);
    y0 = (float)(box.texY + box.d);
    x1 = (float)(box.texX + box.d + box.w);
    y1 = (float)(box.texY + box.d + box.h);
  } else { // back
    x0 = (float)(box.texX + box.d + box.w + box.d);
    y0 = (float)(box.texY + box.d);
    x1 = (float)(box.texX + box.d + box.w + box.d + box.w);
    y1 = (float)(box.texY + box.d + box.h);
  }

  UVRect uv;
  uv.u0 = x0 + eps;
  uv.v0 = y0 + eps;
  uv.u1 = x1 - eps;
  uv.v1 = y1 - eps;
  return uv;
}

static inline void addFace(MobVertex *verts, int &v,
                           float ax, float ay, float az,
                           float bx, float by, float bz,
                           float cx, float cy, float cz,
                           float dx, float dy, float dz,
                           const UVRect &uv, uint32_t col) {
  verts[v++] = {uv.u0, uv.v0, col, ax, ay, az};
  verts[v++] = {uv.u1, uv.v0, col, bx, by, bz};
  verts[v++] = {uv.u1, uv.v1, col, cx, cy, cz};
  verts[v++] = {uv.u0, uv.v0, col, ax, ay, az};
  verts[v++] = {uv.u1, uv.v1, col, cx, cy, cz};
  verts[v++] = {uv.u0, uv.v1, col, dx, dy, dz};
}

static inline void addBox(MobVertex *verts, int &v, const BoxDef &b,
                          float ox, float oy, float oz, uint32_t col) {
  const float x0 = ox + b.x0;
  const float y0 = oy + b.y0;
  const float z0 = oz + b.z0;
  const float x1 = ox + b.x1;
  const float y1 = oy + b.y1;
  const float z1 = oz + b.z1;

  addFace(verts, v, x1, y0, z1, x1, y0, z0, x1, y1, z0, x1, y1, z1, uvRect(b, 0), col); // right
  addFace(verts, v, x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0, uvRect(b, 1), col); // left
  addFace(verts, v, x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1, uvRect(b, 2), col); // top
  addFace(verts, v, x0, y0, z1, x1, y0, z1, x1, y0, z0, x0, y0, z0, uvRect(b, 3), col); // bottom
  addFace(verts, v, x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0, uvRect(b, 4), col); // front
  addFace(verts, v, x1, y0, z1, x0, y0, z1, x0, y1, z1, x1, y1, z1, uvRect(b, 5), col); // back
}

void MobRenderer::render(const MobSystem &mobs, const Player *player, float sunBrightness) {
  if (!m_hasPigTexture) return;

  const std::vector<MobSystem::Mob> &mobList = mobs.getMobs();
  if (mobList.empty()) return;

  const float px = player ? player->getX() : 0.0f;
  const float py = player ? player->getY() : 0.0f;
  const float pz = player ? player->getZ() : 0.0f;
  const float maxDistSq = 96.0f * 96.0f;

  std::vector<size_t> visible;
  visible.reserve(mobList.size());
  for (size_t i = 0; i < mobList.size(); ++i) {
    const MobSystem::Mob &m = mobList[i];
    if (m.removed || m.type != MobSystem::TYPE_PIG) continue;
    float dx = m.x - px;
    float dy = m.y - py;
    float dz = m.z - pz;
    if ((dx * dx + dy * dy + dz * dz) > maxDistSq) continue;
    visible.push_back(i);
  }
  if (visible.empty()) return;

  uint8_t g = (uint8_t)Mth::clamp((int)(255.0f * (sunBrightness * 0.85f + 0.15f)), 0, 255);
  uint32_t col = 0xFF000000u | ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;

  m_pigTexture.bind();
  sceGuEnable(GU_CULL_FACE);
  sceGuFrontFace(GU_CW);

  static const BoxDef kHead = {-0.25f, 0.375f, -0.875f, 0.25f, 0.875f, -0.375f, 0, 0, 8, 8, 8};
  static const BoxDef kSnout = {-0.125f, 0.5f, -0.9375f, 0.125f, 0.6875f, -0.875f, 16, 16, 4, 3, 1};
  static const BoxDef kBody = {-0.3125f, 0.375f, -0.5f, 0.3125f, 0.875f, 0.5f, 28, 8, 10, 8, 16};
  static const BoxDef kLegFL = {-0.3125f, 0.0f, -0.375f, -0.0625f, 0.375f, -0.125f, 0, 16, 4, 6, 4};
  static const BoxDef kLegFR = {0.0625f, 0.0f, -0.375f, 0.3125f, 0.375f, -0.125f, 0, 16, 4, 6, 4};
  static const BoxDef kLegBL = {-0.3125f, 0.0f, 0.125f, -0.0625f, 0.375f, 0.375f, 0, 16, 4, 6, 4};
  static const BoxDef kLegBR = {0.0625f, 0.0f, 0.125f, 0.3125f, 0.375f, 0.375f, 0, 16, 4, 6, 4};

  MobVertex *verts = (MobVertex *)sceGuGetMemory((int)visible.size() * 6 * 6 * 7 * sizeof(MobVertex));
  int v = 0;
  for (size_t i = 0; i < visible.size(); ++i) {
    const MobSystem::Mob &m = mobList[visible[i]];
    addBox(verts, v, kHead, m.x, m.y, m.z, col);
    addBox(verts, v, kSnout, m.x, m.y, m.z, col);
    addBox(verts, v, kBody, m.x, m.y, m.z, col);
    addBox(verts, v, kLegFL, m.x, m.y, m.z, col);
    addBox(verts, v, kLegFR, m.x, m.y, m.z, col);
    addBox(verts, v, kLegBL, m.x, m.y, m.z, col);
    addBox(verts, v, kLegBR, m.x, m.y, m.z, col);
  }

  sceGuDrawArray(GU_TRIANGLES,
                 GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                 v, 0, verts);
  sceGuFrontFace(GU_CCW);
}
