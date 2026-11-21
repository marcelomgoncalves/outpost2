#include "sound.h"

#include <SDL.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../core/config.h"
#include "music.h"
#include "../game/game_map.h"
#include "../res/res_manager.h"

namespace op2 {

namespace {

// off_4E0160: nome do WAV por id (lido do OUTPOST2.EXE).
const char* const kSoundNames[252] = {
    "bld_fal3.wav","bld_fal1.wav","bld_fal2.wav","bld_make.wav","ag_sel.wav","tokamsel.wav","pantheon.wav","commsel.wav",
    "dirt.wav","fac_andr.wav","fac_cons.wav","fac_sel.wav","fac_sel.wav","gar_sel.wav","geo_sel.wav","gorf.wav",
    "guardsel.wav","lab_3.wav","lab_1.wav","lab_2.wav","light.wav","mag_sel.wav","medisel.wav","dirt.wav",
    "mine_1.wav","mine_2.wav","nur_sel.wav","observat.wav","rec_fac.wav","res1_sel.wav","res2_sel.wav","res3_sel.wav",
    "robosel.wav","smelter1.wav","smelter1.wav","tokamsel.wav","spaceprt.wav","storage1.wav","storage2.wav","tokamsel.wav",
    "pantheon.wav","uni_sel.wav","consvhc.wav","dozr_sel.wav","evac_trn.wav","earthwrk.wav","robot4.wav","spidsel.wav",
    "infantry.wav","repvhc.wav","robmine.wav","robsurv.wav","scout.wav","combhev.wav","comblit.wav","combmed.wav",
    "truk_sel.wav","bld_exp.wav","uni_grad.wav","tokaexp.wav","geyser.wav","gar_doc.wav","doc_dor1.wav","doc_dor2.wav",
    "doc_grab.wav","sulv.wav","robdep.wav","reprep.wav","consrep.wav","dump.wav","micwarn.wav","micfire.wav",
    "bigexp1.wav","bigexp2.wav","bigexp3.wav","medexp1.wav","medexp2.wav","smalxp1.wav","smalxp2.wav","smalxp3.wav",
    "quake.wav","tornado2.wav","thunder.wav","microbe.wav","fuse.wav","beep2.wav","beep2.wav","beep5.wav",
    "bld_not.wav","beep6.wav","beep9.wav","beep8.wav","beep5.wav","message2.wav","savnt212.wav","savant2.wav",
    "savant3.wav","savant4.wav","savant6.wav","savant7.wav","savant8.wav","savant9.wav","savant10.wav","savant11.wav",
    "savnt206.wav","savant13.wav","savant14.wav","savant15.wav","savnt204.wav","savant17.wav","savant18.wav","savant19.wav",
    "savant20.wav","savant21.wav","savant22.wav","savant23.wav","savant24.wav","savant25.wav","savant26.wav","savant27.wav",
    "savant28.wav","savant29.wav","savant30.wav","savant32.wav","savant33.wav","savant46.wav","savant47.wav","savant48.wav",
    "savant49.wav","savnt221.wav","savant51.wav","savant53.wav","savant58.wav","savnt202.wav","savnt227.wav","savant65.wav",
    "savant66.wav","savant67.wav","savant68.wav","savant69.wav","savant70.wav","savant71.wav","savant72.wav","savant73.wav",
    "savant74.wav","savant75.wav","savant76.wav","savant77.wav","savant78.wav","savant79.wav","savant80.wav","savant93.wav",
    "savnt278.wav","savnt207.wav","savant96.wav","savant97.wav","savant98.wav","savant99.wav","savnt100.wav","savnt101.wav",
    "savnt201.wav","savnt203.wav","savnt205.wav","savnt208.wav","savnt209.wav","savnt210.wav","savnt211.wav","savnt213.wav",
    "savnt214.wav","savnt215.wav","savnt216.wav","savnt218.wav","savnt219.wav","savnt220.wav","savnt223.wav","savnt224.wav",
    "savnt225.wav","savnt226.wav","savnt228.wav","savnt229.wav","savnt230.wav","savnt231.wav","savnt232.wav","savnt233.wav",
    "savnt234.wav","savnt235.wav","savnt236.wav","savnt237.wav","savnt238.wav","savnt239.wav","savnt240.wav","savnt241.wav",
    "savnt242.wav","savnt243.wav","savnt244.wav","savnt245.wav","savnt246.wav","savnt247.wav","savnt248.wav","savnt249.wav",
    "savnt250.wav","savnt251.wav","savnt252.wav","savnt253.wav","savnt254.wav","savnt255.wav","savnt256.wav","savnt257.wav",
    "savnt258.wav","savnt259.wav","savnt260.wav","savnt261.wav","savnt262.wav","savnt263.wav","savnt264.wav","savnt265.wav",
    "savnt266.wav","savnt267.wav","savnt268.wav","savnt269.wav","savnt270.wav","savnt271.wav","savnt272.wav","savnt273.wav",
    "savnt274.wav","savnt275.wav","savnt276.wav","savnt277.wav","mis_fire.wav","acid2.wav","mis_fire.wav","geo_sel.wav",
    "laser.wav","medexp1.wav","microwve.wav","medexp1.wav","railfir.wav","railtar.wav","bigexp1.wav","bigexp2.wav",
    "mis_fire.wav","spam1.wav","mis_fire.wav","foam.wav","thor.wav","medexp1.wav","mis_fire.wav","medexp2.wav",
    "laser.wav","smalxp1.wav","gun1.wav","medexp1.wav",
};

struct Sample {                                          // dword_56CFC0 + 12*id
    std::vector<int16_t> pcm;                            // mono 16 bits na taxa do dispositivo
    uint32_t ms = 0;                                     // [+0]: bytes*1000*8/176400 (sub_47F350)
};
struct Channel {
    const Sample* s = nullptr;
    size_t pos = 0;
    float gl = 1.f, gr = 1.f;
    bool positional = false;
    int x = -1, y = -1;                                  // [+0x14]/[+0x18]
};
struct Voice { int id, x, y; };

SDL_AudioDeviceID s_dev = 0;
SDL_AudioSpec s_spec{};
Sample s_samples[252];
Channel s_ch[63];
bool s_enabled = false;
int s_soundVol = 0;                                      // [+0x914] "SoundVol"
// Volume mestre em 0..100 ("AudioVolume", so deste porte). O callback do mixer
// o le fora da trava, por isso atomico.
std::atomic<int> s_master{50};
int s_lx = 0, s_ly = 0;                                  // centro da vista (pixels)
std::vector<Voice> s_queue;                              // +0x924 (8)
uint32_t s_nextVoice = 0;                                // +0x9A4

void mix(void*, Uint8* stream, int len) {
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    const int frames = len / 4;
    std::vector<float> acc(static_cast<size_t>(frames) * 2, 0.f);
    for (Channel& c : s_ch) {
        if (!c.s) continue;
        const size_t n = c.s->pcm.size();
        for (int f = 0; f < frames && c.pos < n; ++f, ++c.pos) {
            const float v = c.s->pcm[c.pos];
            acc[f * 2] += v * c.gl;
            acc[f * 2 + 1] += v * c.gr;
        }
        if (c.pos >= n) c.s = nullptr;
    }
    musicMix(acc.data(), frames, s_spec.freq);              // dword_564750 (buffer próprio no original)
    // O volume mestre vem por ultimo, sobre a mistura inteira (ver sound.h).
    const float master = static_cast<float>(s_master.load(std::memory_order_relaxed)) / 100.f;
    for (int i = 0; i < frames * 2; ++i) {
        float v = acc[i] * master;
        if (v > 32767.f) v = 32767.f;
        if (v < -32768.f) v = -32768.f;
        out[i] = static_cast<int16_t>(v);
    }
}

// Converte o WAV (22050 Hz, 8 bits, mono no OP2) p/ mono 16 bits na taxa do dispositivo.
bool decode(const void* data, size_t size, Sample& s) {
    SDL_AudioSpec spec;
    Uint8* buf = nullptr; Uint32 blen = 0;
    if (!SDL_LoadWAV_RW(SDL_RWFromConstMem(data, static_cast<int>(size)), 1, &spec, &buf, &blen)) return false;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, s_spec.freq) < 0) {
        SDL_FreeWAV(buf); return false;
    }
    std::vector<Uint8> tmp(static_cast<size_t>(blen) * (cvt.len_mult > 0 ? cvt.len_mult : 1));
    std::memcpy(tmp.data(), buf, blen);
    SDL_FreeWAV(buf);
    cvt.buf = tmp.data(); cvt.len = static_cast<int>(blen);
    s.ms = static_cast<uint32_t>(static_cast<uint64_t>(blen) * 1000 * 8 / 0x2B110 /
                                 (SDL_AUDIO_BITSIZE(spec.format) / 8) / spec.channels);
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) return false;
    const int bytes = cvt.needed ? cvt.len_cvt : static_cast<int>(blen);
    s.pcm.resize(static_cast<size_t>(bytes) / 2);
    std::memcpy(s.pcm.data(), tmp.data(), s.pcm.size() * 2);
    return true;
}

// Volume DirectSound (1/100 dB) e pan (−10000..10000) → ganhos L/R.
void gains(int vol, int pan, float& gl, float& gr) {
    const float g = std::pow(10.f, static_cast<float>(vol) / 2000.f);
    gl = gr = g;
    if (pan > 0) gl *= std::pow(10.f, -static_cast<float>(pan) / 2000.f);
    if (pan < 0) gr *= std::pow(10.f, static_cast<float>(pan) / 2000.f);
}

// sub_47E760: canal livre; um som global pode roubar um posicional.
int allocChannel(bool global) {
    for (int i = 0; i < 63; ++i) if (!s_ch[i].s) return i;
    if (!global) return -1;
    for (int i = 0; i < 63; ++i) if (s_ch[i].positional) return i;
    return -1;
}

// Volume/pan de um som em (x,y) pixels — sub_47EE00 e o recálculo de sub_47EA60.
// false = longe demais p/ começar (≥ 32 tiles ou −4000).
bool positionalGains(int x, int y, float& gl, float& gr) {
    int dx = x - s_lx;
    const int W = g_gameMap.ok() ? g_gameMap.width() * 32 : 0;
    if (W) { if (dx < -W / 2) dx += W; else if (dx > W / 2) dx -= W; }
    const int tx = dx >> 5, ax = tx < 0 ? -tx : tx;
    int ay = y - s_ly; if (ay < 0) ay = -ay; ay >>= 5;
    const int dist = (ax > ay ? ax : ay) + (ax < ay ? ax : ay) / 2;
    int vol = dist >= 0x20 ? -4000 : -(s_soundVol * 250) - dist * 125;
    if (vol < -4000) vol = -4000;
    gains(vol, tx * 125, gl, gr);
    return dist < 0x20 && vol > -4000;
}

int startSound(int id, int x, int y, bool positional) {     // sub_47EE00
    if (!s_enabled || id < 0 || id >= 252 || s_samples[id].pcm.empty()) return -1;
    float gl, gr;
    if (positional) { if (!positionalGains(x, y, gl, gr)) return -1; }
    else gains(-(s_soundVol * 250), 0, gl, gr);
    SDL_LockAudioDevice(s_dev);
    const int c = allocChannel(!positional);
    if (c >= 0) {
        Channel& ch = s_ch[c];
        ch.s = &s_samples[id]; ch.pos = 0; ch.positional = positional;
        ch.x = x; ch.y = y; ch.gl = gl; ch.gr = gr;
    }
    SDL_UnlockAudioDevice(s_dev);
    return c;
}

} // namespace

bool soundExitBeep() {
    static bool feito = false;                            // dword_4E0BB8
    if (feito) return false;
    feito = true;
    if (soundMasterVolume() <= 0) return false;
    if (startSound(0x5C, 0, 0, false) < 0) return false;  // sub_47F020(0x5C)
    SDL_Delay(3500);                                      // Sleep(0DACh)
    return true;
}

bool soundInit() {                                        // sub_47E020
    Config cfg("OUTPOST2.INI");
    if (cfg.load()) {
        s_soundVol = cfg.getInt("Game", "SoundVol", 0);
        soundSetMasterVolume(cfg.getInt("Game", "AudioVolume", 50));
        if (!cfg.getInt("Game", "Sound", 1)) return false;
    }
    SDL_AudioSpec want{};
    want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = mix;
    s_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &s_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!s_dev) { std::cerr << "[Sound] sem dispositivo: " << SDL_GetError() << std::endl; return false; }
    int ok = 0;
    for (int i = 0; i < 252; ++i) {
        auto st = g_ResManager.openStream(kSoundNames[i]);
        if (st && decode(st->data(), st->size(), s_samples[i])) ++ok;
    }
    std::cout << "[Sound] " << ok << "/252 sons, " << s_spec.freq << " Hz" << std::endl;
    s_enabled = ok > 0;
    SDL_PauseAudioDevice(s_dev, 0);
    return s_enabled;
}

void soundSetMasterVolume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_master.store(percent, std::memory_order_relaxed);
}

int soundMasterVolume() { return s_master.load(std::memory_order_relaxed); }

void soundStopAll() {                                     // sub_47E0C0
    if (!s_dev) return;
    SDL_LockAudioDevice(s_dev);
    for (Channel& c : s_ch) { c.s = nullptr; c.positional = false; }
    SDL_UnlockAudioDevice(s_dev);
    s_queue.clear();
    s_nextVoice = soundClock();
}

// sub_47E6F0 (subir) e sub_47E750 (descer), o contador [+0x91C].
static int s_suspend = 0;

void soundSuspend(bool on) {
    if (!on) { if (s_suspend > 0) --s_suspend; return; }
    if (s_suspend++ != 0) return;                 // so a subida de 0 para 1 age
    if (!s_dev) return;
    SDL_LockAudioDevice(s_dev);
    for (Channel& c : s_ch) { c.s = nullptr; c.positional = false; }
    SDL_UnlockAudioDevice(s_dev);
}

int soundSuspendCount() { return s_suspend; }

int soundActiveChannels() {
    int n = 0;
    for (const Channel& c : s_ch) if (c.s) ++n;
    return n;
}

void soundLockAudio(bool lock) {
    if (!s_dev) return;
    if (lock) SDL_LockAudioDevice(s_dev); else SDL_UnlockAudioDevice(s_dev);
}

void soundShutdown() {
    if (s_dev) SDL_CloseAudioDevice(s_dev);
    s_dev = 0; s_enabled = false;
}

// sub_47EA60 (a cada 4 ticks): listener = centro da vista; os canais
// posicionais em andamento têm volume/pan recalculados.
void soundSetListener(int cx, int cy) {
    if (cx == s_lx && cy == s_ly) return;
    s_lx = cx; s_ly = cy;
    if (!s_enabled) return;
    SDL_LockAudioDevice(s_dev);
    for (Channel& c : s_ch)
        if (c.s && c.positional) positionalGains(c.x, c.y, c.gl, c.gr);
    SDL_UnlockAudioDevice(s_dev);
}
// --harness (ver sound.h).
static uint32_t (*s_clock)() = nullptr;
static SoundLogFn s_log = nullptr;
void soundSetHarness(uint32_t (*clock)(), SoundLogFn log) { s_clock = clock; s_log = log; }
uint32_t soundClock() { return s_clock ? s_clock() : SDL_GetTicks(); }
void soundLog(const char* ev, int id, int x, int y, int pass) { if (s_log) s_log(ev, id, x, y, pass); }
const char* soundName(int id) { return id > 0 && id < 252 ? kSoundNames[id] : (id == 0 ? "" : "?"); }
int soundLengthMs(int id) {
    if (id < 0 || id >= 252) return 0;
    if (!s_samples[id].pcm.empty() || s_samples[id].ms) return static_cast<int>(s_samples[id].ms);
    // Sem dispositivo: so o cabecalho, com a mesma conta do decode (sub_47F300).
    static int cache[252];
    static bool done[252];
    if (!done[id]) {
        done[id] = true;
        if (auto st = g_ResManager.openStream(kSoundNames[id])) {
            SDL_AudioSpec spec;
            Uint8* buf = nullptr; Uint32 blen = 0;
            if (SDL_LoadWAV_RW(SDL_RWFromConstMem(st->data(), static_cast<int>(st->size())), 1, &spec, &buf, &blen)) {
                cache[id] = static_cast<int>(static_cast<uint64_t>(blen) * 1000 * 8 / 0x2B110 /
                                             (SDL_AUDIO_BITSIZE(spec.format) / 8) / spec.channels);
                SDL_FreeWAV(buf);
            }
        }
    }
    return cache[id];
}

void soundPlay(int id) { soundLog("sfx", id, -1, -1); startSound(id, -1, -1, false); }
void soundPlayAt(int x, int y, int id) {   // y = −1 → global
    soundLog("sfx", id, y == -1 ? -1 : x, y);
    startSound(id, x, y, y != -1);
}

static int s_lastVoiceReq = -1;                           // so para os testes
int  soundLastVoiceRequest() { return s_lastVoiceReq; }
void soundClearVoiceRequest() { s_lastVoiceReq = -1; }

void soundVoice(int id, int x, int y) {                   // sub_47F060 -> sub_47F0A0
    s_lastVoiceReq = id;                                  // antes do porteiro do audio
    soundLog("savant", id, x, y);
    if (!s_enabled && !s_clock) return;                   // (o harness tem a fila sem dispositivo)
    if (id == 0x87 || id == 0x86 || id == 0xAD) {        // fim de jogo: cala tudo e zera a fila
        if (s_dev) {
            SDL_LockAudioDevice(s_dev);
            for (Channel& c : s_ch) c.s = nullptr;
            SDL_UnlockAudioDevice(s_dev);
        }
        s_queue.clear();
        s_nextVoice = soundClock();
    }
    if (s_queue.size() < 8) s_queue.push_back(Voice{id, x, y});   // cheia: descarta
}

// sub_47F250: quando passa a hora, a voz da frente toca como som global e a
// próxima fica p/ agora + duração + 750 ms.
static void pumpVoices() {
    if (s_queue.empty()) return;
    const uint32_t now = soundClock();
    if (s_nextVoice >= now) return;
    const Voice v = s_queue.front();
    s_queue.erase(s_queue.begin());
    soundLog("play", v.id, -1, -1);
    startSound(v.id, -1, -1, false);
    s_nextVoice = now + static_cast<uint32_t>(soundLengthMs(v.id)) + 0x2EE;
}

void soundUpdate() {
    if (s_clock || !s_enabled) return;                    // com o harness, so o soundHarnessPump
    pumpVoices();
}

void soundHarnessPump() { pumpVoices(); }

// +0x1F0 gravado pelo vt[4] de cada descritor (ex.: sub_405D60 do Cargo Truck =
// 0x38); veículos 0x01..0x10 e prédios 0x15..0x3A. Paredes/tubos, armas e o
// resto não gravam o campo (fica 0).
int unitSelectSound(int mapId) {
    static const uint8_t kSel[0x3B] = {
        0x00, 0x38, 0x2A, 0x2F, 0x30, 0x36, 0x37, 0x35, 0x33, 0x32, 0x2E, 0x34, 0x2B, 0x2C, 0x31, 0x2D,
        0x34, 0x00, 0x00, 0x00, 0x00, 0x18, 0x19, 0x10, 0x14, 0x25, 0x26, 0x06, 0x07, 0x05, 0x1D, 0x20,
        0x28, 0x12, 0x16, 0x1A, 0x23, 0x1C, 0x29, 0x04, 0x08, 0x0D, 0x15, 0x17, 0x0E, 0x09, 0x0A, 0x0B,
        0x0C, 0x13, 0x11, 0x1B, 0x1E, 0x1F, 0x21, 0x24, 0x22, 0x0F, 0x27,
    };
    return mapId >= 0 && mapId < 0x3B ? kSel[mapId] : 0;
}

} // namespace op2
