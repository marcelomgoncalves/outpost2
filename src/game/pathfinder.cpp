#include "pathfinder.h"

#include <cstdio>
#include <cstdlib>
#include "weapon.h"

#include "game_map.h"
#include "terrain_cost.h"
#include "unit.h"
#include "player.h"

namespace op2 {

PfContext g_pf;
std::vector<PfContext> g_pfStack;

static constexpr int kSentinel = static_cast<int>(0x80000000);

// Espaço do pathfinder = largura INTERNA do original (dword_54EBC8/54EBC4): nos
// mapas com menos de 512 colunas ela é 2W, e as colunas que ficam fora do mapa
// guardam o valor 6 com que sub_4353F0 enche o grid (tipo 6 = intransponível),
// então a busca não dá a volta pela borda. Em x o port usa 0..W−1; as colunas
// W..2W−1 (e x < 0) são as de fora. Nos mapas de 512 a largura interna é W e
// o mapa dá a volta.
static int pfW()     { return g_gameMap.internalWidth(); }
static int pfMaskX() { return g_gameMap.internalWidth() - 1; }
static uint32_t pfCell(int x, int y) {
    if (g_gameMap.internalXOffset() && (x < 0 || x >= g_gameMap.width())) return 6u;
    return g_gameMap.rawCell(x, y);
}

// ---- Pilha / contexto -------------------------------------------------------

void pfResetContext() {               // sub_438100
    g_pf.unitId = 0;
    g_pf.goalMode = 0;
    g_pf.targetX = g_pf.targetY = kSentinel;
    g_pf.target2X = g_pf.target2Y = kSentinel;
}

void pfInitStack() {                   // sub_438130
    g_pfStack.clear();
}

void pfPush() {                        // sub_438180: empilha o ctx atual e reinicia alvos
    g_pfStack.push_back(g_pf);
    g_pf.unitId = 0;
    g_pf.goalMode = 0;
    g_pf.targetX = g_pf.targetY = kSentinel;
    g_pf.target2X = g_pf.target2Y = kSentinel;
}

void pfPop() {                         // sub_438200: restaura o ctx do topo
    if (g_pfStack.empty()) { g_pf = PfContext{}; return; }
    g_pf = g_pfStack.back();
    g_pfStack.pop_back();
    if (g_pf.unitId != 0) {            // revalida classe/tubo do ctx restaurado
        pfSetContext(g_pf.unitId);
    }
}

void pfSetContext(int unitId) {        // sub_438260
    g_pf.unitId = unitId;
    if (unitId <= 0 || unitId >= static_cast<int>(g_units.size())) {
        g_pf.moveClass = 0; g_pf.tubeCost = 0; return;
    }
    g_pf.moveClass = g_units[unitId].moveClass;   // [objUnit+0x1E8]
    g_pf.tubeCost  = pfUnitCanUseTube(unitId);     // sub_44D4E0 → dword_4E0C08
}

void pfSetTarget(int x, int y)  { g_pf.targetX = x;  g_pf.targetY = y;  }   // sub_438290
void pfSetTarget2(int x, int y) { g_pf.target2X = x; g_pf.target2Y = y; }   // sub_4382A0
void pfSetRadius(int r)         { g_pf.radius2 = r * r; }                    // sub_4382B0
void pfSetGoal(int mode, int x, int y, int dist) {
    g_pf.goalMode = mode;
    pfSetTarget(x, y);
    if (mode == 1 || mode == 2) { pfSetTarget2(x, y); pfSetRadius(dist); }
    // Modo 3 (sub_4401F0 case 3): o próprio alvo já vem deslocado p/ x − 1
    // (ver movement.cpp) e também é o ponto de referência dos vizinhos.
    if (mode == 3) pfSetTarget2(x, y);
}

// ---- Helpers de ocupação ----------------------------------------------------

bool pfPlayersAllied(int a, int b) {   // sub_490C50: bit a no allyMask de b
    if (b < 0 || b >= kMaxPlayers) return false;
    return (g_playerData[b].allyMask & (1u << (a & 0x1F))) != 0;
}

int pfUnitCanUseTube(int unitId) {     // sub_44D4E0
    if (unitId <= 0 || unitId >= static_cast<int>(g_units.size())) return 0;
    const UnitRecord& u = g_units[unitId];
    int owner = u.owner & 0x0F;
    // dword_56E304[owner*777] = campo do jogador em +0x28 (0 = pode usar parede/tubo).
    // Modelado como isHuman (refinar quando o struct completo do jogador for mapeado).
    if (owner >= 0 && owner < kMaxPlayers && g_playerData[owner].isHuman != 0) return 0;
    if (!(u.flags & 0x10)) return 0;               // flag +0x44 bit4
    uint16_t cargo = u.cargo;                       // +0x24
    if (cargo == 0x3D || cargo == 0x3E) return 1;
    return 0;
}

int pfOccupantPassable(int occId, int searcherId) {  // sub_44BD60
    if (occId <= 0 || occId >= static_cast<int>(g_units.size())) return 1;
    const UnitRecord& o = g_units[occId];
    if (o.id == -1) return 1;                        // [+4]==-1 (morto) → atravessável
    uint32_t f = o.flags;                            // [+0x44]
    if (f & UNIT_DEAD) return 1;                      // 0x20000 → atravessável
    if (!(f & UNIT_VEHICLE)) return 0;               // !bit2(=0x2? bit1) prédio → bloqueia
    if (f & 0xC0000) return 0;                        // flags 18/19 → bloqueia

    int occOwner = o.owner & 0x0F;
    int searcherOwner = 0;
    if (searcherId > 0 && searcherId < static_cast<int>(g_units.size()))
        searcherOwner = g_units[searcherId].owner & 0x0F;

    // Aliança: sub_490C50(cmdObj[occOwner], searcherOwner). Não-aliado ARMADO
    // bloqueia; não-aliado DESARMADO passa (um caminhão inimigo parado também
    // sai da frente):
    //     call sub_490C50 / test eax,eax / jnz segue      -> aliado
    //     test [+44h],10h / jz segue                     -> desarmado
    //     xor eax,eax / retn                             -> armado: bloqueia
    // O porte tinha isto INVERTIDO (bloqueava o desarmado e deixava o armado).
    bool allied = pfPlayersAllied(occOwner, searcherOwner);
    if (!allied) {
        if (f & 0x10) return 0;                      // não-aliado e armado → bloqueia
    }

    uint8_t action = o.action;                       // [+0x21]
    switch (action) {                                // ações ocupadas → bloqueia
        case 0x27: case 0x28: case 3: case 1:
        case 0x26: case 0x1F: case 0x0F:
            return 0;
        case 6:
            if (o.field22 == 0) return 0;
            break;
        default: break;
    }
    uint8_t sub = o.field22;                          // [+0x22]
    if (sub == 0x0A) return 0;
    if (f & 0x10) {                                   // flag 0x10 → exige field4E == 0x7FFF
        if (o.field4E != 0x7FFF) return 0;
    }
    return (sub != 8) ? 1 : 0;                        // bloqueia só se sub-estado 8
}

// ---- Primitivas de célula ---------------------------------------------------

int pfCanEnter(int x, int y) {         // sub_4382C0
    if (y < 0 || y >= g_gameMap.height()) return 0;
    uint32_t cell = pfCell(x, y);

    if (cell & GameMap::CELL_WALLBITS) {             // 0xA8000000
        if (cell & GameMap::CELL_HARDBLOCK) return 0; // 0x28000000
        int ct = static_cast<int>(cell & 0x1F);
        if (ct >= GameMap::TUBE_CELLTYPE_LO && ct <= GameMap::TUBE_CELLTYPE_HI)
            return g_pf.tubeCost;                     // dword_4E0C08
        return 0;
    }

    int ct = static_cast<int>(cell & 0x1F);
    if (terrainMoveCost(ct, g_pf.moveClass) == 0) return 0;  // custo 0 = impassável

    uint32_t occ = cell & GameMap::CELL_OCCUPANCY;    // 0x7FF0000
    if (occ == 0) return 1;
    int occId = static_cast<int>(occ >> GameMap::CELL_OCC_SHIFT);
    if (occId == g_pf.unitId) return 1;               // própria unidade
    return pfOccupantPassable(occId, g_pf.unitId) ? 1 : 0;
}

// Tabela de direções dword_4D5338 (verbatim do asm @326542).
const DirDelta kDir8[8] = {
    { 1,  0},  // 0 E
    { 1,  1},  // 1 SE
    { 0,  1},  // 2 S
    {-1,  1},  // 3 SW
    {-1,  0},  // 4 W
    {-1, -1},  // 5 NW
    { 0, -1},  // 6 N
    { 1, -1},  // 7 NE
};

int pfDirFromDelta(int dx, int dy) {       // sub_45C540 (ecx=dx, edx=dy)
    if (dx == 0) {
        return (dy < 0) ? 6 : 2;           // N : S  (y para baixo)
    }
    // razão = (dy<<8)/dx  (idiv, trunca p/ zero como o asm)
    int ratio = (dy << 8) / dx;
    if (ratio >= 0x26A) {                   // loc_45C57C (quase vertical)
        return (dx < 0) ? 6 : 2;            //   dx<0 → N : S
    }
    if (ratio <= -0x26A) {                   // loc_45C58C
        return (dx >= 0) ? 6 : 2;           //   dx>=0 → N : S
    }
    if (ratio >= 0x6A) {                     // loc_45C596 (diagonal)
        return (dx < 0) ? 5 : 1;           //   dx<0 → NW : SE
    }
    if (ratio <= -0x6A) {                    // loc_45C5A6
        return (dx < 0) ? 3 : 7;           //   dx<0 → SW : NE
    }
    return (dx < 0) ? 4 : 0;                 // quase horizontal: W : E
}

int pfSmoothPath(int* dirs, int count) {   // sub_43FC50 (ecx=dirs, edx=count)
    int edx = count - 1;                     // último índice
    if (edx < 1) return edx + 1;
    for (;;) {                               // loc_43FC58
        int ebx = dirs[edx];                 // direção atual
        int prev = dirs[edx - 1];            // direção anterior
        int esi = (ebx - prev) & 7;          // diferença
        bool adjusted = false;
        if (prev & 1) {                      // anterior = diagonal (ímpar)
            if (esi == 3)      { edx--; ebx -= 2; adjusted = true; }
            else if (esi == 4) { edx -= 2; }
            else if (esi == 5) { edx--; ebx += 2; adjusted = true; }
            else break;                      // {0,1,2,6,7} → fim
        } else {                             // anterior = cardinal (par)
            if (esi == 2 || esi == 3) { edx--; ebx -= 1; adjusted = true; }
            else if (esi == 4)        { edx -= 2; }
            else if (esi == 5 || esi == 6) { edx--; ebx += 1; adjusted = true; }
            else break;                      // {0,1,7} → fim
        }
        if (adjusted) {                      // loc_43FCBA
            ebx &= 7;
            dirs[edx] = ebx;
        }
        if (edx < 1) break;                  // loc_43FCC0
    }
    return edx + 1;
}

int pfWallFollow(int& dir, int& x, int& y, int turnSign) {  // sub_43FBB0
    int rev = 8 - turnSign;                 // var_4: passo de rotação reverso
    int mask = pfMaskX();
    for (int guard = 0; guard < 8; ++guard) {   // no original é ilimitado; guard = 1 volta
        int d = dir & 7;
        int nx = (x + kDir8[d].dx) & mask;
        int ny = y + kDir8[d].dy;
        if (pfCanEnter(nx, ny)) {           // dword_550700
            x = nx;
            y = ny;
            dir = (d + ((d & 1) ? turnSign * 2 : turnSign)) & 7;  // ajuste (×2 se diagonal)
            return d;                        // direção do passo (eax no asm)
        }
        dir = (d + rev) & 7;                 // rotaciona
    }
    return dir & 7;
}

// Distância horizontal com wrap em X, mapeada p/ [-metade, +metade] (loc_43F609
// do asm): diff = dx; se diff < -half → +width; se diff > half → -((-diff)&mask).
static int pfWrapDX(int diff) {
    int mask = pfMaskX();                   // dword_54EBC4
    int w    = pfW();                       // dword_54EBC8
    int half = mask >> 1;
    if (-half > diff) diff += w;
    else if (half < diff) diff = -(((-diff)) & mask);
    return diff;
}

int pfOctDistToTarget(int x, int y) {       // sub_43FCD0
    int dx = pfWrapDX(g_pf.targetX - x);
    int adx = (dx < 0 ? -dx : dx) << 16;
    int dy = g_pf.targetY - y;
    int ady = (dy < 0 ? -dy : dy) << 16;
    int mx = adx, mn = ady;
    if (mn > mx) { int t = mx; mx = mn; mn = t; }   // mx = maior, mn = menor
    return mx + (mn >> 1);                           // max + min/2
}

// dword_4D0510: custo de passo cardinal (dir par) / diagonal (dir ímpar).
static const int kStepCost[2] = { 0x20, 0x2D };

// sub_43FF20 @106667 — avalia truncamentos do caminho no sentido FORWARD. Para
// cada prefixo i, anexa dir[i] ao buffer suavizado (pfSmoothPath), acumula o custo
// de passo e soma a distância octogonal de pos[i] ao alvo; guarda o (custo,índice)
// mínimo. Para se a distância chega a 0 (alcançou o alvo).
static void evalForward(const int* dir, int count, const int* px, const int* py,
                        int& outCost, int& outIdx) {
    outCost = 0x7FFFFFFF;
    outIdx = 0;
    static int sm[640];
    static int cum[640];
    int L = 0;
    for (int i = 0; i < count; ++i) {
        sm[L] = dir[i];
        L = pfSmoothPath(sm, L + 1);         // sub_43FC50
        int ebx = L - 1;
        cum[ebx] = kStepCost[sm[ebx] & 1] + (ebx > 0 ? cum[ebx - 1] : 0);
        int dist = pfOctDistToTarget(px[i], py[i]);   // sub_43FCD0
        int total = cum[ebx] + dist;
        if (total < outCost) { outCost = total; outIdx = i; }
        if (dist == 0) break;
    }
}

// sub_440080 @106817 — igual, mas no sentido REVERSE: percorre dir[] do fim para o
// começo com cada direção invertida ((d+4)&7) e pos[] de trás para frente.
static void evalReverse(const int* dir, int count, const int* px, const int* py,
                        int& outCost, int& outIdx) {
    outCost = 0x7FFFFFFF;
    outIdx = 0;
    static int sm[640];
    static int cum[640];
    int L = 0;
    // edi = count-1 .. 1 ; pos indexado de [count-2] para trás (como o asm).
    for (int i = count - 1, p = count - 2; i > 0; --i, --p) {
        sm[L] = (dir[i] + 4) & 7;            // direção invertida
        L = pfSmoothPath(sm, L + 1);
        int ebx = L - 1;
        cum[ebx] = kStepCost[sm[ebx] & 1] + (ebx > 0 ? cum[ebx - 1] : 0);
        int dist = (p >= 0) ? pfOctDistToTarget(px[p], py[p]) : 0;
        int total = cum[ebx] + dist;
        if (total < outCost) { outCost = total; outIdx = i; }
        if (dist == 0) break;
    }
}

// Emenda os primeiros (idx+1) passos de um contorno à rota de saída, suavizando
// (loc_43F9DC do asm). idx < 0 não emenda nada.
static int appendContour(const int* dir, int idx, int* out, int pathLen, int maxDirs) {
    if (idx < 0) return pathLen;
    for (int k = 0; k <= idx; ++k) {
        if (pathLen >= maxDirs) break;
        out[pathLen++] = dir[k];
        pathLen = pfSmoothPath(out, pathLen);
    }
    return pathLen;
}

int pfSearchRoute(const SearchContext& ctx, int* outDirs, int maxDirs, int* reached) {
    // sub_43F5B0 — busca de rota (seguir-parede bidirecional).
    int curX = ctx.startX;
    int curY = ctx.startY;
    int pathLen = 0;
    int failedOnce = 0;                                // var_3858
    int mask = pfMaskX();
    // Clip (54EBD4/D8/DC/E0) em x interno → x do port (−deslocamento; nos mapas
    // de 512 fica −1..0x7FFFFFFF, sem limite).
    const MAP_RECT& clip = g_gameMap.clipRect();
    const int xoff = g_gameMap.internalXOffset();
    int cx1 = clip.x1 - xoff, cy1 = clip.y1, cx2 = clip.x2 - xoff, cy2 = clip.y2;
    static int dirA[640], posAx[640], posAy[640];
    static int dirB[640], posBx[640], posBy[640];

    for (;;) {
        // --- FASE A: reta rumo ao alvo (loc_43F5EB) ---
        int blockedDir = 0;
        bool blocked = false;
        for (;;) {
            if (curX == ctx.destX && curY == ctx.destY) {
                if (reached) *reached = 1;
                return pathLen;
            }
            if (pathLen >= maxDirs) {
                if (reached) *reached = 0;
                return pathLen;
            }
            int dx = pfWrapDX(ctx.destX - curX);
            int dy = ctx.destY - curY;
            int dir = pfDirFromDelta(dx, dy);           // sub_45C540
            int nx = (curX + kDir8[dir].dx) & mask;
            int ny = curY + kDir8[dir].dy;
            if (pfCanEnter(nx, ny)) {                     // passo reto possível
                outDirs[pathLen++] = dir;
                pathLen = pfSmoothPath(outDirs, pathLen); // sub_43FC50
                curX = nx; curY = ny;
                continue;
            }
            blocked = true; blockedDir = dir; break;      // obstáculo → FASE B
        }
        (void)blocked;

        // --- FASE B: contorno de obstáculo (loc_43F6C6) ---
        if (failedOnce) {                                 // só um desvio por chamada
            if (reached) *reached = 0;
            return pathLen;
        }
        int budget = maxDirs - pathLen;                   // var_3884

        // Contorno A (turnSign = 7): entrada contada antes do teste de clip.
        int cntA = 0;
        { int wd = (blockedDir + 1) & 7, wx = curX, wy = curY;
          while (!(budget < cntA)) {                       // cntA <= budget
              int sd = pfWallFollow(wd, wx, wy, 7);
              dirA[cntA] = sd; posAx[cntA] = wx; posAy[cntA] = wy; ++cntA;
              if (wy == cy1 && wd == 7) break;
              if (wy == cy2 && wd == 3) break;
              if (wx == cx1 && wd == 5) break;
              if (wx == cx2 && wd == 1) break;
          }
        }
        int useA = (budget < cntA) ? budget : cntA;        // min(budget, cntA)

        // Contorno B (turnSign = 1): dir gravado antes do teste; só conta se não quebrar.
        int cntB = 0;
        if (budget > 0) { int wd = (blockedDir - 1) & 7, wx = curX, wy = curY;
          while (cntB < budget) {
              int sd = pfWallFollow(wd, wx, wy, 1);
              dirB[cntB] = sd;
              if (wy == cy1 && wd == 5) break;
              if (wy == cy2 && wd == 1) break;
              if (wx == cx1 && wd == 3) break;
              if (wx == cx2 && wd == 7) break;
              posBx[cntB] = wx; posBy[cntB] = wy; ++cntB;
          }
        }

        // Avalia os dois contornos e a distância direta.
        int costA, idxA, costB, idxB;
        evalForward(dirA, useA, posAx, posAy, costA, idxA);   // sub_43FF20
        evalForward(dirB, cntB, posBx, posBy, costB, idxB);
        int distDirect = pfOctDistToTarget(curX, curY);       // sub_43FCD0

        // Seleção: menor custo; empate → prefere A se idxA < idxB.
        bool chooseB;
        if (costB > costA) chooseB = false;
        else if (costB < costA) chooseB = true;
        else chooseB = !(idxA < idxB);

        int endX = curX, endY = curY;
        if (chooseB) {
            if (distDirect > costB) {                          // usa contorno B
                endX = posBx[idxB]; endY = posBy[idxB];
                pathLen = appendContour(dirB, idxB, outDirs, pathLen, maxDirs);
            }
        } else {
            if (distDirect > costA) {                          // usa contorno A
                endX = posAx[idxA]; endY = posAy[idxA];
                pathLen = appendContour(dirA, idxA, outDirs, pathLen, maxDirs);
            }
        }

        // loc_43FB2E: sem progresso → encerra; senão marca failedOnce e segue.
        if (curX == endX && curY == endY) {
            if (reached) *reached = (curX == ctx.destX && curY == ctx.destY) ? 1 : 0;
            return pathLen;
        }
        failedOnce = 1;                                        // desvio normal (eax==0)
        curX = endX; curY = endY;
    }
}

StepChoice pfChooseStep(int unitId, int currentDir, int avoidDir) {
    StepChoice best;
    if (unitId <= 0 || unitId >= static_cast<int>(g_units.size())) return best;
    const UnitRecord& u = g_units[unitId];

    int back = (currentDir + 4) & 7;       // var_2C: direção de trás (não voltar)
    int curX = u.pixelX >> 5;              // [+0x14] >> 5
    int curY = u.pixelY >> 5;              // [+0x18] >> 5

    pfPush();                              // sub_438180
    pfSetContext(unitId);                  // sub_438260

    int bestCost = PF_BLOCKED;             // var_28
    int ebx = back;
    for (;;) {
        ebx = (ebx + 1) & 7;               // próxima direção
        if (ebx == back) break;            // tentou todas menos a de trás
        int newX = (curX + kDir8[ebx].dx) & pfMaskX();  // wrap em X
        int newY = curY + kDir8[ebx].dy;
        if (newY <= 0) newY = 0;
        if (newY >= g_gameMap.height()) newY = g_gameMap.height() - 1;

        int cost = pfMoveCost(newX, newY); // sub_438390
        if (ebx == avoidDir) {             // breadcrumb → penaliza (3*cost - sign) >> 1
            int32_t t = static_cast<int32_t>(static_cast<uint32_t>(cost) * 3u);  // wrap 32-bit como o asm
            t -= (t < 0 ? -1 : 0);         // sub eax, edx (edx = sign)
            cost = t >> 1;                 // sar eax, 1 (aritmético)
        }
        if (cost < bestCost) {
            bestCost = cost;
            best.dir = ebx;
            best.x = newX;
            best.y = newY;
        }
    }

    pfPop();                               // sub_438200

    if (bestCost != PF_BLOCKED) {
        best.found = true;
        best.cost = bestCost;
    }
    return best;
}

int pfMoveCost(int x, int y) {         // sub_438390
    if (y < 0 || y >= g_gameMap.height()) return PF_BLOCKED;
    uint32_t cell = pfCell(x, y);

    int base;
    // Modo 3 (sub_438850): parede/tubo bloqueia sem exceção.
    if (g_pf.goalMode == 3 && (cell & GameMap::CELL_WALLBITS)) return PF_BLOCKED;
    if (cell & GameMap::CELL_WALLBITS) {              // 0xA8000000
        int gate;
        if (cell & GameMap::CELL_HARDBLOCK) {         // 0x28000000
            gate = 0;
        } else {
            int ct = static_cast<int>(cell & 0x1F);
            gate = (ct >= GameMap::TUBE_CELLTYPE_LO && ct <= GameMap::TUBE_CELLTYPE_HI)
                   ? g_pf.tubeCost : 0;
        }
        if (gate == 0) return PF_BLOCKED;
        base = 0x8000;                                // custo-base fixo de parede/tubo
    } else {
        int ct = static_cast<int>(cell & 0x1F);
        base = terrainMoveCost(ct, g_pf.moveClass);
        if (base == 0) return PF_BLOCKED;
    }

    // Célula-alvo → custo negativo (prioridade).
    bool goal = x == g_pf.targetX && y == g_pf.targetY;
    if (!goal && (g_pf.goalMode == 1 || g_pf.goalMode == 2)) {   // sub_4384D0 / sub_438660
        const int dx = pfWrapDX(g_pf.target2X - x), dy = g_pf.target2Y - y;
        if (dx * dx + dy * dy <= g_pf.radius2) {
            if (g_pf.goalMode == 1 || (dx == 0 && dy == 0)) goal = true;
            else goal = mapLineOfSight(x, y, g_pf.target2X, g_pf.target2Y) != 0;   // sub_436EA0
        }
    }
    if (!goal && g_pf.goalMode == 3) {                // ao lado do alvo (4 vizinhos)
        const int dy = g_pf.target2Y - y, dx = pfWrapDX(x - g_pf.target2X);
        if (dy == -1 || dy == 1) goal = dx == 1;
        else if (dy == 0) goal = dx == 0 || dx == 2;
    }
    if (goal) base = -base;

    uint32_t occ = cell & GameMap::CELL_OCCUPANCY;
    if (occ == 0) return base;
    int occId = static_cast<int>(occ >> GameMap::CELL_OCC_SHIFT);
    if (occId == g_pf.unitId) return base;            // própria unidade

    if (occId <= 0 || occId >= static_cast<int>(g_units.size())) return base;
    const UnitRecord& o = g_units[occId];
    if (o.id == -1) return base;                      // morto
    if (o.flags & UNIT_DEAD) return base;             // 0x20000
    if (!(o.flags & UNIT_VEHICLE)) return PF_BLOCKED; // prédio → bloqueia

    if (!pfOccupantPassable(occId, g_pf.unitId)) return PF_BLOCKED;  // inimigo/ocupado
    if (o.field22 == 0) return base + base;           // veículo parado → ×2
    return base * 4;                                  // veículo movendo → ×4
}

// ---- Planejador de rota (sub_4401F0) -----------------------------------------

namespace {

int pmMask() { return pfMaskX(); }                // dword_54EBC4
int pmW()    { return pfW(); }                    // dword_54EBC8
int pmH()    { return g_gameMap.height(); }       // dword_54EBD0

// Corredor de busca (var_C24 de sub_440660 / sub_440C50): por linha, intervalos
// [x1, x2] em ordem de x; o nó-sentinela do original tem x1 = x2 = 0x7FFFFFFF.
struct Corridor {
    struct Span { int x1, x2, base; };
    std::vector<std::vector<Span>> rows;
    explicit Corridor(int h) : rows(h > 0 ? static_cast<size_t>(h) : 0) {}

    // sub_440A30: acha o 1º intervalo com x2 >= x1; se ele começa até x2, estica
    // (min/max); senão insere antes dele. Depois engole os seguintes que começam
    // até o novo x2.
    void add(int row, int x1, int x2) {
        if (row < 0 || row >= static_cast<int>(rows.size())) return;
        std::vector<Span>& v = rows[static_cast<size_t>(row)];
        size_t i = 0;
        while (i < v.size() && v[i].x2 < x1) ++i;
        if (i < v.size() && v[i].x1 <= x2) {
            if (v[i].x1 >= x1) v[i].x1 = x1;
            if (v[i].x2 <= x2) v[i].x2 = x2;
        } else {
            v.insert(v.begin() + static_cast<long>(i), Span{x1, x2, 0});
        }
        while (i + 1 < v.size() && v[i + 1].x1 <= v[i].x2) {
            if (v[i + 1].x2 > v[i].x2) v[i].x2 = v[i + 1].x2;
            v.erase(v.begin() + static_cast<long>(i) + 1);
        }
    }
    // sub_441250 (início): numeração das células, linha a linha.
    int number() {
        int base = 0;
        for (auto& v : rows)
            for (Span& s : v) { s.base = base; base += s.x2 - s.x1 + 1; }
        return base;
    }
    int cell(int x, int y) const {
        if (y < 0 || y >= static_cast<int>(rows.size())) return -1;
        const std::vector<Span>& v = rows[static_cast<size_t>(y)];
        size_t i = 0;
        while (i < v.size() && v[i].x2 < x) ++i;
        if (i == v.size() || x < v[i].x1) return -1;
        return v[i].base + x - v[i].x1;
    }
};

// Meia-largura das linhas do borrão (unk_4D0518), k = 1..8.
const int kBlobW[8] = {8, 8, 8, 7, 7, 6, 5, 4};

// Borrão em volta de (cx, cy): linhas cy+k−1 e cy−k com [cx−w, cx+w−1],
// cortando na emenda da largura (trecho repetido em sub_440660 e sub_440C50).
void addBlob(Corridor& C, int cx, int cy) {
    const int W = pmW(), mask = pmMask(), H = pmH();
    for (int k = 1; k <= 8; ++k) {
        const int w = kBlobW[k - 1];
        const int rowsK[2] = {cy + k - 1, cy - k};
        for (int row : rowsK) {
            if (row < 0 || row >= H) continue;
            int l = cx - w, r = cx + w - 1;
            if (l < 0) {
                if (r > 0) { C.add(row, l & mask, W - 1); l = 0; }
                else { r &= mask; l &= mask; }
            }
            if (r > W - 1) {
                if (l < W) { C.add(row, 0, r & mask); r = W - 1; }
                else { r &= mask; l &= mask; }
            }
            C.add(row, l, r);
        }
    }
}

// sub_441250: busca de menor custo dentro do corredor, de (sx, sy) até uma
// célula-objetivo (pfMoveCost < 0). Custo do passo = média dos custos das duas
// células (×723/512 na diagonal); estimativa = 576·(maior + menor/2) até
// (hx, hy). A lista aberta é a lista dupla do original, em ordem decrescente de
// chave, com o "dedo" de inserção (var_C004) — os empates saem na mesma ordem.
int corridorSearch(Corridor& C, int sx, int sy, int hx, int hy, std::vector<int>& out,
                   int maxSteps, int& reached) {
    const int mask = pmMask(), H = pmH();
    const int total = C.number();
    struct Rec { int state = 0, dir = 0, g = 0, cost = 0, h = 0, depth = 0, parent = 0, node = -1; };
    struct ONode { int key, x, y, cell, skip, prev, next; };
    const int si = C.cell(sx & mask, sy);
    if (si < 0) { reached = 0; return -1; }
    std::vector<Rec> cells(static_cast<size_t>(total));
    std::vector<ONode> on;
    on.push_back({0x7FFFFFFF, 0, 0, 0, 0, -1, 1});            // var_C03C
    on.push_back({static_cast<int>(0x80000000), 0, 0, 0, 0, 0, -1});   // var_C020
    int cursor = 1;                                            // var_C004
    auto insert = [&](int n) {
        const int k = on[static_cast<size_t>(n)].key;
        int p = cursor;
        if (on[static_cast<size_t>(p)].key > k) {
            while (on[static_cast<size_t>(on[static_cast<size_t>(p)].next)].key > k) p = on[static_cast<size_t>(p)].next;
        } else if (on[static_cast<size_t>(p)].key < k) {
            do p = on[static_cast<size_t>(p)].prev; while (on[static_cast<size_t>(p)].key < k);
        }
        ONode& nn = on[static_cast<size_t>(n)];
        nn.next = on[static_cast<size_t>(p)].next;
        nn.prev = p;
        on[static_cast<size_t>(p)].next = n;
        on[static_cast<size_t>(nn.next)].prev = n;
        cursor = n;
    };
    auto unlink = [&](int n) {
        if (cursor == n) cursor = on[static_cast<size_t>(n)].next;
        const int pv = on[static_cast<size_t>(n)].prev, nx = on[static_cast<size_t>(n)].next;
        on[static_cast<size_t>(pv)].next = nx;
        on[static_cast<size_t>(nx)].prev = pv;
    };
    Rec& s = cells[static_cast<size_t>(si)];
    s.state = 1; s.g = 0; s.cost = pfMoveCost(sx, sy); s.depth = -1;
    on.push_back({0, sx, sy, si, 9, 0, 0});
    insert(static_cast<int>(on.size()) - 1);
    int cur;
    for (;;) {
        const int n = on[1].prev;
        if (n == 0) { reached = 0; return 0; }
        if (cursor == n) cursor = on[static_cast<size_t>(n)].next;
        const ONode node = on[static_cast<size_t>(n)];
        on[1].prev = node.prev;
        on[static_cast<size_t>(node.prev)].next = 1;
        cur = node.cell;
        if (cells[static_cast<size_t>(cur)].state == 4) break;
        cells[static_cast<size_t>(cur)].state = 2;
        for (int d = 0; d < 8; ++d) {
            if (d == node.skip) continue;
            const int nx = (node.x + kDir8[d].dx) & mask, ny = node.y + kDir8[d].dy;
            if (ny < 0 || ny >= H) continue;
            const int ci = C.cell(nx, ny);
            if (ci < 0) continue;
            Rec& r = cells[static_cast<size_t>(ci)];
            const Rec& c = cells[static_cast<size_t>(cur)];
            int key;
            if (r.state == 0) {
                const int cost = pfMoveCost(nx, ny);
                r.cost = cost;
                if (cost == PF_BLOCKED) { r.state = 2; continue; }
                if (cost < 0) { r.state = 4; r.cost = -cost; }
                else r.state = 1;
                int step = (c.cost + r.cost) / 2;
                if (d & 1) step = step * 723 / 512;
                r.g = c.g + step;
                int ax = pfWrapDX(nx - hx), ay = hy - ny;
                if (ax < 0) ax = -ax;
                if (ay < 0) ay = -ay;
                const int mx = ax < ay ? ay : ax, mn = ax < ay ? ax : ay;
                r.h = ((mx + mn / 2) * 9 * 512) / 8;
                key = r.g + r.h;
            } else {
                if (r.cost == PF_BLOCKED) continue;
                int step = (r.cost + c.cost) / 2;
                if (d & 1) step = step * 723 / 512;
                const int ng = c.g + step;
                if (r.g <= ng) continue;
                r.g = ng;
                key = ng + r.h;
                if ((r.state & 5) && r.node >= 0) unlink(r.node);
                else r.state = 1;
            }
            r.depth = cells[static_cast<size_t>(cur)].depth + 1;
            r.dir = d;
            r.parent = cur;
            on.push_back({key, nx, ny, ci, (d + 4) & 7, 0, 0});
            r.node = static_cast<int>(on.size()) - 1;
            insert(r.node);
        }
    }
    reached = 1;
    while (cells[static_cast<size_t>(cur)].depth >= maxSteps) cur = cells[static_cast<size_t>(cur)].parent;
    const int steps = cells[static_cast<size_t>(cur)].depth + 1;
    out.assign(static_cast<size_t>(steps), 0);
    for (;;) {
        const Rec& r = cells[static_cast<size_t>(cur)];
        out[static_cast<size_t>(r.depth)] = r.dir;
        cur = r.parent;
        if (cells[static_cast<size_t>(cur)].depth < 0) break;
    }
    return steps;
}

// sub_440BA0 (objetivo exato a dist² <= 4): passos pela direção do delta que
// falta. O teste de entrada é sempre a partir da ORIGEM (o original não avança a
// posição) — mantido.
int shortSearch(int sx, int sy, int dx, int dy, std::vector<int>& out, int maxSteps, int& reached) {
    const int mask = pmMask();
    out.clear();
    while ((dx || dy) && maxSteps > static_cast<int>(out.size())) {
        const int d = pfDirFromDelta(dx, dy);
        if (!pfCanEnter((sx + kDir8[d].dx) & mask, sy + kDir8[d].dy)) { reached = 0; out.clear(); return 0; }
        out.push_back(d);
        dx -= kDir8[d].dx;
        dy -= kDir8[d].dy;
    }
    reached = 1;
    return static_cast<int>(out.size());
}

// sub_440660 (dist² <= 0x240 com refinamento): corredor = borrão a cada dois
// pontos da reta de Bresenham até o alvo; depois sub_441250 até 0x258 passos.
int mediumSearch(int sx, int sy, int dx, int dy, int destX, int destY, std::vector<int>& out, int& reached) {
    Corridor C(pmH());
    const int ax2 = 2 * (dx < 0 ? -dx : dx), ay2 = 2 * (dy < 0 ? -dy : dy);
    bool xMajor;
    int minor, major, majorLen2, minorLen2, minorD, majorD;
    if (ay2 <= ax2) {
        xMajor = true; minor = sy; major = 0; majorLen2 = ax2; minorLen2 = ay2; minorD = dy; majorD = dx;
    } else {
        xMajor = false; major = sy; minor = 0; majorLen2 = ay2; minorLen2 = ax2; minorD = dx; majorD = dy;
    }
    const int minorStep = minorD > 0 ? 1 : -1, majorStep = majorD > 0 ? 1 : -1;
    int err = -(minorLen2 / 2 + majorLen2 / 2);
    bool blob = true;
    for (;;) {
        const int xo = xMajor ? major : minor, cy = xMajor ? minor : major;
        err += minorLen2;
        if (err >= 0) { err -= majorLen2; minor += minorStep; }
        major += majorStep;
        const int cx = (sx + xo) & pmMask();
        if (blob) addBlob(C, cx, cy);
        blob = !blob;
        if (cx == destX && cy == destY) break;
    }
    return corridorSearch(C, sx, sy, destX, destY, out, 0x258, reached);
}

// sub_441160: até 64 direções do cache da busca longa viram a rota; para na
// 1ª célula-objetivo (e o cache acaba ali).
void plainSegment(UnitRecord& u) {
    const int count = static_cast<int>(u.routeCache.size());
    const int end = u.cacheIdx + 0x40 < count ? u.cacheIdx + 0x40 : count;
    u.routeDirs.clear();
    while (u.cacheIdx < end) {
        const int d = u.routeCache[static_cast<size_t>(u.cacheIdx)];
        u.cacheX = (u.cacheX + kDir8[d].dx) & pmMask();
        u.cacheY += kDir8[d].dy;
        u.routeDirs.push_back(d);
        ++u.cacheIdx;
        if (pfMoveCost(u.cacheX, u.cacheY) < 0) {
            u.routeCache.resize(static_cast<size_t>(u.cacheIdx));
            break;
        }
    }
}

// sub_440C50: refaz o próximo trecho (48 direções do cache) com sub_441250
// num corredor em volta das posições ímpares; as pares só conferem a entrada.
// Devolve o nº de passos ou −1 (o chamador descarta o cache e busca de novo).
int refineSegment(UnitRecord& u, int sx, int sy) {
    const int count = static_cast<int>(u.routeCache.size());
    if (count <= 2) { plainSegment(u); return static_cast<int>(u.routeDirs.size()); }
    Corridor C(pmH());
    const int end = u.cacheIdx + 0x30 < count ? u.cacheIdx + 0x30 : count;
    while (u.cacheIdx < end) {
        const int i = u.cacheIdx, d = u.routeCache[static_cast<size_t>(i)];
        u.cacheX = (u.cacheX + kDir8[d].dx) & pmMask();
        u.cacheY += kDir8[d].dy;
        if (!(i & 1)) {
            if (!pfCanEnter(u.cacheX, u.cacheY)) { u.routeDirs.clear(); return -1; }
        } else {
            addBlob(C, u.cacheX, u.cacheY);
        }
        ++u.cacheIdx;
    }
    u.routeDirs.clear();
    if (u.cacheX == sx && u.cacheY == sy) return 0;
    pfSetTarget(u.cacheX, u.cacheY);                         // sub_438290
    std::vector<int> out;
    int reached = 0;
    int n = corridorSearch(C, sx, sy, u.cacheX, u.cacheY, out, 0x40, reached);
    if (!reached) return -1;
    if (u.cacheIdx != count && n >= 12) n -= 6;
    u.routeDirs.assign(out.begin(), out.begin() + n);
    return n;
}

} // namespace

int pfPlanRoute(int id, int sx, int sy, int destX, int destY, int mode, int dist) {
    UnitRecord& u = g_units[static_cast<size_t>(id)];
    const int mask = pmMask();
    pfPush();                                                // sub_438180
    pfSetContext(id);                                        // sub_438260
    const bool shortOk = mode == 0;
    bool refine = g_playerData[u.owner & 0x0F].pathRefine != 0;   // [jogador+0x1B8]
    if (mode == 3) refine = true;
    pfSetGoal(mode, destX, destY, dist);
    const int dx = pfWrapDX(destX - sx), dy = destY - sy;    // [+0x20]/[+0x24]
    u.routeDirs.clear();
    u.routeIdx = 0;
    int reached = 0;
    if (pfMoveCost(sx, sy) < 0) { pfPop(); return 1; }       // já no objetivo
    bool any = false;
    for (int d = 0; d < 8 && !any; ++d) any = pfCanEnter((sx + kDir8[d].dx) & mask, sy + kDir8[d].dy) != 0;
    if (!any) { pfPop(); return 0; }
    for (int guard = 0;; ++guard) {
        if (u.cacheIdx >= static_cast<int>(u.routeCache.size())) {      // loc_440393
            u.routeCache.clear();
            u.cacheIdx = 0;
            int d2 = dx * dx + dy * dy;
            if (mode == 0) {
                int k = 0;
                while (k < 8 && !pfCanEnter((destX + kDir8[k].dx) & mask, destY + kDir8[k].dy)) ++k;
                if (k == 8 || !pfCanEnter(destX, destY)) d2 = 0x7FFFFFFF;
            }
            reached = 0;
            std::vector<int> out;
            if (shortOk && d2 <= 4) {
                shortSearch(sx, sy, dx, dy, out, 0x258, reached);
                u.routeDirs = out;
                if (reached) break;
            }
            if (refine && d2 <= 0x240) {
                mediumSearch(sx, sy, dx, dy, destX, destY, out, reached);
                u.routeDirs = out;
                if (reached) break;
            }
            static int dirs[0x258];
            SearchContext sc;
            sc.unitId = id; sc.startX = sx; sc.startY = sy; sc.destX = destX; sc.destY = destY;
            const int n = pfSearchRoute(sc, dirs, 0x258, &reached);   // sub_43F5B0
            u.routeDirs.clear();
            if (n <= 0) break;
            u.routeCache.assign(dirs, dirs + n);
            u.cacheX = sx;
            u.cacheY = sy;
        }
        if (!refine) { plainSegment(u); break; }             // sub_441160
        if (refineSegment(u, sx, sy) >= 0) break;            // sub_440C50
        u.routeCache.clear();                                // [+0xBC] = 0 → loc_440393
        u.cacheIdx = 0;
        // Guarda do port: o original repete sem limite (a mesma busca dá o
        // mesmo cache); aqui, depois de 8 tentativas, fica sem rota.
        if (guard >= 8) { u.routeDirs.clear(); break; }
    }
    pfPop();                                                 // sub_438200
    static const bool trace = std::getenv("OP2_PFTRACE") != nullptr;
    if (trace)
        std::printf("[PF] u%d (%d,%d)->(%d,%d) m%d ref%d passos=%d cache=%d/%d chegou=%d\n", id, sx, sy, destX, destY,
                    mode, refine ? 1 : 0, static_cast<int>(u.routeDirs.size()), u.cacheIdx,
                    static_cast<int>(u.routeCache.size()), reached);
    return reached;
}

} // namespace op2
