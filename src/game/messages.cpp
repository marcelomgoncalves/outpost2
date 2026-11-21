#include "messages.h"
#include "../audio/sound.h"

#include <SDL.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "game_map.h"
#include "tethys_game.h"
#include "save_game.h"
#include "save_stream.h"

namespace op2 {

static GameMessage s_ring[64];
static int s_start = 0, s_count = 0;            // [obj+0] início, [obj+4] quantidade
static int s_serial = 0;
static std::string s_status;

// Estado do filtro sub_439490 (campos +0x130C..+0x13FC do objeto de log).
struct Recent { int x = 0, y = 0, id = 0; uint32_t time = 0; };
static Recent s_recent[0x32];                   // +0x1330, 50 x 16 bytes
static int s_lastPowerId = 0, s_lastMoraleId = 0, s_lastFoodId = 0;
static uint32_t s_lastPowerT = 0, s_lastMoraleT = 0, s_lastFoodT = 0;
static uint32_t s_lastCommonT = 0, s_lastRareT = 0, s_lastDisabledT = 0;

void messagesSerialize(SaveStream& s) {
    for (GameMessage& m : s_ring) { s.io(m.x); s.io(m.y); s.io(m.soundId); s.ioStr(m.text); }
    s.io(s_start); s.io(s_count); s.io(s_serial);
    s.ioStr(s_status);
    s.io(s_recent);
    s.io(s_lastPowerId); s.io(s_lastMoraleId); s.io(s_lastFoodId);
    s.io(s_lastPowerT); s.io(s_lastMoraleT); s.io(s_lastFoodT);
    s.io(s_lastCommonT); s.io(s_lastRareT); s.io(s_lastDisabledT);
}

void resetMessages() {
    s_start = s_count = 0;
    s_serial = 0;
    s_status.clear();
    for (auto& r : s_recent) r = Recent{};
    s_lastPowerId = s_lastMoraleId = s_lastFoodId = 0;
    s_lastPowerT = s_lastMoraleT = s_lastFoodT = 0;
    s_lastCommonT = s_lastRareT = s_lastDisabledT = 0;
}

// Grupo com "última mensagem": a mesma mensagem fica barrada por 2 min; outra
// do grupo passa na hora.
static bool groupAllow(int id, int& lastId, uint32_t& lastT, uint32_t now) {
    if (lastId == 0 && lastT < now) { lastT = now; lastId = id; return true; }
    if (lastId != 0 && lastId != id) { lastT = now; lastId = id; return true; }
    if (lastT + 0x1D4C0 >= now) return false;
    lastT = now; lastId = id;
    return true;
}
static bool cooldownAllow(uint32_t& lastT, uint32_t span, uint32_t now) {
    // CORRECAO: `lastT == 0` quer dizer "nunca avisou", e nunca-avisou tem de
    // passar. No original isto se resolvia sozinho porque o relogio e o
    // timeGetTime -- tempo desde o boot do Windows, sempre enorme --, entao
    // `0 + span >= now` era falso. Aqui o relogio e o SDL_GetTicks, que comeca
    // em ZERO com o processo: a conta ficava verdadeira e barrava os avisos de
    // armazenamento e de predio desligado nos DOIS PRIMEIROS MINUTOS de cada
    // partida, em silencio. (groupAllow nao tem o problema: ele ja trata o
    // "primeiro" por lastId == 0.)
    if (lastT != 0 && lastT + span >= now) return false;
    lastT = now;
    return true;
}

// sub_439490: retorna false p/ barrar a mensagem.
static bool messageAllowed(int id, int x, int y) {
    const uint32_t now = soundClock();                       // timeGetTime (o do harness, com --harness)
    switch (id) {
        case 0x60: case 0x61: case 0x67: case 0x68: case 0x6A: case 0x6B: case 0x6C: case 0x99: {
            int freeSlot = -1;
            const int mapW = g_gameMap.ok() ? g_gameMap.width() * 32 : 0;
            for (int i = 0; i < 0x32; ++i) {
                Recent& r = s_recent[i];
                // (o `now - 0x61A8` do original estoura por baixo nos
                // primeiros 25 s com um relogio que comeca em zero; somar do
                // outro lado da a mesma conta sem estourar)
                if (r.id != 0 && r.time + 0x61A8 < now) r = Recent{};   // > 25 s: esquece
                if (r.id == 0 && freeSlot < 0) freeSlot = i;
                if (r.id != id) continue;
                if (std::abs(r.y - y) >= 0x100) continue;
                int dx = x - r.x;
                if (mapW) { if (dx > mapW / 2) dx -= mapW; else if (dx < -mapW / 2) dx += mapW; }
                if (std::abs(dx) < 0x100) return false;      // mesmo aviso ali perto
            }
            if (freeSlot >= 0) s_recent[freeSlot] = Recent{x, y, id, now};
            return true;
        }
        case 0x6F: case 0x70: case 0x71: case 0x72: case 0x73:          // moral
            return groupAllow(id, s_lastMoraleId, s_lastMoraleT, now);
        case 0x74: case 0x75: case 0x76: case 0xB1:                     // comida
            return groupAllow(id, s_lastFoodId, s_lastFoodT, now);
        case 0x77: case 0x78: case 0x79:                                // energia
            return groupAllow(id, s_lastPowerId, s_lastPowerT, now);
        case 0x81: return cooldownAllow(s_lastCommonT, 0x1D4C0, now);   // armazenamento
        case 0x82: return cooldownAllow(s_lastRareT, 0x1D4C0, now);
        case 0x85: case 0xA1: return cooldownAllow(s_lastDisabledT, 0x7530, now);
        default: return true;
    }
}

static void logMessage(int x, int y, const char* text, int soundId);

void postMessage(int x, int y, const char* text, int soundId) {
    // Sem som → beep 0x5B; vozes do Savant (0x5E..0xE3) passam pelo filtro e
    // vão p/ a fila (sub_47F060); o resto toca direto (sub_47F020).
    if (soundId >= 0x5E && soundId < 0xE4) {
        if (!messageAllowed(soundId, x, y)) { soundLog("msg", soundId, x, y, 0); return; }
        soundVoice(soundId, x, y);
    } else {
        soundPlay(soundId ? soundId : 0x5B);
    }
    logMessage(x, y, text, soundId ? soundId : 0x5B);
    soundLog("msg", soundId, x, y, soundId >= 0x5E && soundId < 0xE4 ? 1 : -1);
}

// Extra deste porte: entra no log e na barra sem tocar nada (o diagnostico de
// comando recusado, command_debug.cpp).
void postMessageQuiet(int x, int y, const char* text) { logMessage(x, y, text, 0x5B); }

static void logMessage(int x, int y, const char* text, int soundId) {
    const int slot = (s_start + s_count) & 0x3F;
    if (s_count == 0x40) s_start = (s_start + 1) & 0x3F; else ++s_count;
    GameMessage& m = s_ring[slot];
    m.x = x; m.y = y; m.soundId = soundId;
    char buf[0x40];
    std::snprintf(buf, sizeof(buf), "<N>%d: %s", TethysGame::tick() / 100, text);
    m.text = buf;
    ++s_serial;
    statusBarSet(text);
}

// sub_49BF10: ate 80 caracteres, em MAIUSCULAS.
void statusBarSet(const char* text) {
    s_status.assign(text ? text : "", 0, 0x50);
    for (char& c : s_status) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

int messageCount() { return s_count; }
int messageSerial() { return s_serial; }
const GameMessage& messageAt(int i) { return s_ring[(s_start + i) & 0x3F]; }
const std::string& statusBarText() { return s_status; }

} // namespace op2
