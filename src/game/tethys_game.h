#pragma once

#include <cstdint>

#include "game_rng.h"
#include "unit.h"
#include "location.h"

namespace op2 {

// Núcleo da simulação — porta TethysGame (asm @193905+), que no original é uma
// classe de métodos static sobre globais. Ver [[op2-engine-architecture]].
//   Tick()  = dword_56DEDC            (contador de ticks de simulação)
//   Time()  = dword_56DEDC >> 2       (4 ticks por "marca")
//   NoPlayers()/LocalPlayer()         = dword_56DEC0 / dword_56DEE8
//   GetRand/SetSeed                   = RNG determinístico (unk_56B1E0)
class TethysGame {
public:
    static void reset(uint32_t seed, int numPlayers, int localPlayer);

    static int tick()        { return static_cast<int>(s_tick); }
    static int time()        { return static_cast<int>(s_tick >> 2); }
    static int noPlayers()   { return s_numPlayers; }

    // As duas taxas que a tela "Network Statistics" mostra: quantos TIQUES de
    // simulacao e quantos QUADROS saem por segundo de verdade. O original as
    // mede com QueryPerformanceCounter; aqui a janela e de um segundo.
    static void noteFrame();
    static void noteTick();
    static void rates(float& gameSpeed, float& updateRate);
    static int localPlayer() { return s_localPlayer; }
    // dword_56DEE8: em rede quem define e o lobby (sub_4893A0 acha o indice
    // comparando os ids dos jogadores com o da conexao local).
    static void setLocalPlayer(int p) { s_localPlayer = p; }

    // Avança um tick de simulação (o jogo incrementa dword_56DEDC por passo).
    static void advanceTick() { ++s_tick; }

    // ---- Dia/noite (turno sub_489DD0) ---------------------------------------
    // Fase de 16 bits (dword_5472A0) avança 4 por turno se a luz se move
    // ([obj+0x48] = dword_56DEA0); posição da faixa de luz em colunas INTERNAS
    // do mapa: dword_54729C = larguraInterna * fase >> 16. "Luz em toda parte"
    // (dword_56DEA4) desliga o escurecimento. As missões ligam/desligam via
    // SetDaylightMoves / SetDaylightEverywhere (CommandPacket 0x2B, sub-ids
    // 0x12/0x13 → sub_4229F0).
    static void advanceDaylight(int internalWidth) {
        if (s_daylightMoves) {
            s_daylightPhase = (s_daylightPhase + 4) & 0xFFFF;
            s_daylightPos = (internalWidth * s_daylightPhase) >> 16;
        }
    }
    static void SetDaylightMoves(int on)      { s_daylightMoves = on; }
    // --harness `mem`: a escrita direta na posicao da faixa (dword_54729C) e na fase
    // (dword_5472A0), como o roteiro do harness faz no original.
    static void setDaylightPosRaw(int pos)     { s_daylightPos = pos; }
    static void setDaylightPhaseRaw(int phase) { s_daylightPhase = phase & 0xFFFF; }
    // GameMap::SetInitialLightLevel → sub_41F9A0(0, fase, 0): fase de 16 bits
    // e posição = larguraInterna * fase >> 16.
    static void setDaylightPhase(int phase, int internalWidth) {
        s_daylightPhase = phase & 0xFFFF;
        s_daylightPos = (internalWidth * s_daylightPhase) >> 16;
    }
    // Opções de jogo (CommandPacket 0x2B, sub-id i → dword_56DE58[i]): 0 recursos
    // ilimitados, 1 produz tudo, 4 unidades rápidas, 5 produção rápida; 14
    // (dword_56DE90, comando 40172) e 15 (dword_56DE94) desligam / ligam o
    // refinamento de rota dos jogadores (sub_471F30).
    static int  cheatFlag(int i)             { return (i >= 0 && i < 16) ? s_cheat[i] : 0; }
    static void setCheatFlag(int i, int v)   { if (i >= 0 && i < 16) s_cheat[i] = v; }
    // Moral forçada (CommandPacket 0x2B → colony.cpp). player -1 = todos.
    static void ForceMoraleGreat(int player);
    static void ForceMoraleGood(int player);
    static void ForceMoraleOK(int player);
    static void ForceMoralePoor(int player);
    static void ForceMoraleRotten(int player);
    static void FreeMoraleLevel(int player);
    static void SetDaylightEverywhere(int on) { s_daylightEverywhere = on; }
    // Flags do GameStartInfo (dword_56DEFC, cópia do início da partida): bit0
    // desastres, bit1 dia/noite, bit2 moral FIXA, bits 17..20 unidades iniciais.
    static uint32_t startFlags()            { return s_startFlags; }
    static void     setStartFlags(uint32_t f) { s_startFlags = f; }
    static int  daylightPos()        { return s_daylightPos; }
    // A fase e o campo [+0x48] do objeto do globo (dword_5472A0): o
    // terminador da esfera acompanha o dia porque e a MESMA variavel.
    static int  daylightPhase()      { return s_daylightPhase; }
    static int  daylightEverywhere() { return s_daylightEverywhere; }
    static int  daylightMoves()      { return s_daylightMoves; }

    static void setSeed(uint32_t seed) { s_rng.setSeed(seed); }
    static int  getRand(int n)         { return s_rng.getRand(n); }
    static GameRNG& rng()              { return s_rng; }

    // CreateUnit (@195141): cria uma unidade e devolve o handle em `unit`.
    static int CreateUnit(Unit& unit, map_id type, LOCATION loc, int owner,
                          map_id cargo = kMapIdNone, int rotation = 0) {
        return createUnit(unit, type, loc, owner, cargo, rotation) ? 1 : 0;
    }

    static void serialize(class SaveStream& s);
    static uint64_t getRandState() { return (static_cast<uint64_t>(s_rng.s0()) << 32) | s_rng.s1(); }   // jogo salvo (TethysGame, 0x484 bytes no original)

private:
    static uint32_t s_tick;        // dword_56DEDC
    static int      s_daylightPhase;      // dword_5472A0
    static int      s_daylightPos;        // dword_54729C
    static int      s_daylightMoves;      // dword_56DEA0 ([obj+0x48])
    static int      s_daylightEverywhere; // dword_56DEA4 ([obj+0x4C])
    static int      s_numPlayers;  // dword_56DEC0
    static int      s_localPlayer; // dword_56DEE8
    static GameRNG  s_rng;         // unk_56B1E0
    static int      s_cheat[16];    // dword_56DE58..
    static uint32_t s_startFlags;  // dword_56DEFC
};

} // namespace op2
