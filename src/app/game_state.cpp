#include "game_state.h"

#include "../net/net_game.h"

#include <algorithm>
#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tapp.h"
#include "info_pane.h"
#include "../game/production.h"
#include "../game/construction.h"
#include "../game/mining.h"
#include "../game/earthwork.h"
#include "../game/terrain_cost.h"
#include "../gfx/renderer.h"
#include "../gfx/font.h"
#include "../gfx/ui_font.h"
#include "../res/res_manager.h"
#include "../core/config.h"
#include "../audio/sound.h"
#include "../game/tethys_game.h"
#include "../game/player.h"
#include "../game/unit.h"
#include "../game/command.h"
#include "../game/command_apply.h"
#include "../game/pathfinder.h"
#include "../game/movement.h"
#include "../game/vehicle_anim.h"
#include "../game/structure_anim.h"
#include "../game/missile.h"
#include "../game/salvage.h"
#include "../audio/music.h"
#include "../game/structure.h"
#include "../game/building_sheet.h"
#include "../game/colony.h"
#include "../game/messages.h"
#include "../game/weapon.h"
#include "../game/trigger.h"
#include "../game/research.h"
#include "../game/disaster.h"
#include "../game/commands.h"
#include "../game/scenario.h"
#include "../game/ai_group.h"
#include "../game/start_info.h"
#include "../game/save_game.h"
#include "../game/save_stream.h"
#include "../mission/mission.h"
#include "../gfx/artfile.h"
#include "../gfx/art_blit.h"
#include "cursors.h"
#include "../game/docking.h"

namespace op2 {

GameState::GameState() = default;
GameState::~GameState() = default;

void GameState::loadSettings() {
    // Cadência do tick (sub_489160 relê "GameSpeed" do INI ao iniciar a partida;
    // sub_49BF80: intervalo = 1000 / clamp(GameSpeed*4, 4, 40) ms).
    Config config("OUTPOST2.INI");
    GameSettings gs;
    if (config.load()) gs.loadFrom(config);
    int speed = gs.gameSpeed;
    if (speed > 10) speed = 10;
    if (speed < 1) speed = 1;
    int tps = speed * 4;
    if (tps < 4) tps = 4;
    if (tps > 40) tps = 40;
    m_tickPeriod = 1.0f / static_cast<float>(tps);
    setGameSpeedValue(speed * 4);                 // TethysGame +0x50 (GameStartInfo +0xAE)
    setAmbientAnimations(gs.ambient != 0);   // "Ambient" → dword_575628
    m_stationaryShadows = gs.stationaryShadows != 0;   // dword_575634
    m_mobileShadows = gs.mobileShadows != 0;           // dword_575638
    m_overlay = gs.overlay != 0;                       // "Overlay" → dword_57562C
    m_frameSkip = gs.frameSkip;                        // Value (FrameSkip)
    // "ScrollRate" (1..6): o porte lia do INI e nao usava. E um dos dois
    // fatores do passo da rolagem, junto do FrameSkip -- ver scrollView.
    m_scrollRate = gs.scrollRate < 1 ? 1 : (gs.scrollRate > 6 ? 6 : gs.scrollRate);
    m_gameSpeedValue = speed * 4;
    m_aiLevel = gs.aiLevel;                            // "AILevel" (so deste porte)
    if (m_aiLevel < 0 || m_aiLevel > 3) m_aiLevel = 0;
    setNightDarkness(gs.nightDarkness);                // "NightDarkness" (idem)
    soundSetMasterVolume(gs.audioVolume);              // "AudioVolume" (idem)
    m_demoCam = gs.demoCamera != 0;                    // "DemoCamera" (idem)
    m_mapWrapAll = gs.mapWrap != 0;                    // "MapWrap" (fork)
    m_showBottomRow = gs.showBottomRow != 0;           // "ShowBottomRow" (fork)
    if (g_gameMap.ok() && m_terrain.mapPixelHeight() > 0) { m_maxY = viewMaxY(); clampCamera(); }
    // "Fullscreen" NAO entra aqui: a janela nao pode pular de modo a cada
    // loadSettings (o Alt+Enter troca a tela sem gravar o INI, e o valor do
    // arquivo estaria velho). Quem aplica e o APPLY, por applyScreenMode.
}

// Does the view wrap horizontally? The original only wraps "around the world"
// maps (lgWidth >= 9, internal X offset 0); the others stop at their edges.
// MapWrap=1 makes every map wrap, as this port used to.
bool GameState::viewWraps() const {
    if (m_mapWrapAll) return true;
    return g_gameMap.ok() && g_gameMap.internalXOffset() == 0;
}

// Entrar no PREFERENCES. O espelho do painel vem do INI; como o Alt+Enter nao
// grava nada la, o radio do modo de tela e acertado com a janela de verdade no
// quadro seguinte.
void GameState::enterPrefs() {
    m_gameOptions.setMode(GameOptionsPane::PREFS);
    m_screenModeSync = true;
}

// O pedido deixado pelo painel (ver applyScreenMode em game_state.h).
void GameState::applyScreenMode(Renderer& renderer) {
    if (m_screenModeSync) {
        m_screenModeSync = false;
        m_gameOptions.syncScreenMode(renderer.fullscreen());
    }
    if (m_screenModeWanted < 0) return;
    const bool cheia = m_screenModeWanted != 0;
    m_screenModeWanted = -1;
    if (renderer.fullscreen() != cheia) renderer.setFullscreen(cheia);
}

// sub_407A10: a rolagem da vista, por quadro.
//
// O passo base e `(FrameSkip * ScrollRate * 20) / (GameSpeed * 4)`, no minimo 1
// -- `Value` e o FrameSkip e `dword_56DEA8` e o GameSpeed*4, os dois conferidos
// no disassembly. Dai o original acelera: enquanto a borda ou a seta pedem, a
// velocidade do eixo anda `passo` por quadro e satura em `passo * 32`; mudando
// de lado, ela primeiro salta `passo * 4` na direcao do zero. Quando NINGUEM
// pede (nos dois eixos -- o original usa um unico sinalizador para os dois),
// ela volta para zero de `passo * 4` por quadro.
//
// A borda e a da JANELA com 2 px de margem (GetWindowRect menos 2), e nao a da
// vista do mapa: no original o jogo ocupava a tela toda e era o canto do
// monitor que rolava. As setas entram pelo mesmo teste, com GetAsyncKeyState.
//
// Diferenca consciente: so rola com o ponteiro DENTRO da janela (o original usa
// GetCursorPos e rola ate com a janela atras de outra) e nao rola no demo de
// atracao, onde a camera ja e de quem passeia.
// A tabela 0x4E9840 do original dá o cursor de cada comando, em pares: o
// "pode" e o "não pode", escolhidos pelo teste de validade (no original um
// `cmp eax,1 / adc`, ex. sub_4515xx escolhendo dock/nodock). Aqui o teste é o
// mesmo que o comando já usa: `uiCommandCellOk` nos comandos de área, a
// unidade sob o ponteiro nos que miram unidade, e a doca para o mover.
int GameState::cursorForHover() const {
    if (!m_mission && !g_gameMap.ok()) return CUR_ARROW;
    const int vx = m_mouseNX - kViewX, vy = m_mouseNY - kViewY;
    if (vx < 0 || vy < 0 || vx >= viewportW() || vy >= viewportH()) return CUR_ARROW;
    if (m_targetCmd < 0) return CUR_ARROW;

    const int wm = g_gameMap.ok() ? (g_gameMap.width() - 1) : 0;
    const int tx = ((m_camX + vx) >> 5) & wm, ty = (m_camY + vy) >> 5;

    // Comandos de área (muro, tubo, kit, mina, terraplanar, recolher): vale o
    // mesmo teste de célula que pinta o retângulo do arrasto.
    if (uiCommandIsArea(m_targetCmd)) {
        const bool ok = uiCommandCellOk(m_targetCmd, m_selected, tx, ty);
        switch (m_targetCmd) {
            case UC_BULLDOZE: return CUR_BULLDOZE;
            case UC_SALVAGE:  return CUR_SALVAGE;
            default:          return ok ? CUR_BUILD : CUR_NOBUILD;
        }
    }

    const int sob = unitAtBox(vx, vy, /*meusApenas*/ false);
    const int me = TethysGame::localPlayer();
    auto inimigo = [&](int id) {
        if (!id) return false;
        const int o = g_units[id].owner & 0x0F;
        return o != me && !(o < kMaxPlayers && (g_playerData[o].allyMask & (1u << me)));
    };

    switch (m_targetCmd) {
        case UC_ATTACK:    return inimigo(sob) ? CUR_ATTACK : CUR_NOATTACK;
        case UC_TAKE_OVER: return inimigo(sob) ? CUR_REPROG : CUR_NOREPROG;
        case UC_REPAIR: {
            if (!sob || (g_units[sob].owner & 0x0F) != me) return CUR_NOREPAIR;
            return g_units[sob].damage > 0 ? CUR_REPAIR : CUR_NOREPAIR;
        }
        case UC_GUARD:     return CUR_GUARD;
        case UC_PATROL:    return CUR_WAYPOINT;
        case UC_SELF_DESTRUCT: return CUR_DESTRUCT;
        case UC_MOVE: {
            // Sobre a doca de um prédio, o mover vira atracar -- e o cursor
            // diz se aquele veículo pode usá-la (o par 13/14 do original).
            if (sob && (g_units[sob].flags & UNIT_BUILDING) && !m_selected.empty()) {
                int dx = 0, dy = 0;
                if (dockCell(sob, dx, dy) && dx == tx && dy == ty)
                    return vehicleCanDock(m_selected[0], sob, true) ? CUR_DOCK : CUR_NODOCK;
            }
            return g_gameMap.isPassable(LOCATION{tx, ty}) ? CUR_MOVE : CUR_NOMOVE;
        }
        default: return CUR_ARROW;
    }
}

int GameState::scrollStep() const {
    const int p = (m_frameSkip * m_scrollRate * 20) / (m_gameSpeedValue > 0 ? m_gameSpeedValue : 1);
    return p < 1 ? 1 : p;
}

void GameState::scrollView(Renderer& renderer) {
    if (demoGame() && m_demoCam) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(renderer.getWindow(), &w, &h);
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    // Ponteiro DENTRO da janela e a janela em primeiro plano: janela atras de
    // outra nao rola (o original usa GetCursorPos e rolaria mesmo assim).
    const uint32_t jf = SDL_GetWindowFlags(renderer.getWindow());
    const bool naJanela = (jf & SDL_WINDOW_MOUSE_FOCUS) && (jf & SDL_WINDOW_INPUT_FOCUS);
    const Uint8* ks = SDL_GetKeyboardState(nullptr);

    bool esq = false, dir = false, cima = false, baixo = false;
    scrollWanted(mx, my, w, h, naJanela,
                 ks[SDL_SCANCODE_LEFT] != 0, ks[SDL_SCANCODE_RIGHT] != 0,
                 ks[SDL_SCANCODE_UP] != 0, ks[SDL_SCANCODE_DOWN] != 0,
                 esq, dir, cima, baixo);
    scrollApply(esq, dir, cima, baixo);
}

void GameState::scrollWanted(int mx, int my, int w, int h, bool naJanela,
                             bool kEsq, bool kDir, bool kCima, bool kBaixo,
                             bool& esq, bool& dir, bool& cima, bool& baixo) {
    esq   = (naJanela && mx <= 2) || kEsq;
    dir   = !esq  && ((naJanela && mx >= w - 3) || kDir);
    cima  = (naJanela && my <= 2) || kCima;
    baixo = !cima && ((naJanela && my >= h - 3) || kBaixo);
}

void GameState::scrollApply(bool esq, bool dir, bool cima, bool baixo) {
    const int passo = scrollStep();
    const int teto = passo * 32;
    bool pediu = false;
    auto acelera = [&](int& vel, bool neg, bool pos) {
        if (neg) {
            pediu = true;
            if (vel >= 0) { vel -= passo * 4; if (vel < 0) vel = 0; }   // troca de lado
            vel -= passo;
            if (vel < -teto) vel = -teto;
        } else if (pos) {
            pediu = true;
            if (vel <= 0) { vel += passo * 4; if (vel > 0) vel = 0; }
            vel += passo;
            if (vel > teto) vel = teto;
        }
    };
    acelera(m_scrollVX, esq, dir);
    acelera(m_scrollVY, cima, baixo);

    // A rolagem soma o tremor acumulado no desenho anterior e zera.
    m_camX += m_scrollVX + m_shakeX;
    m_camY += m_scrollVY + m_shakeY;
    m_shakeX = m_shakeY = 0;
    clampCamera();

    if (!pediu) {                                   // loc_407CF4
        auto freia = [&](int& vel) {
            if (vel > 0) { vel -= passo * 4; if (vel < 0) vel = 0; }
            else if (vel < 0) { vel += passo * 4; if (vel > 0) vel = 0; }
        };
        freia(m_scrollVX);
        freia(m_scrollVY);
    }
}

bool GameState::loadMap(const std::string& mapName) {
    m_mapName = mapName;
    auto stream = g_ResManager.openStream(mapName);
    if (!stream || !g_gameMap.load(stream->data(), stream->size())) {
        std::cerr << "[GameState] mapa invalido: " << mapName << std::endl;
        return false;
    }
    if (!m_terrain.init(g_gameMap, g_ResManager)) {
        std::cerr << "[GameState] falha ao carregar tilesets de " << mapName << std::endl;
        return false;
    }
    const int VW = m_fbW, VH = m_fbH;
    m_maxX = m_terrain.mapPixelWidth() - viewportW();
    m_maxY = viewMaxY();
    // começa na área jogável (clipRect), como o jogo
    m_camX = g_gameMap.clipRect().x1 * 32;
    m_camY = g_gameMap.clipRect().y1 * 32;
    clampCamera();
    m_mini.resetForMap();                        // InitializeForMap: zoom 0 (fit)
    m_mini.configure(kColW, kMiniMapH);          // sub_48E7F0 com a janela 192x128
    // Sem o 4o botao nao ha globo: sub_458930 zera dword_5748A0 nesse caso.
    if (g_gameMap.internalXOffset()) m_mini.setGlobe(false);
    m_globeTex.clear();
    m_globeCamX = m_camX;
    m_globe.setSpinTarget(globeSpinTargetOf(m_camX));
    return true;
}

// Estado da interface no jogo salvo (o lugar de sub_4083B0): câmera e a
// GameStartInfo (TethysGame+0xA4 no original).
// sub_420580 / sub_420640: o bloco que o original grava logo depois da
// pesquisa, na sequencia de sub_48A120. Seis dwords, nesta ordem: globo ligado
// (dword_5748A0), sol (a fase do dia, [+0x48] = dword_5472A0), rotacao
// ([+0x4C]), sombreamento ([+0x04]), um campo que e gravado e lido mas nunca
// usado (dword_57486C) e a vista tatica (dword_574864).
std::vector<uint8_t> GameState::minimapStateSave() {
    SaveStream s(false);
    int glb = m_mini.globe() ? 1 : 0;
    int sun = TethysGame::daylightPhase();
    int rot = m_globe.rotation();
    int shaded = TethysGame::daylightEverywhere() ? 0 : 1;
    int unused = 0;
    int tac = m_mini.tactical() ? 1 : 0;
    s.io(glb); s.io(sun); s.io(rot); s.io(shaded); s.io(unused); s.io(tac);
    return s.buffer();
}

bool GameState::minimapStateLoad(const std::vector<uint8_t>& blob) {
    SaveStream s(true);
    s.setBuffer(blob);
    int glb = 0, sun = 0, rot = 0, shaded = 1, unused = 0, tac = 0;
    s.io(glb); s.io(sun); s.io(rot); s.io(shaded); s.io(unused); s.io(tac);
    if (!s.ok()) return false;
    m_mini.setGlobe(glb != 0);
    // O original escreve direto em [+0x48] e deixa [+0x44] (a coluna da luz)
    // desatualizado ate o turno seguinte; aqui as duas andam juntas.
    TethysGame::setDaylightPhase(sun, g_gameMap.ok() ? g_gameMap.internalWidth() : 1);
    m_globe.setRotation(rot);
    // loc_420711: o alvo passa a ser a rotacao gravada, entao o globo nao sai
    // girando ao abrir o jogo salvo.
    m_globe.setSpinTarget(rot);
    m_mini.setTactical(tac != 0);
    return true;
}

std::vector<uint8_t> GameState::uiStateSave() {
    SaveStream s(false);
    s.io(m_camX); s.io(m_camY);
    GameStartInfo& g = m_gsi;
    s.io(g.flags); s.ioStr(g.dll); s.io(g.playerWord); s.io(g.endCode); s.io(g.missionNumber);
    s.io(g.food); s.io(g.ore); s.io(g.rareOre); s.io(g.workers); s.io(g.scientists); s.io(g.kids);
    s.io(g.spaceWord); s.io(g.localPlayer); s.io(g.endTick); s.io(g.playerBlock);
    s.io(g.techCount); s.io(g.techLevel); s.ioVec(g.techIdx); s.ioVec(g.techMask);
    return s.buffer();
}
bool GameState::uiStateLoad(const std::vector<uint8_t>& blob) {
    SaveStream s(true);
    s.setBuffer(blob);
    s.io(m_camX); s.io(m_camY);
    GameStartInfo& g = m_gsi;
    s.io(g.flags); s.ioStr(g.dll); s.io(g.playerWord); s.io(g.endCode); s.io(g.missionNumber);
    s.io(g.food); s.io(g.ore); s.io(g.rareOre); s.io(g.workers); s.io(g.scientists); s.io(g.kids);
    s.io(g.spaceWord); s.io(g.localPlayer); s.io(g.endTick); s.io(g.playerBlock);
    s.io(g.techCount); s.io(g.techLevel); s.ioVec(g.techIdx); s.ioVec(g.techMask);
    clampCamera();
    m_globeCamX = m_camX;         // a camera do arquivo nao e um movimento
    return s.ok();
}

bool GameState::saveToFile(const std::string& path, const std::string& description) {
    if (!m_mission) return false;
    return saveGameWrite(path, description, minimapStateSave(), uiStateSave());   // sub_48A120
}

bool GameState::loadSavedGame(const std::string& path) {
    SaveHeader h;
    if (!saveReadHeader(path, h)) {
        std::cerr << "[Load] " << path << " nao e um jogo salvo desta versao" << std::endl;
        return false;
    }
    std::cout << "[Load] " << path << ": '" << h.description << "' " << h.dll << " tick " << h.tick << std::endl;
    return loadMission(h.dll, nullptr, &path);
}

bool GameState::loadMission(const std::string& dllName, const GameStartInfo* gsi, const std::string* savePath) {
    if (m_mission) { setCurrentMission(nullptr); m_mission.reset(); }
    resetTriggers();
    auto mission = std::make_unique<MissionScript>();
    std::string err;
    if (!mission->load(dllName, err)) {          // sub_402A10 ("Could not load script file")
        std::cerr << "[GameState] nao carregou " << dllName << ": " << err << std::endl;
        return false;
    }
    return startMission(std::move(mission), gsi, savePath);
}

bool GameState::startMission(std::unique_ptr<MissionScript> mission, const GameStartInfo* gsi,
                             const std::string* savePath) {
    loadSettings();
    if (m_mission) { setCurrentMission(nullptr); m_mission.reset(); }
    resetTriggers();
    const std::string dllName = mission->dllName;
    const AIModDesc* d = mission->desc;
    std::cout << "[Mission] " << dllName << ": '" << mission->levelDesc << "' tipo=" << d->missionType
              << " jogadores=" << d->numPlayers << " tech=" << d->maxTechLevel
              << " mapa=" << mission->mapName << " arvore=" << mission->techtreeName << std::endl;
    if (!loadMap(mission->mapName)) return false;
    m_mission = std::move(mission);
    setCurrentMission(m_mission.get());
    closeTabPanes();
    m_tab = 5;                                               // sub_4893A0: unk_565AC8 no início
    m_lastSel.clear();

    TethysGame::reset(m_seed, d->numPlayers, /*localPlayer*/ 0);
    m_tickAccum = 0.0f;
    resetPlayers(d->numPlayers);
    // sub_4893A0: em MULTIJOGADOR todos os jogadores sao humanos — loc_489648
    // chama GoHuman (sub_4905E0) para cada um; em um jogador so o 0
    // (loc_4896D5). O shell decide pelo bit 0x10 do GameStartInfo; sem ele
    // (testes por linha de comando) vale o tipo da missao, que so e M* em
    // multijogador. Sem isto o CreateOnePlayerLeftTrigger das M* ve um unico
    // humano vivo e declara vitoria no primeiro tique.
    setDemoGame(gsi && gsi->demo);                      // dword_56DF08 bit 3
    const bool multiGame = gsi ? ((gsi->flags & 0x10) != 0)
                               : (d->missionType <= -4 && d->missionType >= -8);
    // (o registro vem pronto do GameStartInfo: só o campo, sem GoAI/GoHuman).
    for (int p = 0; p < d->numPlayers; ++p)
        g_playerData[p].isHuman = (multiGame || p == 0) ? 1 : 0;
    // sub_4893A0: a estrutura vai para TethysGame+0xA4; dificuldade de cada
    // jogador = bits 3-4 da palavra dele (+0x30).
    m_gsi = gsi ? *gsi : GameStartInfo{};
    m_gsi.dll = dllName;
    m_endPhase = 0;
    if (gsi)
        for (int p = 0; p < d->numPlayers && p < 6; ++p) g_playerData[p].difficulty = gsi->difficulty(p);
    // "AILevel" do INI, que o original nao tem. A dificuldade e o unico botao
    // que mexe com o adversario: as DLLs de campanha chamam
    // _Player::Difficulty() do jogador 0 e calibram o inimigo com ela (E01..E12
    // e P01..P12 todas consultam). Em multijogador ela e por jogador e vem
    // combinada no lobby (o combo Resources grava nos mesmos bits 3-4), entao
    // uma preferencia local mudaria a simulacao de um lado so e quebraria o
    // lockstep — por isso so vale fora do multijogador.
    if (m_aiLevel > 0 && !multiGame)
        for (int p = 0; p < d->numPlayers && p < 6; ++p)
            g_playerData[p].difficulty = m_aiLevel - 1;
    // sub_4893A0: planilhas relidas (sub_445070) e árvore da missão (sub_4733B0).
    reloadSheets();
    loadMoraleSheet();
    loadMinesSheet();                       // MINES.TXT (sub_44AF00, no TApp::Init)
    loadTechTree(m_mission->techtreeName, d->maxTechLevel);
    musicEndGame();                         // sub_489FB0 → sub_4506C0 (partida anterior)
    musicStartGame(d->missionType, false);  // sub_4507B0 (antes do InitProc)
    resetMessages();
    resetUnits();
    lavaReset();                                 // sub_4322A0 (início da partida)
    TethysGame::SetDaylightMoves(0);
    TethysGame::SetDaylightEverywhere(0);
    // Jogo salvo (sub_402CB0 & cia.): no lugar do InitProc, o estado do arquivo.
    if (savePath) {
        std::vector<uint8_t> extra, mm;
        if (!saveGameRead(*savePath, mm, extra) || !minimapStateLoad(mm) || !uiStateLoad(extra)) {
            std::cerr << "[Load] falhou: " << *savePath << std::endl;
            setCurrentMission(nullptr);
            m_mission.reset();
            resetTriggers();
            return false;
        }
        std::cout << "[Load] ok: tick " << TethysGame::tick() << ", unidades " << (g_units.size() - 1) << std::endl;
        return true;
    }
    // Campanha: sobras da missão anterior (sub_4898F0), antes do InitProc.
    if (gsi && gsi->campaign()) startInfoApplyCampaign(m_gsi);
    // sub_402E10: grupo padrão de cada computador (sub_402E70) e InitProc.
    aiDefaultGroupsStart();
    const int ok = m_mission->initProc ? m_mission->initProc() : 1;
    researchStartGame();                         // sub_472C30 (depois do InitProc)
    std::cout << "[Mission] InitProc -> " << ok << "; unidades=" << (g_units.size() - 1) << std::endl;
    int cx, cy;
    if (takeCenterViewRequest(cx, cy)) {           // CenterViewOn
        m_camX = cx - viewportW() / 2;
        m_camY = cy - viewportH() / 2;
        clampCamera();
    }
    return ok != 0;
}

bool GameState::load(const std::string& mapName) {
    loadSettings();
    if (m_mission) { setCurrentMission(nullptr); m_mission.reset(); }
    resetTriggers();
    if (!loadMap(mapName)) return false;

    // Inicia o núcleo de simulação (relógio zerado + RNG semeado). O seed virá da
    // missão/netgame nos próximos slices; por ora um valor fixo p/ determinismo.
    TethysGame::reset(/*seed*/ 0x12345678u, /*numPlayers*/ 2, /*localPlayer*/ 0);
    m_tickAccum = 0.0f;

    // Semeia o jogador local como uma missão faria no InitProc (via Set*). A
    // colônia/recursos reais virão da DLL da missão no próximo slice.
    resetPlayers(TethysGame::noPlayers());
    _Player& p = Player[TethysGame::localPlayer()];
    bool plymouth = (mapName.size() >= 3 &&
                     (mapName[0] == 'p' || mapName[0] == 'P') &&
                     (mapName[1] == 'l' || mapName[1] == 'L'));
    if (plymouth) p.GoPlymouth(); else p.GoEden();
    p.GoHuman();
    p.SetOre(5000);
    p.SetRareOre(0);
    p.SetFoodStored(3000);
    p.SetWorkers(16);
    p.SetScientists(8);
    p.SetKids(4);

    // Cria algumas unidades iniciais (como o InitProc/CreateUnit de uma missão),
    // centradas na área jogável — prova o pool de unidades + CreateUnit + render.
    // Planilhas de unidades (sub_445070): HP, Build Points, entulho, explosao...
    loadBuildingSheet();
    loadVehicleSheet();
    loadMinesSheet();
    loadWeaponSheet();
    loadSpaceSheet();
    loadMoraleSheet();
    resetMessages();
    resetUnits();
    lavaReset();                                 // sub_4322A0 (início da partida)
    // Dia/noite como as missões de colônia configuram (SetDaylightMoves(1) /
    // SetDaylightEverywhere(0)); as missões reais definem isso no InitProc.
    TethysGame::SetDaylightMoves(1);
    TethysGame::SetDaylightEverywhere(0);
    // Centro do demo: meio do mapa (o centro do clipRect da eden01 cai na emenda
    // do wrap horizontal, o que espalha a base pelas duas bordas da tela).
    int cx = (g_gameMap.width() / 2) & (g_gameMap.width() - 1);
    int cy = g_gameMap.height() / 2;
    int owner = TethysGame::localPlayer();
    // Posiciona cada unidade na célula passável mais próxima do ponto desejado
    // (evita montanhas/cânions). As posições reais virão da missão depois.
    auto place = [&](map_id type, int dx, int dy, map_id cargo = kMapIdNone) {
        Unit u;
        // Célula livre (passável E desocupada) p/ não sobrepor footprint/unidades.
        LOCATION loc = g_gameMap.findFreeNear(LOCATION{cx + dx, cy + dy});
        TethysGame::CreateUnit(u, type, loc, owner, cargo);
    };
    // Tanques armados (cargo = arma, map_id): Eden usa Laser / Rail Gun.
    place(mapCommandCenter, 0, 0);
    place(mapConVec, 2, 0);
    place(mapTiger, -2, 1, 0x3F);       // Rail Gun Tiger
    place(mapScout, 1, -2);
    place(mapRoboMiner, -3, -2);
    place(mapLynx, 3, 2, 0x3D);         // Laser Lynx

    // Inimigo Plymouth (player 1) p/ ver a facção (sprites verdes) + distinção.
    if (TethysGame::noPlayers() >= 2) {
        Player[1].GoPlymouth(); Player[1].GoAI();
        auto placeE = [&](map_id type, int dx, int dy, map_id cargo = kMapIdNone) {
            Unit u;
            LOCATION loc = g_gameMap.findFreeNear(LOCATION{(cx + dx) & (g_gameMap.width()-1), cy + dy});
            TethysGame::CreateUnit(u, type, loc, 1, cargo);
        };
        // Plymouth: RPG / Microwave.
        placeE(mapTiger, 8, 0, 0x40);   // RPG Tiger
        placeE(mapLynx, 9, 2, 0x3E);    // Microwave Lynx
        placeE(mapScout, 8, -2);
    }

    // Centraliza a câmera na base inicial (como CenterViewOn do jogo).
    m_camX = cx * 32 - viewportW() / 2;
    m_camY = cy * 32 - viewportH() / 2;
    clampCamera();

    std::cout << "[GameState] mapa " << mapName << " carregado ("
              << g_gameMap.width() << "x" << g_gameMap.height() << " tiles)" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Passeio da câmera durante o demo de atração — ACRÉSCIMO DESTE PORTE.
//
// Os três ademo*.dll chamam CenterViewOn UMA vez, no InitProc, e a câmera
// fica parada o demo inteiro (conferido nas três DLLs: é a única chamada em
// cada uma). O resultado é que boa parte do que o demo monta acontece fora da
// tela — no ademo1 o tornado cai em (3,34) com a câmera parada em (44,42).
//
// Então aqui a câmera passeia: a cada poucos segundos escolhe um ponto de
// interesse e rola até ele. A ordem de preferência é desastre em cena, depois
// combate recente, depois as unidades de cada jogador; entre candidatos de
// mesmo peso há rodízio, para não ficar voltando ao mesmo lugar.
//
// Nada disto toca a simulação: mexe só em m_camX/m_camY, e a escolha usa um
// contador próprio em vez do RNG do jogo, que é determinístico e não pode
// receber consumos a mais. Quem quiser o comportamento do original põe
// "[Game] DemoCamera=0".
void GameState::demoCameraTick() {
    if (!demoGame() || !m_demoCam) return;

    constexpr int kEspera = 110;    // ~5,5 s parado em cada ponto
    constexpr int kPasso = 7;       // px por tique (≈140 px/s)
    constexpr int kRecente = 160;   // "combate recente": 8 s

    if (--m_demoCamHold <= 0) {
        m_demoCamHold = kEspera;
        // Candidatos, do mais interessante ao menos.
        std::vector<std::pair<int, int>> desastre, briga, gente;
        const int agora = static_cast<int>(TethysGame::tick());
        for (size_t i = 1; i < g_units.size(); ++i) {
            const UnitRecord& r = g_units[i];
            if (r.id == -1 || (r.flags & UNIT_DEAD)) continue;
            const std::pair<int, int> onde(unitOX(r), unitOY(r));
            if (r.type >= 0x4C && r.type <= 0x50) desastre.push_back(onde);
            else if (r.lastAttacker && agora - r.lastAttackTick < kRecente) briga.push_back(onde);
            else if (!(r.flags & UNIT_BUILDING)) gente.push_back(onde);
        }
        const std::vector<std::pair<int, int>>& lista =
            !desastre.empty() ? desastre : (!briga.empty() ? briga : gente);
        if (!lista.empty()) {
            const std::pair<int, int>& alvo = lista[m_demoCamSeq++ % lista.size()];
            m_demoTargX = alvo.first;
            m_demoTargY = alvo.second;
        }
    }
    if (m_demoTargX < 0) return;

    // Rola até o alvo. Em X o mapa dá a volta, então vai pelo lado mais curto.
    const int larguraPx = g_gameMap.ok() ? g_gameMap.width() * 32 : 0;
    int dx = m_demoTargX - (m_camX + viewportW() / 2);
    if (larguraPx && viewWraps()) {
        while (dx > larguraPx / 2) dx -= larguraPx;
        while (dx < -larguraPx / 2) dx += larguraPx;
    }
    const int dy = m_demoTargY - (m_camY + viewportH() / 2);
    auto anda = [&](int d) { return d > kPasso ? kPasso : (d < -kPasso ? -kPasso : d); };
    m_camX += anda(dx);
    m_camY += anda(dy);
    if (larguraPx && viewWraps()) {
        while (m_camX < 0) m_camX += larguraPx;
        while (m_camX >= larguraPx) m_camX -= larguraPx;
    }
    clampCamera();
}

// "E&nd current mission and restart it": o original (sub_457880 caso 8) copia o
// GameStartInfo guardado, troca a semente e chama TApp::StartSingleGame de
// novo. Aqui basta reativar a mesma DLL com o mesmo GameStartInfo -- quem
// zera o mundo e o proprio loadMission.
void GameState::requestRestartMission() {
    if (!m_mission) return;
    const std::string dll = m_mission->dllName;
    const GameStartInfo gsi = m_gsi;
    if (!gTApp.activateMission(dll, &gsi))
        std::cerr << "[GameState] nao consegui reiniciar " << dll << std::endl;
}

// "Go &Back to Main Menu" (sub_457880 caso 9): o original NAO encerra na mao --
// manda o mesmo pacote 0x31, com o metodo GoAI. A colonia fica de pe nas maos
// da IA e quem saiu e que encerra, com o codigo 6 em jogo solo e 9 em rede.
void GameState::leaveToMainMenu() {
    sendQuit(QUIT_GO_AI);
}

// "&Transfer vehicles/destroy structures" (caso 0xC): doa os veiculos ao
// aliado e sai. Quem varre o mapa e o proprio pacote 0x31.
void GameState::transferAndLeave() {
    sendQuit(QUIT_TRANSFER);
}

// "&Destroy all vehicles and structures and leave the game": tira do mapa tudo
// o que e do jogador local e encerra com o codigo 6.
void GameState::destroyAllAndLeave() {
    // Nao e uma destruicao feita na mao: o original manda o pacote 0x31 (Quit)
    // com o metodo SelfDestruct, e e ele que varre as unidades -- em todas as
    // maquinas, se a partida for em rede.
    sendQuit(QUIT_SELF_DESTRUCT);
}

// sub_4115F0. Devolve true quando a tecla foi consumida pela linha.
bool GameState::chatKey(const SDL_Event& e) {
    if (e.type == SDL_TEXTINPUT) {
        if (!m_chatOpen) return false;
        for (const char* c = e.text.text; *c; ++c)
            if (static_cast<unsigned char>(*c) >= 0x20 && m_chatLine.size() < 0x3F)
                m_chatLine += *c;
        return true;
    }
    if (e.type != SDL_KEYDOWN) return false;
    const SDL_Keycode k = e.key.keysym.sym;
    if (!m_chatOpen) {
        // Fechada, so o Enter interessa. E em QUALQUER partida: sub_4115F0 nao
        // testa o tipo de jogo, e com razao -- e por esta linha que se digitam
        // os cheats "Dan's" (ver cheat.cpp), que em jogo solo e justamente
        // onde fazem sentido. O porte exigia multijogador aqui, e com isso os
        // cheats ficavam inalcancaveis fora da rede.
        if (k != SDLK_RETURN && k != SDLK_KP_ENTER) return false;
        if (m_saveDlg.active() || m_endPhase) return false;
        m_chatOpen = true;
        m_chatLine.clear();
        return true;
    }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {     // loc_41188B: manda e fecha
        if (!m_chatLine.empty()) sendChat(m_chatLine);
        m_chatOpen = false;
        m_chatLine.clear();
        return true;
    }
    if (k == SDLK_ESCAPE) {                           // desiste sem mandar
        m_chatOpen = false;
        m_chatLine.clear();
        return true;
    }
    if (k == SDLK_BACKSPACE) {
        if (!m_chatLine.empty()) m_chatLine.pop_back();
        return true;
    }
    return true;                                      // aberta, come o resto
}

// A linha fica rente ao pe da vista, com o fundo preto e o cursor em vermelho
// -- as cores sao as que sub_411530 cria (texto 0xFFFFFF, pincel 0x0000FF).
//
// O RETANGULO do original nao saiu do disassembly: o filtro le [+0x80..+0x8C]
// mas nenhum trecho que achamos escreve esses campos (quem os calcula nao
// aparece nas referencias a dword_546B70). O pe da vista e escolha nossa, e o
// rotulo "Say:" tambem; o resto do comportamento e o de sub_4115F0.
void GameState::drawChatLine(uint32_t* fb, int vw, int vh) const {
    if (!m_chatOpen) return;
    (void)vh;
    const int h = UiFont::lineHeight() + 4;
    const int t = kClientY + kViewH - h, l = kClientX, r = kClientX + kViewW;
    for (int y = t; y < t + h; ++y)
        for (int x = l; x < r; ++x)
            if (y >= 0 && y < vh && x >= 0 && x < vw) fb[y * vw + x] = 0xFF000000u;
    const std::string texto = "Say: " + m_chatLine;
    const int tx = l + 4, ty = t + 2;
    UiFont::draw(fb, vw, vh, texto, tx, ty, 0xFFFFFFFFu);
    // O cursor pisca duas vezes por segundo.
    if ((SDL_GetTicks() / 250) & 1) {
        const int cx = tx + UiFont::width(texto);
        for (int y = ty; y < ty + UiFont::lineHeight(); ++y)
            for (int x = cx; x < cx + 2; ++x)
                if (y >= 0 && y < vh && x >= 0 && x < vw) fb[y * vw + x] = 0xFFFF0000u;
    }
}

// Lowest camera Y. The original stops the view at the bottom of the map's clip
// rect (stock maps: y2 = height - 2), so the map's last tile row is never
// shown. ShowBottomRow=1 lets the view reach the real bottom of the map.
int GameState::viewMaxY() const {
    int bottom = m_terrain.mapPixelHeight();
    if (!m_showBottomRow && g_gameMap.ok()) {
        const int clipBottom = (g_gameMap.clipRect().y2 + 1) * 32;
        if (clipBottom > 0 && clipBottom < bottom) bottom = clipBottom;
    }
    const int maxY = bottom - viewportH();
    return maxY < 0 ? 0 : maxY;
}

void GameState::clampCamera() {
    if (m_camY < 0) m_camY = 0;
    if (m_camY > m_maxY) m_camY = m_maxY;
    if (!g_gameMap.ok()) return;
    const int worldW = g_gameMap.width() * 32;
    if (viewWraps()) {
        // Wrapping map: scroll freely, kept in [0, worldW) (terrain wraps).
        if (worldW > 0) m_camX = ((m_camX % worldW) + worldW) % worldW;
        return;
    }
    // Like OP2: the view stops at the left and right edges of the map.
    const int maxX = worldW - viewportW();
    if (m_camX > maxX) m_camX = maxX;
    if (m_camX < 0) m_camX = 0;
}

void GameState::onActivate() {
    std::cout << "[GameState] ativo: " << m_mapName << std::endl;
}

// sub_49C830: códigos 10/11 (demo) saem direto; os demais esperam 2 s
// (Sleep(2000) com o cursor de espera) e, numa missão de campanha cumprida,
// abrem a caixa "Success" com "Save Game" / "Continue" (sub_41D5E0, Dialog 262).
void GameState::beginGameEnd() {
    const int code = gameEndCode();
    if (code == 10 || code == 11 || !gTApp.hasShell()) { finishGameEnd(); return; }
    m_endPhase = 1;
    m_endWaitStart = SDL_GetTicks();
}

void GameState::finishGameEnd() {
    const int code = gameEndCode();
    startInfoCollectResults(m_gsi, m_mission ? m_mission->desc : nullptr, code);   // sub_489A30
    if (code == 3 && m_gsi.campaign()) ++m_gsi.missionNumber;                    // +0x16A
    const GameStartInfo results = m_gsi;
    m_endPhase = 0;
    setCurrentMission(nullptr);
    m_mission.reset();
    resetTriggers();
    gTApp.activateShell(code, &results);                     // ActivateShell(motivo, resultados)
}

// Arestas do botão aceso sem quadro próprio (sub_40AEE0: duas linhas 0x39CF).
static constexpr uint32_t kBevelLit = 0xFF707078u;

// Arte crua dos botões do minimapa (dword_4E2E18): Zoom In, Zoom Out,
// Toggle Tactical View, Toggle Globe View.
static const int kMiniBtnArt[4] = {0x9D, 0x9C, 0x9E, 0x9A};

// Dialog 262 (OUT2RES.DLL, 186x94 DLU): texto + dois botões (25,57) e (102,58).
static constexpr int kSdW = 279, kSdH = 153;
static int kSdX = (640 - kSdW) / 2, kSdY = (480 - kSdH) / 2;   // centrada: relayout()

void GameState::relayout(int fbW, int fbH) {
    m_fbW = std::max(fbW, 640);
    m_fbH = std::max(fbH, 480);
    // Moldura: título de 22 px em cima, bordas de 12 nos lados e embaixo.
    kClientW = m_fbW - kClientX - 12;
    kClientH = m_fbH - kClientY - 12;
    // Coluna direita presa à borda (filete de 8 px em R − 0xC8, sub_499BC0);
    // a vista ocupa o resto da largura e tudo menos a barra de status na altura.
    kViewW = kClientW - kColW - 8;
    kViewH = kClientH - kStatusH;
    kColX = kClientX + kClientW - kColW;
    kPaneBottom = kClientY + kClientH;
    kStatusY = kViewY + kViewH;
    kSdX = (m_fbW - kSdW) / 2;
    kSdY = (m_fbH - kSdH) / 2;
    // A vista mudou de tamanho: o limite da câmera também.
    if (m_terrain.mapPixelWidth() > 0) {
        m_maxX = m_terrain.mapPixelWidth() - viewportW();
        m_maxY = viewMaxY();
        clampCamera();
    }
}

int GameState::successButtonAt(int x, int y) const {
    if (y >= kSdY + 93 && y < kSdY + 93 + 23) {
        if (x >= kSdX + 37 && x < kSdX + 37 + 75) return 1;
        if (x >= kSdX + 153 && x < kSdX + 153 + 75) return 2;
    }
    return 0;
}

void GameState::drawSuccessDialog(Renderer& renderer) {
    renderer.drawDialogFrame(kSdX, kSdY, kSdW, kSdH, "");
    Font::drawTextCentered(renderer.getFramebuffer(), m_fbW, m_fbH, "Success",
                           kSdX + kSdW / 2, kSdY + 45, 0xFFFFFFFF, true);
    renderer.drawButton(kSdX + 37, kSdY + 93, 75, 23, "Save Game", successButtonAt(m_mouseNX, m_mouseNY) == 1, false);
    renderer.drawButton(kSdX + 153, kSdY + 93, 75, 23, "Continue", successButtonAt(m_mouseNX, m_mouseNY) == 2, false);
}

bool GameState::simTick() {
    TethysGame::noteTick();                  // p/ "Actual Game Speed"
    netApplyTurn(static_cast<int>(TethysGame::tick()) + 1);
    TethysGame::advanceDaylight(g_gameMap.internalWidth());   // início do turno (sub_489DD0)
    lavaTick();                              // sub_49DC20 (lava e micróbio)
    // The debug HUD's sample is read from the RNG's state, not drawn: the original draws no number per tick, and an
    // extra draw would change every random outcome after it.
    m_lastRand = static_cast<int>(TethysGame::rng().s1() % 100);
    tickColony();                            // economia de cada jogador (sub_471F30)
    missionTriggerTick(currentMission());    // triggers, AIProc, vitória/derrota (sub_403110)
    tickMovement();                          // move as unidades pela rota do pathfinder
    tickUnits();                             // predios + efeitos (sub_43AC30)
    // The original raises the tick after the unit loop (ProcessGameCycle: call 0x43AD30 at 0x48A01B, then
    // [GameImpl+0x84] += 1 at 0x48A020..0x48A02D): everything in the cycle sees the previous tick.
    TethysGame::advanceTick();
    demoCameraTick();                        // só a vista; não mexe na simulação
    if ((TethysGame::tick() & 3) == 2) {     // sub_47EA60: listener = centro da vista
        soundSetListener(m_camX + viewportW() / 2, m_camY + viewportH() / 2);
        soundUpdate();                       // sub_47F250: fila de vozes
    }
    // Fim de jogo (TethysGame +0x98): chegado o tick, sub_49C830.
    return gameEndCode() && TethysGame::tick() >= gameEndTick();
}

void GameState::onIdle(Renderer& renderer, float dt) {
    applyScreenMode(renderer);               // extra do porte: o radio "Screen Mode"
    // Fork: o quadro do jogo tem o tamanho da janela (1:1), nao 640x480 esticado.
    // Vem DEPOIS do applyScreenMode: se o &APPLY acabou de trocar o modo, o
    // encaixe ja ve a janela nova.
    renderer.fitToWindow(640, 480);
    if (renderer.getWidth() != m_fbW || renderer.getHeight() != m_fbH)
        relayout(renderer.getWidth(), renderer.getHeight());
    if (!m_pendingLoad.empty()) {                            // TFrame::PostDelayedLoadMessage
        const std::string path = m_pendingLoad;
        m_pendingLoad.clear();
        if (!loadSavedGame(path)) { gTApp.activateShell(); return; }
    }
    if (m_endPhase == 1 && SDL_GetTicks() - m_endWaitStart >= 2000) {
        const int code = gameEndCode();
        // sub_402820: DescBlock[0] > 0 = missão de campanha (e dword_56DEF8 = 0).
        if (code == 3 && m_mission && m_mission->desc && m_mission->desc->missionType > 0) m_endPhase = 2;
        else { finishGameEnd(); return; }
    }
    scrollView(renderer);                    // sub_407A10: borda da janela e setas
    cursorSet(static_cast<CursorKind>(cursorForHover()));   // .ANI de comando
    cursorAnimate();

    TethysGame::noteFrame();                     // p/ "Actual Update Rate"
    // Avança o relógio de simulação na cadência do GameSpeed (m_tickPeriod).
    const float kTickPeriod = m_tickPeriod;
    m_tickAccum += dt;
    // Encerrada, dialogo modal ou PAUSADA: sem turnos. O acumulador zera a
    // cada quadro para o jogo nao disparar em camera rapida ao voltar.
    if (m_endPhase || m_saveDlg.active() || gTApp.paused()) m_tickAccum = 0.0f;
    while (m_tickAccum >= kTickPeriod) {
        // Lockstep: em rede o tique so avanca quando o turno de todos chegou
        // (o anel de 16 turnos do original, pacote 0x33).
        if (!netReadyForTick(static_cast<int>(TethysGame::tick()) + 1)) {
            // O tempo passado esperando o turno do outro nao vira credito: sem
            // este grampo o acumulador cresce durante a espera e o jogo dispara
            // em camera rapida assim que a rede destrava, avancando varios
            // tiques numa chamada so.
            if (m_tickAccum > kTickPeriod) m_tickAccum = kTickPeriod;
            break;
        }
        const bool ended = simTick();
        m_tickAccum -= kTickPeriod;
        if (ended) {
            beginGameEnd();
            if (!m_endPhase) return;
            break;
        }
    }
    // Minimapa segurado (sub_4587B0 → sub_41F9E0): relê o cursor e centra a vista.
    if (m_miniDrag) {
        if (!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT))) {
            m_miniDrag = false;
        } else {
            int px, py;
            if (miniToWorld(m_mouseNX, m_mouseNY, px, py)) requestCenterView(px, py);
        }
    }
    {   // CenterViewOn pedido pelo script.
        int cx, cy;
        if (takeCenterViewRequest(cx, cy)) {
            m_camX = cx - viewportW() / 2;
            m_camY = cy - viewportH() / 2;
            clampCamera();
        }
    }

    // Seleção: tira unidades que morreram (o slot pode ter sido reusado por
    // um projétil/efeito — sub_439910 reaproveita slots livres).
    m_selected.erase(std::remove_if(m_selected.begin(), m_selected.end(), [](int id) {
        if (id <= 0 || id >= (int)g_units.size()) return true;
        const UnitRecord& r = g_units[id];
        return r.id == -1 || (r.flags & UNIT_DEAD) || !(r.flags & (UNIT_VEHICLE | UNIT_BUILDING));
    }), m_selected.end());

    renderer.clear(0xFF000000);
    // Ciclo dia/noite no terreno: cada coluna de tile sai pela paleta da sua
    // escuridao (sub_4A8BD0). Com SetDaylightEverywhere nao se escurece nada,
    // exatamente como o teste de dword_56DEA4 la dentro.
    m_terrain.render(renderer.getFramebuffer(), m_fbW,
                     m_fbH, m_camX, m_camY,
                     TethysGame::daylightEverywhere() ? nullptr : &GameState::tileDarkness,
                     viewWraps());

    // Overlay de passabilidade (debug, tecla G): tinge de vermelho as células
    // impassáveis visíveis. Base visual para waypoints/pathfinding.
    if (m_showPassability) {
        uint32_t* fb = renderer.getFramebuffer();
        int t0x = m_camX / 32, t0y = m_camY / 32;
        int t1x = (m_camX + m_fbW) / 32;
        int t1y = (m_camY + m_fbH) / 32;
        for (int ty = t0y; ty <= t1y; ++ty) {
            if (ty < 0 || ty >= g_gameMap.height()) continue;
            for (int tx = t0x; tx <= t1x; ++tx) {
                if (g_gameMap.isPassable(LOCATION{tx, ty})) continue;
                int sx = tx * 32 - m_camX, sy = ty * 32 - m_camY;
                for (int yy = 0; yy < 32; ++yy) {
                    int py = sy + yy;
                    if (py < 0 || py >= m_fbH) continue;
                    for (int xx = 0; xx < 32; ++xx) {
                        int px = sx + xx;
                        if (px < 0 || px >= m_fbW) continue;
                        uint32_t c = fb[py * m_fbW + px];
                        uint32_t r = ((c >> 16) & 0xFF), g = (c >> 8) & 0xFF, b = c & 0xFF;
                        r = r / 2 + 128; g = g / 2; b = b / 2;   // tinge vermelho
                        fb[py * m_fbW + px] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }

    // Unidades: por ora marcadores coloridos por dono (os sprites op2_art entram
    // num slice futuro). Prédio = quadrado maior; veículo = menor.
    static const uint32_t kOwnerColor[7] = {
        0xFF40A0FF, 0xFFFF5040, 0xFF40D070, 0xFFFFD040,
        0xFFC060FF, 0xFF40E0E0, 0xFFFF9030,
    };
    // (Aqui havia um desenho de depuracao SEMPRE LIGADO: uma linha de cada
    // unidade com ordem de movimento ate o destino, mais um X branco no tile
    // alvo. Era a prova visivel do caminho do comando enquanto o pathfinder e
    // o integrador nao estavam portados -- e ficou ligado depois que eles
    // entraram, aparecendo ate no demo de atracao. O original nao desenha nada
    // disso. Para depurar rota, os modos --movelog e --movecap continuam.)

    // Blit de um frame de animação do op2_art (g_art) CENTRADO no ponto da unidade
    // na tela. As pictures são posicionadas por (x,y) menos o HOTSPOT da animação
    // (header[5],header[6] = centro do bbox header[1..4]) — como o original faz
    // (`sub eax, dword_50B368[...]` em sub_403E00). Índice 0 = transparente.
    // Sprites NAO escurecem com a noite: sub_405160 (o desenho da unidade)
    // passa -1 como coluna de luz, que e o sentinela de sub_403E00 para usar a
    // paleta base. Dos 84 pontos que chamam sub_403E00 so dois passam a coluna
    // de verdade (sub_4056E0 / sub_405840, a animacao fixa 0x2A2): no original
    // so o TERRENO escurece, e as unidades seguem legiveis no escuro.
    auto drawSprite = [&](int animIdx, int frameIdx, int cx, int cy, int playerColor) {
        return blitArtFrame(renderer.getFramebuffer(), m_fbW, m_fbH,
                            animIdx, frameIdx, cx, cy, playerColor,
                            0, 0, viewportW(), viewportH());
    };

    // Colchete de seleção (sub_43E950): guardo o frame do corpo de cada unidade
    // selecionada durante o desenho e traço os cantos depois, por cima de tudo.
    // O hover (sem seleção, unidade sob o cursor há >10 ticks) entra em cinza.
    struct SelDeco { int id, cx, cy, anim, frame; bool hover; };
    std::vector<SelDeco> selDeco;
    auto isSel = [&](int id) {
        for (int s : m_selected) if (s == id) return true;
        return false;
    };
    const bool hoverOn = m_selected.empty() && m_hoverUnit > 0 &&
                         static_cast<int>(TethysGame::tick()) - m_hoverStartTick > 0x0A;
    auto recDeco = [&](int id, int cx, int cy, int anim, int frame) {
        if (isSel(id)) selDeco.push_back({id, cx, cy, anim, frame, false});
        else if (hoverOn && id == m_hoverUnit) selDeco.push_back({id, cx, cy, anim, frame, true});
    };

    // Passe de sombra (sub_49E8C0): vt[30] das unidades com 0x10000000 — a
    // poeira do tornado — antes de tudo.
    for (size_t i = 1; i < g_units.size(); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id != -1 && r.type == mapAmbientAnim && (r.flags & UNIT_EFFECT_HIDDEN)) {   // sub_405080
            int anim, frame;
            if (effectAnimFrame(r, anim, frame, true) && g_art.ok() && anim < g_art.numAnimations())
                drawSprite(anim, frame, screenXOf(r.pixelX) + 16, r.pixelY + 16 - m_camY, -1);
            continue;
        }
        if (r.id == -1 || !isDisaster(r.type)) continue;
        std::vector<WeaponDrawOp> ops;
        disasterDrawOps((int)i, true, ops);
        for (const WeaponDrawOp& op : ops)
            if (op.anim >= 0 && g_art.ok() && op.anim < g_art.numAnimations())
                drawSprite(op.anim, op.frame, screenXOf(op.x), op.y - m_camY, -1);
    }

    // Fork: ground layer (vt[30] "DrawGroundLayer", pass 1 of the
    // original renderer, under every unit body). The pads of the buildings whose
    // art carries none: Agridome sub_401970 (0x87E/0x87F/0x880 by state), both
    // Storage Tanks sub_409590 / sub_409840 (0x881/0x882/0x883), and the mines
    // -- Common/Rare Ore Mine and Magma Well share sub_44B280 (0x747, frame 0).
    // Drawn at the unit's position like the body, except while dying (0x2F) or
    // being built (0x21).
    setArtShadows(m_stationaryShadows);
    for (size_t i = 1; i < g_units.size(); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id != -1 && r.type == 0x52) {
            // Magma Vent (sub_4057F0): animation 0x2A2, frame tick % last frame,
            // shaded by day/night like the terrain; hidden once a Magma Well
            // (0x2A) stands on its tile.
            const int tx = (unitOX(r) >> 5) & (g_gameMap.width() - 1), ty = unitOY(r) >> 5;
            const int occ = g_gameMap.cellOccupant(tx, ty);
            if (occ > 0 && occ < static_cast<int>(g_units.size()) && g_units[occ].id != -1 &&
                g_units[occ].type == 0x2A) continue;
            const int anim = 0x2A2 - 0x65;
            if (!g_art.ok() || anim >= g_art.numAnimations()) continue;
            const int last = static_cast<int>(g_art.animation(anim).frames.size()) - 1;
            const int dark = TethysGame::daylightEverywhere() ? 0 : tileDarkness(tx);
            blitArtFrame(renderer.getFramebuffer(), m_fbW, m_fbH, anim,
                         last > 0 ? static_cast<int>(TethysGame::tick() % last) : 0,
                         screenXOf(unitOX(r)), unitOY(r) - m_camY, -1, 0, 0, viewportW(), viewportH(), dark);
            continue;
        }
        if (r.id == -1 || (r.flags & UNIT_DEAD) || !(r.flags & UNIT_BUILDING)) continue;
        if (r.action == 0x2F || r.action == 0x21) continue;
        const int st = static_cast<int>((r.state34 >> 16) & 0xFFFF);
        auto byState = [st](int base) {
            return (st == 0 || st >= 5) ? base : st == 1 ? base + 1 : st == 2 ? base + 2 : 0;
        };
        int raw = 0;
        if (r.type == 0x27) raw = byState(0x87E);                                   // Agridome
        else if (r.type == 0x19 || r.type == 0x1A) raw = byState(0x881);            // Storage Tanks
        else if (r.type == 0x15 || r.type == 0x16 || r.type == 0x2A) raw = 0x747;    // mines
        if (!raw || !g_art.ok() || raw - 0x65 >= g_art.numAnimations()) continue;
        const int ow = r.owner & 0x0F;
        const int pc = (ow < kMaxPlayers) ? g_playerData[ow].colorNumber : 0;
        drawSprite(raw - 0x65, 0, screenXOf(unitOX(r)), unitOY(r) - m_camY, pc);
    }

    // Tres passes: ESTRUTURAS primeiro (ficam por baixo), depois veiculos e por
    // fim as unidades-efeito (explosoes) por cima.
    for (int pass = 0; pass < 3; ++pass)
    for (size_t i = 1; i < g_units.size(); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id == -1 || (r.flags & UNIT_DEAD)) continue;
        const bool isBld = (r.flags & UNIT_BUILDING) != 0;
        const bool isEffect = !(r.flags & (UNIT_BUILDING | UNIT_VEHICLE));
        if (pass != (isBld ? 0 : isEffect ? 2 : 1)) continue;
        // Sombras pela opção do INI da classe (dword_575634 / dword_575638).
        setArtShadows(isBld ? m_stationaryShadows : m_mobileShadows);
        // Centro da unidade na tela = [+0x14]/[+0x18] do original (unitOX/OY:
        // canto do tile + 16; prédio só nas dimensões ímpares — sub_4369E0).
        // Usa a posição INTERPOLADA (não o tile), p/ casar com o deslize.
        int ucx = screenXOf(unitOX(r));       // wrap horizontal do mapa
        int ucy = unitOY(r) - m_camY;

        bool drew = false;
        if ((r.flags & UNIT_VEHICLE) && hasVehicleAnim(r.type)) {
            int ow = r.owner & 0x0F;
            bool eden = (ow >= 0 && ow < kMaxPlayers) ? (g_playerData[ow].isEden != 0) : true;
            int anim = vehicleAnimIndex(r.type, r.rotation, eden);   // resolver fiel (facção)
            if (r.type == mapCargoTruck) {               // vt[0x80] sub_4062B0 (carga/doca)
                const int raw = truckAnimRaw(r, ((r.rotation + 8) & 0xF0) >> 4);
                if (raw) anim = raw - 0x65;
            }
            if (anim >= 0) {
                int nf = g_art.ok() && anim < g_art.numAnimations()
                         ? (int)g_art.animation(anim).frames.size() : 0;
                // Frame — porta sub_44C5C0: fica em 0 (parado) a menos que a
                // unidade esteja em MOVIMENTO ([+0x22]==1) ou em acao 0x1C/0x1D;
                // ai cicla com o relogio: ((clock >> flag) + [unit+0x10]) % nFrames.
                int frame = 0;
                if (isTankChassis(r.type)) {
                    // sub_484A90 (tanques): só em movimento; (id + tick/2) % nF.
                    if (r.field22 == 1 && nf > 0) frame = (((int)i + (int)(TethysGame::tick() >> 1)) % nf + nf) % nf;
                } else if (r.type == mapRoboSurveyor || r.type == mapRoboMiner || r.type == mapGeoCon ||
                           r.type == mapRoboDozer || r.type == mapEvacuationTransport ||
                           r.type == mapRepairVehicle || r.type == mapEarthworker) {
                    // vt[8] sub_44C7B0 (Robo-Surveyor/Miner/Dozer, GeoCon, Evacuation
                    // Transport, Repair, Earthworker; o caminhão em truckFrame — tipos
                    // tirados das vtables): cicla com QUALQUER ação
                    // ([+0x21] != 0) — parado trabalhando também (o bico do
                    // Earthworker, a lâmina do Dozer). Doca (0x0D): conta do início.
                    if (r.field22 == 0x0D && nf > 0) {
                        const int f = (TethysGame::tick() - r.dockTick) & 0xFFFF;
                        frame = f < nf - 1 ? f : nf - 2 > 0 ? nf - 2 : 0;
                    } else if (r.action && nf > 0) {
                        const int t = (int)(TethysGame::tick() >> ((r.flags & 0x10000) ? 1 : 0)) + (int)i;
                        frame = ((t % nf) + nf) % nf;
                    }
                } else {
                    bool animating = (r.field22 == 1) || (r.action == 0x1D) || (r.action == 0x1C);
                    if (animating && nf > 0) {
                        int shift = (r.flags & 0x10000) ? 1 : 0;
                        int t = (TethysGame::tick() >> shift) + (int)i;   // [unit+0x10] = id
                        frame = ((t % nf) + nf) % nf;
                    }
                }
                // Cor do dono: campo +0x30 do registro do jogador (colorNumber),
                // que indexa as rampas de 24 cores de color.bmp (sub_4A8D70).
                if (r.type == mapCargoTruck) frame = truckFrame(r, (int)i);   // vt[8] sub_44C7B0
                int pc = (ow >= 0 && ow < kMaxPlayers) ? g_playerData[ow].colorNumber : 0;
                drew = drawSprite(anim, frame, ucx, ucy, pc);
                if (drew) recDeco((int)i, ucx, ucy, anim, frame);
                // Torre (sub_484CF0): anim da arma p/ a direção da torre [+0x2D],
                // frame 0, no ponto de encaixe do frame atual do chassi.
                int tAnim, tx, ty;
                if (drew && tankTurret(r, anim, frame, tAnim, tx, ty) && tAnim >= 0 &&
                    tAnim < g_art.numAnimations())
                    drawSprite(tAnim, 0, ucx + tx, ucy + ty, pc);
                // Overlays (sub_44C910 / fim de sub_484CF0), fora da doca: espuma
                // 0x2FE (contador [+0x54] < nF-1) / 0x2FD (endurecida, pelo tempo
                // desde [+0x60]), ESG 0x619 e EMP 0x2FF ((id + tick) % nF).
                if (drew && r.field22 != 0x0D && g_art.ok()) {
                    auto lastF = [](int raw) {
                        const int a = raw - 0x65;
                        return a < g_art.numAnimations() ? (int)g_art.animation(a).frames.size() - 1 : 0;
                    };
                    const int tick = (int)TethysGame::tick();
                    if (r.flags & 0x40000u) {
                        const int n = lastF(0x2FE);
                        if (r.foamTicks < n) drawSprite(0x2FE - 0x65, n - r.foamTicks, ucx, ucy, -1);
                        else {
                            const int cap = lastF(0x2FD) - 1;
                            const int f = static_cast<int>((static_cast<unsigned>(tick - r.foamStart) & 0xFFFEu) >> 1);
                            drawSprite(0x2FD - 0x65, f < cap ? f : cap, ucx, ucy, -1);
                        }
                    }
                    if (r.flags & 0x100000u) drawSprite(0x619 - 0x65, (int)((unsigned)((int)i + tick) % (unsigned)(lastF(0x619) + 1)), ucx, ucy, -1);
                    if (r.flags & UNIT_EMP) drawSprite(0x2FF - 0x65, (int)((unsigned)((int)i + tick) % (unsigned)(lastF(0x2FF) + 1)), ucx, ucy, -1);
                }
            }
        }
        if (isBld) {
            // Estrutura -- porta sub_408BE0: anim = tabela[estado] (sub_4835A0/
            // sub_483720), frame = sub_409220. Estado 0 com contador 0 = ultimo
            // frame (predio pronto); construcao, dano (1/2) e destruicao (3) vem
            // da maquina de estados (src/game/structure.cpp).
            int ow = r.owner & 0x0F;
            int anim = structureCurAnim(r);
            int gpAnim;
            const bool guardPost = guardPostSprite(r, gpAnim);     // vt[8] sub_42D250
            if (guardPost) anim = gpAnim;
            if (anim >= 0 && g_art.ok() && anim < g_art.numAnimations()) {
                int pc = (ow >= 0 && ow < kMaxPlayers) ? g_playerData[ow].colorNumber : 0;
                const int bldFrame = guardPost ? 0 : structureFrame(r);
                drew = drawSprite(anim, bldFrame, ucx, ucy, pc);
                if (drew) recDeco((int)i, ucx, ucy, anim, bldFrame);
                // EMP / ESG (0x180000) no prédio (sub_408DB0): grade de 16 px no
                // footprint; cada ponto aparece se rnd(100) > 80 (40 com lado 1),
                // com a RNG semeada por (tick>>5) + id + [+0x18] + 0x11790718.
                if ((r.flags & 0x180000u) && (!guardPost || (r.flags & UNIT_EMP))) {   // GP: só EMP (sub_42D300)
                    const StructSheetDesc* sd = structSheet(r.type);
                    const int sx = sd ? sd->sizeX : 1, sy = sd ? sd->sizeY : 1;
                    const int oraw = (r.flags & UNIT_EMP) ? 0x2FF : 0x619;
                    const int nF = (int)g_art.animation(oraw - 0x65).frames.size();
                    GameRNG rng;
                    rng.setSeed((uint32_t)((int)(TethysGame::tick() >> 5) + (int)i + unitOY(r) + 0x11790718));
                    const int thr = (sx == 1 || sy == 1) ? 0x28 : 0x50;
                    int px = screenXOf(((unitOX(r) & ~0x1F) - (sx / 2) * 32 + 16));
                    const int py0 = (unitOY(r) & ~0x1F) - (sy / 2) * 32 - m_camY + 16;
                    for (int gx = 0; gx < 2 * sx - 1; ++gx, px += 16) {
                        int py = py0;
                        for (int gy = 0; gy < 2 * sy - 1; ++gy, py += 16) {
                            if (rng.getRand(100) <= thr) continue;
                            const int f = (rng.getRand(100) + (int)(TethysGame::tick() >> 1)) % (nF > 0 ? nF : 1);
                            drawSprite(oraw - 0x65, f, px, py, -1);
                        }
                    }
                }
                // Luz de status (fim de sub_408DB0, opção "Overlay"): prédio de
                // um aliado do jogador local, fora de desmonte/0x60000: 0x96 em
                // idle (ação 0x12), 0x94 operando, 0x95 parado; no canto do bbox
                // do sprite + 3; frame = tick % nF.
                const int me = TethysGame::localPlayer();
                if (guardPost) {
                    // sub_42D300: a luz só quando NÃO opera — 0x96 parado (0x12) / 0x95 —
                    // em (x − 3, y − 3).
                    if (m_overlay && ow < kMaxPlayers && (g_playerData[me].allyMask & (1u << ow)) &&
                        !structureOperational(r)) {
                        const int la = (r.action == 0x12 ? 0x96 : 0x95) - 0x65;
                        const int nF = (int)g_art.animation(la).frames.size();
                        drawSprite(la, nF ? TethysGame::tick() % nF : 0, ucx - 3, ucy - 3, -1);
                    }
                } else if (m_overlay && ow < kMaxPlayers && (g_playerData[me].allyMask & (1u << ow)) &&
                    r.action != ACT_DISMANTLE && !(r.flags & 0x60000u)) {
                    const int light = r.action == 0x12 ? 0x96 : structureOperational(r) ? 0x94 : 0x95;
                    const uint32_t* h = g_art.animation(anim).header;
                    const int la = light - 0x65;
                    if (la < g_art.numAnimations()) {
                        const int nF = static_cast<int>(g_art.animation(la).frames.size());
                        drawSprite(la, nF ? TethysGame::tick() % nF : 0,
                                   ucx - static_cast<int>(h[5] - h[1]) + 3, ucy - static_cast<int>(h[6] - h[2]) + 3, -1);
                    }
                }
            }
        }
        if (isEffect && r.type == 0x51) {
            // Beacon de mineração (sub_405310): pesquisado pelo local → anim pelo
            // rendimento (raro 0x73F/0x73E/0x73D, comum 0x73C/0x73B/0x73A); senão
            // 0x740. Frame = tick % nF.
            int raw = 0x740;
            if (beaconSurveyed(static_cast<int>(i), TethysGame::localPlayer())) {
                static const int kRare[3] = {0x73F, 0x73E, 0x73D}, kCommon[3] = {0x73C, 0x73B, 0x73A};
                const int y = r.beaconYield >= 0 && r.beaconYield <= 2 ? r.beaconYield : 0;
                raw = r.beaconRare ? kRare[y] : kCommon[y];
            }
            const int anim = raw - 0x65;
            if (g_art.ok() && anim < g_art.numAnimations()) {
                const int nf = static_cast<int>(g_art.animation(anim).frames.size());
                drawSprite(anim, nf ? TethysGame::tick() % nf : 0, ucx, ucy, -1);
            }
            continue;
        }
        if (isEffect && r.type == 0x53) {
            // Fumarola (vt[6] sub_405840): some sob um Geothermal Plant (0x2C) ou
            // com uma unidade no tile; frame = tick % último frame da anim 0x9F; a
            // cada 100 ticks, gêiser 0x3C (o som sai no desenho → só visível).
            const int tx = (unitOX(r) >> 5) & (g_gameMap.width() - 1), ty = unitOY(r) >> 5;
            const int occ = g_gameMap.cellOccupant(tx, ty);
            if (occ > 0 && occ < static_cast<int>(g_units.size())) {
                const UnitRecord& o = g_units[occ];
                if (o.id != -1 && (o.type == 0x2C ||
                    (((unitOX(o) >> 5) & (g_gameMap.width() - 1)) == tx && (unitOY(o) >> 5) == ty)))
                    continue;
            }
            const int anim = 0x9F - 0x65;
            if (g_art.ok() && anim < g_art.numAnimations()) {
                const int last = static_cast<int>(g_art.animation(anim).frames.size()) - 1;
                drawSprite(anim, last > 0 ? TethysGame::tick() % last : 0, ucx, ucy, -1);
            }
            if (TethysGame::tick() % 100 == 0 && onScreen(ucx, ucy)) drawSound(static_cast<int>(i), unitOX(r), unitOY(r), 0x3C);
            continue;
        }
        if (isEffect && r.type == 0x50 && (r.flags & 0x2000u) && (r.flags & 0x40000u) &&
            r.dsAux > 0 && r.dsAux < static_cast<int>(g_units.size()) && onScreen(ucx, ucy)) {
            // sub_44A9A0: ao desenhar o laser da defesa, 0x47 na defesa e 0x4E no
            // meteoro destruído (0x10000).
            const UnitRecord& d = g_units[r.dsAux];
            drawSound(static_cast<int>(i), unitOX(d), unitOY(d), 0x47);
            if (r.flags & 0x10000u) drawSound(static_cast<int>(i), unitOX(r), unitOY(r), 0x4E);
        }
        if (isEffect && (isLaunchUnit(r.type) || isEmpBlast(r.type))) {
            // Lançamento do Spaceport (sub_4805F0) / anel da EMP_BIG (sub_413BC0).
            std::vector<WeaponDrawOp> ops;
            missileDrawOps((int)i, ops);
            for (const WeaponDrawOp& op : ops) {
                if (op.kind == WeaponDrawOp::SPRITE) {
                    if (op.anim >= 0 && g_art.ok() && op.anim < g_art.numAnimations())
                        drawSprite(op.anim, op.frame, screenXOf(op.x), op.y - m_camY, -1);
                } else {
                    drawBeam(renderer, op);
                    // Ao desenhar o laser: 0x47 na defesa, 0x4E no míssil derrubado.
                    if (onScreen(ucx, ucy)) {
                        const UnitRecord& d = g_units[r.launchDefense];
                        drawSound(static_cast<int>(i), unitOX(d), unitOY(d), 0x47);
                        if (r.flags & 0x10000u) drawSound(static_cast<int>(i), r.pixelX, r.pixelY, 0x4E);
                    }
                }
            }
            continue;
        }
        if (isEffect && isDisaster(r.type)) {
            // Meteoro / raio / tornado (vt[6]); terremoto e erupção não desenham.
            std::vector<WeaponDrawOp> ops;
            disasterDrawOps((int)i, false, ops);
            for (const WeaponDrawOp& op : ops) {
                if (op.kind == WeaponDrawOp::SPRITE) {
                    if (op.anim >= 0 && g_art.ok() && op.anim < g_art.numAnimations())
                        drawSprite(op.anim, op.frame, screenXOf(op.x), op.y - m_camY, -1);
                } else {
                    drawBeam(renderer, op);
                }
            }
            continue;
        }
        if (isEffect && isWeaponProjectile(r.type)) {
            // Projétil/explosão de arma (vt[6]: sub_4A3420 / sub_4A3000 /
            // sub_4A2A00 / sub_4A2D50) em coordenadas de mundo do original.
            std::vector<WeaponDrawOp> ops;
            weaponDrawOps((int)i, ops);
            for (const WeaponDrawOp& op : ops) {
                if (op.kind == WeaponDrawOp::SOUND) {                // som tocado pelo desenho (ácido)
                    if (onScreen(ucx, ucy)) drawSound(static_cast<int>(i), op.x, op.y, op.anim);
                } else if (op.kind == WeaponDrawOp::SPRITE) {
                    if (op.anim >= 0 && g_art.ok() && op.anim < g_art.numAnimations())
                        drawSprite(op.anim, op.frame, screenXOf(op.x), op.y - m_camY, -1);
                } else {
                    drawBeam(renderer, op);
                }
            }
            continue;
        }
        if (isEffect && r.type == 0x54) {
            // Destroço (sub_405A50): 0x7A5 com a cor do jogador 1, se descoberto
            // por um aliado do jogador local.
            int raw, frame;
            if (wreckSprite(static_cast<int>(i), raw, frame) && g_art.ok() && raw - 0x65 < g_art.numAnimations())
                drawSprite(raw - 0x65, frame, ucx, ucy, g_playerData[1].colorNumber);
            continue;
        }
        if (isEffect) {
            // Unidade-efeito AMBIENT_ANIM (sub_405160): sem remap de cor salvo
            // UNIT_EFFECT_COLOR; invisivel antes do atraso.
            int anim, frame;
            if (effectAnimFrame(r, anim, frame) && g_art.ok() && anim < g_art.numAnimations())
                drawSprite(anim, frame, ucx, ucy,
                           (r.flags & UNIT_EFFECT_COLOR) ? r.effectColor : -1);
            continue;   // efeitos nao tem marcador nem barrinha
        }
        if (!drew) {
            // Fallback: marcador colorido (prédios/tipos sem sprite ainda).
            uint32_t col = kOwnerColor[(r.owner & 0x0F) % 7];
            int sz = (r.flags & UNIT_BUILDING) ? 26 : 16;
            renderer.fillRect(ucx - sz / 2, ucy - sz / 2, sz, sz, col);
            renderer.drawRect(ucx - sz / 2, ucy - sz / 2, sz, sz, 0xFF101010);
        }
    }

    // Colchete + barra de vida (sub_43E950) das unidades selecionadas.
    const int vw = viewportW(), vh = viewportH();
    uint32_t* fb = renderer.getFramebuffer();
    auto put = [&](int x, int y, uint32_t c) {         // 1 px recortado à vista
        if (x >= 0 && x < vw && y >= 0 && y < vh) fb[y * m_fbW + x] = c;
    };
    auto hseg = [&](int x, int y, int len, uint32_t c) {   // len<0 = p/ a esquerda
        const int s = len < 0 ? -1 : 1;
        for (int k = 0; k <= (len < 0 ? -len : len); ++k) put(x + k * s, y, c);
    };
    // sub_41F1C0: tinge um tile de 32x32 misturando 50% com o terreno, em 5 bits
    // por canal (dst = (dst>>1 & 0x3DEF) + (cor>>1 & 0x3DEF)).
    auto tintTile = [&](int px, int py, unsigned short c) {
        const int r5 = (c >> 10) & 31, g5 = (c >> 5) & 31, b5 = c & 31;
        for (int yy = py; yy < py + 32; ++yy) {
            if (yy < 0 || yy >= vh) continue;
            for (int xx = px; xx < px + 32; ++xx) {
                if (xx < 0 || xx >= vw) continue;
                uint32_t& d = fb[yy * m_fbW + xx];
                const int dr = ((d >> 19) & 31) >> 1, dg = ((d >> 11) & 31) >> 1,
                          db = ((d >> 3) & 31) >> 1;
                d = 0xFF000000u | (static_cast<uint32_t>((dr + (r5 >> 1)) << 3) << 16) |
                    (static_cast<uint32_t>((dg + (g5 >> 1)) << 3) << 8) |
                    static_cast<uint32_t>((db + (b5 >> 1)) << 3);
            }
        }
    };
    auto vseg = [&](int x, int y, int len, uint32_t c) {
        const int s = len < 0 ? -1 : 1;
        for (int k = 0; k <= (len < 0 ? -len : len); ++k) put(x, y + k * s, c);
    };
    for (const SelDeco& d : selDeco) {
        int ox, oy, w, h;
        if (!artFrameBounds(d.anim, d.frame, ox, oy, w, h)) continue;
        const int l = d.cx + ox, t = d.cy + oy, rt = l + w, bt = t + h;
        // Colchete: 4 cantos em "L", cada perna = dimensão/6 (idiv 6 do original).
        // Não é um retângulo contínuo. Seleção = branco (0x7FFF); hover = cinza
        // (0x4210, arg_0&1 no original, (128,128,128) em RGB888).
        const uint32_t CLR = d.hover ? 0xFF808080u : 0xFFFFFFFFu;
        const uint32_t WHITE = 0xFFFFFFFFu;            // moldura da barra
        const int lx = w / 6, ly = h / 6;
        hseg(l, t, lx, CLR);     vseg(l, t, ly, CLR);     // superior-esquerdo
        hseg(rt, t, -lx, CLR);   vseg(rt, t, ly, CLR);    // superior-direito
        hseg(l, bt, lx, CLR);    vseg(l, bt, -ly, CLR);   // inferior-esquerdo
        hseg(rt, bt, -lx, CLR);  vseg(rt, bt, -ly, CLR);  // inferior-direito

        // Barra de vida acima do colchete. maxHP por jogador (planilha), dano
        // em +0x1E; cor por faixa: >=40% verde (0x3E0), >25% amarelo (0x7FE0),
        // <=25% vermelho (0x7C00) piscando com o bit 1 do relógio.
        if (d.id <= 0 || d.id >= (int)g_units.size()) continue;
        const UnitRecord& r = g_units[d.id];
        const int pl = (r.owner >> 4) & 0x0F;
        int maxHP = 0;
        if (r.flags & UNIT_VEHICLE) { const VehPlayerStats* v = vehicleSheet(r.type, pl); maxHP = v ? v->hitPoints : 0; }
        else if (r.flags & UNIT_BUILDING) { const StructSheetDesc* ss = structSheet(r.type); maxHP = ss ? ss->player[pl].hitPoints : 0; }
        if (maxHP <= 0) continue;
        int dmg = r.damage; if (dmg < 0) dmg = 0; if (dmg > maxHP) dmg = maxHP;
        const int hpRest = maxHP - dmg;
        uint32_t barCol;                               // conversão RGB555→888 (31→255)
        bool draw = true;
        if (hpRest * 10 >= maxHP * 4) barCol = 0xFF00FF00u;       // verde  (>= 40%)
        else if (hpRest * 4 > maxHP) barCol = 0xFFFFFF00u;        // amarelo (> 25%)
        else { barCol = 0xFFFF0000u; draw = (TethysGame::tick() & 2) != 0; }  // vermelho piscando
        const int barW = (w > 56) ? 48 : 24;           // var_188: 0x30 se largo, senão 0x18
        const int bx = (l + rt) / 2 - barW / 2;        // centrado na unidade
        const int by = t - 7;                          // var_1B0 = topo do colchete - 7
        // Moldura: laterais brancas, topo e base pretas (6 px de altura).
        vseg(bx, by, 6, WHITE);  vseg(bx + barW + 2, by, 6, WHITE);
        hseg(bx, by, barW + 2, 0xFF000000u);  hseg(bx, by + 6, barW + 2, 0xFF000000u);
        if (draw) {                                    // preenchimento proporcional
            const int fillW = hpRest * barW / maxHP;
            for (int yy = by + 1; yy <= by + 5; ++yy)
                for (int xx = bx + 1; xx <= bx + fillW; ++xx) put(xx, yy, barCol);
        }
    }
    // Modo de posicionamento do kit (sub_451AC0 → sub_41B5B0: retângulo do
    // tamanho da área no cursor). sub_41BC50 pergunta a cor de cada tile ao
    // comando (vt+0x14) e TINGE o tile inteiro com ela, misturando 50% com o
    // terreno (sub_41F1C0: dst = (dst>>1 & 0x3DEF) + (cor>>1 & 0x3DEF)).
    if ((m_targetCmd == UC_BUILD_KIT || m_targetCmd == UC_BUILD_MINE) && !m_selected.empty()) {
        int w = 0, h = 0;
        const int mvx = m_mouseNX - kViewX, mvy = m_mouseNY - kViewY;
        const bool mineCmd = m_targetCmd == UC_BUILD_MINE;
        if (mineCmd) mineFootprint(w, h);                           // sub_451FC0: off_4E1394
        else kitFootprint(m_selected[0], w, h);
        if (w && mvx >= 0 && mvx < viewportW() && mvy >= 0 && mvy < viewportH()) {
            int tx = (m_camX + mvx) / 32, ty = (m_camY + mvy) / 32, l, t, r, b;
            if (g_gameMap.ok()) tx &= (g_gameMap.width() - 1);
            kitPlacementRect(w, h, tx, ty, l, t, r, b);
            const std::vector<unsigned short> col =
                placementTileColors(m_selected[0], mineCmd, l, t, w, h);
            for (int j = 0; j < h + 2; ++j)
                for (int i = 0; i < w + 2; ++i)
                    tintTile(screenXOf(((l + i) & (g_gameMap.ok() ? g_gameMap.width() - 1 : 0x7FFFFFFF)) * 32),
                             (t + j) * 32 - m_camY, col[static_cast<size_t>(j) * (w + 2) + i]);
        }
    }
    // Área em arrasto (muro/tubo: reta de até 8 tiles; terraplanar: retângulo).
    // sub_41BDC0: cada tile é tingido conforme o vt+0x10 do comando — 0x7FD0 se
    // aceita, 0x7C00 se não — e a área inteira ganha um contorno 0x3F (azul),
    // que é o sub_41C080 do fim da rotina.
    if (m_areaDrag && m_targetCmd >= 0) {
        int x1, y1, x2, y2;
        areaRect(m_camX + m_mouseNX - kViewX, m_camY + m_mouseNY - kViewY, x1, y1, x2, y2);
        const int wm = g_gameMap.ok() ? g_gameMap.width() - 1 : 0x7FFFFFFF;
        int xe = x2 - 1; if (xe < x1) xe += g_gameMap.ok() ? g_gameMap.width() : 0;
        for (int x = x1; x <= xe; ++x)
            for (int y = y1; y <= y2 - 1; ++y)
                tintTile(screenXOf((x & wm) * 32), y * 32 - m_camY,
                         uiCommandCellOk(m_targetCmd, m_selected, x & wm, y) ? 0x7FD0 : 0x7C00);
        const int sx = screenXOf(x1 * 32), sy = y1 * 32 - m_camY;
        renderer.drawRect(sx, sy, (x2 - x1) * 32, (y2 - y1) * 32, 0xFF0008F8);   // 0x3F
    }
    // Caixa de arrasto (multi-seleção).
    if (m_dragging) {
        const int mvx = m_mouseNX - kViewX, mvy = m_mouseNY - kViewY;   // mouse na vista
        int x0 = std::min(m_dragSX, mvx), y0 = std::min(m_dragSY, mvy);
        int w = std::abs(mvx - m_dragSX), h = std::abs(mvy - m_dragSY);
        renderer.drawRect(x0, y0, w, h, 0xFF40FF40);
    }

    // Painel de comando (a "MAINE"): minimapa + info + botões.
    // Fim de jogo: o letreiro do controle TFrame+0x1F4, que o sub_49CAC0 enche
    // com o texto do código e o sub_4085C0 desenha. Reproduzido como lá:
    //   * a fonte é criada uma vez (sub_4084D0) com lfHeight **0x30** e peso
    //     0x2BC — Arial negrito de corpo 48, não a fontinha de depuração;
    //   * `SetBkMode(TRANSPARENT)` e `DrawText` com `DT_CENTER`, centrado
    //     também na vertical pela altura medida com `DT_CALCRECT`;
    //   * CINCO passadas: `SetTextColor(0)` e o texto nos quatro cantos
    //     diagonais de ±2 px — que é o contorno —, e por fim
    //     `SetTextColor(0xFF)`, ou seja **VERMELHO**, no lugar certo.
    // O mesmo controle serve aos dois: o fim de missao tem preferencia (e quem
    // o preenche por ultimo, no sub_49CAC0), e a pausa vem depois.
    if (gameEndCode()) drawBanner(renderer, gameEndText(gameEndCode()));
    else if (gTApp.paused()) drawBanner(renderer, "Game Paused. Press CTRL+P to Resume");
    // vt[6] do terremoto (sub_413560): tremor p/ o próximo quadro, pelo centro da vista.
    disasterShake(m_camX + (viewportW() / 32) * 16,
                  m_camY + (viewportH() / 32) * 16,
                  m_frameSkip, m_shakeX, m_shakeY);
    // A vista foi desenhada na origem: vai p/ o seu lugar na área cliente
    // (12,22); o resto da tela é a moldura e a coluna direita (renderPanel).
    {
        uint32_t* fb = renderer.getFramebuffer();
        const int VW = m_fbW;
        for (int y = kViewH - 1; y >= 0; --y)
            std::memmove(&fb[(y + kViewY) * VW + kViewX], &fb[y * VW], sizeof(uint32_t) * kViewW);
    }
    renderPanel(renderer);
    drawChatLine(renderer.getFramebuffer(), m_fbW, m_fbH);

    // Overlay: relógio (Tick/Time) + uma amostra do RNG — prova visível de que o
    // núcleo de simulação está rodando determinístico.
    if (m_showDebugHud) {
    uint32_t* fb = renderer.getFramebuffer();
    const int VW = m_fbW, VH = m_fbH;
    const _Player& p = Player[TethysGame::localPlayer()];

    char l1[96], l2[128];
    std::snprintf(l1, sizeof(l1), "Tick %d   Time %d   Rand %d",
                  TethysGame::tick(), TethysGame::time(), m_lastRand);
    std::snprintf(l2, sizeof(l2),
                  "%s   Ore %d  Rare %d  Food %d   W%d S%d K%d",
                  p.IsEden() ? "EDEN" : "PLYMOUTH",
                  p.Ore(), p.RareOre(), p.FoodStored(),
                  p.Workers(), p.Scientists(), p.Kids());
    int liveUnits = 0;
    for (size_t i = 1; i < g_units.size(); ++i) { Unit u((int)i); if (u.IsLive()) ++liveUnits; }
    char l3[128];
    std::snprintf(l3, sizeof(l3), "%s   Units %d   (setas rolar | btn-dir mover | G passab | ESC menu)",
                  m_mapName.c_str(), liveUnits);
    // Economia (sub_471F30): energia gerada/exigida, mão de obra sobrando e
    // prédios operando/total do jogador local.
    const ColonyState& cs = g_colony[TethysGame::localPlayer()];
    char l4[128];
    std::snprintf(l4, sizeof(l4), "Energia %d/%d (sobra %d)   Trab sobra %d  Cient sobra %d   Operando %d/%d",
                  cs.powerGenerated, cs.powerRequired, cs.powerLeft,
                  cs.workersLeft, cs.scientistsLeft, cs.activeCount, cs.structCount);
    Font::drawText(fb, VW, VH, l1, 8, 8, 0xFFFFE060, true);
    Font::drawText(fb, VW, VH, l2, 8, 20, 0xFF80E0A0, true);
    Font::drawText(fb, VW, VH, l3, 8, 32, 0xFFB0C4DE, true);
    Font::drawText(fb, VW, VH, l4, 8, 44, 0xFFE0C080, true);
    }

    if (m_endPhase == 2 && !m_saveDlg.active()) drawSuccessDialog(renderer);
    if (m_saveDlg.active()) {
        // O diálogo tem coordenadas fixas de 640x480 (é o mesmo do shell). Num
        // quadro maior ele é desenhado no lugar de sempre e o retângulo dele é
        // levado para o centro, devolvendo o que estava por baixo.
        const int ox = (m_fbW - 640) / 2, oy = (m_fbH - 480) / 2;
        if (ox == 0 && oy == 0) {
            m_saveDlg.render(renderer);
        } else {
            constexpr int W = SaveSlotsDialog::kDlgW, H = SaveSlotsDialog::kDlgH;
            constexpr int dx = (640 - W) / 2, dy = (480 - H) / 2;
            uint32_t* fb = renderer.getFramebuffer();
            std::vector<uint32_t> under(W * H), dlg(W * H);
            for (int y = 0; y < H; ++y)
                std::memcpy(&under[y * W], &fb[(dy + y) * m_fbW + dx], W * sizeof(uint32_t));
            m_saveDlg.render(renderer);
            for (int y = 0; y < H; ++y) {
                std::memcpy(&dlg[y * W], &fb[(dy + y) * m_fbW + dx], W * sizeof(uint32_t));
                std::memcpy(&fb[(dy + y) * m_fbW + dx], &under[y * W], W * sizeof(uint32_t));
            }
            for (int y = 0; y < H; ++y)
                std::memcpy(&fb[(dy + oy + y) * m_fbW + dx + ox], &dlg[y * W], W * sizeof(uint32_t));
        }
    }
    renderer.present();
}

int GameState::viewportW() const { return kViewW; }

// Feixes das armas instantâneas: Bresenham com bloco 2x2 por ponto, como
// sub_41E8F0 (laser: OR 0x7C00 = vermelho máximo em RGB555) e loc_41ECE0
// (microwave: padrão 0xFE1FC3F8 girado pela fase; cor 0xD69FD69E que perde
// 0x30003 a cada ponto aceso e volta ao início a cada falha do padrão).
void GameState::drawBeam(Renderer& renderer, const WeaponDrawOp& op) {
    uint32_t* fb = renderer.getFramebuffer();
    const int VW = m_fbW, clipW = viewportW(), clipH = viewportH();
    auto c555 = [](uint32_t w) {
        const uint32_t r = (w >> 10) & 31, g = (w >> 5) & 31, b = w & 31;
        return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
    };
    auto to555 = [](uint32_t p) {
        return static_cast<uint16_t>((((p >> 19) & 31) << 10) | (((p >> 11) & 31) << 5) | ((p >> 3) & 31));
    };
    if (op.kind == WeaponDrawOp::BOLT) {                           // sub_48AD70
        weaponDrawBolt(op, m_camX, m_camY, clipW, clipH, [&](int x, int y, uint16_t c) {
            if (x < 0 || x >= clipW || y < 0 || y >= clipH) return;
            fb[y * VW + x] = c555(c);
        });
        return;
    }
    auto put = [&](int x, int y, uint32_t argb, bool orCol) {
        if (x < 0 || x >= clipW || y < 0 || y >= clipH) return;
        uint32_t& px = fb[y * VW + x];
        px = orCol ? c555(to555(px) | op.orColor) : argb;
    };
    int x0 = screenXOf(op.x), y0 = op.y - m_camY;
    int x1 = x0 + (screenXOf(op.x2) - screenXOf(op.x)), y1 = op.y2 - m_camY;
    const bool laser = op.kind == WeaponDrawOp::LASER_LINE;
    uint32_t pat = 0xFE1FC3F8u;
    if (op.phase & 31) pat = (pat << (op.phase & 31)) | (pat >> (32 - (op.phase & 31)));
    const uint32_t start = 0xD69FD69Eu;
    uint32_t col = start;
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (laser) {
            put(x0, y0, 0, true); put(x0 + 1, y0, 0, true);
            put(x0, y0 + 1, 0, true); put(x0 + 1, y0 + 1, 0, true);
        } else if (pat & 1) {
            const uint32_t lo = c555(col & 0xFFFF), hi = c555(col >> 16);
            put(x0, y0, lo, false); put(x0 + 1, y0, hi, false);
            put(x0, y0 + 1, lo, false); put(x0 + 1, y0 + 1, hi, false);
            col -= 0x30003u;
        } else {
            col = start;
        }
        if (x0 == x1 && y0 == y1) break;
        pat = (pat << 1) | (pat >> 31);
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// sub_437250 na superficie do globo ([+0x10A4]): a largura INTERNA do mapa por
// altura + 2 margens de altura/8, em RGB555. E a textura que a esfera le.
void GameState::buildGlobeTerrain() {
    if (!g_gameMap.ok()) return;
    const int w = g_gameMap.internalWidth(), h = g_gameMap.height();
    const int pad = m_mini.padY();
    m_globeTex.assign(static_cast<size_t>(w) * (h + 2 * pad), 0);
    for (int ty = 0; ty < h; ++ty) {
        uint16_t* line = &m_globeTex[static_cast<size_t>(ty + pad) * w];
        for (int tx = 0; tx < w; ++tx) {
            const uint32_t cell = g_gameMap.rawCell(tx, ty);
            line[tx] = (m_mini.tactical() && !(cell & 0x08000000u))
                     ? terrainTacticalColor(static_cast<int>(cell & 0x1F))
                     : m_terrain.minimapColor(g_gameMap.getTile(LOCATION{tx, ty}));
        }
    }
}

int GameState::globeSpinTargetOf(int camX) const {
    const int w = g_gameMap.ok() ? g_gameMap.internalWidth() : 1;
    return static_cast<int>((static_cast<long long>(camX) << 16) / (w * 32)) - 0x4000;
}

// Visao do globo (sub_48DB20 -> sub_420000). As unidades vao PARA A TEXTURA,
// como em sub_48CCD0, e saem de la depois (sub_48D2B0).
void GameState::drawGlobe(uint32_t* fb) {
    if (!g_gameMap.ok()) return;
    // Refeita a cada quadro, como sub_48CBB0 faz com a superficie do minimapa:
    // o terreno muda (terraplenagem, entulho, muros) e custa o mesmo laco que o
    // minimapa comum ja percorre.
    buildGlobeTerrain();
    const int w = g_gameMap.internalWidth(), h = g_gameMap.height(), pad = m_mini.padY();
    m_globe.setTarget(kColW, kMiniMapH);
    m_globe.setSource(m_globeTex.data(), w, h + 2 * pad);
    m_globe.setRadius(50 + m_mini.zoom() * 10);     // sub_41F970(zoom*10 + 50, 1)
    m_globe.setSun(TethysGame::daylightPhase());    // [+0x48] = dword_5472A0
    m_globe.setShaded(!TethysGame::daylightEverywhere());   // sub_41F910 (dword_56DEA4)
    // dword_547290 (sub_407A10 / sub_408120): o alvo do giro e a coluna da
    // BORDA ESQUERDA da vista, reescrito SO quando a vista se move.
    if (m_camX != m_globeCamX) {
        m_globeCamX = m_camX;
        m_globe.setSpinTarget(globeSpinTargetOf(m_camX));
    }
    m_globe.tick();                                 // sub_41F9E0

    const int me = TethysGame::localPlayer();
    const bool shade = !TethysGame::daylightEverywhere();
    // No demo de atracao o inimigo aparece mesmo no escuro (ver escondeInimigo
    // no minimapa, logo abaixo): a cena fica confusa sem quem esta atirando.
    const bool escondeInimigo = shade && !demoGame();
    // As mesmas cores de dono do minimapa, em RGB555 (a textura e de 16 bits).
    static const uint16_t kOC5[7] = { 0x219F, 0x7D48, 0x234E, 0x7F48, 0x619F, 0x239C, 0x7E46 };
    m_globeDirty.clear();
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id == -1 || (r.flags & UNIT_DEAD) || !(r.flags & (UNIT_VEHICLE | UNIT_BUILDING))) continue;
        const int ow = r.owner & 0x0F;
        if ((r.flags & UNIT_VEHICLE) && ow != me && escondeInimigo) {
            const bool ally = (g_playerData[me].allyMask >> ow) & 1;
            if (!ally && !(r.flags & 1) &&
                g_gameMap.lightDarkness((r.pixelX >> 5) + g_gameMap.internalXOffset()
                                        - TethysGame::daylightPos()) >= 16)
                continue;
        }
        const int tx = (r.pixelX >> 5) & (w - 1), ty = r.pixelY >> 5;
        if (ty < 0 || ty >= h) continue;
        const size_t idx = static_cast<size_t>(ty + pad) * w + tx;
        m_globeDirty.emplace_back(static_cast<int>(idx), m_globeTex[idx]);
        m_globeTex[idx] = kOC5[ow % 7];
    }

    m_globe.draw(fb, m_fbW, kColX, kMiniMapY,
                 m_camX >> 5, m_camY >> 5,
                 (m_camX + viewportW()) >> 5, (m_camY + viewportH()) >> 5,
                 m_miniDrag);

    for (const auto& d : m_globeDirty) m_globeTex[d.first] = d.second;
}

// sub_48D860: ponto da janela do minimapa → pixel do mundo (tile << 5, o canto
// do tile), invertendo a escala/deslocamento do MiniMap. Fora do mapa (ou nos
// botões do topo, que são janelas filhas) não converte.
// Escuridao de uma coluna de TILE: byte_54EC14[(tx - dword_54729C) & mascara],
// com o tx em coordenadas internas.
int GameState::tileDarkness(int tx) {
    if (!g_gameMap.ok()) return 0;
    return g_gameMap.lightDarkness(tx + g_gameMap.internalXOffset() - TethysGame::daylightPos());
}

bool GameState::miniToWorld(int nx, int ny, int& px, int& py) const {
    if (!g_gameMap.ok()) return false;
    const int mx = nx - kColX, my = ny - kMiniMapY;
    if (mx < 0 || mx >= kColW || my < 0 || my >= kMiniMapH) return false;
    if (my < 16 && mx >= kColW / 2 - 0x20 && mx < kColW / 2 - 0x20 + 16 * miniButtonCount()) return false;
    // Com o globo ligado sub_48D860 delega a conversao a sub_420750.
    if (m_mini.globe()) return m_globe.unproject(mx, my, px, py);
    const int mw = g_gameMap.width(), mh = g_gameMap.height();
    const int sx = m_mini.scaleX(), sy = m_mini.scaleY();
    if (sx <= 0 || sy <= 0) return false;
    // px = ((tile + xoff) * escala >> 16) + desl  →  tile = ((px − desl) << 16) / escala − xoff
    const int tx = static_cast<int>(((static_cast<long long>(mx - m_mini.offX()) << 16) / sx))
                 - g_gameMap.internalXOffset();
    const int ty = static_cast<int>(((static_cast<long long>(my - m_mini.offY()) << 16) / sy))
                 - m_mini.padY();
    if (ty < 0 || ty >= mh) return false;
    px = ((tx % mw + mw) % mw) << 5;
    py = ty << 5;
    return true;
}

// sub_458930: 4 botões, menos o do globo nos mapas guardados dobrados.
int GameState::miniButtonCount() const {
    return g_gameMap.ok() && g_gameMap.internalXOffset() ? 3 : 4;
}

int GameState::screenXOf(int worldPixelX) const {
    int sx = worldPixelX - m_camX;
    if (!g_gameMap.ok() || !viewWraps()) return sx;
    const int worldW = g_gameMap.width() * 32;       // largura do mapa em pixels
    if (worldW <= 0) return sx;
    sx = ((sx % worldW) + worldW) % worldW;           // [0, worldW)
    if (sx > worldW / 2) sx -= worldW;                // → [-worldW/2, worldW/2)
    return sx;
}

// A caixa que o clique acerta, em coordenadas da vista.
//
// O PREDIO responde na pegada inteira: as mesmas `w x h` celulas que ele marca
// no mapa ao nascer (ver createUnit, que preenche a ocupacao de
// `(cx - w/2, cy - h/2)` por `w x h`). Antes o porte usava um raio de 20 px do
// centro para tudo, entao num predio de 3x3 (96 px) so o miolo respondia -- era
// preciso acertar um alvo pequeno no meio da construcao.
//
// O VEICULO responde numa caixa de 48 px em volta do centro, que e da ordem do
// sprite dele (o tile tem 32).
void GameState::unitHitBox(int id, int& x, int& y, int& w, int& h) const {
    const UnitRecord& r = g_units[id];
    if (r.flags & UNIT_BUILDING) {
        int tw = 1, th = 1;
        buildingFootprint(static_cast<map_id>(r.type), tw, th);
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        const int tlx = (r.pixelX >> 5) - (tw >> 1);
        const int tly = (r.pixelY >> 5) - (th >> 1);
        x = screenXOf(tlx << 5);
        y = (tly << 5) - m_camY;
        w = tw << 5;
        h = th << 5;
        return;
    }
    x = screenXOf(unitOX(r) - 24);
    y = unitOY(r) - 24 - m_camY;
    w = 48;
    h = 48;
}

// A unidade sob o ponto: a de caixa acertada cujo CENTRO esta mais perto (duas
// caixas podem se sobrepor).
int GameState::unitAtBox(int vx, int vy, bool meusApenas) const {
    const int me = TethysGame::localPlayer();
    int best = 0;
    long long bestD2 = 0;
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id == -1 || (r.flags & UNIT_DEAD)) continue;
        if (!(r.flags & (UNIT_VEHICLE | UNIT_BUILDING))) continue;   // efeitos não
        if (meusApenas && (r.owner & 0x0F) != me) continue;
        int bx, by, bw, bh;
        unitHitBox(i, bx, by, bw, bh);
        if (vx < bx || vx >= bx + bw || vy < by || vy >= by + bh) continue;
        const long long dx = bx + bw / 2 - vx, dy = by + bh / 2 - vy;
        const long long d2 = dx * dx + dy * dy;
        if (!best || d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

int GameState::unitAtView(int vx, int vy) const {
    return unitAtBox(vx, vy, /*meusApenas*/ false);
}

// As cinco passadas do sub_4085C0 sobre a vista, com a fonte do sub_4084D0:
// o texto em PRETO nos quatro cantos diagonais de +-2 px -- o contorno -- e
// depois em VERMELHO (o COLORREF 0xFF) no lugar certo. O retangulo do original
// e a superficie inteira do controle, com DT_CENTER e a centragem vertical
// pela altura que o DT_CALCRECT mediu, que aqui e a altura da linha.
void GameState::drawBanner(Renderer& renderer, const std::string& msg) {
    uint32_t* fb = renderer.getFramebuffer();
    const int cx = viewportW() / 2;
    const int y = (viewportH() - UiFont::lineHeight(UiFont::kBanner)) / 2;
    for (int dx = -2; dx <= 2; dx += 4)
        for (int dy = -2; dy <= 2; dy += 4)
            UiFont::drawCentered(fb, m_fbW, m_fbH, msg, cx + dx, y + dy,
                                 0xFF000000, UiFont::kBanner);
    UiFont::drawCentered(fb, m_fbW, m_fbH, msg, cx, y, 0xFFFF0000, UiFont::kBanner);
}

// sub_49BFE0: a pausa e um CONTADOR ([+0x1E0]) -- o jogo tambem se pausa
// sozinho noutras situacoes --, entao so a subida de 0 para 1 e a descida de
// volta a 0 e que fazem alguma coisa. Ao subir: para a musica (sub_450A90,
// que mata o temporizador do sequenciador), suspende o som (sub_47E6F0, que
// solta os canais que estao tocando) e avisa o TApp (OnPauseGame, que larga a
// captura do mouse). Ao descer, o caminho inverso -- e o original ainda
// devolve os campos de tempo do quadro, que aqui e o acumulador do tique: sem
// isso o jogo dispararia em camera rapida ao voltar.
void GameState::pauseGame(bool on) {
    if (on) {
        if (m_pauseCount++ != 0) return;
        musicPause();                      // sub_450A90
        soundSuspend(true);                // sub_47E6F0
        gTApp.setPaused(true);             // TApp::OnPauseGame(1)
        // ReleaseCapture: o que estiver sendo arrastado acaba aqui.
        m_dragging = false;
        m_areaDrag = false;
        m_miniDrag = false;
        return;
    }
    if (m_pauseCount <= 0 || --m_pauseCount != 0) return;
    m_tickAccum = 0.0f;                    // [+0xC0] = [+0xBC], [+0xC4] e [+0xCC] zerados
    soundSuspend(false);                   // sub_47E750
    musicResume();                         // sub_450B00
    gTApp.setPaused(false);                // TApp::OnPauseGame(0)
}

// Comando 40001 (Ctrl+P e a tecla PAUSE, no acelerador 130 do OUT2RES):
// sub_49891C. Em partida multijogador nao pausa -- so o aviso na barra. Antes
// do primeiro tique (dword_56DEDC == 0) a tecla nao faz nada.
void GameState::togglePause() {
    // O original olha o objeto da rede (dword_56DC78). Aqui tambem vale a
    // missao multijogador rodando sozinha, que no original nao existe.
    if (netActive() || multiplayerMission()) {
        statusBarSet("Cannot Pause in multiplayer games");
        return;
    }
    if (TethysGame::tick() == 0) return;
    pauseGame(!gTApp.paused());
}

// O ESC largava a partida na HORA, sem perguntar e sem passar pelo caminho de
// saida do jogo (o pacote 0x31). Agora ele desiste do que estiver no ar e, se
// nao houver nada, pergunta -- com a frase do proprio executavel e pelo mesmo
// caminho do "&BACK TO MAIN MENU".
void GameState::escapePressed() {
    if (m_saveDlg.active()) { m_saveDlg.close(); return; }      // dialogo de slots
    if (m_confirm.isOpen()) { m_confirm.close(); return; }      // ja perguntou: e um "NAO"
    if (m_pickPlayer) { pickCancel(); return; }                 // escolhedor de jogador
    if (m_targetCmd >= 0) { m_targetCmd = -1; return; }         // desiste da mira
    if (!m_mission) { gTApp.activateShell(); return; }          // mapa solto (testes)

    // A pergunta mora no painel, que e da aba Options: leva para la, abre a
    // tela de fim de missao e poe a confirmacao em cima.
    m_tab = 5;
    m_gameOptions.open();
    m_gameOptions.setMode(GameOptionsPane::ENDMISSION);
    // Em multijogador nao existe "voltar ao menu" (a tela de fim e outra, so
    // com autodestruir e transferir), entao ali o ESC so abre a tela.
    if (multiplayerMission()) return;
    m_confirm.open(m_selected.empty() ? 0 : m_selected[0],
                   "Are you sure you want to abort the mission?", kCmdOptMainMenu,
                   /*fromOptions*/ true);
}

void GameState::selectAt(int nx, int ny, bool additive) {
    // Seleciona a unidade do jogador sob o clique (coords da vista, entao o
    // wrap horizontal ja esta resolvido): predio pela pegada inteira, veiculo
    // pelo sprite -- ver unitHitBox.
    if (!additive) m_selected.clear();
    const int best = unitAtBox(nx, ny, /*meusApenas*/ true);
    if (best && m_selected.size() < 0x20) {               // sub_41A9E0: teto de 32
        m_selected.push_back(best);
        soundPlay(unitSelectSound(g_units[best].type));   // sub_41AAA0: desc+0x1F0
    }
}

void GameState::selectBox(int x0, int y0, int x1, int y1) {
    // Seleciona os veículos do jogador cujo CENTRO na tela cai na caixa.
    // (coords de tela → o wrap horizontal já está resolvido por screenXOf.)
    int bx0 = std::min(x0, x1), by0 = std::min(y0, y1);
    int bx1 = std::max(x0, x1), by1 = std::max(y0, y1);
    int me = TethysGame::localPlayer();
    m_selected.clear();
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id == -1 || (r.flags & UNIT_DEAD)) continue;
        if ((r.owner & 0x0F) != me || !(r.flags & UNIT_VEHICLE)) continue;
        int ucx = screenXOf(r.pixelX) + 16, ucy = r.pixelY + 16 - m_camY;
        if (ucx >= bx0 && ucx <= bx1 && ucy >= by0 && ucy <= by1) {
            if (m_selected.size() >= 0x20) break;   // sub_41A9E0: a seleção para em 32
            m_selected.push_back(i);
        }
    }
    // sub_41AAA0: 1 unidade → som dela (desc+0x1F0); várias → 0x55.
    if (m_selected.size() == 1) soundPlay(unitSelectSound(g_units[m_selected[0]].type));
    else if (!m_selected.empty()) soundPlay(0x55);
}

void GameState::handleEvent(Renderer& renderer, const SDL_Event& e) {
    // Demo de atracao: qualquer tecla ou clique encerra (codigo 10). No
    // original isso vem de dois ganchos do Windows instalados durante o demo.
    if (demoGame() && (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN)) {
        endDemoGame();
        return;
    }
    if (m_saveDlg.active()) {                                // TFileDialog (modal)
        // O diálogo foi desenhado centrado no quadro (ver onIdle): o mouse
        // volta para as coordenadas de 640x480 dele.
        renderer.setInputOffset((m_fbW - 640) / 2, (m_fbH - 480) / 2);
        const SaveSlotsDialog::Result r = m_saveDlg.handleEvent(renderer, e);
        renderer.setInputOffset(0, 0);
        if (r == SaveSlotsDialog::kDoSave) {
            saveToFile(m_saveDlg.path(), m_saveDlg.description());   // TApp::SaveGame
        } else if (r == SaveSlotsDialog::kDoLoad) {
            m_endAfterSave = false;
            m_endPhase = 0;
            if (!loadSavedGame(m_saveDlg.path())) gTApp.activateShell();
            return;
        }
        if (r != SaveSlotsDialog::kNone && m_endAfterSave) { m_endAfterSave = false; finishGameEnd(); }
        return;
    }
    if (m_endPhase) {                                        // diálogo modal / espera
        if (e.type == SDL_MOUSEMOTION) renderer.screenToNative(e.motion.x, e.motion.y, m_mouseNX, m_mouseNY);
        if (m_endPhase != 2) return;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            int nx, ny;
            renderer.screenToNative(e.button.x, e.button.y, nx, ny);
            const int b = successButtonAt(nx, ny);
            if (b == 1) { m_endAfterSave = true; m_saveDlg.open(SaveSlotsDialog::kSave, m_mission ? m_mission->levelDesc : ""); }
            else if (b == 2) finishGameEnd();
        } else if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) {           // IDOK = "Save Game" (OnSaveSlot)
                m_endAfterSave = true;
                m_saveDlg.open(SaveSlotsDialog::kSave, m_mission ? m_mission->levelDesc : "");
            } else if (e.key.keysym.sym == SDLK_ESCAPE) finishGameEnd();
        }
        return;
    }
    // A linha de conversa vem ANTES de tudo: aberta, ela e quem fica com as
    // teclas (e o que o filtro do original faz, por estar no topo da cadeia).
    if (chatKey(e)) return;
    if (e.type == SDL_KEYDOWN) {
        // Aceleradores 130 do OUT2RES: Ctrl+S = 40017 (OnSaveSlot), Ctrl+L =
        // 40018, Ctrl+P = 40001 (a pausa, tambem na tecla PAUSE). Os
        // modificadores vem do PROPRIO evento: o acelerador vale pelas teclas
        // que estavam apertadas no momento da batida, e nao pelas de agora --
        // que e o que o SDL_GetModState() responde.
        const bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
        if (ctrl && e.key.keysym.sym == SDLK_s && m_mission) {
            m_saveDlg.open(SaveSlotsDialog::kSave, m_mission->levelDesc);
            return;
        }
        if (ctrl && e.key.keysym.sym == SDLK_l) {
            m_saveDlg.open(SaveSlotsDialog::kLoad, "");
            return;
        }
        if ((ctrl && e.key.keysym.sym == SDLK_p) || e.key.keysym.sym == SDLK_PAUSE) {
            togglePause();
            return;
        }
        if (e.key.keysym.sym == SDLK_ESCAPE) { escapePressed(); return; }
        if (e.key.keysym.sym == SDLK_g) m_showPassability = !m_showPassability;
        if (e.key.keysym.sym == SDLK_F3) m_showDebugHud = !m_showDebugHud;
        // DEPURACAO dos estados de predio (ainda nao ha armas/ConVec construindo):
        //   D = +10% do HP max de dano   H = repara (dano 0)
        //   K = Unit::DoDeath (Kill(1))  B = reinicia a construcao (sub_43DE50)
        //   O = poe o predio OPERANDO (sub_483810 — o que a economia faria)
        const SDL_Keycode k = e.key.keysym.sym;
        if (ctrl && (k == SDLK_d || k == SDLK_h || k == SDLK_k || k == SDLK_b || k == SDLK_o)) {
            for (int id : std::vector<int>(m_selected)) {
                if (id <= 0 || id >= (int)g_units.size()) continue;
                UnitRecord& r = g_units[id];
                if (r.id == -1 || !(r.flags & UNIT_BUILDING)) continue;
                if (k == SDLK_d) {
                    int hp = structureMaxHP(r);
                    r.damage = static_cast<int16_t>(std::min(hp, r.damage + hp / 10));
                } else if (k == SDLK_h) {
                    r.damage = 0;
                } else if (k == SDLK_k) {
                    Unit(id).DoDeath();
                } else if (k == SDLK_b && r.action != ACT_DIE) {
                    structureStartBuild(id, 0);
                } else if (k == SDLK_o) {
                    structureTurnOn(r);
                }
            }
        }
        // University: L&ESS / &MORE.
        if (!ctrl && m_students.isOpen()) {
            if (k == SDLK_e) m_students.less();
            if (k == SDLK_m) m_students.more();
        }
        // Pesquisa (modo 1): L&ESS / &MORE.
        if (!ctrl && m_research.isOpen() && m_research.mode() == 1) {
            if (k == SDLK_e) m_research.less();
            if (k == SDLK_m) m_research.more();
        }
        // STORAGE: Alt+1..6 = botão da baia (sub_40A900(0x31 + i)).
        // sub_498890: Alt+V (40224) só com Robot Command Center ([jogador+0x1A8]).
        if ((SDL_GetModState() & KMOD_ALT) && k == SDLK_v &&
            g_colony[TethysGame::localPlayer()].stat(ST_ROBOT_GRP)) {
            closePanes(); closeTabPanes(); m_tab = -1; m_vehicles.open();
        }
        // 40200..40205: Alt+C/M/P/F/N/R → Resources no modo 1..6 (fora de missão
        // só de unidades; Alt+R só com a mina de raro pesquisada); 40206: Alt+O.
        if ((SDL_GetModState() & KMOD_ALT) && !reportsDisabled()) {
            const int resMode = k == SDLK_c ? 1 : k == SDLK_m ? 2 : k == SDLK_p ? 3 : k == SDLK_f ? 4 :
                                k == SDLK_n ? 5 : k == SDLK_r ? 6 : 0;
            if (resMode && (resMode != 6 || ResourcesPane::rareAvailable())) openReport(2, resMode);
        }
        if ((SDL_GetModState() & KMOD_ALT) && k == SDLK_o && m_mission) openReport(5, 0);
        if ((SDL_GetModState() & KMOD_ALT) && k >= SDLK_1 && k <= SDLK_6 && m_storage.isOpen())
            m_storage.pressBay(static_cast<int>(k - SDLK_1));
        // Atalhos dos botões de comando (letra após o '&' da dica).
        if (!ctrl && k >= SDLK_a && k <= SDLK_z) {
            const char key = static_cast<char>('A' + (k - SDLK_a));
            bool done = false;
            for (const CmdButton& b : commandButtons())
                if (b.hotkey == key) { clickCommand(b.cmd); done = true; break; }
            if (!done)
                for (const TextButton& b : structureButtons())
                    if (b.hotkey == key) { clickCommand(b.cmd); break; }
        }
    }
    if (e.type == SDL_MOUSEMOTION) {
        renderer.screenToNative(e.motion.x, e.motion.y, m_mouseNX, m_mouseNY);
        // Hover: a unidade sob o cursor na vista; ao mudar, zera o relógio do
        // realce (+0x14/+0x18 em sub_41B620).
        const int hvx = m_mouseNX - kViewX, hvy = m_mouseNY - kViewY;
        const int hv = (hvx >= 0 && hvx < viewportW() && hvy >= 0 && hvy < viewportH())
                       ? unitAtView(hvx, hvy) : 0;
        if (hv != m_hoverUnit) { m_hoverUnit = hv; m_hoverStartTick = static_cast<int>(TethysGame::tick()); }
    }
    // Botões da barra de título (dword_4EA240): apertam com o botão em baixo e
    // só agem ao soltar em cima deles, postando o comando de sistema
    // (SC_MINIMIZE / SC_MAXIMIZE / SC_CLOSE).
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int nx, ny; renderer.screenToNative(e.button.x, e.button.y, nx, ny);
        const int c = m_frame.captionButtonAt(nx, ny, m_fbW);
        if (c >= 0) { m_capPressed = c; return; }
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && m_capPressed >= 0) {
        int nx, ny; renderer.screenToNative(e.button.x, e.button.y, nx, ny);
        const int c = m_frame.captionButtonAt(nx, ny, m_fbW);
        const int was = m_capPressed;
        m_capPressed = -1;
        if (c == was) {
            if (c == 0) SDL_MinimizeWindow(renderer.getWindow());
            else if (c == 1) {
                // Em tela cheia, "restaurar" devolve a janela; em janela, o
                // botao leva a tela cheia (o maximizar do original).
                if (renderer.fullscreen()) renderer.setFullscreen(false);
                else if (SDL_GetWindowFlags(renderer.getWindow()) & SDL_WINDOW_MAXIMIZED)
                    SDL_RestoreWindow(renderer.getWindow());
                else renderer.setFullscreen(true);
            } else {
                SDL_Event q{};
                q.type = SDL_QUIT;
                SDL_PushEvent(&q);
            }
        }
        return;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN) {
        int nx, ny; renderer.screenToNative(e.button.x, e.button.y, nx, ny);
        m_mouseNX = nx; m_mouseNY = ny;
        // Vista do mapa em (kViewX, kViewY); vx/vy = coordenadas dentro dela.
        const int vx = nx - kViewX, vy = ny - kViewY;
        bool inView = vx >= 0 && vx < viewportW() && vy >= 0 && vy < viewportH();
        // Painel de produção: lista e setas (sub_4683F0 / sub_4683B0 / sub_430910-60).
        bool paneHit = false;
        // Botões do minimapa (sub_458930): Zoom In / Zoom Out / Tática / Globo.
        if (e.button.button == SDL_BUTTON_LEFT && !inView) {
            const int bx0 = kColX + kColW / 2 - 0x20;
            if (ny >= kMiniMapY && ny < kMiniMapY + 16 && nx >= bx0 &&
                nx < bx0 + 16 * miniButtonCount()) {
                const int k = (nx - bx0) / 16;
                if (k == 0 && m_mini.canZoomIn()) { soundPlay(0x57); m_mini.zoomIn(); }
                else if (k == 1 && m_mini.canZoomOut()) { soundPlay(0x57); m_mini.zoomOut(); }
                else if (k == 2) { soundPlay(0x57); m_mini.toggleTactical(); }   // sub_48E960
                else if (k == 3) { soundPlay(0x57); m_mini.toggleGlobe(); }       // sub_48E9E0
                return;
            }
        }
        // Minimapa (sub_4583D0 case 513): ponto válido → arrasto da vista.
        if (e.button.button == SDL_BUTTON_LEFT && !inView) {
            int px, py;
            if (miniToWorld(nx, ny, px, py)) {
                m_miniDrag = true;
                requestCenterView(px, py);
                paneHit = true;
            }
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && m_produce.isOpen()) {
            bool buildNow = false;
            paneHit = m_produce.click(infoPaneRect(), nx, ny, e.button.clicks >= 2, buildNow);
            if (buildNow) produceBuildIt();
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_students.isOpen())
            paneHit = m_students.click(kColX, kPaneWinY, nx, ny);   // LESS/MORE: 0x57 lá dentro
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_research.isOpen()) {
            bool dbl = false;
            paneHit = m_research.click(kColX, kPaneWinY, infoPaneRect(), nx, ny, e.button.clicks >= 2, dbl);
            if (dbl) clickCommand(kCmdAssign);
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_vehicles.isOpen())
            paneHit = m_vehicles.click(infoPaneRect(), nx, ny);
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_storage.isOpen())
            paneHit = m_storage.click(kColX, kPaneWinY, nx, ny);
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit) {
            syncTabToSelection();
            for (int k = 0; k < 6 && !paneHit; ++k) {
                PaneRect r;
                if (tabRect(k, r) && nx >= r.l && nx < r.r && ny >= r.t && ny < r.b) { openTab(k); paneHit = true; }
            }
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_tab == 3 && m_comms.isOpen()) {
            paneHit = m_comms.click(infoPaneRect(), nx, ny);
            if (!paneHit) paneHit = m_comms.clickChat(kColX, kPaneWinY, nx, ny);
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_tab == 4 && m_space.isOpen())
            paneHit = m_space.click(infoPaneRect(), nx, ny);
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_tab == 2 && m_resources.isOpen()) {
            bool dbl = false;
            paneHit = m_resources.click(infoPaneRect(), nx, ny, e.button.clicks >= 2, dbl);
            if (dbl) clickCommand(kCmdDetails);
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_tab == 1 && m_labs.isOpen()) {
            bool dbl = false;
            paneHit = m_labs.click(infoPaneRect(), nx, ny, e.button.clicks >= 2, dbl);
            if (dbl) clickCommand(kCmdDetails);
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_tab == 0 && m_factories.isOpen()) {
            bool dbl = false;
            paneHit = m_factories.click(infoPaneRect(), nx, ny, e.button.clicks >= 2, dbl);
            if (dbl) clickCommand(kCmdDetails);
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit && m_tab == 5 && m_mission) {
            if (m_gameOptions.isOpen()) {                // Game Options no lugar dos objetivos
                bool changed = false, back = false;
                paneHit = m_gameOptions.click(infoPaneRect(), nx, ny, changed, back);
                if (changed) loadSettings();             // reaplica o [Game] alterado
                if (back) m_gameOptions.close();
            } else {
                paneHit = m_objectives.click(infoPaneRect(), nx, ny);
            }
        }
        // Botão de comando do quadro (IDLE / ACTIVATE → CommandPacket 0x12 / 0x13).
        // O clique num botão habilitado toca o som 0x57 (sub_40A900, caso 517).
        if (e.button.button == SDL_BUTTON_LEFT && !inView && !paneHit) {
            for (const TextButton& b : structureButtons())
                if (nx >= b.l && nx < b.r && ny >= b.t && ny < b.b) {
                    if (!b.disabled) { soundPlay(0x57); clickCommand(b.cmd); }
                    break;
                }
        }
        if (e.button.button == SDL_BUTTON_LEFT && !inView) {
            for (const CmdButton& b : commandButtons())
                if (nx >= b.l && nx < b.r && ny >= b.t && ny < b.b) { soundPlay(0x57); clickCommand(b.cmd); break; }
        }
        if (e.button.button == SDL_BUTTON_LEFT && inView && m_targetCmd >= 0 && uiCommandIsArea(m_targetCmd)) {
            // Área (sub_41B550 modos 1/4): o arrasto começa aqui e termina ao soltar.
            m_areaDrag = true;
            m_areaSX = m_camX + vx; m_areaSY = m_camY + vy;
        } else if (e.button.button == SDL_BUTTON_LEFT && inView && m_launchFrom) {
            // sub_457480: o clique escolhe o alvo do míssil (pacote 0x19 no ponto).
            const int px = g_gameMap.ok() ? (m_camX + vx) & (g_gameMap.width() * 32 - 1) : m_camX + vx;
            spaceportLaunch(m_launchFrom, px, m_camY + vy);
            m_launchFrom = 0;
            closePanes();
            soundPlay(0x56);
        } else if (e.button.button == SDL_BUTTON_RIGHT && m_launchFrom) {
            m_launchFrom = 0;
        } else if (e.button.button == SDL_BUTTON_LEFT && inView && m_targetCmd >= 0) {
            // Modo de mira: o clique escolhe o alvo (vt[3] do comando).
            int tileX = (m_camX + vx) / 32, tileY = (m_camY + vy) / 32;
            if (g_gameMap.ok()) tileX &= (g_gameMap.width() - 1);
            uiSetCursorPixel(g_gameMap.ok() ? (m_camX + vx) & (g_gameMap.width() * 32 - 1) : m_camX + vx, m_camY + vy);
            if (uiCommandTarget(m_targetCmd, m_selected, tileX, tileY) != UiClick::TARGET)
                m_targetCmd = -1;                            // Patrulhar: 2 cliques
        } else if (e.button.button == SDL_BUTTON_RIGHT && m_targetCmd >= 0) {
            m_targetCmd = -1;                                // cancela a mira
        } else if (e.button.button == SDL_BUTTON_LEFT && inView) {
            m_dragging = true; m_dragSX = vx; m_dragSY = vy;
        } else if (e.button.button == SDL_BUTTON_RIGHT && inView && !m_selected.empty()) {
            // Mover os SELECIONADOS para o tile clicado (pipeline real + espiral).
            // Fork: (1) o tile vem das coordenadas DA VISTA (vx/vy), não das do
            // quadro (nx/ny) -- antes o alvo caía 12 px à direita e 22 px abaixo
            // do clique, quase sempre no tile de baixo; (2) passa pelo mesmo
            // caminho do botão MOVE (sub_451280 em uiCommandTarget): clicar na
            // doca de um prédio com um veículo que atraca manda o pacote de
            // doca (3, ou 4 na garagem de Eden), não um "mover" simples.
            int tileX = (m_camX + vx) / 32, tileY = (m_camY + vy) / 32;
            if (g_gameMap.ok()) tileX &= (g_gameMap.width() - 1);
            uiSetCursorPixel(g_gameMap.ok() ? (m_camX + vx) & (g_gameMap.width() * 32 - 1) : m_camX + vx, m_camY + vy);
            uiCommandTarget(UC_MOVE, m_selected, tileX, tileY);
        }
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && m_miniDrag) {
        // case 514: ponto final, depois solta (flags 2, sem 1|4).
        int px, py;
        if (miniToWorld(m_mouseNX, m_mouseNY, px, py)) requestCenterView(px, py);
        m_miniDrag = false;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && m_areaDrag) {
        m_areaDrag = false;
        int nx, ny; renderer.screenToNative(e.button.x, e.button.y, nx, ny);
        int x1, y1, x2, y2;
        areaRect(m_camX + nx - kViewX, m_camY + ny - kViewY, x1, y1, x2, y2);
        if (m_targetCmd >= 0 && uiCommandArea(m_targetCmd, m_selected, x1, y1, x2, y2) != UiClick::TARGET)
            m_targetCmd = -1;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && m_dragging) {
        int nx, ny; renderer.screenToNative(e.button.x, e.button.y, nx, ny);
        const int vx = nx - kViewX, vy = ny - kViewY;     // coordenadas na vista
        int dx = std::abs(vx - m_dragSX), dy = std::abs(vy - m_dragSY);
        if (dx < 4 && dy < 4) selectAt(vx, vy, false);   // clique = seleção simples
        else selectBox(m_dragSX, m_dragSY, vx, vy);        // caixa = multi-seleção
        m_dragging = false;
    }
}

// Retângulo do arrasto em tiles (x2/y2 exclusivos): muro/tubo passa pela reta
// de sub_41C410; terraplanar é o retângulo dos dois cantos.
void GameState::areaRect(int ex, int ey, int& x1, int& y1, int& x2, int& y2) const {
    int sx = m_areaSX, sy = m_areaSY;
    if (uiCommandIsLine(m_targetCmd)) wallDragConstrain(sx, sy, ex, ey);
    const int ax = sx >> 5, ay = sy >> 5, bx = ex >> 5, by = ey >> 5;
    x1 = std::min(ax, bx); x2 = std::max(ax, bx) + 1;
    y1 = std::min(ay, by); y2 = std::max(ay, by) + 1;
    if (y1 < 0) y1 = 0;
    if (g_gameMap.ok()) {
        if (y2 > g_gameMap.height()) y2 = g_gameMap.height();
        const int m = g_gameMap.width() - 1;
        const int w = x2 - x1;
        x1 &= m; x2 = x1 + w;
    }
}

// sub_46D540: lista dos 22 (1 unidade) ou dos 11 (várias); cada comando
// disponível vira um botão na posição do descritor, numa grade 3x3 de 46 px:
// x = 28 + 46*col, y = topo do quadro + 0x3C + 57 + 46*lin. Só p/ a botoeira de
// veículo (painel off_4D5B28); prédios usam os botões de texto do quadro.
std::vector<GameState::CmdButton> GameState::commandButtons() const {
    std::vector<CmdButton> out;
    if (m_selected.empty() || m_confirm.isOpen() || m_vehicles.isOpen() || m_tab != -1) return out;   // outro painel no lugar da botoeira
    const UnitRecord& u = g_units[m_selected[0]];
    if (u.id == -1 || !(u.flags & 0x42u) || (u.owner & 0x0F) != TethysGame::localPlayer()) return out;
    const std::vector<int>& list = m_selected.size() == 1 ? uiCommandsSingle() : uiCommandsMulti();
    const int px0 = kColX;                                // janela do quadro
    const int top = kPaneTop + 0x3C + 57;                 // dword_57556C + dword_578F4C + 0x3C
    for (int cmd : list) {
        if (!uiCommandAvailable(cmd, m_selected)) continue;
        const UiCommandDesc d = uiCommandDescribe(cmd, m_selected);
        if (d.slot < 1 || d.slot > 9 || !d.iconRaw) continue;
        const int row = (d.slot - 1) / 3, col = (d.slot - 1) % 3;
        CmdButton b;
        b.cmd = cmd;
        b.l = px0 + 28 + 46 * col; b.t = top + 46 * row;
        b.r = b.l + 44; b.b = b.t + 44;
        b.iconRaw = d.iconRaw;
        b.tip = d.tip;
        b.hotkey = uiCommandHotkey(d);
        out.push_back(b);
    }
    return out;
}

// sub_459F70: com 1 prédio selecionado, os comandos disponíveis da lista de 12
// (até 4) viram botões de texto; sub_458D10 os dispõe a partir de x = 9, de
// baixo p/ cima (altura H = 25, largura W = 87): 1 → largo embaixo; 2 → lado a
// lado; 3 → dois em cima + largo embaixo; 4 → 2x2. PROVISÓRIO: base = fundo do
// quadro (dword_5750EC[+0x40]) e x relativo ao painel.
std::vector<GameState::TextButton> GameState::structureButtons() const {
    std::vector<TextButton> out;
    auto layoutButtons = [&]() {
        const int W = 87, H = 25, x0 = kColX + 9, bottom = kPaneBottom - 2;
        const int n = static_cast<int>(out.size());
        for (int i = 0; i < n; ++i) {
            TextButton& b = out[i];
            const bool twoRows = n >= 3;
            const int row = twoRows ? (i < 2 ? 0 : 1) : 1;
            const int col = (n == 1 || (n == 3 && i == 2)) ? 0 : (i % 2);
            b.wide = n == 1 || (n == 3 && i == 2);
            b.l = x0 + col * W;
            b.t = bottom - (row == 0 ? 2 * H : H);
            b.r = b.l + (b.wide ? 175 : W);
            b.b = b.t + H;
        }
    };
    if (m_pickPlayer) {
        // sub_46D230: um botao largo por jogador ativo que nao seja eu, com o
        // nome dele; o titulo "Transfer Vehicle(s) to:" sai no desenho.
        int empilha = kPaneTop + 30;
        const int me = TethysGame::localPlayer();
        for (int i = 0; i < TethysGame::noPlayers() && i < kMaxPlayers; ++i) {
            if (i == me || !g_playerData[i].isHuman) continue;
            TextButton b{};
            b.cmd = kCmdPickPlayer0 - i;
            b.label = playerName(i);
            b.tip = "Transfer to another player (&X)";
            b.wide = true;
            b.l = kColX + 9; b.t = empilha; b.r = b.l + 175; b.b = b.t + 25;
            empilha += 29;
            out.push_back(b);
        }
        TextButton c{};
        c.cmd = kCmdPickCancel; c.label = "&CANCEL"; c.tip = "&Cancel"; c.hotkey = 'C';
        c.wide = true;
        c.l = kColX + 9; c.t = kPaneBottom - 2 - 25; c.r = c.l + 175; c.b = c.t + 25;
        out.push_back(c);
        return out;
    }
    if (m_vehicles.isOpen()) {                               // sub_46B2D0: &JUMP / &STATUS
        TextButton b{};
        b.cmd = kCmdJump; b.label = "&JUMP"; b.tip = "&Jump to selected vehicle type"; b.hotkey = 'J';
        b.disabled = !m_vehicles.jumpEnabled();
        out.push_back(b);
        b = TextButton{};
        b.cmd = kCmdStatus; b.label = "&STATUS"; b.tip = "Back to Structure &Status"; b.hotkey = 'S';
        out.push_back(b);
        layoutButtons();
        return out;
    }
    if (m_tab == 4) {
        // sub_46CFA0: modo 0 "&STARSHIP" (só com a pesquisa do tipo 0x37); 1 "&SATELLITES".
        TextButton b{};
        b.cmd = kCmdSpaceMode;
        if (m_space.mode() == 0) {
            if (!SpacePane::starshipAvailable()) return out;
            b.label = "&STARSHIP"; b.tip = "Show &Starship Information"; b.hotkey = 'S';
        } else {
            b.label = "&SATELLITES"; b.tip = "Show &Satellite Information"; b.hotkey = 'S';
        }
        out.push_back(b);
        layoutButtons();
        return out;
    }
    if (m_tab == 2) {
        // sub_469490: 0 → "&DETAILS" largo; 1..6 → "S&TRUCTURES" + "&SUMMARY";
        // 7..12 → "&DETAILS" + "&SUMMARY".
        const int md = m_resources.mode();
        TextButton b{};
        if (md == 0 || md >= 7) {
            b.cmd = kCmdDetails; b.label = "&DETAILS"; b.tip = "&Details"; b.hotkey = 'D';
            b.disabled = !m_resources.detailsEnabled();
            out.push_back(b);
        } else {
            b.cmd = kCmdResStructures; b.label = "S&TRUCTURES"; b.tip = "S&tructures"; b.hotkey = 'T';
            out.push_back(b);
        }
        if (md != 0) {
            b = TextButton{};
            b.cmd = kCmdResSummary; b.label = "&SUMMARY"; b.tip = "&Summary"; b.hotkey = 'S';
            out.push_back(b);
        }
        layoutButtons();
        return out;
    }
    if (m_tab == 0) {
        // sub_464BD0: "&DETAILS" largo, habilitado depois de escolher uma fábrica.
        TextButton b{};
        b.cmd = kCmdDetails; b.label = "&DETAILS"; b.tip = "&Details"; b.hotkey = 'D';
        b.disabled = !m_factories.detailsEnabled();
        out.push_back(b);
        layoutButtons();
        return out;
    }
    if (m_tab == 1) {
        // sub_46BB90: modo 0 DETAILS + C&OMPLETED; 1 DETAILS + C&URRENT; 2 C&OMPLETED + C&URRENT.
        TextButton b{};
        if (m_labs.mode() != 2) {
            b.cmd = kCmdDetails; b.label = "&DETAILS"; b.tip = "&Details"; b.hotkey = 'D';
            b.disabled = !m_labs.detailsEnabled();
            out.push_back(b);
        }
        if (m_labs.mode() != 1) {
            b = TextButton{};
            b.cmd = kCmdLabsCompleted; b.label = "C&OMPLETED"; b.tip = "C&ompleted Research"; b.hotkey = 'O';
            out.push_back(b);
        }
        if (m_labs.mode() != 0) {
            b = TextButton{};
            b.cmd = kCmdLabsCurrent; b.label = "C&URRENT"; b.tip = "C&urrent Research"; b.hotkey = 'U';
            out.push_back(b);
        }
        layoutButtons();
        return out;
    }
    if (m_tab == 3) {
        // sub_4670A0. Modo 0: em jogo solo so "&JUMP" (largo), habilitado
        // quando a mensagem escolhida tem posicao (sub_466970); em
        // multijogador ele divide a linha com "&ALLIES/CHAT". Modo 1: a volta
        // ("&MESSAGES") e as estatisticas de rede.
        TextButton b{};
        if (m_comms.mode() != CommsPane::MESSAGES) {
            b.cmd = kCmdMessages; b.label = "&MESSAGES"; b.tip = "Display Past &Messages"; b.hotkey = 'M';
            out.push_back(b);
            b = TextButton{};
            if (m_comms.mode() == CommsPane::CHAT) {
                b.cmd = kCmdNetStats; b.label = "&NET STATS"; b.tip = "Show &network statistics."; b.hotkey = 'N';
            } else {
                b.cmd = kCmdAlliesChat; b.label = "&ALLIES/CHAT";
                b.tip = "Set Up &Allies and Chat Partners"; b.hotkey = 'A';
            }
            out.push_back(b);
            layoutButtons();
            return out;
        }
        b.cmd = kCmdMsgJump; b.label = "&JUMP"; b.tip = "&Jump to the message's location"; b.hotkey = 'J';
        b.disabled = !m_comms.jumpEnabled();
        out.push_back(b);
        if (multiplayerMission()) {                          // sub_4027D0
            b = TextButton{};
            b.cmd = kCmdAlliesChat; b.label = "&ALLIES/CHAT";
            b.tip = "Set Up &Allies and Chat Partners"; b.hotkey = 'A';
            out.push_back(b);
        }
        layoutButtons();
        return out;
    }
    if (m_tab == 5 && m_mission) {
        // sub_45C200: com a confirmacao no ar ela TOMA o painel (sub_45D020
        // troca o painel do contentor), entao os botoes sao os dela -- sem
        // isto a tela END MISSION ficava com os proprios botoes e o &YES nao
        // existia em lugar nenhum.
        if (m_confirm.isOpen()) {
            TextButton y{};
            y.cmd = kCmdYes; y.label = "&YES"; y.tip = "&YES: Confirm command"; y.hotkey = 'Y';
            out.push_back(y);
            TextButton n{};
            n.cmd = kCmdNo; n.label = "&NO"; n.tip = "&NO: Abort command"; n.hotkey = 'N';
            out.push_back(n);
            layoutButtons();
            return out;
        }
        if (m_gameOptions.isOpen()) {
            // Um botao largo da pilha, de cima para baixo.
            int empilha = kPaneTop + 30;
            auto larga = [&](int cmd, const char* rotulo, const char* dica, char tecla) {
                TextButton b{};
                b.cmd = cmd; b.label = rotulo; b.tip = dica; b.hotkey = tecla;
                b.wide = true;
                b.l = kColX + 9; b.t = empilha; b.r = b.l + 175; b.b = b.t + 25;
                empilha += 29;
                out.push_back(b);
            };
            if (m_gameOptions.mode() == GameOptionsPane::MENU) {
                // Os seis de sub_457880, com os rotulos do executavel.
                larga(kCmdOptObjectives, "&MISSION OBJECTIVES", "Show &Mission Objectives", 'M');
                larga(kCmdOptManual, "ONLINE MAN&UAL", "Open Online Man&ual", 'U');
                larga(kCmdOptPrefs, "PR&EFERENCES", "Change Game Pr&eferences", 'E');
                larga(kCmdOptLoad, "L&OAD GAME", "L&oad a Saved Game", 'O');
                larga(kCmdOptSave, "&SAVE GAME", "&Save this Game", 'S');
                larga(kCmdOptEnd, "E&ND MISSION", "E&nd Mission Options", 'N');
                return out;
            }
            // No manual a lista ocupa o painel, entao os botoes vao para o
            // rodape, como no PREFERENCES.
            if (m_gameOptions.mode() == GameOptionsPane::MANUAL ||
                m_gameOptions.mode() == GameOptionsPane::MANUALTEXT) {
                const bool lendo = m_gameOptions.mode() == GameOptionsPane::MANUALTEXT;
                empilha = kPaneBottom - 2 - 25 - (lendo ? 29 : 0);
                if (lendo) larga(kCmdOptManual, "&BACK", "Back to the topic list", 'B');
                larga(kCmdOptMenu, "&GAME OPTIONS", "Back to &Game Options", 'G');
                return out;
            }
            if (m_gameOptions.mode() == GameOptionsPane::ENDMISSION) {
                // sub_465830: sao DUAS telas. Em multijogador (modo 4) nao se
                // reinicia nem se volta ao menu -- so se entrega a colonia; em
                // jogo solo (modo 3) nao existe autodestruicao.
                if (multiplayerMission()) {
                    larga(kCmdOptDestroy, "SELF-&DESTRUCT", "&Destroy all vehicles and structures", 'D');
                    larga(kCmdOptTransfer, "&TRANSFER", "&Transfer vehicles/destroy structures", 'T');
                    out.back().disabled = !quitTransferAvailable();   // sub_4909F0
                } else {
                    larga(kCmdOptRestart, "RESTART MISSIO&N", "E&nd current mission and restart it", 'N');
                    larga(kCmdOptMainMenu, "&BACK TO MAIN MENU", "Go &Back to Main Menu", 'B');
                    larga(kCmdOptExit, "E&XIT OUTPOST 2", "E&xit Outpost 2 and return to Windows", 'X');
                }
                larga(kCmdOptMenu, "&GAME OPTIONS", "Back to &Game Options", 'G');
                return out;
            }
            // PREFERENCES: L&ESS/&MORE no item com valor, e embaixo &APPLY e a
            // volta ao menu.
            const int sel = m_gameOptions.selected();
            const int top = kPaneBottom - 2 - 25;
            if (m_gameOptions.valueItem(sel)) {
                TextButton l{};
                l.cmd = kCmdOptLess; l.label = "L&ESS"; l.tip = m_gameOptions.tipLess(sel); l.hotkey = 'E';
                l.l = kColX + 9; l.t = top - 56; l.r = l.l + 85; l.b = l.t + 25;
                out.push_back(l);
                TextButton m{};
                m.cmd = kCmdOptMore; m.label = "&MORE"; m.tip = m_gameOptions.tipMore(sel); m.hotkey = 'M';
                m.l = kColX + 99; m.t = top - 56; m.r = m.l + 85; m.b = m.t + 25;
                out.push_back(m);
            }
            TextButton a{};
            a.cmd = kCmdOptApply; a.label = "&APPLY"; a.tip = "&Apply new values"; a.hotkey = 'A';
            a.wide = true;
            a.l = kColX + 9; a.t = top - 28; a.r = a.l + 175; a.b = a.t + 25;
            out.push_back(a);
            TextButton b{};
            b.cmd = kCmdOptMenu; b.label = "&GAME OPTIONS"; b.tip = "Back to &Game Options"; b.hotkey = 'G';
            b.wide = true;
            b.l = kColX + 9; b.t = top; b.r = b.l + 175; b.b = b.t + 25;
            out.push_back(b);
            return out;
        }
        // Aba de objetivos (sub_465830, modo 1): "&GAME OPTIONS" largo.
        TextButton b{};
        b.cmd = kCmdGameOptions; b.label = "&GAME OPTIONS"; b.tip = "Show &Game Options"; b.hotkey = 'G';
        b.wide = true;
        b.l = kColX + 9; b.t = kPaneBottom - 2 - 25; b.r = b.l + 175; b.b = b.t + 25;
        out.push_back(b);
        return out;
    }
    if (m_selected.size() != 1) return out;
    const UnitRecord& u = g_units[m_selected[0]];
    if (u.id == -1 || (u.owner & 0x0F) != TethysGame::localPlayer()) return out;
    // Confirmação (dword_564EA8) vale p/ prédio e veículo (despejo de carga).
    if (!(u.flags & UNIT_BUILDING) && !m_confirm.isOpen()) return out;
    if (m_confirm.isOpen()) {                                // sub_45C200: &YES / &NO
        TextButton b{};
        b.cmd = kCmdYes; b.label = "&YES"; b.tip = "&YES: Confirm command"; b.hotkey = 'Y';
        out.push_back(b);
        b = TextButton{};
        b.cmd = kCmdNo; b.label = "&NO"; b.tip = "&NO: Abort command"; b.hotkey = 'N';
        out.push_back(b);
    }
    if (m_students.isOpen()) {                               // sub_46E0D0: S&TART / &STATUS
        TextButton b{};
        b.cmd = kCmdStartClass; b.label = "S&TART"; b.tip = "S&tart Class"; b.hotkey = 'T';
        out.push_back(b);
        b = TextButton{};
        b.cmd = kCmdStatus; b.label = "&STATUS"; b.tip = "&Status"; b.hotkey = 'S';
        out.push_back(b);
    }
    if (m_research.isOpen()) {
        // sub_46C520: modo 0 = ASSIGN (habilita com tópico) / CANCEL;
        // modo 1 = START / TOPICS (ou STATUS se o lab já pesquisa).
        TextButton b{};
        if (m_research.mode() == 0) {
            b.cmd = kCmdAssign; b.label = "&ASSIGN"; b.tip = "&Assign Scientists"; b.hotkey = 'A';
            b.disabled = !m_research.assignEnabled();
            out.push_back(b);
            b = TextButton{};
            b.cmd = kCmdCancel; b.label = "CA&NCEL"; b.tip = "Ca&ncel"; b.hotkey = 'N';
            out.push_back(b);
        } else {
            b.cmd = kCmdStart; b.label = "ST&ART"; b.tip = "St&art Research"; b.hotkey = 'A';
            out.push_back(b);
            b = TextButton{};
            if (m_research.researching()) { b.cmd = kCmdStatus; b.label = "&STATUS"; b.tip = "&Status"; b.hotkey = 'S'; }
            else { b.cmd = kCmdTopics; b.label = "&TOPICS"; b.tip = "Research &Topics"; b.hotkey = 'T'; }
            out.push_back(b);
        }
    }
    if (m_storage.isOpen() && m_storage.resourceMode()) {
        // sub_463170 (recursos): L&OAD / &UNLOAD (dword_564A68 / dword_564A5C) e &STATUS.
        TextButton b{};
        b.cmd = kCmdLoad; b.label = "L&OAD"; b.tip = "L&oad Truck"; b.hotkey = 'O';
        b.disabled = !m_storage.loadEnabled();
        out.push_back(b);
        b = TextButton{};
        b.cmd = kCmdUnload; b.label = "&UNLOAD"; b.tip = "&Unload Truck"; b.hotkey = 'U';
        b.disabled = !m_storage.unloadEnabled();
        out.push_back(b);
        b = TextButton{};
        b.cmd = kCmdStatus; b.label = "&STATUS"; b.tip = "Back to Structure &Status"; b.hotkey = 'S';
        out.push_back(b);
    } else if (m_storage.isOpen()) {
        // sub_463170: "&STATUS"; no modo plataforma ([+0x6C4]) também "L&AUNCH".
        TextButton b{};
        b.cmd = kCmdStatus; b.label = "&STATUS"; b.tip = "Back to Structure &Status"; b.hotkey = 'S';
        out.push_back(b);
        if (m_storage.launchMode()) {
            b = TextButton{};
            b.cmd = kCmdLaunch; b.label = "L&AUNCH"; b.tip = "L&aunch Vehicle into Orbit"; b.hotkey = 'A';
            b.disabled = !spaceportLaunchAvailable(m_storage.building());
            out.push_back(b);
        }
    }
    if (m_produce.isOpen()) {
        // sub_4682C0: "&BUILD IT" (desabilitado até escolher um item pagável) e
        // "&STATUS" — ambos voltam ao painel de status (dword_564A80).
        TextButton b{};
        b.cmd = kCmdBuildIt; b.label = "&BUILD IT"; b.tip = "&Build selected item"; b.hotkey = 'B';
        b.disabled = !m_produce.buildEnabled();
        out.push_back(b);
        b = TextButton{};
        b.cmd = kCmdStatus; b.label = "&STATUS"; b.tip = "Back to Structure &Status"; b.hotkey = 'S';
        out.push_back(b);
    }
    for (int cmd : uiCommandsStructure()) {
        if (anyPaneOpen()) break;
        if (out.size() == 4) break;
        if (!uiCommandAvailable(cmd, m_selected)) continue;
        const UiCommandDesc d = uiCommandDescribe(cmd, m_selected);
        TextButton b{};
        b.cmd = cmd; b.label = d.label; b.tip = d.tip; b.hotkey = uiCommandHotkey(d);
        out.push_back(b);
    }
    // sub_459F70: o Robot Command Center (0x1F) ganha "&VEHICLES" junto dos
    // botões disponíveis (sem nenhum, não aparece).
    if (!anyPaneOpen() && u.type == 0x1F && !out.empty() && out.size() < 4) {
        TextButton b{};
        b.cmd = kCmdVehicles; b.label = "&VEHICLES"; b.tip = "Show &Vehicle List"; b.hotkey = 'V';
        out.push_back(b);
    }
    const int W = 87, H = 25, x0 = kColX + 9, bottom = kPaneBottom - 2;
    const int n = static_cast<int>(out.size());
    for (int i = 0; i < n; ++i) {
        TextButton& b = out[i];
        const bool twoRows = n >= 3;
        const int row = twoRows ? (i < 2 ? 0 : 1) : 1;           // 0 = de cima
        const int col = (n == 1 || (n == 3 && i == 2)) ? 0 : (i % 2);
        b.wide = n == 1 || (n == 3 && i == 2);
        b.l = x0 + col * W;
        b.t = bottom - (row == 0 ? 2 * H : H);
        b.r = b.l + (b.wide ? 175 : W);
        b.b = b.t + H;
    }
    return out;
}

void GameState::debugVehicleList(int row, bool jump) {
    if (!m_vehicles.isOpen()) clickCommand(kCmdVehicles);
    const PaneRect pane = infoPaneRect();
    const ListBoxLayout L = listBoxLayout(pane, 0, 0x0A, 0, 0x12);
    if (row >= 0) m_vehicles.click(pane, L.list.l + 5, L.list.t + 2 + row * L.lineH + 2);
    if (jump) clickCommand(kCmdJump);
}

// sub_45CB60/sub_45CBA0: trocar a seleção (por outro que não o próprio quadro —
// inclusive JUMP/DETAILS dos relatórios) marca [+0x484]; no quadro seguinte
// sub_45CC30 põe o painel da unidade — ou nenhum, sem seleção.
void GameState::syncTabToSelection() {
    if (m_selected == m_lastSel) return;
    m_lastSel = m_selected;
    m_tab = -1;
    closeTabPanes();
    closePanes();                                            // o painel da unidade substitui o atual
}

// sub_457760 (DETAILS): unidade viva → centraliza a vista nela (sub_408120) e a
// seleciona (sub_41A8E0 + sub_41A710) — a troca de seleção põe o painel dela.
void GameState::reportDetails(int unit) {
    if (unit <= 0 || unit >= static_cast<int>(g_units.size())) return;
    const UnitRecord& r = g_units[unit];
    if (r.id == -1 || (r.flags & UNIT_DEAD)) return;
    requestCenterView(unitOX(r), unitOY(r));
    m_selected = {unit};
}

// Retângulo da aba k (sub_45C940): encostadas a partir de x = 1 no topo da
// janela do quadro, cada uma da largura da sua arte (sub_404D30).
bool GameState::tabRect(int k, PaneRect& r) const {
    static const int kTabArt[6] = {0x8E, 0x91, 0x90, 0x8F, 0x92, 0x8D};   // dword_4D5390
    if (!g_art.ok() || k < 0 || k > 5) return false;
    int x = 1;
    for (int i = 0; i <= k; ++i) {
        const int anim = kTabArt[i] - 0x65;
        if (anim >= g_art.numAnimations()) return false;
        const ArtAnimation& an = g_art.animation(anim);
        const int w = static_cast<int>(an.header[3]) - static_cast<int>(an.header[1]);
        const int h = static_cast<int>(an.header[4]) - static_cast<int>(an.header[2]);
        if (i == k) { r = PaneRect{kColX + x, kPaneWinY, kColX + x + w, kPaneWinY + h}; return true; }
        x += w;
    }
    return false;
}

// sub_45C620: clicar na aba limpa a seleção (sub_41A8E0, com o quadro como
// origem: sem [+0x484]), volta o painel ao modo 0 (vt[5]) e o põe no quadro
// (sub_45D020, aba acesa). PROVISÓRIO: só as abas já portadas respondem.
bool GameState::reportsDisabled() const {
    return m_mission && m_mission->desc && m_mission->desc->boolUnitMission;
}

void GameState::openReport(int k, int mode) {
    closePanes();
    closeTabPanes();
    m_targetCmd = -1;
    m_tab = k;
    m_lastSel = m_selected;
    if (k == 0) m_factories.open();
    if (k == 1) m_labs.open(mode);
    if (k == 2) m_resources.open(mode);
    if (k == 3) m_comms.open();
    if (k == 4) m_space.open(mode);
}

void GameState::openTab(int k) {
    if (k == 5 ? !m_mission : k == 4 ? false : (k < 0 || k > 3 || reportsDisabled())) return;
    m_targetCmd = -1;
    m_selected.clear();
    m_lastSel.clear();
    closePanes();
    closeTabPanes();
    m_tab = k;
    if (k == 0) m_factories.open();
    if (k == 1) m_labs.open(0);
    if (k == 2) m_resources.open(0);
    if (k == 3) m_comms.open();
    if (k == 4) m_space.open(0);
    soundPlay(0x57);
}

void GameState::debugTab(int k, int row) {
    PaneRect r;
    if (tabRect(k, r)) openTab(k);
    if (k == 3 && row >= 0) m_comms.select(row);
    if (k == 0 && row >= 0) m_factories.select(row);
    if (k == 1 && row >= 0) m_labs.select(row);
    if (k == 2 && row >= 0) m_resources.select(row);
}

PaneRect GameState::infoPaneRect() const {
    return PaneRect{kColX, kPaneTop, kColX + kColW, kPaneBottom};
}

// BUILD IT (sub_467D50): item escolhido → sub_456B50 (pacote 9); volta ao status.
void GameState::produceBuildIt() {
    if (!m_produce.isOpen() || !m_produce.buildEnabled() || !m_produce.selected()) return;
    sendProduce(m_produce.factory(), m_produce.selected());
    m_produce.close();
}

// Som tocado no desenho (vt[6]) do original: uma vez por tick por unidade/som,
// já que aqui há vários quadros por tick.
void GameState::drawSound(int unitId, int x, int y, int id) {
    const uint32_t t = TethysGame::tick();
    if (t != m_drawSoundTick) { m_drawSoundTick = t; m_drawSounds.clear(); }
    const long long key = (static_cast<long long>(unitId) << 16) | id;
    if (std::find(m_drawSounds.begin(), m_drawSounds.end(), key) != m_drawSounds.end()) return;
    m_drawSounds.push_back(key);
    soundPlayAt(x, y, id);
}

bool GameState::onScreen(int sx, int sy) const {
    return sx > -64 && sx < viewportW() + 64 && sy > -64 && sy < viewportH() + 64;
}

void GameState::clickCommand(int cmd) {                  // vt[1] do comando
    soundPlay(0x57);                                      // sub_40A960: botão solto dentro
    if (cmd == kCmdBuildIt) { produceBuildIt(); return; }
    if (cmd == kCmdLaunch) {                                  // sub_4573C0
        const int sp = m_storage.building();
        if (!spaceportLaunchAvailable(sp)) return;
        if (g_units[sp].padCargo == 0x6B) {                   // míssil: mira (sub_41B550 modo 3)
            m_launchFrom = sp;
            m_targetCmd = -1;
            return;
        }
        spaceportLaunch(sp, 0, 0);                            // pacote 0x19
        closePanes();
        soundPlay(0x56);
        return;
    }
    if (cmd == kCmdLoad) { m_storage.load(); return; }       // pacote 0x0B
    if (cmd == kCmdUnload) { m_storage.unload(); return; }   // pacote 0x0C
    if (cmd == kCmdStatus || cmd == kCmdCancel) { closePanes(); return; }
    if (cmd == kCmdVehicles) { closePanes(); m_vehicles.open(); return; }   // dword_567D80
    if (cmd == kCmdDetails) {
        if (m_tab == 0 && m_factories.detailsEnabled()) reportDetails(m_factories.unit());
        if (m_tab == 2 && m_resources.detailsEnabled()) {
            if (m_resources.mode() == 0) m_resources.open(m_resources.detailsMode());   // botão de modo
            else reportDetails(m_resources.unit());                                     // off_4E2D20
        }
        if (m_tab == 1 && m_labs.detailsEnabled()) {
            if (m_labs.mode() == 0) reportDetails(m_labs.lab());    // sub_4576D0
            else m_labs.open(2);                                    // botão de modo (sub_45C460)
        }
        return;
    }
    if (cmd == kCmdLabsCompleted) { if (m_tab == 1) m_labs.open(1); return; }
    if (cmd == kCmdLabsCurrent) { if (m_tab == 1) m_labs.open(0); return; }
    if (cmd == kCmdResStructures) { if (m_tab == 2) m_resources.open(m_resources.mode() + 6); return; }
    if (cmd == kCmdResSummary) { if (m_tab == 2) m_resources.open(0); return; }
    if (cmd == kCmdSpaceMode) { if (m_tab == 4) m_space.open(m_space.mode() == 0 ? 1 : 0); return; }
    if (cmd == kCmdMsgJump) {                               // sub_466A00 → sub_438EC0 → sub_408120
        int px, py;
        if (m_comms.jump(px, py)) requestCenterView(px, py);
        return;
    }
    if (cmd == kCmdJump) {                                  // sub_46B3D0
        int px, py;
        const int v = m_vehicles.jump(px, py);
        if (v) { requestCenterView(px, py); m_selected = {v}; }
        return;
    }
    if (cmd == kCmdAssign) {                                // botão volta ao painel no modo 1
        if (m_research.isOpen() && m_research.assignEnabled()) m_research.open(m_research.lab(), 1);
        return;
    }
    if (cmd == kCmdTopics) { if (m_research.isOpen()) m_research.open(m_research.lab(), 0); return; }
    if (cmd == kCmdStart) { m_research.start(); return; }
    if (cmd == kCmdStartClass) { m_students.start(); return; }
    if (cmd == kCmdYes) {                                   // confirma o comando guardado
        const int c = m_confirm.command();
        m_confirm.close();
        if (c == UC_DUMP_CARGO) { if (!m_selected.empty()) sendDumpCargo(m_selected[0]); }   // pacote 0x0E
        else if (c == UC_IDLE) { if (!m_selected.empty()) structureDoIdle(m_selected[0]); }  // pacote 0x12
        else if (c == kCmdOptRestart)  { m_gameOptions.close(); requestRestartMission(); }
        else if (c == kCmdOptMainMenu) { m_gameOptions.close(); leaveToMainMenu(); }
        else if (c == kCmdOptExit)     { SDL_Event q{}; q.type = SDL_QUIT; SDL_PushEvent(&q); }
        else if (c == kCmdOptDestroy)  { m_gameOptions.close(); destroyAllAndLeave(); }
        else if (c == kCmdOptTransfer) { m_gameOptions.close(); transferAndLeave(); }
        else uiCommandClick(UC_STOP, m_selected);           // pacote 5 (parar)
        return;
    }
    if (cmd == UC_DUMP_CARGO && m_selected.size() == 1) {   // sub_4557B0(0): pergunta antes
        closePanes();
        m_confirm.open(m_selected[0], "Are you sure you want to dump this cargo?", UC_DUMP_CARGO);
        return;
    }
    if (cmd == kCmdNo) { m_confirm.close(); return; }
    // IDLE de armazém/fundição com metal que não cabe no teto sem ele: sub_455CB0
    // pergunta antes, porque o excedente se perde. O texto do original ("All
    // metal in storage will be lost") só vale quando o prédio é o único armazém
    // daquele metal, que é o único caso em que ele pergunta; quando a perda é
    // parcial, o número entra no lugar do "All" -- ver structureIdleMetalLoss.
    if (cmd == UC_IDLE && m_selected.size() == 1) {
        int perdido = 0;
        const int perda = structureIdleMetalLoss(m_selected[0], &perdido);
        if (perda) {
            const int dono = g_units[m_selected[0]].owner & 0x0F;
            const int estoque = perda == 1 ? g_playerData[dono].ore : g_playerData[dono].rareOre;
            const std::string quanto = perdido >= estoque ? "All" : std::to_string(perdido);
            closePanes();
            m_confirm.open(m_selected[0],
                quanto + (perda == 1 ? " metal" : " rare metal") +
                    " in storage will be lost if this structure is idled.  "
                    "Confirm your idle order?",
                UC_IDLE);
            return;
        }
    }
    if (cmd == UC_ASSIGN_STUDENTS && m_selected.size() == 1) {      // sub_4564E0 → dword_56AF28
        closePanes();
        m_students.open(m_selected[0]);
        return;
    }
    if ((cmd == UC_STOP_RESEARCH || cmd == UC_STOP_TRAINING) && m_selected.size() == 1) {   // sub_457FC0 / sub_458100
        closePanes();
        m_confirm.open(m_selected[0], cmd == UC_STOP_RESEARCH
            ? "Stopping loses all research already done on this topic. Confirm your order to stop?"
            : "Stopping loses all training already done with these workers. Confirm your order to stop?");
        return;
    }
    if (cmd == UC_ASSIGN_SCIENTISTS && m_selected.size() == 1) {   // sub_456340 → unk_569110
        closePanes();
        m_research.open(m_selected[0], g_units[m_selected[0]].action == 0x16 ? 1 : 0);
        return;
    }
    if (cmd == UC_LAUNCH_PAD && m_selected.size() == 1) {   // sub_456730 → painel [+0x6C4] = 1
        closePanes();
        m_storage.open(m_selected[0], true);
        return;
    }
    if (cmd == UC_STORAGE && m_selected.size() == 1) {      // loc_456640 → painel dword_5650F0
        if (StoragePane::supports(g_units[m_selected[0]].type)) {
            closePanes();
            m_storage.open(m_selected[0]);
        } else {
            postMessage(-1, -1, "STORAGE deste predio ainda nao portado", 0);
        }
        return;
    }
    if (cmd == kCmdGameOptions) { m_gameOptions.open(); return; }                  // sub_465830
    if (cmd == kCmdOptLess || cmd == kCmdOptMore) {                                // "Turn ... down/up"
        m_gameOptions.step(cmd == kCmdOptMore ? +1 : -1);
        loadSettings();
        return;
    }
    if (cmd == kCmdOptBack) { m_gameOptions.close(); return; }                     // "Back to ..."
    // --- o menu da aba (sub_457880) ---
    if (cmd == kCmdOptObjectives) { m_gameOptions.close(); return; }               // volta aos objetivos
    if (cmd == kCmdOptPrefs) { enterPrefs(); return; }
    if (cmd == kCmdOptManual) { m_gameOptions.openManual(); return; }   // e o BACK da leitura
    if (cmd == kCmdOptEnd) { m_gameOptions.setMode(GameOptionsPane::ENDMISSION); return; }
    if (cmd == kCmdOptMenu) { m_gameOptions.setMode(GameOptionsPane::MENU); return; }
    if (cmd == kCmdPickCancel) { m_pickPlayer = false; m_pickUnits.clear(); return; }
    if (cmd <= kCmdPickPlayer0 && cmd > kCmdPickPlayer0 - kMaxPlayers) {
        sendTransfer(m_pickUnits, kCmdPickPlayer0 - cmd);   // sub_455950
        m_pickPlayer = false;
        m_pickUnits.clear();
        return;
    }
    if (cmd == kCmdAlliesChat) { m_comms.setMode(CommsPane::CHAT); return; }
    if (cmd == kCmdMessages) { m_comms.setMode(CommsPane::MESSAGES); return; }
    if (cmd == kCmdNetStats) { m_comms.setMode(CommsPane::NETSTATS); return; }
    if (cmd == kCmdOptApply) {                                                     // "&Apply new values"
        m_gameOptions.apply();                 // grava o INI, "Fullscreen" incluso
        loadSettings();
        // Extra do porte: so aqui o modo de tela vale, como todo o resto desta
        // tela. O onIdle e que tem o Renderer para aplicar.
        m_screenModeWanted = m_gameOptions.wantFullscreen() ? 1 : 0;
        return;
    }
    if (cmd == kCmdOptLoad) { m_saveDlg.open(SaveSlotsDialog::kLoad, ""); return; } // caso 1: OnLoadSlot
    if (cmd == kCmdOptSave) {                                                       // caso 2: OnSaveSlot
        m_saveDlg.open(SaveSlotsDialog::kSave, m_mission ? m_mission->levelDesc : "");
        return;
    }
    // Cada um destes confirma antes (casos 3 a 7 de sub_457880), e o YES e que
    // executa o caso 8..0xC. A confirmacao nasce marcada como "do painel de
    // opcoes", senao ela se fecha sozinha por falta de selecao.
    if (cmd == kCmdOptRestart || cmd == kCmdOptMainMenu || cmd == kCmdOptExit ||
        cmd == kCmdOptDestroy || cmd == kCmdOptTransfer) {
        const char* msg = cmd == kCmdOptRestart  ? "Are you sure you want to restart the mission?"
                        : cmd == kCmdOptMainMenu ? "Are you sure you want to abort the mission?"
                        : cmd == kCmdOptExit     ? "Are you sure you want to exit to Windows?"
                        : cmd == kCmdOptDestroy  ? "Are you sure you want to destroy all your vehicles "
                                                   "and structures and leave the game?"
                        : "Are you sure you want to transfer your vehicles to another player "
                          "and leave the game?";
        m_confirm.open(m_selected.empty() ? 0 : m_selected[0], msg, cmd, true);
        return;
    }
    if (cmd == UC_BUILD) {                                // sub_456250 → painel de produção
        if (m_selected.size() == 1) { closePanes(); m_produce.open(m_selected[0]); }
        m_targetCmd = -1;
        return;
    }
    switch (uiCommandClick(cmd, m_selected)) {
        case UiClick::TARGET: m_targetCmd = cmd; break;
        case UiClick::DONE: m_targetCmd = -1; break;
        case UiClick::CHOOSE_PLAYER:                      // sub_455A80
            m_targetCmd = -1;
            m_pickUnits = m_selected;
            m_pickPlayer = !m_pickUnits.empty();
            break;
        case UiClick::NOT_PORTED:
            postMessage(-1, -1, "Comando ainda nao portado", 0);
            break;
    }
}

// Nome curto por map_id de veículo (p/ o painel).
// Nomes de exibicao das unidades, do MANUAL do jogo (seccao "Unit Reference",
// Vehicles). O Code_Name interno do vehicles.txt NAO e o nome mostrado ao
// jogador; a coluna "Unit Name" da planilha e so um indice de letra (a..q).
// A correspondencia foi confirmada pelos dados do proprio vehicles.txt:
//   MOBILE_HOME  Cargo=25  -> Evacuation Transport ("carry up to 25 Colonists")
//   BIG_TANK     Cargo=2   -> Tiger (chassi pesado, torres DUPLAS, tracked)
//   WALL_TRUCK   Track=T   -> Earthworker (tracked; constroi Tubes/Walls)
//   REPAIR_TRUCK Owner=E   -> Repair Vehicle ("available only to Eden")
//   GEO_METRO    Owner=E   -> GeoCon ("available only to Eden")
//   SCORPION     Track=L,P -> Scorpion (legged, Plymouth)
static const char* vehName(int type) {
    switch (type) {                          // map_id REAL (off_4E1340 = SDK)
        case mapCargoTruck:          return "Cargo Truck";          // BIG_TRUCK
        case mapConVec:              return "ConVec";               // CON_TRUCK
        case mapSpider:              return "Spider";
        case mapScorpion:            return "Scorpion";
        case mapLynx:                return "Lynx";                 // LIGHT_TANK (chassi leve)
        case mapPanther:             return "Panther";              // MED_TANK   (chassi medio)
        case mapTiger:               return "Tiger";                // BIG_TANK   (chassi pesado)
        case mapRoboSurveyor:        return "Robo-Surveyor";        // SURVEYOR
        case mapRoboMiner:           return "Robo-Miner";           // MINER
        case mapGeoCon:              return "GeoCon";               // GEO_METRO
        case mapScout:               return "Scout";
        case mapRoboDozer:           return "Robo-Dozer";           // BULL_DOZER
        case mapEvacuationTransport: return "Evacuation Transport"; // MOBILE_HOME
        case mapRepairVehicle:       return "Repair Vehicle";       // REPAIR_TRUCK
        case mapEarthworker:         return "Earthworker";          // WALL_TRUCK
        default: return "Unidade";
    }
}

// Lynx/Panther/Tiger sao CHASSIS: o nome exibido e composto com a torre
// ("Laser Lynx #1-01", "EMP Lynx" -- Tutorial 4 do manual). So esses tres tem
// "Weapon enabled = Y" no vehicles.txt. Enquanto as torres nao estao portadas,
// mostramos so o chassi.
static bool vehIsCombatChassis(int type) {
    return type == mapLynx || type == mapPanther || type == mapTiger;
}

// Numero de serie mostrado pelo jogo: "#<jogador>-<NN>", contado por DONO e por
// TIPO (o manual mostra "Agridome #1-02", "Cargo Truck #1-03", "Lynx #1-01").
static int vehSerial(const UnitRecord& r, size_t idx) {
    int n = 1;
    for (size_t k = 1; k < idx && k < g_units.size(); ++k) {
        const UnitRecord& o = g_units[k];
        if (o.id == -1) continue;
        if (o.owner == r.owner && o.type == r.type) ++n;
    }
    return n;
}

// Lista de objetivos do painel (modo dword_565C8C = 1, sub_4663A0): uma linha
// por VictoryCondition, "<C0>%s" se cumprida (verde) e "<C2>%s" se não (vermelho).
void GameState::renderPanel(Renderer& renderer) {
    const int VW = m_fbW, VH = m_fbH;
    const int px0 = kColX;                       // coluna direita (x)
    uint32_t* fb = renderer.getFramebuffer();
    const int me = TethysGame::localPlayer();

    // Moldura da janela (sub_499E80) e fundo da área cliente (sub_499BC0).
    if (!m_frame.ok()) m_frame.load();
    renderer.fillRect(kViewX + kViewW, kClientY, VW - kViewX - kViewW, VH - kClientY, 0xFF000000);
    renderer.fillRect(kClientX, kStatusY, VW - kClientX, VH - kStatusY, 0xFF000000);
    renderer.fillRect(0, 0, VW, kClientY, 0xFF000000);
    renderer.fillRect(0, 0, kClientX, VH, 0xFF000000);
    m_frame.drawNonClient(fb, VW, VH, VW, VH, true, m_capPressed);
    m_frame.drawClient(fb, VW, VH, kClientX, kClientY, kClientW, kClientH, kViewW, kViewH);

    // --- Minimapa (janela 192x128 em vista+8, 0) ---
    // Cores reais do terreno (média de cada tile, sub_48B320) escurecidas por
    // coluna pela faixa de luz (sub_48EC70 + tabelas de sub_586193), como o
    // original. Prédios de todos os jogadores; veículos inimigos só na luz.
    const int mmX0 = kColX, mmY0 = kMiniMapY, mmW = kColW, mmH = kMiniMapH;
    int mw = g_gameMap.ok() ? g_gameMap.width() : 64;
    int mh = g_gameMap.ok() ? g_gameMap.height() : 64;
    // Escala e deslocamento do minimapa (MiniMap: sub_48E7F0 / sub_48E090 /
    // sub_48E390). Zoom 0 = o mapa esticado na janela; 1..5 = px por tile, com
    // a janela rolando atrás da vista.
    m_mini.update(m_camX, m_camY, viewportW(), viewportH());
    const int xoffT = g_gameMap.ok() ? g_gameMap.internalXOffset() : 0;
    auto mapX = [&](int tx) { return mmX0 + m_mini.pxX(tx + xoffT); };   // tile → pixel do minimapa
    auto mapY = [&](int ty) { return mmY0 + m_mini.pxY(ty); };
    renderer.fillRect(mmX0, mmY0, mmW, mmH, 0xFF000000);
    if (m_mini.globe()) { drawGlobe(fb); } else {
    const bool shade = g_gameMap.ok() && !TethysGame::daylightEverywhere();
    // ACRESCIMO DESTE PORTE, so no demo de atracao: o veiculo inimigo no
    // escuro continua escondido numa partida de verdade (sub_48CCD0, que e a
    // cegueira noturna do jogo), mas no demo ele aparece. Os tres ademo*.dll
    // chamam SetDaylightEverywhere(0) de proposito, entao ha noite; sem esta
    // excecao o demo mostra os tiros e as explosoes sem mostrar quem atira.
    // O ESCURECIMENTO DO TERRENO continua valendo (shade), para a noite ainda
    // ser noite -- aqui so cai a parte que esconde o inimigo.
    const bool escondeInimigo = shade && !demoGame();
    // Brilho 0..31 da coluna do port (x interna = x + deslocamento).
    auto brightness = [&](int tx) {
        return 0x1F - g_gameMap.lightDarkness(tx + g_gameMap.internalXOffset() - TethysGame::daylightPos());
    };
    // Escurecimento de um canal de 5 bits (tabelas byte_585000/585400/585C00),
    // comprimido pelo mesmo ajuste do terreno para o minimapa nao destoar.
    auto shade5 = [](int c5, int b) {
        if (c5 == 31) return 31;
        const int orig = std::min(b + 8, 33);
        const int m = 32 - (32 - orig) * nightDarkness() / 100;
        return (c5 * m) >> 5;
    };
    // Com zoom o mapa é maior que a janela: tudo sai recortado nela.
    // Mapa redondo: sub_48DB20 chama sub_48EC70 duas vezes, a segunda deslocada
    // de sub_48E5C0, senao a rolagem cilindrica deixa a janela meio preta.
    const int wrapStep = m_mini.wrapStep();
    auto mmFill = [&](int x, int y, int w, int h, uint32_t c) {
        auto one = [&](int px) {
            const int x0 = std::max(px, mmX0), y0 = std::max(y, mmY0);
            const int x1 = std::min(px + w, mmX0 + mmW), y1 = std::min(y + h, mmY0 + mmH);
            if (x1 > x0 && y1 > y0) renderer.fillRect(x0, y0, x1 - x0, y1 - y0, c);
        };
        one(x);
        if (wrapStep) one(x + wrapStep);
    };
    if (g_gameMap.ok()) {
        for (int ty = 0; ty < mh; ++ty) {
            const int py = mapY(ty), ph = mapY(ty + 1) - py;
            if (py + ph <= mmY0 || py >= mmY0 + mmH) continue;
            for (int tx = 0; tx < mw; ++tx) {
                const int px = mapX(tx), pw = mapX(tx + 1) - px;
                // Com a repeticao, um tile fora da janela aqui pode estar
                // dentro dela no segundo desenho.
                if ((px + pw <= mmX0 || px >= mmX0 + mmW) &&
                    (!wrapStep || px + wrapStep + pw <= mmX0 || px + wrapStep >= mmX0 + mmW))
                    continue;
                // Vista tatica (sub_437250 com a flag de [+0x104C]): a cor do
                // TIPO de terreno no lugar da cor media do tile, menos nas
                // celulas com bloqueio duro (bit 27), que ficam normais.
                const uint32_t cell = g_gameMap.rawCell(tx, ty);
                uint16_t c;
                if (m_mini.tactical() && !(cell & 0x08000000u))
                    c = terrainTacticalColor(static_cast<int>(cell & 0x1F));
                else
                    c = m_terrain.minimapColor(g_gameMap.getTile(LOCATION{tx, ty}));
                int r5 = (c >> 10) & 31, g5 = (c >> 5) & 31, b5 = c & 31;
                if (shade) {
                    const int b = brightness(tx);
                    r5 = shade5(r5, b); g5 = shade5(g5, b); b5 = shade5(b5, b);
                }
                mmFill(px, py, pw, ph,
                       0xFF000000u | (uint32_t(r5 << 3) << 16) | (uint32_t(g5 << 3) << 8) | uint32_t(b5 << 3));
            }
        }
    }
    static const uint32_t kOC[7] = { 0xFF4060FF,0xFFFF5040,0xFF40D070,0xFFFFD040,
                                     0xFFC060FF,0xFF40E0E0,0xFFFF9030 };
    for (int i = 1; i < (int)g_units.size(); ++i) {
        const UnitRecord& r = g_units[i];
        if (r.id == -1 || (r.flags & UNIT_DEAD) || !(r.flags & (UNIT_VEHICLE | UNIT_BUILDING))) continue;
        const int ow = r.owner & 0x0F;
        if ((r.flags & UNIT_VEHICLE) && ow != me && escondeInimigo) {
            // sub_48CCD0: veículo de quem não é aliado só aparece com a escuridão
            // da coluna < 16 (ou com os faróis acesos, flag 1).
            const bool ally = (g_playerData[me].allyMask >> ow) & 1;
            if (!ally && !(r.flags & 1) &&
                g_gameMap.lightDarkness((r.pixelX >> 5) + g_gameMap.internalXOffset() - TethysGame::daylightPos()) >= 16)
                continue;
        }
        const int tx = r.pixelX >> 5, ty = r.pixelY >> 5;
        mmFill(mapX(tx), mapY(ty), std::max(1, mapX(tx + 1) - mapX(tx)),
               std::max(1, mapY(ty + 1) - mapY(ty)), kOC[ow % 7]);
    }
    {
        const int ctx = ((m_camX / 32) % mw + mw) % mw, cty = m_camY / 32;
        const int vx = mapX(ctx), vy = mapY(cty);
        const int vw2 = mapX(ctx + viewportW() / 32) - vx, vh2 = mapY(cty + viewportH() / 32) - vy;
        mmFill(vx, vy, vw2, 1, 0xFFFFFFFF);
        mmFill(vx, vy + vh2 - 1, vw2, 1, 0xFFFFFFFF);
        mmFill(vx, vy, 1, vh2, 0xFFFFFFFF);
        mmFill(vx + vw2 - 1, vy, 1, vh2, 0xFFFFFFFF);
    }
    }
    // Botões do minimapa (sub_458930, posição [+0x310] = 0: topo, centrados):
    // x = W/2 − 32 e +16 por botão. Arte crua {0x9D Zoom In, 0x9C Zoom Out,
    // 0x9E Toggle Tactical View, 0x9A Toggle Globe View (só sem dword_54EBF0)}.
    if (g_art.ok()) {
        for (int k = 0; k < miniButtonCount(); ++k) {
            const int anim = kMiniBtnArt[k] - 0x65;
            if (anim >= g_art.numAnimations()) continue;
            const int bx = mmX0 + mmW / 2 - 0x20 + 16 * k;
            // Estilo dos botoes (dword_4E2E08) = {0, 0, 0x80, 0x80}: so os dois
            // ultimos sao de ALTERNANCIA, e o quadro 1 marca o estado ligado
            // (flag 0x40 de sub_40AEE0); os de zoom nunca acendem.
            const bool on = (k == 2 && m_mini.tactical()) || (k == 3 && m_mini.globe());
            blitArtFrame(fb, VW, VH, anim, on ? 1 : 0, bx, mmY0, -1,
                         mmX0, mmY0, mmX0 + mmW, mmY0 + mmH);
        }
    }

    // --- Janela do quadro (dword_5750D8): abas no topo + painel atual ---
    // sub_45C940: 6 abas encostadas, x = 1 + soma das larguras (sub_404D30 =
    // retângulo do header), y = 0, arte da tabela dword_4D5390 {0x8E, 0x91,
    // 0x90, 0x8F, 0x92, 0x8D} (crua; frames 0 normal / 1 atual / 2 desabilitada).
    // O conteúdo começa abaixo delas (sub_45CB00). Fundo (sub_45CCF0): preto +
    // emblema da facção centrado (anim crua 0x99 Eden / 0x540 Plymouth).
    const PaneRect pane = infoPaneRect();
    syncTabToSelection();
    {
        const int winT = kPaneWinY;                         // topo da janela = topo das abas
        const int winL = pane.l, winW = pane.r - pane.l, winH = pane.b - winT;
        renderer.fillRect(winL, winT, winW, winH, 0xFF000000);
        if (g_art.ok()) {
            const bool eden = g_playerData[me < kMaxPlayers ? me : 0].isEden != 0;
            const int emb = (eden ? 0x99 : 0x540) - 0x65;
            if (emb < g_art.numAnimations()) {
                const ArtAnimation& an = g_art.animation(emb);
                const int w = static_cast<int>(an.header[3]) - static_cast<int>(an.header[1]);
                const int h = static_cast<int>(an.header[4]) - static_cast<int>(an.header[2]);
                int ey = (winH >> 1) - (h >> 1);
                if (ey < 0x6E) ey = 0x6E;
                const int ex = (winW >> 1) - (w >> 1) - 1;
                blitArtFrame(fb, VW, VH, emb, 0, winL + ex, winT + ey, -1, winL, winT, pane.r, pane.b);
            }
            static const int kTabArt[6] = {0x8E, 0x91, 0x90, 0x8F, 0x92, 0x8D};   // dword_4D5390
            // Aba atual (sub_40A790 com 0x40) = frame 1; desabilitada = frame 2
            // (sub_45C770: abas 0..3 em missão só de unidades).
            const int current = m_tab;
            int x = 1;
            for (int k = 0; k < 6; ++k) {
                const int anim = kTabArt[k] - 0x65;
                if (anim >= g_art.numAnimations()) continue;
                const ArtAnimation& an = g_art.animation(anim);
                // Botão (sub_40AEE0): sub_403E00 com o hotspot no canto do retângulo.
                const int fr = (k <= 3 && reportsDisabled()) ? 2 : k == current ? 1 : 0;
                blitArtFrame(fb, VW, VH, anim, fr, winL + x, winT, -1,
                             kColX, kPaneWinY, kColX + kColW, pane.t);
                x += static_cast<int>(an.header[3]) - static_cast<int>(an.header[1]);
            }
        }
    }

    // Texto do objeto de ajuda (unk_575570): a descrição do controle sob o
    // cursor ou, sobre o mapa, as coordenadas do tile (sub_436E50 monta "x,y").
    std::string help;
    // --- Quadro de informação da seleção (classe de painel @off_4D5220) ---
    m_produce.update(m_selected);
    m_storage.update(m_selected);
    m_research.update(m_selected);
    m_students.update(m_selected);
    m_confirm.update(m_selected);
    m_vehicles.update();
    m_comms.update();
    m_factories.update();
    m_labs.update();
    m_resources.update();
    m_space.update();
    if (m_pickPlayer) {
        // sub_46D3B0: o titulo do escolhedor, com a fonte de titulo.
        drawMarkupText(fb, VW, VH, PaneRect{pane.l + 0xD, pane.t + 2, pane.r - 0xE, pane.t + 0x1A},
                       "Transfer Vehicle(s) to:", 0xFF00FF00u, true, kMarkupLine);
    } else if (m_tab == 5 && m_mission && m_confirm.isOpen())
        m_confirm.draw(fb, VW, VH, pane);                // sub_45C0B0 no lugar do painel
    else if (m_tab == 5 && m_mission && m_gameOptions.isOpen())
        m_gameOptions.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_tab == 5 && m_mission)
        m_objectives.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_tab == 2 && m_resources.isOpen())
        m_resources.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_tab == 4 && m_space.isOpen())
        m_space.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_tab == 3 && m_comms.isOpen())
        m_comms.draw(fb, VW, VH, kColX, kPaneWinY, pane, m_mouseNX, m_mouseNY);
    else if (m_tab == 0 && m_factories.isOpen())
        m_factories.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_tab == 1 && m_labs.isOpen())
        m_labs.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_vehicles.isOpen())
        m_vehicles.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_confirm.isOpen())
        m_confirm.draw(fb, VW, VH, pane);
    else if (m_students.isOpen())
        m_students.draw(fb, VW, VH, kColX, kPaneWinY, pane, m_mouseNX, m_mouseNY);
    else if (m_research.isOpen())
        m_research.draw(fb, VW, VH, kColX, kPaneWinY, pane, m_mouseNX, m_mouseNY);
    else if (m_produce.isOpen())
        m_produce.draw(fb, VW, VH, pane, m_mouseNX, m_mouseNY);
    else if (m_storage.isOpen()) {
        // A caixa da doca fica 0x33 acima da fileira de botões mais alta.
        int topBtn = kPaneBottom - 2 - 25;
        for (const TextButton& b : structureButtons()) topBtn = std::min(topBtn, b.t);
        m_storage.draw(fb, VW, VH, kColX, kPaneWinY, pane, topBtn, m_mouseNX, m_mouseNY);
    }
    else if (!m_selected.empty())
        drawUnitInfoPane(fb, VW, VH, pane, m_selected[0], static_cast<int>(m_selected.size()), me);
    if (m_produce.isOpen()) help = m_produce.helpText();
    // Botoeira do veículo (sub_46D540): ícones verdes do op2_art (anim crua − 0x65).
    {
        const std::vector<CmdButton> btns = commandButtons();
        const CmdButton* hover = nullptr;
        for (const CmdButton& b : btns) {
            const int anim = b.iconRaw - 0x65;
            const bool over = m_mouseNX >= b.l && m_mouseNX < b.r && m_mouseNY >= b.t && m_mouseNY < b.b;
            if (over) hover = &b;
            // Aceso (sub_40AEE0, flag 0x40): os ícones de comando não têm quadro
            // próprio p/ isso ([+0x8A] = 0xFFFF, ver sub_4554B0), então o
            // original desenha o MESMO quadro 1 px abaixo e à direita e pinta as
            // arestas esquerda e superior em 0x39CF — o efeito de botão afundado.
            const bool lit = over || b.cmd == m_targetCmd;
            const int d = lit ? 1 : 0;
            if (g_art.ok() && anim >= 0 && anim < g_art.numAnimations()) {
                // Botão (sub_40AEE0): sub_403E00 com o hotspot no canto do retângulo.
                blitArtFrame(fb, VW, VH, anim, 0, b.l + d, b.t + d, -1, px0, 0, VW, VH);
            }
            if (lit) {
                renderer.fillRect(b.l, b.t, 1, b.b - b.t, kBevelLit);
                renderer.fillRect(b.l, b.t, b.r - b.l, 1, kBevelLit);
            }
        }
        // A ajuda do botão sob o cursor vai para a linha de estado (sub_40AA44:
        // o botão pede o texto ao próprio comando e o entrega a unk_575570),
        // não para uma caixa flutuante.
        if (hover) help = hover->tip;
    }
    // Botões de texto do prédio: arte 0x98 (87x25) / 0x97 larga (175x25), com os
    // quadros do descritor (sub_4554B0, variante de texto): 0 normal, 1 aceso e
    // 0xFFFF p/ desabilitado — que, não existindo, o original desenha como o
    // quadro 0 deslocado de 1 px (sub_40AEE0). O rótulo sai em 0xDDFF22 e, com a
    // flag de desabilitado, em 0x636363 (o `and 0FFBF9C7Ah / add 636363h` do
    // fim de sub_40AEE0); aceso, ele também desce 1 px.
    for (const TextButton& b : structureButtons()) {
        const bool over = !b.disabled && m_mouseNX >= b.l && m_mouseNX < b.r &&
                          m_mouseNY >= b.t && m_mouseNY < b.b;
        const int anim = b.wide ? 0x97 - 0x65 : 0x98 - 0x65;
        const int d = b.disabled ? 1 : 0;
        if (g_art.ok() && anim < g_art.numAnimations()) {
            // Botão (sub_40AEE0): sub_403E00 com o hotspot no canto do retângulo.
            blitArtFrame(fb, VW, VH, anim, over ? 1 : 0, b.l + d, b.t + d, -1, px0, 0, VW, VH);
        }
        if (over) help = b.tip;
        // O rotulo sai centrado, com a letra do '&' em VERMELHO -- e assim que
        // o original marca o atalho dentro do botao (a mesma convencao da
        // linha de ajuda, onde ela ainda vem sublinhada).
        const size_t amp = b.label.find('&');
        std::string antes, acc, depois;
        if (amp == std::string::npos || amp + 1 >= b.label.size()) {
            antes = b.label;
        } else {
            antes = b.label.substr(0, amp);
            acc = b.label.substr(amp + 1, 1);
            depois = b.label.substr(amp + 2);
        }
        const int td = over ? 1 : 0;
        const uint32_t cor = b.disabled ? 0xFF636363 : 0xFFDDFF22;
        const uint32_t corAcc = b.disabled ? 0xFF636363 : 0xFFFF2020;
        const int largura = UiFont::width(antes) + UiFont::width(acc) + UiFont::width(depois);
        int tx = (b.l + b.r) / 2 - largura / 2 + td;
        const int ty = UiFont::centeredY(b.t, b.b) + td;
        UiFont::draw(fb, VW, VH, antes, tx, ty, cor);
        tx += UiFont::width(antes);
        UiFont::draw(fb, VW, VH, acc, tx, ty, corAcc);
        tx += UiFont::width(acc);
        UiFont::draw(fb, VW, VH, depois, tx, ty, cor);
    }

    // --- Barra de status sob a vista (sub_4998D0 / sub_499A30) ---
    // A fonte AQUI nao e a dos paineis: sub_499A30 seleciona TFrame+0x14, que
    // sub_49B140 cria com lfHeight **+12** negrito. Positivo e altura de
    // CELULA, entao os glifos saem menores que os da fonte de −11 usada no
    // resto da interface -- e por isso a barra do original parece miuda.
    // Retângulos na área cliente: linha 1 em y = (H & ~31) + 6, linha 2 12 px
    // abaixo; contadores de minério em x = vista - 0x51 + 8 (ícone 211 comum /
    // 210 raro + número em branco); texto à esquerda até vista - 0x51.
    {
        const int y1 = kClientY + kViewH + 6, y2 = y1 + 12;
        const int oreX = kClientX + kViewW - 0x51 + 8;
        const PlayerData& pd = g_playerData[me < kMaxPlayers ? me : 0];
        char num[16];
        m_frame.drawPiece(fb, VW, VH, 23, oreX, y1 + 2);                 // 211 (comum)
        std::snprintf(num, sizeof(num), "%d", pd.ore);
        UiFont::draw(fb, VW, VH, num, oreX + m_frame.width(23) + 2, y1, 0xFFFFFFFF, UiFont::kStatus);
        // [+0xA8]: linha do minério raro. PROVISÓRIO: mostrada quando há raro.
        if (pd.rareOre > 0) {
            m_frame.drawPiece(fb, VW, VH, 24, oreX, y2 + 2);             // 210 (raro)
            std::snprintf(num, sizeof(num), "%d", pd.rareOre);
            UiFont::draw(fb, VW, VH, num, oreX + m_frame.width(24) + 2, y2, 0xFFFFFFFF, UiFont::kStatus);
        }
        // Linha 1: o objeto de ajuda (unk_575570) — a descrição do controle sob o
        // cursor ou, sobre o mapa, "x,y" (sub_436E50: x = tile − deslocamento
        // interno + 1, y = tile + 1). sub_411BF0 guarda o texto em MAIÚSCULAS e
        // sub_411990 o desenha em branco ([+0x9C] = 0xFFFFFF), com a letra depois
        // do '&' em vermelho ([+0xA0] = 0xFF) e sublinhada (PatBlt de 1 px).
        int tx0 = kClientX + 2;
        const int mvx = m_mouseNX - kViewX, mvy = m_mouseNY - kViewY;
        if (help.empty() && mvx >= 0 && mvx < viewportW() && mvy >= 0 && mvy < viewportH()) {
            int tx = (m_camX + mvx) / 32, ty = (m_camY + mvy) / 32;
            if (g_gameMap.ok()) tx &= (g_gameMap.width() - 1);
            char c[32]; std::snprintf(c, sizeof(c), "%d,%d", tx + 1, ty + 1);
            help = c;
        }
        if (!help.empty()) {
            std::string up;
            for (char ch : help) up += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const size_t amp = up.find('&');
            int hx = tx0;
            const int lh = UiFont::lineHeight(UiFont::kStatus);
            auto put = [&](const std::string& s, uint32_t c) {
                UiFont::draw(fb, VW, VH, s, hx, y1, c, UiFont::kStatus);
                hx += UiFont::width(s, UiFont::kStatus);
            };
            if (amp == std::string::npos || amp + 1 >= up.size()) {
                put(up, 0xFFFFFFFFu);
            } else {
                put(up.substr(0, amp), 0xFFFFFFFFu);
                const std::string acc = up.substr(amp + 1, 1);
                const int accX = hx, accW = UiFont::width(acc, UiFont::kStatus);
                put(acc, 0xFFFF0000u);
                renderer.fillRect(accX, y1 + lh - 1, accW, 1, 0xFFFF0000u);
                put(up.substr(amp + 2), 0xFFFFFFFFu);
            }
        }
        const std::string& msg = (m_targetCmd >= 0 && !uiCommandHint().empty()) ? uiCommandHint() : statusBarText();
        std::string clipped = msg;
        while (!clipped.empty() && tx0 + UiFont::width(clipped, UiFont::kStatus) > oreX - 8) clipped.pop_back();
        UiFont::draw(fb, VW, VH, clipped, tx0, y2, 0xFFE8E8E8, UiFont::kStatus);
    }
}

} // namespace op2
