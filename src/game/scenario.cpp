#include "scenario.h"
#include "../audio/sound.h"

#include <cstring>
#include <cstdio>

#include "building_sheet.h"
#include "colony.h"
#include "game_map.h"
#include "garage.h"
#include "player.h"
#include "structure.h"
#include "salvage.h"
#include "tethys_game.h"
#include "terrain_cost.h"
#include "unit.h"
#include "walls.h"
#include "weapon.h"
#include "mining.h"
#include "vehicle_anim.h"
#include "../gfx/artfile.h"
#include "../game/docking.h"
#include "../mission/mission.h"

namespace op2 {

// sub_4027D0: tipo de missão multiplayer (−8..−4)?
bool multiplayerMission() {
    MissionScript* m = currentMission();
    return m && m->desc && m->desc->missionType <= -4 && m->desc->missionType >= -8;
}

static int widthMask() { return g_gameMap.width() - 1; }
// O original guarda mapas estreitos deslocados (dword_54EBF0 = x da área
// jogável); nas coordenadas do port a área vai de 0 a W-1 e só esses mapas
// prendem x em vez de dar a volta.
static bool clampsX() { return g_gameMap.internalXOffset() > 0; }

static bool cellFree(int x, int y, int moveClass) {
    const uint32_t c = g_gameMap.rawCell(x & widthMask(), y);
    if (c & 0xAFFF0000u) return false;
    return terrainMoveCost(static_cast<int>(c & 0x1F), moveClass) != 0;
}

void findVehicleSpot(int type, int& x, int& y) {             // sub_4366D0
    const int mc = mapIdIsVehicle(type) ? vehicleTrackClass(type) : 0;
    const int maxY = g_gameMap.height() - 1, W = g_gameMap.width();
    if (y >= maxY) y = maxY;
    if (y <= 0) y = 0;
    int dir = 0, run = 1, left = 1;
    for (int guard = 0; guard < 1 << 20; ++guard) {
        if (cellFree(x, y, mc)) break;
        switch (dir) {
            case 0: ++x; if (clampsX() && x > W - 1) x = W - 1; break;
            case 1: y = y - 1 > 0 ? y - 1 : 0; break;
            case 2: --x; if (clampsX() && x < 0) x = 0; break;
            case 3: y = y + 1 < maxY ? y + 1 : maxY; break;
        }
        if (--left == 0) {
            dir = (dir + 1) & 3;
            if (!(dir & 1)) ++run;
            left = run;
        }
    }
    x &= widthMask();
}

void findBuildingSpot(int& x, int& y, int w2, int h2) {    // sub_436820
    const int mc = vehicleTrackClass(mapConVec);             // off_4E1348[+0x1E8]
    const int H = g_gameMap.height(), mask = widthMask();
    const int halfH = h2 / 2, halfW = w2 / 2;
    const int maxY = H - h2 + halfH - 1;
    if (y <= 1) y = 1;
    if (y >= maxY) y = maxY;
    int dir = 0, run = 1, left = 1;
    for (int guard = 0; guard < 1 << 20; ++guard) {
        const int x0 = (x - halfW) & mask;
        const int xEnd = (w2 + x0 - 1) & mask;
        const int y0 = y - halfH, yEnd = h2 + y0 - 1;
        bool ok = true;
        // Colunas x0 .. xEnd-1 (a última não é testada) e linhas y0 .. yEnd.
        for (int yy = y0; ok && yy <= yEnd; ++yy)
            for (int xx = x0; xx != xEnd; xx = (xx + 1) & mask)
                if (!cellFree(xx, yy, mc)) { ok = false; break; }
        if (ok) return;
        switch (dir) {
            case 0: ++x; break;
            case 1: y = y - 1 > 1 ? y - 1 : 1; break;
            case 2: --x; break;
            case 3: y = y + 1 < maxY ? y + 1 : maxY; break;
        }
        if (--left == 0) {
            dir = (dir + 1) & 3;
            if (!(dir & 1)) ++run;
            left = run;
        }
    }
}


bool beaconSurveyed(int id, int player) {                    // sub_4054E0
    const UnitRecord& b = g_units[id];
    if (b.surveyedMask & g_playerData[player].allyMask) return true;
    if (TethysGame::cheatFlag(1)) return true;
    for (size_t i = 1; i < g_units.size(); ++i)             // sub_490800(0x58): EDWARD Satellite
        if (g_units[i].id != -1 && g_units[i].type == 0x58 && (g_units[i].owner & 0x0F) == player) return true;
    return false;
}

int placeBuilding(int type, int x, int y, int owner) {      // sub_4369E0
    const StructSheetDesc* d = structSheet(type);
    if (!d) return 0;
    const int mask = widthMask(), H = g_gameMap.height();
    const int w = d->sizeX, h = d->sizeY;
    const int x1 = (x - w / 2) & mask, x2 = (w + x1 - 1) & mask;
    const int y1 = y - h / 2, y2 = h + y1 - 1;
    int beacon = 0;
    if (type == 0x15) {                                      // mina: procura o beacon embaixo
        for (size_t i = 1; i < g_units.size() && !beacon; ++i) {
            const UnitRecord& b = g_units[i];
            if (b.id == -1 || (b.type != 0x51 && b.type != 0x52)) continue;
            const int bx = unitOX(b) >> 5, by = unitOY(b) >> 5;
            if (by < y1 || by > y2) continue;
            const bool in = x2 > x1 ? (bx >= x1 && bx <= x2) : (bx <= x2 || bx >= x1);
            if (!in) continue;
            if (b.type == 0x51) { type = b.beaconRare ? 0x16 : 0x15; beacon = static_cast<int>(i); }
            else { type = 0x2A; beacon = static_cast<int>(i); }
        }
        if (!beacon) return 0;
    }
    Unit u;
    if (!createUnit(u, type, LOCATION{x & mask, y}, owner)) return 0;
    const int id = u.id();
    // Anel em volta: terraplana; footprint: cellType 0x1A (bloqueado, menos a doca).
    const bool eden = owner >= 0 && owner < kMaxPlayers && g_playerData[owner].isEden;
    const int dock = eden ? d->dockLocE : d->dockLocP;
    int col = -1;
    for (int xx = (x1 - 1) & mask;; xx = (xx + 1) & mask, ++col) {
        for (int yy = y1 - 1; yy <= y2 + 1; ++yy) {
            if (yy < 0 || yy >= H) continue;
            uint32_t c = g_gameMap.rawCell(xx, yy);
            const int tile = static_cast<int>((c & 0xFFE0) >> 5);
            const bool ring = yy == y1 - 1 || yy == y2 + 1 || xx == ((x1 - 1) & mask) || xx == ((x2 + 1) & mask);
            if (ring) {
                if ((c & 0x1F) == 0x1A) continue;
                c = (c & 0xFFFF0000u) | ((static_cast<uint32_t>(g_gameMap.bulldozedTile(tile)) << 5) & 0xFFE0u) | 0x15u;
                g_gameMap.setRawCell(xx, yy, c);
                continue;
            }
            const bool dockCell = yy == y2 && dock != 0 && dock - col == 1;
            if (!dockCell) c |= 0x80000000u;
            c = (c & 0xFFFF0000u) | ((static_cast<uint32_t>(g_gameMap.bulldozedTile(tile)) << 5) & 0xFFE0u) | 0x1Au;
            g_gameMap.setRawCell(xx, yy, c);
            autotileWallTube(xx, yy, 0x11);
        }
        if (xx == ((x2 + 1) & mask)) break;
    }
    if (d->flags24C & 1) {                                  // conectores de tubo
        const int cx = (x1 + w / 2) & mask, cy = y1 + h / 2;
        const int pts[2][2] = {{cx, y2 + 1}, {(x2 + 1) & mask, cy}};
        for (const auto& p : pts) {
            if (p[1] < 0 || p[1] >= H) continue;
            uint32_t c = g_gameMap.rawCell(p[0], p[1]);
            const int tile = static_cast<int>((c & 0xFFE0) >> 5);
            c = (c & 0xFFFF0000u) | ((static_cast<uint32_t>(tubeTileFor(tile)) << 5) & 0xFFE0u) | 0x1Au;
            g_gameMap.setRawCell(p[0], p[1], c);
            autotileWallTube(p[0], p[1], 0x11);
        }
    }
    if ((type == 0x15 || type == 0x16) && beacon) {         // vt[43] do beacon (sub_405620)
        UnitRecord& b = g_units[beacon];
        beaconSurveyBy(beacon, owner);                       // vt[0x9C] sub_405420
        b.beaconHasMine = 1;
        UnitRecord& m = g_units[id];                         // sub_44ADC0
        m.mineVariant = b.beaconVariant;
        m.mineYield = b.beaconYield;
        m.mineLink = b.beaconLink;
        m.mineBeacon = beacon;
    }
    for (int p = 0; p < kMaxPlayers; ++p) colonyMarkDirty(p);    // sub_42F000: rede
    return id;
}

int scenarioCreateUnit(int type, int x, int y, int owner, int cargo, int rotation) {
    const int maxY = g_gameMap.height() - 1;
    if (y <= 0) y = 0;
    if (y >= maxY) y = maxY;
    if (mapIdIsBuilding(type)) {
        const StructSheetDesc* d = structSheet(type);
        if (!d) return 0;
        if (!multiplayerMission() && type != 0x15 && type != 0x2C)
            findBuildingSpot(x, y, d->sizeX + 2, d->sizeY + 2);
        const int id = placeBuilding(type, x, y, owner);
        if (!id) return 0;
        UnitRecord& r = g_units[id];
        r.cargo = static_cast<uint16_t>(cargo);              // [+0x24]
        r.state34 = 0;                                       // [+0x34] = 0: pronto
        colonyMarkDirty(owner & 0x0F);                       // vt+0x8C (sub_483850)
        r.flags |= UNIT_LIGHTS;
        return id;
    }
    findVehicleSpot(type, x, y);
    Unit u;
    if (!createUnit(u, type, LOCATION{x, y}, owner, cargo)) return 0;
    g_units[u.id()].rotation = static_cast<uint8_t>(rotation << 5);
    return u.id();
}

int createUnitFromRecord(const MissionUnitRecord& rec, int x, int y, int owner, int lightsOn) {
    if (mapIdIsBuilding(rec.type)) {                         // sub_49D280
        const StructSheetDesc* d = structSheet(rec.type);
        if (!d) return 0;
        findBuildingSpot(x, y, d->sizeX + 2, d->sizeY + 2);
        const int id = placeBuilding(rec.type, x, y, owner);
        if (!id) return 0;
        UnitRecord& r = g_units[id];
        for (int p = 0; p < kMaxPlayers; ++p) colonyMarkDirty(p);
        if (r.flags & UNIT_TURRET_BLD) r.cargo = static_cast<uint16_t>(rec.weapon);
        r.state34 = 0;
        colonyMarkDirty(owner & 0x0F);
        r.flags |= UNIT_LIGHTS;
        return id;
    }
    findVehicleSpot(rec.type, x, y);
    Unit u;
    if (!createUnit(u, rec.type, LOCATION{x, y}, owner, rec.weapon)) return 0;
    UnitRecord& r = g_units[u.id()];
    r.rotation = static_cast<uint8_t>(rec.rotation);
    r.flags = (r.flags & ~UNIT_LIGHTS) | (lightsOn & 1);
    if (rec.type == mapCargoTruck) unitSetTruckCargo(u.id(), rec.cargoType, rec.cargoAmount);
    return u.id();
}

// vt[41] do beacon (sub_405540): −1 sorteia (raro < 30%, rendimento <20/≤80).
void beaconInit(UnitRecord& b, int commonRare, int yield, int variant) {
    if (commonRare == -1) { if (TethysGame::getRand(100) < 0x1E) b.beaconRare = 1; }
    else if (commonRare == 1) b.beaconRare = 1;
    else if (commonRare == 0) b.beaconRare = 0;
    if (yield == -1) {
        const int r = TethysGame::getRand(100);
        b.beaconYield = r < 0x14 ? 0 : (r <= 0x50 ? 1 : 2);
    } else if (yield == 0) b.beaconYield = 0;
    else if (yield == 1) b.beaconYield = 1;
    else if (yield == 2) b.beaconYield = 2;
    if (variant == -1) { b.beaconVariant = TethysGame::getRand(3); }
    else if (variant >= 0 && variant <= 2) b.beaconVariant = variant;
    b.beaconSet = 1;
}

int createBeacon(int type, int x, int y, int commonRare, int yield, int variant) {
    if (type == 0x53 || type == 0x52) {                      // fumarola / respiradouro: cellType
        const uint32_t c = g_gameMap.rawCell(x & widthMask(), y);
        g_gameMap.setRawCell(x & widthMask(), y, (c & 0xFFFFFFEBu) | 0x0Bu);
    }
    Unit u;
    if (!createUnit(u, type, LOCATION{x & widthMask(), y}, 6)) return 0;
    if (commonRare != -1 || yield != -1 || variant != -1)
        beaconInit(g_units[u.id()], commonRare, yield, variant);
    return 1;
}

void unitSetLights(int id, int on) {                         // case 35 (pacote 0x23)
    if (id <= 0 || id >= static_cast<int>(g_units.size())) return;
    UnitRecord& r = g_units[id];
    if (r.id == -1 || (((r.flags << 2) | r.flags) & UNIT_EMP)) return;
    r.flags = (r.flags & ~UNIT_LIGHTS) | (static_cast<uint32_t>(on) & 1u);
}

void unitSetTruckCargo(int id, int cargoType, int amount) { // sub_406BD0
    if (id <= 0 || id >= static_cast<int>(g_units.size())) return;
    if (cargoType == 9 && amount > 0x1F40) amount -= 0x1F40;
    const uint16_t a = static_cast<uint16_t>(amount << 4);
    g_units[id].cargo = static_cast<uint16_t>((a & ~0xFu) | (static_cast<uint16_t>(cargoType) & 0xFu));
}

void unitSetFactoryCargo(int id, int bay, int type, int cargo) {
    if (id <= 0 || id >= static_cast<int>(g_units.size())) return;
    UnitRecord& r = g_units[id];
    if (type == 0x69 || type == 0x6A || type == 0x6B) { r.padCargo = type; return; }
    if (bay < 0 || bay >= 6) return;
    r.bayType[bay] = static_cast<uint8_t>(type);
    r.bayCargo[bay] = static_cast<uint8_t>(cargo);
}

static bool s_viewReq = false;
static int s_viewX = 0, s_viewY = 0;
void requestCenterView(int px, int py) { s_viewReq = true; s_viewX = px; s_viewY = py; }
bool takeCenterViewRequest(int& px, int& py) {
    if (!s_viewReq) return false;
    s_viewReq = false; px = s_viewX; py = s_viewY;
    return true;
}

// ---- UnitBlock --------------------------------------------------------------
// sub_49D1B0: classe do registro (tanques/Guard Post por arma; demais por tipo).
static int unitClassOf(int type, int weapon) {
    if (((type >= 5 && type <= 7) || type == 0x17) && type >= 1 && type <= 0x0F) {
        if (weapon == 0x46) return 1;
        if (weapon == 0x47 || weapon == 0x3C) return weapon == 0x3C ? 2 : 3;
        return 0;
    }
    if (type == 4) return 0;
    if (type == 3) return 4;
    if (type == 2) return 5;
    if (type == 0x0E) return 6;
    if (type == 1) return 7;
    if (type == 0x0F) return 8;
    if (type == 0x15 || type == 0x16) return 0x0D;
    if (type == 0x30) return 0x0A;
    if (type == 0x2D) return 0x0B;
    if (type == 0x2F) return 0x0C;
    if (type == 0x17) return 0x0E;
    if (type >= 1 && type <= 0x0F) return 9;
    return 0x0F;
}

// _swap / _shortsort / _qsort do CRT do OUTPOST2.EXE (MSVC 4.x).
static void crtSwap(uint8_t* a, uint8_t* b, size_t w) {
    if (a == b) return;
    while (w--) { const uint8_t t = *a; *a++ = *b; *b++ = t; }
}
using Cmp = int (*)(const void*, const void*);
static void crtShortsort(uint8_t* lo, uint8_t* hi, size_t w, Cmp cmp) {
    while (hi > lo) {
        uint8_t* mx = lo;
        for (uint8_t* p = lo + w; p <= hi; p += w)
            if (cmp(p, mx) > 0) mx = p;
        crtSwap(mx, hi, w);
        hi -= w;
    }
}
static void crtQsort(void* base, size_t num, size_t w, Cmp cmp) {
    if (num < 2 || w == 0) return;
    uint8_t* lostk[30]; uint8_t* histk[30];
    int stk = 0;
    uint8_t* lo = static_cast<uint8_t*>(base);
    uint8_t* hi = lo + w * (num - 1);
    for (;;) {
        const size_t size = static_cast<size_t>(hi - lo) / w + 1;
        if (size <= 8) {
            crtShortsort(lo, hi, w, cmp);
        } else {
            crtSwap(lo + (size / 2) * w, lo, w);
            uint8_t* loguy = lo;
            uint8_t* higuy = hi + w;
            for (;;) {
                do { loguy += w; } while (loguy <= hi && cmp(loguy, lo) <= 0);
                do { higuy -= w; } while (higuy > lo && cmp(higuy, lo) >= 0);
                if (higuy < loguy) break;
                crtSwap(loguy, higuy, w);
            }
            crtSwap(lo, higuy, w);
            if (higuy - 1 - lo >= hi - loguy) {
                if (lo + w < higuy) { lostk[stk] = lo; histk[stk] = higuy - w; ++stk; }
                if (loguy < hi) { lo = loguy; continue; }
            } else {
                if (loguy < hi) { lostk[stk] = loguy; histk[stk] = hi; ++stk; }
                if (lo + w < higuy) { hi = higuy - w; continue; }
            }
        }
        if (--stk < 0) return;
        lo = lostk[stk]; hi = histk[stk];
    }
}
static int cmpClass(const void* a, const void* b) {          // sub_49D4E0
    return static_cast<const MissionUnitRecord*>(a)->unitClass - static_cast<const MissionUnitRecord*>(b)->unitClass;
}

void unitBlockSort(MissionUnitRecord* table, int& numUnits, int ranges[16][2]) {
    numUnits = 0;
    while (table[numUnits].type != 0) {
        table[numUnits].unitClass = unitClassOf(table[numUnits].type, table[numUnits].weapon);
        ++numUnits;
    }
    crtQsort(table, static_cast<size_t>(numUnits), sizeof(MissionUnitRecord), cmpClass);
    for (int c = 0; c < 16; ++c) {
        int s = 0;
        while (s < numUnits && table[s].unitClass < c) ++s;
        int e = s;
        while (e < numUnits && table[e].unitClass == c) ++e;
        ranges[c][0] = s;
        ranges[c][1] = e;
    }
}

void unitOrigPixel(const UnitRecord& r, int& x, int& y) { x = unitOX(r); y = unitOY(r); }

void mapRectIterInit(MapRectIter& it, int x, int y, int w, int h, uint32_t playerMask, uint32_t flagMask) {
    const int pxW = g_gameMap.internalWidth() * 32;             // [GameMap+8]
    it.cur = 0;
    it.playerMask = playerMask;
    it.flagMask = flagMask;
    it.x0 = x; it.x1 = x + w; it.wrap = 0;
    if (pxW <= it.x1) { it.x1 -= pxW; it.x0 -= pxW; it.wrap = 1; }
    it.y0 = y;
    it.y1 = y + h;
    if (g_gameMap.height() * 32 <= it.y1) it.y1 = g_gameMap.height() * 32;
}

int mapRectIterNext(MapRectIter& it) {
    const int pxW = g_gameMap.internalWidth() * 32;
    const int off = g_gameMap.internalXOffset() * 32;
    for (int id = it.cur ? unitListNext(it.cur) : unitListFirst(); id != 0; id = unitListNext(id)) {
        it.cur = id;
        const UnitRecord& r = g_units[id];
        if (!(r.flags & 0x46u) || !(it.playerMask & (1u << (r.owner & 0x0F))) || !(r.flags & it.flagMask) ||
            r.field22 == 0x0D || r.id == -1 || (r.flags & UNIT_DEAD))
            continue;
        int ux, uy;
        unitOrigPixel(r, ux, uy);
        ux += off;
        if (uy < it.y0 || uy >= it.y1) continue;
        if ((it.x0 <= ux && ux < it.x1) || (it.wrap && it.x0 <= ux - pxW && ux - pxW < it.x1)) return id;
    }
    return 0;
}

void mapRangeIterInit(MapRangeIter& it, int px, int py, int r, uint32_t playerMask, uint32_t flagMask) {
    it.cur = 0;
    it.playerMask = playerMask;
    it.flagMask = flagMask;
    it.r2 = static_cast<uint32_t>(r * r);
    it.y0 = py - r;
    it.y1 = py + r < g_gameMap.height() * 32 ? py + r : g_gameMap.height() * 32;
    it.cx = px;
    it.cy = py;
}

int mapRangeIterNext(MapRangeIter& it) {
    const int pxW = g_gameMap.internalWidth() * 32, pxMask = pxW - 1;
    const int off = g_gameMap.internalXOffset() * 32;
    for (int id = it.cur ? unitListNext(it.cur) : unitListFirst(); id != 0; id = unitListNext(id)) {
        it.cur = id;
        const UnitRecord& r = g_units[id];
        int ux, uy;
        unitOrigPixel(r, ux, uy);
        if (uy < it.y0) continue;                               // (sub_4374F0 começa aqui)
        if (uy >= it.y1) return 0;
        if (!(r.flags & 0x46u) || !(it.playerMask & (1u << (r.owner & 0x0F))) || !(r.flags & it.flagMask) ||
            r.field22 == 0x0D || r.id == -1 || (r.flags & UNIT_DEAD))
            continue;
        int dx = it.cx - (ux + off);
        const int half = pxMask / 2;
        if (-half >= dx) dx += pxW;
        else if (half < dx) dx = -((-dx) & pxMask);
        const int dy = uy - it.cy;
        if (static_cast<uint32_t>(dx * dx + dy * dy) < it.r2) return id;
    }
    return 0;
}

void mapPointIterInit(MapPointIter& it, int px, int py, uint32_t playerMask, uint32_t flagMask) {
    it.cur = 0;
    it.playerMask = playerMask;
    it.flagMask = flagMask;
    it.y0 = py - 0x60 > 0 ? py - 0x60 : 0;
    it.y1 = py + 0x60 < g_gameMap.height() * 32 ? py + 0x60 : g_gameMap.height() * 32;
    it.px = px;
    it.py = py;
}

// Anim crua desenhada agora: prédio vt[0x2C] (estado), veículo vt[0x80](direção).
static int unitSpriteRaw(const UnitRecord& r) {
    if (r.flags & UNIT_BUILDING) {
        const int a = structureCurAnim(r);
        return a >= 0 ? a + 0x65 : 0;
    }
    const int dir = ((r.rotation + 8) & 0xF0) >> 4;
    if (r.type == mapCargoTruck) return truckAnimRaw(r, dir);
    if (r.type == mapConVec) { if (const int raw = convecDockAnimRaw(r)) return raw; }
    const int ow = r.owner & 0x0F;
    const int a = vehicleAnimIndex(r.type, (dir & 15) << 4, ow < kMaxPlayers ? g_playerData[ow].isEden != 0 : true);
    return a >= 0 ? a + 0x65 : 0;
}

// vt[0x38]: 0 = acertou. Veículo (sub_401850): sempre. Prédio (sub_401B60):
// pixel do sprite; senão a base do estado (0x87E/0x87F/0x880, frame 0).
static bool unitPixelHit(const UnitRecord& r, int raw, int dx, int dy) {
    if (!(r.flags & UNIT_BUILDING)) return true;
    if (!g_art.pixelMiss(raw - 0x65, structureFrame(r), dx, dy)) return true;
    if (r.action == 0x2F || r.action == 0x21) return false;
    int base = 0;
    switch ((r.state34 >> 16) & 0xFFFF) {
        case 0: case 5: case 6: case 7: case 8: case 9: base = 0x87E; break;
        case 1: base = 0x87F; break;
        case 2: base = 0x880; break;
        default: break;
    }
    return base && !g_art.pixelMiss(base - 0x65, 0, dx, dy);
}

int mapPointIterNext(MapPointIter& it) {
    const int pxMask = g_gameMap.internalWidth() * 32 - 1;
    const int off = g_gameMap.internalXOffset() * 32;
    for (int id = it.cur ? unitListNext(it.cur) : unitListFirst(); id != 0; id = unitListNext(id)) {
        it.cur = id;
        const UnitRecord& r = g_units[id];
        const int uy = unitOY(r);
        if (uy < it.y0) continue;                               // (sub_4374F0 começa aqui)
        if (uy >= it.y1) return 0;
        if (!(r.flags & 0x46u) || !(it.playerMask & (1u << (r.owner & 0x0F))) || !(r.flags & it.flagMask) ||
            r.field22 == 0x0D || r.id == -1 || (r.flags & UNIT_DEAD))
            continue;
        const int raw = unitSpriteRaw(r);
        if (!raw || raw - 0x65 >= g_art.numAnimations()) continue;
        const uint32_t* h = g_art.animation(raw - 0x65).header;
        const int w = static_cast<int>(h[3] - h[1]), hh = static_cast<int>(h[4] - h[2]);   // sub_404D30
        const int ox = static_cast<int>(h[5] - h[1]), oy = static_cast<int>(h[6] - h[2]);  // sub_404D90
        const int top = uy - oy;
        if (top > it.py || top + hh <= it.py) continue;
        const int left = (unitOX(r) + off - ox) & pxMask;
        const int dx = (it.px - left) & pxMask;
        if (dx >= w) continue;
        if (r.flags & 0x80u) return id;
        if (unitPixelHit(r, raw, dx, it.py - top)) return id;
    }
    return 0;
}

void mapUnitsInRect(int x, int y, int w, int h, uint32_t playerMask, uint32_t flagMask, std::vector<int>& out) {
    MapRectIter it;
    mapRectIterInit(it, x, y, w, h, playerMask, flagMask);
    for (int id = mapRectIterNext(it); id != 0; id = mapRectIterNext(it)) out.push_back(id);
}

static int buildingMaxHP(const UnitRecord& r) {
    const StructSheetDesc* d = structSheet(r.type);
    return d ? d->player[(r.owner >> 4) & 0x0F].hitPoints : 0;
}

void unitDoInfect(int id) {                                  // ?DoInfect
    UnitRecord& r = g_units[id];
    if (r.id == -1 || (r.flags & UNIT_DEAD) || !(r.flags & UNIT_BUILDING)) return;
    // CORRECAO: o original sai aqui quando a partida tem mais de um jogador
    // (`cmp dword_56DEC0, 1 / jg`). Como quase toda missao de campanha tem dois
    // ou tres jogadores, isso deixava o DoInfect das missoes sem fazer NADA --
    // nada no corpo abaixo depende de haver so um jogador.
    soundPlayAt(unitOX(r), unitOY(r), 0x53);
    colonyStaffKilled(id);
    UnitRecord& b = g_units[id];
    b.flags |= 0x40000u;
    const int act = static_cast<int8_t>(b.action);
    const int hp = buildingMaxHP(b);
    b.damage = static_cast<int16_t>(b.damage + hp / 3);
    if (act == 0x22 || act == 0x21) b.damage = static_cast<int16_t>(hp);
    else if (act != 0x12) { b.field22 = 0; b.action = 0; }
    if (hp <= b.damage) {
        b.damage = static_cast<int16_t>(hp);
        b.flags |= UNIT_DEAD;
        killUnit(id, 5);
    }
    // (se é a única unidade selecionada, o painel é refeito — sub_41A8E0.)
}

void unitDoSelfDestruct(int id) {                            // ?DoSelfDestruct
    UnitRecord& r = g_units[id];
    if (r.id == -1 || (r.flags & UNIT_DEAD)) return;
    // sub_40E210 case 20: comandável (vt[29] sub_44B620: viva, sem 0x80000/0x20000,
    // sub-estado != 0x0A) e veículo/0x40.
    if (((r.flags | (r.flags << 2)) & 0x80000u) || r.field22 == 0x0A) return;
    if (!(r.flags & 0x42u)) return;
    unitSelfDestruct(id);                                    // sub_43A6A0
}

int unitHasOccupiedBay(int id) {                             // ?HasOccupiedBay
    const UnitRecord& r = g_units[id];
    // loc_475FEE: na garagem o original devolve o vt[63] (sub_41C9E0), ou seja
    // o NÚMERO de veículos guardados — não um 0/1 como no ramo da fábrica.
    if (r.type == 0x29) return garageCount(id);
    if (!(r.flags & UNIT_FACTORY)) return 0;
    for (int i = 0; i < 6; ++i)                              // vt[65] = movsx [+0x61+baia]
        if (static_cast<int8_t>(r.bayType[i]) != 0) return 1;
    return 0;
}

} // namespace op2
