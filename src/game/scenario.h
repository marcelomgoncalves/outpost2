#pragma once

#include <cstdint>
#include <vector>

namespace op2 {

struct UnitRecord;

// Criação de unidades e terreno pelos scripts de missão — porta das rotinas do
// EXE que os exports TethysGame/Unit usam. Coordenadas em tiles do PORT (a
// ponte em src/mission/op2_api.cpp converte o x interno do original).

// Registro da tabela de unidades da missão (UnitBlock, 32 bytes).
struct MissionUnitRecord {
    int     type;          // +0x00
    int     x, y;          // +0x04 / +0x08 (coordenadas internas do original)
    int     unk0C;         // +0x0C
    int     rotation;      // +0x10 (byte)
    int     weapon;        // +0x14 arma / carga
    int     unitClass;     // +0x18 (sub_49D1B0)
    int16_t cargoType;     // +0x1C
    int16_t cargoAmount;   // +0x1E
};
static_assert(sizeof(MissionUnitRecord) == 32, "UnitRecord da missao: 32 bytes");

// sub_4027D0: tipo de missão multiplayer ([dword_4EFD24] em −8..−4)?
bool multiplayerMission();
// sub_4366D0: espiral a partir de (x,y) até uma célula livre e transitável p/
// a classe de movimento do tipo.
void findVehicleSpot(int type, int& x, int& y);
// sub_436820: espiral até caber um retângulo w2×h2 livre (classe do ConVec).
void findBuildingSpot(int& x, int& y, int w2, int h2);
// sub_4369E0: cria o prédio centrado em (x,y): terraplana o anel em volta,
// marca o footprint (cellType 0x1A + bloqueio, menos a doca), conectores de
// tubo (Border) e, nas minas, o veio do beacon embaixo. 0 se falhar.
int placeBuilding(int type, int x, int y, int owner);
// TethysGame::CreateUnit (@478 7E0). Devolve o id (0 se falhou).
int scenarioCreateUnit(int type, int x, int y, int owner, int cargo, int rotation);
// sub_49D280: unidade de um registro do UnitBlock (x,y já convertidos).
int createUnitFromRecord(const MissionUnitRecord& rec, int x, int y, int owner, int lightsOn);
// TethysGame::CreateBeacon. Devolve 1 se criou.
int createBeacon(int type, int x, int y, int commonRare, int yield, int variant);
// vt[41] do beacon (CreateBeacon do original): −1 sorteia o campo.
void beaconInit(UnitRecord& b, int commonRare, int yield, int variant);
// Beacon visível (pesquisado) p/ o jogador (vt[40] = sub_4054E0).
bool beaconSurveyed(int id, int player);

// Unit::DoSetLights / SetTruckCargo / SetFactoryCargo.
void unitSetLights(int id, int on);
void unitSetTruckCargo(int id, int cargoType, int amount);
void unitSetFactoryCargo(int id, int bay, int type, int cargo);

// _Player::CenterViewOn do jogador local (sub_408120): centro da vista em
// pixels do port; a GameState consome o pedido.
void requestCenterView(int px, int py);
bool takeCenterViewRequest(int& px, int& py);

// UnitBlock::SortAndInit com o qsort do CRT original (_qsort/_shortsort/_swap).
void unitBlockSort(MissionUnitRecord* table, int& numUnits, int ranges[16][2]);

// [+0x14]/[+0x18] do original (centro; prédio: ((tile*2 + (tam & 1)) << 4)),
// com x nas coordenadas do port.
void unitOrigPixel(const UnitRecord& r, int& x, int& y);

// sub_437700 + sub_437770: unidades vivas (flags 0x46, sem 0x20000, sub-estado
// != 0x0D) do dono em playerMask e com flags & flagMask, na ordem da lista por
// Y, com y em [y, y+h) e x em [x, x+w) — px INTERNOS (x dá a volta).
void mapUnitsInRect(int x, int y, int w, int h, uint32_t playerMask, uint32_t flagMask, std::vector<int>& out);

// O mesmo iterador, preguiçoso (cabe nos 52 bytes do InRectEnumerator da DLL).
struct MapRectIter {
    int cur = 0;                    // [+0x00] nó atual da lista por Y (0 = antes do 1º)
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0, wrap = 0;   // [+0x18]/[+0x20]/[+0x1C]/[+0x24]/[+0x28]
    uint32_t playerMask = 0, flagMask = 0;          // [+0x10] / [+0x14]
};
void mapRectIterInit(MapRectIter& it, int x, int y, int w, int h, uint32_t playerMask, uint32_t flagMask);
int  mapRectIterNext(MapRectIter& it);       // id da próxima unidade (0 = fim)

// Unidades num raio (InRangeEnumerator, sub_437A90/sub_437AF0; px INTERNOS):
// lista por Y de y − r a min(y + r, altura), dx com a volta do mapa,
// dx² + dy² < r² (sem sinal).
struct MapRangeIter {
    int cur = 0;
    int cx = 0, cy = 0, y0 = 0, y1 = 0;                 // [+0x2C]/[+0x30]/[+0x1C]/[+0x24]
    uint32_t r2 = 0;                                    // [+0x0C]
    uint32_t playerMask = 0, flagMask = 0;              // [+0x10] / [+0x14]
};
void mapRangeIterInit(MapRangeIter& it, int px, int py, int r, uint32_t playerMask, uint32_t flagMask);
int  mapRangeIterNext(MapRangeIter& it);

// Unidades SOB um ponto (px INTERNOS): sub_437840 abre a faixa y ± 0x60 na
// lista por Y; sub_437890 testa o bbox do sprite atual (vt[0x30]/vt[0x34]) e,
// salvo flag 0x80, o pixel (vt[0x38]). Cabe no LocationEnumerator da DLL.
struct MapPointIter {
    int cur = 0;                            // [+0x00]
    int y0 = 0, y1 = 0, px = 0, py = 0;     // [+0x1C] / [+0x24] / [+0x2C] / [+0x30]
    uint32_t playerMask = 0, flagMask = 0;  // [+0x10] / [+0x14]
};
void mapPointIterInit(MapPointIter& it, int px, int py, uint32_t playerMask, uint32_t flagMask);
int  mapPointIterNext(MapPointIter& it);

// Unit::DoInfect (prédio; só com 1 jogador): equipe morre, 0x40000, dano de 1/3 do HP.
void unitDoInfect(int id);
// Unit::DoSelfDestruct → comando 0x14 (sub_40E210) → sub_43A6A0.
void unitDoSelfDestruct(int id);
// Unit::HasOccupiedBay: garagem (vt[63]) ou fábrica (0x08) com alguma baia ocupada.
int unitHasOccupiedBay(int id);

} // namespace op2
