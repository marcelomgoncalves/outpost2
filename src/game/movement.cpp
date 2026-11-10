#include "movement.h"
#include "salvage.h"
#include "trigger.h"

#include <algorithm>
#include <cstdlib>

#include "unit.h"
#include "game_map.h"
#include "player.h"
#include "tethys_game.h"
#include "pathfinder.h"
#include "command.h"
#include "terrain_cost.h"
#include "building_sheet.h"
#include "structure.h"
#include "construction.h"
#include "mining.h"

namespace op2 {

// Aplica 1 tick do passo em curso — sub_44C0B0 loc_44C27D: posição += vel
// (16.16, fração em 16 bits com carry), [st+0x64]--, e o giro: se [st+0x68]
// > 0, [st+0x68]-- e rotação += [st+0x60].
static void applyStepTick(UnitRecord& u) {
    const int totalW = g_gameMap.width() * 32;
    uint32_t fx = static_cast<uint32_t>(u.fracX) + static_cast<uint16_t>(u.velX & 0xFFFF);
    u.fracX = static_cast<uint16_t>(fx);
    int px = u.pixelX + (u.velX >> 16) + static_cast<int>(fx >> 16);
    u.pixelX = ((px % totalW) + totalW) % totalW;            // and dword_54EBBC (wrap)
    uint32_t fy = static_cast<uint32_t>(u.fracY) + static_cast<uint16_t>(u.velY & 0xFFFF);
    u.fracY = static_cast<uint16_t>(fy);
    u.pixelY += (u.velY >> 16) + static_cast<int>(fy >> 16);
    --u.stepTicks;
    if (u.turnTicks) {
        --u.turnTicks;
        u.rotation = static_cast<uint8_t>(u.rotation + u.turnStep);
    }
}

// loc_44B8F0 de sub_44B650: a unidade do COMPUTADOR que esbarra num tubo ou
// numa parede passa a ATIRAR NELA em vez de simplesmente parar. É assim que a
// IA abre caminho pelas muralhas, e é o único lugar do jogo que liga o flag
// 0x2000 ("atirar no chão") de um veículo — não existe ordem do jogador para
// isso, apesar do nome.
//
// As condições são todas do original, nesta ordem:
//   test ebx, 2FFF0000h   a célula de destino não pode ter ocupante nem
//                         bloqueio duro (= CELL_OCCUPANCY | CELL_HARDBLOCK):
//                         o que impede a passagem tem de ser a parede;
//   [Player+0x28] == 0    só jogador NÃO humano (isHuman);
//   [+0x44] & 0x10        veículo armado;
//   [+0x24] 3Dh ou 3Eh    e a arma é Laser ou Microwave — justamente as duas
//                         que levam 2,5x de dano contra parede em sub_4A23F0;
//   cellType 17h..19h     o destino é tubo/parede;
//   [+0x4E] == 7FFFh      e a unidade ainda não tem alvo.
// Com tudo isso, [+0x4C]/[+0x4E] recebem o TILE (não um id de unidade) e o
// flag liga; quem dispara é o weapon.cpp, que já trata alvo de tile.
void wallBlockTarget(UnitRecord& u, int nx, int ny) {
    const uint32_t cell = g_gameMap.rawCell(nx, ny);
    if (cell & (GameMap::CELL_OCCUPANCY | GameMap::CELL_HARDBLOCK)) return;
    const int p = u.owner & 0x0F;
    if (p < 0 || p >= kMaxPlayers || g_playerData[p].isHuman) return;
    if (!(u.flags & UNIT_ARMED)) return;
    if (u.cargo != 0x3D && u.cargo != 0x3E) return;
    const int ct = static_cast<int>(cell & 0x1F);
    if (ct < GameMap::TUBE_CELLTYPE_LO || ct > GameMap::TUBE_CELLTYPE_HI) return;
    if (u.field4E != 0x7FFF || (u.flags & UNIT_FIRE_GROUND)) return;
    u.flags |= UNIT_FIRE_GROUND;
    u.target4C = static_cast<int16_t>(nx);
    u.field4E = static_cast<int16_t>(ny);
}

// Trava do empurrao: no original quem e empurrado recebe uma ROTA (vt[0x7C] =
// sub_44C350, que chama o planejador sub_4401F0) e anda no proprio tique --
// nunca empurra ninguem de dentro de um empurrao. O porte da o passo na hora
// (unitStepToward), entao esta trava reproduz isso: quem esta sendo empurrado
// nao empurra.
static int s_empurrando = 0;

// sub_44BBA0: pede a `blockerId` que saia da frente de `pusherId`, que tentava
// andar na direcao `dir` e achou a celula dele ocupada.
//
// Escolhe, entre as 7 vizinhas do bloqueador -- todas menos a que volta para
// quem empurra, (dir + 4) & 7 --, a de menor custo de terreno (sub_438390, com
// o contexto do BLOQUEADOR), penalizando em 1,5x a direcao do PROXIMO trecho de
// quem empurra (o `edi` ali e quem empurra: routeDirs[idx+1] dele). Assim o
// bloqueador nao e mandado para dentro do caminho que vem a seguir.
//
// Corrigido em relacao ao original: o custo de celula bloqueada e 0x7FFFFFFF,
// e `custo * 3 / 2` em 32 bits da a volta para ~0x3FFFFFFE -- uma celula
// INTRANSITAVEL na direcao penalizada podia ganhar. Aqui bloqueada continua
// bloqueada.
//
// So o ramo do bloqueador PARADO (sub-estado 0) esta portado. Se ele estiver
// andando ([+0x22] == 1) e vindo de frente, o original insere um ponto de
// passagem na frente da ordem dele (sub_44BFD0); esse ramo depende de partes
// do bloco de ordem ainda nao decodificadas (com o indice em 0 a escrita cai
// em [ordem+0x10]) e fica de fora -- quem empurra espera e, em 20, replaneja.
static void pushAside(int pusherId, int dir, int blockerId) {
    if (blockerId <= 0 || blockerId >= static_cast<int>(g_units.size())) return;
    UnitRecord& b = g_units[blockerId];
    if (b.id == -1 || (b.flags & UNIT_DEAD)) return;
    if (b.field22 != 0) return;                  // andando (sub_44BFD0) ou ocupado
    const UnitRecord& p = g_units[pusherId];
    const int oposta = (dir + 4) & 7;            // [esp+var_2C]
    // routeDirs[idx+1] de quem empurra ([ecx+eax+7Dh]), se houver.
    const int proxima = (p.routeIdx + 1 < static_cast<int>(p.routeDirs.size()))
                            ? (p.routeDirs[p.routeIdx + 1] & 7) : -1;
    const int mask = g_gameMap.internalWidth() - 1;   // dword_54EBC4
    const int H = g_gameMap.height();                 // dword_54EBD0
    const int cx = b.pixelX >> 5, cy = b.pixelY >> 5;
    pfPush();                                    // sub_438180
    pfSetContext(blockerId);                     // sub_438260
    int melhor = PF_BLOCKED, mx = 0, my = 0;
    for (int k = 1; k < 8; ++k) {
        const int d = (oposta + k) & 7;
        const int nx = (cx + kDir8[d].dx) & mask;
        const int ny = std::min(std::max(cy + kDir8[d].dy, 0), H - 1);
        int custo = pfMoveCost(nx, ny);          // sub_438390
        if (d == proxima && custo != PF_BLOCKED) custo = custo * 3 / 2;
        if (custo < melhor) { melhor = custo; mx = nx; my = ny; }
    }
    pfPop();                                     // sub_438200
    if (melhor == PF_BLOCKED) return;
    ++s_empurrando;
    unitStepToward(blockerId, mx, my);           // vt[0x7C] (sub_44C350)
    --s_empurrando;
    b.field22 = 1;                               // incondicional, como no original
}

// Monta o passo na direção `dir` — sub_44B650. Retorna false se o passo não
// pode ser dado (a rota é descartada e recalculada).
static bool setupStep(int id, int dir) {
    UnitRecord& u = g_units[id];
    // Máscara da largura interna (dword_54EBC4): nos mapas < 512 a coluna
    // vizinha da borda cai fora do mapa e pfCanEnter a recusa (tipo 6).
    const int mask = g_gameMap.internalWidth() - 1;
    const int curX = u.pixelX >> 5, curY = u.pixelY >> 5;
    const int nx = (curX + kDir8[dir].dx) & mask;
    const int ny = curY + kDir8[dir].dy;
    // Limites jogáveis dword_54EBD4..E0 (em y; em x a borda é o tipo 6).
    const MAP_RECT& clip = g_gameMap.clipRect();
    if (ny < clip.y1 || ny > clip.y2) return false;

    const VehPlayerStats* vs = vehicleSheet(u.type, u.owner & 0x0F);

    // Giro: diff = (dir<<5) - rotação (byte com sinal). Passo de giro = Turn_Rate
    // ([desc+0x30+p*0x44]); [st+0x68] = |diff|/passo (1 se |diff| <= passo).
    const uint8_t target = static_cast<uint8_t>(dir << 5);
    const int8_t diff = static_cast<int8_t>(target - u.rotation);
    if (diff != 0) {
        int rate = vs ? vs->turnRate : 0x20;      // (dword_56DE68 força 0x20)
        if (rate <= 0) rate = 0x20;
        int ad = diff < 0 ? -diff : diff;
        if (ad <= rate) { rate = ad; u.turnTicks = 1; }
        else u.turnTicks = ad / rate;
        u.turnStep = static_cast<int8_t>(diff > 0 ? rate : -rate);
        if (ad > 0x10) {                         // só gira: sem deslocamento
            u.velX = u.velY = 0;
            u.stepTicks = u.turnTicks;
            return true;
        }
    } else {
        u.turnStep = 0; u.turnTicks = 0;
    }

    // Célula de destino ocupada por OUTRA unidade: o original nunca entra
    // (`test ebx, 0AFFF0000h` -- so anda sem ocupante e sem parede). Com
    // parede e ocupante, desiste; sem parede, se da para empurrar (sub_44BD60),
    // empurra (sub_44BBA0) e ESPERA -- velocidade zero, a rota fica, e no tique
    // seguinte tenta a mesma direcao. Em 20 tentativas ([st+0x6C]) desiste e
    // replaneja (loc_44B998). O porte usava o pfCanEnter aqui -- que diz se o
    // PLANEJADOR pode passar -- e entrava na celula ocupada, sobrescrevendo a
    // ocupacao: dois veiculos no mesmo tile.
    {
        const uint32_t dcell = g_gameMap.rawCell(nx, ny);
        const int occId = static_cast<int>((dcell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
        if (occId != 0 && occId != id) {
            if (dcell & GameMap::CELL_WALLBITS) return false;     // loc_44B8F0 -> loc_44B998
            if (s_empurrando || !pfOccupantPassable(occId, id)) return false;   // loc_44B998
            pushAside(id, dir, occId);                            // sub_44BBA0
            if (u.pushWait++ >= 20) return false;                 // loc_44B998
            u.velX = u.velY = 0;                                  // [st+0x58] = [st+0x5C] = 0
            u.stepTicks = 1;                                      // um tique parado, e tenta de novo
            return true;
        }
    }
    pfSetContext(id);
    if (!pfCanEnter(nx, ny)) {
        wallBlockTarget(u, nx, ny);    // parede no caminho: a IA atira nela
        return false;
    }
    const int srcCT = g_gameMap.getCellType(LOCATION{curX, curY});
    const int dstCT = g_gameMap.getCellType(LOCATION{nx, ny});
    const int dstCost = terrainMoveCost(dstCT, u.moveClass);
    if (dstCost == 0) return false;                             // loc_44B9B3

    // Transfere a ocupação para a célula de destino já no início do passo.
    g_gameMap.clearUnitOccupancy(curX, curY);
    g_gameMap.setUnitOccupancy(nx, ny, id);
    u.fracX = u.fracY = 0x8000;
    u.stepDestX = nx << 5;
    u.stepDestY = ny << 5;

    // Duração (ticks) = Move_Speed (x sqrt2 = 724/512 na diagonal)
    //                   * (custo(origem) + custo(destino)) >> 10.
    int ms = vs ? vs->moveSpeed : 1;
    if (dir & 1) {
        int t = ms * 724;
        ms = (t + ((t >> 31) & 0x1FF)) >> 9;
    }
    int ticks = (ms * (terrainMoveCost(srcCT, u.moveClass) + dstCost)) >> 10;
    // + escuridão (sub_44B650): sem faróis ([+0x44] bit 0) e sem "luz em toda
    //   parte" (dword_56DEA4), o passo custa mais na proporção da escuridão da
    //   COLUNA da unidade — byte_54EC14[(pixelX>>5) - dword_54729C & máscara],
    //   0..31, ou seja até ~2x mais lento no meio da noite.
    if (!(u.flags & 1) && !TethysGame::daylightEverywhere()) {
        const int dark = g_gameMap.lightDarkness((u.pixelX >> 5) + g_gameMap.internalXOffset()
                                                 - TethysGame::daylightPos());
        ticks += (dark * ticks) >> 5;
    }
    // + dano: ticks += dano*ticks / HPmáx.
    if (vs && vs->hitPoints > 0) ticks += (u.damage * ticks) / vs->hitPoints;
    if (TethysGame::cheatFlag(4)) ticks = 2;        // dword_56DE68 (unidades rápidas)
    if (ticks < 2) ticks = 2;
    u.stepTicks = ticks;

    // Velocidade 16.16: 32 px em `ticks` ticks → 0x200000/ticks + 1.
    const int v = 0x200000 / ticks + 1;
    u.velX = (dir == 0 || dir == 1 || dir == 7) ? v : (dir >= 3 && dir <= 5) ? -v : 0;
    u.velY = (dir >= 5) ? -v : (dir >= 1 && dir <= 3) ? v : 0;
    u.curDir = static_cast<uint8_t>(dir);
    return true;
}

bool unitStepToward(int id, int tx, int ty) {
    const UnitRecord& u = g_units[id];
    const int mask = g_gameMap.width() - 1;
    const int cx = u.pixelX >> 5, cy = u.pixelY >> 5;
    for (int d = 0; d < 8; ++d)
        if (((cx + kDir8[d].dx) & mask) == (tx & mask) && cy + kDir8[d].dy == ty) return setupStep(id, d);
    return false;
}

// sub_44C2F0: objetivo da busca p/ o waypoint atual [ordem+8]. O último usa o
// modo/distância do bloco ([ordem+0x3C]/[+0x40]); os intermediários usam os
// bits 29-31 do ponto (0 = célula exata; n>0 = modo 1, chegar a até n tiles).
static void waypointGoal(const OrderBlock& ob, int& x, int& y, int& mode, int& dist) {
    const uint32_t wp = ob.waypoints[ob.curWaypoint];
    x = unpackLocX(wp); y = unpackLocY(wp);
    if (ob.curWaypoint == ob.countMinus1) { mode = ob.multiFlag; dist = ob.field40; }
    else if (wp & 0xE0000000u) { mode = 1; dist = static_cast<int>(wp >> 29); }
    else { mode = 0; dist = 0; }
}

// sub_44BEC0: pula os waypoints (antes do último) que já estão a até n tiles
// (n = bits 29-31; distância de Chebyshev em pixels/32, x com wrap) da unidade.
static bool waypointAdvance(UnitRecord& u) {
    OrderBlock& ob = u.order;
    bool advanced = false;
    const int W = g_gameMap.ok() ? g_gameMap.width() * 32 : 0;
    while (ob.countMinus1 > ob.curWaypoint) {
        const uint32_t wp = ob.waypoints[ob.curWaypoint];
        // pixelX/Y do port = canto do tile (o original guarda o centro, +16).
        int dx = u.pixelX - static_cast<int>(wp & 0x7FE0u);
        if (W) { if (dx < -W / 2) dx += W; else if (dx > W / 2) dx -= W; }
        const int dy = u.pixelY - static_cast<int>((wp & 0x1FF00000u) >> 15);
        const int d = std::max(std::abs(dx) >> 5, std::abs(dy) >> 5);
        if (d > static_cast<int>(wp >> 29)) break;
        ++ob.curWaypoint;
        advanced = true;
    }
    return advanced;
}

static void stepUnit(int id) {
    UnitRecord& u = g_units[id];
    if (u.id == -1 || (u.flags & UNIT_DEAD)) return;
    if (!(u.flags & UNIT_VEHICLE)) return;
    // Morrendo (ação 0x2F, sub-estado 6): o movimento para onde está.
    if (u.action == ACT_DIE) return;

    // sub_44C0B0: passo em curso? aplica 1 tick e sai (termina mesmo sem ordem).
    if (u.stepTicks > 0) { applyStepTick(u); return; }
    // Sem passo: EMP (0x80000) para no sub-estado 0x0A; espuma (0x40000) só não anda.
    if (u.flags & UNIT_EMP) { u.field22 = 0x0A; return; }
    if (u.flags & 0x40000u) return;
    // Fim de um passo COM deslocamento: avança o cursor da rota e encaixa a
    // posição no destino ([st+0x78]++, [+0x14/+0x18] = [st+0x48/+0x4C]).
    if (u.velX || u.velY) {
        ++u.routeIdx;
        u.pixelX = u.stepDestX; u.pixelY = u.stepDestY;
        u.velX = u.velY = 0;
        u.pushWait = 0;                                  // [st+0x6C] = 0 (loc_44C0F1)
    }

    // Ações de movimento: 2 (mover), 0x26 (ficar parado: anda até o ponto e
    // segura a posição), 0x28 (patrulhar) — sub_43ACA0 inicia todas em
    // loc_43ADAD — e a aproximação de atacar/guardar (0x24/0x25).
    const bool moving = u.action == CMD_MOVE || u.action == 3 || u.action == 4 || u.action == 0x26 || u.action == 0x24 ||
                        (u.action >= 0x1C && u.action <= 0x1E) ||
                        u.action == 0x25 || u.action == 0x27 || u.action == 0x28 ||
                        u.action == 6 || u.action == 7 || u.action == 1 || u.action == 8 ||
                        u.action == 0x1F;   // 6: ConVec indo à obra; 0x1F: SALVAGE de região
                        // 0x27: rota de minério; 7: Earthworker; 1: Robo-Dozer
    if (u.field20 != 1 || !moving || !u.hasOrder || u.order.waypoints.empty()) {
        // Parado: sai do sub-estado de movimento (1). Os demais sub-estados
        // (espera 6, troca na doca 0x0D...) são do laço da unidade (sub_43DE50).
        if (u.field22 == 1) u.field22 = 0;
        return;
    }

    const int curX = u.pixelX >> 5, curY = u.pixelY >> 5;
    OrderBlock& ob = u.order;
    int destX = 0, destY = 0, mode = 0, dist = 0;
    // Laço de sub_44C0B0 (loc_44C114): objetivo = waypoint [ordem+8]; chegou a
    // um ponto intermediário → avança (sub_44BEC0); sem rota p/ um ponto
    // intermediário → pula-o ([ordem+8]++). (Guarda contra laço infinito.)
    for (int guard = 0;; ++guard) {
        if (ob.curWaypoint > ob.countMinus1) ob.curWaypoint = ob.countMinus1;
        waypointGoal(ob, destX, destY, mode, dist);      // sub_44C2F0
        // sub_4401F0 case 3: o alvo da busca é a casa à esquerda ([+0x14] − 1).
        if (mode == 3) destX = (destX - 1) & (g_gameMap.internalWidth() - 1);
        pfSetContext(id);
        pfSetGoal(mode, destX, destY, dist);
        if (pfMoveCost(curX, curY) < 0) {                // sub_440550: chegou ao objetivo
            if (ob.curWaypoint == ob.countMinus1) {      // loc_44C1C0: fim da lista
                u.field20 = 0; u.field22 = 0;
                u.field23 = 1;                           // [+0x23] = sub-estado anterior (movimento)
                u.clearRoute();
                // Patrulhar: a ordem fica; o tick da ação (case 40) recomeça.
                if (u.action == 0x28 || u.action == 0x27) return;   // (0x27: case 39)
                // 3/4: o giro e a doca vêm no tick da ação (moveDockArrive).
                if (u.action == 3 || u.action == 4) { u.hasOrder = false; u.order = OrderBlock{}; return; }
                // Mover (casos 2-4 de jpt_43B312) zera a ação; 0x26 não tem tratamento
                // por tick (def_43B312): a ação fica 0x26 (posição mantida).
                // Atacar/guardar (0x24/0x25) seguem no tick da ação (jpt_43B312 casos 36/37).
                if (u.action == CMD_MOVE) { u.action = 0; unitArrived(id); }   // sub_44D5B0 / sub_406C20 / sub_484460
                g_units[id].hasOrder = false; g_units[id].order = OrderBlock{};
                return;
            }
            waypointAdvance(u);                          // sub_44BEC0
            u.clearRoute();
            if (guard < 64) continue;
            u.field22 = 0; return;
        }

        // (Re)computa a rota se necessário: sub_4401F0 (curta/corredor/longa
        // com cache e refinamento).
        if (u.routeIdx >= static_cast<int>(u.routeDirs.size())) {
            pfPlanRoute(id, curX, curY, destX, destY, mode, dist);
            const int n = static_cast<int>(u.routeDirs.size());
            if (n > 0) u.pushWait = 0;                   // rota nova: [st+0x6C] = 0 (loc_44C1A0)
            if (n == 0) {
                // loc_44C16D: sem rota p/ um ponto intermediário → o próximo.
                if (ob.curWaypoint < ob.countMinus1 && guard < 64) { ++ob.curWaypoint; u.clearRoute(); continue; }   // sub_44C2F0
                // sub_4401F0: as vizinhas enteráveis passam pela função de custo
                // (objetivo = negativo) — uma vizinha-objetivo vira o próximo passo.
                {
                    int goalDir = -1;
                    for (int d = 0; d < 8 && goalDir < 0; ++d) {
                        const int nx = (curX + kDir8[d].dx) & (g_gameMap.internalWidth() - 1), ny = curY + kDir8[d].dy;
                        if (pfCanEnter(nx, ny) && pfMoveCost(nx, ny) < 0) goalDir = d;
                    }
                    if (goalDir >= 0) { u.routeDirs.assign(1, goalDir); u.routeIdx = 0; break; }
                }
                // No destino da busca do último ponto (modo 3: a casa ao lado,
                // que pode ser um tubo e não contar como objetivo): chegou.
                if (ob.curWaypoint == ob.countMinus1 && curX == destX && curY == destY) {
                    u.field20 = 0; u.field22 = 0; u.field23 = 1;
                    u.clearRoute();
                    if (u.action == 0x28 || u.action == 0x27) return;
                    if (u.action == 3 || u.action == 4) { u.hasOrder = false; u.order = OrderBlock{}; return; }
                    if (u.action == CMD_MOVE) { u.action = 0; unitArrived(id); }   // sub_44D5B0...
                    g_units[id].hasOrder = false; g_units[id].order = OrderBlock{};
                    return;
                }
                u.field22 = 0; return;                    // sem rota: parado, não anima
            }
        }
        break;
    }

    pfSetGoal(mode, destX, destY, dist);
    if (!setupStep(id, u.routeDirs[u.routeIdx] & 7)) {
        u.clearRoute(); u.field22 = 0;
        return;
    }
    // Estado "em movimento" — é o que faz o sprite animar. A máquina de ações do
    // original (sub_43ACA0) põe [+0x22]=1 ao executar um passo de movimento
    // (loc_43AD6A / loc_43ADAD) e zera ao parar (loc_43AD5D).
    u.field22 = 1;
    // sub_44C0B0: montado o passo (sub_44B650 retornou 1), o MESMO tick já
    // aplica o deslocamento (jnz loc_44C27D) — sem tick parado entre células.
    applyStepTick(u);
}

// jpt_43B312 casos 3/4 (parado no destino): vira p/ 0x80 / 0xC0; então ação 0,
// vt[0x88] (doca do caminhão, sub_406070) e sub_44D5B0.
void moveDockArrive(int id) {
    UnitRecord& u = g_units[id];
    if (!unitTurnTo(u, u.action == 3 ? 0x80 : 0xC0)) return;
    u.action = 0;
    u.field23 = u.field22;
    u.field22 = 0;
    if (u.type == mapCargoTruck) truckDockStart(id);
    unitArrived(id);                                          // sub_44D5B0 / sub_406C20 / sub_484460
}

void tickMovement() {
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i)
        stepUnit(i);
}

} // namespace op2
