#pragma once

#include <cstdint>
#include <vector>

#include "location.h"

namespace op2 {

// Teto de unidades vivas ao mesmo tempo. O original para em 1024; aqui vai a
// 2048, como o OPUPatch -- e 2048 nao e numero escolhido a dedo: a OCUPACAO da
// celula do mapa tem 11 bits (GameMap::CELL_OCCUPANCY = 0x07FF0000), entao o
// maior id que cabe la e 2047. Passar disso nao daria "cheio": daria ocupacao
// truncada, que e silenciosa e muito pior. Por isso o createUnit recusa.
static constexpr int kMaxLiveUnits = 2048;


// map_id — tipo de unidade do OP2. É o ÍNDICE da tabela de descritores
// `off_4E1340[map_id]` do engine (CreateUnit @195141, sub_43ACA0 etc.) e bate 1:1
// com o enum do SDK. NÃO é a ordem das linhas das planilhas .txt. Resolvido pela
// cadeia real descritor→fábrica→ctor→getter (tools/extract_anim_tables.py --mapids).
// Fronteira confirmada no CreateUnit: 0x15..0x3A são PRÉDIOS.
using map_id = int;
constexpr int kMapIdNone = 0;
enum MapId : int {
    mapCargoTruck = 0x01,          // BIG_TRUCK
    mapConVec = 0x02,              // CON_TRUCK
    mapSpider = 0x03,
    mapScorpion = 0x04,
    mapLynx = 0x05,                // LIGHT_TANK
    mapPanther = 0x06,             // MED_TANK
    mapTiger = 0x07,               // BIG_TANK
    mapRoboSurveyor = 0x08,        // SURVEYOR
    mapRoboMiner = 0x09,           // MINER
    mapGeoCon = 0x0A,              // GEO_METRO
    mapScout = 0x0B,
    mapRoboDozer = 0x0C,           // BULL_DOZER
    mapEvacuationTransport = 0x0D, // MOBILE_HOME
    mapRepairVehicle = 0x0E,       // REPAIR_TRUCK
    mapEarthworker = 0x0F,         // WALL_TRUCK
    mapSmallCapacityAirTransport = 0x10, // SCAT
    mapCommonOreMine = 0x15,       // MINE
    mapCommandCenter = 0x1C,       // COMMAND
    mapStructureFactory = 0x2F,    // FACT_STRUCTURE
    mapVehicleFactory = 0x30,      // FACT_VEHICLE
    mapTokamak = 0x3A,             // TOKAMAK
    mapEMPBlast = 0x4A,            // EMP_BIG
    mapSmallBuildingExplosion = 0x55,  // BLD_EXPL_SML
    mapMediumBuildingExplosion = 0x56, // BLD_EXPL_MED
    mapLargeBuildingExplosion = 0x57,  // BLD_EXPL_BIG
    mapAmbientAnim = 0x72,         // AMBIENT_ANIM (unidade-efeito: toca uma animação)
};
constexpr int kFirstBuilding = 0x15;  // 21
constexpr int kLastBuilding  = 0x3A;  // 58
inline bool mapIdIsBuilding(map_id t) { return t >= kFirstBuilding && t <= kLastBuilding; }

inline bool mapIdIsVehicle(map_id t) { return t >= mapCargoTruck && t <= mapSmallCapacityAirTransport; }

// Flags do registro de unidade (+0x44 no original).
enum UnitFlags : uint32_t {
    UNIT_VEHICLE  = 1u << 1,   // bit1
    UNIT_BUILDING = 1u << 2,   // bit2
    UNIT_LIGHTS   = 1u << 0,   // bit0 (ligado junto com POWERED; vai p/ [+0x40] bit31)
    // Recursos alocados ao prédio pela economia (sub_471F30): energia via
    // vt+0xF0 (liga 0x2000|1), trabalhadores via vt+0xF4, cientistas via vt+0xF8.
    UNIT_POWERED  = 0x2000u,   // com energia        (vt+0xB8 sub_483940 / vt+0xBC sub_483980)
    UNIT_WORKERS  = 0x4000u,   // com trabalhadores  (vt+0xAC sub_4838B0 / vt+0xA8 sub_483910)
    UNIT_EFFECT_COLOR = 0x4000u,   // (efeito) usa a cor [+0x60] (sub_405160)
    UNIT_SCIENTISTS = 0x8000u, // com cientistas     (vt+0xB4 sub_483880 / vt+0xB0 sub_4838E0)
    UNIT_SOLAR_IDLE = 0x10000u,// Solar Array sem satélite (sub_46E7F0)
    UNIT_DISABLED_TIMERS = 0xC0000u, // 0x40000/0x80000: contadores de EMP etc. (sub_43DE50)
    UNIT_DEAD     = 1u << 17,  // bit17 (morta/armazenada; IsLive testa isto)
    UNIT_DISMANTLED = 0x400000u,   // desmontado (case 0x22): sem explosão (sub_4831A0)
    UNIT_EFFECT_HIDDEN = 0x10000000u, // efeito não desenhado (sub_405000 arg_C)
    // Combate (ctors: veículo sub_44B440 |= 2|0x400; prédio sub_482F30 |=
    // 0x20000000|4|0x400; veículo armado off_4D0650 |= 0x800000|0x10; Guard Post
    // sub_42CD90 |= 0x800000|0x80).
    UNIT_FACTORY    = 0x08u,       // prédio com baias (fábrica-base sub_443260: 0x2D/2E/2F/30/37)
    UNIT_ARMED      = 0x10u,       // veículo com arma (Lynx/Panther/Tiger/Scorpion, SCAT)
    UNIT_TURRET_BLD = 0x80u,       // prédio com arma (Guard Post); no projétil: +1 alcance, dano x1.5
    UNIT_DAMAGEABLE = 0x400u,      // alvo de dano/armas (veículos e prédios)
    UNIT_FIRE_GROUND = 0x2000u,    // (veículo) ordem de atirar no chão
    UNIT_ALT_BARREL = 0x4000u,     // (veículo) alterna o cano a cada tiro (Tiger)
    UNIT_HIT_FLASH  = 0x100000u,   // atingido por ESG ([+0x58] ticks)
    UNIT_AUTO_TARGET = 0x800000u,  // alvo automático (prédio: AutoTargeted; veículo armado)
    UNIT_IN_FLIGHT  = 0x800000u,   // (projétil) voo balístico (sub_4A3A90)
    UNIT_EMP        = 0x80000u,    // paralisado por EMP
};

// Ações ([+0x21]) usadas pelas estruturas — switch de sub_43ACA0.
enum UnitAction : uint8_t {
    ACT_BUILD     = 0x21,   // construção (case 33)
    ACT_DISMANTLE = 0x22,   // desmontagem (case 34)
    ACT_DIE       = 0x2F,   // morte (case 47) — Kill/sub_43A890
};
// Sub-estados ([+0x22]) — switch de sub_43DE50.
enum UnitSubState : uint8_t {
    SUB_IDLE   = 0,
    SUB_WAIT   = 6,     // espera [+0x3C] ticks (cases 6/9)
    SUB_REMOVE = 0x0B,  // sub_43DE50 retorna 1 → o laço remove a unidade
    SUB_DRIFT  = 0x10,  // case 16: vt[32] a cada tick (meteoro/raio/tornado andando)
};

// Bloco de ordem — porta o bloco alocado em unit+0x38 por sub_4230F0 (@asm 57211;
// no original 0x35 dwords = 212 bytes, alocado de unk_5565E0 via sub_446A50).
// Guarda a lista de waypoints da ordem de movimento e o progresso do pathfinder.
struct OrderBlock {
    int count       = 0;   // [0x00] nº de waypoints
    int countMinus1 = 0;   // [0x04] count-1 (índice do último waypoint)
    uint32_t flags  = 0;   // [0x34] (|= 1 ao setar a ordem)
    int multiFlag   = 0;   // [0x3C] (setado p/ grupos > 4 unidades)
    int field40     = 0;   // [0x40]
    // [0x14...] lista de LOCATIONs EMPACOTADOS (ver packLocation): x em bits5-14,
    // y em bits20-28, campo de 29 bits. Copiada verbatim do buffer de comando.
    std::vector<uint32_t> waypoints;
    int curWaypoint = 0;   // [0x08] índice do waypoint atual (sub_44BEC0 avança)
};

// Registro de unidade — no original é um array global (dword_54EC08) de registros
// de 120 bytes (0x78); aqui modelamos os campos semânticos decodificados (offset
// real no comentário). O tipo, que no original vem do vtable (+0x00), guardamos direto.
struct UnitRecord {
    map_id   type   = kMapIdNone;  // (+0x00 vtable→GetType) tipo da unidade
    int      id     = -1;          // +0x04 (-1/0xFFFFFFFF = slot livre)
    int      pixelX = 0;           // +0x14 (tile<<5)
    int      pixelY = 0;           // +0x18 (tile<<5)
    uint8_t  owner  = 0;           // +0x1D nibble baixo = dono (jogador)
    uint8_t  field20 = 0;          // +0x20 (=1 quando há ordem ativa)
    uint8_t  action = 0;           // +0x21 (tipo da ação/ordem atual; 2 = move)
    uint8_t  field22 = 0;          // +0x22 (sub-estado; 0x0D = não-comandável)
    uint16_t cargo  = 0;           // +0x24 (cargo/arma, map_id)
    int      field3C = 0;          // +0x3C (progresso/contador da ordem)
    uint32_t flags  = 0;           // +0x44
    uint8_t  rotation = 0;         // +0x1C (direção<<5 no original)
    int      moveClass = 0;        // classe de movimento (no original [objUnit+0x1E8];
                                   // índice na tabela de custo; vem do descritor do tipo)
    uint8_t  curDir = 0;           // direção atual (0..7; no original rotation>>5, +0x1C)
    uint16_t field4E = 0x7FFF;     // +0x4E (usado por sub_44BD60; 0x7FFF = padrão)
    int16_t  damage = 0;           // +0x1E dano acumulado (HP atual = máx - dano)
    uint8_t  field23 = 0;          // +0x23 sub-estado anterior
    uint32_t state34 = 0;          // +0x34 PRÉDIO: (estado<<16)|contador (sub_408930)
    int      builderId = 0;        // +0x48 ConVec que está construindo
    int      buildTime = 0;        // +0x5C duração total da construção (ticks)
    uint16_t ambientCursor = 0;    // +0x54 PRÉDIO: (contagem<<8)|índice em byte_4E4138
    int      extraWorkers = 0;     // +0x60 PRÉDIO: trabalhadores extras (University: alunos)
    int8_t   extraScientists = 0;  // +0x64 PRÉDIO: cientistas extras (labs: pesquisa)
    int      labTopic = 0;         // +0x60 LAB: tópico em pesquisa (índice da tecnologia)
    int      trainTime = 0;        // +0x64 UNIVERSITY: duração do treino
    int      mineYield = 0;        // +0x70 MINA: variante do veio (0/1/2) p/ estatísticas
    uint8_t  netConnected = 0;     // [+0x38]->+0x10: componente de rede ligado a um CC operando
    int      netComp = -1;         // [+0x38]: componente de rede (índice em ColonyState::components)
    uint8_t  serial = 0;           // +0x2C nº de série exibido ("# 1-NN")
    uint32_t effectAnim = 0;       // +0x54 EFEITO: (divisor<<24)|anim (sub_405000)
    int      effectStart = 0;      // +0x58 EFEITO: tick inicial (clock + atraso)
    int      effectColor = -1;     // +0x60 EFEITO: cor (se UNIT_EFFECT_COLOR)
    OrderBlock order;              // +0x38 (ponteiro no original; aqui embutido)
    bool hasOrder = false;         // order block alocado? (unit+0x38 != null)

    // --- Execução de rota (consumo da rota do pathfinder) --------------------
    // NB: executor sobre a rota REAL de pfSearchRoute; o integrador byte-exato
    // (sub_44B650, com o modelo completo do objeto Unit/descritor) é refinamento.
    std::vector<int> routeDirs;    // direções (0..7) da rota atual ([st+0x7C], n +0x74)
    int   routeIdx   = 0;          // próximo passo da rota ([st+0x78])
    // Cache da busca longa (sub_43F5B0): direções [st+0xD4] (n +0xBC), lidas
    // até +0xC0, posição de leitura +0xC4/+0xC8 (pfPlanRoute).
    std::vector<uint8_t> routeCache;
    int   cacheIdx = 0, cacheX = 0, cacheY = 0;
    // sub_44C350 / sub_44B650: zera +0x74/+0x78 e o cache (+0xBC).
    void clearRoute() { routeDirs.clear(); routeIdx = 0; routeCache.clear(); cacheIdx = 0; }
    // Passo em curso — estado de movimento [unit+0x34] do original (sub_44B650
    // monta, sub_44C0B0 aplica 1x por tick):
    int      stepTicks = 0;        // [st+0x64] ticks restantes do passo
    int      velX = 0, velY = 0;   // [st+0x58]/[st+0x5C] velocidade 16.16 (px/tick)
    uint16_t fracX = 0, fracY = 0; // [st+0x44]/[st+0x46] fração 16 bits da posição
    int      turnTicks = 0;        // [st+0x68] ticks de giro restantes
    int8_t   turnStep = 0;         // [st+0x60] giro por tick (com sinal)
    // [st+0x6C] bump retries: a step into a tile held by a bumpable unit waits
    // and retries up to 20 times (Unit::Vehicle::StartNextTileStep). Zeroed when
    // a step completes. One byte, in what was padding, so sizeof (and the save
    // layout signature) is unchanged.
    uint8_t  bumpRetries = 0;
    int      stepDestX = 0, stepDestY = 0;  // [st+0x48]/[st+0x4C] pixel do destino

    // --- Combate (src/game/weapon.cpp) ---------------------------------------
    int16_t  lastAttacker = 0;     // +0x26 último atacante (crédito; expira em 700 ticks)
    int      lastAttackTick = 0;   // +0x28 tick do último ataque
    uint8_t  turretDir = 0;        // +0x2D direção da torre (0..255)
    int16_t  reload = 0;           // +0x2E recarga restante (ticks)
    uint8_t  refCount = 0;         // +0x32 referências (alvos/projéteis); slot não é reusado
    int16_t  orderLo = 0;          // +0x48 (veículo) alvo da ORDEM: id ou x do tile (+0x4A = field4A)
    int16_t  field4A = 0x7FFF;     // +0x4A (0x8000 = unidade, y do tile, 0x7FFF = nenhum)
    int16_t  guardLo = 0, guardHi = 0x7FFF;   // +0x50/+0x52 unidade guardada (ação 0x25)
    int16_t  patrolStart = 0;      // +0x5E (veículo) waypoint onde a patrulha recomeça (ação 0x28)
    // Caminhão (src/game/mining.cpp): pedido da doca +0x5C ((qtd << 4) | tipo),
    // carregando na mina +0x6D; rota de minério (ação 0x27): índices dos pontos
    // da mina +0x62 e do smelter +0x64, ids da mina +0x66 e do smelter +0x68.
    uint16_t cargoRequest = 0;
    uint8_t  dockLoad = 0;
    int16_t  routeMineIdx = 0, routeSmelterIdx = 0, routeMine = 0, routeSmelter = 0;
    // Obras (src/game/earthwork.cpp; retângulo em buildL..buildB = [+0x62..+0x68]):
    // Earthworker célula atual +0x6C/+0x6E e código de percurso +0x70; dozer +0x6A.
    int16_t  wallCurX = 0, wallCurY = -1;
    // Grupo de IA (src/game/ai_group.cpp): +0x30 stub do grupo (0xFF = nenhum).
    uint8_t  groupStub = 0xFF;
    int16_t  empTicks = 0;
    // Veículo atingido por espuma (0x40000): contador +0x54 e tick do início +0x60;
    // por ESG (0x100000): contador +0x58 (prólogo de sub_43DE50, 1x a cada 8 ticks).
    int16_t  foamTicks = 0;
    int      foamStart = 0;
    int16_t  esgTicks = 0;
    int      launchDefense = 0;     // +0x54 míssil (0x69..0x6B): Meteor Defense que atirou
    // Destroço (Wreckage 0x54, vt off_4CF400): +0x58 máscara de quem descobriu,
    // +0x5C tecnologia (vt[0xA0]).
    uint32_t wreckMask = 0;
    int      wreckTech = 0;         // +0x56 (contador do EMP; usado pela escolha do Spider)
    uint8_t  wallCode = 0, dozeCode = 0;
    int16_t  target4C = 0;         // +0x4C alvo: id da unidade ou x do tile
                                   // (+0x4E field4E: 0xFFFF = unidade, y do tile, 0x7FFF = nenhum)
    // Projétil (unidades de arma 0x3B..0x57):
    int      sourceId = 0;         // +0x48 atirador (vt[37] sub_4A27E0)
    int      impactX = 0, impactY = 0; // +0x34/+0x38 ponto de impacto (pixel)
    int      exploded = 0;         // +0x54
    int16_t  flightTicks = 0;      // +0x58 duração do voo
    int8_t   shooterBit = 0;       // +0x5A EMP: 1 << dono das estatísticas do atirador (vt[37] sub_4A47F0)
    int8_t   esgOwnerBit = 0;      // +0x6C ESG: 1 << dono do atirador (vt[37] sub_480A80)
    uint16_t esgMask = 0;          // +0x6E ESG: minas ainda armadas (0x1FFF ao explodir; flag 0x8000)
    int      vecX = 0, vecY = 0;   // +0x5C/+0x60 impacto - origem
    int      velQX = 0, velQY = 0; // +0x64/+0x68 vec*4/duração (rastro)
    bool     rawPixel = false;     // pixelX/Y já no sentido do original (centro, sem +16)

    // --- Desastres (0x4C..0x50) --------------------------------------------
    int      quakeDir = 0;         // +0x54 terremoto: direção do tremor (256 - rand(512))
    int      quakeAmp = 0;         // +0x58 terremoto: amplitude (magnitude*4 + 30)
    // Meteoro 0x50 / raio 0x4C / tornado 0x4D (rawPixel; x do port; os tiles
    // abaixo ficam em x INTERNO, como no original):
    int      dsTick = 0;           // +0x54 tick de início do efeito
    int      dsAux = 0;            // +0x58 meteoro: defesa que atirou; raio: tick do
                                   //       próximo raio; tornado: tick do início da curva
    int16_t  dsTX = 0, dsTY = 0;   // meteoro +0x34/+0x36 alvo; raio +0x60/+0x62 e
                                   // tornado +0x61/+0x63 tile de partida
    int16_t  dsEndX = -1, dsEndY = -1; // raio +0x64/+0x66, tornado +0x65/+0x67 (-1 = sorteado)
    uint8_t  dsDir = 0;            // raio +0x1C / tornado +0x5C direção 0..255
    int16_t  dsAccX = 0, dsAccY = 0;   // raio +0x5C/+0x5E, tornado +0x5D/+0x5F fração 8.8

    // --- Beacon de mineração (0x51..0x53; vt de off_4CF218) -----------------
    int8_t   beaconRare = 0;       // +0x64 1 = minério raro
    int      beaconYield = 0;      // +0x5C 0/1/2 (rendimento → anim 0x73A..0x73F)
    int      beaconVariant = 0;    // +0x60
    uint8_t  beaconSet = 0;        // +0x65
    uint8_t  beaconHasMine = 0;    // +0x66
    uint32_t surveyedMask = 0;     // +0x67 jogadores que pesquisaram o beacon
    int      beaconLink = 0;       // +0x58
    // Mina (sub_44ADC0, vt[63]): mineYield (+0x70) acima; variante e beacon.
    int      mineVariant = 0;      // +0x6C
    int      mineLink = 0;         // +0x68
    int      mineBeacon = 0;       // +0x60
    // Fábrica (Unit::SetFactoryCargo): tipo +0x61+baia, carga +0x48+baia, foguete +0x6E.
    uint8_t  kitWeapon = 0;        // +0x4F ConVec: arma do kit (Unit::SetCargo)
    uint8_t  bayType[6] = {};
    uint8_t  bayBits = 0;          // +0x67 PRÉDIO: máscara por baia (troca com o bit 0x200000 do veículo)
    // Doca (veículo): baia da troca +0x5C e tick +0x5E (sub_415880); sub-estado 0x0D.
    int16_t  dockBay = 0;
    // ConVec construindo (ação 6, sub_423310): retângulo área + anel em [+0x62..+0x69].
    int16_t  buildL = 0, buildT = 0, buildR = 0, buildB = 0;
    int16_t  buildGroup = -1;       // +0x6A ConVec/Robo-Miner/GeoCon: grupo do prédio (pacote 6)
    int16_t  dockTick = 0;
    // Produção da fábrica (pacote 9 / ação 9): tipo em produção +0x60 (byte),
    // arma em +0x4F (kitWeapon), duração total +0x68, grupo de IA +0x6C.
    uint8_t  produceType = 0;
    int      produceTotal = 0;
    int16_t  produceGroup = -1;
    uint8_t  bayCargo[6] = {};
    int      garageSlot[6] = {};    // +0x60.. GARAGEM: veículos guardados (ids; 0 = baia vazia)
    int      padCargo = 0;
    int16_t  padPayload = 0;        // +0x72 Spaceport: carga do SULV (satélite/módulo)
    int16_t  padReserved = 0;       // +0x74 Spaceport: reservado p/ um RLV voltando (sub_47FB90)

    // --- Lista por Y (dword_54EC0C; [+0x04]/[+0x08]) e slot livre -------------
    int      lnext = 0, lprev = 0;
    int      freeTick = 0;         // [+0x08] tick em que o slot foi liberado

    // Alvo da ordem como o dword [+0x48].
    uint32_t orderTarget() const { return (static_cast<uint32_t>(static_cast<uint16_t>(field4A)) << 16) | static_cast<uint16_t>(orderLo); }
    void setOrderTarget(uint32_t v) { orderLo = static_cast<int16_t>(v & 0xFFFF); field4A = static_cast<int16_t>(v >> 16); }
    uint32_t guardTarget() const { return (static_cast<uint32_t>(static_cast<uint16_t>(guardHi)) << 16) | static_cast<uint16_t>(guardLo); }
    // Alvo como o dword [+0x4C]: bit31 = unidade.
    uint32_t target() const { return (static_cast<uint32_t>(field4E) << 16) | static_cast<uint16_t>(target4C); }
    bool hasTarget() const { return field4E != 0x7FFF; }
    bool targetIsUnit() const { return (field4E & 0x8000) != 0; }
};

// Prédio (sub_4369E0): [+0x14] = (x*2 + (largura & 1)) * 16 — meio tile só nas
// dimensões ímpares; nas pares o centro cai na borda do tile central.
int buildingCenterOfsX(map_id type);
int buildingCenterOfsY(map_id type);

// Posição no sentido do original ([+0x14]/[+0x18] = centro): o port guarda o
// canto do tile p/ veículos/prédios/efeitos (+16 = centro; prédio: ver acima);
// projéteis já guardam o pixel do original.
inline int unitOX(const UnitRecord& r) {
    return r.rawPixel ? r.pixelX : r.pixelX + ((r.flags & UNIT_BUILDING) ? buildingCenterOfsX(r.type) : 16);
}
inline int unitOY(const UnitRecord& r) {
    return r.rawPixel ? r.pixelY : r.pixelY + ((r.flags & UNIT_BUILDING) ? buildingCenterOfsY(r.type) : 16);
}

// Lista de unidades ordenada por Y — dword_54EC0C. Slot 0 é a sentinela.
// Inserção (sub_439910 @439A46): antes do 1º nó com y >= y novo. Reordenação
// após o laço de tick: sub_43E7E0. O laço de tick (sub_43AC30) e a busca de
// alvo (sub_4A6DD0/sub_4A6AA0) percorrem esta lista.
void unitListInsert(int id);
void unitListRemove(int id);
void unitListSort();
int  unitListFirst();          // 1º da lista (0 = vazia)
int  unitListNext(int id);     // próximo (0 = fim)
// sub_439B90 (parte do slot): libera o registro ([+0x04] = -1, [+0x08] = tick),
// preservando [+0x32], e põe o slot no anel de livres.
void unitFreeSlot(int id);

// Footprint (tamanho em tiles) de um prédio — X-size × Y-size de building.txt
// (popula o descritor off_4E1340[+0x248/+0x24A] no original). Devolve 1×1 p/
// tipos fora da faixa de prédio. A LOCATION do prédio é o CENTRO do footprint.
void buildingFootprint(map_id type, int& w, int& h);

// Pool global de unidades (índice = id da unidade). O id 0 é "nenhuma unidade".
extern std::vector<UnitRecord> g_units;
void resetUnits();

// Unit — wrapper do id da unidade (porta a classe Unit, asm @189861+). this+0=id.
class Unit {
public:
    Unit() : m_id(0) {}
    explicit Unit(int id) : m_id(id) {}

    void SetId(int id) { m_id = id; }           // ??SetId
    int  id() const { return m_id; }
    UnitRecord* rec() const;                     // registro ou nullptr se inválido

    int IsLive();                                // vivo? (zera o wrapper se não)
    LOCATION Location() const;                   // (pixelX>>5, pixelY>>5)
    int OwnerID() const;                         // dono & 0x0F
    map_id GetType() const;
    int IsBuilding() const;                      // flags bit2
    int IsVehicle() const;                       // flags bit1
    map_id GetCargo() const;
    void SetCargo(map_id c);
    void DoDeath();                              // marca morta (flags bit17)

    // DoMove — porta Unit::DoMove @asm 190538. Monta um CommandPacket tipo 2
    // (MOVE) com o tile alvo empacotado e despacha pelo objeto de comando do
    // dono (sub_40E210). Valida slot vivo + alvo dentro do mapa, como o original.
    void DoMove(LOCATION dest);

private:
    int m_id;   // this+0 = id da unidade
};

// CreateUnit (porta TethysGame::CreateUnit @195141, versão pragmática): aloca um
// slot no pool e preenche o registro (tipo, posição, dono, cargo, rotação),
// marcando prédio/veículo pela faixa do tipo; grava o id em `out`. Simplifica os
// alocadores/vtables/reserva de tiles do original, mas o resultado observável é
// o mesmo (unidade viva, do tipo, na posição, do dono). Retorna true em sucesso.
bool createUnit(Unit& out, map_id type, LOCATION loc, int owner,
                map_id cargo = kMapIdNone, int rotation = 0);

} // namespace op2
