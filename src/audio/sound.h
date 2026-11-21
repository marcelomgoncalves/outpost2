#pragma once

#include <cstdint>

namespace op2 {

// Gerenciador de som — porta o objeto unk_56C610 do OUTPOST2.EXE (DirectSound):
//   sub_47E020  carrega os 252 WAVs da tabela off_4E0160 (SOUND.VOL /
//               VOICES.VOL; id = índice) e lê "Sound"/"SoundVol" do INI
//   sub_47EE00  toca: 63 canais (lista livre; um som global pode roubar um
//               canal posicional — sub_47E760). Volume DirectSound em 1/100 dB:
//               global = −SoundVol*250; posicional (x,y em pixels) também −125
//               por tile de distância do centro da vista (maior eixo + metade
//               do menor, x com wrap); a partir de 32 tiles ou −4000 → mudo;
//               pan = dx(tiles)*125
//   sub_47F020  som global (sem posição)
//   sub_47F060 / sub_47F0A0  voz do Savant: fila de 8 (prioridade byte_56DB90,
//               nunca preenchida → FIFO); toca uma de cada vez.
bool soundInit();                          // após SDL_INIT_AUDIO
void soundShutdown();
void soundSetListener(int centerPixelX, int centerPixelY);   // [+0x90C]/[+0x910]
void soundPlay(int id);                    // sub_47F020
void soundPlayAt(int pixelX, int pixelY, int id);   // sub_47EE00
void soundVoice(int id, int pixelX, int pixelY);    // sub_47F060
void soundUpdate();                        // fila de vozes (sub_47F250)

// O bipe da saida. No IWnd::_WndProc, o WM_NCDESTROY (0x82) da primeira janela
// destruida na sessao (dword_4E0BB8) -- a moldura, no TFrame::ShutDown -- toca o
// som 0x5C ("beep5.wav", sub_47F020 global) e espera 3500 ms (Sleep), para o
// som terminar antes de o audio ser desligado. Uma vez por sessao.
// Corrigido em relacao ao original: ele espera os 3,5 s MESMO sem som (Sound=0,
// sem dispositivo); aqui so espera se o bipe de fato comecou. Devolve se tocou.
bool soundExitBeep();

// Volume mestre, de 0 a 100 por cento ("[Game] AudioVolume"; 50 por padrao).
// Nao existe no original: la cada canal tem a sua atenuacao fixa (MusicVol
// 0..5 pela tabela dword_4D4AD0, SoundVol a -250/100 dB por degrau) e nada
// regula o conjunto. Multiplica a mistura final, depois dos sons e da musica,
// portanto vale para os dois e nao mexe em nenhuma conta do original. Linear
// na amplitude: 50% = metade, ou cerca de -6 dB.
void soundSetMasterVolume(int percent);
int  soundMasterVolume();

// sub_47E0C0: cala TUDO -- solta os 63 canais de uma vez. No original e o
// que TApp::ActivateShell faz ao voltar da partida para o menu, logo depois de
// parar a musica (sub_489FB0 -> sub_4506C0). Sem isto o som da partida
// continua tocando por cima do menu ate as amostras acabarem.
// A fila de vozes do Savant tambem e descartada: no original ela morre com o
// objeto do jogo, e deixa-la aqui faria uma voz atrasada estourar no meio da
// partida seguinte.
void soundStopAll();

// Quantos dos 63 canais estao tocando agora (so para os testes conferirem que
// o soundStopAll de fato calou).
int soundActiveChannels();

// sub_47E6F0 / sub_47E750: a PAUSA do jogo suspende o som. E um contador
// ([+0x91C]): na subida de 0 para 1 solta todos os canais que estao tocando;
// a descida so decrementa. A fila de vozes do Savant NAO e descartada -- isso
// e o soundStopAll, que e outro caminho (voltar ao menu).
void soundSuspend(bool on);
int  soundSuspendCount();

// Tambem so para os testes: o ultimo id que soundVoice RECEBEU, mesmo com o
// audio desligado (os modos de teste sobem sem SDL_INIT_AUDIO, e a fila de
// vozes nem chega a existir). -1 = nenhum desde a ultima limpeza. Nao existe
// no original.
int  soundLastVoiceRequest();
void soundClearVoiceRequest();

// Trava/destrava o callback do mixer (a música altera o estado dele).
void soundLockAudio(bool lock);

// Som de seleção do tipo (campo +0x1F0 do descritor off_4E1340[map_id]).
int unitSelectSound(int mapId);

// --harness (o audio.jsonl do OP2TestHarness). Extra deste porte, nada disto existe
// no original: o harness de la engancha as chamadas do jogo. `clock` troca o relogio
// das vozes e do filtro de mensagens (timeGetTime; aqui SDL_GetTicks) por um que anda
// com os ticks, e `log` recebe cada pedido de som: "sfx" (sub_47EE00/sub_47F020,
// x = -1 sem posicao), "savant" (sub_47F060), "play" (a voz que a fila toca) e "msg"
// (sub_438F70; pass 1 mostrada, 0 barrada pelo filtro, -1 id fora do filtro). Com o
// harness ligado a fila de vozes funciona sem dispositivo e so anda pelo
// soundHarnessPump (o harness a chama nos ticks em que o original a chama).
using SoundLogFn = void (*)(const char* ev, int id, int x, int y, int pass);
void soundSetHarness(uint32_t (*clock)(), SoundLogFn log);
uint32_t soundClock();                     // o relogio em vigor (ms)
void soundLog(const char* ev, int id, int x, int y, int pass = -2);
void soundHarnessPump();                   // sub_47F250 com o relogio do harness
int  soundLengthMs(int id);                // duracao do WAV (do cabecalho; tambem sem audio)
const char* soundName(int id);             // nome do arquivo (off_4E0160)

} // namespace op2
