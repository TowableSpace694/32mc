#include "mc_client.h"

#include "world.h"

#include <ESP.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {

namespace {

enum McStage : uint8_t {
  MC_IDLE = 0,
  MC_WAIT_LOGIN_SUCCESS = 1,
  MC_WAIT_CONFIG_FINISH = 2,
  MC_WAIT_PLAY_LOGIN = 3,
  MC_PLAY = 4,
};

WiFiClient s_mcSocket;
McStage s_mcStage = MC_IDLE;

uint32_t s_pktLenAccum = 0;
uint8_t s_pktLenShift = 0;
uint8_t s_pktLenBytes = 0;
uint32_t s_pktRemaining = 0;
uint32_t s_pktTotalLen = 0;

// Keep enough head bytes to parse the near-player sections from bareiron's
// large Chunk Data packet while staying within ESP32 SRAM limits.
constexpr size_t kPacketHeadCap = 98304;
uint8_t s_pktHead[kPacketHeadCap];
size_t s_pktHeadLen = 0;

bool s_haveServerAnchor = false;
double s_serverBaseX = 0.0;
double s_serverBaseY = 80.0;
double s_serverBaseZ = 0.0;
float s_localAnchorX = 0.0f;
float s_localAnchorFeetY = 0.0f;
float s_localAnchorZ = 0.0f;
double s_serverFeetY = 80.0;
int32_t s_centerChunkX = 0;
int32_t s_centerChunkZ = 0;
bool s_haveCenterChunk = false;
bool s_haveRemoteWorld = false;
bool s_sentKnownPacksAck = false;
bool s_sentConfigAck = false;
uint32_t s_chunkRxCount = 0;
uint32_t s_chunkApplyCount = 0;
uint32_t s_chunkDropCount = 0;

unsigned long s_lastMoveSendMs = 0;
unsigned long s_lastWifiOkMs = 0;
unsigned long s_stageSinceMs = 0;
unsigned long s_lastRxMs = 0;
bool s_sentPlayerLoaded = false;
int32_t s_actionSequence = 1;

constexpr size_t kPacketBufCap = 320;
constexpr unsigned long kMcHandshakeIdleTimeoutMs = 12000;
constexpr unsigned long kMcConfigIdleTimeoutMs = 45000;

void clearRemotePlayers() {
  for (int i = 0; i < kRemotePlayerMax; ++i) {
    s_remotePlayers[i].active = false;
    s_remotePlayers[i].entityId = 0;
    s_remotePlayers[i].x = 0.0f;
    s_remotePlayers[i].feetY = 0.0f;
    s_remotePlayers[i].z = 0.0f;
  }
}

RemotePlayerView *findRemotePlayerSlot(int32_t entityId, bool allowAlloc) {
  RemotePlayerView *freeSlot = nullptr;
  for (int i = 0; i < kRemotePlayerMax; ++i) {
    RemotePlayerView &p = s_remotePlayers[i];
    if (p.active && p.entityId == entityId) {
      return &p;
    }
    if (!p.active && freeSlot == nullptr) {
      freeSlot = &p;
    }
  }
  if (!allowAlloc || freeSlot == nullptr) {
    return nullptr;
  }
  freeSlot->active = true;
  freeSlot->entityId = entityId;
  freeSlot->x = 0.0f;
  freeSlot->feetY = 0.0f;
  freeSlot->z = 0.0f;
  return freeSlot;
}

void updateRemotePlayerFromServer(int32_t entityId, double sx, double sy, double sz) {
  RemotePlayerView *slot = findRemotePlayerSlot(entityId, true);
  if (slot == nullptr) {
    return;
  }
  if (s_haveServerAnchor) {
    slot->x = s_localAnchorX + static_cast<float>(sx - s_serverBaseX);
    slot->feetY = s_localAnchorFeetY + static_cast<float>(sy - s_serverBaseY);
    slot->z = s_localAnchorZ + static_cast<float>(sz - s_serverBaseZ);
    return;
  }
  slot->x = static_cast<float>(sx);
  slot->feetY = static_cast<float>(sy);
  slot->z = static_cast<float>(sz);
}

void removeRemotePlayer(int32_t entityId) {
  RemotePlayerView *slot = findRemotePlayerSlot(entityId, false);
  if (slot == nullptr) {
    return;
  }
  slot->active = false;
}

class PacketWriter {
 public:
  PacketWriter() : len_(0), ok_(true) {}

  bool ok() const { return ok_; }
  size_t len() const { return len_; }
  const uint8_t *data() const { return data_; }

  void writeByte(uint8_t v) { writeBytes(&v, 1); }

  void writeBytes(const void *src, size_t n) {
    if (!ok_ || len_ + n > kPacketBufCap) {
      ok_ = false;
      return;
    }
    memcpy(data_ + len_, src, n);
    len_ += n;
  }

  void writeVarInt(int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    while (true) {
      uint8_t out = static_cast<uint8_t>(u & 0x7F);
      u >>= 7;
      if (u != 0) {
        out |= 0x80;
      }
      writeByte(out);
      if (u == 0) {
        break;
      }
    }
  }

  void writeU16(uint16_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    writeBytes(b, sizeof(b));
  }

  void writeU32(uint32_t v) {
    uint8_t b[4] = {
        static_cast<uint8_t>(v >> 24),
        static_cast<uint8_t>(v >> 16),
        static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v),
    };
    writeBytes(b, sizeof(b));
  }

  void writeU64(uint64_t v) {
    uint8_t b[8] = {
        static_cast<uint8_t>(v >> 56),
        static_cast<uint8_t>(v >> 48),
        static_cast<uint8_t>(v >> 40),
        static_cast<uint8_t>(v >> 32),
        static_cast<uint8_t>(v >> 24),
        static_cast<uint8_t>(v >> 16),
        static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v),
    };
    writeBytes(b, sizeof(b));
  }

  void writeF32(float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    writeU32(bits);
  }

  void writeF64(double v) {
    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    writeU64(bits);
  }

  void writeString(const String &s) {
    writeVarInt(static_cast<int32_t>(s.length()));
    if (s.length() > 0) {
      writeBytes(s.c_str(), s.length());
    }
  }

 private:
  uint8_t data_[kPacketBufCap];
  size_t len_;
  bool ok_;
};

void setMcState(const char *stateText) {
  if (s_mcState == stateText) {
    return;
  }
  s_mcState = stateText;
  Serial.printf("[mc] state=%s\n", stateText);
}

void setMcStage(McStage stage) {
  s_mcStage = stage;
  s_stageSinceMs = millis();
}

void resetPacketParsing() {
  s_pktLenAccum = 0;
  s_pktLenShift = 0;
  s_pktLenBytes = 0;
  s_pktRemaining = 0;
  s_pktTotalLen = 0;
  s_pktHeadLen = 0;
}

void closeSocketToState(const char *stateText) {
  if (s_mcSocket.connected()) {
    s_mcSocket.stop();
  }
  setMcStage(MC_IDLE);
  s_sentPlayerLoaded = false;
  s_sentKnownPacksAck = false;
  s_sentConfigAck = false;
  s_haveServerAnchor = false;
  s_haveCenterChunk = false;
  s_haveRemoteWorld = false;
  clearRemotePlayers();
  clearWorld();
  resetPacketParsing();
  setMcState(stateText);
}

size_t encodeVarInt(uint8_t *out, size_t cap, int32_t v) {
  uint32_t u = static_cast<uint32_t>(v);
  size_t n = 0;
  while (true) {
    if (n >= cap) {
      return 0;
    }
    uint8_t b = static_cast<uint8_t>(u & 0x7F);
    u >>= 7;
    if (u != 0) {
      b |= 0x80;
    }
    out[n++] = b;
    if (u == 0) {
      return n;
    }
  }
}

bool sendPacket(const PacketWriter &packet) {
  if (!packet.ok() || !s_mcSocket.connected()) {
    return false;
  }

  auto writeAll = [](const uint8_t *data, size_t len) -> bool {
    size_t sent = 0;
    unsigned long lastProgressMs = millis();
    while (sent < len) {
      if (!s_mcSocket.connected()) {
        return false;
      }
      const size_t n = s_mcSocket.write(data + sent, len - sent);
      if (n > 0) {
        sent += n;
        lastProgressMs = millis();
        continue;
      }
      if (millis() - lastProgressMs > 12000) {
        return false;
      }
      delay(1);
    }
    return true;
  };

  uint8_t lenPrefix[5];
  const size_t lenBytes = encodeVarInt(lenPrefix, sizeof(lenPrefix), static_cast<int32_t>(packet.len()));
  if (lenBytes == 0) {
    return false;
  }
  if (!writeAll(lenPrefix, lenBytes)) {
    return false;
  }
  if (packet.len() == 0) {
    return true;
  }
  return writeAll(packet.data(), packet.len());
}

void buildLoginUuid(uint8_t out[16]) {
  const uint64_t mac = ESP.getEfuseMac();
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<uint8_t>(mac >> (56 - i * 8));
  }
  for (int i = 8; i < 16; ++i) {
    out[i] = static_cast<uint8_t>(out[i - 8] ^ (0x5A + i * 11));
  }
}

bool sendHandshake() {
  PacketWriter p;
  p.writeVarInt(0x00);   // Handshake
  p.writeVarInt(772);    // bareiron protocol version
  p.writeString(s_mcHost);
  p.writeU16(s_mcPort);
  p.writeVarInt(2);      // Next state: login
  return sendPacket(p);
}

bool sendLoginStart() {
  PacketWriter p;
  p.writeVarInt(0x00);  // Login Start
  String playerName = s_mcPlayerName;
  playerName.trim();
  if (playerName.length() == 0) {
    playerName = kMcDefaultPlayer;
  }
  if (playerName.length() > 16) {
    playerName = playerName.substring(0, 16);
  }
  p.writeString(playerName);
  uint8_t uuid[16];
  buildLoginUuid(uuid);
  p.writeBytes(uuid, sizeof(uuid));
  return sendPacket(p);
}

bool sendLoginAck() {
  PacketWriter p;
  p.writeVarInt(0x03);  // Login acknowledged
  return sendPacket(p);
}

bool sendClientInformation() {
  PacketWriter p;
  p.writeVarInt(0x00);
  p.writeString(String("zh_cn"));
  p.writeByte(2);       // view distance: keep low to avoid overwhelming ESP32 RX
  p.writeVarInt(0);     // chat mode
  p.writeByte(1);       // chat colors
  p.writeByte(0x7F);    // skin parts
  p.writeVarInt(1);     // main hand: right
  p.writeByte(0);       // text filtering
  p.writeByte(1);       // allow listing
  p.writeVarInt(0);     // particles
  return sendPacket(p);
}

bool sendBrandPluginMessage() {
  PacketWriter p;
  p.writeVarInt(0x02);  // Plugin message
  p.writeString(String("minecraft:brand"));
  p.writeString(String("esp32"));
  return sendPacket(p);
}

bool sendKnownPacksAck() {
  PacketWriter p;
  p.writeVarInt(0x07);  // Known packs
  return sendPacket(p);
}

bool sendConfigAck() {
  PacketWriter p;
  p.writeVarInt(0x03);  // Configuration acknowledged
  return sendPacket(p);
}

bool sendPlayerLoaded() {
  PacketWriter p;
  p.writeVarInt(0x2B);  // Player loaded
  return sendPacket(p);
}

bool sendKeepAlive(uint64_t token) {
  PacketWriter p;
  p.writeVarInt(0x1B);  // Serverbound keep alive
  p.writeU64(token);
  return sendPacket(p);
}

bool sendMovementPacket() {
  PacketWriter p;
  p.writeVarInt(0x1E);  // Set player position and rotation

  const float localFeetY = s_camY - kEyeHeight;
  double sx = s_camX;
  double sy = 80.0 + localFeetY;
  double sz = s_camZ;
  if (s_haveServerAnchor) {
    sx = s_serverBaseX + static_cast<double>(s_camX - s_localAnchorX);
    sy = s_serverBaseY + static_cast<double>(localFeetY - s_localAnchorFeetY);
    sz = s_serverBaseZ + static_cast<double>(s_camZ - s_localAnchorZ);
  }
  s_serverFeetY = sy;

  p.writeF64(sx);
  p.writeF64(sy);
  p.writeF64(sz);

  const float kRadToDeg = 57.295779513f;
  p.writeF32(s_yaw * kRadToDeg);
  p.writeF32(-s_pitch * kRadToDeg);

  const uint8_t onGround = isPlayerCollidingAt(s_camX, s_camY - 0.04f, s_camZ) ? 1 : 0;
  p.writeByte(onGround);

  return sendPacket(p);
}

bool sendHeldItemSlot(uint8_t slot) {
  PacketWriter p;
  p.writeVarInt(0x34);  // Set held item
  p.writeU16(slot);
  return sendPacket(p);
}

uint8_t faceFromNormal(const RayHit &hit) {
  if (hit.ny < 0) return 0;
  if (hit.ny > 0) return 1;
  if (hit.nz < 0) return 2;
  if (hit.nz > 0) return 3;
  if (hit.nx < 0) return 4;
  if (hit.nx > 0) return 5;
  return 1;
}

uint64_t encodeBlockPos(int32_t x, int32_t y, int32_t z) {
  const uint64_t ux = static_cast<uint64_t>(x) & 0x3FFFFFFULL;
  const uint64_t uy = static_cast<uint64_t>(y) & 0xFFFULL;
  const uint64_t uz = static_cast<uint64_t>(z) & 0x3FFFFFFULL;
  return (ux << 38) | (uz << 12) | uy;
}

bool sendUseItemOn(const RayHit &hit) {
  if (!s_mcSocket.connected() || s_mcStage != MC_PLAY) {
    return false;
  }
  if (!s_haveServerAnchor) {
    return false;
  }

  // Convert the locally targeted block to the server anchor frame.
  const double sx = s_serverBaseX + static_cast<double>(hit.x - s_localAnchorX);
  const double sy = s_serverBaseY + static_cast<double>(hit.y - s_localAnchorFeetY);
  const double sz = s_serverBaseZ + static_cast<double>(hit.z - s_localAnchorZ);

  PacketWriter p;
  p.writeVarInt(0x3F);  // Use item on
  p.writeByte(0);       // main hand
  p.writeU64(encodeBlockPos(static_cast<int32_t>(floor(sx)), static_cast<int32_t>(floor(sy)),
                            static_cast<int32_t>(floor(sz))));
  p.writeByte(faceFromNormal(hit));
  // cursor position in block (center); server ignores these values
  p.writeF32(0.5f);
  p.writeF32(0.5f);
  p.writeF32(0.5f);
  p.writeByte(0);  // inside block
  p.writeByte(0);  // world border hit
  p.writeVarInt(s_actionSequence++);
  return sendPacket(p);
}

bool sendBreakBlock(const RayHit &hit) {
  if (!s_mcSocket.connected() || s_mcStage != MC_PLAY) {
    return false;
  }
  if (!s_haveServerAnchor) {
    return false;
  }

  // Convert targeted local voxel coordinates into server world coordinates.
  const double sx = s_serverBaseX + static_cast<double>(hit.x - s_localAnchorX);
  const double sy = s_serverBaseY + static_cast<double>(hit.y - s_localAnchorFeetY);
  const double sz = s_serverBaseZ + static_cast<double>(hit.z - s_localAnchorZ);

  PacketWriter p;
  p.writeVarInt(0x28);  // Player action
  p.writeByte(2);       // finish mining
  p.writeU64(encodeBlockPos(static_cast<int32_t>(floor(sx)), static_cast<int32_t>(floor(sy)),
                            static_cast<int32_t>(floor(sz))));
  p.writeByte(faceFromNormal(hit));
  p.writeVarInt(s_actionSequence++);
  return sendPacket(p);
}

bool isPlayerEntityType(int32_t entityType) {
  // bareiron currently uses 149 for players. Keep 157 for compatibility with
  // other 1.21.8 traces captured during integration.
  return entityType == 149 || entityType == 157;
}

bool readVarInt(const uint8_t *data, size_t len, size_t *offset, int32_t *out) {
  int32_t value = 0;
  int shift = 0;
  while (true) {
    if (*offset >= len || shift > 28) {
      return false;
    }
    const uint8_t b = data[(*offset)++];
    value |= static_cast<int32_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) {
      *out = value;
      return true;
    }
    shift += 7;
  }
}

bool readU64(const uint8_t *data, size_t len, size_t *offset, uint64_t *out) {
  if (*offset + 8 > len) {
    return false;
  }
  const uint8_t *p = data + *offset;
  *out = (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48) |
         (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32) |
         (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16) |
         (static_cast<uint64_t>(p[6]) << 8) | static_cast<uint64_t>(p[7]);
  *offset += 8;
  return true;
}

bool readU32(const uint8_t *data, size_t len, size_t *offset, uint32_t *out) {
  if (*offset + 4 > len) {
    return false;
  }
  const uint8_t *p = data + *offset;
  *out = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
  *offset += 4;
  return true;
}

bool readU16(const uint8_t *data, size_t len, size_t *offset, uint16_t *out) {
  if (*offset + 2 > len) {
    return false;
  }
  const uint8_t *p = data + *offset;
  *out = static_cast<uint16_t>((p[0] << 8) | p[1]);
  *offset += 2;
  return true;
}

bool readI32(const uint8_t *data, size_t len, size_t *offset, int32_t *out) {
  uint32_t v = 0;
  if (!readU32(data, len, offset, &v)) {
    return false;
  }
  *out = static_cast<int32_t>(v);
  return true;
}

bool readF32(const uint8_t *data, size_t len, size_t *offset, float *out) {
  uint32_t bits = 0;
  if (!readU32(data, len, offset, &bits)) {
    return false;
  }
  memcpy(out, &bits, sizeof(bits));
  return true;
}

bool readF64(const uint8_t *data, size_t len, size_t *offset, double *out) {
  uint64_t bits = 0;
  if (!readU64(data, len, offset, &bits)) {
    return false;
  }
  memcpy(out, &bits, sizeof(bits));
  return true;
}

uint8_t mapBareironBlockToLocal(uint8_t b) {
  // Bareiron passable blocks and fluids.
  if (b == 0 || (b >= 1 && b <= 12) || b == 15 || b == 84 || b == 86 || b == 132 || b == 156) {
    return BLOCK_AIR;
  }
  if (b == 37) {
    return BLOCK_BEDROCK;
  }
  if (b == 13 || b == 30 || b == 33 || b == 198) {
    return BLOCK_GRASS;
  }
  if (b == 14 || b == 31 || b == 32 || b == 141) {
    return BLOCK_DIRT;
  }
  if (b == 21 || b == 43 || b == 44 || b == 45 || b == 46 || b == 57 || b == 137 || b == 153 || b == 218 ||
      b == 239) {
    return BLOCK_ORE;
  }
  if (b == 35 || b == 47 || b == 48 || b == 49 || b == 50 || b == 51 || b == 52 || b == 53 || b == 128 ||
      b == 139 || b == 143 || b == 148 || b == 162 || b == 164 || b == 176 || b == 194) {
    return BLOCK_WOOD;
  }
  if (b == 38 || b == 39 || b == 40 || b == 41 || b == 42 || b == 60 || b == 61 || b == 62 || b == 217) {
    return BLOCK_SAND;
  }
  return BLOCK_STONE;
}

uint8_t mapPaletteSingletonToLocal(int32_t stateId) {
  // In bareiron's generated chunk payload, state 0 is air. Non-zero singleton
  // sections are treated as solid fallback in this client.
  return stateId == 0 ? BLOCK_AIR : BLOCK_STONE;
}

bool decodeServerChunkIntoLocal(const uint8_t *packet, size_t len, size_t off) {
  int32_t chunkX = 0;
  int32_t chunkZ = 0;
  if (!readI32(packet, len, &off, &chunkX) || !readI32(packet, len, &off, &chunkZ)) {
    return false;
  }
  if (!s_haveCenterChunk || chunkX != s_centerChunkX || chunkZ != s_centerChunkZ) {
    return false;
  }

  int32_t heightmapNbtLen = 0;
  if (!readVarInt(packet, len, &off, &heightmapNbtLen)) {
    return false;
  }
  if (heightmapNbtLen < 0 || off + static_cast<size_t>(heightmapNbtLen) > len) {
    return false;
  }
  off += static_cast<size_t>(heightmapNbtLen);

  int32_t chunkDataSize = 0;
  if (!readVarInt(packet, len, &off, &chunkDataSize)) {
    return false;
  }
  if (chunkDataSize <= 0) {
    return false;
  }

  const int baseServerY = static_cast<int>(floor(s_serverFeetY)) - 3;
  bool worldCleared = false;
  bool wroteAny = false;

  auto clearLocalChunkWindow = []() {
    for (int x = 0; x < kWorldW; ++x) {
      for (int y = 0; y < kWorldHMax; ++y) {
        for (int z = 0; z < kWorldD; ++z) {
          s_voxel[x][y][z] = BLOCK_AIR;
        }
      }
    }
  };

  // bareiron uses the vanilla 1.21 section stack with min Y = -64.
  constexpr int kSectionBaseY = -64;

  for (int section = 0; section < 32; ++section) {
    uint16_t nonAirCount = 0;
    if (!readU16(packet, len, &off, &nonAirCount)) {
      break;
    }

    if (off >= len) {
      break;
    }
    const uint8_t bitsPerEntry = packet[off++];

    const int sectionY0 = kSectionBaseY + section * 16;
    const int windowMinY = baseServerY;
    const int windowMaxY = baseServerY + (kWorldHMax - 1);
    const bool intersectsWindow = !(sectionY0 + 15 < windowMinY || sectionY0 > windowMaxY);

    if (bitsPerEntry == 0) {
      int32_t singleState = 0;
      if (!readVarInt(packet, len, &off, &singleState)) {
        break;
      }
      if (off + 2 > len) {
        break;
      }
      off += 2;  // biome container bytes

      if (!intersectsWindow) {
        continue;
      }
      if (!worldCleared) {
        clearLocalChunkWindow();
        worldCleared = true;
      }
      const uint8_t mapped = mapPaletteSingletonToLocal(singleState);
      for (int ly = 0; ly < kWorldHMax; ++ly) {
        const int serverY = baseServerY + ly;
        if (serverY < sectionY0 || serverY > sectionY0 + 15) {
          continue;
        }
        for (int x = 0; x < 16; ++x) {
          for (int z = 0; z < 16; ++z) {
            s_voxel[x][ly][z] = mapped;
          }
        }
      }
      wroteAny = true;
      continue;
    }

    int32_t paletteLen = 0;
    if (!readVarInt(packet, len, &off, &paletteLen) || paletteLen <= 0) {
      break;
    }
    for (int32_t i = 0; i < paletteLen; ++i) {
      int32_t ignored = 0;
      if (!readVarInt(packet, len, &off, &ignored)) {
        paletteLen = -1;
        break;
      }
    }
    if (paletteLen < 0) {
      break;
    }

    if (off + 4096 > len) {
      break;
    }
    const uint8_t *sectionData = packet + off;
    off += 4096;

    if (off + 2 > len) {
      break;
    }
    off += 2;  // biome container bytes

    if (!intersectsWindow) {
      continue;
    }
    if (!worldCleared) {
      clearLocalChunkWindow();
      worldCleared = true;
    }

    for (int ly = 0; ly < kWorldHMax; ++ly) {
      const int serverY = baseServerY + ly;
      if (serverY < sectionY0 || serverY > sectionY0 + 15) {
        continue;
      }
      const int dy = serverY - sectionY0;
      for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
          const int addr = x + (z << 4) + (dy << 8);
          const int idx = (addr & ~7) | (7 - (addr & 7));
          const uint8_t bareironBlock = sectionData[idx];
          s_voxel[x][ly][z] = mapBareironBlockToLocal(bareironBlock);
        }
      }
    }
    wroteAny = true;
  }

  return wroteAny;
}

void handlePacket(const uint8_t *packet, size_t len, size_t totalLen, bool truncated) {
  size_t off = 0;
  int32_t packetId = -1;
  if (!readVarInt(packet, len, &off, &packetId)) {
    return;
  }
  if (s_mcStage != MC_PLAY) {
    Serial.printf("[mc] rx id=0x%02X len=%u\n", static_cast<unsigned int>(packetId & 0xFF),
                  static_cast<unsigned int>(len));
  }

  if (packetId == 0x02 && s_mcStage == MC_WAIT_LOGIN_SUCCESS) {
    if (!sendLoginAck() || !sendBrandPluginMessage() || !sendClientInformation()) {
      closeSocketToState("TX_FAIL");
      return;
    }
    setMcStage(MC_WAIT_CONFIG_FINISH);
    s_sentConfigAck = false;
    setMcState("WAIT_CONFIG");
    return;
  }

  if (packetId == 0x0E && s_mcStage == MC_WAIT_CONFIG_FINISH) {  // Clientbound known packs
    if (!s_sentKnownPacksAck) {
      if (!sendKnownPacksAck()) {
        closeSocketToState("TX_FAIL");
        return;
      }
      s_sentKnownPacksAck = true;
    }
    // Be tolerant: some stacks do not reliably expose finish-config packet timing.
    if (!s_sentConfigAck) {
      if (!sendConfigAck()) {
        closeSocketToState("TX_FAIL");
        return;
      }
      s_sentConfigAck = true;
      setMcStage(MC_WAIT_PLAY_LOGIN);
      setMcState("WAIT_PLAY");
    }
    return;
  }

  if (packetId == 0x03 && s_mcStage == MC_WAIT_CONFIG_FINISH) {
    if (!sendConfigAck()) {
      closeSocketToState("TX_FAIL");
      return;
    }
    s_sentConfigAck = true;
    setMcStage(MC_WAIT_PLAY_LOGIN);
    setMcState("WAIT_PLAY");
    return;
  }

  if (packetId == 0x2B && s_mcStage == MC_WAIT_PLAY_LOGIN) {
    if (!s_sentPlayerLoaded) {
      if (!sendPlayerLoaded()) {
        closeSocketToState("TX_FAIL");
        return;
      }
      s_sentPlayerLoaded = true;
    }
    setMcStage(MC_PLAY);
    setMcState("PLAY");
    sendHeldItemSlot(static_cast<uint8_t>(s_selectedSlot));
    return;
  }

  if (packetId == 0x26) {  // Keep alive
    if (s_mcStage == MC_WAIT_CONFIG_FINISH || s_mcStage == MC_WAIT_PLAY_LOGIN) {
      setMcStage(MC_PLAY);
      setMcState("PLAY");
    }
    uint64_t token = 0;
    if (readU64(packet, len, &off, &token)) {
      if (!sendKeepAlive(token)) {
        closeSocketToState("TX_FAIL");
      }
    }
    return;
  }

  if (packetId == 0x41) {  // Synchronize player position
    int32_t teleportId = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (!readVarInt(packet, len, &off, &teleportId) || !readF64(packet, len, &off, &x) ||
        !readF64(packet, len, &off, &y) || !readF64(packet, len, &off, &z) ||
        !readF64(packet, len, &off, &vx) || !readF64(packet, len, &off, &vy) ||
        !readF64(packet, len, &off, &vz) || !readF32(packet, len, &off, &yaw) ||
        !readF32(packet, len, &off, &pitch)) {
      return;
    }
    s_serverBaseX = x;
    s_serverBaseY = y;
    s_serverBaseZ = z;
    s_serverFeetY = y;
    // Re-anchor local coordinates to keep collision and movement aligned
    // with the streamed chunk window around the server's current position.
    const float localFeetY = 3.0f;
    s_camX = 7.5f;
    s_camY = localFeetY + kEyeHeight;
    s_camZ = 7.5f;
    s_localAnchorX = s_camX;
    s_localAnchorFeetY = localFeetY;
    s_localAnchorZ = s_camZ;
    s_haveServerAnchor = true;
    Serial.printf("[mc] sync server=(%.2f,%.2f,%.2f) local_anchor=(%.2f,%.2f,%.2f)\n", s_serverBaseX,
                  s_serverBaseY, s_serverBaseZ, s_localAnchorX, s_localAnchorFeetY, s_localAnchorZ);
    return;
  }

  if (packetId == 0x01) {  // Add entity
    int32_t entityId = 0;
    int32_t entityType = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int32_t data = 0;
    if (!readVarInt(packet, len, &off, &entityId)) {
      return;
    }
    if (off + 16 > len) {
      return;
    }
    off += 16;  // UUID
    if (!readVarInt(packet, len, &off, &entityType) || !readF64(packet, len, &off, &x) ||
        !readF64(packet, len, &off, &y) || !readF64(packet, len, &off, &z)) {
      return;
    }
    if (off + 3 > len) {
      return;
    }
    off += 3;  // yaw/pitch/head yaw
    if (!readVarInt(packet, len, &off, &data)) {
      return;
    }
    if (off + 6 > len) {
      return;
    }
    if (isPlayerEntityType(entityType)) {
      updateRemotePlayerFromServer(entityId, x, y, z);
    }
    return;
  }

  if (packetId == 0x1F) {  // Teleport entity
    int32_t entityId = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!readVarInt(packet, len, &off, &entityId) || !readF64(packet, len, &off, &x) ||
        !readF64(packet, len, &off, &y) || !readF64(packet, len, &off, &z)) {
      return;
    }
    updateRemotePlayerFromServer(entityId, x, y, z);
    return;
  }

  if (packetId == 0x46) {  // Remove entities (single)
    int32_t entityId = 0;
    if (readVarInt(packet, len, &off, &entityId)) {
      removeRemotePlayer(entityId);
    }
    return;
  }

  if (packetId == 0x57) {  // Set center chunk
    int32_t cx = 0;
    int32_t cz = 0;
    if (!readVarInt(packet, len, &off, &cx) || !readVarInt(packet, len, &off, &cz)) {
      return;
    }
    if (s_haveCenterChunk) {
      const int32_t dCx = cx - s_centerChunkX;
      const int32_t dCz = cz - s_centerChunkZ;
      if (dCx != 0 || dCz != 0) {
        const float shiftX = static_cast<float>(dCx * 16);
        const float shiftZ = static_cast<float>(dCz * 16);
        // Keep the player near the local window center while streaming chunks.
        s_camX -= shiftX;
        s_camZ -= shiftZ;
        s_localAnchorX -= shiftX;
        s_localAnchorZ -= shiftZ;
        for (int i = 0; i < kRemotePlayerMax; ++i) {
          if (!s_remotePlayers[i].active) {
            continue;
          }
          s_remotePlayers[i].x -= shiftX;
          s_remotePlayers[i].z -= shiftZ;
        }
      }
      if (dCx != 0 || dCz != 0) {
        Serial.printf("[mc] center (%ld,%ld)->(%ld,%ld) shift=(%ld,%ld) cam=(%.2f,%.2f)\n",
                      static_cast<long>(s_centerChunkX), static_cast<long>(s_centerChunkZ),
                      static_cast<long>(cx), static_cast<long>(cz), static_cast<long>(dCx * 16),
                      static_cast<long>(dCz * 16), s_camX, s_camZ);
      }
    }
    s_centerChunkX = cx;
    s_centerChunkZ = cz;
    s_haveCenterChunk = true;
    return;
  }

  if (packetId == 0x27) {  // Chunk Data and Update Light
    s_chunkRxCount++;
    const bool applied = decodeServerChunkIntoLocal(packet, len, off);
    if (applied) {
      s_haveRemoteWorld = true;
      s_chunkApplyCount++;
    } else {
      s_chunkDropCount++;
    }
    Serial.printf("[mc] chunk packet=%lu total=%u head=%u trunc=%u applied=%u ok=%lu drop=%lu\n",
                  static_cast<unsigned long>(s_chunkRxCount), static_cast<unsigned int>(totalLen),
                  static_cast<unsigned int>(len), truncated ? 1U : 0U, applied ? 1U : 0U,
                  static_cast<unsigned long>(s_chunkApplyCount),
                  static_cast<unsigned long>(s_chunkDropCount));
    return;
  }
}

void processIncoming() {
  size_t budget = 262144;
  while (budget > 0 && s_mcSocket.connected()) {
    if (s_pktRemaining == 0) {
      const int c = s_mcSocket.read();
      if (c < 0) {
        break;
      }
      s_lastRxMs = millis();
      budget--;
      const uint8_t b = static_cast<uint8_t>(c);
      s_pktLenAccum |= static_cast<uint32_t>(b & 0x7F) << s_pktLenShift;
      s_pktLenShift += 7;
      s_pktLenBytes++;
      if (s_pktLenBytes > 5) {
        closeSocketToState("LEN_ERR");
        return;
      }
      if ((b & 0x80) == 0) {
        if (s_pktLenAccum > 1024UL * 1024UL) {
          closeSocketToState("PKT_TOO_BIG");
          return;
        }
        s_pktRemaining = s_pktLenAccum;
        s_pktTotalLen = s_pktLenAccum;
        s_pktLenAccum = 0;
        s_pktLenShift = 0;
        s_pktLenBytes = 0;
        s_pktHeadLen = 0;
      }
      continue;
    }

    size_t toRead = std::min<size_t>(budget, static_cast<size_t>(s_pktRemaining));
    uint8_t tmp[1024];
    if (toRead > sizeof(tmp)) {
      toRead = sizeof(tmp);
    }
    const int n = s_mcSocket.read(tmp, static_cast<int>(toRead));
    if (n <= 0) {
      break;
    }
    s_lastRxMs = millis();
    budget -= static_cast<size_t>(n);

    if (s_pktHeadLen < sizeof(s_pktHead)) {
      const size_t copyN = std::min<size_t>(static_cast<size_t>(n), sizeof(s_pktHead) - s_pktHeadLen);
      memcpy(s_pktHead + s_pktHeadLen, tmp, copyN);
      s_pktHeadLen += copyN;
    }

    s_pktRemaining -= static_cast<uint32_t>(n);
    if (s_pktRemaining == 0) {
      handlePacket(s_pktHead, s_pktHeadLen, static_cast<size_t>(s_pktTotalLen),
                   s_pktHeadLen < s_pktTotalLen);
      s_pktHeadLen = 0;
      s_pktTotalLen = 0;
    }
  }
}

void tryConnectAndLogin() {
  if (millis() - s_lastMcAttemptMs < kMcReconnectMs) {
    return;
  }
  s_lastMcAttemptMs = millis();
  setMcState("CONNECTING");

  if (!s_mcSocket.connect(s_mcHost.c_str(), s_mcPort)) {
    setMcState("CONNECT_FAIL");
    return;
  }
  s_mcSocket.setNoDelay(true);
  resetPacketParsing();
  s_sentPlayerLoaded = false;
  s_sentKnownPacksAck = false;
  s_sentConfigAck = false;
  s_haveServerAnchor = false;
  s_haveCenterChunk = false;
  s_haveRemoteWorld = false;
  clearRemotePlayers();

  if (!sendHandshake() || !sendLoginStart()) {
    closeSocketToState("TX_FAIL");
    return;
  }

  setMcStage(MC_WAIT_LOGIN_SUCCESS);
  s_lastRxMs = millis();
  setMcState("WAIT_LOGIN");
}

bool wifiReadyForMc() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (WiFi.isConnected()) {
    return true;
  }
  return static_cast<uint32_t>(WiFi.localIP()) != 0;
}

}  // namespace

void mcSetConfig(const String &host, uint16_t port, const String &playerName, bool autoConnect) {
  String trimmedHost = host;
  trimmedHost.trim();

  String trimmedPlayer = playerName;
  trimmedPlayer.trim();
  if (trimmedPlayer.length() == 0) {
    trimmedPlayer = kMcDefaultPlayer;
  }
  if (trimmedPlayer.length() > 16) {
    trimmedPlayer = trimmedPlayer.substring(0, 16);
  }

  if (port == 0) {
    port = kMcDefaultPort;
  }

  const bool changed = (s_mcHost != trimmedHost) || (s_mcPort != port) || (s_mcPlayerName != trimmedPlayer) ||
                       (s_mcAutoConnect != autoConnect);
  s_mcHost = trimmedHost;
  s_mcPort = port;
  s_mcPlayerName = trimmedPlayer;
  s_mcAutoConnect = autoConnect;

  if (changed) {
    mcForceReconnect();
  }
}

void mcForceReconnect() {
  if (s_mcSocket.connected()) {
    s_mcSocket.stop();
  }
  setMcStage(MC_IDLE);
  s_sentPlayerLoaded = false;
  s_sentKnownPacksAck = false;
  s_sentConfigAck = false;
  s_haveServerAnchor = false;
  s_haveCenterChunk = false;
  s_haveRemoteWorld = false;
  clearRemotePlayers();
  clearWorld();
  resetPacketParsing();
  s_lastMcAttemptMs = 0;
  setMcState("RECONNECT");
}

void mcSetHeldSlot(uint8_t slot) {
  if (slot >= 9) {
    return;
  }
  if (s_mcSocket.connected() && s_mcStage == MC_PLAY) {
    if (!sendHeldItemSlot(slot)) {
      closeSocketToState("TX_FAIL");
    }
  }
}

bool mcTryPlaceBlockServer(const RayHit &hit, uint8_t localBlockId) {
  (void)localBlockId;
  return sendUseItemOn(hit);
}

bool mcTryBreakBlockServer(const RayHit &hit) {
  return sendBreakBlock(hit);
}

void mcUpdate() {
  unsigned long now = millis();
  const bool wifiReady = wifiReadyForMc();
  if (wifiReady) {
    s_lastWifiOkMs = now;
  }
  // Avoid false NO_WIFI during short status transitions.
  if (!wifiReady && (s_lastWifiOkMs == 0 || now - s_lastWifiOkMs > 2500)) {
    closeSocketToState("NO_WIFI");
    return;
  }
  if (!wifiReady) {
    return;
  }

  if (!s_mcAutoConnect) {
    closeSocketToState("DISABLED");
    return;
  }

  if (s_mcHost.length() == 0) {
    closeSocketToState("NO_HOST");
    return;
  }

  if (!s_mcSocket.connected()) {
    if (s_mcStage != MC_IDLE) {
      closeSocketToState("DISCONNECTED");
    }
    tryConnectAndLogin();
    return;
  }

  processIncoming();
  if (!s_mcSocket.connected()) {
    closeSocketToState("DISCONNECTED");
    return;
  }
  now = millis();

  if (s_mcStage != MC_IDLE && s_mcStage != MC_PLAY) {
    unsigned long idleLimit = kMcHandshakeIdleTimeoutMs;
    if (s_mcStage == MC_WAIT_CONFIG_FINISH || s_mcStage == MC_WAIT_PLAY_LOGIN) {
      idleLimit = kMcConfigIdleTimeoutMs;
    }
    if (now - s_lastRxMs > idleLimit) {
      closeSocketToState("RX_TIMEOUT");
      return;
    }
  }

  if (s_mcStage == MC_PLAY) {
    if (now - s_lastMoveSendMs >= kMcMovePacketMs) {
      if (!sendMovementPacket()) {
        closeSocketToState("TX_FAIL");
        return;
      }
      s_lastMoveSendMs = now;
    }
  }
}

bool mcReadyForGameplay() {
  return s_mcSocket.connected() && s_mcStage == MC_PLAY && s_haveRemoteWorld;
}

}  // namespace game
