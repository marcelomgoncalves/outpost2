#pragma once

#include <cstdint>
#include <vector>

namespace op2 {

// Música — porta o objeto dword_564750 do OUTPOST2.EXE (DirectSound + timer de
// 92 ms, callback fptc):
//   sub_450220  abre op2.clm ("OP2 Clump File Version 1.0", PCM 16 bits mono
//               22050 Hz; entradas de 16 bytes nome[8]/offset/tamanho a partir de
//               +0x3C) e acha as 26 faixas por nome (EDEN11..STATIC05 = SongIds);
//               falta uma → sem música.
//   fptc        buffer circular de 64 KB em duas metades de 0x8000 bytes: ao
//               cruzar uma metade, enche a outra com os próximos 0x8000 bytes da
//               faixa (posição += 0x8000; se passou do tamanho, próxima da lista,
//               e depois da última volta ao índice de repetição). A 1ª carga vai
//               p/ a 1ª metade e a 2ª metade toca vazia (como no original).
//   sub_451090  SetMusicPlayList(n, repetição, ids): troca a lista (índice 0); a
//               faixa em curso termina antes (sem zerar a posição).
//   sub_4507B0  início da partida: "MusicVol"/"Music" do INI; sem lista, a
//               padrão de 9 faixas (multijogador −8..−4: repetição 5, senão 4).
//   sub_4506C0  fim da partida (para e descarta a lista).
//   sub_451050 / sub_450E90  volume (dword_4D4AD0: 0, −5, −9, −12, −14, −15 dB) / liga-desliga.
//   sub_450A90 / sub_450B00  pausa / retoma (contador).
void musicStartGame(int missionType, bool loadedGame);   // sub_4507B0
void musicEndGame();                                      // sub_4506C0
void musicSetPlayList(int count, int repeatStart, const int* songIds);   // sub_451090
void musicSetVolume(int level);                           // sub_451050 (0..5)
void musicSetEnabled(bool on);                            // sub_450E90
void musicPause();                                        // sub_450A90
void musicResume();                                       // sub_450B00
bool musicPlaying();
// --harness (audio.jsonl): a lista [+0xCC], o indice de repeticao [+0xC8] e a posicao [+0xD0].
void musicDebugState(std::vector<int>& list, int& repeatStart, int& index);

// Chamado pelo mixer (thread de áudio): soma `frames` amostras estéreo em acc,
// na taxa `freq` do dispositivo.
void musicMix(float* acc, int frames, int freq);

// Testes: abre um CLM arbitrário (caminho) no lugar de op2.clm.
void musicSetClmPath(const char* path);
// Testes: gera `frames` amostras mono (22050 Hz) da sequência de blocos.
int musicDebugPull(int16_t* out, int frames);

} // namespace op2
