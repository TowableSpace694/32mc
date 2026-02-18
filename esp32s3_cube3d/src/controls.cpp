#include "controls.h"

#include "mc_client.h"
#include "rendering.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {

namespace {
constexpr bool kEnableLocalBlockEdit = false;
}

bool actionDown(const char *action) {
  for (size_t i = 0; i < kBindingCount; ++i) {
    if (strcmp(s_bindings[i].action, action) == 0) {
      return s_bindings[i].active;
    }
  }
  return false;
}

void clearAllActions() {
  for (size_t i = 0; i < kBindingCount; ++i) {
    s_bindings[i].active = false;
  }
}

void setActionByKey(const String &keyCode, bool down) {
  for (size_t i = 0; i < kBindingCount; ++i) {
    if (s_bindings[i].keyCode == keyCode) {
      s_bindings[i].active = down;
    }
  }
}

bool anyActionActive() {
  for (size_t i = 0; i < kBindingCount; ++i) {
    if (s_bindings[i].active) {
      return true;
    }
  }
  return false;
}

int inventoryTotal() {
  int total = 0;
  for (int i = 0; i < kInvSlots; ++i) {
    total += s_hotbar[i].count;
  }
  return total;
}

void initHotbarDefaults() {
  for (int i = 0; i < kInvSlots; ++i) {
    s_hotbar[i].blockId = BLOCK_AIR;
    s_hotbar[i].count = 0;
  }
  s_hotbar[0].blockId = BLOCK_GRASS;
  s_hotbar[0].count = kInvStartBlocks;
  s_hotbar[1].blockId = BLOCK_DIRT;
  s_hotbar[1].count = kInvStartBlocks;
  s_hotbar[2].blockId = BLOCK_STONE;
  s_hotbar[2].count = kInvStartBlocks;
  s_hotbar[3].blockId = BLOCK_WOOD;
  s_hotbar[3].count = kInvStartBlocks;
  s_hotbar[4].blockId = BLOCK_SAND;
  s_hotbar[4].count = kInvStartBlocks;
}

bool inventoryTakeFromSelected(uint8_t *outBlockId) {
  if (s_hotbar[s_selectedSlot].count == 0 || s_hotbar[s_selectedSlot].blockId == BLOCK_AIR) {
    return false;
  }
  if (outBlockId != nullptr) {
    *outBlockId = s_hotbar[s_selectedSlot].blockId;
  }
  s_hotbar[s_selectedSlot].count--;
  if (s_hotbar[s_selectedSlot].count == 0) {
    s_hotbar[s_selectedSlot].blockId = BLOCK_AIR;
  }
  return true;
}

bool inventoryAddBlock(uint8_t blockId) {
  if (blockId == BLOCK_AIR) {
    return false;
  }
  if (s_hotbar[s_selectedSlot].blockId == blockId && s_hotbar[s_selectedSlot].count < kInvStackMax) {
    s_hotbar[s_selectedSlot].count++;
    return true;
  }
  for (int i = 0; i < kInvSlots; ++i) {
    if (s_hotbar[i].blockId == blockId && s_hotbar[i].count < kInvStackMax) {
      s_hotbar[i].count++;
      return true;
    }
  }
  for (int i = 0; i < kInvSlots; ++i) {
    if (s_hotbar[i].count == 0 || s_hotbar[i].blockId == BLOCK_AIR) {
      s_hotbar[i].blockId = blockId;
      s_hotbar[i].count = 1;
      return true;
    }
  }
  return false;
}

unsigned long breakCooldownMsFor(uint8_t blockId) {
  switch (blockId) {
    case BLOCK_GRASS:
    case BLOCK_DIRT:
    case BLOCK_SAND:
      return 95;
    case BLOCK_WOOD:
      return 140;
    case BLOCK_STONE:
      return 185;
    case BLOCK_ORE:
      return 220;
    case BLOCK_BEDROCK:
      return 9999999;
    default:
      return 120;
  }
}

void inventorySelectPrev() {
  s_selectedSlot--;
  if (s_selectedSlot < 0) {
    s_selectedSlot = kInvSlots - 1;
  }
}

void inventorySelectNext() {
  s_selectedSlot++;
  if (s_selectedSlot >= kInvSlots) {
    s_selectedSlot = 0;
  }
}

void updateCamera(float dtSec) {
  const bool left = (digitalRead(kBtnLeft) == LOW) || actionDown("turn_left");
  const bool right = (digitalRead(kBtnRight) == LOW) || actionDown("turn_right");
  const bool lookUp = actionDown("look_up");
  const bool lookDown = actionDown("look_down");
  if (left && !right) {
    s_yaw -= kTurnSpeedRad * dtSec;
  } else if (right && !left) {
    s_yaw += kTurnSpeedRad * dtSec;
  }

  if (lookUp && !lookDown) {
    s_pitch += kPitchSpeedRad * dtSec;
  } else if (lookDown && !lookUp) {
    s_pitch -= kPitchSpeedRad * dtSec;
  }
  s_pitch = std::max(-kMaxPitch, std::min(kMaxPitch, s_pitch));

  if (s_yaw > static_cast<float>(M_PI)) {
    s_yaw -= static_cast<float>(M_PI) * 2.0f;
  } else if (s_yaw < -static_cast<float>(M_PI)) {
    s_yaw += static_cast<float>(M_PI) * 2.0f;
  }
  updateCameraBasis();

  float forward = 0.0f;
  if (actionDown("move_fwd")) {
    forward += 1.0f;
  }
  if (actionDown("move_back")) {
    forward -= 1.0f;
  }

  float strafe = 0.0f;
  if (actionDown("strafe_right")) {
    strafe += 1.0f;
  }
  if (actionDown("strafe_left")) {
    strafe -= 1.0f;
  }

  const bool onlineGameplay = mcReadyForGameplay();
  bool onGround = isPlayerCollidingAt(s_camX, s_camY - 0.04f, s_camZ);

  if (forward != 0.0f || strafe != 0.0f) {
    float len = sqrtf(forward * forward + strafe * strafe);
    if (len > 1.0f) {
      forward /= len;
      strafe /= len;
    }
    const float step = kMoveSpeed * dtSec;
    const float moveX = (s_camSy * forward + s_camCy * strafe) * step;
    const float moveZ = (s_camCy * forward - s_camSy * strafe) * step;

    const bool currentlyColliding = isPlayerCollidingAt(s_camX, s_camY, s_camZ);
    bool movedXZ = false;

    const float tryX = s_camX + moveX;
    if (!isPlayerCollidingAt(tryX, s_camY, s_camZ) || (onlineGameplay && currentlyColliding)) {
      s_camX = tryX;
      movedXZ = true;
    } else if (onGround) {
      constexpr float kStepHeights[] = {0.35f, 0.7f, 1.02f};
      for (float sh : kStepHeights) {
        if (!isPlayerCollidingAt(tryX, s_camY + sh, s_camZ)) {
          s_camY += sh;
          s_camX = tryX;
          movedXZ = true;
          break;
        }
      }
    }

    const float tryZ = s_camZ + moveZ;
    if (!isPlayerCollidingAt(s_camX, s_camY, tryZ) || (onlineGameplay && currentlyColliding)) {
      s_camZ = tryZ;
      movedXZ = true;
    } else if (onGround) {
      constexpr float kStepHeights[] = {0.35f, 0.7f, 1.02f};
      for (float sh : kStepHeights) {
        if (!isPlayerCollidingAt(s_camX, s_camY + sh, tryZ)) {
          s_camY += sh;
          s_camZ = tryZ;
          movedXZ = true;
          break;
        }
      }
    }

    if (onlineGameplay && !movedXZ) {
      // Last-resort anti-stuck path for streamed chunk mismatches.
      s_camX += moveX * 0.8f;
      s_camZ += moveZ * 0.8f;
    }
  }

  onGround = isPlayerCollidingAt(s_camX, s_camY - 0.04f, s_camZ);
  if (onlineGameplay) {
    // Keep feet glued to the streamed terrain in online mode.
    const int gx = static_cast<int>(floorf(s_camX));
    const int gz = static_cast<int>(floorf(s_camZ));
    const int supportY = supportYBelowPlayer(gx, gz, s_camY + 0.35f);
    if (supportY >= 0) {
      const float targetCamY = static_cast<float>(supportY) + 1.0f + kEyeHeight;
      const float dy = targetCamY - s_camY;
      if (fabsf(dy) <= 1.35f) {
        s_camY = targetCamY;
      }
    }
    onGround = isPlayerCollidingAt(s_camX, s_camY - 0.04f, s_camZ);
  }

  const bool jumpDown = actionDown("jump");
  const bool jumpPressed = jumpDown && !s_prevJumpDown;
  s_prevJumpDown = jumpDown;

  if (!mcReadyForGameplay()) {
    if (jumpPressed && onGround) {
      s_velY = kJumpVelocity;
    }

    s_velY -= kGravity * dtSec;
    const float targetY = s_camY + s_velY * dtSec;
    if (!isPlayerCollidingAt(s_camX, targetY, s_camZ)) {
      s_camY = targetY;
    } else {
      float probe = s_camY;
      const float stepY = 0.03f * ((s_velY >= 0.0f) ? 1.0f : -1.0f);
      const int maxSteps = 80;
      for (int i = 0; i < maxSteps; ++i) {
        const float nextY = probe + stepY;
        if ((stepY > 0.0f && nextY > targetY) || (stepY < 0.0f && nextY < targetY)) {
          break;
        }
        if (isPlayerCollidingAt(s_camX, nextY, s_camZ)) {
          break;
        }
        probe = nextY;
      }
      s_camY = probe;
      s_velY = 0.0f;
    }
  } else {
    // Keep server Y stable in streamed online mode to avoid desync drift.
    s_velY = 0.0f;
  }

  const bool breakDown = actionDown("break_block");
  const bool placeDown = actionDown("place_block");
  const bool invPrevDown = actionDown("inv_prev");
  const bool invNextDown = actionDown("inv_next");
  const bool breakPressed = breakDown && !s_prevBreakDown;
  const bool placePressed = placeDown && !s_prevPlaceDown;
  const bool invPrevPressed = invPrevDown && !s_prevInvPrevDown;
  const bool invNextPressed = invNextDown && !s_prevInvNextDown;
  s_prevBreakDown = breakDown;
  s_prevPlaceDown = placeDown;
  s_prevInvPrevDown = invPrevDown;
  s_prevInvNextDown = invNextDown;

  if (invPrevPressed) {
    inventorySelectPrev();
    mcSetHeldSlot(static_cast<uint8_t>(s_selectedSlot));
  }
  if (invNextPressed) {
    inventorySelectNext();
    mcSetHeldSlot(static_cast<uint8_t>(s_selectedSlot));
  }
  for (int i = 0; i < kInvSlots; ++i) {
    const bool slotDown = actionDown(kSlotActions[i]);
    const bool slotPressed = slotDown && !s_prevSlotDown[i];
    s_prevSlotDown[i] = slotDown;
    if (slotPressed) {
      s_selectedSlot = i;
      mcSetHeldSlot(static_cast<uint8_t>(s_selectedSlot));
    }
  }

  RayHit aim = raycastCenter();
  s_hasAimHit = aim.hit;
  if (aim.hit) {
    s_aimHit = aim;
  }

  const unsigned long now = millis();
  if (aim.hit && breakPressed) {
    const RayHit &hit = aim;
    if (kEnableLocalBlockEdit) {
      const uint8_t oldId = s_voxel[hit.x][hit.y][hit.z];
      if (oldId != BLOCK_AIR && oldId != BLOCK_BEDROCK) {
        const unsigned long breakCd = breakCooldownMsFor(oldId);
        if (now - s_lastEditMs >= breakCd) {
          setVoxel(hit.x, hit.y, hit.z, BLOCK_AIR);
          inventoryAddBlock(oldId);
          s_lastEditMs = now;
        }
      }
    } else if (now - s_lastEditMs >= 90) {
      const uint8_t oldId = inWorldXYZ(hit.x, hit.y, hit.z) ? s_voxel[hit.x][hit.y][hit.z] : BLOCK_AIR;
      if (mcTryBreakBlockServer(hit)) {
        if (oldId != BLOCK_AIR && oldId != BLOCK_BEDROCK) {
          setVoxel(hit.x, hit.y, hit.z, BLOCK_AIR);
          inventoryAddBlock(oldId);
        }
        s_lastEditMs = now;
      }
    }
  }

  if (aim.hit && placePressed && now - s_lastEditMs >= kPlaceCooldownMs) {
    const RayHit &hit = aim;
    const int tx2 = hit.x + hit.nx;
    const int ty2 = hit.y + hit.ny;
    const int tz2 = hit.z + hit.nz;
    uint8_t placeId = BLOCK_AIR;
    if (inventoryTakeFromSelected(&placeId)) {
      if (!mcTryPlaceBlockServer(hit, placeId)) {
        inventoryAddBlock(placeId);
      } else {
        // Local prediction only when target is within the current streamed chunk window.
        if (inWorldXYZ(tx2, ty2, tz2) && !isSolidVoxel(tx2, ty2, tz2)) {
          setVoxel(tx2, ty2, tz2, placeId);
        }
        s_lastEditMs = now;
      }
    }
  }
}

void checkInputTimeout() {
  if (s_lastInputMs == 0) {
    return;
  }
  if (millis() - s_lastInputMs > kControlTimeoutMs) {
    clearAllActions();
  }
}

}  // namespace game
