// ═══════════════════════════════════════════════════════════════════════════
//  roguelike_ascii.cpp (HYPER NEON EDITION v2 - Enhanced)
//  E512W3D エンジン 用 強化版ASCIIローグライク
//
//  NEW: 13種モンスター / 4魔法 / 4アイテム種別 / ホバーツールチップ
//       トロル再生 / ヴァンパイア吸血 / シールド / 武器バフ
// ═══════════════════════════════════════════════════════════════════════════

#include "E512W3D.hpp"

E512W3DWindow w;
Object3D camera;

// ───────────────────────────────────────────
//  定数
// ───────────────────────────────────────────
#define MAP_W        40
#define MAP_H        30
#define MAX_ROOMS    10
#define MAX_ENEMIES  32
#define MAX_ITEMS    32
#define SIGHT        8
#define CELL_W       6
#define CELL_H       12
#define VIEW_COLS    78
#define VIEW_ROWS    24
#define PANEL_X      (VIEW_COLS * CELL_W + 4)
#define MSG_Y        (VIEW_ROWS * CELL_H + 2)

#define T_VOID   0
#define T_FLOOR  1
#define T_WALL   2
#define T_STAIRS 3

#define C(r,g,b)  color565(r,g,b)

// ───────────────────────────────────────────
//  モンスター種族データ（13種）
// ───────────────────────────────────────────
struct Species {
    char    g;          // ASCII文字
    uint16_t c;         // 色
    int     hp;         // 基礎HP
    int     atk;        // 攻撃力
    int     minfloor;   // 出現最低フロア
    const char* name;   // 名前（ツールチップ用）
    const char* desc;   // 説明（ツールチップ用）
};

#define NSPECIES 13
const Species sp[NSPECIES] = {
    {'r', C(200,100,100),  2,  1, 1, "Rat",     "A scurrying vermin"},
    {'s', C(138,255,138),  4,  1, 1, "Slime",   "Acidic green blob"},
    {'k', C(255,200,100),  5,  2, 1, "Kobold",  "Sneaky lizardman"},
    {'b', C(200,100,255),  5,  2, 2, "Bat",     "Bloodsucking flyer"},
    {'z', C(100,200,150),  7,  2, 2, "Zombie",  "Undead shambler"},
    {'g', C(100,255,100),  9,  3, 2, "Goblin",  "Green trickster"},
    {'O', C(200,150, 50), 13,  4, 3, "Orc",     "Brutal warrior"},
    {'W', C(150,150,255), 14,  4, 4, "Wraith",  "Soul drainer"},
    {'G', C(220,220,255), 17,  5, 4, "Golem",   "Stone guardian"},
    {'T', C(100,255,100), 20,  4, 5, "Troll",   "Regenerates HP!"},
    {'V', C(220, 50,150), 16,  6, 5, "Vampire", "Life stealer"},
    {'D', C(255,111, 84), 24,  7, 6, "Demon",   "Hellborn fiend"},
    {'X', C(255, 50, 50), 45,  9, 8, "Dragon",  "Ancient destroyer"},
};

// 種族インデックス定数
#define IDX_TROLL   9
#define IDX_VAMPIRE 10

// ───────────────────────────────────────────
//  アイテム種別（4種）
// ───────────────────────────────────────────
enum ItemType { ITEM_HEAL=0, ITEM_MANA=1, ITEM_WEAPON=2, ITEM_SHIELD=3 };

const char     ITEM_CHAR[] = {'!', '?', ')', ']'};
const uint16_t ITEM_COLS[] = {
    C(0,255,100), C(100,150,255), C(255,200,0), C(0,200,255)
};
const char* ITEM_NAMES[] = {
    "[Health Potion]", "[Mana Orb]", "[Power Blade]", "[Force Shield]"
};
const char* ITEM_DESCS[] = {
    "Restores 10 HP",
    "Resets all spell CDs",
    "ATK+3 for 8 turns",
    "Blocks next hit"
};

// ───────────────────────────────────────────
//  構造体
// ───────────────────────────────────────────
struct Enemy {
    int x, y, hp, maxhp;
    bool alive;
    char glyph;
    uint16_t col;
    int atk;
    int type;       // sp[]インデックス
    int regen_tmr;  // トロル用再生タイマー
};

struct Item {
    int x, y;
    int type;       // ItemType
    bool active;
};

#define MAX_FX 256
struct CellFX { int x, y, life, maxlife; char glyph; uint16_t fg, bg; };

#define MAX_DMG_TXT 32
struct DmgText { float px, py; char text[16]; int life, maxlife; uint16_t col; };

struct Room {
    int x, y, w, h;
    int cx(){ return x+w/2; }
    int cy(){ return y+h/2; }
};

// ───────────────────────────────────────────
//  グローバル状態
// ───────────────────────────────────────────
static uint8_t  tmap[MAP_H][MAP_W];
static uint8_t  vis [MAP_H][MAP_W];
static int   px, py, pdir=0;
static int   php=20, phmax=20;
static int   pfloor=1, plevel=1, pxp=0, pxp_next=5;
static int   patk_bonus=0, patk_timer=0;  // 武器バフ
static int   pshield=0;                   // シールド残量
static bool  pgameover=false, pinit=true;

static Enemy enemies[MAX_ENEMIES];
static int   nenm=0;
static Item  items[MAX_ITEMS];
static int   nitem=0;
static CellFX   fxs[MAX_FX];
static DmgText  dtxts[MAX_DMG_TXT];

static int   input_cd=0;
static int   spell_cd[4]={0,0,0,0};   // Z, X, C, V
static int   selected_spell=0;         // 右クリックで選択中の魔法 (0-3)
static int   cursor_blink=0;

static int screen_shake=0, g_shake_x=0, g_shake_y=0;
static int level_up_timer=0, floor_fade=0, damage_flash=0;

#define MSG_LINES 2
static char msgs[MSG_LINES][56] = {"SYSTEM ONLINE.", "HACK THE DUNGEON."};
static int  msg_head=0;

// ───────────────────────────────────────────
//  前方宣言
// ───────────────────────────────────────────
void giveXP();
void moveEnemies();
bool tryMove(int dx, int dy);
void castBeam();
void castChain();
void castFireball();
void castFrostNova();
void generateFloor();
void computeFOV();

// ───────────────────────────────────────────
//  メッセージ
// ───────────────────────────────────────────
void addMsg(const char* s) {
    msg_head = (msg_head+1) % MSG_LINES;
    int i=0;
    while(s[i] && i<55){ msgs[msg_head][i]=s[i]; ++i; }
    msgs[msg_head][i]=0;
}

// ───────────────────────────────────────────
//  VFX システム
// ───────────────────────────────────────────
const char FX_GLYPHS[] = "*!+~#%&@";

void spawnFX(int tx, int ty, uint16_t fg, uint16_t bg, int life=6, char glyph=0){
    for(int i=0; i<MAX_FX; ++i){
        if(fxs[i].life>0) continue;
        if(glyph==0) glyph=FX_GLYPHS[xrnd()%8];
        fxs[i]={tx,ty,life,life,glyph,fg,bg};
        return;
    }
}

void spawnExplosion(int tx, int ty, int radius, uint16_t fg, uint16_t bg) {
    for(int y=-radius; y<=radius; ++y)
        for(int x=-radius; x<=radius; ++x)
            if(x*x+y*y <= radius*radius)
                if(xrnd()%100 < 80)
                    spawnFX(tx+x, ty+y, fg, bg, 8+xrnd()%12);
}

void spawnText(int tx, int ty, const char* str, uint16_t col) {
    for(int i=0;i<MAX_DMG_TXT;++i){
        if(dtxts[i].life<=0){
            dtxts[i].px = tx*CELL_W + (int)(xrnd()%8 - 4);
            dtxts[i].py = ty*CELL_H + (int)(xrnd()%8 - 4);
            int k=0; while(str[k] && k<15) { dtxts[i].text[k]=str[k]; k++; }
            dtxts[i].text[k]='\0';
            dtxts[i].life = dtxts[i].maxlife = 30 + xrnd()%10;
            dtxts[i].col = col;
            return;
        }
    }
}

// ───────────────────────────────────────────
//  視野計算 (FOV)
// ───────────────────────────────────────────
void computeFOV() {
    for(int y=0;y<MAP_H;++y)
        for(int x=0;x<MAP_W;++x)
            if(vis[y][x]==2) vis[y][x]=1;

    for(int angle=0; angle<360; ++angle) {
        float rad = angle * 0.01745f;
        float rx=px, ry=py;
        float dx=cosf(rad)*0.5f, dy=sinf(rad)*0.5f;
        for(int step=0; step<SIGHT*2; ++step) {
            int tx=(int)rx, ty=(int)ry;
            if(tx<0||tx>=MAP_W||ty<0||ty>=MAP_H) break;
            vis[ty][tx]=2;
            if(tmap[ty][tx]==T_WALL) break;
            rx+=dx; ry+=dy;
        }
    }
}

// ───────────────────────────────────────────
//  ダンジョン生成
// ───────────────────────────────────────────
void generateFloor() {
    for(int y=0;y<MAP_H;++y)
        for(int x=0;x<MAP_W;++x){ tmap[y][x]=T_WALL; vis[y][x]=0; }

    Room rooms[MAX_ROOMS]; int nr=0;
    for(int att=0;att<80&&nr<MAX_ROOMS;++att){
        Room r;
        r.w=4+xrnd()%6; r.h=3+xrnd()%5;
        r.x=1+xrnd()%(MAP_W-r.w-2); r.y=1+xrnd()%(MAP_H-r.h-2);
        bool ok=true;
        for(int i=0;i<nr;++i)
            if(r.x<=rooms[i].x+rooms[i].w+1&&r.x+r.w+1>=rooms[i].x&&
               r.y<=rooms[i].y+rooms[i].h+1&&r.y+r.h+1>=rooms[i].y){ok=false;break;}
        if(!ok) continue;
        rooms[nr++]=r;
        for(int y=r.y;y<r.y+r.h;++y)
            for(int x=r.x;x<r.x+r.w;++x) tmap[y][x]=T_FLOOR;
    }
    for(int i=1;i<nr;++i){
        int ax=rooms[i-1].cx(), ay=rooms[i-1].cy();
        int bx=rooms[i].cx(),   by=rooms[i].cy();
        while(ax!=bx){tmap[ay][ax]=T_FLOOR; ax+=(ax<bx)?1:-1;}
        while(ay!=by){tmap[ay][ax]=T_FLOOR; ay+=(ay<by)?1:-1;}
    }
    px=(nr>0)?rooms[0].cx():2;
    py=(nr>0)?rooms[0].cy():2;
    if(nr>1) tmap[rooms[nr-1].cy()][rooms[nr-1].cx()]=T_STAIRS;

    // ─── モンスター配置（各部屋1〜3体）───
    nenm=0;
    for(int i=1;i<nr&&nenm<MAX_ENEMIES;++i){
        int count = 1 + xrnd()%3;
        for(int j=0; j<count&&nenm<MAX_ENEMIES; ++j){
            int pool[NSPECIES], np=0;
            for(int k=0;k<NSPECIES;++k)
                if(sp[k].minfloor<=pfloor) pool[np++]=k;
            int pick = pool[xrnd()%np];
            int hp = sp[pick].hp + (pfloor - sp[pick].minfloor) / 2;
            int ex = rooms[i].cx() + (int)(xrnd()%3) - 1;
            int ey = rooms[i].cy() + (int)(xrnd()%3) - 1;
            if(ex>=0&&ex<MAP_W&&ey>=0&&ey<MAP_H&&tmap[ey][ex]==T_FLOOR){
                enemies[nenm++] = {ex, ey, hp, hp, true,
                                   sp[pick].g, sp[pick].c, sp[pick].atk,
                                   pick, 0};
            }
        }
    }

    // ─── アイテム配置（ランダム種別）───
    nitem=0;
    for(int i=1;i<nr&&nitem<MAX_ITEMS;i+=2){
        int roll = xrnd()%10;
        int itype = (roll<5) ? ITEM_HEAL :
                    (roll<7) ? ITEM_MANA :
                    (roll<9) ? ITEM_WEAPON : ITEM_SHIELD;
        items[nitem++] = {rooms[i].x+1, rooms[i].y+1, itype, true};
    }

    for(int i=0;i<MAX_FX;++i)     fxs[i].life=0;
    for(int i=0;i<MAX_DMG_TXT;++i) dtxts[i].life=0;
    floor_fade = 255;
}

// ───────────────────────────────────────────
//  ターン消費
// ───────────────────────────────────────────
void consumeTurn(){
    for(int s=0;s<4;++s) if(spell_cd[s]>0) spell_cd[s]--;
    if(patk_timer>0){
        patk_timer--;
        if(patk_timer==0) patk_bonus=0;
    }
}

// ───────────────────────────────────────────
//  経験値・レベルアップ
// ───────────────────────────────────────────
void giveXP() {
    pxp++;
    if(pxp >= pxp_next){
        pxp=0; plevel++; pxp_next=plevel*5;
        phmax+=5; php=phmax;
        char lubuf[56];
        sprintf(lubuf, "Lv%d! Max HP+5 -> HP %d/%d", plevel, php, phmax);
        addMsg(lubuf);
        level_up_timer = 60;
        screen_shake = 15;
        spawnExplosion(px, py, 4, C(255,255,0), C(200,100,0));
    }
}

// ───────────────────────────────────────────
//  移動・バンプ攻撃
// ───────────────────────────────────────────
bool tryMove(int dx, int dy){
    int nx=px+dx, ny=py+dy;
    if(nx<0||nx>=MAP_W||ny<0||ny>=MAP_H) return false;
    if(tmap[ny][nx]==T_WALL) return false;

    spawnFX(px, py, C(50,0,0), C(100,100,0), 5, 'e');

    for(int i=0;i<nenm;++i){
        if(!enemies[i].alive||enemies[i].x!=nx||enemies[i].y!=ny) continue;
        int dmg = 1 + plevel/3 + patk_bonus;
        bool crit = (xrnd()%100 < 25);
        if(crit) dmg *= 2;

        enemies[i].hp -= dmg;
        screen_shake = crit ? 6 : 3;

        spawnExplosion(nx, ny, 1, C(255,255,0), C(100,0,0));
        char buf[16];
        sprintf(buf, "%d", dmg);
        spawnText(nx, ny, crit ? "CRIT!" : buf, crit ? C(255,100,0) : C(255,255,255));

        if(enemies[i].hp<=0){
            enemies[i].alive=false;
            spawnExplosion(nx, ny, 2, C(255,0,0), C(100,0,0));
            char kbuf[56];
            if(crit) sprintf(kbuf, "CRIT! %s hit for %d -> KILLED!", sp[enemies[i].type].name, dmg);
            else     sprintf(kbuf, "%s hit for %d -> KILLED!", sp[enemies[i].type].name, dmg);
            addMsg(kbuf);
            screen_shake = 5;
            // アイテムドロップ 25%
            if(nitem<MAX_ITEMS && xrnd()%100 < 25){
                int itype = xrnd()%4;
                items[nitem++] = {nx, ny, itype, true};
                spawnText(nx, ny, "DROP!", C(0,255,0));
            }
            giveXP();
        } else {
            char kbuf[56];
            if(crit) sprintf(kbuf, "CRIT! %s hit for %d (HP:%d left)",
                sp[enemies[i].type].name, dmg, enemies[i].hp);
            else     sprintf(kbuf, "%s hit for %d (HP:%d left)",
                sp[enemies[i].type].name, dmg, enemies[i].hp);
            addMsg(kbuf);
        }
        return true;
    }
    px=nx; py=ny;
    return true;
}

// ───────────────────────────────────────────
//  敵AI（特殊能力付き）
// ───────────────────────────────────────────
void moveEnemies(){
    int chase = min(40+(pfloor-1)*8, 85);
    for(int i=0;i<nenm;++i){
        if(!enemies[i].alive) continue;

        // ── トロル: HP再生 ──
        if(enemies[i].type == IDX_TROLL){
            enemies[i].regen_tmr++;
            if(enemies[i].regen_tmr >= 3){
                enemies[i].regen_tmr=0;
                if(enemies[i].hp < enemies[i].maxhp){
                    enemies[i].hp = min(enemies[i].hp+1, enemies[i].maxhp);
                    spawnFX(enemies[i].x, enemies[i].y, C(0,255,0), C(0,50,0), 4, '+');
                }
            }
        }

        int dx=px-enemies[i].x, dy=py-enemies[i].y;
        int mx=0, my=0;
        if((int)(xrnd()%100)<chase && !(dx==0&&dy==0)){
            if(abs(dx)>=abs(dy)) mx=(dx>0)?1:-1;
            else                 my=(dy>0)?1:-1;
        } else {
            int r=xrnd()%4;
            const int rdx[]={1,-1,0,0}, rdy[]={0,0,1,-1};
            mx=rdx[r]; my=rdy[r];
        }

        int nx2=enemies[i].x+mx, ny2=enemies[i].y+my;
        if(nx2==px && ny2==py){
            // ── シールドブロック ──
            if(pshield > 0){
                pshield--;
                spawnText(px, py, "BLOCKED!", C(0,200,255));
                char sbuf[56];
                sprintf(sbuf, "Shield blocked %s's attack!", sp[enemies[i].type].name);
                addMsg(sbuf);
                screen_shake=2;
                continue;
            }
            int dmg = enemies[i].atk;
            php -= dmg;
            screen_shake = 8;
            damage_flash = 120;
            spawnExplosion(px, py, 1, C(255,0,0), C(100,0,0));
            char buf[16]; sprintf(buf, "-%d", dmg);
            spawnText(px, py, buf, C(255,0,0));

            // ── ヴァンパイア: 吸血 ──
            if(enemies[i].type == IDX_VAMPIRE){
                int steal = max(1, dmg/2);
                enemies[i].hp = min(enemies[i].hp+steal, enemies[i].maxhp);
                spawnText(enemies[i].x, enemies[i].y, "DRAIN!", C(200,0,150));
                char vbuf[56];
                sprintf(vbuf, "Vampire drained %d HP! Your HP:%d/%d", dmg, php, phmax);
                addMsg(vbuf);
            } else {
                char abuf[56];
                sprintf(abuf, "%s hit you for %d! HP:%d/%d",
                    sp[enemies[i].type].name, dmg, php, phmax);
                addMsg(abuf);
            }

            if(php<=0){ php=0; pgameover=true; }
            continue;
        }
        bool can = (nx2>=0&&nx2<MAP_W&&ny2>=0&&ny2<MAP_H&&tmap[ny2][nx2]!=T_WALL);
        for(int j=0;j<nenm&&can;++j)
            if(j!=i&&enemies[j].alive&&enemies[j].x==nx2&&enemies[j].y==ny2) can=false;
        if(can){ enemies[i].x=nx2; enemies[i].y=ny2; }
    }
}

// ───────────────────────────────────────────
//  魔法①: ハイパービーム  [Z]  CD:3
// ───────────────────────────────────────────
void castBeam(){
    const int ddx[]={1,0,-1,0}, ddy[]={0,-1,0,1};
    int dx=ddx[pdir], dy=ddy[pdir];
    bool hit=false;
    screen_shake=10;

    for(int s=1;s<=12;++s){
        int tx=px+dx*s, ty=py+dy*s;
        if(tx<0||tx>=MAP_W||ty<0||ty>=MAP_H||tmap[ty][tx]==T_WALL){
            spawnExplosion(tx, ty, 2, C(255,255,0), C(150,50,0));
            break;
        }
        spawnFX(tx, ty, C(255,255,255), C(0,255,255), 12, '*');
        spawnFX(tx-dy, ty-dx, C(0,255,255), C(0,50,150), 8, FX_GLYPHS[xrnd()%7]);
        spawnFX(tx+dy, ty+dx, C(0,255,255), C(0,50,150), 8, FX_GLYPHS[xrnd()%7]);

        for(int i=0;i<nenm;++i){
            if(!enemies[i].alive||enemies[i].x!=tx||enemies[i].y!=ty) continue;
            int dmg = 8 + plevel;
            enemies[i].hp -= dmg;
            spawnExplosion(tx, ty, 2, C(255,255,0), C(200,50,0));
            char buf[16]; sprintf(buf, "-%d", dmg);
            spawnText(tx, ty, buf, C(0,255,255));
            hit=true;
            if(enemies[i].hp<=0){
                enemies[i].alive=false;
                spawnExplosion(tx, ty, 3, C(255,0,0), C(100,0,0));
                char mbuf[56];
                sprintf(mbuf, "Beam: %s hit for %d -> KILLED!", sp[enemies[i].type].name, dmg);
                addMsg(mbuf);
                giveXP();
            } else {
                char mbuf[56];
                sprintf(mbuf, "Beam: %s hit for %d (HP:%d left)", sp[enemies[i].type].name, dmg, enemies[i].hp);
                addMsg(mbuf);
            }
        }
    }
    if(!hit) addMsg("Beam: missed!");
}

// ───────────────────────────────────────────
//  魔法②: メガチェーン雷  [X]  CD:16
// ───────────────────────────────────────────
void castChain(){
    int first=-1; float best=999;
    for(int i=0;i<nenm;++i){
        if(!enemies[i].alive) continue;
        float d=sqrtf((float)((enemies[i].x-px)*(enemies[i].x-px)+(enemies[i].y-py)*(enemies[i].y-py)));
        if(d<(float)SIGHT && d<best){ best=d; first=i; }
    }
    if(first<0){ addMsg("NO TARGET."); return; }

    screen_shake=15;
    bool hit[MAX_ENEMIES]={};
    int cur=first;
    int chain_count=0, total_dmg=0;

    for(int c=0;c<8&&cur>=0;++c){
        hit[cur]=true;
        int dmg = 5+c*2;
        total_dmg += dmg;
        enemies[cur].hp -= dmg;
        int etx=enemies[cur].x, ety=enemies[cur].y;
        spawnExplosion(etx, ety, 2, C(255,255,255), C(0,150,255));
        char buf[16]; sprintf(buf, "-%d", dmg);
        spawnText(etx, ety, buf, C(0,255,255));
        if(enemies[cur].hp<=0){
            enemies[cur].alive=false;
            spawnExplosion(etx, ety, 3, C(255,0,0), C(100,0,0));
            giveXP();
        }
        chain_count++;
        int next=-1; float nb=999;
        for(int j=0;j<nenm;++j){
            if(!enemies[j].alive||hit[j]) continue;
            float d=sqrtf((float)((enemies[j].x-etx)*(enemies[j].x-etx)+
                                  (enemies[j].y-ety)*(enemies[j].y-ety)));
            if(d<=6 && d<nb){ nb=d; next=j; }
        }
        cur=next;
    }
    char mbuf[56];
    sprintf(mbuf, "Chain: %d targets, %d total dmg!", chain_count, total_dmg);
    addMsg(mbuf);
}

// ───────────────────────────────────────────
//  魔法③: ファイアボール AOE  [C]  CD:10
// ───────────────────────────────────────────
void castFireball(){
    const int ddx[]={1,0,-1,0}, ddy[]={0,-1,0,1};
    // 前方4マスを中心に爆発
    int cx = px + ddx[pdir]*4;
    int cy = py + ddy[pdir]*4;
    cx = max(0, min(cx, MAP_W-1));
    cy = max(0, min(cy, MAP_H-1));

    screen_shake=12;
    // 派手な火炎爆発
    spawnExplosion(cx, cy, 3, C(255,100,0), C(200,50,0));
    spawnExplosion(cx, cy, 2, C(255,255,0), C(255,50,0));
    spawnText(cx, cy, "FIREBALL!", C(255,100,0));

    int hits=0, total_dmg=0;
    for(int i=0;i<nenm;++i){
        if(!enemies[i].alive) continue;
        int ex=enemies[i].x-cx, ey=enemies[i].y-cy;
        if(ex*ex+ey*ey <= 9){
            int dmg = 6 + plevel*2;
            total_dmg += dmg;
            enemies[i].hp -= dmg;
            char buf[16]; sprintf(buf, "-%d", dmg);
            spawnText(enemies[i].x, enemies[i].y, buf, C(255,100,0));
            if(enemies[i].hp<=0){
                enemies[i].alive=false;
                spawnExplosion(enemies[i].x, enemies[i].y, 2, C(255,0,0), C(100,0,0));
                giveXP();
            }
            hits++;
        }
    }
    char mbuf[56];
    if(hits>0) sprintf(mbuf, "Fireball: %d hits, %d dmg each, %d total!", hits, 6+plevel*2, total_dmg);
    else       sprintf(mbuf, "Fireball: missed!");
    addMsg(mbuf);
}

// ───────────────────────────────────────────
//  魔法④: フロストノヴァ  [V]  CD:12
//  周囲の敵にダメージ＋吹き飛ばし
// ───────────────────────────────────────────
void castFrostNova(){
    screen_shake=8;
    spawnExplosion(px, py, 4, C(150,200,255), C(0,100,200));
    spawnText(px, py, "FROST NOVA!", C(150,200,255));

    int hits=0, total_dmg=0;
    for(int i=0;i<nenm;++i){
        if(!enemies[i].alive) continue;
        int ex=enemies[i].x-px, ey=enemies[i].y-py;
        if(ex*ex+ey*ey <= 16){
            int dmg = 4 + plevel;
            total_dmg += dmg;
            enemies[i].hp -= dmg;
            char buf[16]; sprintf(buf, "-%d", dmg);
            spawnText(enemies[i].x, enemies[i].y, buf, C(150,200,255));

            int npx2 = enemies[i].x + (ex>0?1:ex<0?-1:0);
            int npy2 = enemies[i].y + (ey>0?1:ey<0?-1:0);
            if(npx2>=0&&npx2<MAP_W&&npy2>=0&&npy2<MAP_H&&tmap[npy2][npx2]!=T_WALL){
                enemies[i].x=npx2; enemies[i].y=npy2;
            }
            if(enemies[i].hp<=0){
                enemies[i].alive=false;
                spawnExplosion(enemies[i].x, enemies[i].y, 2, C(200,200,255), C(0,50,150));
                giveXP();
            }
            hits++;
        }
    }
    char mbuf[56];
    if(hits>0) sprintf(mbuf, "Frost Nova: %d frozen, %d dmg+knockback!", hits, 4+plevel);
    else       sprintf(mbuf, "Frost Nova: missed!");
    addMsg(mbuf);
}

// ───────────────────────────────────────────
//  描画: グリフ
// ───────────────────────────────────────────
void drawGlyph(int sx, int sy, char c, uint16_t fg, uint16_t bg=0){
    w.drawRect(sx, sy, sx+CELL_W, sy+CELL_H, bg);
    w.text_color = fg;
    w.drawChar((uint8_t)c, sx, sy);
}

// ───────────────────────────────────────────
//  描画: VFX
// ───────────────────────────────────────────
void drawAndUpdateFX(int offx, int offy){
    for(int i=0;i<MAX_FX;++i){
        if(fxs[i].life<=0) continue;
        fxs[i].life--;
        int lx=fxs[i].x-offx, ly=fxs[i].y-offy;
        if(lx<0||lx>=VIEW_COLS||ly<0||ly>=VIEW_ROWS) continue;
        float t=(float)fxs[i].life/fxs[i].maxlife;
        uint16_t bg=fxs[i].bg;
        uint16_t r=((uint16_t)((bg>>11&0x1F)*t))&0x1F;
        uint16_t g=((uint16_t)((bg>>5 &0x3F)*t))&0x3F;
        uint16_t b=((uint16_t)((bg    &0x1F)*t))&0x1F;
        drawGlyph(lx*CELL_W+g_shake_x, ly*CELL_H+g_shake_y,
                  fxs[i].glyph, fxs[i].fg, (r<<11)|(g<<5)|b);
    }
}

// ───────────────────────────────────────────
//  描画: ダメージテキスト（浮上）
// ───────────────────────────────────────────
void drawDmgTexts(int offx, int offy){
    for(int i=0;i<MAX_DMG_TXT;++i){
        if(dtxts[i].life<=0) continue;
        dtxts[i].life--;
        dtxts[i].py -= 0.8f;
        int lx=(int)dtxts[i].px - offx*CELL_W + g_shake_x;
        int ly=(int)dtxts[i].py - offy*CELL_H + g_shake_y;
        float prog = 1.0f-(float)dtxts[i].life/dtxts[i].maxlife;
        ly -= (int)(sinf(prog*3.14f)*10.0f);
        if(lx>0&&lx<VIEW_COLS*CELL_W&&ly>0&&ly<VIEW_ROWS*CELL_H){
            w.setTextCursor(lx, ly);
            w.text_color = dtxts[i].col;
            // 読みやすい黒背景
            int tw = strlen(dtxts[i].text)*CELL_W;
            w.drawRect(lx-1, ly-1, lx+tw+1, ly+CELL_H+1, C(0,0,0));
            w.print(dtxts[i].text);
        }
    }
}

// ───────────────────────────────────────────
//  描画: ホバーツールチップ（視野内のみ）
// ───────────────────────────────────────────
void drawTooltip(int offx, int offy){
    int mcx = cursor_x / CELL_W + offx;
    int mcy = cursor_y / CELL_H + offy;
    if(mcx<0||mcx>=MAP_W||mcy<0||mcy>=MAP_H) return;
    if(vis[mcy][mcx] != 2) return;  // 視野外は表示しない

    char line1[36]="", line2[36]="";
    uint16_t ttcol = C(255,255,255);
    bool found = false;

    // ── 敵チェック ──
    for(int i=0;i<nenm&&!found;++i){
        if(!enemies[i].alive||enemies[i].x!=mcx||enemies[i].y!=mcy) continue;
        sprintf(line1, "[%s]", sp[enemies[i].type].name);
        sprintf(line2, "HP:%d/%d ATK:%d  %s",
                enemies[i].hp, enemies[i].maxhp,
                enemies[i].atk, sp[enemies[i].type].desc);
        ttcol = enemies[i].col;
        found = true;
    }

    // ── アイテムチェック ──
    for(int i=0;i<nitem&&!found;++i){
        if(!items[i].active||items[i].x!=mcx||items[i].y!=mcy) continue;
        strncpy(line1, ITEM_NAMES[items[i].type], 35);
        strncpy(line2, ITEM_DESCS[items[i].type], 35);
        ttcol = ITEM_COLS[items[i].type];
        found = true;
    }

    // ── 階段チェック ──
    if(!found && tmap[mcy][mcx]==T_STAIRS){
        strcpy(line1, "[Stairs Down  >]");
        strcpy(line2, "Descend to next floor");
        ttcol = C(255,255,0);
        found = true;
    }

    // ── プレイヤーチェック ──
    if(!found && mcx==px && mcy==py){
        strcpy(line1, "[You - Hacker]");
        sprintf(line2, "Lv:%d HP:%d/%d ATK:%d",
                plevel, php, phmax, 1+plevel/3+patk_bonus);
        ttcol = C(255,200,255);
        found = true;
    }

    if(!found) return;

    // ── ツールチップ描画 ──
    int len1=strlen(line1), len2=strlen(line2);
    int maxlen = (len1>len2) ? len1 : len2;
    int tw = maxlen*CELL_W + 10;
    int th = CELL_H*2 + 10;
    int tx = cursor_x + 10;
    int ty = cursor_y - th - 4;

    // 画面端クランプ
    if(tx + tw > VIEW_COLS*CELL_W - 2) tx = cursor_x - tw - 6;
    if(ty < 0)                          ty = cursor_y + 14;
    if(ty + th > VIEW_ROWS*CELL_H - 2) ty = VIEW_ROWS*CELL_H - th - 2;

    // 影
    w.drawRect(tx+2, ty+2, tx+tw+2, ty+th+2, C(0,0,0));
    // 背景
    w.drawRect(tx, ty, tx+tw, ty+th, C(5,5,25));
    // 枠線
    w.drawLine(tx,    ty,    tx+tw, ty,    ttcol);
    w.drawLine(tx,    ty+th, tx+tw, ty+th, ttcol);
    w.drawLine(tx,    ty,    tx,    ty+th, ttcol);
    w.drawLine(tx+tw, ty,    tx+tw, ty+th, ttcol);
    // 内側ハイライト（上辺）
    w.drawLine(tx+1, ty+1, tx+tw-1, ty+1, C(
        ((ttcol>>11)&0x1F)<<3,
        ((ttcol>>5)&0x3F)<<2,
        (ttcol&0x1F)<<3));

    // テキスト
    w.setTextCursor(tx+4, ty+3);
    w.text_color = ttcol;
    w.print(line1);
    w.setTextCursor(tx+4, ty+3+CELL_H);
    w.text_color = C(180,180,180);
    w.print(line2);
}

// ───────────────────────────────────────────
//  描画: マップ＋エンティティ
// ───────────────────────────────────────────
void drawMap(){
    int offx = px - VIEW_COLS/2;
    int offy = py - VIEW_ROWS/2;
    offx = max(0, min(offx, MAP_W-VIEW_COLS));
    offy = max(0, min(offy, MAP_H-VIEW_ROWS));

    w.drawRect(0, 0, VIEW_COLS*CELL_W, VIEW_ROWS*CELL_H, C(0,0,0));

    float phase = (float)(millis()%2000) / 2000.0f * 6.2832f;

    // ── タイル描画 ──
    for(int my=0;my<VIEW_ROWS;++my){
        for(int mx=0;mx<VIEW_COLS;++mx){
            int wx=mx+offx, wy=my+offy;
            if(wx<0||wx>=MAP_W||wy<0||wy>=MAP_H) continue;
            int v=vis[wy][wx];
            if(v==0) continue;

            int draw_x = mx*CELL_W + g_shake_x;
            int draw_y = my*CELL_H + g_shake_y;
            bool inSight = (v==2);
            uint8_t t = tmap[wy][wx];
            char gc; uint16_t fg, bg;

            float hx = (float)wx / (MAP_W-1);
            float vy = (float)wy / (MAP_H-1);
            float wave = sinf(phase + hx*5.0f + vy*5.0f) * 0.5f + 0.5f;

            if(t==T_FLOOR){
                gc='.';
                int fgR = inSight ? (int)(150 + wave*105) : 40;
                int fgG = inSight ? (int)(50 + (1.0f-wave)*200) : 20;
                int fgB = inSight ? 255 : 80;
                fg = color565(fgR, fgG, fgB);
                bg = color565(fgR/4, fgG/4, fgB/4);
            } else if(t==T_WALL){
                gc='#';
                int fgR = inSight ? 255 : 80;
                int fgG = inSight ? (int)(100 + wave*155) : 30;
                int fgB = inSight ? 50 : 10;
                fg = color565(fgR, fgG, fgB);
                bg = color565(fgR/4, fgG/4, fgB/4);
            } else if(t==T_STAIRS){
                gc='>';
                fg = inSight ? C(255,255,0) : C(100,100,0);
                bg = inSight ? C(50,50,0)   : C(20,20,0);
            } else continue;

            drawGlyph(draw_x, draw_y, gc, fg, bg);
        }
    }

    // ── アイテム（種別ごとに異なる文字・色）──
    for(int i=0;i<nitem;++i){
        if(!items[i].active) continue;
        int lx=items[i].x-offx, ly=items[i].y-offy;
        if(lx<0||lx>=VIEW_COLS||ly<0||ly>=VIEW_ROWS) continue;
        if(vis[items[i].y][items[i].x]!=2) continue;
        // 点滅エフェクト
        bool blink2 = (millis()/300)%2==0;
        uint16_t icol = blink2 ? ITEM_COLS[items[i].type] :
            color565(
                ((ITEM_COLS[items[i].type]>>11)&0x1F)<<2,
                ((ITEM_COLS[items[i].type]>>5)&0x3F)<<1,
                (ITEM_COLS[items[i].type]&0x1F)<<2);
        drawGlyph(lx*CELL_W+g_shake_x, ly*CELL_H+g_shake_y,
                  ITEM_CHAR[items[i].type], icol, C(0,20,10));
    }

    // ── 敵（HP比で色が変化）──
    for(int i=0;i<nenm;++i){
        if(!enemies[i].alive) continue;
        if(vis[enemies[i].y][enemies[i].x]!=2) continue;
        int lx=enemies[i].x-offx, ly=enemies[i].y-offy;
        if(lx<0||lx>=VIEW_COLS||ly<0||ly>=VIEW_ROWS) continue;
        float hf = 0.5f + 0.5f*(float)enemies[i].hp/enemies[i].maxhp;
        uint16_t c=enemies[i].col;
        uint16_t r2=((uint16_t)((((c>>11)&0x1F)*hf)))&0x1F;
        uint16_t g2=((uint16_t)((((c>> 5)&0x3F)*hf)))&0x3F;
        uint16_t b2=((uint16_t)(((c      &0x1F)*hf)))&0x1F;
        drawGlyph(lx*CELL_W+g_shake_x, ly*CELL_H+g_shake_y,
                  enemies[i].glyph, (r2<<11)|(g2<<5)|b2, C(40,20,20));
    }

    // ── プレイヤー ──
    {
        int lx=px-offx, ly=py-offy;
        bool blink=(millis()/150)%2==0;
        uint16_t pcol;
        if(pshield>0)      pcol = blink ? C(0,255,255) : C(0,100,200);
        else if(patk_timer>0) pcol = blink ? C(255,200,0) : C(255,100,0);
        else               pcol = blink ? C(255,255,255) : C(255,50,150);
        drawGlyph(lx*CELL_W+g_shake_x, ly*CELL_H+g_shake_y, 'e', pcol, C(50,0,50));
    }

    drawAndUpdateFX(offx, offy);
    drawDmgTexts(offx, offy);

    // ツールチップは最前面（揺れなし）
    drawTooltip(offx, offy);

    // カーソル十字線（揺れなし）
    int clx=cursor_x/CELL_W, cly=cursor_y/CELL_H;
    if(clx>=0&&clx<VIEW_COLS&&cly>=0&&cly<VIEW_ROWS){
        uint16_t ccol=(cursor_blink/4)%2==0 ? C(0,255,255) : C(0,150,150);
        w.drawLine(cursor_x-3, cursor_y,   cursor_x+3, cursor_y,   ccol);
        w.drawLine(cursor_x,   cursor_y-3, cursor_x,   cursor_y+3, ccol);
    }
}

// ───────────────────────────────────────────
//  描画: サイドパネル
// ───────────────────────────────────────────
void drawPanel(){
    // CW=6, CH=12 グリッドに完全整列
    const int CW = CELL_W;   // 6
    const int CH = CELL_H;   // 12
    const int PX = PANEL_X;  // パネル左端X

    // ラベル列・値列の定義（CW倍数）
    const int COL_LBL = PX;          // ラベル開始
    const int COL_VAL = PX + 5*CW;   // 値列 (5文字=30px)
    const int COL_RDY = PX + 11*CW;  // RDY列 (11文字=66px)

    w.drawRect(PX, 0, w.width, w.height, C(0,0,0));
    // 境界線（CW整列）
    w.drawRect(PX-2, 0, PX-1, VIEW_ROWS*CH, C(255,0,255));

    int y = 0;

    // ── ヘッダー ──
    w.setTextCursor(COL_LBL, y); w.text_color=C(0,255,255); w.print("NEON CORE"); y+=CH;

    // Floor
    w.setTextCursor(COL_LBL, y); w.text_color=C(255,100,255); w.print("Floor");
    w.setTextCursor(COL_VAL, y); w.text_color=C(255,255,255); w.print(pfloor); y+=CH;

    y+=CH; // 空行1行

    // ── PLAYER ──
    w.setTextCursor(COL_LBL, y); w.text_color=C(0,255,100); w.print("PLAYER"); y+=CH;

    // HP バー（幅 = 16*CW = 96px, 高さ = 8px → y+8でバー, +4余白）
    {
        const int BW = 16*CW; // 96px
        const int BH = 8;
        w.drawRect(COL_LBL, y, COL_LBL+BW, y+BH, C(50,0,0));
        int filled = (int)((float)php/phmax * BW);
        uint16_t hcol = php>phmax/2 ? C(0,255,100) :
                        php>phmax/4 ? C(255,200,0)  : C(255,0,0);
        if(php<=phmax/4 && (millis()/100)%2==0) hcol=C(255,255,255);
        if(filled>0) w.drawRect(COL_LBL, y, COL_LBL+filled, y+BH, hcol);
        y += BH + 4; // 8+4=12 → 次のテキスト行と揃う
    }

    // HP / Lv / XP
    w.setTextCursor(COL_LBL, y); w.text_color=C(200,200,200); w.print("HP");
    w.setTextCursor(COL_VAL, y); w.text_color=C(255,255,255);
    w.print(php); w.print("/"); w.print(phmax); y+=CH;

    w.setTextCursor(COL_LBL, y); w.text_color=C(200,200,200); w.print("Lv");
    w.setTextCursor(COL_VAL, y); w.text_color=C(255,255,0); w.print(plevel); y+=CH;

    w.setTextCursor(COL_LBL, y); w.text_color=C(200,200,200); w.print("XP");
    w.setTextCursor(COL_VAL, y); w.text_color=C(0,255,255);
    w.print(pxp); w.print("/"); w.print(pxp_next); y+=CH;

    y+=CH; // 空行

    // ── アクティブバフ ──
    if(patk_bonus>0){
        w.setTextCursor(COL_LBL, y); w.text_color=C(255,200,0);
        w.print(")ATK+"); w.print(patk_bonus);
        w.setTextCursor(COL_RDY, y); w.text_color=C(200,150,0);
        w.print(patk_timer); w.print("t"); y+=CH;
    }
    if(pshield>0){
        w.setTextCursor(COL_LBL, y); w.text_color=C(0,200,255);
        w.print("]SHIELD");
        w.setTextCursor(COL_RDY, y); w.text_color=C(0,200,255);
        w.print(pshield); w.print("x"); y+=CH;
    }

    y+=CH; // 空行

    // ── MAP KEY ──
    w.setTextCursor(COL_LBL, y); w.text_color=C(120,120,120); w.print("MAP KEY"); y+=CH;
    w.setTextCursor(COL_LBL, y); w.text_color=C(80,80,80);
    w.print("e=You  >=Stairs"); y+=CH;
    w.setTextCursor(COL_LBL, y); w.text_color=C(0,180,80);
    w.print("!=HP  ?=MP  )=Atk"); y+=CH;
    w.setTextCursor(COL_LBL, y); w.text_color=C(0,180,80);
    w.print("]=Shld"); y+=CH;
    w.setTextCursor(COL_LBL, y); w.text_color=C(150,150,150);
    w.print("WASD:move"); y+=CH;
    w.setTextCursor(COL_LBL, y); w.text_color=C(100,100,200);
    w.print("Z/X/C/V"); y+=CH;
    w.setTextCursor(COL_LBL, y);
    w.print("RClk:sel"); y+=CH;
    w.setTextCursor(COL_LBL, y);
    w.print("LClk:use"); y+=CH;

    y+=CH; // 空行

    // ── SPELLS ──
    w.setTextCursor(COL_LBL, y); w.text_color=C(255,100,255); w.print("SPELLS"); y+=CH;

    // pspell: arrow(1CW) key(2CW) name(7CW) → RDY at COL_RDY
    const char* skeys[] = {"Z","X","C","V"};
    const char* snames[]= {"Beam   ","Chain  ","Fireball","Frost  "};
    const uint16_t scols[]={C(255,255,0),C(0,255,255),C(255,100,0),C(150,200,255)};

    for(int s=0;s<4;++s){
        bool sel=(s==selected_spell);
        if(sel){
            w.drawRect(COL_LBL, y, COL_LBL+16*CW, y+CH, C(30,0,60));
            w.drawLine(COL_LBL, y,    COL_LBL+16*CW, y,    scols[s]);
            w.drawLine(COL_LBL, y+CH-1, COL_LBL+16*CW, y+CH-1, scols[s]);
        }
        // arrow  (col 0, 1文字)
        w.setTextCursor(COL_LBL, y);
        w.text_color = sel ? scols[s] : C(40,40,40);
        w.print(sel ? ">" : " ");
        // key    (col 1, 1文字)
        w.setTextCursor(COL_LBL+1*CW, y);
        w.text_color = spell_cd[s]==0 ? scols[s] : C(80,80,80);
        w.print(skeys[s]);
        // name   (col 3〜, 7文字)
        w.setTextCursor(COL_LBL+3*CW, y);
        w.print(snames[s]);
        // RDY/cd (col 11)
        w.setTextCursor(COL_RDY, y);
        w.text_color = spell_cd[s]==0 ? C(0,255,0) : C(255,80,80);
        if(spell_cd[s]>0){ w.print(spell_cd[s]); w.print("t"); }
        else w.print("RDY");
        y+=CH;
    }
}

// ───────────────────────────────────────────
//  描画: メッセージログ
// ───────────────────────────────────────────
void drawMessages(){
    w.drawRect(0, MSG_Y, VIEW_COLS*CELL_W, w.height, C(20,0,20));
    for(int i=0;i<MSG_LINES;++i){
        int idx=(msg_head-MSG_LINES+1+i+MSG_LINES)%MSG_LINES;
        float alpha=(i==MSG_LINES-1)?1.0f:0.5f;
        w.setTextCursor(2, MSG_Y+2+i*(CELL_H-4));
        w.text_color=C((int)(255*alpha),(int)(200*alpha),(int)(255*alpha));
        w.print(msgs[idx]);
    }
}

// ───────────────────────────────────────────
//  メインループ
// ───────────────────────────────────────────
void game(){
    // ─ 初期化 ─
    if(pinit){
        php=phmax=20; pfloor=1; plevel=1; pxp=0; pxp_next=5;
        patk_bonus=0; patk_timer=0; pshield=0;
        pgameover=false; input_cd=0;
        for(int s=0;s<4;++s) spell_cd[s]=0;
        selected_spell=0;
        pdir=0; screen_shake=0; level_up_timer=0; floor_fade=255; damage_flash=0;
        for(int i=0;i<MAX_DMG_TXT;++i) dtxts[i].life=0;
        generateFloor(); computeFOV(); pinit=false;
    }

    // ─ ゲームオーバー画面 ─
    if(pgameover){
        w.drawRect(0,0,w.width,w.height,C(0,0,0));
        w.setTextCursor(80,60); w.text_color=C(255,0,0); w.setTextSize(2);
        w.print("SYSTEM FAILURE");
        w.setTextSize(1);
        w.setTextCursor(80,110); w.text_color=C(255,255,255);
        w.print("Floor:"); w.print(pfloor);
        w.print("  Lv:"); w.print(plevel);
        w.setTextCursor(80,130); w.text_color=C(0,255,255);
        w.print("Press R to REBOOT");
        if(E512W3DInput::getKey('R')){ pinit=true; w.setTextSize(1); }
        return;
    }

    // ─ 画面揺れ ─
    g_shake_x = screen_shake>0 ? (int)(xrnd()%(screen_shake*2+1))-screen_shake : 0;
    g_shake_y = screen_shake>0 ? (int)(xrnd()%(screen_shake*2+1))-screen_shake : 0;
    if(screen_shake>0) screen_shake--;
    if(input_cd>0) input_cd--;

    // ─ 移動入力 ─
    int ddx=0, ddy=0;
    if     (E512W3DInput::getKey('W')){ ddy=-1; pdir=1; }
    else if(E512W3DInput::getKey('S')){ ddy= 1; pdir=3; }
    else if(E512W3DInput::getKey('A')){ ddx=-1; pdir=2; }
    else if(E512W3DInput::getKey('D')){ ddx= 1; pdir=0; }

    if((ddx||ddy) && input_cd==0){
        input_cd=4;
        if(tryMove(ddx, ddy)){
            // ─ アイテム拾得 ─
            for(int i=0;i<nitem;++i){
                if(!items[i].active||items[i].x!=px||items[i].y!=py) continue;
                items[i].active=false;
                switch(items[i].type){
                    case ITEM_HEAL: {
                        int before=php;
                        php = min(php+10, phmax);
                        spawnExplosion(px,py,2,C(0,255,0),C(0,100,0));
                        spawnText(px,py,"HEAL +10",C(0,255,0));
                        char hbuf[56];
                        sprintf(hbuf, "Health Potion: HP %d -> %d (+%d)", before, php, php-before);
                        addMsg(hbuf);
                        break;
                    }
                    case ITEM_MANA:
                        for(int s=0;s<4;++s) spell_cd[s]=0;
                        spawnExplosion(px,py,2,C(100,150,255),C(0,50,200));
                        spawnText(px,py,"MANA ORB!",C(100,150,255));
                        addMsg("Mana Orb: all spell CDs reset!");
                        break;
                    case ITEM_WEAPON:
                        patk_bonus=3; patk_timer=8;
                        spawnExplosion(px,py,2,C(255,200,0),C(100,50,0));
                        spawnText(px,py,"ATK+3!",C(255,200,0));
                        addMsg("Power Blade: ATK+3 for 8 turns!");
                        break;
                    case ITEM_SHIELD:
                        pshield++;
                        spawnExplosion(px,py,2,C(0,200,255),C(0,50,100));
                        spawnText(px,py,"SHIELD UP!",C(0,200,255));
                        addMsg("Force Shield: next hit will be blocked!");
                        break;
                }
                screen_shake=3;
            }
            moveEnemies();
            consumeTurn();
        }
        // 階段
        if(tmap[py][px]==T_STAIRS){
            pfloor++;
            php=min(php+4, phmax);
            char fbuf[56];
            sprintf(fbuf, "Descended to B%d. HP+4 -> %d/%d", pfloor, php, phmax);
            addMsg(fbuf);
            generateFloor();
        }
        computeFOV();
    }

    // ─ 右クリック: 次の魔法を選択（サイクル）─
    if(E512W3DInput::getButtonDown(2)){
        selected_spell = (selected_spell + 1) % 4;
        const char* names[4]={"BEAM","CHAIN","FIREBALL","FROST NOVA"};
        char mbuf[32]; sprintf(mbuf,"SPELL: %s", names[selected_spell]);
        addMsg(mbuf);
    }

    // ─ 左クリック: 選択魔法を発動 ─
    if(E512W3DInput::getButtonDown(0)){
        if(spell_cd[selected_spell] == 0){
            switch(selected_spell){
                case 0: spell_cd[0]=3;  castBeam();      break;
                case 1: spell_cd[1]=16; castChain();     break;
                case 2: spell_cd[2]=10; castFireball();  break;
                case 3: spell_cd[3]=12; castFrostNova(); break;
            }
            moveEnemies(); consumeTurn(); computeFOV();
        } else {
            const char* names[4]={"BEAM","CHAIN","FIREBALL","FROST NOVA"};
            char mbuf[40]; sprintf(mbuf,"%s NOT READY! (%dt)",
                names[selected_spell], spell_cd[selected_spell]);
            addMsg(mbuf);
        }
    }

    // ─ 魔法キー入力（4種） ─
    if(E512W3DInput::getKey('Z') && spell_cd[0]==0){
        spell_cd[0]=3;  castBeam();      moveEnemies(); consumeTurn(); computeFOV();
    }
    if(E512W3DInput::getKey('X') && spell_cd[1]==0){
        spell_cd[1]=16; castChain();     moveEnemies(); consumeTurn(); computeFOV();
    }
    if(E512W3DInput::getKey('C') && spell_cd[2]==0){
        spell_cd[2]=10; castFireball();  moveEnemies(); consumeTurn(); computeFOV();
    }
    if(E512W3DInput::getKey('V') && spell_cd[3]==0){
        spell_cd[3]=12; castFrostNova(); moveEnemies(); consumeTurn(); computeFOV();
    }

    cursor_blink++;
    drawMap();
    drawPanel();
    drawMessages();

    // ─ ダメージフラッシュ（画面周囲） ─
    if(damage_flash>0){
        uint16_t dc=color565(damage_flash,0,0);
        w.drawRect(0,0,w.width,8,dc);
        w.drawRect(0,w.height-8,w.width,w.height,dc);
        w.drawRect(0,0,8,w.height,dc);
        w.drawRect(w.width-8,0,w.width,w.height,dc);
        damage_flash-=10;
    }

    // ─ フロアフェードイン（シンプルな暗転） ─
    if(floor_fade>0){
        uint16_t fc = color565(floor_fade/4, floor_fade/4, floor_fade/4);
        w.drawRect(0, 0, w.width, w.height, fc);
        floor_fade -= 20;
    }

    // ─ レベルアップ演出 ─
    if(level_up_timer>0){
        level_up_timer--;
        w.setTextSize(3);
        int cx=(VIEW_COLS*CELL_W)/2-80;
        int cy=(VIEW_ROWS*CELL_H)/2-20;
        w.setTextCursor(cx+(int)(xrnd()%9-4), cy+(int)(xrnd()%9-4));
        w.text_color=color565(xrnd()%255, xrnd()%255, xrnd()%255);
        w.print("LEVEL UP!");
        w.setTextSize(1);
    }
}

// ───────────────────────────────────────────
//  Arduino エントリポイント
// ───────────────────────────────────────────
void setup(){
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Axp.ScreenBreath(12);
    M5.IMU.Init();

    e512w3d.width=160*4; e512w3d.height=80*4;
    w.width=e512w3d.width; w.height=e512w3d.height;
    w.setCamera(camera);
    e512w3d.add(w);
    e512w3d.begin();
    w.setTextSize(1);
    w.bgcolor=C(0,0,0);
}

void loop(){
    if(e512w3d.isFixedTime()){
        E512W3DInput::update();
        e512w3d.clear();
        w.begin();
        game();
        e512w3d.pushScreen();
    }
}