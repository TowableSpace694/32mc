#include "rendering.h"

#include "controls.h"
#include "world.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {

float cameraSpaceZ(float wx, float wy, float wz) {
  const float dx = wx - s_camX;
  const float dy = wy - s_camY;
  const float dz = wz - s_camZ;
  const float yawZ = dx * s_camSy + dz * s_camCy;
  return dy * s_camSp + yawZ * s_camCp;
}

void tryAddFace(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, uint16_t color) {
  if (s_faceCount >= kMaxFaces) {
    return;
  }
  ProjVert p0;
  ProjVert p1;
  ProjVert p2;
  ProjVert p3;
  if (!projectToScreen(a, p0) || !projectToScreen(b, p1) || !projectToScreen(c, p2) ||
      !projectToScreen(d, p3)) {
    return;
  }
  const int16_t minX = std::min(std::min(p0.sx, p1.sx), std::min(p2.sx, p3.sx));
  const int16_t maxX = std::max(std::max(p0.sx, p1.sx), std::max(p2.sx, p3.sx));
  const int16_t minY = std::min(std::min(p0.sy, p1.sy), std::min(p2.sy, p3.sy));
  const int16_t maxY = std::max(std::max(p0.sy, p1.sy), std::max(p2.sy, p3.sy));
  if (maxX < 0 || minX >= kScreenW || maxY < 0 || minY >= kScreenH) {
    return;
  }
  FaceQuad &f = s_faces[s_faceCount++];
  f.p[0] = p0;
  f.p[1] = p1;
  f.p[2] = p2;
  f.p[3] = p3;
  f.depth = (p0.cz + p1.cz + p2.cz + p3.cz) * 0.25f;
  f.color = color;
}

void drawRemotePlayers() {
  constexpr uint16_t kBody = rgb565(242, 106, 88);
  constexpr uint16_t kOutline = rgb565(255, 226, 86);

  for (int i = 0; i < kRemotePlayerMax; ++i) {
    const RemotePlayerView &rp = s_remotePlayers[i];
    if (!rp.active) {
      continue;
    }
    const Vec3 feet = {rp.x, rp.feetY, rp.z};
    const Vec3 head = {rp.x, rp.feetY + 1.75f, rp.z};
    ProjVert pFeet;
    ProjVert pHead;
    if (!projectToScreen(feet, pFeet) || !projectToScreen(head, pHead)) {
      continue;
    }
    int yTop = std::min<int>(pHead.sy, pFeet.sy);
    int yBottom = std::max<int>(pHead.sy, pFeet.sy);
    int h = yBottom - yTop;
    if (h < 4) {
      h = 4;
      yBottom = yTop + h;
    }
    if (h > kScreenH) {
      h = kScreenH;
      yBottom = yTop + h;
    }
    int w = std::max<int>(2, h / 4);
    const int xLeft = pHead.sx - w / 2;
    const int xRight = xLeft + w - 1;

    if (xRight < 0 || xLeft >= kScreenW || yBottom < 0 || yTop >= kScreenH) {
      continue;
    }

    const int clipX0 = std::max(0, xLeft);
    const int clipY0 = std::max(0, yTop);
    const int clipX1 = std::min(kScreenW - 1, xRight);
    const int clipY1 = std::min(kScreenH - 1, yBottom);

    canvas.fillRect(clipX0, clipY0, clipX1 - clipX0 + 1, clipY1 - clipY0 + 1, kBody);
    canvas.drawRect(xLeft, yTop, w, h, kOutline);
  }
}

}  // namespace

void updateCameraBasis() {
  s_camCy = cosf(s_yaw);
  s_camSy = sinf(s_yaw);
  s_camCp = cosf(s_pitch);
  s_camSp = sinf(s_pitch);
}

bool projectToScreen(const Vec3 &w, ProjVert &out) {
  const float dx = w.x - s_camX;
  const float dy = w.y - s_camY;
  const float dz = w.z - s_camZ;

  const float yawX = dx * s_camCy - dz * s_camSy;
  const float yawZ = dx * s_camSy + dz * s_camCy;
  const float camY = dy * s_camCp - yawZ * s_camSp;
  const float camZ = dy * s_camSp + yawZ * s_camCp;
  const float camX = yawX;

  if (camZ <= kNearPlane) {
    return false;
  }
  const float scale = kFocal / camZ;
  out.sx = static_cast<int16_t>(kScreenW * 0.5f + camX * scale);
  out.sy = static_cast<int16_t>(kScreenH * 0.5f - camY * scale);
  out.cz = camZ;
  return true;
}

void buildVisibleFaces() {
  s_faceCount = 0;
  const int cx = static_cast<int>(floorf(s_camX));
  const int cz = static_cast<int>(floorf(s_camZ));
  const int r = static_cast<int>(kRenderRadius);
  const int minX = std::max(0, cx - r);
  const int maxX = std::min(kWorldW - 1, cx + r);
  const int minZ = std::max(0, cz - r);
  const int maxZ = std::min(kWorldD - 1, cz + r);
  const float radiusSq = kRenderRadius * kRenderRadius;

  for (int x = minX; x <= maxX; ++x) {
    for (int z = minZ; z <= maxZ; ++z) {
      const float dcx = (static_cast<float>(x) + 0.5f) - s_camX;
      const float dcz = (static_cast<float>(z) + 0.5f) - s_camZ;
      if (dcx * dcx + dcz * dcz > radiusSq) {
        continue;
      }

      for (int y = 0; y < kWorldHMax; ++y) {
        const uint8_t blockId = s_voxel[x][y][z];
        if (blockId == BLOCK_AIR) {
          continue;
        }

        const float xf = static_cast<float>(x);
        const float yf = static_cast<float>(y);
        const float zf = static_cast<float>(z);

        // Culling optimization: skip blocks fully behind the camera.
        const float blockCamZ = cameraSpaceZ(xf + 0.5f, yf + 0.5f, zf + 0.5f);
        if (blockCamZ < -1.1f) {
          continue;
        }

        const uint16_t sideColor = blockSideColor(blockId);
        const uint16_t topColor = blockTopColor(blockId);

        // Per-face backface culling based on camera side of the face plane.
        if (!isSolidVoxel(x, y + 1, z) && s_camY > yf + 1.0f) {
          tryAddFace({xf, yf + 1.0f, zf}, {xf + 1.0f, yf + 1.0f, zf}, {xf + 1.0f, yf + 1.0f, zf + 1.0f},
                     {xf, yf + 1.0f, zf + 1.0f}, topColor);
        }
        if (!isSolidVoxel(x, y, z - 1) && s_camZ < zf) {
          tryAddFace({xf, yf, zf}, {xf + 1.0f, yf, zf}, {xf + 1.0f, yf + 1.0f, zf}, {xf, yf + 1.0f, zf},
                     sideColor);
        }
        if (!isSolidVoxel(x, y, z + 1) && s_camZ > zf + 1.0f) {
          tryAddFace({xf + 1.0f, yf, zf + 1.0f}, {xf, yf, zf + 1.0f}, {xf, yf + 1.0f, zf + 1.0f},
                     {xf + 1.0f, yf + 1.0f, zf + 1.0f}, sideColor);
        }
        if (!isSolidVoxel(x - 1, y, z) && s_camX < xf) {
          tryAddFace({xf, yf, zf + 1.0f}, {xf, yf, zf}, {xf, yf + 1.0f, zf}, {xf, yf + 1.0f, zf + 1.0f},
                     sideColor);
        }
        if (!isSolidVoxel(x + 1, y, z) && s_camX > xf + 1.0f) {
          tryAddFace({xf + 1.0f, yf, zf}, {xf + 1.0f, yf, zf + 1.0f}, {xf + 1.0f, yf + 1.0f, zf + 1.0f},
                     {xf + 1.0f, yf + 1.0f, zf}, sideColor);
        }
      }
    }
  }

  if (s_faceCount > 1) {
    std::sort(s_faces, s_faces + s_faceCount,
              [](const FaceQuad &l, const FaceQuad &r) { return l.depth > r.depth; });
  }
}

void drawWorld() {
  canvas.fillScreen(kSky);
  canvas.fillRect(0, kScreenH / 2, kScreenW, kScreenH / 2, kGroundFog);

  buildVisibleFaces();
  for (int i = 0; i < s_faceCount; ++i) {
    const FaceQuad &f = s_faces[i];
    canvas.fillTriangle(f.p[0].sx, f.p[0].sy, f.p[1].sx, f.p[1].sy, f.p[2].sx, f.p[2].sy, f.color);
    canvas.fillTriangle(f.p[0].sx, f.p[0].sy, f.p[2].sx, f.p[2].sy, f.p[3].sx, f.p[3].sy, f.color);
    if (kDrawEdges) {
      canvas.drawLine(f.p[0].sx, f.p[0].sy, f.p[1].sx, f.p[1].sy, kEdge);
      canvas.drawLine(f.p[1].sx, f.p[1].sy, f.p[2].sx, f.p[2].sy, kEdge);
      canvas.drawLine(f.p[2].sx, f.p[2].sy, f.p[3].sx, f.p[3].sy, kEdge);
      canvas.drawLine(f.p[3].sx, f.p[3].sy, f.p[0].sx, f.p[0].sy, kEdge);
    }
  }

  drawRemotePlayers();
}

void drawHud() {
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  canvas.setCursor(2, 2);
  canvas.setTextColor(ST77XX_GREEN);
  canvas.print("FPS ");
  canvas.print(s_fps);

  canvas.setCursor(2, 12);
  canvas.setTextColor(ST77XX_CYAN);
  canvas.print("X");
  canvas.print(s_camX, 1);
  canvas.print(" Y");
  canvas.print(s_camY, 1);
  canvas.print(" Z");
  canvas.print(s_camZ, 1);

  canvas.setCursor(2, 22);
  canvas.setTextColor(ST77XX_YELLOW);
  canvas.print("BAR ");
  canvas.print(s_selectedSlot + 1);
  canvas.print("/");
  canvas.print(kInvSlots);
  canvas.print(" ");
  canvas.setTextColor(ST77XX_WHITE);
  canvas.print(blockShortName(s_hotbar[s_selectedSlot].blockId));
  canvas.print("x");
  canvas.print(s_hotbar[s_selectedSlot].count);
  canvas.print(" T");
  canvas.print(inventoryTotal());

  const int y0 = kScreenH - 12;
  const int boxW = 30;
  const int gap = 2;
  const int totalW = kInvSlots * boxW + (kInvSlots - 1) * gap;
  int x0 = (kScreenW - totalW) / 2;
  for (int i = 0; i < kInvSlots; ++i) {
    const uint16_t frame = (i == s_selectedSlot) ? ST77XX_YELLOW : rgb565(170, 170, 170);
    canvas.fillRect(x0 + 1, y0 + 1, boxW - 2, 9, blockTopColor(s_hotbar[i].blockId));
    canvas.drawRect(x0, y0, boxW, 11, frame);
    canvas.setCursor(x0 + 2, y0 + 2);
    canvas.setTextColor(ST77XX_WHITE);
    canvas.print(blockShortName(s_hotbar[i].blockId));
    canvas.setCursor(x0 + 16, y0 + 2);
    canvas.print(s_hotbar[i].count);
    x0 += boxW + gap;
  }
}

void drawHomeScreen() {
  canvas.fillScreen(rgb565(10, 16, 24));
  canvas.fillRect(0, 0, kScreenW, 18, rgb565(22, 44, 68));

  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(4, 5);
  canvas.print("ESP32 MC Online Client");

  canvas.setTextColor(ST77XX_YELLOW);
  canvas.setCursor(6, 26);
  canvas.print("LOCAL WORLD DISABLED");

  canvas.setTextColor(rgb565(176, 218, 248));
  canvas.setCursor(6, 44);
  canvas.print("mc_state:");
  canvas.setTextColor(ST77XX_CYAN);
  canvas.print(s_mcState);

  String endpoint = s_mcHost + ":" + String(s_mcPort);
  if (endpoint.length() > 22) {
    endpoint = endpoint.substring(0, 22);
  }
  canvas.setTextColor(rgb565(176, 218, 248));
  canvas.setCursor(6, 56);
  canvas.print(endpoint);

  canvas.setCursor(6, 70);
  canvas.print("auto:");
  canvas.print(s_mcAutoConnect ? "on" : "off");

  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(6, 86);
  canvas.print("Waiting for server...");
  canvas.setCursor(6, 96);
  canvas.print("Need PLAY + chunk data");

  if ((millis() / 500) % 2 == 0) {
    canvas.setTextColor(ST77XX_GREEN);
    canvas.setCursor(6, 112);
    canvas.print("CONNECTING");
  } else {
    canvas.setTextColor(rgb565(90, 126, 154));
    canvas.setCursor(6, 112);
    canvas.print("OPEN WEB PANEL FOR CFG");
  }
}

void drawCrosshair() {
  const int cx = kScreenW / 2;
  const int cy = kScreenH / 2;
  const uint16_t col = rgb565(250, 250, 250);
  canvas.drawFastHLine(cx - 5, cy, 11, col);
  canvas.drawFastVLine(cx, cy - 5, 11, col);
  canvas.drawRect(cx - 1, cy - 1, 3, 3, ST77XX_BLACK);
}

void drawAimHighlight() {
  if (!s_hasAimHit) {
    return;
  }
  const float x = static_cast<float>(s_aimHit.x);
  const float y = static_cast<float>(s_aimHit.y);
  const float z = static_cast<float>(s_aimHit.z);

  Vec3 verts[8] = {
      {x, y, z},
      {x + 1.0f, y, z},
      {x + 1.0f, y + 1.0f, z},
      {x, y + 1.0f, z},
      {x, y, z + 1.0f},
      {x + 1.0f, y, z + 1},
      {x + 1.0f, y + 1.0f, z + 1},
      {x, y + 1.0f, z + 1},
  };
  ProjVert p[8];
  for (int i = 0; i < 8; ++i) {
    if (!projectToScreen(verts[i], p[i])) {
      return;
    }
  }
  constexpr uint16_t kAim = rgb565(250, 250, 95);
  const uint8_t edges[12][2] = {
      {0, 1},
      {1, 2},
      {2, 3},
      {3, 0},
      {4, 5},
      {5, 6},
      {6, 7},
      {7, 4},
      {0, 4},
      {1, 5},
      {2, 6},
      {3, 7},
  };
  for (int i = 0; i < 12; ++i) {
    const uint8_t a = edges[i][0];
    const uint8_t b = edges[i][1];
    canvas.drawLine(p[a].sx, p[a].sy, p[b].sx, p[b].sy, kAim);
  }
}

}  // namespace game
