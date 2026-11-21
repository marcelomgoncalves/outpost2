#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "unit.h"

namespace op2 {

// Armas e combate — porta 1:1 do disassembly (ver [[op2-combat]] na memória).
//
// Cadeia por tick (laço sub_43AC30, na ordem da lista por Y):
//   veículo armado (flags 0x10, ou 0x80 nos prédios-torre) — cauda de
//   sub_43ACA0: off_4E1340[arma]->vt[5] = sub_4A7AF0: recarga [+0x2E], busca
//   de alvo a cada 8 ticks (vt[6]: sub_4A6DD0 com linha de visada / sub_4A6AA0
//   sem), alcance (sub_485140) e disparo (vt[4] do veículo = sub_484F80), que
//   cria a unidade-projétil do tipo da arma ([+0x24]).
//   projétil — ação 0x36 (case 54 de sub_43ACA0) com os sub-estados de
//   sub_43DE50: 0x12 gira a torre do atirador até o alvo, lança (vt[31]:
//   sub_4A3A90 balístico / sub_4A3E00 instantâneo, com mira antecipada
//   sub_4A6940), 0x13 alinha, 0x0F voa, explode (vt[34] = sub_4A35C0: dano em
//   área com queda pela distância e armadura) e espera a animação (vt[36]).
//   morte do veículo — case 47 (loc_43D8A3): explosão AMBIENT_ANIM, marca no
//   terreno, crédito de moral ao atacante ([+0x26]) e mensagem.
//
// Armas portadas: LASER 0x3D, MICROWAVE 0x3E, RAIL_GUN 0x3F, CANNON (RPG) 0x40,
// THORS_HAMMER 0x48, SCORPION_WEAPON 0x49, BFG 0x4B.

// Projétil de uma das armas portadas?
bool isWeaponProjectile(int type);
// Nome de exibição da arma (desc+0x1F8) — "Laser", "RPG"...
const char* weaponDisplayName(int type);

// Cauda de sub_43ACA0: tick da arma do veículo/prédio armado (sub_4A7AF0).
void weaponTick(int id);
// vt[20] do veículo armado (sub_485340), chamado 1x a cada 8 ticks pelo
// prólogo de sub_43DE50. Só o ramo de aquisição de alvo está portado; a
// retaliação (ordem de ataque 0x24, sub_423850) depende das ordens de ataque.
void weaponAutoEngage(int id);
// Prólogo de sub_43DE50 (1x a cada 8 ticks): expira o crédito do atacante
// ([+0x26]) após 700 ticks e chama vt[20].
void unitPeriodicTick(int id);

// Case 54 de sub_43ACA0 (projétil, ação 0x36).
void weaponProjectileAction(int id);
// Sub-estados 0x0F/0x12/0x13 de sub_43DE50 (projétil). true se tratou.
bool weaponProjectileSubstate(int id);
// vt[19] (destrutor) do projétil: solta a referência ao atirador ([+0x32]).
void weaponUnitRemoved(int id);

// sub_43A6A0 (comando 0x14, Unit::DoSelfDestruct): Starflare/Supernova detonam a
// carga; os demais criam a SCUTTLE_DESTRUCT (0x45) e morrem.
void unitSelfDestruct(int id);
// vt[0x60] do veículo (sub_44B5C0): espuma por `ticks` passadas de 8 ticks.
void unitApplyFoam(int id, int ticks);

// Case 47 de sub_43ACA0 para veículo (loc_43D8A3).
void vehicleDeath(int id);

// ---- Desenho ----------------------------------------------------------------
// Operações de desenho em coordenadas de MUNDO (pixel do original, centro).
struct WeaponDrawOp {
    enum Kind { SPRITE, LASER_LINE, MICROWAVE_LINE, BOLT, SOUND } kind = SPRITE;   // SOUND: som `anim` em (x, y)
    int anim = -1, frame = 0;       // SPRITE: animação real (sem viés) e frame
    int x = 0, y = 0;               // SPRITE: ponto; linhas: início
    int x2 = 0, y2 = 0;             // linhas: fim
    int phase = 0;                  // MICROWAVE_LINE: rotação do padrão; BOLT: ângulo inicial
    uint16_t orColor = 0x7C00;      // LASER_LINE: cor 555 somada por OR (sub_41E8F0)
    uint32_t seed = 0;              // BOLT: semente da RNG do desenho (unk_5737E8)
    int segs = 0;                   // BOLT: segmentos ([+0x58])
};
// Raio do Thor's Hammer (sub_48AD70 → sub_48A850): subdivide de (x,y) a
// (x2,y2) com a RNG própria do desenho e risca com sub_41E620 (2x2, 1º nível) /
// sub_41E7B0 (1 px, ramos) na superfície de vw x vh com a câmera em (camX, camY).
// put(x, y, cor555) recebe pixels já dentro da superfície.
void weaponDrawBolt(const WeaponDrawOp& op, int camX, int camY, int vw, int vh,
                    const std::function<void(int, int, uint16_t)>& put);
// vt[6] do projétil (sub_4A3420 laser, sub_4A3000 microwave, sub_4A2A00 rail
// gun, sub_4A2D50 genérico).
void weaponDrawOps(int id, std::vector<WeaponDrawOp>& ops);
// Torre do tanque (sub_484CF0): animação real da torre e deslocamento em
// relação ao centro do chassi (ponto de encaixe do frame do chassi). false se
// o veículo não tem torre (sem arma ou chassi sem tabela).
bool tankTurret(const UnitRecord& r, int bodyAnimReal, int bodyFrame,
                int& turretAnimReal, int& offX, int& offY);
// vt[8] do Guard Post (sub_42D250): pronto, o sprite é a torre da arma (vt[7]
// do descritor) na direção [+0x2D], frame 0. false em obra / outro tipo.
bool guardPostSprite(const UnitRecord& r, int& animReal);
// Torre do ícone de veículo (sub_458EF0): direção 0 da tabela da arma p/ o
// chassi, na facção pedida (anim crua; 0 se não há).
int weaponTurretIconRaw(int weapon, int chassis, bool eden);
// sub_484A90: frame do chassi do tanque (em movimento: (id + tick/2) % nF).
bool isTankChassis(int type);

// Ordens de ataque/guarda (ações 0x24/0x25):
// sub_423850 — alvo da ordem [+0x48], rota até ele ou arma apontada se ao alcance.
void unitOrderAttack(int id, int type, const std::vector<uint32_t>& wps, uint32_t tgt, int unitIdx, int adj);
// sub_40E210 casos 36/37 (pacotes 0x24 atacar / 0x25 guardar).
void processAttackPacket(int owner, int type, const std::vector<int>& ids,
                         const std::vector<uint32_t>& wps, uint32_t tgt);
// sub_43ACA0: ramo [+0x20] dos casos 36/37 e o tick da ação 0x24 (jpt_43B312 case 36).
void weaponAttackStart(int id);
void weaponAttackTick(int id);
void weaponGuardTick(int id);                     // case 37 (ação 0x25)
// sub_4534F0: pacote 0x25 c/ a unidade aliada sob o clique (ou nenhuma).
void sendGuardTarget(int me, const std::vector<int>& sel, int tileX, int tileY,
                     const std::vector<uint32_t>& wps = {});   // wps: os pontos do Ctrl (sub_451420)

// Utilitários compartilhados com os desastres:
int8_t  dirCos(uint8_t dir);                 // byte_4E9AF8 (cos * 64)
int8_t  dirSin(uint8_t dir);                 // byte_4E9BF8 (sen * 64)
uint8_t dirOfVec(int dx, int dy);            // sub_45C4F0
// vt[0x6C]: tile da unidade mais próximo de (sx, sy) (prédio: do footprint).
void unitNearestTile(const UnitRecord& t, int sx, int sy, int& ox, int& oy);
// sub_439810: ponto de rota do alvo (lugar: o tile; unidade: o tile mais próximo).
uint32_t unitTargetWaypoint(const UnitRecord& u, uint32_t tgt);
int artLastFrameRaw(int raw);                // sub_404C70 (nFrames-1, animação crua)
const int8_t* artFrameOffsets(int raw, int frame);   // sub_404DF0 (bytes x1,y1,x2,y2)
// sub_48BAE0 + sub_48B6F0: tile queimado p/ o tile dado (0 = nenhum).
int scorchTileFor(int tile);
// sub_436EA0: linha de visada entre tiles do port (≠0 = livre).
int mapLineOfSight(int x0, int y0, int x1, int y1);
// desc+0x250 da arma (1 = sem linha de visada no teste de alcance); -1 se o tipo não é arma.
int weaponRangeMode(int type);

} // namespace op2
