#pragma once

#include <cstdint>
#include <vector>

namespace op2 {

struct UnitRecord;

// Ciclo do minério — porta:
//   sub_44AF00  MINES.TXT → tabela unk_564560 (por rendimento High/Med/Low e
//               variante 1..3: INITIAL / PEAK_TRUCK / PEAK / MIN_TRUCK / MIN,
//               em % de 1000)
//   sub_44ADF0  vt[0x100] da mina: carga = sub_44AE00 e [+0x68]++ (cargas tiradas)
//   sub_44AE00  sub_44B0B0(rendimento [+0x70], variante [+0x6C], cargas [+0x68]);
//               +20% com o "Improved" (bloco+0x1D); mina rara (0x16) → metade
//   sub_44B120  Magma Well (0x2A): carga = Production_Capacity (bloco+0x34)
//   sub_47DB40 / sub_47DC30  smelter comum/raro recebe o minério bruto: +20% com
//               o "Improved" e soma direto no estoque do jogador (corta na
//               capacidade e devolve 0)
//   sub_47DBC0 / sub_47DCB0  cabe mais (quantidade 1) no estoque?
// Caminhão (vtable off_4CF4A8): carga [+0x24] = (quantidade << 4) | tipo
//   (1 comida, 2 minério comum bruto, 3 raro bruto, 4 metal comum, 5 metal raro,
//   6..10 outros).
//   sub_406070  vt[0x88] começa a troca no prédio sob o caminhão
//   sub_406530  vt[0x94] pedido (+0x5C) e sub-estado 0x0D (carregar na mina: 1 tick
//               na 1ª fase; demais: último frame da animação de doca)
//   sub_406610  vt[0x98] troca no meio da doca
//   sub_4062B0  vt[0x80] animação (doca por tipo de carga; com carga por tabela)
//   sub_44C7B0  vt[8] frame (doca: tick − [+0x5E], travado no penúltimo)
// Rota (comando sub_453FA0 → pacote 0x27; ação 0x27 = jpt_43B312 case 39).

bool loadMinesSheet();

// Prédio do jogador `owner` cujo footprint contém o tile (x,y) — o iterador
// sub_437EB0 com a classe 4 e a máscara do dono. 0 se nenhum.
int buildingAtTile(int x, int y, int owner);

// Carga da mina (vt[0x100]); incrementa as cargas tiradas.
int mineTakeLoad(int mineId);
// vt[0x104]: tamanho da próxima carga (sub_44AE00 / Magma Well sub_44B140), sem tirar.
int mineLoadAmount(int mineId);

// Smelter: cabe `amount` (sub_47DBC0/sub_47DCB0)? / recebe (sub_47DB40/sub_47DC30).
bool smelterCanAccept(int smelterId, int amount);
bool smelterReceive(int smelterId, int amount);

// Caminhão.
void truckDockStart(int truckId);                 // vt[0x88]
// vt[0x94] (sub_406530): carga 6/7/9 grava direto [+0x24] (9: −8000 se > 8000);
// as demais viram pedido [+0x5C] com o sub-estado de doca 0x0D.
void truckSetCargo(int truckId, int kind, int amount, int loadFlag);
void truckDockExchange(int truckId);              // vt[0x98]
int  truckAnimRaw(const UnitRecord& u, int dir);  // vt[0x80] (anim crua)
int  truckFrame(const UnitRecord& u, int id);     // vt[8]
int  truckDockPhaseTicks(const UnitRecord& u);    // [+0x3C] da 2ª fase

// Rota de minério.
// Alvo válido do clique (sub_454290): 0 = mina (0x15/0x16/0x2A), 1 = smelter
// compatível com a mina, 2 = a mesma mina; o tile tem de ser a DOCA do prédio.
int  oreRouteTarget(int stage, int mineId, int truckOwner, int tileX, int tileY);
// sub_453FA0 3º clique: pacote 0x27 (caminhões, pontos, índices e ids).
void sendOreRoute(const std::vector<int>& sel, const std::vector<uint32_t>& pts,
                  int mineIdx, int smelterIdx, int mineId, int smelterId);
void oreRouteTick(int truckId);                   // jpt_43B312 case 39

// ACRESCIMO DESTE PORTE (nao existe no original; chave "AutoOreRoute").
// A rota dos tres cliques, montada sozinha: dada a mina, acha a fundicao
// COMPATIVEL mais proxima do mesmo dono e manda o mesmo pacote 0x27. Devolve
// false (e nao manda nada) se a mina nao serve, se nao ha caminhao de carga
// vazio na selecao ou se nao existe fundicao compativel.
// `nearestSmelterFor` e a parte nova de verdade: o resto ja era do jogo.
int  nearestSmelterFor(int mineId);
bool autoOreRoute(const std::vector<int>& sel, int mineId);

// sub_44DA80: o veículo pode atracar no prédio (com a carga atual)?
bool vehicleCanDock(int vehicleId, int buildingId, bool ownerCheck);
// sub_406210: nome da carga do caminhão ("(empty)", "Common Ore"...).
const char* truckCargoName(const UnitRecord& u);

// STORAGE de recursos (dword_5650F0 com [+0x6C0] = 0): armazéns 0x19/0x1A,
// smelters 0x36/0x38 e Agridome 0x27.
int  storageStock(int buildingId);              // [+0x6A4]: metal comum/raro ou comida
bool storageLoadAvailable(int buildingId);      // sub_456EC0 (comando dword_564A68)
bool storageUnloadAvailable(int buildingId);    // sub_4570D0 (comando dword_564A5C)
void storageLoad(int buildingId);               // pacote 0x0B → sub_423F70
void storageUnload(int buildingId);             // pacote 0x0C → sub_424120

// Despejar carga (comando off_4D4EA0 sub_4557B0: confirmação → pacote 0x0E,
// só a 1ª unidade): ação 0x0E (jpt_43AD56 case 14: espera o último frame da
// anim 0x7BC, som 0x45, [+0x5E] = tick, sub-estado 6; case 14: carga = 0).
void sendDumpCargo(int unitId);
void dumpCargoOrder(int unitId);      // case 14 sem o teste de dono (pacote da IA)
void dumpCargoStart(int unitId);
void dumpCargoTick(int unitId);

} // namespace op2
