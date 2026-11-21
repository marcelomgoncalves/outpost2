#pragma once

#include <vector>

#include "player.h"
#include "unit.h"

namespace op2 {

// Economia da colônia — porta sub_471F30 (chamada por jogador a cada tick no
// turno sub_489DD0, antes dos comandos e do laço de unidades sub_43AC30).
//
// FASE A (este módulo): lista de prédios por jogador, rede de tubos até o CC e
// alocação de ENERGIA / TRABALHADORES / CIENTISTAS (liga e desliga as flags
// 0x2000/0x4000/0x8000 dos prédios). FASE B (feita depois, neste mesmo
// módulo): comida, população, moral, armazenamento e as mensagens
// ("Power Levels: ...", "%i Structures Disabled").

// Campos +0xA0.. do registro do jogador (stride 0xC24 a partir de 0x56E2DC).
struct ColonyState {
    int dirty = 1;               // +0xA0 dword_56E37C: recalcular a alocação
    int workersLeft = 0;         // +0xA4 trabalhadores sobrando
    int scientistsLeft = 0;      // +0xA8 cientistas sobrando
    int powerGenerated = 0;      // +0xAC energia gerada (geradores ativos + CC)
    int powerIdle = 0;           // +0xB0 capacidade dos geradores parados
    int powerRequired = 0;       // +0xB4 energia exigida pelos prédios
    int powerLeft = 0;           // +0xB8 energia sobrando
    int inactiveCount = 0;       // +0xBC prédios em construção/desmontagem/0x12
    int activeCount = 0;         // +0xC0 prédios operando
    int structCount = 0;         // +0xC4 prédios na lista
    int unpoweredCount = 0;      // +0xC8 prédios sem energia
    int workersRequired = 0;     // +0xCC
    int scientistsRequired = 0;  // +0xD0
    int scientistsAsWorkers = 0; // +0xD4 cientistas usados como trabalhadores
    int researchAssigned = 0;    // +0xD8 soma de vt+0xE4 (cientistas em pesquisa)
    int foodProduced = 0;        // +0xDC comida dos prédios operando (vt+0xD8)
    int foodEaten = 0;           // +0xE0
    int foodDeficit = 0;         // +0xE4
    int foodNet = 0;             // +0xE8 variação do estoque no último ciclo
    int solarSatellites = 0;     // +0xEC unidades 0x59 do jogador (sub_490800)
    // +0xF0..+0x1B7: estatísticas por tipo (0x32 dwords), zeradas e refeitas
    // pelo vt+0x9C de cada prédio a cada alocação. Índice = (offset-0xF0)/4.
    int stats[0x32] = {};
    int stat(int off) const { return stats[off / 4]; }
    // População (sub_471CC0): acumuladores e rodízio da fome (sub_471BD0).
    int accGrowUp = 0;           // +0x80 crianças → trabalhadores
    int accBirth = 0;            // +0x84
    int accKidDeath = 0;         // +0x88
    int accSciDeath = 0;         // +0x8C
    int accWorkerDeath = 0;      // +0x90
    int starveRotor = 0;         // +0x1EC
    int oreCapacity = 0;         // +0x18 capacidade de minério comum
    int rareCapacity = 0;        // +0x1C capacidade de minério raro
    // Moral (sub_44E550) — campos +0x38..+0x78 do registro do jogador. O
    // nível em si fica em PlayerData::morale (+0x34).
    int moraleEvents = 0;        // +0x38 pontos de evento (decaem por EVENT_DEC_RATE)
    int moraleValue = 0;         // +0x40 valor 0..99 (varia no máx. ±5 por ciclo)
    int moraleTrend = 0;         // +0x44 +1 / 0 / -1
    int crowding = 0;            // +0x48 pop*100 / capacidade das residências
    int foodState = 0;           // +0x4C 0 sobra, 1 déficit c/ estoque, 2 déficit, 3 fome
    int disabledPct = 0;         // +0x50 % de prédios desligados
    int recreationLoad = 0;      // +0x54 pop*100 / (Recreation + Forum)
    int medicalLoad = 0;         // +0x5C pop*100 / Medical Center
    int hasNursery = 0;          // +0x60
    int hasUniversity = 0;       // +0x64
    int disasterTechs = 0;       // +0x68
    int dirtCoverage = 0;        // +0x6C sub_42FE40
    int idlePct = 0;             // +0x70 ociosos (trabalhadores+cientistas sobrando)
    int phdPct = 0;              // +0x74 cientistas usados como trabalhadores
    int hasGorf = 0;             // +0x78
    // Componentes de rede da última alocação (campos dos nós de dword_547350):
    // nº de prédios (+0x18), CCs (+0x0C), conectado (+0x10), DIRTs (+0x14).
    struct NetComponent { int structures = 0, ccs = 0, connected = 0, dirts = 0; };
    std::vector<NetComponent> components;
};

// Offsets (relativos a +0xF0) do vetor de estatísticas — vt+0x9C das classes.
enum ColonyStat {
    ST_REC_CAP = 0x00, ST_FORUM_CAP = 0x04, ST_MED_CAP = 0x08, ST_RES_CAP = 0x0C,
    ST_CC_OP = 0x10, ST_NURSERY = 0x14, ST_UNIVERSITY = 0x18, ST_OBSERVATORY = 0x1C,
    ST_METEOR_DEF = 0x20, ST_TRADE = 0x24,
    ST_POWER_GRP = 0x28,      // CC + geradores: total/operando/parado/inativo
    ST_AGRI_GRP = 0x38, ST_SMELT_GRP = 0x48, ST_STORE_GRP = 0x58, ST_RSTORE_GRP = 0x68,
    ST_RSMELT_GRP = 0x78, ST_GORF_GRP = 0x88, ST_MINE = 0x98, ST_RMINE = 0xA8,
    ST_ROBOT_GRP = 0xB8,
};
extern ColonyState g_colony[kMaxPlayers];

// Lista de prédios do jogador ([jogador+0xC18], encadeada por [unit+0x0C]),
// ORDENADA por Resource Priority (desc+0x2A0): sub_439FE0 insere antes do 1º
// prédio de prioridade >= a do novo. sub_439C00 retira. Ambas marcam dirty.
void colonyStructureAdded(int id);
void colonyStructureRemoved(int id);
const std::vector<int>& colonyStructures(int player);
// Veículos do jogador ([+0xC1C]; PlayerUnitEnum/PlayerVehicleEnum): cabeça = mais novo.
const std::vector<int>& colonyVehicles(int player);
void colonyVehicleAdded(int id);
void colonyVehicleRemoved(int id);
// sub_482C30: a equipe do prédio morre (trabalhadores; a falta sai dos
// cientistas), moral + mensagem (sub_44F120) e o prédio fica sem 0x4000/0x8000.
void colonyStaffKilled(int id);
void colonyMarkDirty(int player);
void colonyReset();

// Tabela de moral (MORALE.TXT, sub_44E180): 5 linhas UTOPIA..WAYBAD, bônus em
// /256 = (pct*256+50)/100.
struct MoraleRow {
    int points = 0, powBonus = 0, rschBonus = 0, factBonus = 0, foodBonus = 0;
    int defectRate = 0, fertRate = 0, dieRate = 0;
};
bool loadMoraleSheet();
const MoraleRow& moraleRow(int level);

// vt+0xA0 por classe (sub_483750 base; sub_40C1B0 CC; sub_46E6D0 geradores;
// sub_46E880 Solar; sub_44AD60 minas/Magma Well).
bool structureOperational(const UnitRecord& r);
// sub_471BD0: tira `n` colonos em rodízio (fome; módulo de evacuação com quiet).
void colonyRemoveColonists(int player, int n, bool quiet);

// Eventos de moral (somam em +0x38): prédio do jogador destruído (sub_44F6C0),
// prédio inimigo destruído pelo jogador (sub_44FBE0), CC concluído (sub_44F790).
void moraleStructureLost(int player, int type);
void moraleEnemyStructureKilled(int player, int type);
void moraleStructureBuilt(int player, int type);
// sub_44FA10: bens de consumo produzidos (0x6C/0x6D/0x6E → CONSUMER_GOODS_1/2/3).
void moraleConsumerGoods(int player, int type);
// sub_44F840: pesquisa concluída (NEW_TECH_BORN).
void moraleNewTech(int player);
// sub_44F330: n cientistas formados (PHD_TRAINED por cientista, ou +n com >= 40 cientistas).
void moralePhdTrained(int player, int n);

// Produção exibida no painel (sub_45AEB0): energia (vt+0xD0) e comida (vt+0xD8).
int structurePowerOutput(const UnitRecord& r);
int structureFoodOutput(const UnitRecord& r);
// Comandos do botão do painel (CommandPacket 0x12 / 0x13, sub_40E210):
bool structureCanIdle(const UnitRecord& r);

// Desligar um armazem/fundicao tira a capacidade dele da colonia, e o que
// passar do novo teto e PERDIDO (sub_471F30 trunca: "if (ore > capacity) ore =
// capacity"). O original avisa antes, em sub_455CB0: para os tipos de metal
// COMUM (0x36 fundicao, 0x19 armazem) compara a capacidade com o campo +0x18
// do jogador, que sub_406610 mostra ser a CAPACIDADE de minerio, e para os de
// metal RARO (0x38, 0x1A) o +0x1C. Devolve 0 = nao perde nada, 1 = perde metal
// comum, 2 = perde metal raro, e em *perdido QUANTO sai do estoque.
int structureIdleMetalLoss(int buildingId, int* perdido = nullptr);   // sub_455CB0
void structureDoIdle(int id);                       // case 18
bool structureDoActivate(int id);                   // case 19 (false se faltar recurso)

// Moral forçada das missões — TethysGame::ForceMorale{Great,Good,OK,Poor,
// Rotten} / FreeMoraleLevel → CommandPacket 0x2B sub-ids 0x15..0x19 / 0x20
// (sub_4229F0). Cinco bitmasks (dword_56DEAC..BC → 99/75/50/25/0). player
// -1 = todos. Fiel ao original, INCLUSIVE o XOR: forçar um nível alterna o
// bit do jogador nas CINCO máscaras antes de ligá-lo na escolhida (então um
// 1º ForceMoraleGood deixa todas ligadas e vale o Great, checado primeiro).
void colonyForceMorale(int player, int level);
// As cinco mascaras da moral forcada (Great..Rotten, bit = jogador); o --harness
// as escreve como o roteiro do original escreve dword_56EAEC..56EAFC.
uint32_t* colonyForceMasks();      // level 0=Great .. 4=Rotten
void colonyFreeMorale(int player);
// O bit do jogador está em alguma das cinco máscaras (moral "(Steady)").
bool colonyMoraleForced(int player);
// Bloco [jogador+0x34..+0x7B] (0x12 dwords) que a campanha leva de uma missão
// à seguinte (MissionResults +0x18F): nível de moral e os campos de sub_44E550.
// +0x3C e +0x58 não estão modelados (ficam 0 / não são lidos).
void colonyPlayerBlockSave(int player, int out[0x12]);
void colonyPlayerBlockLoad(int player, const int in[0x12]);

// sub_42FF00: redução de dano do DIRT (0, 625..2500 de 10000) p/ o prédio.
int colonyDirtProtection(const UnitRecord& r);
// Crédito de moral ao matar veículo inimigo (sub_44FD40: ENEMY_VEH_DIES).
void moraleEnemyVehicleKilled(int player);
// sub_44F8E0: desastre atingiu a colônia (com ou sem aviso).
void moraleDisaster(int player, bool warned);

// Turno: sub_471F30 p/ cada jogador.
void tickColony();

} // namespace op2
