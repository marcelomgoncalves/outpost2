#include "weapon.h"

#include "../net/net_game.h"
#include "../audio/sound.h"

#include "command.h"
#include "commands.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "building_sheet.h"
#include "colony.h"
#include "game_map.h"
#include "game_rng.h"
#include "messages.h"
#include "player.h"
#include "structure.h"
#include "tethys_game.h"
#include "terrain_cost.h"
#include "vehicle_anim.h"
#include "disaster.h"
#include "missile.h"
#include "walls.h"
#include "../gfx/artfile.h"

namespace op2 {

static const int kAnimBias = 0x65;   // sub_403E00: `lea eax,[ecx-65h]`

// ---- Classes de arma --------------------------------------------------------
// Por map_id: vt[4] do descritor (+0x250 modo de alcance, +0x254/+0x258 sons),
// vt[31] da unidade (lançamento), vt[6] do descritor (busca de alvo), vt[6] da
// unidade (desenho), vt[35] (animação da explosão) e vt[11] do descritor
// (antecipação). Valores lidos do OUTPOST2.EXE.
enum Launch { LAUNCH_BALLISTIC /*sub_4A3A90*/, LAUNCH_INSTANT /*sub_4A3E00*/, LAUNCH_THOR /*sub_48AF60*/ };
enum Finder { FIND_LOS /*sub_4A6DD0*/, FIND_NEAR /*sub_4A6AA0*/, FIND_BOX /*sub_4A7400*/,
              FIND_EMP /*sub_4A7060*/, FIND_FOAM /*sub_4A7270*/, FIND_NONE /*sub_4471E0*/ };
enum DrawKind { DRAW_PROJECTILE /*sub_4A2D50*/, DRAW_LASER /*sub_4A3420*/,
                DRAW_MICROWAVE /*sub_4A3000*/, DRAW_RAILGUN /*sub_4A2A00*/,
                DRAW_SCORPION /*sub_4A31D0*/, DRAW_THOR /*sub_48AD70*/,
                DRAW_ESG /*sub_480DB0*/, DRAW_ACID /*sub_401560*/,
                DRAW_DESTRUCT /*sub_4A3370 / sub_4A64D0*/ };
struct WeaponClass {
    int type;
    int rangeMode;          // desc+0x250: 1 = sem linha de visada no teste de alcance
    int fireSound, impactSound;   // desc+0x254 / +0x258
    Launch launch;
    Finder finder;
    DrawKind draw;
    int explodeAnim;        // vt[35] (cru, com viés)
    int leadFixed;          // vt[11]: valor fixo, ou 0 = max(dist/16 + leadAdd, leadMin)
    int leadAdd, leadMin;
};
static const WeaponClass kClasses[] = {
    // EMP: sub_4A5670 / sub_4A3A90 / sub_4A7060 / sub_4A2D50 / sub_4A6810 / sub_447100;
    // fire tick sub_4A7580, impacto sub_4A4490, vt[37] sub_4A47F0.
    {0x3C, 1, 0xE6, 0xE7, LAUNCH_BALLISTIC, FIND_EMP, DRAW_PROJECTILE, 0x2FF, 0, 1, 3},
    // STICKYFOAM: sub_4A61C0 / sub_4A3A90 / sub_4A7270 / sub_4A2D50 / sub_4A6910 / sub_447240;
    // fire tick sub_4A7950, impacto sub_4A4F10.
    {0x47, 1, 0xF2, 0xF3, LAUNCH_BALLISTIC, FIND_FOAM, DRAW_PROJECTILE, 0x47D, 0, 1, 3},
    // Autodestruições: vt[32] sub_4A4030 (explode no atirador), impacto sub_4A4090,
    // antecipação sub_4470B0 (2). SELF_DESTRUCT (Starflare) sub_4A5EF0 / busca sub_4471E0;
    // SELF_DESTRUCT_ADV (Supernova) sub_4A6080 / sub_447200; TURRET_DESTRUCT(_ADV)
    // (Guard Post) sub_4A5E50 / sub_4A5EA0 (sem +0x250) / sub_4A7400; SCUTTLE_DESTRUCT
    // sub_4A6170 / sub_4A7400. Explosões vt[35] sub_4A6890/68B0/6870/68D0/68F0.
    {0x41, 0, 0xEE, 0xFB, LAUNCH_INSTANT, FIND_NONE, DRAW_DESTRUCT, 0x2FB, 2, 0, 0},
    {0x42, 0, 0xEF, 0xFB, LAUNCH_INSTANT, FIND_NONE, DRAW_DESTRUCT, 0x5B5, 2, 0, 0},
    {0x43, 0, 0xEE, 0xFB, LAUNCH_INSTANT, FIND_BOX, DRAW_DESTRUCT, 0x2FB, 2, 0, 0},
    {0x44, 0, 0xEE, 0xFB, LAUNCH_INSTANT, FIND_BOX, DRAW_DESTRUCT, 0x5B5, 2, 0, 0},
    {0x45, 0, 0xEF, 0xFB, LAUNCH_INSTANT, FIND_BOX, DRAW_DESTRUCT, 0x2FC, 2, 0, 0},
    // ACID: sub_4A5480 / sub_4A3A90 / sub_4A6AA0 / sub_401560 / sub_401910 / sub_4470D0;
    // impacto nullsub_5, nuvem no vt[20] sub_401050, duração vt[36] sub_4013D0.
    {0x3B, 1, 0xE4, 0xE5, LAUNCH_BALLISTIC, FIND_NEAR, DRAW_ACID, 0x414, 0, 5, 6},
    // SPAM (ESG): sub_480AC0 / sub_4A3A90 / sub_4A7400 / sub_480DB0 / sub_4A2870 / sub_447460;
    // impacto nullsub_122, minas no vt[20] sub_481170, duração vt[36] sub_480C10 (0xD8).
    {0x46, 1, 0xF0, 0xF1, LAUNCH_BALLISTIC, FIND_BOX, DRAW_ESG, 0x14B, 0, 4, 8},
    // LASER: sub_4A58D0 / sub_4A3E00 / sub_4A7020→sub_4A6DD0 / sub_4A3420 / sub_4A2880 / sub_447130
    {0x3D, 2, 0xE8, 0xE9, LAUNCH_INSTANT, FIND_LOS, DRAW_LASER, 0x2F6, 4, 0, 0},
    // MICROWAVE: sub_4A5A30 / sub_4A3E00 / sub_4A7040 / sub_4A3000 / sub_4A2880 / sub_447150
    {0x3E, 2, 0xEA, 0xEB, LAUNCH_INSTANT, FIND_LOS, DRAW_MICROWAVE, 0x2F6, 4, 0, 0},
    // RAIL_GUN: sub_4A5B90 / sub_4A3A90 / sub_4A7000 / sub_4A2A00 / sub_4A2870 / sub_447170
    {0x3F, 2, 0xEC, 0xED, LAUNCH_BALLISTIC, FIND_LOS, DRAW_RAILGUN, 0x14B, 0, 1, 3},
    // CANNON (RPG): sub_4A5CF0 / sub_4A3A90 / sub_4A6AA0 / sub_4A2D50 / sub_4A2870
    {0x40, 1, 0xF6, 0xF7, LAUNCH_BALLISTIC, FIND_NEAR, DRAW_PROJECTILE, 0x14B, 0, 1, 3},
    // THORS_HAMMER: sub_48A6F0 / sub_48AF60 / sub_4A7400 / sub_48AD70 / sub_4A2880 / sub_447440
    {0x48, 1, 0xF4, 0xF5, LAUNCH_THOR, FIND_BOX, DRAW_THOR, 0x2F6, 13, 0, 0},
    // SCORPION_WEAPON: sub_4A6480 / sub_4A3E00 / sub_4A6FE0→sub_4A6DD0 / sub_4A31D0 / sub_4A2880 / sub_4472A0
    {0x49, 2, 0xF8, 0xF9, LAUNCH_INSTANT, FIND_LOS, DRAW_SCORPION, 0x2F6, 4, 0, 0},
    // BFG: sub_4A6320 / sub_4A3A90 / sub_4A7400 / sub_4A2D50 / sub_4A2870 / sub_447270
    {0x4B, 1, 0xFA, 0xFB, LAUNCH_BALLISTIC, FIND_BOX, DRAW_PROJECTILE, 0x14B, 0, 1, 3},
};

// Torres — vt[7] Guard Post, vt[8] Tiger, vt[9] Panther, vt[10] Lynx do
// descritor (as vt[5] dos chassis sub_485990 / sub_4857E0 / sub_485600 chamam
// +0x20 / +0x24 / +0x28). kind 1: tabela de 16 direções indexada por
// (dir + 8) >> 4, com a 1ª tabela se o dono das estatísticas ([+0x1D] >> 4) é
// Eden (dword_56E308) e `sar` = o 2º cano do Tiger nos offsets x2/y2 do frame
// (com 0x4000); kind 2: animação fixa por facção, boca no centro (offsets 0).
// Gerado do OUTPOST2.EXE (scratchpad genweap2.py).
struct TurretDesc { uint8_t kind, sar; int16_t eden[16], ply[16]; };
struct TurretSet { int type; TurretDesc t[4]; };
static const TurretSet kTurrets[] = {
    {0x3B, {{1, 0, {0x491,0x4A0,0x49F,0x49E,0x49D,0x49C,0x49B,0x49A,0x499,0x498,0x497,0x496,0x495,0x494,0x493,0x492},
                  {0x491,0x4A0,0x49F,0x49E,0x49D,0x49C,0x49B,0x49A,0x499,0x498,0x497,0x496,0x495,0x494,0x493,0x492}},
           {1, 1, {0x6AA,0x6B9,0x6B8,0x6B7,0x6B6,0x6B5,0x6B4,0x6B3,0x6B2,0x6B1,0x6B0,0x6AF,0x6AE,0x6AD,0x6AC,0x6AB},
                  {0x6AA,0x6B9,0x6B8,0x6B7,0x6B6,0x6B5,0x6B4,0x6B3,0x6B2,0x6B1,0x6B0,0x6AF,0x6AE,0x6AD,0x6AC,0x6AB}},
           {1, 0, {0x69A,0x6A9,0x6A8,0x6A7,0x6A6,0x6A5,0x6A4,0x6A3,0x6A2,0x6A1,0x6A0,0x69F,0x69E,0x69D,0x69C,0x69B},
                  {0x69A,0x6A9,0x6A8,0x6A7,0x6A6,0x6A5,0x6A4,0x6A3,0x6A2,0x6A1,0x6A0,0x69F,0x69E,0x69D,0x69C,0x69B}},
           {1, 0, {0x69A,0x6A9,0x6A8,0x6A7,0x6A6,0x6A5,0x6A4,0x6A3,0x6A2,0x6A1,0x6A0,0x69F,0x69E,0x69D,0x69C,0x69B},
                  {0x69A,0x6A9,0x6A8,0x6A7,0x6A6,0x6A5,0x6A4,0x6A3,0x6A2,0x6A1,0x6A0,0x69F,0x69E,0x69D,0x69C,0x69B}}}},
    {0x3C, {{1, 0, {0x4A1,0x4B0,0x4AF,0x4AE,0x4AD,0x4AC,0x4AB,0x4AA,0x4A9,0x4A8,0x4A7,0x4A6,0x4A5,0x4A4,0x4A3,0x4A2},
                  {0x860,0x86F,0x86E,0x86D,0x86C,0x86B,0x86A,0x869,0x868,0x867,0x866,0x865,0x864,0x863,0x862,0x861}},
           {1, 1, {0x56B,0x57A,0x579,0x578,0x577,0x576,0x575,0x574,0x573,0x572,0x571,0x570,0x56F,0x56E,0x56D,0x56C},
                  {0x58B,0x59A,0x599,0x598,0x597,0x596,0x595,0x594,0x593,0x592,0x591,0x590,0x58F,0x58E,0x58D,0x58C}},
           {1, 0, {0x55B,0x56A,0x569,0x568,0x567,0x566,0x565,0x564,0x563,0x562,0x561,0x560,0x55F,0x55E,0x55D,0x55C},
                  {0x57B,0x58A,0x589,0x588,0x587,0x586,0x585,0x584,0x583,0x582,0x581,0x580,0x57F,0x57E,0x57D,0x57C}},
           {1, 0, {0x55B,0x56A,0x569,0x568,0x567,0x566,0x565,0x564,0x563,0x562,0x561,0x560,0x55F,0x55E,0x55D,0x55C},
                  {0x57B,0x58A,0x589,0x588,0x587,0x586,0x585,0x584,0x583,0x582,0x581,0x580,0x57F,0x57E,0x57D,0x57C}}}},
    {0x3D, {{1, 0, {0x42B,0x43A,0x439,0x438,0x437,0x436,0x435,0x434,0x433,0x432,0x431,0x430,0x42F,0x42E,0x42D,0x42C},
                  {0x42B,0x43A,0x439,0x438,0x437,0x436,0x435,0x434,0x433,0x432,0x431,0x430,0x42F,0x42E,0x42D,0x42C}},
           {1, 1, {0x66A,0x679,0x678,0x677,0x676,0x675,0x674,0x673,0x672,0x671,0x670,0x66F,0x66E,0x66D,0x66C,0x66B},
                  {0x66A,0x679,0x678,0x677,0x676,0x675,0x674,0x673,0x672,0x671,0x670,0x66F,0x66E,0x66D,0x66C,0x66B}},
           {1, 0, {0x65A,0x669,0x668,0x667,0x666,0x665,0x664,0x663,0x662,0x661,0x660,0x65F,0x65E,0x65D,0x65C,0x65B},
                  {0x65A,0x669,0x668,0x667,0x666,0x665,0x664,0x663,0x662,0x661,0x660,0x65F,0x65E,0x65D,0x65C,0x65B}},
           {1, 0, {0x65A,0x669,0x668,0x667,0x666,0x665,0x664,0x663,0x662,0x661,0x660,0x65F,0x65E,0x65D,0x65C,0x65B},
                  {0x65A,0x669,0x668,0x667,0x666,0x665,0x664,0x663,0x662,0x661,0x660,0x65F,0x65E,0x65D,0x65C,0x65B}}}},
    {0x3E, {{1, 0, {0x458,0x467,0x466,0x465,0x464,0x463,0x462,0x461,0x460,0x45F,0x45E,0x45D,0x45C,0x45B,0x45A,0x459},
                  {0x458,0x467,0x466,0x465,0x464,0x463,0x462,0x461,0x460,0x45F,0x45E,0x45D,0x45C,0x45B,0x45A,0x459}},
           {1, 1, {0x64A,0x659,0x658,0x657,0x656,0x655,0x654,0x653,0x652,0x651,0x650,0x64F,0x64E,0x64D,0x64C,0x64B},
                  {0x64A,0x659,0x658,0x657,0x656,0x655,0x654,0x653,0x652,0x651,0x650,0x64F,0x64E,0x64D,0x64C,0x64B}},
           {1, 0, {0x63A,0x649,0x648,0x647,0x646,0x645,0x644,0x643,0x642,0x641,0x640,0x63F,0x63E,0x63D,0x63C,0x63B},
                  {0x63A,0x649,0x648,0x647,0x646,0x645,0x644,0x643,0x642,0x641,0x640,0x63F,0x63E,0x63D,0x63C,0x63B}},
           {1, 0, {0x63A,0x649,0x648,0x647,0x646,0x645,0x644,0x643,0x642,0x641,0x640,0x63F,0x63E,0x63D,0x63C,0x63B},
                  {0x63A,0x649,0x648,0x647,0x646,0x645,0x644,0x643,0x642,0x641,0x640,0x63F,0x63E,0x63D,0x63C,0x63B}}}},
    {0x3F, {{1, 0, {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C},
                  {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C}},
           {1, 1, {0x5C8,0x5D7,0x5D6,0x5D5,0x5D4,0x5D3,0x5D2,0x5D1,0x5D0,0x5CF,0x5CE,0x5CD,0x5CC,0x5CB,0x5CA,0x5C9},
                  {0x5C8,0x5D7,0x5D6,0x5D5,0x5D4,0x5D3,0x5D2,0x5D1,0x5D0,0x5CF,0x5CE,0x5CD,0x5CC,0x5CB,0x5CA,0x5C9}},
           {1, 0, {0x5B8,0x5C7,0x5C6,0x5C5,0x5C4,0x5C3,0x5C2,0x5C1,0x5C0,0x5BF,0x5BE,0x5BD,0x5BC,0x5BB,0x5BA,0x5B9},
                  {0x5B8,0x5C7,0x5C6,0x5C5,0x5C4,0x5C3,0x5C2,0x5C1,0x5C0,0x5BF,0x5BE,0x5BD,0x5BC,0x5BB,0x5BA,0x5B9}},
           {1, 0, {0x5B8,0x5C7,0x5C6,0x5C5,0x5C4,0x5C3,0x5C2,0x5C1,0x5C0,0x5BF,0x5BE,0x5BD,0x5BC,0x5BB,0x5BA,0x5B9},
                  {0x5B8,0x5C7,0x5C6,0x5C5,0x5C4,0x5C3,0x5C2,0x5C1,0x5C0,0x5BF,0x5BE,0x5BD,0x5BC,0x5BB,0x5BA,0x5B9}}}},
    {0x40, {{1, 0, {0x2D5,0x2E4,0x2E3,0x2E2,0x2E1,0x2E0,0x2DF,0x2DE,0x2DD,0x2DC,0x2DB,0x2DA,0x2D9,0x2D8,0x2D7,0x2D6},
                  {0x2D5,0x2E4,0x2E3,0x2E2,0x2E1,0x2E0,0x2DF,0x2DE,0x2DD,0x2DC,0x2DB,0x2DA,0x2D9,0x2D8,0x2D7,0x2D6}},
           {1, 1, {0x5E8,0x5F7,0x5F6,0x5F5,0x5F4,0x5F3,0x5F2,0x5F1,0x5F0,0x5EF,0x5EE,0x5ED,0x5EC,0x5EB,0x5EA,0x5E9},
                  {0x5E8,0x5F7,0x5F6,0x5F5,0x5F4,0x5F3,0x5F2,0x5F1,0x5F0,0x5EF,0x5EE,0x5ED,0x5EC,0x5EB,0x5EA,0x5E9}},
           {1, 0, {0x5D8,0x5E7,0x5E6,0x5E5,0x5E4,0x5E3,0x5E2,0x5E1,0x5E0,0x5DF,0x5DE,0x5DD,0x5DC,0x5DB,0x5DA,0x5D9},
                  {0x5D8,0x5E7,0x5E6,0x5E5,0x5E4,0x5E3,0x5E2,0x5E1,0x5E0,0x5DF,0x5DE,0x5DD,0x5DC,0x5DB,0x5DA,0x5D9}},
           {1, 0, {0x5D8,0x5E7,0x5E6,0x5E5,0x5E4,0x5E3,0x5E2,0x5E1,0x5E0,0x5DF,0x5DE,0x5DD,0x5DC,0x5DB,0x5DA,0x5D9},
                  {0x5D8,0x5E7,0x5E6,0x5E5,0x5E4,0x5E3,0x5E2,0x5E1,0x5E0,0x5DF,0x5DE,0x5DD,0x5DC,0x5DB,0x5DA,0x5D9}}}},
    {0x41, {{0, 0, {0}, {0}},
           {2, 0, {0x6BD}, {0x6BA}},
           {2, 0, {0x6BC}, {0x5A2}},
           {2, 0, {0x6BC}, {0x5A2}}}},
    {0x42, {{0, 0, {0}, {0}},
           {2, 0, {0x6BB}, {0x6BB}},
           {2, 0, {0x5A3}, {0x5A3}},
           {1, 0, {0x5A3,0x5B2,0x5B1,0x5B0,0x5AF,0x5AE,0x5AD,0x5AC,0x5AB,0x5AA,0x5A9,0x5A8,0x5A7,0x5A6,0x5A5,0x5A4},
                  {0x5A3,0x5B2,0x5B1,0x5B0,0x5AF,0x5AE,0x5AD,0x5AC,0x5AB,0x5AA,0x5A9,0x5A8,0x5A7,0x5A6,0x5A5,0x5A4}}}},
    {0x43, {{2, 0, {0x703}, {0x40F}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}}}},
    {0x44, {{2, 0, {0x410}, {0x410}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}}}},
    {0x45, {{0, 0, {0}, {0}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}}}},
    {0x46, {{1, 0, {0x54B,0x55A,0x559,0x558,0x557,0x556,0x555,0x554,0x553,0x552,0x551,0x550,0x54F,0x54E,0x54D,0x54C},
                  {0x54B,0x55A,0x559,0x558,0x557,0x556,0x555,0x554,0x553,0x552,0x551,0x550,0x54F,0x54E,0x54D,0x54C}},
           {1, 1, {0x62A,0x639,0x638,0x637,0x636,0x635,0x634,0x633,0x632,0x631,0x630,0x62F,0x62E,0x62D,0x62C,0x62B},
                  {0x62A,0x639,0x638,0x637,0x636,0x635,0x634,0x633,0x632,0x631,0x630,0x62F,0x62E,0x62D,0x62C,0x62B}},
           {1, 0, {0x61A,0x629,0x628,0x627,0x626,0x625,0x624,0x623,0x622,0x621,0x620,0x61F,0x61E,0x61D,0x61C,0x61B},
                  {0x61A,0x629,0x628,0x627,0x626,0x625,0x624,0x623,0x622,0x621,0x620,0x61F,0x61E,0x61D,0x61C,0x61B}},
           {1, 0, {0x61A,0x629,0x628,0x627,0x626,0x625,0x624,0x623,0x622,0x621,0x620,0x61F,0x61E,0x61D,0x61C,0x61B},
                  {0x61A,0x629,0x628,0x627,0x626,0x625,0x624,0x623,0x622,0x621,0x620,0x61F,0x61E,0x61D,0x61C,0x61B}}}},
    {0x47, {{1, 0, {0x46C,0x47B,0x47A,0x479,0x478,0x477,0x476,0x475,0x474,0x473,0x472,0x471,0x470,0x46F,0x46E,0x46D},
                  {0x46C,0x47B,0x47A,0x479,0x478,0x477,0x476,0x475,0x474,0x473,0x472,0x471,0x470,0x46F,0x46E,0x46D}},
           {1, 1, {0x608,0x617,0x616,0x615,0x614,0x613,0x612,0x611,0x610,0x60F,0x60E,0x60D,0x60C,0x60B,0x60A,0x609},
                  {0x608,0x617,0x616,0x615,0x614,0x613,0x612,0x611,0x610,0x60F,0x60E,0x60D,0x60C,0x60B,0x60A,0x609}},
           {1, 0, {0x5F8,0x607,0x606,0x605,0x604,0x603,0x602,0x601,0x600,0x5FF,0x5FE,0x5FD,0x5FC,0x5FB,0x5FA,0x5F9},
                  {0x5F8,0x607,0x606,0x605,0x604,0x603,0x602,0x601,0x600,0x5FF,0x5FE,0x5FD,0x5FC,0x5FB,0x5FA,0x5F9}},
           {1, 0, {0x5F8,0x607,0x606,0x605,0x604,0x603,0x602,0x601,0x600,0x5FF,0x5FE,0x5FD,0x5FC,0x5FB,0x5FA,0x5F9},
                  {0x5F8,0x607,0x606,0x605,0x604,0x603,0x602,0x601,0x600,0x5FF,0x5FE,0x5FD,0x5FC,0x5FB,0x5FA,0x5F9}}}},
    {0x48, {{1, 0, {0x41C,0x41B,0x41A,0x419,0x418,0x427,0x426,0x425,0x424,0x423,0x422,0x421,0x420,0x41F,0x41E,0x41D},
                  {0x41C,0x41B,0x41A,0x419,0x418,0x427,0x426,0x425,0x424,0x423,0x422,0x421,0x420,0x41F,0x41E,0x41D}},
           {1, 1, {0x68A,0x699,0x698,0x697,0x696,0x695,0x694,0x693,0x692,0x691,0x690,0x68F,0x68E,0x68D,0x68C,0x68B},
                  {0x68A,0x699,0x698,0x697,0x696,0x695,0x694,0x693,0x692,0x691,0x690,0x68F,0x68E,0x68D,0x68C,0x68B}},
           {1, 0, {0x67A,0x689,0x688,0x687,0x686,0x685,0x684,0x683,0x682,0x681,0x680,0x67F,0x67E,0x67D,0x67C,0x67B},
                  {0x67A,0x689,0x688,0x687,0x686,0x685,0x684,0x683,0x682,0x681,0x680,0x67F,0x67E,0x67D,0x67C,0x67B}},
           {1, 0, {0x67A,0x689,0x688,0x687,0x686,0x685,0x684,0x683,0x682,0x681,0x680,0x67F,0x67E,0x67D,0x67C,0x67B},
                  {0x67A,0x689,0x688,0x687,0x686,0x685,0x684,0x683,0x682,0x681,0x680,0x67F,0x67E,0x67D,0x67C,0x67B}}}},
    {0x49, {{0, 0, {0}, {0}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}},
           {0, 0, {0}, {0}}}},
    {0x4B, {{1, 0, {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C},
                  {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C}},
           {1, 1, {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C},
                  {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C}},
           {1, 0, {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C},
                  {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C}},
           {1, 0, {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C},
                  {0x43B,0x44A,0x449,0x448,0x447,0x446,0x445,0x444,0x443,0x442,0x441,0x440,0x43F,0x43E,0x43D,0x43C}}}},
};
enum { TUR_GP, TUR_TIGER, TUR_PANTHER, TUR_LYNX };

static const TurretDesc* turretOf(int weapon, int chassis) {
    const int slot = chassis == mapTiger ? TUR_TIGER : chassis == mapPanther ? TUR_PANTHER
                   : chassis == mapLynx ? TUR_LYNX : TUR_GP;
    for (const auto& s : kTurrets)
        if (s.type == weapon) return s.t[slot].kind ? &s.t[slot] : nullptr;
    return nullptr;
}

static const WeaponClass* classOf(int type) {
    for (const auto& c : kClasses) if (c.type == type) return &c;
    return nullptr;
}

bool isWeaponProjectile(int type) { return classOf(type) != nullptr; }
int weaponRangeMode(int type) { const WeaponClass* c = classOf(type); return c ? c->rangeMode : -1; }

static bool esgPeriodic(int pid);

const char* weaponDisplayName(int type) {
    const WeaponSheetDesc* d = weaponSheet(type);
    return d ? d->displayName : "";
}

// byte_4E9AF8 / byte_4E9BF8: cos/sen * 64 para as 256 direções.
static const int8_t kCos[256] = {
    64, 63, 63, 63, 63, 63, 63, 63, 62, 62, 62, 61, 61, 60, 60, 59,
    59, 58, 57, 57, 56, 55, 54, 54, 53, 52, 51, 50, 49, 48, 47, 46,
    45, 44, 42, 41, 40, 39, 38, 36, 35, 34, 32, 31, 30, 28, 27, 25,
    24, 23, 21, 20, 18, 17, 15, 14, 12, 10, 9, 7, 6, 4, 3, 1,
    0, -1, -3, -4, -6, -7, -9, -10, -12, -14, -15, -17, -18, -20, -21, -23,
    -24, -25, -27, -28, -30, -31, -32, -34, -35, -36, -38, -39, -40, -41, -42, -44,
    -45, -46, -47, -48, -49, -50, -51, -52, -53, -54, -54, -55, -56, -57, -57, -58,
    -59, -59, -60, -60, -61, -61, -62, -62, -62, -63, -63, -63, -63, -63, -63, -63,
    -64, -63, -63, -63, -63, -63, -63, -63, -62, -62, -62, -61, -61, -60, -60, -59,
    -59, -58, -57, -57, -56, -55, -54, -54, -53, -52, -51, -50, -49, -48, -47, -46,
    -45, -44, -42, -41, -40, -39, -38, -36, -35, -34, -32, -31, -30, -28, -27, -25,
    -24, -23, -21, -20, -18, -17, -15, -14, -12, -10, -9, -7, -6, -4, -3, -1,
    0, 1, 3, 4, 6, 7, 9, 10, 12, 14, 15, 17, 18, 20, 21, 23,
    24, 25, 27, 28, 30, 31, 32, 34, 35, 36, 38, 39, 40, 41, 42, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 57, 58,
    59, 59, 60, 60, 61, 61, 62, 62, 62, 63, 63, 63, 63, 63, 63, 63,
};
static const int8_t kSin[256] = {
    0, 1, 3, 4, 6, 7, 9, 10, 12, 14, 15, 17, 18, 20, 21, 23,
    24, 25, 27, 28, 30, 31, 32, 34, 35, 36, 38, 39, 40, 41, 42, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 57, 58,
    59, 59, 60, 60, 61, 61, 62, 62, 62, 63, 63, 63, 63, 63, 63, 63,
    64, 63, 63, 63, 63, 63, 63, 63, 62, 62, 62, 61, 61, 60, 60, 59,
    59, 58, 57, 57, 56, 55, 54, 54, 53, 52, 51, 50, 49, 48, 47, 46,
    45, 44, 42, 41, 40, 39, 38, 36, 35, 34, 32, 31, 30, 28, 27, 25,
    24, 23, 21, 20, 18, 17, 15, 14, 12, 10, 9, 7, 6, 4, 3, 1,
    0, -1, -3, -4, -6, -7, -9, -10, -12, -14, -15, -17, -18, -20, -21, -23,
    -24, -25, -27, -28, -30, -31, -32, -34, -35, -36, -38, -39, -40, -41, -42, -44,
    -45, -46, -47, -48, -49, -50, -51, -52, -53, -54, -54, -55, -56, -57, -57, -58,
    -59, -59, -60, -60, -61, -61, -62, -62, -62, -63, -63, -63, -63, -63, -63, -63,
    -64, -63, -63, -63, -63, -63, -63, -63, -62, -62, -62, -61, -61, -60, -60, -59,
    -59, -58, -57, -57, -56, -55, -54, -54, -53, -52, -51, -50, -49, -48, -47, -46,
    -45, -44, -42, -41, -40, -39, -38, -36, -35, -34, -32, -31, -30, -28, -27, -25,
    -24, -23, -21, -20, -18, -17, -15, -14, -12, -10, -9, -7, -6, -4, -3, -1,
};

// ---- Utilitários --------------------------------------------------------------
static int pxW()      { return g_gameMap.width() * 32; }       // dword_54EBC0
static int pxMask()   { return pxW() - 1; }                    // dword_54EBBC
static int tileMask() { return g_gameMap.width() - 1; }        // dword_54EBC4
static int half(int v) { return v / 2; }                       // cdq; sub; sar 1

// |d| com a volta do mapa em X pela largura (padrão com dword_54EBC0).
static int wrapAbsW(int d) {
    d = std::abs(d);
    if (half(pxW()) < d) d = pxW() - d;
    return d;
}
// Vetor dx com a volta pela MÁSCARA (padrão com dword_54EBBC: ±(W-1)).
static int wrapVecMask(int d) {
    const int h = half(pxMask());
    if (h < std::abs(d)) d = d > 0 ? d - pxMask() : d + pxMask();
    return d;
}
// Distância octogonal: maior + menor/2.
static int octo(int a, int b) { return a < b ? b + half(a) : a + half(b); }

// sub_45C4F0: direção 0..255 de (dx, dy) — atan2 * 256 / (2*pi), truncada.
static uint8_t dirOf(int dx, int dy) {
    static const double kPi = std::acos(-1.0);
    const double v = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) * 256.0 / (kPi * 2.0);
    return static_cast<uint8_t>(static_cast<int>(v));
}

static bool alive(int id) {
    return id > 0 && id < static_cast<int>(g_units.size()) && g_units[id].id != -1 &&
           !(g_units[id].flags & UNIT_DEAD);
}
static int ownerOf(const UnitRecord& r) { return r.owner & 0x0F; }
static int statsOf(const UnitRecord& r) { return (r.owner >> 4) & 0x0F; }   // [+0x1D] sar 4

static const WeapPlayerStats* weapStats(int weaponType, int player) {
    const WeaponSheetDesc* d = weaponSheet(weaponType);
    return (d && player >= 0 && player < 7) ? &d->player[player] : nullptr;
}

// Bloco do descritor do alvo: HP (+0x00) e armadura (+0x08) p/ o nibble alto.
static int maxHPOf(const UnitRecord& r) {
    const int p = statsOf(r);
    if (r.flags & UNIT_VEHICLE) { const VehPlayerStats* v = vehicleSheet(r.type, p); return v ? v->hitPoints : 0; }
    if (r.flags & UNIT_BUILDING) { const StructSheetDesc* d = structSheet(r.type); return d ? d->player[p].hitPoints : 0; }
    return 0;
}
static int armorOf(const UnitRecord& r) {
    const int p = statsOf(r);
    if (r.flags & UNIT_VEHICLE) { const VehPlayerStats* v = vehicleSheet(r.type, p); return v ? v->armor : 256; }
    if (r.flags & UNIT_BUILDING) { const StructSheetDesc* d = structSheet(r.type); return d ? d->player[p].armor : 256; }
    return 256;
}

// sub_404C70: nFrames-1 da animação crua.
static int lastFrameRaw(int raw) {
    const int real = raw - kAnimBias;
    if (!g_art.ok() || real < 0 || real >= g_art.numAnimations()) return 0;
    const int n = static_cast<int>(g_art.animation(real).frames.size());
    return n > 0 ? n - 1 : 0;
}
// sub_404DF0: offsets (x1,y1,x2,y2) do frame — 0 se a animação não tem.
static const int8_t* frameOffsets(int animReal, int frame) {
    static const int8_t kZero[4] = {0, 0, 0, 0};
    if (!g_art.ok() || animReal < 0 || animReal >= g_art.numAnimations()) return kZero;
    const ArtAnimation& a = g_art.animation(animReal);
    if (!a.hasOffsets || frame < 0 || frame >= static_cast<int>(a.frames.size())) return kZero;
    return a.frames[frame].off;
}

bool isTankChassis(int type) { return type == mapLynx || type == mapPanther || type == mapTiger; }

int8_t  dirCos(uint8_t dir) { return kCos[dir]; }
int8_t  dirSin(uint8_t dir) { return kSin[dir]; }
uint8_t dirOfVec(int dx, int dy) { return dirOf(dx, dy); }
int artLastFrameRaw(int raw) { return lastFrameRaw(raw); }
const int8_t* artFrameOffsets(int raw, int frame) { return frameOffsets(raw - kAnimBias, frame); }

static bool isEdenUnit(const UnitRecord& r) {
    const int ow = ownerOf(r);
    return ow < kMaxPlayers && g_playerData[ow].isEden != 0;
}

static bool statsEden(const UnitRecord& r) {                     // dword_56E308[[+0x1D] >> 4]
    const int p = statsOf(r);
    return p < kMaxPlayers && g_playerData[p].isEden != 0;
}

// vt[7..10] do descritor da arma: animação crua da torre p/ a direção.
static int turretAnimRaw(const TurretDesc& t, const UnitRecord& u, uint8_t dir) {
    const bool eden = statsEden(u);
    if (t.kind == 2) return eden ? t.eden[0] : t.ply[0];
    const int idx = static_cast<uint8_t>(dir + 8) >> 4;           // add al,8; shr al,2; and ~3
    return eden ? t.eden[idx] : t.ply[idx];
}

// vt[5] do veículo armado (sub_485600 Lynx / sub_4857E0 Panther / sub_485990
// Tiger): boca da arma = offset do frame 0 da torre (2º par se o projétil/
// unidade `alt` tem 0x4000, só no Tiger) + encaixe do frame 0 do chassi.
static void muzzleOffset(const UnitRecord& shooter, bool alt, uint8_t dir, int& mx, int& my) {
    mx = my = 0;
    if ((shooter.flags & UNIT_BUILDING) && shooter.type == 0x17) {   // Guard Post: sub_42CFB0
        const TurretDesc* td = turretOf(static_cast<int16_t>(shooter.cargo), shooter.type);
        if (td && td->kind == 1) {
            const int8_t* t = frameOffsets(turretAnimRaw(*td, shooter, dir) - kAnimBias, 0);
            mx = t[0]; my = t[1];
        }
        return;
    }
    if (!isTankChassis(shooter.type)) return;                       // Scorpion: sub_481850 (0)
    const TurretDesc* td = turretOf(static_cast<int16_t>(shooter.cargo), shooter.type);
    if (td && td->kind == 1) {
        const int8_t* t = frameOffsets(turretAnimRaw(*td, shooter, dir) - kAnimBias, 0);
        const bool second = alt && td->sar;                         // sub_4A5960: sar 10h
        mx = second ? t[2] : t[0];
        my = second ? t[3] : t[1];
    }
    const int body = vehicleAnimIndex(shooter.type, shooter.rotation, isEdenUnit(shooter));
    const int8_t* b = frameOffsets(body, 0);
    mx += b[0];
    my += b[1];
}

// ---- Linha de visada (sub_436EA0) --------------------------------------------
// Bresenham de (x0,y0) a (x1,y1) em tiles; o ponto de partida e o de chegada não
// são testados. Bloqueia: célula com bit 31 (estrutura/tubo/parede) ou terreno
// com a coluna 5 de dword_4DEB9C != 0 (dword_4DEBB0 = flag de bloqueio de
// visada) — exceto penhascos (8/9) colados à origem ou ao destino. 0 = sem
// visada (inclusive no mesmo tile).
static int lineOfSight(int x0, int y0, int x1, int y1) {
    const int mask = tileMask(), W = g_gameMap.width();
    int dx = x1 - x0;
    const int h = half(mask);
    if (-h > dx) dx = W - x0 + x1;
    else if (h < dx) dx = -((-dx) & mask);
    if (dx == 0 && y1 == y0) return 0;
    const int dy = y1 - y0;
    int major, minor, maj2, min2;                 // var_38/var_44, var_2C/var_28
    int edi, ebx;                                 // eixo maior / menor
    const bool xMajor = std::abs(2 * dy) <= std::abs(2 * dx);
    if (xMajor) { major = dx; minor = dy; maj2 = std::abs(2 * dx); min2 = std::abs(2 * dy); edi = 0; ebx = y0; }
    else        { major = dy; minor = dx; maj2 = std::abs(2 * dy); min2 = std::abs(2 * dx); ebx = 0; edi = y0; }
    const int sMin = minor > 0 ? 1 : -1;          // var_1C
    const int sMaj = major > 0 ? 1 : -1;          // var_20
    int err = min2 - (half(min2) + half(maj2));   // ebp
    int pending = 0;                              // var_14
    auto step = [&]() {
        err += min2;
        if (err > 0) { err -= maj2; ebx += sMin; pending = 1; }
        else if (err == 0) { err = -maj2; ebx += sMin; edi += sMaj; }
        else edi += sMaj;
    };
    if (err > 0) { err -= maj2; ebx += sMin; pending = 1; }
    else if (err == 0) { err = -maj2; ebx += sMin; edi += sMaj; }
    else edi += sMaj;
    int nearStart = 0, nearEnd = 0;               // var_44 / var_38
    auto px = [&]() { return xMajor ? edi : ebx; };   // var_40: x relativo
    auto py = [&]() { return xMajor ? ebx : edi; };   // var_3C: y absoluto
    int cx = px(), cy = py();
    if (pending) { edi += sMaj; --pending; } else step();
    for (;;) {
        if (cx == dx && cy == y1) {
            static const int kRes[4] = {1, -1, 2, 1};             // dword_4D0480
            return kRes[nearStart + nearEnd * 2];
        }
        const uint32_t cell = g_gameMap.rawCell((cx + x0) & mask, cy);
        if (cell & 0x80000000u) return 0;
        const int type = static_cast<int>(cell & 0x1F);
        if (terrainMoveCost(type, 5) != 0) {                    // dword_4DEBB0: bloqueia a visada
            if (type != 8 && type != 9) return 0;
            if (std::abs(cx) <= 1 && std::abs(cy - y0) <= 1) nearStart = 1;
            else if (std::abs(dx - cx) <= 1 && std::abs(cy - y1) <= 1) nearEnd = 1;
            else return 0;
        }
        cx = px(); cy = py();
        if (pending) { edi += sMaj; --pending; } else step();
    }
}

// ---- Alvos -------------------------------------------------------------------
// vt[27]: tile da unidade mais próximo de (sx,sy) — veículo: o próprio tile
// (sub_4018B0); prédio: o tile do footprint mais próximo (sub_483B90).
static void nearestTile(const UnitRecord& t, int sx, int sy, int& ox, int& oy) {
    const int tx = unitOX(t) >> 5, ty = unitOY(t) >> 5;
    if (!(t.flags & UNIT_BUILDING)) { ox = tx; oy = ty; return; }
    const StructSheetDesc* d = structSheet(t.type);
    const int w = d ? d->sizeX : 1, h = d ? d->sizeY : 1;
    const int mask = tileMask(), W = g_gameMap.width();
    // sub_408880: retângulo do footprint.
    const int x1 = (tx - (w >> 1)) & mask;
    int y1 = ty - (h >> 1); if (y1 <= 0) y1 = 0;
    const int x2 = (w + x1 - 1) & mask;
    int y2 = h + y1 - 1; if (y2 > g_gameMap.height() - 1) y2 = g_gameMap.height() - 1;
    int d1 = sx - x1; { const int hh = half(mask); if (d1 < -hh) d1 = W - x1 + sx; else if (d1 > hh) d1 = -((-d1) & mask); } d1 = std::abs(d1);
    int d2 = sx - x2; { const int hh = half(mask); if (d2 < -hh) d2 = W - x2 + sx; else if (d2 > hh) d2 = -((-d2) & mask); } d2 = std::abs(d2);
    if (x2 < x1) {                                  // footprint cruzando a emenda
        if (x1 < sx || x2 > sx) ox = sx;
        else ox = d2 <= d1 ? x2 : x1;
    } else {
        if (x1 <= sx && x2 > sx) ox = sx;
        else ox = d2 <= d1 ? x2 : x1;
    }
    oy = y1 >= sy ? y1 : (y2 <= sy ? y2 : sy);
}

// Prédio-torre ligado à rede (Guard Post: [+0x38]->+0x10) ganha +1 de alcance.
static bool turretBonus(const UnitRecord& u) {
    return (u.flags & UNIT_TURRET_BLD) && u.netConnected;
}

// sub_485140(alvo): o alvo (dword no formato de [+0x4C]) está ao alcance (e à
// vista, se o modo exigir)?
static bool inRangeTo(const UnitRecord& u, uint32_t tgt) {
    const int p = statsOf(u);
    int R;
    if (u.action == 0x25) {
        const VehPlayerStats* v = vehicleSheet(u.type, p);
        R = (v ? v->sight : 0) - 2;                   // desc+0x20 do próprio veículo
        if (R <= 1) R = 1;
    } else {
        const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), p);
        R = w ? w->range : 0;
        if (turretBonus(u)) { ++R; if (R >= 7) R = 7; }
    }
    const int sx = unitOX(u) >> 5, sy = unitOY(u) >> 5;
    int tx, ty, dx, dy;
    const int16_t tLo = static_cast<int16_t>(tgt & 0xFFFF), tHi = static_cast<int16_t>(tgt >> 16);
    if ((tgt & 0x80000000u) && tHi != 0x7FFF) {
        const UnitRecord& t = g_units[tLo > 0 && tLo < static_cast<int>(g_units.size()) ? tLo : 0];
        nearestTile(t, sx, sy, tx, ty);
        dy = std::abs(unitOY(u) - ty * 32 - 16);
        const int cx = tx * 32 + 16, ux = unitOX(u);
        dx = (ux > cx ? ux : cx) - (ux < cx ? ux : cx);
    } else {
        tx = tLo; ty = tHi;
        dx = ((tx > sx ? tx : sx) - (tx < sx ? tx : sx)) * 32;
        dy = std::abs(sy - ty) * 32;
    }
    if (half(pxW()) < dx) dx = pxW() - dx;
    const int dist = octo(dx, dy) >> 5;
    const WeaponClass* c = classOf(static_cast<int16_t>(u.cargo));
    const int mode = c ? c->rangeMode : 1;
    if (mode == 1 || dist == 0) return R >= dist;
    if (R < dist) return false;
    return lineOfSight(sx, sy, tx, ty) != 0;
}
static bool inRange(const UnitRecord& u) { return inRangeTo(u, u.target()); }

// Filtro comum das buscas: prédio só se AutoTargeted; máscara de flags; não aliado.
static bool candidateOk(const UnitRecord& c, uint32_t mask, uint32_t ownerBit) {
    if ((c.flags & UNIT_BUILDING) && !(c.flags & UNIT_AUTO_TARGET)) return false;
    if ((c.flags & mask) != mask) return false;
    const int o = ownerOf(c);
    if (o < kMaxPlayers && (g_playerData[o].allyMask & ownerBit)) return false;
    return true;
}

// sub_4A6DD0: varre a lista por Y p/ cima e depois p/ baixo a partir do
// atirador; 1º candidato dentro da caixa e com linha de visada. sub_4A7400
// (BFG, Thor, SPAM, destruições de torre) é a mesma varredura sem a visada;
// sub_4A7270 (espuma) também pula quem tem espuma ou EMP (0xC0000).
static int findTargetLOS(int id, uint32_t mask, bool los = true, bool skipFx = false) {
    const UnitRecord& u = g_units[id];
    const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), statsOf(u));
    int R = w ? w->range : 0;                     // min(desc deste, desc da arma) = o mesmo
    if (turretBonus(u)) { ++R; if (R >= 7) R = 7; }
    const int Rpx = R << 5;
    const int sx = unitOX(u), sy = unitOY(u);
    const int xL = sx - Rpx, xR = sx + Rpx;
    const int stx = sx >> 5, sty = sy >> 5;
    const uint32_t bit = 1u << ownerOf(u);
    for (int c = u.lprev; c != 0 && unitOY(g_units[c]) >= sy - Rpx; c = g_units[c].lprev) {
        const UnitRecord& t = g_units[c];
        const int x = unitOX(t);
        if (x <= xL || x >= xR) continue;
        if (!candidateOk(t, mask, bit)) continue;
        if (skipFx && (t.flags & 0xC0000u)) continue;
        if (los && !lineOfSight(x >> 5, unitOY(t) >> 5, stx, sty)) continue;
        return c;
    }
    for (int c = u.lnext; c != 0 && unitOY(g_units[c]) <= sy + Rpx; c = g_units[c].lnext) {
        const UnitRecord& t = g_units[c];
        const int x = unitOX(t);
        if (x <= xL || x >= xR) continue;
        if (!candidateOk(t, mask, bit)) continue;
        if (skipFx && (t.flags & 0xC0000u)) continue;
        if (los && !lineOfSight(x >> 5, unitOY(t) >> 5, stx, sty)) continue;
        return c;
    }
    return 0;
}

// sub_4A6AA0 (RPG/Acid): sem linha de visada, distância octogonal < alcance.
// Começa p/ cima ou p/ baixo conforme ((id + tick/32) & 1) e — como no
// original — devolve 0 ao fim da 1ª varredura (o contador começa em 2).
static int findTargetNear(int id, uint32_t mask) {
    const UnitRecord& u = g_units[id];
    const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), statsOf(u));
    const int Rfull = w ? w->range : 0;           // var_C
    int R = Rfull;                                // var_18
    if (turretBonus(u)) { ++R; if (R >= 7) R = 7; }
    const int sx = unitOX(u), sy = unitOY(u);
    const int xL = sx - R * 32, xR = sx + R * 32;
    const uint32_t bit = 1u << ownerOf(u);
    auto test = [&](int c) -> bool {
        const UnitRecord& t = g_units[c];
        const int x = unitOX(t);
        if (xL >= x || xR <= x) return false;
        if (!candidateOk(t, mask, bit)) return false;
        const int dx = wrapAbsW(x - sx), dy = std::abs(sy - unitOY(t));
        return (Rfull << 5) > octo(dx, dy);
    };
    const bool up = ((u.id + (TethysGame::tick() >> 5)) & 1) == 0;
    if (up) {
        for (int c = u.lprev; c != 0 && unitOY(g_units[c]) >= sy - R * 32; c = g_units[c].lprev)
            if (test(c)) return c;
    } else {
        for (int c = u.lnext; c != 0 && unitOY(g_units[c]) <= sy + R * 32; c = g_units[c].lnext)
            if (test(c)) return c;
    }
    return 0;
}

// sub_4A7060 (EMP): a caixa sem visada, mas sem o filtro de AutoTargeted; pula
// quem já está em EMP, prédios que não consomem energia (bloco +0x24) e as
// aranhas (desc+4 = map_id 3 Spider / 4 Scorpion).
static bool empCandidateOk(const UnitRecord& c, uint32_t mask, uint32_t ownerBit) {
    if ((c.flags & mask) != mask) return false;
    const int o = ownerOf(c);
    if (o < kMaxPlayers && (g_playerData[o].allyMask & ownerBit)) return false;
    if (c.flags & UNIT_EMP) return false;
    if (c.flags & UNIT_BUILDING) {
        const StructSheetDesc* d = structSheet(c.type);
        if (!d || d->player[statsOf(c)].power == 0) return false;
    }
    return c.type != mapSpider && c.type != mapScorpion;
}
static int findTargetEMP(int id, uint32_t mask) {
    const UnitRecord& u = g_units[id];
    const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), statsOf(u));
    int R = w ? w->range : 0;
    if (turretBonus(u)) { ++R; if (R >= 7) R = 7; }
    const int Rpx = R << 5;
    const int sx = unitOX(u), sy = unitOY(u);
    const int xL = sx - Rpx, xR = sx + Rpx;
    const uint32_t bit = 1u << ownerOf(u);
    for (int c = u.lprev; c != 0 && unitOY(g_units[c]) >= sy - Rpx; c = g_units[c].lprev) {
        const UnitRecord& t = g_units[c];
        const int x = unitOX(t);
        if (x <= xL || x >= xR) continue;
        if (empCandidateOk(t, mask, bit)) return c;
    }
    for (int c = u.lnext; c != 0 && unitOY(g_units[c]) <= sy + Rpx; c = g_units[c].lnext) {
        const UnitRecord& t = g_units[c];
        const int x = unitOX(t);
        if (x <= xL || x >= xR) continue;
        if (empCandidateOk(t, mask, bit)) return c;
    }
    return 0;
}

static int findTarget(int id, uint32_t mask) {
    const WeaponClass* c = classOf(static_cast<int16_t>(g_units[id].cargo));
    if (!c) return 0;
    switch (c->finder) {
        case FIND_LOS:  return findTargetLOS(id, mask);
        case FIND_BOX:  return findTargetLOS(id, mask, false);
        case FIND_NEAR: return findTargetNear(id, mask);
        case FIND_EMP:  return findTargetEMP(id, mask);
        case FIND_FOAM: return findTargetLOS(id, mask, false, true);
        case FIND_NONE: return 0;
    }
    return 0;
}

static void dropRef(int id) {
    if (id > 0 && id < static_cast<int>(g_units.size()) && g_units[id].refCount)
        --g_units[id].refCount;
}
static void clearTarget(UnitRecord& u) {
    if (u.targetIsUnit() && u.hasTarget()) dropRef(u.target4C);
    u.field4E = 0x7FFF;
}
static void setUnitTarget(UnitRecord& u, int t) {
    u.target4C = static_cast<int16_t>(t);
    u.field4E = 0xFFFF;                            // [+0x4C] = id | 0xFFFF0000
    ++g_units[t].refCount;
}

// ---- Disparo (vt[4] do veículo armado = sub_484F80) ---------------------------
static int createProjectile(int type, int x, int y, int owner) {
    // sub_4466B0(..., arg_18=1): centro do tile, y em [16, H*32-16].
    int sx = (x & ~31) + 16;
    int sy = (y & ~31) + 16;
    const int maxY = g_gameMap.height() * 32 - 16;
    if (sy > maxY) sy = maxY;
    if (sy <= 16) sy = 16;
    sx &= pxMask();
    Unit u;
    if (!createUnit(u, type, LOCATION{sx >> 5, sy >> 5}, owner)) return 0;
    UnitRecord& p = g_units[u.id()];
    p.rawPixel = true;
    p.pixelX = sx;
    p.pixelY = sy;
    if (const WeaponSheetDesc* d = weaponSheet(type)) p.flags |= d->ownerFlags | 0x100u;   // desc+0x1F4
    return u.id();
}

// vt[32] das autodestruições (sub_4A4030): impacto na posição do atirador
// ([+0x48]), sem voo, espera 1 tick (alvo = o próprio atirador) ou 4 e explode
// (o sub-estado 6 grava 6 em [+0x23] — não passa pelo 0x13 nem toca o disparo).
static void destructArm(int pid, int source, int target) {
    UnitRecord& p = g_units[pid];
    const UnitRecord& s = g_units[source];
    p.impactX = unitOX(s);
    p.impactY = unitOY(s);
    p.flags &= ~UNIT_IN_FLIGHT;
    p.field22 = SUB_WAIT; p.field23 = 0x13; p.action = 0x36;
    p.field3C = target == source ? 1 : 4;
}

static bool fireWeapon(int id) {
    UnitRecord& u = g_units[id];
    if (!inRange(u)) { clearTarget(u); return false; }
    // (0x2000 = atirar no chão: sub_4A1BC0 confere o tile — ordem ainda não portada.)
    const int pid = createProjectile(static_cast<int16_t>(u.cargo), unitOX(u), unitOY(u) + 1, ownerOf(u));
    if (!pid) return false;
    UnitRecord& sh = g_units[id];                  // (createUnit pode realocar o vetor)
    UnitRecord& p = g_units[pid];
    p.owner = static_cast<uint8_t>((p.owner & 0xF6) | 6);       // dono 6, nibble alto = atirador
    sh.flags ^= UNIT_ALT_BARREL;                                // alterna o cano
    p.flags = (p.flags & ~UNIT_ALT_BARREL) | (sh.flags & UNIT_ALT_BARREL);
    p.sourceId = id;                                            // vt[37] = sub_4A27E0
    ++sh.refCount;
    if (p.type == 0x3C) p.shooterBit = static_cast<int8_t>(1 << statsOf(sh));   // sub_4A47F0
    if (p.type == 0x46) p.esgOwnerBit = static_cast<int8_t>(1 << ownerOf(sh));  // sub_480A80
    if (sh.cargo == 0x41 || sh.cargo == 0x42) {
        // sub_484F80: Starflare/Supernova — vt[32] (sub_4A4030) com o próprio
        // atirador e larga o alvo dele.
        destructArm(pid, id, id);
        UnitRecord& s2 = g_units[id];
        if (s2.targetIsUnit() && s2.hasTarget()) dropRef(s2.target4C);
        s2.field4E = 0x7FFF;
        return true;
    }
    // vt[32] (sub_4A3DD0, alvo unidade) / vt[33] (sub_4A3DA0, alvo lugar).
    p.flags &= ~UNIT_IN_FLIGHT;
    p.field23 = 0; p.field22 = 0x12; p.action = 0x36;
    p.target4C = sh.target4C;
    p.field4E = sh.targetIsUnit() ? 0xFFFF : sh.field4E;
    if (sh.flags & UNIT_FIRE_GROUND) { sh.flags &= ~UNIT_FIRE_GROUND; clearTarget(sh); }
    return true;
}

// sub_4A7D00: sem alvo — a torre passeia (fase (id ^ tick/256) & 15; em
// movimento a fase passa por dword_4EADD8).
static void idleTurret(UnitRecord& u) {
    static const int kMoving[16] = {0, 1, 0, 3, 4, 5, 6, 7, 8, 6, 10, 11, 12, 13, 14, 14};
    int phase = (u.id ^ (TethysGame::tick() >> 8)) & 0x0F;
    if (u.field22 == 1) phase = kMoving[phase];
    const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), statsOf(u));
    int target;
    switch (phase) {
        case 0: case 3: case 5: case 7: case 10: case 12: case 14:
            target = u.rotation; break;                           // volta p/ a frente
        case 1: u.turretDir = static_cast<uint8_t>(u.turretDir + (w ? w->turnRate : 0)); return;
        case 8: u.turretDir = static_cast<uint8_t>(u.turretDir - (w ? w->turnRate : 0)); return;
        case 6: case 13: target = TethysGame::getRand(0x100); break;
        default: return;
    }
    // Giro pelo bloco do PRÓPRIO tipo, +0x24 (Move_Speed nos veículos; no Guard
    // Post é o Power_Required).
    const uint8_t* blk = unitStatBlock(u.type, statsOf(u));
    int rate = 0;
    if (blk) std::memcpy(&rate, blk + 0x24, 4);
    const int8_t diff = static_cast<int8_t>(static_cast<uint8_t>(target) - u.turretDir);
    if (std::abs(static_cast<int>(diff)) < rate) u.turretDir = static_cast<uint8_t>(target);
    else if (diff < 0) u.turretDir = static_cast<uint8_t>(u.turretDir - rate);
    else u.turretDir = static_cast<uint8_t>(u.turretDir + rate);
}

// vt[4] do Guard Post (sub_42CFD0): operando e com o alvo ao alcance, cria o
// projétil ([+0x18]+1), dono 6; ligado à rede o projétil ganha 0x10|0x80 (+1 de
// alcance, dano x1.5); vt[37] e vt[32]/[33] como no veículo (sem trocar o cano).
static bool guardPostFire(int id) {
    UnitRecord& u = g_units[id];
    if (!structureOperational(u) || !inRange(u)) {
        if (u.field4E != 0x7FFF) clearTarget(u);
        return false;
    }
    const bool linked = turretBonus(u);
    const int pid = createProjectile(static_cast<int16_t>(u.cargo), unitOX(u), unitOY(u) + 1, ownerOf(u));
    if (!pid) return false;
    UnitRecord& sh = g_units[id];
    UnitRecord& p = g_units[pid];
    p.owner = static_cast<uint8_t>((p.owner & 0xF6) | 6);
    if (linked) p.flags |= 0x10u | UNIT_TURRET_BLD;
    p.sourceId = id;                                              // vt[37]
    ++sh.refCount;
    if (p.type == 0x3C) p.shooterBit = static_cast<int8_t>(1 << statsOf(sh));
    if (p.type == 0x46) p.esgOwnerBit = static_cast<int8_t>(1 << ownerOf(sh));
    p.flags &= ~UNIT_IN_FLIGHT;
    p.field23 = 0; p.field22 = 0x12; p.action = 0x36;
    p.target4C = sh.target4C;
    p.field4E = sh.targetIsUnit() ? 0xFFFF : sh.field4E;
    return true;
}

void weaponTick(int id) {                                     // sub_4A7AF0 / sub_4A7740
    UnitRecord& u = g_units[id];
    const bool gp = (u.flags & UNIT_BUILDING) && u.type == 0x17;  // Guard Post (vt[4] sub_42CFD0)
    if (!(u.flags & UNIT_VEHICLE) && !gp) return;
    const WeaponClass* c = classOf(static_cast<int16_t>(u.cargo));
    if (!c) return;
    if (u.reload) { --u.reload; return; }
    const WeapPlayerStats* w = weapStats(c->type, statsOf(u));
    auto reload = [&]() {
        UnitRecord& s = g_units[id];
        s.reload = static_cast<int16_t>((w ? w->reload : 0) >> ((s.flags & 0x20) ? 1 : 0));
    };
    if (!u.hasTarget()) {
        if (((u.id ^ TethysGame::tick()) & 7) != 0) return;
        uint32_t mask = UNIT_DAMAGEABLE;
        if (c->type != 0x47) {                                     // sub_4A7950: só 0x400
            if (u.field22 == 1) mask |= UNIT_VEHICLE;
            if (u.field4A != 0x7FFF) mask |= UNIT_ARMED;
        }
        const int t = findTarget(id, mask);
        if (t) setUnitTarget(g_units[id], t);
        else idleTurret(g_units[id]);
        return;
    }
    if (c->type == 0x3C || c->type == 0x47) {
        // sub_4A7580 (EMP) / sub_4A7950 (espuma): alvo morto ou já em EMP
        // (0xA0000) / com espuma (0x60000) é largado; sem o teste de alcance
        // antes do disparo (o vt[4] confere).
        const uint32_t fx = c->type == 0x3C ? UNIT_EMP : 0x40000u;
        if (u.targetIsUnit() && (!alive(u.target4C) || (g_units[u.target4C].flags & fx))) {
            u.field4E = 0x7FFF;
            dropRef(u.target4C);
            return;
        }
    } else if (u.targetIsUnit()) {
        if (!alive(u.target4C) || (c->rangeMode == 1 && !inRange(u))) {
            u.field4E = 0x7FFF;
            dropRef(u.target4C);
            return;
        }
    } else if (c->rangeMode == 1 && !inRange(u)) {
        return;
    }
    if (gp ? guardPostFire(id) : fireWeapon(id)) reload();
}

// vt[63] do Guard Post (sub_423E50): ordem de ataque [+0x48] sem rota.
static void guardPostOrder(int id, int type, uint32_t tgt) {
    UnitRecord& u = g_units[id];
    if (tgt & 0x80000000u) {
        const int t = static_cast<int16_t>(tgt & 0xFFFF);
        if (!alive(t)) {
            u.action = 0; u.field3C = 0; u.field20 = 1;
            if (u.orderTarget() & 0x80000000u) { dropRef(u.orderLo); u.field4A = 0x7FFF; }
            return;
        }
        u.action = static_cast<uint8_t>(type);
        if (u.orderTarget() == tgt) return;
        if (u.orderTarget() & 0x80000000u) dropRef(u.orderLo);
        u.setOrderTarget(tgt);
        if (u.orderLo > 0 && u.orderLo < static_cast<int>(g_units.size())) ++g_units[u.orderLo].refCount;
        return;
    }
    u.action = static_cast<uint8_t>(type);
    if (u.orderTarget() & 0x80000000u) dropRef(u.orderLo);
    u.setOrderTarget(tgt);
}

// vt[20] do Guard Post (sub_42CDD0), 1x a cada 8 ticks: sem alvo e operando,
// parado e atacado há < 75 ticks por quem não é aliado (EMP não revida
// Scorpion) → ordem de ataque 0x24; senão busca (vt[6], máscara 0x400); sem
// achar, a torre gira pelo Turret Turn Rate na fase (id ^ tick/512) & 3 = 1/3.
// Com alvo fora do alcance, larga.
static void guardPostPeriodic(int id) {
    UnitRecord& u = g_units[id];
    if (u.field4E == 0x7FFF) {
        if (!structureOperational(u)) return;
        if (u.action == 0 && u.field22 == 0 && u.lastAttacker &&
            static_cast<int>(TethysGame::tick()) - u.lastAttackTick < 0x4B) {
            const UnitRecord& a = g_units[u.lastAttacker];
            const bool empVsScorpion = static_cast<int16_t>(u.cargo) == 0x3C && a.type == mapScorpion;
            const int ow = ownerOf(u);
            const bool ally = ow < kMaxPlayers && (g_playerData[ow].allyMask & (1u << ownerOf(a)));
            if (!empVsScorpion && !ally) {
                u.action = 0;
                guardPostOrder(id, 0x24, static_cast<uint32_t>(a.id) | 0xFFFF0000u);
                return;
            }
        }
        const int t = findTarget(id, UNIT_DAMAGEABLE);
        if (!t) {
            const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), statsOf(u));
            const int tr = w ? w->turnRate : 0;
            const int ph = static_cast<int>((static_cast<uint32_t>(u.id) ^ (TethysGame::tick() >> 9)) & 3);
            if (ph == 1) u.turretDir = static_cast<uint8_t>(u.turretDir + tr);
            else if (ph == 3) u.turretDir = static_cast<uint8_t>(u.turretDir - tr);
            return;
        }
        setUnitTarget(g_units[id], t);
    }
    UnitRecord& g = g_units[id];
    if (g.field4E != 0x7FFF && !inRange(g)) clearTarget(g);
}

int weaponTurretIconRaw(int weapon, int chassis, bool eden) {
    const TurretDesc* td = turretOf(weapon, chassis);
    if (!td) return 0;
    return eden ? td->eden[0] : td->ply[0];
}

bool guardPostSprite(const UnitRecord& r, int& animReal) {   // vt[8] sub_42D250
    if (r.type != 0x17 || !(r.flags & UNIT_BUILDING) || r.action == 0x21 || r.action == 0x22) return false;
    const TurretDesc* td = turretOf(static_cast<int16_t>(r.cargo), r.type);
    if (!td) return false;
    animReal = turretAnimRaw(*td, r, r.turretDir) - kAnimBias;
    return true;
}

static void addRef(int id) {
    if (id > 0 && id < static_cast<int>(g_units.size())) ++g_units[id].refCount;
}
static void dropIfUnit(uint32_t tgt) { if (tgt & 0x80000000u) dropRef(static_cast<int16_t>(tgt & 0xFFFF)); }

// sub_439810: ponto de rota do alvo — lugar: centro do tile; unidade: o tile
// dela mais próximo da posição (vt[27]).
static uint32_t targetWaypoint(const UnitRecord& u, uint32_t tgt) {
    int tx, ty;
    if (tgt & 0x80000000u) {
        nearestTile(g_units[static_cast<int16_t>(tgt & 0xFFFF)], unitOX(u) >> 5, unitOY(u) >> 5, tx, ty);
    } else {
        tx = static_cast<int16_t>(tgt & 0xFFFF); ty = static_cast<int16_t>(tgt >> 16);
    }
    return packLocation(tx & tileMask(), ty);
}

void unitNearestTile(const UnitRecord& t, int sx, int sy, int& ox, int& oy) { nearestTile(t, sx, sy, ox, oy); }
uint32_t unitTargetWaypoint(const UnitRecord& u, uint32_t tgt) { return targetWaypoint(u, tgt); }

void unitOrderAttack(int id, int type, const std::vector<uint32_t>& wps, uint32_t tgt, int unitIdx, int adj) {   // sub_423850
    UnitRecord& u = g_units[id];
    if ((tgt >> 16) == 0x7FFF) tgt = (tgt & 0xFFFF) | 0xFFFF0000u;
    const bool tUnit = (tgt & 0x80000000u) != 0;
    if (tUnit) {
        const int t = static_cast<int16_t>(tgt & 0xFFFF);
        if (t <= 0 || t >= static_cast<int>(g_units.size()) || g_units[t].id == -1 || (g_units[t].flags & UNIT_DEAD)) {
            u.action = 0; u.field3C = 0; u.field20 = 1;          // alvo morto: cancela
            if (u.orderTarget() & 0x80000000u) { dropRef(u.orderLo); u.field4A = 0x7FFF; }
            if (u.guardTarget() & 0x80000000u) { dropRef(u.guardLo); u.guardHi = 0x7FFF; }
            u.flags &= ~0x400000u;
            return;
        }
    }
    u.field20 = 1;
    u.action = static_cast<uint8_t>(type);
    u.field3C = unitIdx / 4;
    const uint32_t cur = u.orderTarget();
    if (!(u.field4A != 0x7FFF && (cur & 0x80000000u) && tUnit && u.orderLo == static_cast<int16_t>(tgt & 0xFFFF))) {
        dropIfUnit(cur);
        u.setOrderTarget(tgt);
        if (tUnit) addRef(u.orderLo);
    }
    if (type == 0x25) {
        if (u.guardTarget() != tgt) {
            dropIfUnit(u.guardTarget());
            u.guardLo = static_cast<int16_t>(tgt & 0xFFFF); u.guardHi = static_cast<int16_t>(tgt >> 16);
            addRef(u.guardLo);
        }
    } else if (!(u.flags & 0x400000u)) {
        dropIfUnit(u.guardTarget());
        u.guardHi = 0x7FFF;
    }
    if (inRangeTo(u, u.orderTarget()) && wps.empty()) {
        u.hasOrder = false; u.order = OrderBlock{};          // já ao alcance: sem rota
        u.clearRoute();
        if (u.field4E != 0x7FFF && u.target4C == u.orderLo) return;
        if (u.targetIsUnit() && u.hasTarget()) dropRef(u.target4C);
        u.field4E = 0x7FFF;
        if (u.action == 0x25 || (u.flags & 0x400000u)) return;
        u.target4C = u.orderLo; u.field4E = u.field4A;       // [+0x4C] = [+0x48]
        if (u.targetIsUnit()) addRef(u.target4C);
        return;
    }
    // Rota até o alvo; [ordem+0x40] = distância de parada pelo alcance da arma.
    u.hasOrder = true;
    u.order = OrderBlock{};
    u.order.waypoints = wps;
    u.order.count = static_cast<int>(wps.size()) + 1;
    u.order.countMinus1 = static_cast<int>(wps.size());
    const bool moving = tUnit && g_units[u.orderLo].field22 == 1;
    const WeapPlayerStats* w = weapStats(static_cast<int16_t>(u.cargo), statsOf(u));
    const int R = w ? w->range : 0;
    int d;
    if (type == 0x25) d = moving ? R >> 1 : R - 1;
    else d = R - (moving ? 2 : 0) - adj;
    if (d <= 1) d = 1;
    u.order.field40 = d;
    const WeaponClass* wc = classOf(static_cast<int16_t>(u.cargo));
    u.order.multiFlag = wc ? wc->rangeMode : 0;              // [ordem+0x3C] = desc+0x250 (modo do objetivo)
    u.order.waypoints.push_back(targetWaypoint(u, tgt));
    u.clearRoute();
}

void processAttackPacket(int owner, int type, const std::vector<int>& ids,
                         const std::vector<uint32_t>& wps, uint32_t tgt) {   // sub_40E210 cases 36,37
    // Formação (sub_410210): com mais de 4 unidades, ataca-se na ordem de
    // DISTÂNCIA ao alvo (distMaxMin = maior + menor/2, com wrap em X), as mortas
    // por último — assim o escalonamento [+0x3C]=índice/4 põe as próximas na frente.
    std::vector<int> order(ids);
    if (order.size() > 4) {
        int tpx, tpy;
        const int16_t tLo = static_cast<int16_t>(tgt & 0xFFFF), tHi = static_cast<int16_t>(tgt >> 16);
        if ((tgt & 0x80000000u) && tHi != 0x7FFF && tLo > 0 && tLo < static_cast<int>(g_units.size())) {
            tpx = unitOX(g_units[tLo]); tpy = unitOY(g_units[tLo]);
        } else { tpx = (tLo << 5) + 16; tpy = (tHi << 5) + 16; }
        auto key = [&](int id) -> int {
            const UnitRecord& u = g_units[id];
            if (u.id == -1 || (u.flags & UNIT_DEAD)) return 0x7FFF;   // mortas por último
            const int dx = wrapAbsW(tpx - unitOX(u)), dy = std::abs(unitOY(u) - tpy);
            return dx >= dy ? dx + dy / 2 : dy + dx / 2;
        };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return key(a) < key(b); });
    }
    const std::vector<int>& seq = order;
    bool wallOrBld = false;                                  // var_114 → flag 0x4000000
    if (!(tgt & 0x80000000u)) {
        const int tx = static_cast<int16_t>(tgt & 0xFFFF) & tileMask(), ty = static_cast<int16_t>(tgt >> 16);
        const int ct = static_cast<int>(g_gameMap.rawCell(tx, ty) & 0x1F);
        wallOrBld = ct >= 0x17 && ct <= 0x19;
    } else {
        wallOrBld = (g_units[static_cast<int16_t>(tgt & 0xFFFF)].flags & UNIT_BUILDING) != 0;
    }
    int idx = 0;
    for (int id : seq) {
        UnitRecord& u = g_units[id];
        const int i = idx++;
        if ((u.owner & 0x0F) != owner) continue;
        u.flags = (u.flags & ~0x4000000u) | (wallOrBld ? 0x4000000u : 0u);
        unitResetCombat(id);
        if ((tgt & 0x80000000u) && static_cast<int16_t>(tgt & 0xFFFF) == id) continue;
        const bool cmdable = u.id != -1 && !((u.flags | (u.flags << 2)) & 0x80000u) && u.field22 != 0x0A;
        if ((u.flags & UNIT_ARMED) && cmdable) {
            if (type == 0x24) {
                u.flags &= ~0x400000u;
                if (tgt & 0x80000000u) u.flags &= ~UNIT_FIRE_GROUND;
            }
            unitOrderAttack(id, type, wps, tgt, i, 0);
        }
        // Prédio-torre (0x80) operando: vt[63] (sub_423E50); 0x24 limpa 0x400000.
        if ((u.flags & UNIT_TURRET_BLD) && (u.flags & UNIT_BUILDING) && structureOperational(u)) {
            if (type == 0x24) u.flags &= ~0x400000u;
            guardPostOrder(id, type, tgt);
        }
    }
}

// sub_43ACA0, ramo [+0x20] dos casos 36/37 (loc_43B2B7): com rota, anda; sem, para.
void weaponAttackStart(int id) {
    UnitRecord& u = g_units[id];
    if (!(u.flags & UNIT_ARMED)) { u.field20 = 0; return; }
    if (!u.hasOrder) { u.field22 = 0; u.field20 = 0; }
}

// Fim da ordem de ataque: guarda volta a guardar; senão ação 0 e alvo automático.
static void attackEnded(int id) {
    UnitRecord& u = g_units[id];
    if (u.flags & 0x400000u) unitOrderAttack(id, 0x25, {}, u.guardTarget(), 0, 0);
    else { u.action = 0; u.flags |= UNIT_AUTO_TARGET; }
}

void weaponAttackTick(int id) {                              // sub_43ACA0 jpt_43B312 case 36
    UnitRecord& u = g_units[id];
    const uint32_t ord = u.orderTarget();
    if (!(ord & 0x80000000u)) {                              // lugar
        if (u.flags & 0x4000000u) {
            const int tx = u.orderLo & tileMask(), ty = u.field4A;
            if (!(g_gameMap.rawCell(tx, ty) & 0x80000000u)) {   // a parede sumiu
                u.field4A = 0x7FFF;
                clearTarget(u);
                attackEnded(id);
                return;
            }
        }
        if (!inRangeTo(u, ord)) { u.action = 0; u.flags |= UNIT_AUTO_TARGET; clearTarget(u); return; }
        if (u.target() == ord) return;
        clearTarget(u);
        u.target4C = u.orderLo; u.field4E = u.field4A;
        return;
    }
    const int t = u.orderLo;
    const UnitRecord* tr = (t > 0 && t < static_cast<int>(g_units.size())) ? &g_units[t] : nullptr;
    const int16_t wpn = static_cast<int16_t>(u.cargo);
    if (!tr || tr->id == -1 || (tr->flags & UNIT_DEAD) || !(tr->flags & UNIT_DAMAGEABLE) ||
        ((tr->flags & UNIT_EMP) && wpn == 0x3C) || ((tr->flags & 0x40000u) && wpn == 0x47)) {
        dropRef(t);
        u.field4A = 0x7FFF;
        attackEnded(id);
        return;
    }
    if (u.flags & UNIT_ARMED) {
        if (!inRangeTo(u, ord)) {                            // fora do alcance: refaz a rota
            unitOrderAttack(id, u.action, {}, u.field4E != 0x7FFF ? u.target() : ord, 0, 1);
            return;
        }
        if (u.field4E != 0x7FFF && u.target4C == u.orderLo) {
            if (!(u.flags & 0x400000u)) unitOrderAttack(id, 0x24, {}, ord, 0, 0);
            else unitOrderAttack(id, 0x25, {}, u.guardTarget(), 0, 0);
            return;
        }
        clearTarget(u);
        u.target4C = u.orderLo; u.field4E = u.field4A;
        addRef(u.target4C);
        return;
    }
    if (u.target() == ord || !inRangeTo(u, ord)) return;
    clearTarget(u);
    u.target4C = u.orderLo; u.field4E = u.field4A;
    addRef(u.target4C);
}

void weaponGuardTick(int id) {                               // sub_43ACA0 jpt_43B312 case 37
    UnitRecord& u = g_units[id];
    u.flags |= 0x400000u;
    const int gi = u.guardLo;
    const bool gUnit = (u.guardTarget() & 0x80000000u) != 0;
    const bool gAlive = gi > 0 && gi < static_cast<int>(g_units.size()) && g_units[gi].id != -1 &&
                        !(g_units[gi].flags & UNIT_DEAD);
    if (!gAlive || !gUnit) {                                  // guarda desfeita
        if (!gAlive && gUnit) { dropRef(gi); u.guardHi = 0x7FFF; }
        if (u.orderTarget() & 0x80000000u) { dropRef(u.orderLo); u.field4A = 0x7FFF; }
        u.flags &= ~0x400000u;
        u.action = 0;
        return;
    }
    UnitRecord& g = g_units[gi];
    if (g.lastAttacker && TethysGame::tick() - g.lastAttackTick < 0x96) {
        const int ai = g.lastAttacker;
        const UnitRecord& a = g_units[ai];
        if (a.id != -1 && !(a.flags & UNIT_DEAD)) {
            const int ao = ownerOf(a);
            if (ao < kMaxPlayers && (g_playerData[ao].allyMask & (1u << ownerOf(u)))) return;
            unitOrderAttack(id, 0x24, {}, static_cast<uint32_t>(ai) | 0xFFFF0000u, 0, 1);
            return;
        }
        dropRef(ai);                                          // atacante morto
        g.lastAttacker = 0;
        u.field22 = SUB_WAIT; u.field3C = 4;
        return;
    }
    if (inRangeTo(u, u.orderTarget())) {                      // perto do guardado: espera
        u.field22 = SUB_WAIT; u.field3C = 0x28;
        if (g.lastAttacker) { dropRef(g.lastAttacker); g.lastAttacker = 0; }
        return;
    }
    unitOrderAttack(id, 0x25, {}, u.guardTarget(), 0, 0);    // longe: vai até ele
    u.field3C = 0x28;
}

void sendGuardTarget(int me, const std::vector<int>& sel, int tileX, int tileY,
                     const std::vector<uint32_t>& wps) {   // sub_4534F0
    std::vector<int> ids;
    for (int id : sel)
        if ((g_units[id].owner & 0x0F) == me && (g_units[id].flags & UNIT_ARMED)) ids.push_back(id);
    // Alvo: unidade sob o clique se o dono dela é aliado do jogador; senão 0.
    uint32_t tgt = 0xFFFF0000u;
    const int occ = g_gameMap.cellOccupant(tileX & tileMask(), tileY);
    if (occ > 0 && occ < static_cast<int>(g_units.size()) && g_units[occ].id != -1) {
        const int ow = ownerOf(g_units[occ]);
        if (ow < kMaxPlayers && (g_playerData[ow].allyMask & (1u << me))) tgt = static_cast<uint32_t>(occ) | 0xFFFF0000u;
    }
    if (ids.empty()) return;
    CommandPacket pkt;                                        // pacote 0x25
    pkt.type = CMD_GUARD;
    pkt.unitIds = ids;
    pkt.target = tgt;
    pkt.waypoints = wps;                                      // intermediarios (sub_451420)
    commandSubmit(me, pkt);
}

// sub_44DE10: a unidade atacada AGORA MESMO alerta a vizinhança. Todo veículo
// armado do próprio lado a menos de 96 px (3 tiles) em x e y que esteja parado
// e ainda sem agressor recebe o MESMO agressor, datado de 25 ticks atrás —
// sobram 125 dos 150 da janela de revide, ou seja o esquadrão reage junto em
// vez de só quem levou o tiro. Cada alerta soma uma referência ([+0x32]) no
// agressor, como o original.
//
// Só vale para dono HUMANO: o gate do topo é dword_56E304[dono] = isHuman
// ([jogador+0x28]). A IA tem o caminho dela, ScGroup::HasBeenAttacked.
static void alertNearbyAllies(int id) {
    const UnitRecord& me = g_units[id];
    const int owner = ownerOf(me);
    if (owner >= kMaxPlayers || !g_playerData[owner].isHuman) return;
    const int attacker = me.lastAttacker;
    if (attacker <= 0 || attacker >= static_cast<int>(g_units.size())) return;
    const uint32_t bit = 1u << owner;
    const int px = me.pixelX, py = me.pixelY;
    // O original anda na lista de unidades ordenada por [+0x18] (pixelY) nos dois
    // sentidos; a caixa de tiles dá o mesmo conjunto (veículo ocupa uma célula) e
    // o teste de pixel é o mesmo: px−96 < [+0x14] < px+96 (estrito, sem volta do
    // mapa, como no asm) e py−96 <= [+0x18] <= py+96.
    for (int ty = (py - 96) >> 5; ty <= (py + 96) >> 5; ++ty) {
        if (ty < 0 || ty >= g_gameMap.height()) continue;
        for (int tx = (px - 96) >> 5; tx <= (px + 96) >> 5; ++tx) {
            const int o = g_gameMap.cellOccupant(tx & tileMask(), ty);
            if (o <= 0 || o >= static_cast<int>(g_units.size()) || o == id) continue;
            UnitRecord& n = g_units[o];
            if (!alive(o)) continue;
            if (n.pixelX <= px - 96 || n.pixelX >= px + 96) continue;
            if (n.pixelY < py - 96 || n.pixelY > py + 96) continue;
            if (!(n.flags & UNIT_ARMED)) continue;                 // [+0x44] & 0x10
            const int no = ownerOf(n);
            if (no >= kMaxPlayers || !(g_playerData[no].allyMask & bit)) continue;
            if (n.flags & UNIT_EMP) continue;                     // [+0x44] & 0x80000
            if (n.action != 0 || n.lastAttacker != 0) continue;    // [+0x21] e [+0x26]
            n.lastAttacker = static_cast<int16_t>(attacker);
            n.lastAttackTick = TethysGame::tick() - 0x19;
            ++g_units[attacker].refCount;
        }
    }
}

void weaponAutoEngage(int id) {                               // sub_485340
    UnitRecord& u = g_units[id];
    // Antes de tudo (topo de sub_485340): atacada nos últimos 12 ticks → alerta.
    if (u.lastAttacker && TethysGame::tick() - u.lastAttackTick < 0x0C)
        alertNearbyAllies(id);
    const WeaponClass* c = classOf(static_cast<int16_t>(u.cargo));
    if (!c) return;
    if (!u.hasTarget() && (u.flags & UNIT_AUTO_TARGET)) {
        const int16_t wpn = static_cast<int16_t>(u.cargo);
        if (wpn == 0x41 || wpn == 0x42) return;              // autodestruição: nunca procura
        bool retaliate = u.action == 0 && u.field22 == 0;
        if (!retaliate && u.action == 0x25 && (u.field22 == 0 || u.field22 == 6)) {
            const int g = u.guardLo;
            retaliate = g > 0 && g < static_cast<int>(g_units.size()) && g_units[g].lastAttacker == 0;
        }
        // Contra-ataque: quem atacou nos últimos 150 ticks (não aliado) vira alvo
        // de uma ordem de ataque (0x24) — persegue.
        if (retaliate && u.lastAttacker && TethysGame::tick() - u.lastAttackTick < 0x96) {
            const UnitRecord& a = g_units[u.lastAttacker];
            const bool empVsScorpion = wpn == 0x3C && a.type == mapScorpion;
            const int ow = ownerOf(u);
            const bool ally = ow < kMaxPlayers && (g_playerData[ow].allyMask & (1u << ownerOf(a)));
            if (!empVsScorpion && !ally) {
                u.action = 0;
                unitOrderAttack(id, 0x24, {}, static_cast<uint32_t>(u.lastAttacker) | 0xFFFF0000u, 0, 0);
                return;
            }
        }
        uint32_t mask = UNIT_DAMAGEABLE;
        const int o = ownerOf(u);
        if (o < kMaxPlayers && g_playerData[o].isHuman) {
            if (u.field22 == 1) mask |= UNIT_VEHICLE;
            if (u.field4A != 0x7FFF) mask |= UNIT_ARMED;
        }
        const int t = findTarget(id, mask);
        if (t) setUnitTarget(g_units[id], t);
        return;
    }
    // Aproximação (loc_485507): atacando e andando, com o alvo da ordem vivo —
    // ao alcance, refaz a ordem (sem rota: para e aponta a arma).
    if (u.action == 0x24 && u.field22 == 1 && (u.orderTarget() & 0x80000000u) && alive(u.orderLo)) {
        // Ao alcance → re-emite (para e aponta); senão, com a ordem no último
        // waypoint (loc_485586: [ordem+8]==[ordem+4]) o alvo se moveu → re-emite
        // p/ re-rotear pela posição ATUAL do alvo (unitOrderAttack a relê).
        if (inRangeTo(u, u.orderTarget()) ||
            (u.hasOrder && u.order.curWaypoint == u.order.countMinus1))
            unitOrderAttack(id, u.action, {}, u.orderTarget(), 0, 0);
    }
}

void unitPeriodicTick(int id) {                               // prólogo de sub_43DE50
    UnitRecord& u = g_units[id];
    if (((u.id ^ TethysGame::tick()) & 7) != 0) return;
    if ((u.flags & 0x40000u) && (u.flags & UNIT_VEHICLE) && --u.foamTicks == 0)   // espuma [+0x54]
        u.flags &= ~0x40000u;
    if ((u.flags & 0x100000u) && --u.esgTicks == 0)               // ESG [+0x58]
        u.flags &= ~0x100000u;
    if (u.flags & UNIT_EMP) {                                   // loc_43DEA8: [+0x56]
        if (--u.empTicks != 0) return;
        u.flags &= ~UNIT_EMP;
        if (u.field22 == 0x0A) u.field22 = 0;
        if (u.flags & UNIT_BUILDING) { colonyMarkDirty(u.owner & 0x0F); return; }   // dword_56E37C
        if ((u.flags & UNIT_VEHICLE) && u.id != -1 && !(u.flags & UNIT_DEAD)) {     // loc_43DF05
            u.flags |= UNIT_LIGHTS;
            u.action = 0; u.field20 = 1; u.field3C = 0;
            if (u.flags & UNIT_ARMED) weaponAutoEngage(id);  // vt[0x50]
        }
        return;
    }
    if (u.lastAttacker && TethysGame::tick() - u.lastAttackTick > 0x2BC) {
        dropRef(u.lastAttacker);
        u.lastAttacker = 0;
    }
    if (u.id != -1 && !(u.flags & UNIT_DEAD) && disasterPeriodicTick(id)) return;   // vt[20] do desastre
    if (u.id != -1 && !(u.flags & UNIT_DEAD) && missilePeriodicTick(id)) return;    // vt[20] lançamento / EMP_BIG
    if (u.id != -1 && !(u.flags & UNIT_DEAD) && esgPeriodic(id)) return;            // vt[20] do ESG
    if (u.id != -1 && !(u.flags & UNIT_DEAD) && (u.flags & UNIT_VEHICLE) && (u.flags & UNIT_ARMED))
        weaponAutoEngage(id);                                     // vt[20] (sub_485340)
    else if (u.id != -1 && !(u.flags & UNIT_DEAD) && (u.flags & UNIT_BUILDING) && u.type == 0x17)
        guardPostPeriodic(id);                                    // vt[20] (sub_42CDD0)
}

// ---- Lançamento ----------------------------------------------------------------
// Dispersão (comum a sub_4A3A90/sub_4A3E00) + empurra o impacto p/ dentro de
// [pixels pulados, alcance].
static void scatterAndClamp(UnitRecord& p, int tx, int ty) {
    int a = std::abs(tx - p.pixelX);
    if (half(pxW()) < a) a = pxW() - a;
    int b = std::abs(ty - p.pixelY);
    const int s = octo(a, b) >> 4;
    p.impactX = TethysGame::getRand(s) + tx;
    p.impactY = TethysGame::getRand(s) + ty;
    p.impactY -= TethysGame::getRand(s);
    p.impactX -= TethysGame::getRand(s);
    p.impactX &= pxMask();
    const WeaponSheetDesc* d = weaponSheet(p.type);
    int minD = d ? d->skipped : 0;
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    int maxD = (w ? w->range : 0) << 5;
    if (p.flags & UNIT_TURRET_BLD) maxD += 32;
    const int maxY = g_gameMap.height() * 32 - 16;
    for (;;) {
        int vx = p.impactX - p.pixelX;
        const int vy = p.impactY - p.pixelY;
        vx = wrapVecMask(vx);
        p.vecX = vx; p.vecY = vy;
        p.rotation = dirOf(vx, vy);
        const int dist = octo(std::abs(vx), std::abs(vy));
        int L;
        if (dist < minD) L = minD;
        else if (dist > maxD) L = maxD;
        else break;
        p.impactX = p.pixelX + (kCos[p.rotation] * L >> 6);
        int iy = p.pixelY + (kSin[p.rotation] * L >> 6);
        if (iy > maxY) iy = maxY;
        if (iy <= 16) iy = 16;
        p.impactY = iy;
        if (dist < minD) minD = 0; else maxD *= 2;
    }
}

// sub_48AF60 (Thor's Hammer): dispersão por dist/8, o mesmo empurrão p/ dentro
// do alcance, sem voo — espera 0x10 ticks; [+0x58] = segmentos do raio
// (dword_4E9A70[dist/32]) e [+0x5C]/[+0x60] = sementes do desenho.
static const int kThorSegs[15] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static void launchThor(UnitRecord& p, int tx, int ty) {
    const int a = wrapAbsW(tx - p.pixelX), b = std::abs(ty - p.pixelY);
    const int dist = octo(a, b);
    const int si = dist >> 5;
    p.flightTicks = static_cast<int16_t>(si >= 0 && si < 15 ? kThorSegs[si] : kThorSegs[14]);
    const int s = dist >> 3;
    p.impactX = TethysGame::getRand(s) + tx;
    p.impactY = TethysGame::getRand(s) + ty;
    p.impactY -= TethysGame::getRand(s);
    p.impactX -= TethysGame::getRand(s);
    p.impactX &= pxMask();
    const WeaponSheetDesc* d = weaponSheet(p.type);
    int minD = d ? d->skipped : 0;
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    int maxD = (w ? w->range : 0) << 5;
    if (p.flags & UNIT_TURRET_BLD) maxD += 32;
    const int maxY = g_gameMap.height() * 32 - 16;
    for (;;) {
        const int vx = wrapVecMask(p.impactX - p.pixelX);
        const int vy = p.impactY - p.pixelY;
        p.rotation = dirOf(vx, vy);
        const int dist2 = octo(std::abs(vx), std::abs(vy));
        int L;
        if (dist2 < minD) L = minD;
        else if (dist2 > maxD) L = maxD;
        else break;
        p.impactX = p.pixelX + (kCos[p.rotation] * L >> 6);
        int iy = p.pixelY + (kSin[p.rotation] * L >> 6);
        if (iy > maxY) iy = maxY;
        if (iy <= 16) iy = 16;
        p.impactY = iy;
        if (dist2 < minD) minD = 0; else maxD *= 2;
    }
    p.field3C = 0x10;
    p.field23 = 0; p.field22 = 0x13; p.action = 0x36;
    p.vecX = static_cast<int>(TethysGame::rng().next());
    p.vecY = static_cast<int>(TethysGame::rng().next());
}

static void launch(int pid, int tx, int ty) {
    UnitRecord& p = g_units[pid];
    const WeaponClass* c = classOf(p.type);
    if (c && c->launch == LAUNCH_THOR) { launchThor(p, tx, ty); return; }
    scatterAndClamp(p, tx, ty);
    if (c && c->launch == LAUNCH_INSTANT) { p.field3C = 4; return; }      // sub_4A3E00
    // sub_4A3A90: sai da boca da arma, duração = dist/16 + 1 (mín. 3).
    int mx = 0, my = 0;
    if (p.sourceId > 0 && p.sourceId < static_cast<int>(g_units.size()))
        muzzleOffset(g_units[p.sourceId], (p.flags & UNIT_ALT_BARREL) != 0, p.rotation, mx, my);
    p.pixelX += mx;
    p.pixelY += my;
    int vx = wrapVecMask(p.impactX - p.pixelX);
    const int vy = p.impactY - p.pixelY;
    p.vecX = vx; p.vecY = vy;
    int T = (octo(std::abs(vx), std::abs(vy)) >> 4) + 1;
    if (T <= 3) T = 3;
    p.flightTicks = static_cast<int16_t>(T);
    p.flags |= UNIT_IN_FLIGHT;
    p.field3C = 0; p.field23 = 0; p.field22 = 0x13; p.action = 0x36;
    p.velQX = vx * 4 / T;
    p.velQY = vy * 4 / T;
}

// sub_4A6940: ponto de mira num alvo-unidade; se ele está andando, antecipa
// pela velocidade do passo vezes o tempo de voo (vt[11] da arma do atirador).
static void aimPoint(const UnitRecord& shooter, const UnitRecord& t, int& ax, int& ay) {
    if (t.field22 != 1) { ax = unitOX(t); ay = unitOY(t); return; }
    const int dx = wrapAbsW(unitOX(shooter) - unitOX(t));
    const int dy = std::abs(unitOY(shooter) - unitOY(t));
    const int dist = octo(dx, dy);
    const WeaponClass* c = classOf(static_cast<int16_t>(shooter.cargo));
    int lead;
    if (c && c->leadFixed) lead = c->leadFixed;
    else {
        lead = (dist >> 4) + (c ? c->leadAdd : 1);
        const int mn = c ? c->leadMin : 3;
        if (lead <= mn) lead = mn;
    }
    const int32_t px = t.velX * lead, py = t.velY * lead;
    const int cx = ((static_cast<uint32_t>(px) & 0xFFFF) + t.fracX) > 0xFFFF ? 1 : 0;
    const int cy = ((static_cast<uint32_t>(py) & 0xFFFF) + t.fracY) > 0xFFFF ? 1 : 0;
    ax = (unitOX(t) + (px >> 16) + cx) & pxMask();
    ay = unitOY(t) + (py >> 16) + cy;
}

// vt[36]: duração da explosão.
static int explosionDuration(const UnitRecord& p) {
    const WeaponClass* c = classOf(p.type);
    if (c && c->draw == DRAW_RAILGUN)                              // sub_4A29C0
        return lastFrameRaw(0x2F8) + lastFrameRaw(0x2F9) + lastFrameRaw(0x2FA) + 3;
    if (c && c->draw == DRAW_ESG) return 0xD8;                     // sub_480C10
    if (c && c->draw == DRAW_ACID)                                 // sub_4013D0
        return lastFrameRaw(0x415) * 3 + lastFrameRaw(0x416) + lastFrameRaw(0x414) + 4;
    return lastFrameRaw(c ? c->explodeAnim : 0);                   // sub_4A27C0
}

// ---- Dano (vt[34] = sub_4A35C0) --------------------------------------------------
static void damageMessage(const UnitRecord& t, int attackerPlayer) {
    if (ownerOf(t) != TethysGame::localPlayer()) return;
    if (attackerPlayer != ownerOf(t)) {
        if (t.flags & UNIT_BUILDING) postMessage(unitOX(t), unitOY(t), "Our colony is under attack!", 0x60);
        else postMessage(unitOX(t), unitOY(t), "We are under attack!", 0x61);
    } else {
        postMessage(unitOX(t), unitOY(t), "Damage Warning!", 0x99);
    }
}

static void explode(int pid) {
    const UnitRecord& p = g_units[pid];
    const WeaponSheetDesc* d = weaponSheet(p.type);
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    if (!d || !w) return;
    const int R = d->radius;
    const int rt = (R + 31) >> 5;
    const int ix = p.impactX, iy = p.impactY;
    if (const WeaponClass* c = classOf(p.type)) soundPlayAt(ix, iy, c->impactSound);   // desc+0x258
    const int x0 = (ix >> 5) - rt, x1 = (ix >> 5) + rt;
    int y0 = (iy >> 5) - rt; if (y0 <= 0) y0 = 0;
    int y1 = (iy >> 5) + rt; if (y1 > g_gameMap.height() - 1) y1 = g_gameMap.height() - 1;
    const int attacker = statsOf(p), source = p.sourceId;
    const bool flag80 = (p.flags & UNIT_TURRET_BLD) != 0;
    for (int x = x0; x <= x1; ++x) {
        const int cxp = x * 32 + 16;
        for (int y = y0; y <= y1; ++y) {
            const uint32_t cell = g_gameMap.rawCell(x & tileMask(), y);
            const int occ = static_cast<int>((cell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
            if (!occ) {
                // Parede (cellType 0x17..0x19): (raio − d) * Penetration / raio,
                // x2.5 de laser/micro-ondas → sub_4A23F0.
                const int ct = static_cast<int>(cell & 0x1F);
                if (ct < 0x17 || ct > 0x19) continue;
                const int wdy = std::abs(iy - y * 32 - 16);
                int wdx = (ix > cxp ? ix : cxp) - (ix < cxp ? ix : cxp);
                if (half(pxW()) < wdx) wdx = pxW() - wdx;
                const int wd = octo(wdx, wdy);
                if (wd >= R) continue;
                int wdmg = (R - wd) * w->penetration / R;
                if (p.type == 0x3D || p.type == 0x3E) wdmg = wdmg * 5 / 2;
                damageWall(x & tileMask(), y, wdmg);
                continue;
            }
            if (!alive(occ)) continue;
            UnitRecord& t = g_units[occ];
            if (!(t.flags & UNIT_DAMAGEABLE)) continue;
            int dx, dy, Reff;
            if (t.flags & UNIT_BUILDING) {
                dy = std::abs(iy - y * 32 - 16);
                dx = (ix > cxp ? ix : cxp) - (ix < cxp ? ix : cxp);
                Reff = R >= 0x1A ? R : 0x1A;
            } else {
                dy = std::abs(iy - unitOY(t));
                const int tx = unitOX(t);
                dx = (tx > ix ? tx : ix) - (tx < ix ? tx : ix);
                Reff = R;
            }
            if (half(pxW()) < dx) dx = pxW() - dx;
            const int dist = octo(dx, dy);
            if (dist >= Reff) continue;
            int dmg = ((armorOf(t) * w->concussion) >> 8) + w->penetration;
            if (flag80) dmg += dmg >> 1;
            // (dword_56DE64 — trapaça de dano x4 — fica em 0.)
            if (!(t.flags & UNIT_BUILDING)) dmg = (R - dist) * dmg / R;
            else dmg = (10000 - colonyDirtProtection(t)) * dmg / 20000;
            if (dmg <= 0) continue;
            t.damage = static_cast<int16_t>(t.damage + dmg);
            damageMessage(t, attacker);
            const int hp = maxHPOf(t);
            if (hp <= t.damage) {
                t.damage = static_cast<int16_t>(hp);
                t.flags |= UNIT_DEAD;
                killUnit(occ, 5);
            }
            // Crédito ao atirador ([+0x26]/[+0x28]).
            UnitRecord& tt = g_units[occ];
            if (tt.lastAttacker != source) {
                if (tt.lastAttacker) dropRef(tt.lastAttacker);
                if (source > 0 && source < static_cast<int>(g_units.size())) ++g_units[source].refCount;
                tt.lastAttacker = static_cast<int16_t>(source);
            }
            tt.lastAttackTick = TethysGame::tick();
        }
    }
}

// vt[34] do EMP (sub_4A4490): nas células do raio, cada unidade danificável
// fora de EMP (prédio só se consome energia; nunca aranhas) a < raio leva o
// crédito do atirador e, com chance ((blind-60)/2 + 200) * ((d/4 - d + R) *
// 0xE7 / R) / 256 contra rnd & 0xFF, fica em EMP por 0x10 (dono aliado do
// atirador) ou 0x20 passadas (vt[0x5C]) com o som 0xE7.
static void explodeEMP(int pid) {
    const UnitRecord& p = g_units[pid];
    const WeaponSheetDesc* d = weaponSheet(p.type);
    if (!d) return;
    const int R = d->radius;
    const int rt = (R + 31) >> 5;
    const int ix = p.impactX, iy = p.impactY;
    const int x0 = (ix >> 5) - rt, x1 = (ix >> 5) + rt;
    int y0 = (iy >> 5) - rt; if (y0 <= 0) y0 = 0;
    int y1 = (iy >> 5) + rt; if (y1 > g_gameMap.height() - 1) y1 = g_gameMap.height() - 1;
    const int base = 0xE7;                                        // 0x39C com dword_56DE64
    const int source = p.sourceId;
    const int8_t shooterBit = p.shooterBit;
    for (int x = x0; x <= x1; ++x) {
        const int cxp = x * 32 + 16;
        for (int y = y0; y <= y1; ++y) {
            const uint32_t cell = g_gameMap.rawCell(x & tileMask(), y);
            const int occ = static_cast<int>((cell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
            if (!occ || !alive(occ)) continue;
            UnitRecord& t = g_units[occ];
            if (!(t.flags & UNIT_DAMAGEABLE) || (t.flags & UNIT_EMP)) continue;
            int dx, dy;
            if (t.flags & UNIT_BUILDING) {
                const StructSheetDesc* sd = structSheet(t.type);
                if (!sd || sd->player[statsOf(t)].power == 0) continue;
                dy = std::abs(iy - y * 32 - 16);
                dx = (ix > cxp ? ix : cxp) - (ix < cxp ? ix : cxp);
            } else {
                if (t.type == mapSpider || t.type == mapScorpion) continue;
                dy = std::abs(iy - unitOY(t));
                const int tx = unitOX(t);
                dx = (tx > ix ? tx : ix) - (tx < ix ? tx : ix);
            }
            if (half(pxW()) < dx) dx = pxW() - dx;
            const int dist = octo(dx, dy);
            if (R <= dist) continue;
            if (t.lastAttacker != source) {
                if (t.lastAttacker) dropRef(t.lastAttacker);
                if (source > 0 && source < static_cast<int>(g_units.size())) ++g_units[source].refCount;
                t.lastAttacker = static_cast<int16_t>(source);
            }
            t.lastAttackTick = TethysGame::tick();
            const int c = (armorOf(t) - 60) / 2 + 200;
            const int a = (dist / 4 - dist + R) * base / R;
            const int chance = c * a / 256;
            if (static_cast<int>(TethysGame::rng().next() & 0xFF) > chance) continue;
            const int ow = ownerOf(t);
            const bool allied = ow < kMaxPlayers && (g_playerData[ow].allyMask & static_cast<uint32_t>(shooterBit));
            unitApplyEMP(occ, allied ? 0x10 : 0x20);
            const UnitRecord& tt = g_units[occ];
            soundPlayAt(unitOX(tt), unitOY(tt), 0xE7);
        }
    }
}

// vt[34] da espuma (sub_4A4F10): som do impacto; nas células do raio, cada
// unidade danificável sem espuma com 1500 / (dist + 200) < raio leva o crédito
// do atirador e dano fixo (veículo 0x3C, prédio 0x0F); se sobrevive e é
// veículo, gruda (vt[0x60] sub_44B5C0) pela Penetration da espuma do dono das
// estatísticas dela — e o projétil ganha 0x40000 (sem sprite de explosão).
static void explodeFoam(int pid) {
    const UnitRecord& p = g_units[pid];
    const WeaponSheetDesc* d = weaponSheet(p.type);
    if (!d) return;
    const int R = d->radius;
    const int rt = (R + 31) >> 5;
    const int ix = p.impactX, iy = p.impactY;
    const int x0 = (ix >> 5) - rt, x1 = (ix >> 5) + rt;
    int y0 = (iy >> 5) - rt; if (y0 <= 0) y0 = 0;
    int y1 = (iy >> 5) + rt; if (y1 > g_gameMap.height() - 1) y1 = g_gameMap.height() - 1;
    soundPlayAt(ix, iy, 0xF3);                                    // desc+0x258
    const int source = p.sourceId, attacker = statsOf(p);
    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            const uint32_t cell = g_gameMap.rawCell(x & tileMask(), y);
            const int occ = static_cast<int>((cell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
            if (!occ || !alive(occ)) continue;
            UnitRecord& t = g_units[occ];
            if (!(t.flags & UNIT_DAMAGEABLE) || (t.flags & 0x40000u)) continue;
            int dx, dy;
            if (t.flags & UNIT_BUILDING) {                        // (+16, como no original)
                dy = std::abs(iy - y * 32 + 16);
                dx = std::abs(ix - x * 32 + 16);
            } else {
                dy = std::abs(iy - unitOY(t));
                const int tx = unitOX(t);
                dx = (tx > ix ? tx : ix) - (tx < ix ? tx : ix);
            }
            if (half(pxW()) < dx) dx = pxW() - dx;
            if (1500 / (octo(dx, dy) + 200) >= R) continue;
            if (t.lastAttacker != source) {
                if (t.lastAttacker) dropRef(t.lastAttacker);
                if (source > 0 && source < static_cast<int>(g_units.size())) ++g_units[source].refCount;
                t.lastAttacker = static_cast<int16_t>(source);
            }
            t.lastAttackTick = TethysGame::tick();
            t.damage = static_cast<int16_t>(t.damage + ((t.flags & UNIT_BUILDING) ? 0x0F : 0x3C));
            damageMessage(t, attacker);
            const int hp = maxHPOf(t);
            if (hp <= t.damage) {
                t.damage = static_cast<int16_t>(hp);
                t.flags |= UNIT_DEAD;
                killUnit(occ, 5);
            } else if (t.flags & UNIT_VEHICLE) {
                const WeapPlayerStats* fw = weapStats(0x47, statsOf(t));
                unitApplyFoam(occ, fw ? fw->penetration : 0);
                g_units[pid].flags |= 0x40000u;
            }
        }
    }
}

// vt[20] do ESG (sub_481170, 1x a cada 8 ticks): ao explodir arma 13 minas no
// losango 5x5 de byte_4E40F0 em volta do tile do impacto ([+0x6E] = 0x1FFF,
// flag 0x8000). Cada mina dispara uma vez no 1º veículo inimigo (dono fora de
// [+0x6C]) sobre ela que não esteja passando rápido (passo restante >= 9):
// dano (blind * Concussion >> 8) + Penetration da arma (x1.5 de prédio-torre,
// /2 em ação 0x12), 0x100000 por 4 passadas ([+0x58]) e o crédito do atirador
// só se o dono do alvo é IA. Sem minas, o projétil sai (sub-estado 0x0B).
// vt[20] do ácido (sub_401050): explodido, liga 0x8000 e a cada passada fere
// TODA unidade danificável (sem teste de aliança) nos 3x3 tiles do impacto:
// (blind * Concussion >> 8) + Penetration; prédio /4 e pela terra (sub_42FF00);
// x1.5 de prédio-torre; /2 em ação 0x12; crédito só contra dono IA.
static void acidPeriodic(int pid) {
    UnitRecord& p = g_units[pid];
    if (p.exploded) p.flags |= 0x8000u;
    if (!(p.flags & 0x8000u)) return;
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    const int iy = p.impactY >> 5, ix = p.impactX >> 5;
    int y1 = iy + 2; if (y1 >= g_gameMap.height()) y1 = g_gameMap.height();
    int y = iy - 1; if (y <= 0) y = 0;
    for (; y < y1; ++y) {
        for (int x = ix - 1; x < ix + 2; ++x) {
            const uint32_t cell = g_gameMap.rawCell(x & tileMask(), y);
            const int occ = static_cast<int>((cell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
            if (!occ || !alive(occ)) continue;
            UnitRecord& t = g_units[occ];
            if (!(t.flags & UNIT_DAMAGEABLE)) continue;
            int dmg = ((armorOf(t) * (w ? w->concussion : 0)) >> 8) + (w ? w->penetration : 0);
            if (t.flags & UNIT_BUILDING) {
                dmg >>= 2;
                dmg = (10000 - colonyDirtProtection(t)) * dmg / 10000;
            }
            if (g_units[pid].flags & UNIT_TURRET_BLD) dmg += dmg >> 1;
            if (t.action == 0x12) dmg >>= 1;
            t.damage = static_cast<int16_t>(t.damage + dmg);
            const int ow = ownerOf(t);
            if (ow < kMaxPlayers && !g_playerData[ow].isHuman) {
                const int source = g_units[pid].sourceId;
                if (t.lastAttacker != source) {
                    if (t.lastAttacker) dropRef(t.lastAttacker);
                    if (source > 0 && source < static_cast<int>(g_units.size())) ++g_units[source].refCount;
                    t.lastAttacker = static_cast<int16_t>(source);
                }
                t.lastAttackTick = TethysGame::tick();
            }
            damageMessage(t, statsOf(g_units[pid]));
            const int hp = maxHPOf(t);
            if (hp <= t.damage) {
                t.damage = static_cast<int16_t>(hp);
                t.flags |= UNIT_DEAD;
                killUnit(occ, 5);
                // (Quem conta a baixa e o ponto da morte -- a remocao do predio
                // em structure.cpp e a explosao do veiculo mais abaixo --, como
                // nos tres pontos do original: sub_401050, sub_43ACA0 e
                // sub_481170.)
            }
        }
    }
}

static bool esgPeriodic(int pid) {
    UnitRecord& p = g_units[pid];
    if (p.type == 0x3B) { acidPeriodic(pid); return true; }
    if (p.type != 0x46) return false;
    if (p.exploded && !(p.flags & 0x8000u)) { p.esgMask = 0x1FFF; p.flags |= 0x8000u; }
    if (!(p.flags & 0x8000u)) return true;
    static const uint8_t kMines[25] = {0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1,
                                       0, 1, 1, 1, 0, 0, 0, 1, 0, 0};   // byte_4E40F0
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    const int y0 = (p.impactY >> 5) - 2, x0 = (p.impactX >> 5) - 2;
    int k = 0;
    uint16_t bit = 1;
    for (int y = y0; y < y0 + 5; ++y) {
        for (int x = x0; x < x0 + 5; ++x) {
            if (!kMines[k++] || y < 0 || y >= g_gameMap.height()) continue;
            const uint16_t me = bit;
            bit = static_cast<uint16_t>(bit << 1);
            if (!(g_units[pid].esgMask & me)) continue;
            const uint32_t cell = g_gameMap.rawCell(x & tileMask(), y);
            const int occ = static_cast<int>((cell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
            if (!occ || !alive(occ)) continue;
            UnitRecord& t = g_units[occ];
            if (!(t.flags & UNIT_DAMAGEABLE)) continue;
            const int ow = ownerOf(t);
            if (ow < kMaxPlayers && (g_playerData[ow].allyMask & static_cast<uint32_t>(g_units[pid].esgOwnerBit))) continue;
            if (!(t.flags & UNIT_VEHICLE)) continue;
            if (t.field22 == 1 && t.stepTicks >= 9) continue;
            int dmg = ((armorOf(t) * (w ? w->concussion : 0)) >> 8) + (w ? w->penetration : 0);
            if (g_units[pid].flags & UNIT_TURRET_BLD) dmg += dmg >> 1;
            // (dword_56DE64 x4 fica em 0; prédios — dirt — não chegam aqui.)
            if (t.action == 0x12) dmg >>= 1;
            t.damage = static_cast<int16_t>(t.damage + dmg);
            t.flags |= 0x100000u;
            t.esgTicks = 4;
            if (ow < kMaxPlayers && !g_playerData[ow].isHuman) {
                const int source = g_units[pid].sourceId;
                if (t.lastAttacker != source) {
                    if (t.lastAttacker) dropRef(t.lastAttacker);
                    if (source > 0 && source < static_cast<int>(g_units.size())) ++g_units[source].refCount;
                    t.lastAttacker = static_cast<int16_t>(source);
                }
                t.lastAttackTick = TethysGame::tick();
            }
            damageMessage(t, statsOf(g_units[pid]));
            const int hp = maxHPOf(t);
            if (hp <= t.damage) {
                t.damage = static_cast<int16_t>(hp);
                t.flags |= UNIT_DEAD;
                killUnit(occ, 5);
                // (Contadores de baixas: ver a nota acima -- dados mortos no original.)
            }
            UnitRecord& q = g_units[pid];
            q.esgMask = static_cast<uint16_t>(q.esgMask & ~me);
            if (!q.esgMask) q.field22 = SUB_REMOVE;
        }
    }
    return true;
}

void unitApplyFoam(int id, int ticks) {                       // vt[0x60] sub_44B5C0
    UnitRecord& u = g_units[id];
    u.flags |= 0x40000u;
    u.foamTicks = static_cast<int16_t>(ticks);
    u.foamStart = static_cast<int16_t>(TethysGame::tick());
}

// vt[34] das autodestruições (sub_4A4090): o dano em área de sub_4A35C0 sem
// crédito ao atirador, sem o x1.5 de prédio-torre e sem som — fere todos,
// inclusive o próprio atirador. Paredes/tubos 0x17..0x19 levam sub_4A23F0 com
// (raio − d) * (Concussion + Penetration) / raio.
static void explodeDestruct(int pid) {
    const UnitRecord& p = g_units[pid];
    const WeaponSheetDesc* d = weaponSheet(p.type);
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    if (!d || !w) return;
    const int R = d->radius;
    const int rt = (R + 31) >> 5;
    const int ix = p.impactX, iy = p.impactY;
    const int x0 = (ix >> 5) - rt, x1 = (ix >> 5) + rt;
    int y0 = (iy >> 5) - rt; if (y0 <= 0) y0 = 0;
    int y1 = (iy >> 5) + rt; if (y1 > g_gameMap.height() - 1) y1 = g_gameMap.height() - 1;
    const int attacker = statsOf(p);
    for (int x = x0; x <= x1; ++x) {
        const int cxp = x * 32 + 16;
        for (int y = y0; y <= y1; ++y) {
            const uint32_t cell = g_gameMap.rawCell(x & tileMask(), y);
            const int occ = static_cast<int>((cell & GameMap::CELL_OCCUPANCY) >> GameMap::CELL_OCC_SHIFT);
            if (!occ) {
                // Parede: (raio − d) * (Concussion + Penetration) / raio → sub_4A23F0.
                const int ct = static_cast<int>(cell & 0x1F);
                if (ct < 0x17 || ct > 0x19) continue;
                const int wdy = std::abs(iy - y * 32 - 16);
                int wdx = (ix > cxp ? ix : cxp) - (ix < cxp ? ix : cxp);
                if (half(pxW()) < wdx) wdx = pxW() - wdx;
                const int wd = octo(wdx, wdy);
                if (R <= wd) continue;
                damageWall(x & tileMask(), y, (R - wd) * (w->concussion + w->penetration) / R);
                continue;
            }
            if (!alive(occ)) continue;
            UnitRecord& t = g_units[occ];
            if (!(t.flags & UNIT_DAMAGEABLE)) continue;
            int dx, dy;
            if (t.flags & UNIT_BUILDING) {
                dy = std::abs(iy - y * 32 - 16);
                dx = (ix > cxp ? ix : cxp) - (ix < cxp ? ix : cxp);
            } else {
                dy = std::abs(iy - unitOY(t));
                const int tx = unitOX(t);
                dx = (tx > ix ? tx : ix) - (tx < ix ? tx : ix);
            }
            if (half(pxW()) < dx) dx = pxW() - dx;
            const int dist = octo(dx, dy);
            if (R <= dist) continue;
            int dmg = ((armorOf(t) * w->concussion) >> 8) + w->penetration;
            if (!(t.flags & UNIT_BUILDING)) dmg = (R - dist) * dmg / R;
            else dmg = (10000 - colonyDirtProtection(t)) * dmg / 20000;
            if (dmg <= 0) continue;
            t.damage = static_cast<int16_t>(t.damage + dmg);
            damageMessage(t, attacker);
            const int hp = maxHPOf(t);
            if (hp <= t.damage) {
                t.damage = static_cast<int16_t>(hp);
                t.flags |= UNIT_DEAD;
                killUnit(occ, 5);
            }
        }
    }
}

// sub_43A6A0 (comando 0x14): Starflare/Supernova armado mira o próprio tile e
// dispara (vt[4]); os demais criam a SCUTTLE_DESTRUCT (0x45) no lugar, com o
// vt[32] apontando p/ si, e morrem (0x20000, ação 0x2F, [+0x3C] = 5) — prédio
// em obra (0x21/0x22, estado 0) leva junto o ConVec que o constrói ([+0x48]).
void unitSelfDestruct(int id) {
    UnitRecord& u = g_units[id];
    if ((u.flags & 0x90u) && (u.cargo == 0x41 || u.cargo == 0x42)) {
        if (u.targetIsUnit() && u.hasTarget()) dropRef(u.target4C);
        u.target4C = static_cast<int16_t>(unitOX(u) >> 5);
        u.field4E = static_cast<uint16_t>(unitOY(u) >> 5);
        fireWeapon(id);                                           // vt[4] sub_484F80
        return;
    }
    const int pid = createProjectile(0x45, unitOX(u), unitOY(u), ownerOf(u));
    if (!pid) return;
    UnitRecord& p = g_units[pid];
    UnitRecord& s = g_units[id];
    p.owner = static_cast<uint8_t>((p.owner & 0xF6) | 6);
    ++s.refCount;
    p.sourceId = id;
    destructArm(pid, id, id);                                     // vt[32] sub_4A4030
    UnitRecord& v = g_units[id];
    v.flags |= UNIT_DEAD;
    if ((v.flags & UNIT_BUILDING) && (v.action == 0x21 || v.action == 0x22) && (v.state34 >> 16) == 0) {
        const int b = v.builderId;
        if (b > 0 && b < static_cast<int>(g_units.size()) && g_units[b].id != -1) {
            UnitRecord& c = g_units[b];
            c.damage = static_cast<int16_t>(maxHPOf(c));
            c.flags |= UNIT_DEAD;
            killUnit(b, 5);
        }
    }
    UnitRecord& v2 = g_units[id];
    v2.action = 0x2F; v2.field3C = 5; v2.field20 = 1;
}

// ---- Máquina do projétil ----------------------------------------------------------
// vt[29] do atirador: veículo (sub_44B620) vivo, sem EMP e fora do estado 0xA.
static bool sourceReady(int src) {
    if (src <= 0 || src >= static_cast<int>(g_units.size())) return false;
    const UnitRecord& s = g_units[src];
    if (s.id == -1) return false;
    if (s.flags & UNIT_VEHICLE) {
        if (((s.flags << 2) | s.flags) & UNIT_EMP) return false;   // 0x20000 ou 0x80000
        return s.field22 != 0x0A;
    }
    return !(s.flags & UNIT_DEAD);                                // sub_4018E0
}

void weaponProjectileAction(int id) {                         // case 54
    UnitRecord& p = g_units[id];
    if (p.field23 == 0x12) {
        if (!sourceReady(p.sourceId)) { p.field22 = SUB_REMOVE; p.field3C = 1; return; }
        const UnitRecord& s = g_units[p.sourceId];
        p.pixelX = unitOX(s);
        p.pixelY = unitOY(s);
        int tx, ty;
        if (p.targetIsUnit()) aimPoint(s, g_units[p.target4C], tx, ty);
        else { tx = p.target4C * 32 + 16; ty = static_cast<int16_t>(p.field4E) * 32 + 16; }
        launch(id, tx, ty);
        g_units[id].field22 = 0x13;
        return;
    }
    if (p.field23 == 0x13) {
        if (!sourceReady(p.sourceId)) { p.field22 = SUB_REMOVE; p.field3C = 1; return; }
        if (const WeaponClass* c = classOf(p.type))      // sub_43ACA0: desc+0x254
            soundPlayAt(unitOX(p), unitOY(p), c->fireSound);
        p.field22 = (p.flags & UNIT_IN_FLIGHT) ? 0x0F : SUB_WAIT;
        return;
    }
    if (!p.exploded) {
        p.exploded = 1;
        if (p.type == 0x46 || p.type == 0x3B) {}                  // vt[34] nullsub_122 / nullsub_5 (efeito no vt[20])
        else if (p.type == 0x3C) explodeEMP(id);                 // vt[34] sub_4A4490
        else if (p.type == 0x47) explodeFoam(id);                 // vt[34] sub_4A4F10
        else if (p.type >= 0x41 && p.type <= 0x45) explodeDestruct(id);   // vt[34] sub_4A4090
        else explode(id);                                         // vt[34] sub_4A35C0
        UnitRecord& q = g_units[id];
        q.field22 = SUB_WAIT;
        q.field3C = explosionDuration(q);
    } else {
        p.field22 = SUB_REMOVE;
    }
}

// Sub-estados 0x12/0x13 (sub_43DE50 cases 18/19): gira a torre do atirador
// ([+0x2D]) até a direção do projétil ([+0x1C]) pelo Turret Turn Rate da arma.
static void alignTurret(UnitRecord& p, UnitRecord& s) {
    const WeapPlayerStats* w = weapStats(p.type, statsOf(p));
    const int tr = w ? w->turnRate : 1;
    const int8_t diff = static_cast<int8_t>(p.rotation - s.turretDir);
    if (std::abs(static_cast<int>(diff)) < tr) {
        p.turretDir = p.rotation;
        s.turretDir = p.rotation;
        p.field23 = p.field22;
        p.field22 = 0;
    } else if (diff < 0) {
        s.turretDir = static_cast<uint8_t>(s.turretDir - tr);
    } else {
        s.turretDir = static_cast<uint8_t>(s.turretDir + tr);
    }
}

bool weaponProjectileSubstate(int id) {
    UnitRecord& p = g_units[id];
    if (!isWeaponProjectile(p.type)) return false;
    switch (p.field22) {
        case 0x0F: {                                              // voo (case 15)
            const int t = ++p.field3C;
            const int T = p.flightTicks;
            if (T < t) { p.field23 = p.field22; p.field22 = 0; return true; }
            p.pixelX = p.impactX + (t - T) * p.vecX / T;
            p.pixelY = p.impactY + (t - T) * p.vecY / T;
            return true;
        }
        case 0x12: {                                              // mira (case 18)
            if (p.sourceId <= 0 || p.sourceId >= static_cast<int>(g_units.size())) return true;
            UnitRecord& s = g_units[p.sourceId];
            int vx, vy;
            if (!p.targetIsUnit()) {
                vx = p.target4C * 32 - unitOX(s) + 16;
                vy = static_cast<int16_t>(p.field4E) * 32 - unitOY(s) + 16;
            } else {
                const UnitRecord& t = g_units[p.target4C];
                vx = unitOX(t) - unitOX(s);
                vy = unitOY(t) - unitOY(s);
            }
            p.rotation = dirOf(wrapVecMask(vx), vy);
            alignTurret(p, s);
            return true;
        }
        case 0x13: {                                              // alinha (case 19)
            if (p.sourceId <= 0 || p.sourceId >= static_cast<int>(g_units.size())) return true;
            alignTurret(p, g_units[p.sourceId]);
            return true;
        }
        default: return false;
    }
}

void weaponUnitRemoved(int id) {                              // vt[19] (sub_4A4910...)
    UnitRecord& r = g_units[id];
    if ((isWeaponProjectile(r.type) || isEmpBlast(r.type)) && r.sourceId) dropRef(r.sourceId);
    if (r.lastAttacker) { dropRef(r.lastAttacker); r.lastAttacker = 0; }
    if (r.hasTarget() && r.targetIsUnit() && !isWeaponProjectile(r.type)) clearTarget(r);
}

// ---- Morte do veículo (case 47, loc_43D8A3) --------------------------------------
// sub_48BAE0 / sub_48B6F0: marca de queimado no terreno sob o veículo destruído.
static int scorchTile(int tile) {
    const auto& types = g_gameMap.terrainTypes();
    bool any = false;
    for (const auto& t : types) {
        for (int k = 0; k < 3; ++k) {
            const int lo = t.word(0xDE + k * 4), hi = t.word(0xE0 + k * 4);
            if (lo && lo <= tile && hi >= tile) { any = true; break; }
        }
        if (any) break;
    }
    if (!any) return 0;
    int base = 0;
    for (const auto& t : types) {
        if (t.word(0) > tile || t.word(2) < tile) continue;
        int b = t.word(0xDC);
        if (!b) continue;
        bool dozed = false;
        for (const auto& u : types) if (u.word(4) == tile) { dozed = true; break; }
        if (!dozed && tile != 10) b += 3;
        base = b;
    }
    if (base) base += TethysGame::getRand(3);
    return base;
}

int scorchTileFor(int tile) { return scorchTile(tile); }
int mapLineOfSight(int x0, int y0, int x1, int y1) { return lineOfSight(x0, y0, x1, y1); }

void vehicleDeath(int id) {
    UnitRecord& r = g_units[id];
    // vt[35] (loc_43D8B3 chama só com o bit 1 de [+0x44], ou seja só em veículo):
    // em toda classe de veículo é sub_483850, que põe dword_56E37C[dono] = 1 — a
    // economia do dono recalcula no próximo passe. (Conferido nas vtables: das 40
    // tabelas de unidade, só off_4D1788 tem nullsub ali.)
    colonyMarkDirty(ownerOf(r));
    const int hp = maxHPOf(r);
    if (hp == r.damage) {
        // Explosão: AMBIENT_ANIM 0x87D (Spider/Scorpion: 0x87C com a cor do dono).
        const bool arach = r.type == mapSpider || r.type == mapScorpion;
        const int eid = createAmbientAnim(r.pixelX, r.pixelY, arach ? 0x87C : 0x87D, 2, 0);
        if (eid && arach) {
            g_units[eid].flags |= UNIT_EFFECT_COLOR;
            g_units[eid].effectColor = ownerOf(g_units[id]);
        }
        const UnitRecord& v = g_units[id];
        const int tx = (unitOX(v) >> 5) & tileMask(), ty = unitOY(v) >> 5;
        const int tile = g_gameMap.getTile(LOCATION{tx, ty});
        const int nt = scorchTile(tile);
        if (nt) g_gameMap.setTileAndCellType(tx, ty, nt, g_gameMap.getCellType(LOCATION{tx, ty}));
    }
    UnitRecord& v = g_units[id];
    if (v.lastAttacker > 0 && v.lastAttacker < static_cast<int>(g_units.size()) &&
        ((g_units[v.lastAttacker].owner ^ v.owner) & 0x0F)) {
        const int quem = ownerOf(g_units[v.lastAttacker]);
        moraleEnemyVehicleKilled(quem);                               // sub_44FD40
        // sub_481170: os dois contadores da tela de estatisticas.
        if (quem < kMaxPlayers) ++g_playerData[quem].enemyUnitsDestroyed;
        ++g_playerData[ownerOf(v)].unitsLost;
    }
    // Som da morte (sub_43ACA0): com atacante 0x4D; senão pela carga — Starflare
    // (0x41) 0x4F / Tiger 0x4B, Supernova (0x42) 0x4B / Tiger 0x4A, outros 0x4E.
    {
        int snd = 0x4E;
        if (v.lastAttacker) snd = 0x4D;
        else if (v.cargo == 0x41) snd = v.type == mapTiger ? 0x4B : 0x4F;
        else if (v.cargo == 0x42) snd = v.type == mapTiger ? 0x4A : 0x4B;
        soundPlayAt(unitOX(v), unitOY(v), snd);
    }
    if (ownerOf(v) == TethysGame::localPlayer() && maxHPOf(v) == v.damage) {
        char msg[100];
        std::snprintf(msg, sizeof(msg), "%s destroyed", unitTitle(v).c_str());
        postMessage(unitOX(v), unitOY(v), msg, 0x68);
    }
    v.field22 = SUB_REMOVE;
}

// ---- Desenho -------------------------------------------------------------------------
static void pushSprite(std::vector<WeaponDrawOp>& ops, int raw, int frame, int x, int y) {
    WeaponDrawOp op; op.kind = WeaponDrawOp::SPRITE;
    op.anim = raw - kAnimBias; op.frame = frame; op.x = x; op.y = y;
    ops.push_back(op);
}

static void muzzleWorld(const UnitRecord& p, int& x, int& y) {
    const int sid = p.sourceId;
    int mx = 0, my = 0;
    const UnitRecord& s = g_units[(sid > 0 && sid < static_cast<int>(g_units.size())) ? sid : 0];
    if (alive(sid)) muzzleOffset(s, (p.flags & UNIT_ALT_BARREL) != 0, s.turretDir, mx, my);
    x = (unitOX(s) + mx) & pxMask();
    y = unitOY(s) + my;
}

void weaponDrawOps(int id, std::vector<WeaponDrawOp>& ops) {
    const UnitRecord& p = g_units[id];
    const WeaponClass* c = classOf(p.type);
    if (!c) return;
    if (p.field22 == 0 || p.field22 >= 0x12) return;
    DrawKind kind = c->draw;
    if (kind == DRAW_ACID && !p.exploded) kind = DRAW_PROJECTILE;    // sub_401560 → sub_4A2D50
    switch (kind) {
        case DRAW_THOR: {                                         // sub_48AD70
            // Explodido ([+0x54]): só chama vt[35] — não desenha nada.
            if (p.exploded) return;
            const int fc = p.field3C;
            static const int kPick[18] = {0, 2, 2, 2, 0, 1, 0, 2, 0, 0, 1, 1, 1, 0, 2, 2, 0, 0};   // dword_4E9A28
            const int k = fc >= 0 && fc < 18 ? kPick[fc] : 0;
            if (!k) return;
            WeaponDrawOp op; op.kind = WeaponDrawOp::BOLT;
            muzzleWorld(p, op.x, op.y);
            op.x2 = p.impactX; op.y2 = p.impactY;
            op.seed = static_cast<uint32_t>(k == 1 ? p.vecX : p.vecY);       // [+0x58 + k*4]
            uint8_t ang = static_cast<uint8_t>((p.vecX & 0x0F) + 0x0C);
            if (k == 1) ang = static_cast<uint8_t>(-ang);
            op.phase = ang;
            op.segs = p.flightTicks;
            ops.push_back(op);
            return;
        }
        case DRAW_SCORPION:                                       // sub_4A31D0
        case DRAW_LASER:                                          // sub_4A3420
        case DRAW_MICROWAVE: {                                    // sub_4A3000
            bool beam = !p.exploded || p.field3C > (c->draw == DRAW_SCORPION ? 4 : 5);
            if (c->draw == DRAW_MICROWAVE && p.field3C == 2) beam = false;
            if (beam) {
                int mx, my; muzzleWorld(p, mx, my);
                WeaponDrawOp op;
                if (c->draw != DRAW_MICROWAVE) {
                    // sub_41E8F0: OR de 0x7C00 (laser) / 0x0018 (Scorpion) em 2x2.
                    op.kind = WeaponDrawOp::LASER_LINE;
                    op.orColor = c->draw == DRAW_LASER ? 0x7C00 : 0x0018;
                    op.x = mx; op.y = my; op.x2 = p.impactX; op.y2 = p.impactY;
                } else {
                    op.kind = WeaponDrawOp::MICROWAVE_LINE;
                    op.x = p.impactX; op.y = p.impactY; op.x2 = mx; op.y2 = my;
                    op.phase = (TethysGame::tick() & 0x0F) * 2;
                }
                ops.push_back(op);
            }
            if (p.exploded)
                pushSprite(ops, c->explodeAnim, lastFrameRaw(c->explodeAnim) - p.field3C, p.impactX, p.impactY);
            return;
        }
        case DRAW_DESTRUCT:                                       // sub_4A3370 / sub_4A64D0
            if (p.exploded)
                pushSprite(ops, c->explodeAnim, lastFrameRaw(c->explodeAnim) - p.field3C, p.impactX, p.impactY);
            return;
        case DRAW_ACID: {                                         // sub_401560
            if (!p.exploded) break;                               // → sub_4A2D50
            const int dur = explosionDuration(p);
            int e = dur - p.field3C;                              // var_1C
            if (p.flightTicks > 0) {
                const int e0 = e > 0 ? e : 0;
                int X8 = p.impactX * 8, Y8 = p.impactY * 8;
                for (int i = e0;; ++i) {
                    int lim = e0 + p.flightTicks * 2;
                    const int cap = lastFrameRaw(0x2C0) * 2;
                    if (lim >= cap) lim = cap;
                    if (lim <= i) break;
                    X8 -= p.velQX; Y8 -= p.velQY;
                    pushSprite(ops, 0x2C0 + i % 3, i / 3, X8 >> 3, Y8 >> 3);
                }
            }
            if (p.field3C % 20 == 0) {                            // som 0xE5 a cada 20 ticks
                WeaponDrawOp s; s.kind = WeaponDrawOp::SOUND;
                s.anim = 0xE5; s.x = p.pixelX; s.y = p.pixelY;
                ops.push_back(s);
            }
            const int n414 = lastFrameRaw(0x414) + 1;
            if (n414 > e) { pushSprite(ops, 0x414, e, p.impactX, p.impactY); return; }
            e -= n414;
            const int n415 = lastFrameRaw(0x415) + 1;
            if (e < n415 * 3) pushSprite(ops, 0x415, e % n415, p.impactX, p.impactY);
            else pushSprite(ops, 0x416, e - n415 * 3, p.impactX, p.impactY);
            return;
        }
        case DRAW_ESG: {                                          // sub_480DB0
            const int tick = static_cast<int>(TethysGame::tick());
            if (p.exploded) {
                const int dur = explosionDuration(p);                 // 0xD8
                const int e = dur - p.field3C > 0 ? dur - p.field3C : 0;
                const int nT = lastFrameRaw(0x2C0), T = p.flightTicks;
                if (nT + T * 2 > e && T > 0) {
                    int X8 = p.impactX * 8, Y8 = p.impactY * 8;
                    for (int i = e;; ++i) {
                        int lim = e + T * 2;
                        if (lim >= nT * 2) lim = nT * 2;
                        if (lim <= i) break;
                        X8 -= p.velQX; Y8 -= p.velQY;
                        pushSprite(ops, 0x2C0 + (tick + i) % 3, i / 3, X8 >> 3, Y8 >> 3);
                    }
                }
                if (lastFrameRaw(c->explodeAnim) >= dur - p.field3C) pushSprite(ops, c->explodeAnim, e, p.impactX, p.impactY);
            } else {
                if (p.field3C) pushSprite(ops, 0x2BC, 0x1F - (static_cast<uint8_t>(p.rotation + 4) >> 3),
                                          p.pixelX & pxMask(), p.pixelY);
                int X8 = (p.pixelX & pxMask()) * 8, Y8 = p.pixelY * 8;
                int n = p.field3C * 2;
                const int cap = lastFrameRaw(0x2C0) * 3;
                if (n >= cap) n = cap;
                if (p.flightTicks * 2 < n) n = p.flightTicks * 2;
                for (int b = 0; b < n; ++b) {
                    X8 -= p.velQX; Y8 -= p.velQY;
                    pushSprite(ops, 0x2C0 + (tick + b) % 3, b / 3, X8 >> 3, Y8 >> 3);
                }
            }
            return;
        }
        case DRAW_PROJECTILE: {                                   // sub_4A2D50
            if (p.exploded) {
                int f = lastFrameRaw(c->explodeAnim) - p.field3C;
                if (f <= 0) f = 0;
                if (p.flightTicks > 0) {
                    int X8 = p.impactX * 8, Y8 = p.impactY * 8;
                    for (int e = f;; ++e) {
                        int lim = f + p.flightTicks * 2;
                        const int cap = lastFrameRaw(0x2C0) * 2;
                        if (lim >= cap) lim = cap;
                        if (lim <= e) break;
                        X8 -= p.velQX; Y8 -= p.velQY;
                        pushSprite(ops, 0x2C0 + e % 3, e / 3, X8 >> 3, Y8 >> 3);
                    }
                }
                if (!(p.flags & 0x40000)) pushSprite(ops, c->explodeAnim, f, p.impactX, p.impactY);
            } else {
                if (p.field3C) pushSprite(ops, 0x2BC, 0x1F - (static_cast<uint8_t>(p.rotation + 4) >> 3),
                                          p.pixelX & pxMask(), p.pixelY);
                int X8 = (p.pixelX & pxMask()) * 8, Y8 = p.pixelY * 8;
                int n = p.field3C * 2;
                const int cap = lastFrameRaw(0x2C0) * 3;
                if (n >= cap) n = cap;
                if (p.flightTicks * 2 < n) n = p.flightTicks * 2;
                for (int b = 0; b < n; ++b) {
                    X8 -= p.velQX; Y8 -= p.velQY;
                    pushSprite(ops, 0x2C0 + b % 3, b / 3, X8 >> 3, Y8 >> 3);
                }
            }
            return;
        }
        case DRAW_RAILGUN: {                                      // sub_4A2A00
            const int l8 = lastFrameRaw(0x2F8) + 1, l9 = lastFrameRaw(0x2F9) + 1, lA = lastFrameRaw(0x2FA) + 1;
            GameRNG rng; rng.setSeed(static_cast<uint32_t>(p.id + 0x3125323));
            if (p.exploded) {
                const int E = c->explodeAnim;
                int X8 = p.impactX * 8, Y8 = p.impactY * 8;
                const int f = explosionDuration(p) - p.field3C;       // var_10
                for (int k = 0; k < p.flightTicks; ++k) {
                    const int e = f + k;
                    const uint32_t rnd = rng.next();
                    X8 -= p.velQX * 2; Y8 -= p.velQY * 2;
                    if (e < l8) pushSprite(ops, 0x2F8, e, X8 >> 3, Y8 >> 3);
                    else if (e - l8 < l9) pushSprite(ops, 0x2F9, static_cast<int>((rnd + static_cast<uint32_t>(e - l8)) % static_cast<uint32_t>(l9)), X8 >> 3, Y8 >> 3);
                    else if (e - l8 - l9 < lA) pushSprite(ops, 0x2FA, e - l8 - l9, X8 >> 3, Y8 >> 3);
                }
                if (lastFrameRaw(E) >= f) pushSprite(ops, E, f, p.impactX, p.impactY);
            } else {
                const int x = p.pixelX & pxMask(), y = p.pixelY;
                pushSprite(ops, 0x389, 0x1F - (static_cast<uint8_t>(p.rotation + 4) >> 3), x, y);
                int X8 = x * 8, Y8 = y * 8;
                int n = p.field3C - 1;
                if (n <= 0) n = 0;
                for (int k = 0; k < n; ++k) {
                    const uint32_t rnd = rng.next();
                    X8 -= p.velQX * 2; Y8 -= p.velQY * 2;
                    if (k < l8) pushSprite(ops, 0x2F8, k, X8 >> 3, Y8 >> 3);
                    else pushSprite(ops, 0x2F9, static_cast<int>((rnd + static_cast<uint32_t>(k - l8)) % static_cast<uint32_t>(l9)), X8 >> 3, Y8 >> 3);
                }
            }
            return;
        }
    }
}

// ---- Raio do Thor's Hammer (sub_48A850) ---------------------------------------
namespace {
struct BoltCtx {
    GameRNG rng;                       // unk_5737E8
    int depth = 0;                     // dword_5737F0
    int endX = 0, endY = 0;            // dword_5737F8 / dword_5737FC
    int camX = 0, camY = 0;            // vista +0x830 / +0x834
    int r = 0, b = 0;                  // superfície +0x100 / +0x104 (l = t = 0)
    const std::function<void(int, int, uint16_t)>* put = nullptr;
};

// sub_41E440: Cohen-Sutherland — só diz se a linha toca o retângulo.
bool clipTest(int x0, int y0, int x1, int y1, int l, int t, int r, int b) {
    for (int guard = 0; guard < 16; ++guard) {
        int c0 = 0, c1 = 0;
        if (l > x0) c0 = 1; else if (r < x0) c0 = 4;
        if (l > x1) c1 = 1; else if (r < x1) c1 = 4;
        if (t > y0) c0 |= 2; else if (b < y0) c0 |= 8;
        if (t > y1) c1 |= 2; else if (b < y1) c1 |= 8;
        if (c0 & c1) return false;
        if (!(c0 | c1)) return true;
        if (!c0) { std::swap(x0, x1); std::swap(y0, y1); c0 = c1; }
        if (c0 & 1) {
            if (x1 == x0) return false;
            y0 = (x1 - l) * (y0 - y1) / (x1 - x0) + y1; x0 = l;
        } else if (c0 & 4) {
            if (x0 == x1) return false;
            y0 = (r - x1) * (y0 - y1) / (x0 - x1) + y1; x0 = r;
        } else if (c0 & 2) {
            if (y1 == y0) return false;
            x0 = (y1 - t) * (x0 - x1) / (y1 - y0) + x1; y0 = t;
        } else {
            if (y0 == y1) return false;
            x0 = (b - y1) * (x0 - x1) / (y0 - y1) + x1; y0 = b;
        }
    }
    return false;
}

// sub_41E620 (big: 2x2, dentro de (0, r-1) x (0, b-1)) / sub_41E7B0 (1 px):
// passo aleatório — anda em x se |rnd*dy| < |rnd*dx|, senão em y.
void boltLine(BoltCtx& c, int x0, int y0, int x1, int y1, uint16_t color, bool big) {
    if (!clipTest(x0, y0, x1, y1, 0, 0, c.r, c.b)) return;
    const int R = big ? c.r - 1 : c.r, B = big ? c.b - 1 : c.b;
    int x = x0, y = y0;
    for (;;) {
        if (x > 0 && x < R && y > 0 && y < B) {
            (*c.put)(x, y, color);
            if (big) { (*c.put)(x + 1, y, color); (*c.put)(x, y + 1, color); (*c.put)(x + 1, y + 1, color); }
        }
        const int dx = x1 - x, dy = y1 - y;
        if (!(dx | dy)) return;
        const int32_t rx = static_cast<int32_t>(static_cast<uint32_t>(c.rng.next() & 0x7FFFF) * static_cast<uint32_t>(dx));
        const int32_t ry = static_cast<int32_t>(static_cast<uint32_t>(c.rng.next() & 0x7FFFF) * static_cast<uint32_t>(dy));
        if (std::abs(ry) < std::abs(rx)) x += rx < 0 ? -1 : 1;
        else y += ry < 0 ? -1 : 1;
    }
}

int boltScreenX(const BoltCtx& c, int x) {
    int v = (x & pxMask()) - (c.camX & pxMask());
    const int h = half(pxMask());
    if (-h > v) v &= pxMask();
    if (h < v) v -= pxMask();
    return v;
}

// dword_4E99F8 (1º nível) e dword_4E99F4[nível]: cinzas-azulados em 555.
const uint16_t kBoltColor[12] = {0, 0x7BDE, 0x6B5E, 0x631E, 0x5ADE, 0x4E7C, 0x463B,
                                 0x39DA, 0x39DA, 0x39DA, 0x39DA, 0x39DA};

void boltRecurse(BoltCtx& c, int sx, int sy, int ex, int ey, int n, uint8_t angle) {
    ++c.depth;
    int cx = sx, cy = sy;
    int k = (n - 1) * 128;                                        // var_24
    for (int i = 0; i < n; ++i, k -= 128) {
        bool branch = false;
        int r = 0;                                                // var_28
        int e = k / n;
        int dx = ex - cx, dy = ey - cy;
        if (half(pxW()) < std::abs(dx)) dx = dx > 0 ? dx - pxW() : dx + pxW();
        uint8_t dir = dirOf(dx, dy);
        const int rem = n - i;
        if (i == 0) {
            dir = static_cast<uint8_t>(dir + angle);
        } else if (rem != 1) {
            if (e > 16 && i % 3 == 2) {
                e >>= 2;
                r = c.rng.getRand(e);
                branch = true;
            } else {
                const int h = e / 2, q = e / 4;
                do { r = c.rng.getRand(e) - h; } while (std::abs(r) < q);
            }
            dir = static_cast<uint8_t>(dir - static_cast<uint8_t>(r));
        }
        int len = static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
        if (rem != 1) len = ((c.rng.getRand(0x38) + 100) * len) / 128;
        const int step = len / rem;
        const int nx = cx + (kCos[dir] * step >> 6), ny = cy + (kSin[dir] * step >> 6);
        const int ax = boltScreenX(c, cx), ay = cy - c.camY;
        const int bx = boltScreenX(c, nx), by = ny - c.camY;
        boltLine(c, ax, ay, bx, by, kBoltColor[c.depth < 12 ? c.depth : 11], c.depth == 1);
        if (branch) {
            const uint8_t a1 = static_cast<uint8_t>(c.rng.getRand(0x100));
            const int r2 = c.rng.getRand(0x100);
            const int tx = c.endX + (kCos[a1] * r2 >> 9), ty = c.endY + (kSin[a1] * r2 >> 9);
            boltRecurse(c, cx, cy, tx, ty, rem, static_cast<uint8_t>(r));
        }
        cx = nx; cy = ny;
    }
    --c.depth;
}
} // namespace

void weaponDrawBolt(const WeaponDrawOp& op, int camX, int camY, int vw, int vh,
                    const std::function<void(int, int, uint16_t)>& put) {
    if (!g_gameMap.ok() || op.segs <= 0) return;
    BoltCtx c;
    c.rng.setSeed(op.seed);
    c.endX = op.x2; c.endY = op.y2;
    c.camX = camX; c.camY = camY;
    c.r = vw; c.b = vh;
    c.put = &put;
    boltRecurse(c, op.x, op.y, op.x2, op.y2, op.segs, static_cast<uint8_t>(op.phase));
}

bool tankTurret(const UnitRecord& r, int bodyAnimReal, int bodyFrame,
                int& turretAnimReal, int& offX, int& offY) {
    if (!isTankChassis(r.type)) return false;
    const TurretDesc* td = turretOf(static_cast<int16_t>(r.cargo), r.type);
    if (!td) return false;
    turretAnimReal = turretAnimRaw(*td, r, r.turretDir) - kAnimBias;
    const int8_t* o = frameOffsets(bodyAnimReal, bodyFrame);
    offX = o[0];
    offY = o[1];
    return true;
}

} // namespace op2
