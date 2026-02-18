#pragma once

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstddef>
#include <cstdint>

namespace game {

// WiFi credentials requested by user.
inline constexpr char kWifiSsid[] = "";
inline constexpr char kWifiPass[] = "";

// ST7735S wiring on this board.
inline constexpr int kTftMosi = 18;
inline constexpr int kTftSclk = 17;
inline constexpr int kTftCs = 46;
inline constexpr int kTftDc = 8;
inline constexpr int kTftRst = 20;
inline constexpr int kTftBlk = 9;

// Physical fallback buttons.
inline constexpr int kBtnLeft = 39;   // BTN39
inline constexpr int kBtnRight = 40;  // BTN40

inline constexpr int kScreenW = 160;
inline constexpr int kScreenH = 128;
inline constexpr float kFocal = 90.0f;
inline constexpr float kNearPlane = 0.25f;
inline constexpr float kTurnSpeedRad = 1.6f;
inline constexpr float kPitchSpeedRad = 1.35f;
inline constexpr float kMoveSpeed = 2.35f;
inline constexpr float kJumpVelocity = 5.9f;
inline constexpr float kGravity = 11.5f;
inline constexpr float kEyeHeight = 1.72f;
inline constexpr float kPlayerHeight = 1.82f;
inline constexpr float kPlayerRadius = 0.27f;
inline constexpr float kMaxPitch = 1.52f;
inline constexpr float kRenderRadius = 9.0f;
inline constexpr bool kDrawEdges = false;

inline constexpr int kWorldW = 16;
inline constexpr int kWorldD = 16;
inline constexpr int kWorldHMax = 14;
inline constexpr int kMaxFaces = 2600;
inline constexpr int kInvSlots = 5;
inline constexpr int kInvStackMax = 99;
inline constexpr uint8_t kInvStartBlocks = 48;
inline constexpr size_t kBindingCount = 18;
inline constexpr int kRemotePlayerMax = 8;

inline constexpr unsigned long kWifiRetryMs = 9000;
inline constexpr unsigned long kControlTimeoutMs = 2200;
inline constexpr unsigned long kPlaceCooldownMs = 120;
inline constexpr unsigned long kMcReconnectMs = 5000;
inline constexpr unsigned long kMcMovePacketMs = 200;
inline constexpr uint16_t kMcDefaultPort = 25565;
inline constexpr char kMcDefaultHost[] = "192.168.3.144";
inline constexpr char kMcDefaultPlayer[] = "esp32player";

struct Vec3 {
  float x;
  float y;
  float z;
};

struct ProjVert {
  int16_t sx;
  int16_t sy;
  float cz;
};

struct FaceQuad {
  ProjVert p[4];
  float depth;
  uint16_t color;
};

struct KeyBinding {
  const char *action;
  String keyCode;
  bool active;
};

struct HotbarSlot {
  uint8_t blockId;
  uint8_t count;
};

struct RemotePlayerView {
  bool active;
  int32_t entityId;
  float x;
  float feetY;
  float z;
};

enum BlockId : uint8_t {
  BLOCK_AIR = 0,
  BLOCK_GRASS = 1,
  BLOCK_DIRT = 2,
  BLOCK_STONE = 3,
  BLOCK_WOOD = 4,
  BLOCK_SAND = 5,
  BLOCK_ORE = 6,
  BLOCK_BEDROCK = 7,
};

struct RayHit {
  bool hit;
  int x;
  int y;
  int z;
  int prevX;
  int prevY;
  int prevZ;
  int nx;
  int ny;
  int nz;
};

extern Adafruit_ST7735 tft;
extern GFXcanvas16 canvas;
extern WebServer server;

extern uint8_t s_voxel[kWorldW][kWorldHMax][kWorldD];
extern FaceQuad s_faces[kMaxFaces];
extern int s_faceCount;

extern float s_camX;
extern float s_camY;
extern float s_camZ;
extern float s_yaw;
extern float s_pitch;
extern float s_velY;
extern float s_camCy;
extern float s_camSy;
extern float s_camCp;
extern float s_camSp;

extern unsigned long s_lastFpsMs;
extern unsigned long s_lastFrameMs;
extern uint32_t s_frameCounter;
extern uint16_t s_fps;

extern unsigned long s_lastWifiAttemptMs;
extern unsigned long s_lastInputMs;
extern unsigned long s_lastEditMs;
extern unsigned long s_lastMcAttemptMs;
extern bool s_prevJumpDown;
extern bool s_prevBreakDown;
extern bool s_prevPlaceDown;
extern bool s_prevInvPrevDown;
extern bool s_prevInvNextDown;
extern bool s_prevSlotDown[kInvSlots];
extern bool s_gameStarted;
extern HotbarSlot s_hotbar[kInvSlots];
extern int s_selectedSlot;

extern KeyBinding s_bindings[kBindingCount];
extern const char *kSlotActions[kInvSlots];

extern bool s_hasAimHit;
extern RayHit s_aimHit;
extern RemotePlayerView s_remotePlayers[kRemotePlayerMax];

extern String s_mcHost;
extern uint16_t s_mcPort;
extern String s_mcPlayerName;
extern bool s_mcAutoConnect;
extern String s_mcState;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline constexpr uint16_t kSky = rgb565(86, 147, 217);
inline constexpr uint16_t kGroundFog = rgb565(97, 125, 74);
inline constexpr uint16_t kGrassTop = rgb565(78, 171, 84);
inline constexpr uint16_t kGrassSide = rgb565(114, 96, 62);
inline constexpr uint16_t kDirt = rgb565(126, 91, 61);
inline constexpr uint16_t kStone = rgb565(116, 120, 127);
inline constexpr uint16_t kWood = rgb565(139, 103, 66);
inline constexpr uint16_t kSand = rgb565(218, 202, 140);
inline constexpr uint16_t kOre = rgb565(210, 170, 58);
inline constexpr uint16_t kBedrock = rgb565(56, 56, 66);
inline constexpr uint16_t kEdge = rgb565(24, 24, 24);

const char *blockShortName(uint8_t blockId);
uint16_t blockTopColor(uint8_t blockId);
uint16_t blockSideColor(uint8_t blockId);

}  // namespace game
