#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui_state.h"
#include "../game/game_map.h"
#include "../game/terrain_renderer.h"
#include "../game/start_info.h"
#include "location.h"
#include "produce_pane.h"
#include "vehicle_list_pane.h"
#include "comms_pane.h"
#include "factories_pane.h"
#include "labs_pane.h"
#include "resources_pane.h"
#include "space_pane.h"
#include "objectives_pane.h"
#include "storage_pane.h"
#include "research_pane.h"
#include "game_frame.h"
#include "save_dialog.h"
#include "game_options_pane.h"
#include "minimap.h"
#include "globe.h"
#include "cursors.h"

namespace op2 {

struct WeaponDrawOp;
struct MissionScript;

// UIState do jogo — equivale à UIState estática do engine (dword_574A80, criada
// uma vez em TApp::CreateFrames). Por enquanto desenha o terreno da missão e rola
// com as setas; a simulação (TethysGame/Unit) entra nos próximos slices.
class GameState : public UIState {
public:
    // Carrega o mapa de uma missão (nome do .map, resolvido do export MapName da
    // DLL). false se o mapa/tilesets não forem encontrados.
    bool load(const std::string& mapName);
    // Inicia uma missão a partir da DLL (T##/E##/P##.DLL) — single player de
    // sub_4893A0: DescBlock, mapa, jogadores, InitProc; triggers por turno.
    bool loadMission(const std::string& dllName, const GameStartInfo* gsi = nullptr,
                     const std::string* savePath = nullptr);
    // Inicia uma missão já montada (DescBlock e InitProc em código, sem DLL):
    // o mesmo caminho de loadMission depois de carregar a DLL. Usado pelo modo
    // --harness (src/harness_mode.cpp).
    bool startMission(std::unique_ptr<MissionScript> mission, const GameStartInfo* gsi = nullptr,
                      const std::string* savePath = nullptr);
    // Semente do RNG da próxima partida (o GameStartInfo do original a traz;
    // sem ela vale 0x12345678).
    void setSeed(uint32_t seed) { m_seed = seed; }
    // Um tick de simulação, sem relógio nem desenho (o corpo do laço de onIdle).
    // true = a partida acabou neste tick.
    bool simTick();
    // Jogo salvo (TApp::SaveGame / LoadGame): grava a partida em andamento /
    // recarrega a DLL e o mapa da missão do arquivo e restaura o estado.
    bool saveToFile(const std::string& path, const std::string& description);
    bool loadSavedGame(const std::string& path);
    bool gameEnded() const { return m_endPhase != 0; }   // vitória/derrota: turnos parados
    // TethysGame::LoadGame (export p/ as DLLs): carga adiada (PostDelayedLoadMessage).
    void requestDelayedLoad(const std::string& path) { m_pendingLoad = path; }
    GameState();
    ~GameState();
    bool hasMap() const { return m_terrain.ok(); }
    const std::string& mapName() const { return m_mapName; }

    void onActivate() override;
    void onIdle(Renderer& renderer, float dt) override;
    void handleEvent(Renderer& renderer, const SDL_Event& e) override;

    void setShowPassability(bool v) { m_showPassability = v; }
    void setSelection(const std::vector<int>& ids) { m_selected = ids; }
    // Testes: abre a lista de veículos, clica na linha `row` e (opcional) JUMP.
    void debugVehicleList(int row, bool jump);
    // Testes: clica na aba k (0..5) do quadro; na de mensagens escolhe `row`.
    void debugTab(int k, int row = -1);
    void debugLabs(int mode, int row) { m_labs.open(mode == 2 ? 2 : mode); if (row >= 0) m_labs.select(row); }
    void debugResources(int mode, int row) { openReport(2, mode); if (row >= 0) m_resources.select(row); }
    void debugSpace(int mode) { openReport(4, mode); }
    void pressCommand(int cmd) { clickCommand(cmd); }      // testes: botão do painel
    bool pickingPlayer() const { return m_pickPlayer; }    // testes: o escolhedor abriu?
    void pickPlayer(int p) { clickCommand(kCmdPickPlayer0 - p); }   // testes: escolhe
    void pickCancel() { clickCommand(kCmdPickCancel); }             // testes: desiste
    ResearchPane& researchPaneForTest() { return m_research; }
    void setMouse(int nx, int ny) { m_mouseNX = nx; m_mouseNY = ny; }
    void setCamera(int x, int y) { m_camX = x; m_camY = y; clampCamera(); }
    int camX() const { return m_camX; }
    int camY() const { return m_camY; }
    int shakeX() const { return m_shakeX; }
    int shakeY() const { return m_shakeY; }

    // Layout da janela do jogo em 640x480 (sub_499E80 / sub_4998D0): moldura
    // com título de 22 px e bordas de 12 → área cliente 616x446 em (12,22):
    //   vista (0,0) 416x416 [(W-200)&~31, H&~31]; filete de 8 px; minimapa
    //   (vista+8, 0) 192x128; faixa de 6; janela do quadro (vista+8, 0x86)
    //   192 x resto, com as abas no topo e o conteúdo 42 px abaixo
    //   (sub_45CB00); barra de status sob a vista.
    //
    // Fork: o quadro do jogo acompanha o tamanho da janela (como o original e
    // o OPU, cuja janela é redimensionável). A coluna direita tem largura fixa
    // e fica presa à borda direita (o filete de sub_499BC0 está em R − 0xC8);
    // a vista ocupa o resto. Os valores abaixo são os de 640x480 e são
    // recalculados por relayout(). Diferença do original: a vista preenche a
    // área toda em vez de arredondar para múltiplo de 32, para não sobrar faixa
    // vazia entre a vista e a coluna.
    static constexpr int kClientX = 12, kClientY = 22;
    static constexpr int kViewX = kClientX, kViewY = kClientY;
    static constexpr int kColW = 0xC0;                        // 192
    static constexpr int kMiniMapY = kClientY, kMiniMapH = 0x80;
    static constexpr int kPaneWinY = kClientY + 0x86;         // 156: janela do quadro (abas)
    static constexpr int kPaneTop = kPaneWinY + 42;           // 198: conteúdo
    static constexpr int kStatusH = 30;                       // barra de status
    int kClientW = 616, kClientH = 446;
    int kViewW = 416, kViewH = 416;
    int kColX = kClientX + 416 + 8;                           // 436
    int kPaneBottom = kClientY + 446;                         // 468
    int kStatusY = kViewY + 416;                              // 438
    int m_fbW = 640, m_fbH = 480;                             // quadro inteiro
    // Recalcula o layout para um quadro de fbW x fbH (mínimo 640x480).
    void relayout(int fbW, int fbH);

private:
    void loadSettings();
    // sub_407A10: a rolagem da vista. A borda da janela (2 px, como o
    // GetWindowRect do original) e as setas pedem a MESMA coisa, e a
    // velocidade por eixo ([+0x14C] e [+0x148]) acelera enquanto alguem pede e
    // desacelera quando ninguem pede. O minimapa acompanha sozinho: ele e
    // redesenhado da camera a cada quadro.
    void scrollView(Renderer& renderer);
    // Qual cursor de comando mostrar agora: o par "pode"/"não pode" do comando
    // em mira, testado na célula (ou na unidade) sob o ponteiro. Fora de
    // qualquer mira, a seta. Ver cursors.h.
    int cursorForHover() const;
    // A conta, sem SDL: quem le a borda e o teclado e o scrollView. Separada
    // para o --scrolltest poder exercita-la sem mouse nem janela.
    void scrollApply(bool esq, bool dir, bool cima, bool baixo);
    int scrollStep() const;
public:
    // A regra da borda do original: o ponteiro a 2 px ou menos do canto da
    // JANELA (GetWindowRect menos 2) pede o mesmo que a seta, e a esquerda
    // ganha da direita (o original testa um e so olha o outro se o primeiro
    // nao pediu). Pura e estatica para o --scrolltest medir sem mouse.
    static void scrollWanted(int mx, int my, int w, int h, bool naJanela,
                             bool kEsq, bool kDir, bool kCima, bool kBaixo,
                             bool& esq, bool& dir, bool& cima, bool& baixo);
private:
    int m_scrollVX = 0, m_scrollVY = 0;         // [+0x14C] / [+0x148]
    int m_scrollRate = 4;                       // "ScrollRate" do INI (1..6)
    int m_gameSpeedValue = 20;                  // TethysGame +0x50 (GameSpeed * 4)
    void enterPrefs();                          // PREFERENCES (sub_465830 modo 2)
    // Extra do porte (o radio "Screen Mode"): o clique nao tem o Renderer na
    // mao -- clickCommand recebe so o id do comando --, entao o pedido fica
    // marcado aqui e o onIdle, que tem, resolve no quadro seguinte.
    void applyScreenMode(Renderer& renderer);
    int  m_screenModeWanted = -1;               // 0/1 pedidos pelo APPLY; -1 = nada
    bool m_screenModeSync = false;              // acertar o espelho com a janela real
    bool loadMap(const std::string& mapName);
    std::unique_ptr<MissionScript> m_mission;   // dword_4EFCD8
    // GameStartInfo copiada em TethysGame+0xA4 (sub_4893A0) e o fim da partida
    // (sub_49C830): 1 = espera de 2 s, 2 = diálogo "Success" (Dialog 262).
    GameStartInfo m_gsi;
    std::string m_pendingLoad;
    SaveSlotsDialog m_saveDlg;         // Ctrl+S / Ctrl+L (comandos 40017 / 40018)
    bool m_endAfterSave = false;       // "Save Game" da caixa Success
    std::vector<uint8_t> minimapStateSave();   // sub_420580
    bool minimapStateLoad(const std::vector<uint8_t>& blob);   // sub_420640
    std::vector<uint8_t> uiStateSave();
    bool uiStateLoad(const std::vector<uint8_t>& blob);
    int m_endPhase = 0;
    int m_pauseCount = 0;          // TFrame+0x1E0: as pausas empilhadas
    uint32_t m_endWaitStart = 0;
    void beginGameEnd();
    void finishGameEnd();
    void drawSuccessDialog(Renderer& renderer);
    int successButtonAt(int x, int y) const;
    std::string m_mapName;
    TerrainRenderer m_terrain;   // desenha sobre o g_gameMap (singleton do engine)
    int m_camX = 0, m_camY = 0;
    uint32_t m_drawSoundTick = 0xFFFFFFFFu;     // sons de desenho já tocados neste tick
    std::vector<long long> m_drawSounds;
    void drawSound(int unitId, int x, int y, int id);
    bool onScreen(int sx, int sy) const;
    int m_maxX = 0, m_maxY = 0;
    // --- passeio da camera no demo de atracao (so deste porte) ---
    bool m_demoCam = true;         // "[Game] DemoCamera"
    int  m_demoCamHold = 0;        // tiques ate escolher o proximo ponto
    int  m_demoCamSeq = 0;         // rodizio entre os candidatos (sem usar o RNG)
    int  m_demoTargX = -1, m_demoTargY = -1;   // centro de vista perseguido
    void demoCameraTick();         // escolhe o ponto e rola ate ele
    float m_tickAccum = 0.0f;     // acumulador p/ avançar o tick de simulação
    float m_tickPeriod = 0.05f;   // s/tick: 1/clamp(GameSpeed*4, 4, 40) (sub_49BF80)
    int m_lastRand = 0;           // última amostra do RNG (1 por tick), p/ o HUD
    uint32_t m_seed = 0x12345678u; // semente da próxima partida (setSeed)
    bool m_showPassability = false; // overlay de debug (tecla G)
    bool m_showDebugHud = false;    // overlay de depuração (Tick/RNG/economia; tecla F3)
    bool m_stationaryShadows = false; // INI StationaryShadows (dword_575634)
    bool m_overlay = true;           // "Overlay" (dword_57562C): luz de status dos prédios
    bool m_mobileShadows = false;     // INI MobileShadows (dword_575638)
    int  m_frameSkip = 1;             // INI FrameSkip (Value)
    // "AILevel" do INI: 0 = AUTO (vale o que a partida escolheu), 1..3 forcam
    // Easy/Normal/Hard. Ver applyAiLevel em activateMission.
    int m_aiLevel = 0;
    int  m_shakeX = 0, m_shakeY = 0;  // tremor acumulado (vista +0x138/+0x13C)

    // --- Seleção / interação (a "MAINE" / command pane) ---
    std::vector<int> m_selected;   // ids de unidades selecionadas
    // Hover (sub_41B620/loc_41B6B9): sem seleção, a unidade sob o cursor há >10
    // ticks ganha o colchete (cinza) + barra. +0x14 = unidade, +0x18 = tick base.
    int  m_hoverUnit = 0;
    int  m_hoverStartTick = 0;
    int  m_capPressed = -1;        // botão da barra de título apertado (dword_4EA240)
    MiniMap m_mini;                // escala/zoom/deslocamento do minimapa
    int  miniButtonCount() const;  // 4 botões (3 nos mapas guardados dobrados)
    // Visão do globo: a esfera (unk_547258) e a superfície do minimapa que ela
    // usa de textura ([+0x10A4], largura do mapa × altura + 2 margens, RGB555).
    // O original pinta as unidades NESSA superfície (sub_48CCD0) e depois as
    // apaga (sub_48D2B0) — aqui a lista m_globeDirty faz o mesmo papel.
    Globe m_globe;
    std::vector<uint16_t> m_globeTex;
    std::vector<std::pair<int, uint16_t>> m_globeDirty;
    // dword_547290 só é reescrito quando a VISTA se move (sub_407A10 /
    // sub_408120); a câmera que vem de um jogo salvo não conta, senão o
    // `[+0x38] = [+0x4C]` do fim de sub_420640 não teria efeito.
    int  m_globeCamX = 0;
    int  globeSpinTargetOf(int camX) const;
    void buildGlobeTerrain();      // sub_437250 na superfície do globo
    void drawGlobe(uint32_t* fb);
    int  unitAtView(int vx, int vy) const;   // unidade (qualquer dono) sob o cursor, raio 20
public:
    void debugGameOptions() { m_gameOptions.open(); }   // só p/ --panetest
    // Captura (--panetest): a tela ALLIES/CHAT da aba Communications.
    void debugAlliesChat() { m_comms.setMode(CommsPane::CHAT); }
    void debugNetStats() { m_comms.setMode(CommsPane::NETSTATS); }
    // Captura (--panetest): entra numa das telas da aba Options. 0 menu,
    // 1 preferencias, 2 fim de missao, 3 manual, 4 um topico do manual.
    void debugOptionsScreen(int qual) {
        m_gameOptions.open();
        if (qual == 1) enterPrefs();
        else if (qual == 2) m_gameOptions.setMode(GameOptionsPane::ENDMISSION);
        else if (qual >= 3) {
            m_gameOptions.openManual();
            if (qual == 4) m_gameOptions.debugOpenTopic(1);
        }
    }
    // --optmenutest: a aba Options pela mesma porta do jogador. Acha o botão
    // pelo rótulo e clica nele; um botão desabilitado não responde, como no
    // clique de verdade.
    bool debugOptClick(const std::string& rotulo) {
        for (const TextButton& b : structureButtons())
            if (b.label == rotulo) {
                if (b.disabled) return false;
                clickCommand(b.cmd);
                return true;
            }
        return false;
    }
    // A dica do botao de rotulo `rotulo` (vazia se nao existe).
    std::string debugOptTip(const std::string& rotulo) const {
        for (const TextButton& b : const_cast<GameState*>(this)->structureButtons())
            if (b.label == rotulo) return b.tip;
        return std::string();
    }
    std::vector<std::string> debugOptLabels() const {
        std::vector<std::string> out;
        for (const TextButton& b : structureButtons()) out.push_back(b.label);
        return out;
    }
    bool debugOptDisabled(const std::string& rotulo) const {
        for (const TextButton& b : structureButtons())
            if (b.label == rotulo) return b.disabled;
        return false;
    }
    void debugOptSelect(int i) { m_gameOptions.debugSelect(i); }
    void debugOptScroll(int linha) { m_gameOptions.debugScroll(linha); }
    // --selecttest: o clique na vista e o que ficou marcado.
    void debugSelectAt(int vx, int vy) { selectAt(vx, vy, false); }
    void debugClearSelection() { m_selected.clear(); }
    int debugSelectedCount() const { return static_cast<int>(m_selected.size()); }
    int debugSelectedId(int i) const {
        return (i >= 0 && i < static_cast<int>(m_selected.size())) ? m_selected[i] : 0;
    }
    int debugUnitAtView(int vx, int vy) const { return unitAtView(vx, vy); }
    // --cursortest: o cursor que apareceria com o ponteiro na vista em
    // (vx,vy) e o comando `cmd` em mira (-1 = nenhum).
    int debugCursor(int vx, int vy, int cmd) {
        setMouse(vx + kViewX, vy + kViewY);
        m_targetCmd = cmd;
        return cursorForHover();
    }
    // --scrolltest: um quadro de rolagem com os pedidos dados na mao, sem
    // mouse nem janela, e os valores que o caso mede.
    void debugScrollFrame(bool esq, bool dir, bool cima, bool baixo) {
        scrollApply(esq, dir, cima, baixo);
    }
    int debugScrollStep() const { return scrollStep(); }
    int debugScrollVX() const { return m_scrollVX; }
    int debugScrollVY() const { return m_scrollVY; }
    // As setas de rolagem NAO podem cair debaixo de um botao: era isso que
    // tornava o fim da lista do PREFERENCES inalcancavel.
    bool debugOptArrowsClear() const {
        const PaneRect p = infoPaneRect();
        const ListBoxLayout L = m_gameOptions.debugLayout(p);
        for (const TextButton& b : structureButtons())
            for (const PaneRect& s : {L.up, L.down})
                if (s.l < b.r && s.r > b.l && s.t < b.b && s.b > b.t) return false;
        return true;
    }
    // Clica na seta de baixo, onde ela esta, e devolve a 1a linha visivel.
    int debugOptPageDown() {
        const PaneRect p = infoPaneRect();
        const ListBoxLayout L = m_gameOptions.debugLayout(p);
        bool mudou = false, voltou = false;
        m_gameOptions.click(p, (L.down.l + L.down.r) / 2, (L.down.t + L.down.b) / 2,
                            mudou, voltou);
        return m_gameOptions.debugTop();
    }
    // O clique vai ao PAINEL antes dos botoes (ver handleEvent): se a lista
    // alcanca o lugar do botao, ela come o clique e o botao nunca responde.
    bool debugOptPaneEats(const std::string& rotulo) {
        for (const TextButton& b : structureButtons())
            if (b.label == rotulo) {
                bool mudou = false, voltou = false;
                return m_gameOptions.click(infoPaneRect(), (b.l + b.r) / 2, (b.t + b.b) / 2,
                                           mudou, voltou);
            }
        return false;
    }
    int debugOptLines() const { return m_gameOptions.debugLineCount(infoPaneRect()); }
    int debugOptRows() const { return m_gameOptions.debugLayout(infoPaneRect()).rows; }
    std::string debugOptItem(int i) const { return m_gameOptions.debugItemText(i); }
    std::string debugOptTitle() const { return m_gameOptions.paneTitle(); }
    // O radio "Screen Mode": o clique na linha e o modo escolhido. Quem
    // aplica na janela e o onIdle, e o teste chama o onIdle de verdade.
    void debugOptRow(int i) { m_gameOptions.debugRowClick(i); }
    bool debugWantFullscreen() const { return m_gameOptions.wantFullscreen(); }
    bool debugConfirmOpen() const { return m_confirm.isOpen(); }
    // --optmenutest: o ESC pela mesma porta da tecla, o texto da pergunta que
    // ele levanta, a aba em que o jogo ficou e a mira em curso.
    std::string debugConfirmText() const { return m_confirm.message(); }
    void debugEscape(Renderer& r) {
        SDL_Event ev{};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_ESCAPE;
        handleEvent(r, ev);
    }
    int debugTabNow() const { return m_tab; }
    int debugArmed() const { return m_targetCmd; }
    // --pausetest: a pausa pela mesma porta da tecla, e o que ela deixou.
    void debugCtrlP(Renderer& r) {
        SDL_Event ev{};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_p;
        ev.key.keysym.mod = KMOD_LCTRL;
        handleEvent(r, ev);
    }
    int debugPauseCount() const { return m_pauseCount; }
    bool debugAnyDrag() const { return m_dragging || m_areaDrag || m_miniDrag; }
    // --pausetest: a pausa por dentro. O Ctrl+P so ALTERNA, entao e por aqui
    // que se empilham as pausas, como o jogo faz noutras situacoes.
    void debugPauseGame(bool on) { pauseGame(on); }
    // --bannertest: a vista em coordenadas de TELA, para o teste saber onde o
    // letreiro deveria estar centrado.
    void debugViewRect(int& x, int& y, int& w, int& h) const {
        x = kViewX; y = kViewY; w = viewportW(); h = viewportH();
    }
    // O quadro seguinte: é aqui que a confirmação se fechava sozinha.
    void debugPaneFrame() { m_confirm.update(m_selected); }
    void debugSetHover(int id) { m_hoverUnit = id; m_hoverStartTick = -1000; }  // só p/ --selshot
    void debugArmCommand(int cmd) { m_targetCmd = cmd; }                        // só p/ --placeshot
    void debugMiniZoom(int n) { for (int k = 0; k < n; ++k) m_mini.zoomIn(); }   // só p/ --minishot
    int  debugMiniZoomLevel() const { return m_mini.zoom(); }
    void debugMiniTactical() { m_mini.toggleTactical(); }   // so p/ --minishot
    // Escuridão da coluna (byte_54EC14) — usada pelo render e pelo --playshot.
    static int tileDarkness(int tileX);
    void debugMiniGlobe() { m_mini.toggleGlobe(); }         // so p/ --globeshot
    bool debugMiniTacticalOn() const { return m_mini.tactical(); }
    bool debugMiniGlobeOn() const { return m_mini.globe(); }
    int  debugGlobeRotation() const { return m_globe.rotation(); }
    void debugGlobeSpin(int d) { m_globe.rotateBy(d); }     // so p/ --globeshot
    // so p/ --globeshot: mundo -> tela -> mundo (sub_4208B0 / sub_420750)
    bool debugGlobeRoundTrip(int wx, int wy, int& sx, int& sy, int& bx, int& by) const {
        return m_globe.project(wx, wy, sx, sy) && m_globe.unproject(sx, sy, bx, by);
    }
    void debugAreaDrag(int cmd) {            // só p/ --placeshot: arrasto de 3 tiles
        m_targetCmd = cmd; m_areaDrag = true;
        m_areaSX = m_camX + m_mouseNX - kViewX - 96;
        m_areaSY = m_camY + m_mouseNY - kViewY - 96;
    }
private:
    bool m_dragging = false;       // arrastando caixa de seleção?
    int  m_dragSX = 0, m_dragSY = 0;   // início da caixa (coords nativas)
    int  m_mouseNX = 0, m_mouseNY = 0; // mouse atual (coords nativas)
    int  m_targetCmd = -1;         // comando em modo de mira (UiCommand) ou -1
    bool m_areaDrag = false;
    // Minimapa (sub_4583D0/sub_4587B0/sub_41F9E0): botão apertado nele = flags 1|4
    // de dword_57489C; enquanto segurado, a vista centra no ponto (sub_408120).
    bool m_miniDrag = false;
    bool miniToWorld(int nx, int ny, int& px, int& py) const;   // sub_48D860 (zoom 0)       // arrastando a área de um comando (muro/terraplanar)
    int  m_areaSX = 0, m_areaSY = 0;   // início do arrasto (pixel do mundo)
    void areaRect(int ex, int ey, int& x1, int& y1, int& x2, int& y2) const;
    static constexpr int kCmdLoad = -12, kCmdUnload = -13;   // STORAGE de recursos
    static constexpr int kCmdLaunch = -14;                   // LAUNCH do painel da plataforma (dword_564A28)
    int m_launchFrom = 0;                                    // Spaceport mirando o míssil EMP (sub_457480)

    // Botoeira do veículo (sub_46D540): botões montados p/ a seleção atual.
    struct CmdButton { int cmd; int l, t, r, b; int iconRaw; std::string tip; char hotkey; };
    std::vector<CmdButton> commandButtons() const;
    // Botões de texto do prédio (sub_459F70 + layout sub_458D10). wide = arte 0x97.
    struct TextButton { int cmd; int l, t, r, b; bool wide; std::string label, tip; char hotkey;
                        bool disabled = false; };
    // Painel de produção (BUILD da fábrica) e seus botões (sub_4682C0).
    ProducePane m_produce;
    VehicleListPane m_vehicles;    // lista de veículos (Alt+V / &VEHICLES do Robot Command Center)
    ObjectivesPane m_objectives;   // aba 5 (unk_565AC8: Game Options / objetivos)
    GameOptionsPane m_gameOptions; // "&GAME OPTIONS" da aba 5
    CommsPane m_comms;             // aba 3 (dword_566510: Communications)
    FactoriesPane m_factories;     // aba 0 (dword_5657C8: Factories Report)
    LabsPane m_labs;               // aba 1 (dword_568248: Labs)
    ResourcesPane m_resources;     // aba 2 (dword_567920: Resources)
    SpacePane m_space;             // aba 4 (dword_56A0A8: Space Program)
    void closeTabPanes() {
        m_comms.close(); m_factories.close(); m_labs.close(); m_resources.close(); m_space.close();
    }
    // DescBlock+0xC (missão só de unidades): abas 0..3 desabilitadas (sub_45C770)
    // e os atalhos dos relatórios ignorados (sub_498890).
    bool reportsDisabled() const;
    void openReport(int k, int mode);   // sub_45D020 direto (atalhos): a seleção fica
    // Painel de aba aberto no quadro (índice em dword_4D5390) ou −1 = o status da
    // seleção (sub_45CC30: unidade escolhida, ou nada).
    int m_tab = -1;
    std::vector<int> m_lastSel;    // p/ notar troca de seleção (sub_45CB60 → [+0x484])
    void syncTabToSelection();
    bool tabRect(int k, PaneRect& r) const;
    void openTab(int k);           // sub_45C620
    StoragePane m_storage;         // STORAGE (baias)
    ResearchPane m_research;       // ASSIGN (pesquisa do laboratório)
    StudentsPane m_students;       // ASSIGN (alunos da University)
    ConfirmPane m_confirm;         // confirmação dos STOP
    static constexpr int kCmdAssign = -5, kCmdCancel = -6, kCmdStart = -7, kCmdTopics = -8;
    static constexpr int kCmdYes = -9, kCmdNo = -10, kCmdStartClass = -11;
    void closePanes() {
        m_produce.close(); m_storage.close(); m_research.close(); m_students.close(); m_confirm.close();
        m_vehicles.close();
    }
    bool anyPaneOpen() const {
        return m_produce.isOpen() || m_storage.isOpen() || m_research.isOpen() || m_students.isOpen() || m_confirm.isOpen() ||
               m_vehicles.isOpen();
    }
    static constexpr int kCmdGameOptions = -4;
    // -70..-72 e NAO -20..-22: nesses tres os ids colidiam com
    // kCmdLabsCurrent, kCmdResStructures e kCmdResSummary, cujos tratadores
    // vem antes em clickCommand e saem com `return` em QUALQUER aba -- o
    // L&ESS e o &MORE do PREFERENCES nao faziam nada.
    static constexpr int kCmdOptLess = -70, kCmdOptMore = -71, kCmdOptBack = -72;
    // Os seis itens do menu da aba (sub_457880) e os da tela END MISSION.
    static constexpr int kCmdOptObjectives = -30, kCmdOptManual = -31, kCmdOptPrefs = -32;
    static constexpr int kCmdOptLoad = -33, kCmdOptSave = -34, kCmdOptEnd = -35;
    static constexpr int kCmdOptRestart = -36, kCmdOptMainMenu = -37, kCmdOptExit = -38;
    static constexpr int kCmdOptDestroy = -39, kCmdOptApply = -40, kCmdOptMenu = -41;
    static constexpr int kCmdOptTransfer = -73;   // "&TRANSFER" (so em multijogador)
    // Aba Communications em multijogador (sub_4670A0).
    static constexpr int kCmdAlliesChat = -42, kCmdMessages = -43, kCmdNetStats = -44;
    // Escolhedor de jogador do TRANSFER (sub_46D230): um botao por jogador,
    // -50 - i, mais a desistencia.
    static constexpr int kCmdPickPlayer0 = -50, kCmdPickCancel = -57;
    static constexpr int kCmdVehicles = -15, kCmdJump = -16;   // &VEHICLES (RCC) / &JUMP da lista
    static constexpr int kCmdMsgJump = -17;                    // &JUMP das mensagens (sub_466A00)
    static constexpr int kCmdDetails = -18;                    // &DETAILS dos relatórios (sub_458CC0)
    static constexpr int kCmdLabsCompleted = -19, kCmdLabsCurrent = -20;   // modos 1 / 0 (sub_45C460)
    static constexpr int kCmdResStructures = -21, kCmdResSummary = -22;    // Resources: modo+6 / 0
    static constexpr int kCmdSpaceMode = -23;                              // &STARSHIP / &SATELLITES
    void reportDetails(int unit);                              // sub_457760
    static constexpr int kCmdBuildIt = -2, kCmdStatus = -3;
    PaneRect infoPaneRect() const;
    void produceBuildIt();
    std::vector<TextButton> structureButtons() const;
    void clickCommand(int cmd);

    int viewportW() const;         // largura da vista do mapa (416)
    int viewportH() const { return kViewH; }   // altura da vista (416)
    GameFrame m_frame;             // moldura (bitmaps do OUT2RES.DLL)
    // X de tela de um pixel de mundo, tratando o WRAP horizontal do mapa
    // (o engine faz o mapa dar a volta em X; unidades do outro lado da emenda
    // precisam aparecer no lado certo da câmera).
    int screenXOf(int worldPixelX) const;
    void drawBeam(Renderer& renderer, const WeaponDrawOp& op);
    void clampCamera();
    bool viewWraps() const;        // horizontal wrap of the view (MapWrap / world maps)
    bool m_mapWrapAll = false;     // "[Game] MapWrap" (fork): wrap every map
    int viewMaxY() const;          // lowest camera Y (clip rect bottom, or map bottom)
    bool m_showBottomRow = false;  // "[Game] ShowBottomRow" (fork): show the map's last row
    // A linha de conversa do jogo (o filtro de teclado sub_4115F0, instalado no
    // TFrame por sub_49AE60). Fechada, o Enter a abre; aberta, ela come as
    // teclas: Enter manda, Esc desiste, Backspace apaga e o resto entra ate
    // 0x3F caracteres, que e o que cabe no corpo do pacote 0x30.
    bool chatKey(const SDL_Event& e);
    void drawChatLine(uint32_t* fb, int vw, int vh) const;

    // "Transfer Vehicle(s) to:" aberto? Guarda a selecao do momento do clique,
    // porque o painel toma a frente ate a escolha.
    bool m_pickPlayer = false;
    std::vector<int> m_pickUnits;

    bool m_chatOpen = false;        // [filtro+0x78]
    std::string m_chatLine;         // [filtro+0x0C], tamanho em [+0x7C]
    // Itens da tela END MISSION da aba Options, depois da confirmacao.
    void requestRestartMission();   // recarrega a MESMA missao, do zero
    void destroyAllAndLeave();      // destroi o que e meu e encerra a partida
    void leaveToMainMenu();         // entrega a colonia a IA e sai (pacote 0x31)
    void transferAndLeave();        // doa as unidades ao aliado e sai
    void selectAt(int nx, int ny, bool additive);
    // O ESC dentro da partida. Ordem: desiste da mira, fecha o que estiver
    // aberto e, por fim, pergunta se e para sair da missao -- com a pergunta do
    // proprio jogo ("Are you sure you want to abort the mission?") e pelo mesmo
    // caminho do "&BACK TO MAIN MENU" da tela END MISSION.
    void escapePressed();
    // A pausa do Ctrl+P. `pauseGame` e o sub_49BFE0: um CONTADOR ([+0x1E0]),
    // porque o jogo se pausa sozinho em outras situacoes alem da tecla; so a
    // subida de 0 para 1 e a descida para 0 e que agem. `togglePause` e o
    // comando 40001 (Ctrl+P e a tecla PAUSE no acelerador 130 do OUT2RES).
    void pauseGame(bool on);
    void togglePause();
    // As cinco passadas do sub_4085C0: contorno preto de +-2 px e vermelho.
    void drawBanner(Renderer& renderer, const std::string& msg);
    // A area em que o clique (e o realce) acertam a unidade, em coordenadas da
    // VISTA. O predio responde na pegada INTEIRA -- a mesma que ele ocupa no
    // mapa --, e nao num circulo no meio dele; o veiculo responde numa caixa do
    // tamanho do sprite em volta do seu centro. Empate: o centro mais perto.
    void unitHitBox(int id, int& x, int& y, int& w, int& h) const;
    int unitAtBox(int vx, int vy, bool meusApenas) const;
    void selectBox(int x0, int y0, int x1, int y1);
    void renderPanel(Renderer& renderer);
};

} // namespace op2
