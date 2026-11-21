#include "music.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../core/config.h"
#include "sound.h"

namespace op2 {

namespace {

// off_4E26F8: nomes das faixas no CLM, na ordem do enum SongIds.
const char* const kSongNames[26] = {
    "EDEN11", "EDEN21", "EDEN22", "EDEN31", "EDEN32", "EDEN33", "EP41", "EP42", "EP43",
    "EP51", "EP52", "EP61", "EP62", "EP63", "PLYMTH11", "PLYMTH12", "PLYMTH21", "PLYMTH22",
    "PLYMTH31", "PLYMTH32", "PLYMTH33", "STATIC01", "STATIC02", "STATIC03", "STATIC04", "STATIC05",
};
// dword_4D4AD0: atenuação DirectSound (1/100 dB) por "MusicVol".
const int kVolume[6] = {0, -500, -900, -1200, -1400, -1500};
constexpr int kHalf = 0x8000;                       // bytes por metade do buffer
constexpr int kHalfSamples = kHalf / 2;             // 16 bits mono

struct Entry { char name[9]; uint32_t offset, length; };

struct State {
    std::string clmPath = "op2.clm";
    FILE* file = nullptr;                           // [+0x20]
    std::vector<Entry> entries;                     // [+0x1C]
    int track[26];                                  // [+0x28] índice da entrada por SongId
    int volume = 0;                                 // [+0x10] "MusicVol"
    bool enabled = true;                            // [+0x18] "Music"
    int pauseCount = 0;                             // [+0x14]
    bool running = false;                           // timer ativo ([+4])
    bool bufferPlaying = false;                     // dword_564758
    std::vector<int> list;                          // [+0xCC] (n = [+0xC4])
    int repeatStart = 0;                            // [+0xC8]
    int index = 0;                                  // [+0xD0]
    int started = 0;                                // [+0xD4]
    int current = 0;                                // dword_564828: entrada em curso
    int pos = 0;                                    // [+0xDC] / dword_56482C
    // Saída: blocos de 0x8000 bytes na ordem em que tocam.
    std::vector<int16_t> block;
    size_t blockPos = 0;
    bool silentHalf = false;                        // 2ª metade da 1ª volta (nunca escrita)
    double frac = 0.0;                              // reamostragem p/ a taxa do dispositivo
} s;

void closeClm() {
    if (s.file) std::fclose(s.file);
    s.file = nullptr;
    s.entries.clear();
}

bool openClm() {                                    // sub_450220
    closeClm();
    FILE* f = std::fopen(s.clmPath.c_str(), "rb");
    if (!f) return false;
    uint8_t h[0x4C];
    auto fail = [&]() { std::fclose(f); return false; };
    if (std::fread(h, 1, sizeof(h), f) != sizeof(h)) return fail();
    if (_strnicmp(reinterpret_cast<const char*>(h), "OP2 Clump File Version 1.0\x1A", 0x20) != 0) return fail();
    auto u16 = [&](int o) { return static_cast<int>(h[o] | (h[o + 1] << 8)); };
    auto i32 = [&](int o) { int v; std::memcpy(&v, h + o, 4); return v; };
    const int unk = i32(0x34);
    if ((((unk < 0 ? -unk : unk) & 0x7FFF)) != 0) return fail();
    if (u16(0x20) != 1 || u16(0x22) != 1 || u16(0x2C) != 2 || i32(0x24) != 0x5622) return fail();
    const int n = i32(0x38);
    if (n < 0 || n > 4096) return fail();
    // A tabela começa em +0x3C (dentro do cabeçalho de 0x4C já lido).
    std::vector<uint8_t> all(static_cast<size_t>(n) * 16 + 0x3C);
    std::memcpy(all.data(), h, std::min<size_t>(sizeof(h), all.size()));
    if (all.size() > sizeof(h) &&
        std::fread(all.data() + sizeof(h), 1, all.size() - sizeof(h), f) != all.size() - sizeof(h)) return fail();
    for (int i = 0; i < n; ++i) {
        Entry e{};
        std::memcpy(e.name, all.data() + 0x3C + i * 16, 8);
        e.name[8] = 0;
        std::memcpy(&e.offset, all.data() + 0x3C + i * 16 + 8, 4);
        std::memcpy(&e.length, all.data() + 0x3C + i * 16 + 12, 4);
        s.entries.push_back(e);
    }
    s.file = f;
    // sub_450450: cada SongId pelo nome (8 letras, sem caixa); falta uma → falha.
    for (int k = 0; k < 26; ++k) {
        s.track[k] = -1;
        for (int i = 0; i < n; ++i)
            if (_strnicmp(kSongNames[k], s.entries[i].name, 8) == 0) { s.track[k] = i; break; }
        if (s.track[k] < 0) { closeClm(); return false; }
    }
    return true;
}

int entryOf(int listIdx) {
    if (listIdx < 0 || listIdx >= static_cast<int>(s.list.size())) return 0;
    const int id = s.list[listIdx];
    return (id >= 0 && id < 26) ? s.track[id] : 0;
}

// Lê 0x8000 bytes da entrada atual a partir de pos (zeros se falhar).
void readBlock() {
    s.block.assign(kHalfSamples, 0);
    s.blockPos = 0;
    if (!s.file || s.current < 0 || s.current >= static_cast<int>(s.entries.size())) return;
    const Entry& e = s.entries[s.current];
    if (std::fseek(s.file, static_cast<long>(e.offset) + s.pos, SEEK_SET) != 0) return;
    const size_t got = std::fread(s.block.data(), 1, kHalf, s.file);
    if (got != static_cast<size_t>(kHalf)) s.block.assign(kHalfSamples, 0);   // ReadFile falhou → zera
}

// Próxima metade a tocar (fptc).
void nextBlock() {
    if (!s.bufferPlaying) {                         // loc_450D38: 1ª carga
        s.current = entryOf(s.index);
        s.started = 1;
        readBlock();
        s.bufferPlaying = true;
        s.silentHalf = true;                        // a 2ª metade ainda não foi escrita
        return;
    }
    if (s.silentHalf) {                             // toca a 2ª metade vazia
        s.silentHalf = false;
        s.block.assign(kHalfSamples, 0);
        s.blockPos = 0;
        return;
    }
    s.pos += kHalf;                                 // loc_450CA0
    const int len = (s.current >= 0 && s.current < static_cast<int>(s.entries.size()))
                        ? static_cast<int>(s.entries[s.current].length) : 0;
    if (len <= s.pos) {
        if (s.started) {
            s.pos = 0;
            if (++s.index >= static_cast<int>(s.list.size())) s.index = s.repeatStart;
        } else {
            s.started = 1;                          // lista trocada: começa do índice 0
        }
        s.current = entryOf(s.index);
    }
    readBlock();
}

void startTimer() { s.running = true; s.bufferPlaying = false; }

} // namespace

void musicDebugState(std::vector<int>& list, int& repeatStart, int& index) {
    list = s.list; repeatStart = s.repeatStart; index = s.index;
}

void musicSetClmPath(const char* path) { s.clmPath = path ? path : "op2.clm"; }

void musicStartGame(int missionType, bool loadedGame) {      // sub_4507B0
    struct Lock { Lock() { soundLockAudio(true); } ~Lock() { soundLockAudio(false); } } lock;
    Config cfg("OUTPOST2.INI");
    cfg.load();
    s.volume = cfg.getInt("Game", "MusicVol", 0);
    s.enabled = cfg.getInt("Game", "Music", 1) != 0;
    if (s.list.empty()) {
        const bool multi = missionType >= -8 && missionType <= -4;   // sub_4027D0
        if (!multi) { s.repeatStart = 4; s.list = {0x15, 0x0F, 2, 6, 7, 0x0B, 1, 0x0F, 1}; }
        else        { s.repeatStart = 5; s.list = {0x18, 0x0F, 1, 2, 6, 8, 0x0B, 0, 6}; }
    }
    if (!s.enabled) return;
    if (!openClm()) return;
    s.index = loadedGame ? s.repeatStart : 0;
    s.started = 0;
    s.pos = 0;
    if (s.pauseCount == 0) startTimer();
}

void musicEndGame() {                                         // sub_4506C0
    soundLockAudio(true);
    s.running = false;
    s.bufferPlaying = false;
    s.pos = 0;
    closeClm();
    s.list.clear();
    soundLockAudio(false);
}

void musicSetPlayList(int count, int repeatStart, const int* ids) {   // sub_451090
    struct Lock { Lock() { soundLockAudio(true); } ~Lock() { soundLockAudio(false); } } lock;
    const bool wasEmpty = s.list.empty();
    s.list.assign(ids, ids + (count > 0 ? count : 0));
    s.repeatStart = repeatStart;
    s.index = 0;
    s.started = 0;
    if (wasEmpty && s.enabled && openClm() && s.pauseCount == 0) {
        s.pos = 0;
        startTimer();
    }
}

void musicSetVolume(int level) {                              // sub_451050
    s.volume = level;
    Config cfg("OUTPOST2.INI");
    cfg.load();
    cfg.setInt("Game", "MusicVol", level);
    cfg.save();
}

void musicSetEnabled(bool on) {                               // sub_450E90
    if (s.enabled == on) return;
    s.enabled = on;
    Config cfg("OUTPOST2.INI");
    cfg.load();
    cfg.setInt("Game", "Music", on ? 1 : 0);
    cfg.save();
    if (!on) { s.running = false; closeClm(); return; }
    if (!s.list.empty() && openClm() && s.pauseCount == 0) startTimer();
}

void musicPause() {                                           // sub_450A90
    if (s.pauseCount++ == 0 && s.running) s.running = false;
}

void musicResume() {                                          // sub_450B00
    if (--s.pauseCount == 0 && s.file) startTimer();
}

bool musicPlaying() { return s.running && s.file != nullptr; }

int musicDebugPull(int16_t* out, int frames) {
    int n = 0;
    while (n < frames && s.running && s.file) {
        if (s.blockPos >= s.block.size()) nextBlock();
        out[n++] = s.block[s.blockPos++];
    }
    return n;
}

void musicMix(float* acc, int frames, int freq) {
    if (!s.running || !s.file || freq <= 0) return;
    const int v = s.volume >= 0 && s.volume < 6 ? kVolume[s.volume] : kVolume[5];
    const float g = std::pow(10.f, static_cast<float>(v) / 2000.f);
    const double step = 22050.0 / freq;
    for (int f = 0; f < frames; ++f) {
        if (s.blockPos >= s.block.size()) nextBlock();
        const float x = s.block.empty() ? 0.f : s.block[s.blockPos] * g;
        acc[f * 2] += x;
        acc[f * 2 + 1] += x;
        s.frac += step;
        while (s.frac >= 1.0) { s.frac -= 1.0; ++s.blockPos; if (s.blockPos >= s.block.size()) break; }
    }
}

} // namespace op2
