#include "unit.h"

#include <climits>
#include <deque>

#include "game_map.h"
#include "building_sheet.h"
#include "tethys_game.h"
#include "command.h"
#include "structure.h"
#include "colony.h"
#include "save_game.h"
#include "scenario.h"
#include "save_stream.h"

namespace op2 {

// Pool global. Índice 0 reservado como "nenhuma unidade" (IsLive trata id 0).
std::vector<UnitRecord> g_units;

// Footprint por map_id REAL (building.txt X-size/Y-size), id começando em 0x15.
// Ordem = off_4E1340 (enum do SDK), NÃO a ordem das linhas da planilha.
// Mesma fonte que popula off_4E1340[type]->[+0x248/+0x24A] no original.
static const struct { int w, h; } kBuildingFootprint[] = {
    { 2, 1 },  // 0x15 MINE
    { 2, 1 },  // 0x16 MINE_ADV
    { 1, 1 },  // 0x17 TOWER_GUARD
    { 1, 1 },  // 0x18 TOWER_LIGHT
    { 1, 2 },  // 0x19 STORAGE_ORE
    { 1, 2 },  // 0x1A STORAGE_RARE
    { 2, 2 },  // 0x1B FORUM
    { 3, 2 },  // 0x1C COMMAND
    { 2, 2 },  // 0x1D ASE
    { 2, 2 },  // 0x1E RESIDENCE
    { 2, 2 },  // 0x1F ROBOT_COMMAND
    { 2, 2 },  // 0x20 TRADE
    { 2, 2 },  // 0x21 LAB
    { 2, 2 },  // 0x22 MED_CENTER
    { 2, 2 },  // 0x23 NURSERY
    { 3, 2 },  // 0x24 SOLAR
    { 2, 2 },  // 0x25 RECREATION
    { 2, 2 },  // 0x26 UNIVERSITY
    { 3, 2 },  // 0x27 AGRIDOME
    { 3, 2 },  // 0x28 DIRT
    { 3, 2 },  // 0x29 GARAGE
    { 2, 1 },  // 0x2A MAGMA_WELL
    { 2, 2 },  // 0x2B METEOR_DEF
    { 2, 1 },  // 0x2C GEOTHERMAL
    { 2, 2 },  // 0x2D FACT_ANDROID
    { 3, 3 },  // 0x2E FACT_LUXURY
    { 4, 3 },  // 0x2F FACT_STRUCTURE
    { 4, 3 },  // 0x30 FACT_VEHICLE
    { 3, 2 },  // 0x31 LAB_STANDARD
    { 3, 3 },  // 0x32 LAB_ADV
    { 2, 2 },  // 0x33 OBSERVATORY
    { 3, 2 },  // 0x34 RESIDENCE_P
    { 3, 3 },  // 0x35 RESIDENCE_E
    { 4, 3 },  // 0x36 SMELTER
    { 5, 4 },  // 0x37 SPACEPORT
    { 4, 3 },  // 0x38 SMELTER_ADV
    { 3, 2 },  // 0x39 GORF
    { 2, 2 },  // 0x3A TOKAMAK
};

int buildingCenterOfsX(map_id type) {
    int w, h;
    buildingFootprint(type, w, h);
    return (w & 1) << 4;
}
int buildingCenterOfsY(map_id type) {
    int w, h;
    buildingFootprint(type, w, h);
    return (h & 1) << 4;
}

void buildingFootprint(map_id type, int& w, int& h) {
    w = 1; h = 1;
    if (type >= kFirstBuilding && type <= kLastBuilding) {
        const auto& f = kBuildingFootprint[type - kFirstBuilding];
        w = f.w; h = f.h;
    }
}

// Slots liberados (anel dword_54EC04, cabeça dword_54EC00 / cauda dword_54EBFC):
// sub_439B90 empurra o slot com [+0x08] = tick; sub_439910 reusa o 1º que foi
// liberado há mais de 0x80 ticks e não tem referências ([+0x32] == 0).
static std::deque<int> s_freeRing;

void unitsSerialize(SaveStream& s) {
    uint32_t n = static_cast<uint32_t>(g_units.size());
    s.io(n);
    if (s.loading()) { if (!s.ok() || n > 0x10000) { s.fail(); return; } g_units.assign(n, UnitRecord{}); }
    for (UnitRecord& u : g_units) {
        s.ioExcept(u, {SaveStream::member(u, u.order.waypoints), SaveStream::member(u, u.routeDirs),
                       SaveStream::member(u, u.routeCache)});
        s.ioVec(u.order.waypoints);
        s.ioVec(u.routeDirs);
        s.ioVec(u.routeCache);
    }
    s.ioDeque(s_freeRing);
}

void resetUnits() {
    colonyReset();
    g_units.clear();
    g_units.emplace_back();   // slot 0 = nulo / sentinela da lista por Y
    g_units[0].lnext = g_units[0].lprev = 0;
    s_freeRing.clear();
}

// ---- Lista por Y (dword_54EC0C) --------------------------------------------
// O original tem sentinelas de y mínimo (início) e máximo (fim); aqui o slot 0
// faz as duas vezes: andando p/ frente vale +inf, p/ trás vale -inf.
static int listYFwd(int id)  { return id == 0 ? INT_MAX : unitOY(g_units[id]); }
static int listYBack(int id) { return id == 0 ? INT_MIN : unitOY(g_units[id]); }

void unitListInsert(int id) {          // sub_439910 @439A46
    const int y = unitOY(g_units[id]);
    int at = g_units[0].lnext;
    while (listYFwd(at) < y) at = g_units[at].lnext;
    UnitRecord& r = g_units[id];
    r.lnext = at;
    r.lprev = g_units[at].lprev;
    g_units[r.lprev].lnext = id;
    g_units[at].lprev = id;
}

void unitListRemove(int id) {          // sub_439B90
    UnitRecord& r = g_units[id];
    g_units[r.lprev].lnext = r.lnext;
    g_units[r.lnext].lprev = r.lprev;
    r.lnext = r.lprev = 0;
}

int unitListFirst() { return g_units.empty() ? 0 : g_units[0].lnext; }
int unitListNext(int id) { return g_units[id].lnext; }

// sub_43E7E0: passadas de inserção até a lista ficar ordenada por [+0x18].
void unitListSort() {
    if (g_units.empty()) return;
    bool moved;
    do {
        moved = false;
        int c = 0;
        while (g_units[c].lnext != 0) {
            c = g_units[c].lnext;
            const int nx = g_units[c].lnext, pv = g_units[c].lprev;
            const int y = unitOY(g_units[c]);
            if (listYFwd(nx) < y) {
                // anda p/ frente até o 1º com y >= y(c) e insere antes dele
                int at = nx;
                while (listYFwd(at) < y) at = g_units[at].lnext;
                g_units[pv].lnext = nx; g_units[nx].lprev = pv;
                g_units[c].lnext = at; g_units[c].lprev = g_units[at].lprev;
                g_units[g_units[at].lprev].lnext = c; g_units[at].lprev = c;
                moved = true;
            } else if (listYBack(pv) > y) {
                // anda p/ trás até o 1º com y <= y(c) e insere depois dele
                int at = pv;
                while (listYBack(at) > y) at = g_units[at].lprev;
                g_units[pv].lnext = nx; g_units[nx].lprev = pv;
                g_units[c].lprev = at; g_units[c].lnext = g_units[at].lnext;
                g_units[g_units[at].lnext].lprev = c; g_units[at].lnext = c;
                moved = true;
            }
        }
    } while (moved);
}

void unitFreeSlot(int id) {            // sub_439B90 (parte do slot)
    const uint8_t refs = g_units[id].refCount;   // [+0x32] sobrevive à liberação
    g_units[id] = UnitRecord{};                  // [+0x04] = -1
    g_units[id].refCount = refs;
    g_units[id].freeTick = TethysGame::tick();
    s_freeRing.push_back(id);
}

const std::deque<int>& unitFreeRing() { return s_freeRing; }

UnitRecord* Unit::rec() const {
    if (m_id <= 0 || m_id >= static_cast<int>(g_units.size())) return nullptr;
    return &g_units[m_id];
}

int Unit::IsLive() {
    // Original: id!=0, [rec+4]!=0xFFFFFFFF e !(flags & 0x20000); senão zera o wrapper.
    UnitRecord* r = rec();
    if (m_id != 0 && r && r->id != -1 && !(r->flags & UNIT_DEAD))
        return 1;
    m_id = 0;
    return 0;
}

LOCATION Unit::Location() const {
    UnitRecord* r = rec();
    if (!r) return LOCATION{0, 0};
    return LOCATION{ r->pixelX >> 5, r->pixelY >> 5 };   // tile = pixel>>5
}

int Unit::OwnerID() const {
    UnitRecord* r = rec();
    return r ? (r->owner & 0x0F) : 0;
}

map_id Unit::GetType() const {
    UnitRecord* r = rec();
    return r ? r->type : kMapIdNone;
}

int Unit::IsBuilding() const {
    UnitRecord* r = rec();
    return (r && (r->flags & UNIT_BUILDING)) ? 1 : 0;
}

int Unit::IsVehicle() const {
    UnitRecord* r = rec();
    return (r && (r->flags & UNIT_VEHICLE)) ? 1 : 0;
}

map_id Unit::GetCargo() const {
    UnitRecord* r = rec();
    return r ? static_cast<map_id>(static_cast<int16_t>(r->cargo)) : kMapIdNone;
}

void Unit::SetCargo(map_id c) {
    UnitRecord* r = rec();
    if (r) r->cargo = static_cast<uint16_t>(c);
}

void Unit::DoDeath() {
    UnitRecord* r = rec();
    if (!r || r->id == -1) return;
    // Unit::DoDeath @191271 = sub_43A890(1): só agenda a morte (ação 0x2F); a
    // máquina de estados faz o resto (prédio: animação de destruição, entulho,
    // remoção — src/game/structure.cpp).
    // Veículo: case 47 (loc_43D8A3) — sem explosão nem destroços, pois o dano
    // não chegou ao máximo (src/game/weapon.cpp, vehicleDeath).
    killUnit(m_id, 1);
}

void Unit::DoMove(LOCATION dest) {
    // Porta Unit::DoMove @asm 190538: valida slot vivo + alvo dentro do mapa,
    // monta o CommandPacket tipo 2 (MOVE) com o tile empacotado e despacha pelo
    // objeto de comando do dono.
    UnitRecord* r = rec();
    if (!r || r->id == -1) return;             // [esi+4] == -1 (slot morto)
    if (r->flags & UNIT_DEAD) return;          // flags & 0x20000
    if (g_gameMap.ok()) {
        if (dest.x < 0 || dest.x >= g_gameMap.width())  return;  // 0 <= x < width (54EBC8)
        if (dest.y < 0 || dest.y >= g_gameMap.height()) return;  // 0 <= y < height (54EBD0)
    }

    CommandPacket pkt;
    pkt.type = CMD_MOVE;                        // var_74 = 2
    pkt.unitIds.push_back(m_id);               // lista de unidades (count=1)
    pkt.waypoints.push_back(packLocation(dest.x, dest.y));  // LOCATION empacotado
    processCommandPacket(r->owner & 0x0F, pkt);            // sub_40E210
}

bool createUnit(Unit& out, map_id type, LOCATION loc, int owner,
                map_id cargo, int rotation) {
    if (g_units.empty()) resetUnits();

    // Clampa a posição ao mapa (o original usa clip/limites; aqui o essencial).
    if (g_gameMap.ok()) {
        int w = g_gameMap.width(), h = g_gameMap.height();
        loc.x &= (w - 1);                       // wrap horizontal, como o engine
        if (loc.y < 0) loc.y = 0;
        if (loc.y >= h) loc.y = h - 1;
    }

    // Aloca um slot (sub_439910): o 1º do anel de livres liberado há mais de
    // 0x80 ticks e sem referências; senão um slot novo.
    int slot = -1;
    for (auto it = s_freeRing.begin(); it != s_freeRing.end(); ++it) {
        const UnitRecord& f = g_units[*it];
        if (f.freeTick + 0x80 < TethysGame::tick() && f.refCount == 0) {
            slot = *it; s_freeRing.erase(it); break;
        }
    }
    if (slot < 0) {
        // Teto de 2048 (ver kMaxLiveUnits): o id precisa caber nos 11 bits da
        // ocupacao da celula. Sem isto o pool crescia sem limite e o id 2048
        // entrava truncado no mapa.
        if (static_cast<int>(g_units.size()) >= kMaxLiveUnits) return false;
        slot = static_cast<int>(g_units.size());
        g_units.emplace_back();
    }

    UnitRecord& r = g_units[slot];
    r = UnitRecord{};
    r.type   = type;
    r.id     = slot;
    r.pixelX = loc.x << 5;                       // tile<<5 (como +0x14/+0x18)
    r.pixelY = loc.y << 5;
    // sub_439F90: [+0x1D] = dono | (dono << 4) — o nibble alto é o jogador cujas
    // estatísticas (bloco do descritor) valem p/ a unidade.
    r.owner  = static_cast<uint8_t>((owner & 0x0F) | ((owner & 0x0F) << 4));
    r.cargo  = static_cast<uint16_t>(cargo);
    r.rotation = static_cast<uint8_t>(rotation & 0xFF);
    // Classe pela faixa do map_id: prédio (0x15..0x3A), veículo (0x01..0x10) ou
    // nenhuma das duas (efeitos/armas, ex. AMBIENT_ANIM 0x72 — sem ocupação).
    r.flags  = mapIdIsBuilding(type) ? UNIT_BUILDING
             : mapIdIsVehicle(type)  ? UNIT_VEHICLE : 0u;
    if (r.flags & UNIT_VEHICLE) {
        r.moveClass = vehicleTrackClass(type);                   // desc+0x1E8 (Track Type)
        r.flags |= UNIT_DAMAGEABLE;                              // sub_44B440
        if (type == mapScorpion || type == mapLynx || type == mapPanther || type == mapTiger)
            r.flags |= UNIT_AUTO_TARGET | UNIT_ARMED;            // off_4D0650
        else if (type == mapSmallCapacityAirTransport)
            r.flags |= UNIT_ARMED;                               // SCAT
    }
    if (r.flags & UNIT_BUILDING) {
        r.flags |= 0x20000000u | UNIT_DAMAGEABLE;                // sub_482F30
        if (type == 0x17) r.flags |= UNIT_TURRET_BLD;            // Guard Post (sub_42CD90)
        if (type == 0x2D || type == 0x2E || type == 0x2F || type == 0x30 || type == 0x37)
            r.flags |= UNIT_FACTORY;                             // sub_443260 (off_4D29D0)
        // sub_4466B0: bit 0x800000 = bit6 de desc+0x24C (AutoTargeted).
        const StructSheetDesc* d = structSheet(type);
        if (d && (d->flags24C & 0x40)) r.flags |= UNIT_AUTO_TARGET;
        else r.flags &= ~UNIT_AUTO_TARGET;
    }

    // Grava a ocupação no grid runtime (bits 16-26), como o engine faz ao
    // posicionar a unidade — é o que sub_4382C0/sub_438390 leem. Veículo = 1 tile;
    // prédio = footprint W×H (building.txt), com a LOCATION no CENTRO (canto
    // superior-esquerdo = loc - (w/2, h/2), como o render/placement do original).
    if (g_gameMap.ok()) {
        int cx = r.pixelX >> 5, cy = r.pixelY >> 5;
        if (mapIdIsBuilding(type)) {
            int w, h; buildingFootprint(type, w, h);
            int tlx = cx - (w >> 1), tly = cy - (h >> 1);
            for (int dy = 0; dy < h; ++dy)
                for (int dx = 0; dx < w; ++dx)
                    g_gameMap.setUnitOccupancy(tlx + dx, tly + dy, slot);
            // Doca (desc+0x2A4 Eden / +0x2A5 Plymouth, coluna 1-based na
            // última linha): a célula fica sem ocupação (and 0xF800FFFF).
            if (const StructSheetDesc* sd = structSheet(type)) {
                const bool eden = (owner & 0x0F) < kMaxPlayers && g_playerData[owner & 0x0F].isEden;
                const int dock = eden ? sd->dockLocE : sd->dockLocP;
                if (dock) g_gameMap.clearUnitOccupancy(tlx + dock - 1, tly + h - 1);
            }
        } else if (mapIdIsVehicle(type)) {
            g_gameMap.setUnitOccupancy(cx, cy, slot);
        }
    }

    // Nº de série exibido (byte +0x2C). PROVISÓRIO: contagem por dono e tipo —
    // o contador exato que o original grava em +0x2C ainda não foi rastreado.
    {
        int n = 0;
        for (int i = 1; i < static_cast<int>(g_units.size()); ++i)
            if (i != slot && g_units[i].id != -1 && g_units[i].type == type &&
                (g_units[i].owner & 0x0F) == (owner & 0x0F)) ++n;
        r.serial = static_cast<uint8_t>(n + 1);
    }
    // O construtor do beacon (MiningBeacon::Constructor) chama CreateBeacon(-1,
    // -1, -1): todo beacon novo sorteia minerio, rendimento e variante (3
    // sorteios do RNG do jogo), e o TethysGame::CreateBeacon da missao chama de
    // novo com os argumentos dela. Sem isto o porte ficava 3 sorteios atras a
    // cada beacon, e tudo o que a missao sorteava depois saia diferente.
    if (type == 0x51) beaconInit(r, -1, -1, -1);
    unitListInsert(slot);                        // lista por Y (sub_439910)
    // sub_439FE0: prédio entra na lista do dono, ordenada por prioridade.
    if (r.flags & UNIT_BUILDING) colonyStructureAdded(slot);
    else if (r.flags & UNIT_VEHICLE) colonyVehicleAdded(slot);

    out.SetId(slot);
    return true;
}

} // namespace op2
