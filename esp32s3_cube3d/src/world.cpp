#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {

namespace {

float terrainNoise(float x, float z) {
  return sinf(x * 0.62f) + cosf(z * 0.54f) + 0.55f * sinf((x + z) * 0.31f) +
         0.35f * cosf((x - z) * 0.73f);
}

float caveNoise(float x, float y, float z) {
  return sinf(x * 0.87f + y * 0.41f) + cosf(z * 0.79f - y * 0.36f) +
         0.62f * sinf((x + z) * 0.34f + y * 0.93f);
}

float oreNoise(float x, float y, float z) {
  return cosf(x * 1.21f + y * 0.77f) + sinf(z * 1.07f - y * 0.68f) +
         0.41f * cosf((x - z) * 0.83f + y * 0.55f);
}

}  // namespace

void clearWorld() {
  for (int x = 0; x < kWorldW; ++x) {
    for (int y = 0; y < kWorldHMax; ++y) {
      for (int z = 0; z < kWorldD; ++z) {
        s_voxel[x][y][z] = BLOCK_AIR;
      }
    }
  }
}

void buildWorld() {
  clearWorld();
  for (int x = 0; x < kWorldW; ++x) {
    for (int z = 0; z < kWorldD; ++z) {
      const float n = terrainNoise(static_cast<float>(x), static_cast<float>(z));
      int h = static_cast<int>(5.8f + n * 1.65f);
      h = std::max(3, std::min(kWorldHMax - 1, h));

      for (int y = 0; y < h; ++y) {
        if (y == h - 1) {
          s_voxel[x][y][z] = BLOCK_GRASS;
        } else if (y >= h - 3) {
          s_voxel[x][y][z] = BLOCK_DIRT;
        } else {
          s_voxel[x][y][z] = BLOCK_STONE;
        }
      }

      s_voxel[x][0][z] = BLOCK_BEDROCK;  // Unbreakable bottom layer.

      // Carve simple caves and sparse ore underground.
      if (h >= 6) {
        for (int y = 2; y <= h - 3; ++y) {
          const float cn = caveNoise(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
          if (cn > 2.04f) {
            s_voxel[x][y][z] = BLOCK_AIR;
            continue;
          }
          if (s_voxel[x][y][z] == BLOCK_STONE) {
            const float on = oreNoise(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            if (on > 1.78f) {
              s_voxel[x][y][z] = BLOCK_ORE;
            }
          }
        }
      }
    }
  }
}

bool isSolidVoxel(int x, int y, int z) {
  if (x < 0 || z < 0 || x >= kWorldW || z >= kWorldD) {
    return false;
  }
  if (y < 0 || y >= kWorldHMax) {
    return false;
  }
  return s_voxel[x][y][z] != BLOCK_AIR;
}

void setVoxel(int x, int y, int z, uint8_t blockId) {
  if (x < 0 || z < 0 || x >= kWorldW || z >= kWorldD) {
    return;
  }
  if (y < 0 || y >= kWorldHMax) {
    return;
  }
  s_voxel[x][y][z] = blockId;
}

int supportYBelowPlayer(int x, int z, float camY) {
  if (x < 0 || z < 0 || x >= kWorldW || z >= kWorldD) {
    return -1;
  }
  int y0 = static_cast<int>(floorf(camY - kEyeHeight));
  y0 = std::max(0, std::min(kWorldHMax - 1, y0));
  for (int y = y0; y >= 0; --y) {
    if (isSolidVoxel(x, y, z)) {
      return y;
    }
  }
  return -1;
}

bool isPlayerCollidingAt(float camX, float camY, float camZ) {
  const float foot = camY - kEyeHeight;
  const float head = foot + kPlayerHeight;

  const float minX = camX - kPlayerRadius;
  const float maxX = camX + kPlayerRadius;
  const float minY = foot + 0.02f;
  const float maxY = head - 0.02f;
  const float minZ = camZ - kPlayerRadius;
  const float maxZ = camZ + kPlayerRadius;

  if (minY < 0.0f) {
    return true;
  }

  const int x0 = static_cast<int>(floorf(minX));
  const int x1 = static_cast<int>(floorf(maxX));
  const int y0 = std::max(0, static_cast<int>(floorf(minY)));
  const int y1 = std::min(kWorldHMax - 1, static_cast<int>(floorf(maxY)));
  const int z0 = static_cast<int>(floorf(minZ));
  const int z1 = static_cast<int>(floorf(maxZ));

  if (y0 > y1) {
    return false;
  }

  for (int x = x0; x <= x1; ++x) {
    for (int y = y0; y <= y1; ++y) {
      for (int z = z0; z <= z1; ++z) {
        if (isSolidVoxel(x, y, z)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool inWorldXYZ(int x, int y, int z) {
  return x >= 0 && x < kWorldW && y >= 0 && y < kWorldHMax && z >= 0 && z < kWorldD;
}

RayHit raycastCenter() {
  const Vec3 dir = {s_camSy * s_camCp, s_camSp, s_camCy * s_camCp};
  constexpr float kStep = 0.08f;
  constexpr float kMaxDist = 12.0f;
  bool havePrevAir = false;
  int prevX = 0;
  int prevY = 0;
  int prevZ = 0;

  for (float t = 0.2f; t <= kMaxDist; t += kStep) {
    const float px = s_camX + dir.x * t;
    const float py = s_camY + dir.y * t;
    const float pz = s_camZ + dir.z * t;
    const int vx = static_cast<int>(floorf(px));
    const int vy = static_cast<int>(floorf(py));
    const int vz = static_cast<int>(floorf(pz));

    if (isSolidVoxel(vx, vy, vz)) {
      RayHit out = {true, vx, vy, vz, vx, vy + 1, vz, 0, 1, 0};
      if (havePrevAir) {
        out.prevX = prevX;
        out.prevY = prevY;
        out.prevZ = prevZ;
        const int dx = prevX - vx;
        const int dy = prevY - vy;
        const int dz = prevZ - vz;
        const int ax = abs(dx);
        const int ay = abs(dy);
        const int az = abs(dz);
        if (ay >= ax && ay >= az) {
          out.nx = 0;
          out.ny = (dy >= 0) ? 1 : -1;
          out.nz = 0;
        } else if (ax >= az) {
          out.nx = (dx >= 0) ? 1 : -1;
          out.ny = 0;
          out.nz = 0;
        } else {
          out.nx = 0;
          out.ny = 0;
          out.nz = (dz >= 0) ? 1 : -1;
        }
      }
      return out;
    }

    prevX = vx;
    prevY = vy;
    prevZ = vz;
    havePrevAir = true;
  }

  return {false, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}

}  // namespace game
