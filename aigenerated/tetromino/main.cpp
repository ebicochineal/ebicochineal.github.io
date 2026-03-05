#include "E512W3D.hpp"
#include "cube.hpp"
#include <math.h>

#define FW 10
#define FH 20
#define BLOCK_SIZE 2.2f

E512W3DWindow w(color565(4, 10, 14));
Object3D camera;

int8_t field[FH][FW] = {0};
Object3D fieldObjs[FH][FW];
Object3D currentMinoObjs[4];
Object3D ghostObjs[4];
Object3D nextMinoObjs[6][4];
Object3D holdMinoObjs[4];

#define WALL_COUNT ((FH * 2) + FW + 2)
Object3D wallObjs[WALL_COUNT];

uint16_t minoColors[8] = {
    0,
    color565(240, 220,  50),  // O ゴールドイエロー
    color565( 40, 220, 230),  // I ブライトアクア
    color565(190,  60, 230),  // T バイオレット
    color565(240, 130,  30),  // L バーントオレンジ
    color565( 50,  90, 240),  // J コバルトブルー
    color565( 50, 210,  90),  // S エメラルド
    color565(230,  60,  80),  // Z クリムゾン
};

uint16_t getDimColor(int type) {
    switch(type) {
        case 1: return color565(40, 40, 0);
        case 2: return color565(0, 40, 40);
        case 3: return color565(30, 0, 40);
        case 4: return color565(40, 25, 0);
        case 5: return color565(0, 0, 40);
        case 6: return color565(0, 40, 0);
        case 7: return color565(40, 0, 0);
        default: return color565(30, 30, 30);
    }
}

struct FlashEffect { int count = 0; uint16_t color = 0; } flashEffect;
float cameraOffsetY = 0.0f;

int score = 0;
int level = 1;
int totalLines = 0;

const int fallIntervalMs[21] = {
    0,    // index 0 unused
    800,  // 1
    717,  // 2
    633,  // 3
    550,  // 4
    467,  // 5
    383,  // 6
    300,  // 7
    217,  // 8
    133,  // 9
    100,  // 10
     83,  // 11
     67,  // 12
     50,  // 13
     33,  // 14
     17,  // 15
     17,  // 16
     17,  // 17
     17,  // 18
     17,  // 19
     17,  // 20+
};

int getFallInterval() {
    int lv = level < 20 ? level : 20;
    return fallIntervalMs[lv];
}
int holdType = -1;
bool holdUsed = false;
bool gameOver = false;

// ライン消去フラッシュ
#define MAX_CLEAR_LINES 4
bool  lineClearPending  = false;
int   lineClearTimer    = 0;
int   lineClearRows[MAX_CLEAR_LINES];
int   lineClearCount    = 0;
int   pendingScore      = 0;
uint16_t lineClearFlashCol = 0;

// ============================================================
//  背景エフェクト
// ============================================================

struct BGStar {
    float    x, y, speed;
    float    phase;
    float    phaseSpeed;
    uint8_t  size;
};

// 常時漂うアンビエント粒子
struct BGFlow {
    float    x, y, vx, vy;
    float    phase;
    float    phaseSpeed;
    int      life, maxLife;
    bool     active;
};

// 放射状スターバースト（サークルの代わり）
struct BGBurst {
    float    cx, cy;
    float    r, maxR;   // 現在半径, 最大半径
    float    innerFrac; // 線の内側 = r * innerFrac
    uint16_t color;
    int      numRays;
    bool     active;
};

struct BGParticle {
    float    x, y, vx, vy;
    uint16_t color;
    int      life;
};

#define NUM_STARS         120
#define NUM_BG_BURSTS       6
#define NUM_BG_PARTICLES   96
#define NUM_BG_FLOW       140

BGStar     bgStars[NUM_STARS];
BGBurst    bgBursts[NUM_BG_BURSTS];
BGParticle bgParticles[NUM_BG_PARTICLES];
BGFlow     bgFlow[NUM_BG_FLOW];
int        bgFrame = 0;

// グローバルカラー位相（毎フレーム少しずつ進む）
float bgColorPhase = 0.0f;

// 画面座標＋グローバル位相から rgb565 を返す（水色→青→紫 の帯）
uint16_t posColor(float x, float y, float bright) {
    // 0.0〜1.0 の位相値を作る（位置 + 時間）
    float t = (x / 320.0f) * 0.5f
            + (y / 240.0f) * 0.3f
            + bgColorPhase;
    t = t - (int)t; // 0〜1 に正規化

    // 水色(0) → 青(0.4) → 紫(0.7) → 青紫(1.0) の範囲だけ使う
    // RGB を直接補間
    float r, g, b;
    if (t < 0.4f) {
        // 水色 → 青
        float s = t / 0.4f;
        r = 0.0f;
        g = 1.0f - s * 0.85f;   // 255 → 38
        b = 1.0f;
    } else if (t < 0.7f) {
        // 青 → 紫
        float s = (t - 0.4f) / 0.3f;
        r = s * 0.72f;           // 0 → 184
        g = 0.15f - s * 0.15f;  // 38 → 0
        b = 1.0f;
    } else {
        // 紫 → 水色（ループ）
        float s = (t - 0.7f) / 0.3f;
        r = 0.72f - s * 0.72f;  // 184 → 0
        g = s * 1.0f;            // 0 → 255
        b = 1.0f;
    }

    uint8_t R = (uint8_t)(r * bright * 255.0f);
    uint8_t G = (uint8_t)(g * bright * 255.0f);
    uint8_t B = (uint8_t)(b * bright * 255.0f);
    return color565(R, G, B);
}

void initBG() {
    for (int i = 0; i < NUM_STARS; i++) {
        bgStars[i].x          = rand() % 320;
        bgStars[i].y          = rand() % 240;
        bgStars[i].speed      = 0.08f + (rand() % 18) * 0.05f;
        bgStars[i].phase      = (rand() % 628) * 0.01f;
        bgStars[i].phaseSpeed = 0.018f + (rand() % 20) * 0.003f;
        bgStars[i].size       = (rand() % 7 == 0) ? 2 : 1;
    }
    for (int i = 0; i < NUM_BG_BURSTS;    i++) bgBursts[i].active  = false;
    for (int i = 0; i < NUM_BG_PARTICLES; i++) bgParticles[i].life = 0;
    for (int i = 0; i < NUM_BG_FLOW; i++) {
        bgFlow[i].active     = true;
        bgFlow[i].x          = rand() % 320;
        bgFlow[i].y          = rand() % 240;
        bgFlow[i].vx         = ((rand() % 20) - 10) * 0.02f;
        bgFlow[i].vy         = -0.15f - (rand() % 20) * 0.02f;
        bgFlow[i].phase      = (rand() % 628) * 0.01f;
        bgFlow[i].phaseSpeed = 0.025f + (rand() % 15) * 0.004f;
        bgFlow[i].maxLife    = 120 + rand() % 180;
        bgFlow[i].life       = rand() % bgFlow[i].maxLife;
    }
}

// cx,cy: 中心スクリーン座標, col: 色, rays: 放射本数, maxR: 最大半径
void spawnBGBurst(float cx, float cy, uint16_t col, int rays = 12, float maxR = 140.0f) {
    for (int i = 0; i < NUM_BG_BURSTS; i++) {
        if (!bgBursts[i].active) {
            bgBursts[i] = { cx, cy, 2.0f, maxR, 0.55f, col, rays, true };
            return;
        }
    }
    // 空きがなければ最も進んだものを上書き
    int oldest = 0;
    for (int i = 1; i < NUM_BG_BURSTS; i++)
        if (bgBursts[i].r > bgBursts[oldest].r) oldest = i;
    bgBursts[oldest] = { cx, cy, 2.0f, maxR, 0.55f, col, rays, true };
}

void spawnBGParticles(float cx, float cy, uint16_t col, int count) {
    int spawned = 0;
    for (int i = 0; i < NUM_BG_PARTICLES && spawned < count; i++) {
        if (bgParticles[i].life <= 0) {
            float a = (rand() % 628) * 0.01f;
            float s = 0.5f + (rand() % 28) * 0.11f;
            bgParticles[i].x    = cx;
            bgParticles[i].y    = cy;
            bgParticles[i].vx   = cosf(a) * s;
            bgParticles[i].vy   = sinf(a) * s - 0.6f;
            bgParticles[i].color = col;
            bgParticles[i].life  = 28 + rand() % 28;
            spawned++;
        }
    }
}

// 入力由来のグローバル流れベクトル（滑らかに追従）
float bgWindX = 0.0f;   // 横流れ（左右移動）
float bgWindY = 0.0f;   // 縦流れ（ソフト/ハードドロップ）
float bgSwirlPhase = 0.0f;  // 回転時の渦エネルギー
float bgSwirlDecay = 0.0f;

// loop()から呼ぶ：入力に応じてwind/swirlを更新
void updateBGInput(bool moveL, bool moveR, bool softDrop, bool hardDrop, bool rotating) {
    // 目標値に向けてスムーズに近づく
    float targetX = moveL ? -1.8f : (moveR ? 1.8f : 0.0f);
    float targetY = softDrop ? 2.5f : (hardDrop ? 8.0f : 0.0f);
    bgWindX += (targetX - bgWindX) * 0.18f;
    bgWindY += (targetY - bgWindY) * 0.18f;
    // ハードドロップ瞬間だけ強くキック
    if (hardDrop) bgWindY = 10.0f;
    // 回転時は渦エネルギーを注入
    if (rotating) bgSwirlDecay = 1.0f;
    bgSwirlDecay *= 0.92f;
    bgSwirlPhase += 0.15f + bgSwirlDecay * 0.3f;
}

void updateBG() {
    bgFrame++;
    bgColorPhase += 0.008f;
    if (bgColorPhase > 6.0f) bgColorPhase -= 6.0f;

    for (int i = 0; i < NUM_STARS; i++) {
        // wind の影響を星にも加える（軽め）
        bgStars[i].x    += bgWindX * 0.25f;
        bgStars[i].y    += bgStars[i].speed + bgWindY * 0.15f;
        bgStars[i].phase += bgStars[i].phaseSpeed;
        if (bgStars[i].phase > 6.2832f) bgStars[i].phase -= 6.2832f;
        if (bgStars[i].y >= 242.0f) { bgStars[i].y = -2.0f;  bgStars[i].x = rand() % 320; }
        if (bgStars[i].y < -2.0f)   { bgStars[i].y = 242.0f; bgStars[i].x = rand() % 320; }
        if (bgStars[i].x < -2.0f)   bgStars[i].x = 322.0f;
        if (bgStars[i].x > 322.0f)  bgStars[i].x = -2.0f;
    }
    for (int i = 0; i < NUM_BG_BURSTS; i++) {
        if (bgBursts[i].active) {
            bgBursts[i].r += bgBursts[i].maxR * 0.045f;
            if (bgBursts[i].r > bgBursts[i].maxR) bgBursts[i].active = false;
        }
    }
    for (int i = 0; i < NUM_BG_PARTICLES; i++) {
        if (bgParticles[i].life > 0) {
            bgParticles[i].x  += bgParticles[i].vx + bgWindX * 0.12f;
            bgParticles[i].y  += bgParticles[i].vy + bgWindY * 0.08f;
            bgParticles[i].vy += 0.11f;
            bgParticles[i].life--;
        }
    }
    // アンビエントフロー粒子（wind＋swirlの影響を受ける）
    for (int i = 0; i < NUM_BG_FLOW; i++) {
        if (!bgFlow[i].active) continue;

        // 渦：粒子の位置に応じて中心からの接線方向に力を加える
        float dx = bgFlow[i].x - 160.0f;
        float dy = bgFlow[i].y - 120.0f;
        float swirlVx =  dy * 0.0015f * bgSwirlDecay;
        float swirlVy = -dx * 0.0015f * bgSwirlDecay;

        bgFlow[i].x     += bgFlow[i].vx + bgWindX * 0.35f + swirlVx;
        bgFlow[i].y     += bgFlow[i].vy + bgWindY * 0.25f + swirlVy;
        bgFlow[i].phase += bgFlow[i].phaseSpeed;
        if (bgFlow[i].phase > 6.2832f) bgFlow[i].phase -= 6.2832f;
        bgFlow[i].life++;
        if (bgFlow[i].life >= bgFlow[i].maxLife ||
            bgFlow[i].y < -5.0f || bgFlow[i].x < -5.0f || bgFlow[i].x > 325.0f) {
            // ハードドロップ中は下から大量スポーン
            float spawnX = (float)(rand() % 320);
            float spawnY = (bgWindY > 3.0f) ? 244.0f : (float)(rand() % 240);
            bgFlow[i].x          = spawnX;
            bgFlow[i].y          = spawnY;
            bgFlow[i].vx         = ((rand() % 20) - 10) * 0.02f;
            bgFlow[i].vy         = -0.15f - (rand() % 20) * 0.02f;
            bgFlow[i].phase      = (rand() % 628) * 0.01f;
            bgFlow[i].phaseSpeed = 0.025f + (rand() % 15) * 0.004f;
            bgFlow[i].maxLife    = 120 + rand() % 180;
            bgFlow[i].life       = 0;
        }
    }
}

void drawBG() {
    // 極薄グリッド
    uint16_t gc = color565(12, 15, 24);
    for (int y = 0; y < 240; y += 20) w.drawLine(0, y, 319, y, gc);
    for (int x = 0; x < 320; x += 20) w.drawLine(x, 0, x, 239, gc);

    // 流れ星（位置ベースグラデーション呼吸グロー）
    for (int i = 0; i < NUM_STARS; i++) {
        float bright = 0.15f + 0.85f * (0.5f + 0.5f * sinf(bgStars[i].phase));
        uint16_t sc  = posColor(bgStars[i].x, bgStars[i].y, bright);
        int      sz  = bgStars[i].size;
        if (bright > 0.88f) {
            sz = 2;
            w.drawPoint((int16_t)bgStars[i].x, (int16_t)(bgStars[i].y - 1), 1,
                        posColor(bgStars[i].x, bgStars[i].y, 0.4f));
            w.drawPoint((int16_t)bgStars[i].x, (int16_t)(bgStars[i].y + 1), 1,
                        posColor(bgStars[i].x, bgStars[i].y, 0.25f));
        }
        w.drawPoint((int16_t)bgStars[i].x, (int16_t)bgStars[i].y, sz, sc);
    }

    // アンビエントフロー粒子（位置ベースグラデーション）
    for (int i = 0; i < NUM_BG_FLOW; i++) {
        if (!bgFlow[i].active) continue;
        float lifeFrac = (float)bgFlow[i].life / bgFlow[i].maxLife;
        float fade = (lifeFrac < 0.15f) ? (lifeFrac / 0.15f)
                   : (lifeFrac > 0.85f) ? ((1.0f - lifeFrac) / 0.15f)
                   : 1.0f;
        float bright = fade * (0.25f + 0.65f * (0.5f + 0.5f * sinf(bgFlow[i].phase)));
        if (bright < 0.04f) continue;
        w.drawPoint((int16_t)bgFlow[i].x, (int16_t)bgFlow[i].y, 1,
                    posColor(bgFlow[i].x, bgFlow[i].y, bright));
    }

    // 放射状スターバースト
    for (int i = 0; i < NUM_BG_BURSTS; i++) {
        if (!bgBursts[i].active) continue;
        float r    = bgBursts[i].r;
        float rIn  = r * bgBursts[i].innerFrac;
        float cx   = bgBursts[i].cx;
        float cy   = bgBursts[i].cy;
        uint16_t bc = bgBursts[i].color;
        int  rays  = bgBursts[i].numRays;
        float prog = r / bgBursts[i].maxR;          // 0→1

        // 輝度を進行度で落とす（消えていく感じ）
        float fade = 1.0f - prog * 0.85f;
        uint8_t br = (uint8_t)(((bc >> 11) & 0x1F) * fade) & 0x1F;
        uint8_t bg_ = (uint8_t)(((bc >>  5) & 0x3F) * fade) & 0x3F;
        uint8_t bb  = (uint8_t)(((bc      ) & 0x1F) * fade) & 0x1F;
        uint16_t fadeCol = (br << 11) | (bg_ << 5) | bb;

        for (int ray = 0; ray < rays; ray++) {
            float angle = ray * (6.2832f / rays);
            float cosA = cosf(angle), sinA = sinf(angle);
            int x1 = (int)(cx + cosA * rIn);
            int y1 = (int)(cy + sinA * rIn);
            int x2 = (int)(cx + cosA * r);
            int y2 = (int)(cy + sinA * r);
            uint16_t rc = posColor(x2, y2, fade * 0.9f);
            w.drawLine(x1, y1, x2, y2, rc);
            if (prog < 0.6f)
                w.drawPoint((int16_t)x2, (int16_t)y2, 2,
                            posColor(x2, y2, fade));
        }

        // 中間リングは省略
    }

    // ロック飛散パーティクル（位置ベース色＋フェードアウト）
    for (int i = 0; i < NUM_BG_PARTICLES; i++) {
        if (bgParticles[i].life <= 0) continue;
        float fad = bgParticles[i].life * (1.0f / 56.0f);
        if (fad > 1.0f) fad = 1.0f;
        w.drawPoint((int16_t)bgParticles[i].x, (int16_t)bgParticles[i].y, 1,
                    posColor(bgParticles[i].x, bgParticles[i].y, fad * 0.9f));
    }

    // 四隅ダイヤモンド装飾
    uint16_t dc = color565(32, 48, 72);
    w.drawLine( 4, 34,  20, 18, dc); w.drawLine(20, 18, 36, 34, dc);
    w.drawLine( 4, 34,  20, 50, dc); w.drawLine(20, 50, 36, 34, dc);
    w.drawLine(284, 34, 300, 18, dc); w.drawLine(300, 18, 316, 34, dc);
    w.drawLine(284, 34, 300, 50, dc); w.drawLine(300, 50, 316, 34, dc);
    w.drawLine( 4, 206,  20, 190, dc); w.drawLine(20, 190, 36, 206, dc);
    w.drawLine( 4, 206,  20, 222, dc); w.drawLine(20, 222, 36, 206, dc);
    w.drawLine(284, 206, 300, 190, dc); w.drawLine(300, 190, 316, 206, dc);
    w.drawLine(284, 206, 300, 222, dc); w.drawLine(300, 222, 316, 206, dc);

    // ライトストリーク
    int phase = bgFrame % 220;
    if (phase < 110) {
        int lx = phase * 3 - 50;
        w.drawLine(lx,     0, lx + 25, 239, color565( 0, 28, 48));
        w.drawLine(lx + 4, 0, lx + 28, 239, color565( 0, 18, 32));
    }
}

// ============================================================
//  ゲームデータ
// ============================================================

int bagPool[7];
int bagPos = 7;

void fillBag() {
    for (int i = 0; i < 7; i++) bagPool[i] = i;
    for (int i = 6; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = bagPool[i]; bagPool[i] = bagPool[j]; bagPool[j] = tmp;
    }
    bagPos = 0;
}

int drawFromBag() {
    if (bagPos >= 7) fillBag();
    return bagPool[bagPos++];
}

#define NEXT_COUNT 6
int nextQueue[NEXT_COUNT];

void initNextQueue() {
    for (int i = 0; i < NEXT_COUNT; i++) nextQueue[i] = drawFromBag();
}

int popNextQueue() {
    int val = nextQueue[0];
    for (int i = 0; i < NEXT_COUNT - 1; i++) nextQueue[i] = nextQueue[i + 1];
    nextQueue[NEXT_COUNT - 1] = drawFromBag();
    return val;
}

int tetrominoes[7][4][4][2] = {
    {{{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{0,1},{1,1}}},
    {{{-1,0},{0,0},{1,0},{2,0}}, {{1,-1},{1,0},{1,1},{1,2}}, {{-1,1},{0,1},{1,1},{2,1}}, {{0,-1},{0,0},{0,1},{0,2}}},
    {{{-1,0},{0,0},{1,0},{0,-1}}, {{0,-1},{0,0},{1,0},{0,1}}, {{-1,0},{0,0},{1,0},{0,1}}, {{0,-1},{-1,0},{0,0},{0,1}}},
    {{{-1,0},{0,0},{1,0},{1,-1}}, {{0,-1},{0,0},{0,1},{1,1}}, {{-1,1},{-1,0},{0,0},{1,0}}, {{-1,-1},{0,-1},{0,0},{0,1}}},
    {{{-1,-1},{-1,0},{0,0},{1,0}}, {{1,-1},{0,-1},{0,0},{0,1}}, {{-1,0},{0,0},{1,0},{1,1}}, {{0,-1},{0,0},{0,1},{-1,1}}},
    {{{-1,0},{0,0},{0,-1},{1,-1}}, {{0,-1},{0,0},{1,0},{1,1}}, {{-1,1},{0,1},{0,0},{1,0}}, {{-1,-1},{-1,0},{0,0},{0,1}}},
    {{{-1,-1},{0,-1},{0,0},{1,0}}, {{1,-1},{1,0},{0,0},{0,1}}, {{-1,0},{0,0},{0,1},{1,1}}, {{0,-1},{-1,0},{-1,1},{0,0}}}
};

const int kickTable_R[4][5][2]   = {{{0,0},{-1,0},{-1,-1},{0,2},{-1,2}}, {{0,0},{1,0},{1,1},{0,-2},{1,-2}}, {{0,0},{1,0},{1,-1},{0,2},{1,2}}, {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}};
const int kickTable_L[4][5][2]   = {{{0,0},{1,0},{1,-1},{0,2},{1,2}}, {{0,0},{1,0},{1,1},{0,-2},{1,-2}}, {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}}, {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}};
const int kickTable_I_R[4][5][2] = {{{0,0},{-2,0},{1,0},{-2,1},{1,-2}}, {{0,0},{-1,0},{2,0},{-1,-2},{2,1}}, {{0,0},{2,0},{-1,0},{2,-1},{-1,2}}, {{0,0},{1,0},{-2,0},{1,2},{-2,-1}}};
const int kickTable_I_L[4][5][2] = {{{0,0},{-1,0},{2,0},{-1,2},{2,-1}}, {{0,0},{2,0},{-1,0},{2,-1},{-1,2}}, {{0,0},{1,0},{-2,0},{1,2},{-2,-1}}, {{0,0},{-2,0},{1,0},{-2,-1},{1,2}}};

int curX = 4, curY = 0, curRot = 0, curType = 0;
unsigned long lastFall = 0;
int lowestY = 0, moveCount = 0;
unsigned long lockDelayTimer = 0;

bool currentKeys[256] = {false};
bool prevKeys[256] = {false};
unsigned long dasTimerA = 0, dasTimerD = 0;

void updateKeys() {
    int keysToCheck[] = {'A', 'D', 'S', 'W', 32};
    for (int k : keysToCheck) {
        prevKeys[k] = currentKeys[k];
        currentKeys[k] = E512W3DInput::getKey(k);
    }
}
bool isKeyDown(int k) { return currentKeys[k] && !prevKeys[k]; }
bool isKey(int k) { return currentKeys[k]; }

bool checkCollision(int tx, int ty, int tr) {
    for (int i = 0; i < 4; i++) {
        int x = tetrominoes[curType][tr][i][0], y = tetrominoes[curType][tr][i][1];
        if (tx+x < 0 || tx+x >= FW || ty+y >= FH) return true;
        if (ty+y >= 0 && field[ty+y][tx+x]) return true;
    }
    return false;
}

void updateNextMinoColors() {
    for (int n = 0; n < NEXT_COUNT; n++) {
        uint16_t c = minoColors[nextQueue[n] + 1];
        for (int i = 0; i < 4; i++) nextMinoObjs[n][i].color = c;
    }
}

void spawn() {
    curX = 4; curY = 0; curRot = 0;
    curType = popNextQueue();

    for (int i = 0; i < 4; i++) {
        currentMinoObjs[i].color = minoColors[curType + 1];
        ghostObjs[i].color = minoColors[curType + 1];
    }
    updateNextMinoColors();

    lowestY = 0;
    moveCount = 0;
    lockDelayTimer = 0;
    holdUsed = false;

    if (checkCollision(curX, curY, curRot)) {
        gameOver = true;
    }
}

// フェーズ2: グレーフラッシュ後に実際に行を消す
void collapseLines() {
    for (int c = 0; c < lineClearCount; c++) {
        int row = lineClearRows[c];
        for (int ty = row; ty > 0; ty--)
            for (int x = 0; x < FW; x++)
                field[ty][x] = field[ty-1][x];
        for (int x = 0; x < FW; x++) field[0][x] = 0;
        // 後続のrowインデックスを1つ下にずらす
        for (int d = c+1; d < lineClearCount; d++)
            lineClearRows[d]++;
    }

    score += pendingScore;
    totalLines += lineClearCount;
    level = totalLines / 10 + 1;

    flashEffect.count = 6;
    flashEffect.color = lineClearFlashCol;
    cameraOffsetY = 1.5f + (lineClearCount * 0.5f);

    uint16_t lockCol = minoColors[curType + 1];
    spawnBGBurst(160.0f, 120.0f, lockCol, 12 + lineClearCount * 4, 130.0f + lineClearCount * 20.0f);
    if (lineClearCount >= 2) spawnBGBurst( 80.0f, 120.0f, lockCol, 8, 100.0f);
    if (lineClearCount >= 3) spawnBGBurst(240.0f, 120.0f, lockCol, 8, 100.0f);
    if (lineClearCount == 4) spawnBGBurst(160.0f,  50.0f, lockCol, 16, 160.0f);

    lineClearPending = false;
    lineClearCount   = 0;
    spawn();
}

void lockMino() {
    // ブロックをフィールドに配置
    for (int i = 0; i < 4; i++) {
        int x = tetrominoes[curType][curRot][i][0], y = tetrominoes[curType][curRot][i][1];
        if (curY + y >= 0) field[curY + y][curX + x] = curType + 1;
    }

    // ---- 背景パーティクル（ピース固定時は常に） ----
    {
        const float PERSP = 160.0f / 62.0f;
        uint16_t lockCol = minoColors[curType + 1];
        for (int i = 0; i < 4; i++) {
            int bx = tetrominoes[curType][curRot][i][0];
            int by = tetrominoes[curType][curRot][i][1];
            float px = 160.0f + (curX + bx - FW / 2.0f) * BLOCK_SIZE * PERSP;
            float py = 120.0f - (curY + by - FH / 2.0f) * BLOCK_SIZE * PERSP;
            spawnBGParticles(px, py, lockCol, 14);
        }
    }

    // 揃った行を探してグレー(9)でマーク → フラッシュ待ち
    lineClearCount = 0;
    pendingScore   = 0;
    for (int y = FH - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < FW; x++) if (!field[y][x] || field[y][x] == 9) full = false;
        if (full) {
            lineClearRows[lineClearCount++] = y;
            for (int x = 0; x < FW; x++) field[y][x] = 9; // グレーマーカー
        }
    }

    if (lineClearCount > 0) {
        if (lineClearCount == 1) pendingScore = 100 * level;
        else if (lineClearCount == 2) pendingScore = 300 * level;
        else if (lineClearCount == 3) pendingScore = 500 * level;
        else                          pendingScore = 800 * level;

        lineClearFlashCol  = getDimColor(curType + 1);
        lineClearPending   = true;
        lineClearTimer     = 6;  // 約2回点滅
    } else {
        cameraOffsetY = 1.0f;
        spawn();
    }
}

void setup() {
    e512w3d.width = 320; e512w3d.height = 240; w.width = 320; w.height = 240;
    cubeInit();

    bagPos = 7;
    initNextQueue();

    for (int y = 0; y < FH; y++) {
        for (int x = 0; x < FW; x++) {
            fieldObjs[y][x].mesh = &cube;
            fieldObjs[y][x].position = Vector3((x - FW/2.0f) * BLOCK_SIZE, (FH/2.0f - y) * BLOCK_SIZE, 0);
            fieldObjs[y][x].render_type = RenderType::None;
            w.addChild(fieldObjs[y][x]);
        }
    }

    int wallIdx = 0;
    for (int y = 0; y < FH; y++) {
        wallObjs[wallIdx].mesh = &cube;
        wallObjs[wallIdx].position = Vector3((-1 - FW/2.0f) * BLOCK_SIZE, (FH/2.0f - y) * BLOCK_SIZE, 0);
        wallObjs[wallIdx].render_type = RenderType::PolygonColor; wallObjs[wallIdx].color = color565(60, 60, 60);
        w.addChild(wallObjs[wallIdx++]);

        wallObjs[wallIdx].mesh = &cube;
        wallObjs[wallIdx].position = Vector3((FW - FW/2.0f) * BLOCK_SIZE, (FH/2.0f - y) * BLOCK_SIZE, 0);
        wallObjs[wallIdx].render_type = RenderType::PolygonColor; wallObjs[wallIdx].color = color565(60, 60, 60);
        w.addChild(wallObjs[wallIdx++]);
    }
    for (int x = -1; x <= FW; x++) {
        wallObjs[wallIdx].mesh = &cube;
        wallObjs[wallIdx].position = Vector3((x - FW/2.0f) * BLOCK_SIZE, (FH/2.0f - FH) * BLOCK_SIZE, 0);
        wallObjs[wallIdx].render_type = RenderType::PolygonColor; wallObjs[wallIdx].color = color565(60, 60, 60);
        w.addChild(wallObjs[wallIdx++]);
    }

    for (int i = 0; i < 4; i++) {
        currentMinoObjs[i].mesh = &cube; currentMinoObjs[i].render_type = RenderType::PolygonColor; w.addChild(currentMinoObjs[i]);
        ghostObjs[i].mesh = &cube; ghostObjs[i].render_type = RenderType::WireFrame; w.addChild(ghostObjs[i]);
        holdMinoObjs[i].mesh = &cube; holdMinoObjs[i].render_type = RenderType::None; w.addChild(holdMinoObjs[i]);
    }

    for (int n = 0; n < NEXT_COUNT; n++) {
        for (int i = 0; i < 4; i++) {
            nextMinoObjs[n][i].mesh = &cube;
            nextMinoObjs[n][i].render_type = RenderType::PolygonColor;
            w.addChild(nextMinoObjs[n][i]);
        }
    }

    camera.position = Vector3(0, 0, 62);
    w.setCamera(camera); w.setDirectionalLight(-1, -1, -1);
    e512w3d.add(w); e512w3d.begin();

    initBG();

    spawn();
    
    w.ambient = 0.25;
}

void loop() {
    if (e512w3d.isFixedTime()) {
        unsigned long now = millis();
        E512W3DInput::update();
        updateKeys();

        // ゲームオーバー中の処理
        if (gameOver) {
            bool anyKey = isKeyDown('A') || isKeyDown('D') || isKeyDown('S') || isKeyDown('W')
                       || isKeyDown(32)
                       || E512W3DInput::getButtonDown(0) || E512W3DInput::getButtonDown(2);

            if (anyKey) {
                for (int y = 0; y < FH; y++)
                    for (int x = 0; x < FW; x++)
                        field[y][x] = 0;
                score = 0;
                level = 1;
                totalLines = 0;
                holdType = -1;
                holdUsed = false;
                gameOver = false;
                cameraOffsetY = 0.0f;
                flashEffect.count = 0;
                bagPos = 7;
                initNextQueue();
                spawn();
            }

            for (int y = 0; y < FH; y++) {
                for (int x = 0; x < FW; x++) {
                    if (field[y][x]) {
                        fieldObjs[y][x].render_type = RenderType::PolygonColor;
                        fieldObjs[y][x].color = getDimColor(field[y][x]);
                    } else {
                        fieldObjs[y][x].render_type = RenderType::None;
                    }
                }
            }
            for (int i = 0; i < 4; i++) {
                currentMinoObjs[i].render_type = RenderType::None;
                ghostObjs[i].render_type = RenderType::None;
                holdMinoObjs[i].render_type = RenderType::None;
            }
            for (int n = 0; n < NEXT_COUNT; n++)
                for (int i = 0; i < 4; i++)
                    nextMinoObjs[n][i].render_type = RenderType::None;

            w.bgcolor = color565(4, 10, 14);
            camera.position = Vector3(0, 0, 62);
            w.setCamera(camera);

            e512w3d.clear();
            w.begin();
            updateBG();
            drawBG();

            // 各オブジェクトを個別描画
            for (int y = 0; y < FH; y++)
                for (int x = 0; x < FW; x++)
                    w.draw(fieldObjs[y][x]);
            for (int i = 0; i < WALL_COUNT; i++)
                w.draw(wallObjs[i]);
            for (int i = 0; i < 4; i++) {
                w.draw(currentMinoObjs[i]);
                w.draw(ghostObjs[i]);
                w.draw(holdMinoObjs[i]);
            }
            for (int n = 0; n < NEXT_COUNT; n++)
                for (int i = 0; i < 4; i++)
                    w.draw(nextMinoObjs[n][i]);

            w.setTextCursor(95, 90);
            w.print("GAME OVER");
            w.setTextCursor(85, 108);
            w.print("SCORE:");
            w.print(score);
            w.setTextCursor(85, 122);
            w.print("LEVEL:");
            w.print(level);
            w.setTextCursor(85, 136);
            w.print("LINES:");
            w.print(totalLines);
            w.setTextCursor(60, 154);
            w.print("PRESS ANY KEY");

            e512w3d.pushScreen();
            return;
        }

        // ライン消去フラッシュ待ち
        if (lineClearPending) {
            lineClearTimer--;
            if (lineClearTimer <= 0) collapseLines();
        } else {

        // 通常のゲームループ
        if (isKeyDown(32)) {
            while (!checkCollision(curX, curY + 1, curRot)) curY++;
            lockMino();
        } else {
            bool acted = false;

            if (isKeyDown('A')) { if (!checkCollision(curX-1, curY, curRot)) { curX--; acted = true; } dasTimerA = now + 150; }
            else if (isKey('A') && now > dasTimerA) { if (!checkCollision(curX-1, curY, curRot)) { curX--; acted = true; } dasTimerA = now + 40; }

            if (isKeyDown('D')) { if (!checkCollision(curX+1, curY, curRot)) { curX++; acted = true; } dasTimerD = now + 150; }
            else if (isKey('D') && now > dasTimerD) { if (!checkCollision(curX+1, curY, curRot)) { curX++; acted = true; } dasTimerD = now + 40; }

            if (isKeyDown('W')) {
                if (!holdUsed) {
                    if (holdType == -1) {
                        holdType = curType;
                        spawn();
                    } else {
                        int tmp = curType;
                        curType = holdType;
                        holdType = tmp;
                        curX = 4; curY = 0; curRot = 0;
                        lowestY = curY; moveCount = 0; lockDelayTimer = 0;
                        for (int i = 0; i < 4; i++) {
                            currentMinoObjs[i].color = minoColors[curType + 1];
                            ghostObjs[i].color = minoColors[curType + 1];
                        }
                    }
                    holdUsed = true;
                    for (int i = 0; i < 4; i++)
                        holdMinoObjs[i].color = minoColors[holdType + 1];
                    acted = true;
                }
            }

            bool rotL = E512W3DInput::getButtonDown(0);
            bool rotR = E512W3DInput::getButtonDown(2);

            if (rotL || rotR) {
                int dir = rotR ? 1 : -1;
                int nextRot = (curRot + dir + 4) % 4;
                if (curType == 0) {
                    if (!checkCollision(curX, curY, nextRot)) { curRot = nextRot; acted = true; }
                } else {
                    for (int test = 0; test < 5; test++) {
                        int kx = (dir==1) ? ((curType==1) ? kickTable_I_R[curRot][test][0] : kickTable_R[curRot][test][0])
                                          : ((curType==1) ? kickTable_I_L[curRot][test][0] : kickTable_L[curRot][test][0]);
                        int ky = (dir==1) ? ((curType==1) ? kickTable_I_R[curRot][test][1] : kickTable_R[curRot][test][1])
                                          : ((curType==1) ? kickTable_I_L[curRot][test][1] : kickTable_L[curRot][test][1]);
                        if (!checkCollision(curX + kx, curY + ky, nextRot)) {
                            curX += kx; curY += ky; curRot = nextRot; acted = true; break;
                        }
                    }
                }
            }

            if (acted) {
                if (curY > lowestY) { lowestY = curY; moveCount = 0; }
                if (checkCollision(curX, curY + 1, curRot)) {
                    moveCount++;
                    lockDelayTimer = now;
                }
            }

            int interval = isKey('S') ? 50 : getFallInterval();
            if (now - lastFall > interval) {
                if (!checkCollision(curX, curY + 1, curRot)) {
                    curY++;
                    lastFall = now;
                    if (curY > lowestY) { lowestY = curY; moveCount = 0; }
                }
            }

            if (checkCollision(curX, curY + 1, curRot)) {
                if (lockDelayTimer == 0) lockDelayTimer = now;
                if (now - lockDelayTimer > 500 || moveCount >= 16) lockMino();
            } else {
                lockDelayTimer = 0;
            }
        } // end isKeyDown(32) else
        } // end lineClearPending else

        // 入力を背景エフェクトへ反映
        {
            bool rotL = E512W3DInput::getButtonDown(0);
            bool rotR = E512W3DInput::getButtonDown(2);
            updateBGInput(
                isKey('A'),
                isKey('D'),
                isKey('S'),
                isKeyDown(32),
                rotL || rotR
            );
        }

        // ゴーストY計算
        int ghostY = curY;
        while (!checkCollision(curX, ghostY + 1, curRot)) ghostY++;

        if (flashEffect.count > 0) {
            w.bgcolor = (flashEffect.count % 2 == 0) ? flashEffect.color : color565(4, 10, 14);
            flashEffect.count--;
        } else {
            w.bgcolor = color565(4, 10, 14);
        }

        // カメラ揺れ減衰
        camera.position.y = cameraOffsetY;
        cameraOffsetY *= 0.8f;
        if (abs(cameraOffsetY) < 0.05f) cameraOffsetY = 0.0f;
        w.setCamera(camera);

        // フィールド描画
        // value==9 はライン消去フラッシュ中の行（点滅グレー）
        bool flashOn = (lineClearPending && (lineClearTimer % 3) < 2);
        uint16_t flashWhite = flashOn ? color565(220, 220, 255) : color565(80, 80, 80);
        for (int y = 0; y < FH; y++) {
            for (int x = 0; x < FW; x++) {
                int v = field[y][x];
                if (v == 9) {
                    fieldObjs[y][x].render_type = RenderType::PolygonColor;
                    fieldObjs[y][x].color = flashWhite;
                } else if (v) {
                    fieldObjs[y][x].render_type = RenderType::PolygonColor;
                    fieldObjs[y][x].color = minoColors[v];
                } else {
                    fieldObjs[y][x].render_type = RenderType::None;
                }
            }
        }

        // 現在ミノ・ゴースト
        for (int i = 0; i < 4; i++) {
            int x = tetrominoes[curType][curRot][i][0], y = tetrominoes[curType][curRot][i][1];
            currentMinoObjs[i].render_type = RenderType::PolygonColor;
            currentMinoObjs[i].position = Vector3((curX + x - FW/2.0f) * BLOCK_SIZE, (FH/2.0f - (curY + y)) * BLOCK_SIZE, 0);
            ghostObjs[i].render_type = RenderType::WireFrame;
            ghostObjs[i].position = Vector3((curX + x - FW/2.0f) * BLOCK_SIZE, (FH/2.0f - (ghostY + y)) * BLOCK_SIZE, 0);
        }

        // ネクスト6つを縦に並べて表示
        float nextBaseX = FW/2.0f + 2.5f;
        for (int n = 0; n < NEXT_COUNT; n++) {
            float baseY = FH/2.0f - 1.5f - n * 3.2f;
            for (int i = 0; i < 4; i++) {
                int nx = tetrominoes[nextQueue[n]][0][i][0];
                int ny = tetrominoes[nextQueue[n]][0][i][1];
                nextMinoObjs[n][i].render_type = RenderType::PolygonColor;
                nextMinoObjs[n][i].position = Vector3(
                    (nextBaseX + nx) * BLOCK_SIZE,
                    (baseY - ny) * BLOCK_SIZE,
                    0
                );
            }
        }

        // ホールド
        if (holdType != -1) {
            for (int i = 0; i < 4; i++) {
                int hx = tetrominoes[holdType][0][i][0], hy = tetrominoes[holdType][0][i][1];
                holdMinoObjs[i].position = Vector3((-FW/2.0f - 4.5f + hx) * BLOCK_SIZE, (FH/2.0f - 2.0f - hy) * BLOCK_SIZE, 0);
                holdMinoObjs[i].color = holdUsed ? color565(100, 100, 100) : minoColors[holdType + 1];
                holdMinoObjs[i].render_type = RenderType::PolygonColor;
            }
        } else {
            for (int i = 0; i < 4; i++) holdMinoObjs[i].render_type = RenderType::None;
        }

        // 描画
        e512w3d.clear();
        w.begin();
        updateBG();
        drawBG();

        // 各オブジェクトを個別描画
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FW; x++)
                w.draw(fieldObjs[y][x]);
        for (int i = 0; i < WALL_COUNT; i++)
            w.draw(wallObjs[i]);
        for (int i = 0; i < 4; i++) {
            w.draw(currentMinoObjs[i]);
            w.draw(ghostObjs[i]);
            w.draw(holdMinoObjs[i]);
        }
        for (int n = 0; n < NEXT_COUNT; n++)
            for (int i = 0; i < 4; i++)
                w.draw(nextMinoObjs[n][i]);

        w.setTextCursor(20, 30);
        w.print("HOLD");

        w.setTextCursor(275, 30);
        w.print("NEXT");

        w.setTextCursor(20, 120);
        w.print("SCORE");
        w.setTextCursor(20, 133);
        w.print(score);

        w.setTextCursor(20, 153);
        w.print("LEVEL");
        w.setTextCursor(20, 166);
        w.print(level);

        w.setTextCursor(20, 186);
        w.print("LINES");
        w.setTextCursor(20, 199);
        w.print(totalLines);

        e512w3d.pushScreen();
    }
}