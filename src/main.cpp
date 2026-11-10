#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <set>
#include "gfx/renderer.h"
#include "gfx/ui_font.h"
#include "shell/shell_menu.h"
#include "shell/scores.h"
#include "core/config.h"
#include "gfx/pbmp.h"
#include "res/res_manager.h"
#include "res/winhelp.h"
#include "res/ani_cursor.h"
#include "app/cursors.h"
#include "game/game_map.h"
#include "game/walls.h"
#include "game/garage.h"
#include "audio/music.h"
#include "audio/sound.h"
#include "game/salvage.h"
#include "game/terrain_renderer.h"
#include "game/game_rng.h"
#include "game/player.h"
#include "game/unit.h"
#include "game/tethys_game.h"
#include "game/pathfinder.h"
#include "game/command.h"
#include "game/command_apply.h"
#include "game/movement.h"
#include "game/structure.h"
#include "game/weapon.h"
#include "game/trigger.h"
#include "game/disaster.h"
#include "game/commands.h"
#include "game/messages.h"
#include "game/pinwheel.h"
#include "mission/mission.h"
#include "harness_mode.h"
#include "game/research.h"
#include "game/start_info.h"
#include "game/save_game.h"
#include "game/building_sheet.h"
#include "game/colony.h"
#include "game/cheat.h"
#include "game/missile.h"
#include "game/repair.h"
#include "game/structure_anim.h"
#include "game/production.h"
#include "game/docking.h"
#include "game/construction.h"
#include "game/mining.h"
#include "game/earthwork.h"
#include "game/ai_group.h"
#include "game/scenario.h"
#include "game/commands.h"
#include "game/research_lab.h"
#include "game/research.h"
#include "gfx/artfile.h"
#include "app/tapp.h"
#include "app/game_state.h"
#include "app/shell_state.h"
#include "net/net_game.h"
#include "net/net_lobby.h"

// --unittest: exercita o pool de unidades + o wrapper Unit.
static int runUnitTest() {
    op2::resetUnits();
    op2::Unit b, v;
    op2::TethysGame::CreateUnit(b, op2::mapCommandCenter, op2::LOCATION{10, 10}, 0);
    op2::TethysGame::CreateUnit(v, 1, op2::LOCATION{12, 10}, 1);
    auto show = [](const char* nm, op2::Unit& u) {
        op2::LOCATION l = u.Location();
        std::printf("%s id=%d live=%d type=%d owner=%d loc=(%d,%d) bld=%d veh=%d\n",
                    nm, u.id(), u.IsLive(), u.GetType(), u.OwnerID(), l.x, l.y,
                    u.IsBuilding(), u.IsVehicle());
    };
    show("building", b);
    show("vehicle ", v);
    v.DoDeath();   // marca morta (flag 0x20000); slot continua ocupado (fiel)
    std::printf("apos DoDeath: vehicle live=%d (morta, mas slot nao e liberado)\n", v.IsLive());
    op2::Unit v2;
    op2::TethysGame::CreateUnit(v2, 2, op2::LOCATION{5, 5}, 0);
    std::printf("novo veiculo id=%d type=%d (novo slot, pois morta != removida)\n",
                v2.id(), v2.GetType());

    // Teto de unidades (kMaxLiveUnits = 2048, como o OPUPatch). Nao e numero
    // de gosto: o id precisa caber nos 11 bits da ocupacao da celula
    // (CELL_OCCUPANCY = 0x07FF0000), entao o maior que cabe e 2047. Enche ate
    // recusar e confere onde parou.
    int criadas = 0;
    bool recusou = false;
    for (int k = 0; k < 4000; ++k) {
        op2::Unit u;
        if (!op2::TethysGame::CreateUnit(u, 1, op2::LOCATION{3 + (k % 60), 3 + (k / 60) % 60}, 0)) {
            recusou = true;
            break;
        }
        ++criadas;
    }
    const int ultimo = static_cast<int>(op2::g_units.size()) - 1;
    const bool cabe = ultimo <= 0x7FF;
    const bool tetoOk = recusou && ultimo == op2::kMaxLiveUnits - 1 && cabe;
    std::printf("teto: criou mais %d, recusou=%d, maior id=%d (cabe nos 11 bits: %d) %s\n",
                criadas, recusou ? 1 : 0, ultimo, cabe ? 1 : 0, tetoOk ? "ok" : "FALHOU");
    return tetoOk ? 0 : 1;
}

// --playertest: exercita a API de _Player (wrapper de índice + array global).
static int runPlayerTest() {
    op2::resetPlayers(4);
    op2::Player[0].GoEden();     op2::Player[0].SetOre(5000); op2::Player[0].SetWorkers(16);
    op2::Player[1].GoPlymouth(); op2::Player[1].SetOre(1234); op2::Player[1].GoAI();
    op2::Player[0].SetSolarSat(99);  // deve saturar em 15
    for (int i = 0; i < 2; ++i) {
        const op2::_Player& p = op2::Player[i];
        std::printf("Player %d: %s %s  Ore=%d Workers=%d SolarSat=%d\n",
                    i, p.IsEden() ? "EDEN" : "PLYMOUTH", p.IsHuman() ? "HUMAN" : "AI",
                    p.Ore(), p.Workers(), p.SolarSat());
    }
    std::printf("P0.Ore=%d  P1.Ore=%d (independentes)  P0.SolarSat(clamp)=%d\n",
                op2::Player[0].Ore(), op2::Player[1].Ore(), op2::Player[0].SolarSat());
    return 0;
}

// --rngtest <seedHex> <count>: imprime a sequência do RNG (next cru e getRand),
// para validar o port contra uma transcrição independente do x86.
static int runRngTest(const char* seedStr, const char* countStr) {
    uint32_t seed = static_cast<uint32_t>(strtoul(seedStr, nullptr, 0));
    int count = atoi(countStr);
    op2::GameRNG rng;
    rng.setSeed(seed);
    std::printf("seed=0x%08X  s0=0x%08X s1=0x%08X\n", seed, rng.s0(), rng.s1());
    op2::GameRNG r2; r2.setSeed(seed);
    for (int i = 0; i < count; ++i) {
        uint32_t raw = rng.next();
        int rnd = r2.getRand(100);
        std::printf("%3d  next=0x%08X  getRand(100)=%d\n", i, raw, rnd);
    }
    return 0;
}

// Modo utilitário (sem SDL): extrai um recurso via ResManager e grava em disco.
// Usado para validar o leitor de VOL/LZHUF contra o decoder Python (byte a byte).
//   outpost2_modern.exe --extract <nome_no_vol> <arquivo_saida>
static int runExtract(const std::string& name, const std::string& outPath) {
    if (!op2::g_ResManager.init(".")) return 2;
    auto stream = op2::g_ResManager.openStream(name);
    if (!stream) {
        std::cerr << "[Extract] recurso nao encontrado: " << name << std::endl;
        return 3;
    }
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::cerr << "[Extract] nao foi possivel criar: " << outPath << std::endl;
        return 4;
    }
    out.write(reinterpret_cast<const char*>(stream->data()),
              static_cast<std::streamsize>(stream->size()));
    std::cout << "[Extract] " << name << " -> " << outPath
              << " (" << stream->size() << " bytes)" << std::endl;
    return 0;
}

// Grava um buffer ARGB (w x h, top-down) como BMP 32-bit.
static bool writeBmp32(const std::string& path, const uint32_t* px, int w, int h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint8_t hdr[54] = {
        'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0,
        40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 32,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
    };
    uint32_t fileSize = 54 + static_cast<uint32_t>(w) * h * 4;
    memcpy(&hdr[2], &fileSize, 4);
    memcpy(&hdr[18], &w, 4);
    int negH = -h;  // top-down
    memcpy(&hdr[22], &negH, 4);
    fwrite(hdr, 1, 54, f);
    fwrite(px, 4, static_cast<size_t>(w) * h, f);
    fclose(f);
    return true;
}

// Modo utilitário (sem SDL): renderiza um mapa inteiro com terreno para um BMP.
//   outpost2_modern.exe --rendermap <nome.map> <saida.bmp>
static int runRenderMap(const std::string& mapName, const std::string& outPath) {
    if (!op2::g_ResManager.init(".")) return 2;
    auto mapStream = op2::g_ResManager.openStream(mapName);
    if (!mapStream) { std::cerr << "[RenderMap] mapa nao encontrado: " << mapName << std::endl; return 3; }
    if (!op2::g_gameMap.load(mapStream->data(), mapStream->size())) {
        std::cerr << "[RenderMap] .map invalido: " << mapName << std::endl; return 4;
    }
    op2::TerrainRenderer terrain;
    if (!terrain.init(op2::g_gameMap, op2::g_ResManager)) {
        std::cerr << "[RenderMap] falha ao carregar tilesets" << std::endl; return 5;
    }
    int W = terrain.mapPixelWidth();
    int H = terrain.mapPixelHeight();
    std::vector<uint32_t> buf(static_cast<size_t>(W) * H, 0xFF000000u);
    terrain.render(buf.data(), W, H, 0, 0);
    if (!writeBmp32(outPath, buf.data(), W, H)) {
        std::cerr << "[RenderMap] nao foi possivel gravar: " << outPath << std::endl; return 6;
    }
    std::cout << "[RenderMap] " << mapName << " -> " << outPath
              << " (" << W << "x" << H << ", "
              << op2::g_gameMap.width() << "x" << op2::g_gameMap.height() << " tiles, "
              << op2::g_gameMap.tilesets().size() << " tilesets)" << std::endl;
    return 0;
}

// Visualizador de terreno em-engine: abre a janela e rola o mapa com as setas.
//   outpost2_modern.exe --game <nome.map>
static int runGameView(const std::string& mapName) {
    if (!op2::g_ResManager.init(".")) return 2;
    auto mapStream = op2::g_ResManager.openStream(mapName);
    if (!mapStream || !op2::g_gameMap.load(mapStream->data(), mapStream->size())) {
        std::cerr << "[GameView] mapa invalido: " << mapName << std::endl; return 3;
    }
    op2::TerrainRenderer terrain;
    if (!terrain.init(op2::g_gameMap, op2::g_ResManager)) {
        std::cerr << "[GameView] falha ao carregar tilesets" << std::endl; return 4;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init falhou: " << SDL_GetError() << std::endl; return 5;
    }
    op2::Renderer renderer;
    if (!renderer.init("Outpost 2 - Terreno (setas p/ rolar)", 1280, 960, false)) {
        SDL_Quit(); return 6;
    }

    const int VW = op2::Renderer::NATIVE_WIDTH;
    const int VH = op2::Renderer::NATIVE_HEIGHT;
    int maxX = terrain.mapPixelWidth() - VW;
    int maxY = terrain.mapPixelHeight() - VH;
    if (maxY < 0) maxY = 0;
    // começa no clipRect (área jogável), como o jogo
    int camX = op2::g_gameMap.clipRect().x1 * 32;
    int camY = op2::g_gameMap.clipRect().y1 * 32;

    bool running = true;
    uint32_t last = SDL_GetTicks();
    while (running) {
        uint32_t now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        int speed = static_cast<int>(600 * dt);
        if (speed < 1) speed = 1;
        if (ks[SDL_SCANCODE_LEFT])  camX -= speed;
        if (ks[SDL_SCANCODE_RIGHT]) camX += speed;
        if (ks[SDL_SCANCODE_UP])    camY -= speed;
        if (ks[SDL_SCANCODE_DOWN])  camY += speed;
        if (camY < 0) camY = 0;
        if (camY > maxY) camY = maxY;
        // X faz wrap no engine; deixamos rolar livremente.

        renderer.clear(0xFF000000);
        terrain.render(renderer.getFramebuffer(), VW, VH, camX, camY);
        renderer.present();

        uint32_t frame = SDL_GetTicks() - now;
        if (frame < 16) SDL_Delay(16 - frame);
    }
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// Modo de captura (um frame de uma tela do shell), para screenshots.
//   outpost2_modern.exe [--menu|--tutorials|--campaign|--prefs|--colony|--about] --screenshot <out.bmp>
static int runScreenshot(int argc, char* argv[], const std::string& outPath) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << std::endl; return 1; }
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 screenshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::ShellMenu shellMenu;
    shellMenu.init("OP2SHRES.DLL");
    op2::SaveSlotsDialog slotsDlg;                           // --savedlg / --loaddlg
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--savedlg") slotsDlg.open(op2::SaveSlotsDialog::kSave, "Eden Mission 1");
        else if (a == "--loaddlg") slotsDlg.open(op2::SaveSlotsDialog::kLoad, "");
        else if (a == "--tutorials") shellMenu.showTutorials();
        else if (a == "--menu") shellMenu.showMainMenu();
        else if (a == "--campaign") shellMenu.showNewCampaign();
        else if (a == "--prefs") shellMenu.showPreferences();
        else if (a == "--colony") shellMenu.showColonyGames();
        else if (a == "--about") shellMenu.showAbout();
        else if (a == "--help2") shellMenu.showHelp();
        else if (a == "--uialpha" && i + 2 < argc) {
            shellMenu.debugSetUiAlpha(atoi(argv[i + 1]), atoi(argv[i + 2]));
            shellMenu.showMainMenu();
        }
        else if (a == "--scores") shellMenu.showScores();
        else if (a == "--ename") shellMenu.showEnterName("Colony: Eden Starship");
        else if (a == "--mpstats") shellMenu.showMpStats();
        else if (a == "--mpopts")  shellMenu.showMpScreen(0, true, 1);
        else if (a == "--mpnew")   shellMenu.showMpScreen(1, true, 1);
        else if (a == "--mpfind")  shellMenu.showMpScreen(2, false, 1);
        else if (a == "--mppick")  shellMenu.showMpScreen(3, false, 1);
        else if (a == "--mpjoin")  shellMenu.showMpScreen(4, false, 1);
        else if (a == "--mpsettings") shellMenu.showMpScreen(5, true, 2);
        else if (a == "--mpcombo") { shellMenu.showMpScreen(5, true, 2); shellMenu.debugOpenCombo(40001); }
        else if (a == "--mpcombo2") { shellMenu.showMpScreen(5, true, 2); shellMenu.debugOpenCombo(40032); }
        else if (a == "--mpsettings2") shellMenu.showMpScreen(5, false, 2);
        else if ((a == "--cmenu" || a == "--cbrief" || a == "--cobj" || a == "--cchap" || a == "--cstory") &&
                 i + 1 < argc) {
            bool eden = true;
            int dif = 1;
            for (int j = 1; j < argc; ++j) {
                if (std::string(argv[j]) == "--plymouth") eden = false;
                if (std::string(argv[j]) == "--hard") dif = 2;
                if (std::string(argv[j]) == "--easy") dif = 0;
            }
            const int scr = a == "--cmenu" ? 0 : a == "--cbrief" ? 1 : a == "--cobj" ? 2 : a == "--cchap" ? 3 : 4;
            shellMenu.debugCampaign(eden, dif, atoi(argv[i + 1]), scr);
        }
    }
    renderer.clear(0xFF000000);
    shellMenu.render(renderer);
    slotsDlg.render(renderer);
    bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                         renderer.getWidth(), renderer.getHeight());
    std::cout << "[Screenshot] " << (ok ? "salvo em " : "falha ao salvar ") << outPath << std::endl;
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --selshot <DLL> <out.bmp>: carrega a missão, seleciona o 1º veículo (ou
// prédio), centra a câmera nele e salva um quadro — p/ ver o colchete de seleção.
static int runSelShot(const std::string& dll, const std::string& outPath, bool hover) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << std::endl; return 1; }
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 selshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    for (int i = 0; i < 3; ++i) gs.onIdle(renderer, 0.1f);
    int pick = 0;
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id != -1 && (r.flags & op2::UNIT_VEHICLE)) { pick = (int)i; break; }
    }
    if (!pick) for (size_t i = 1; i < op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1) { pick = (int)i; break; }
    if (pick) {
        if (hover) gs.debugSetHover(pick); else gs.setSelection({pick});
        gs.setCamera(op2::unitOX(op2::g_units[pick]) - 416 / 2,
                     op2::unitOY(op2::g_units[pick]) - 416 / 2);
    }
    gs.onIdle(renderer, 0.0f);
    bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                         renderer.getWidth(), renderer.getHeight());
    std::cout << "[SelShot] unidade " << pick << " -> " << (ok ? outPath : "falha") << std::endl;
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --scoretest: grava uma entrada no placar [ColonyGameScores], relê e restaura
// o Count original (valida o formato do OP2Shell.dll).
static int runScoreTest() {
    op2::Config cfg("OUTPOST2.INI");
    cfg.load();
    const int before = cfg.getInt("ColonyGameScores", "Count", 0);
    op2::ScoreEntry e;
    e.game = "Colony: Eden Starship"; e.difficulty = op2::scoreDifficultyName(8);
    e.time = 4269; e.name = "COMMANDER"; e.date = op2::scoreToday();
    op2::scoresAdd(e);
    const std::vector<op2::ScoreEntry> all = op2::scoresLoad();
    std::printf("[ScoreTest] Count %d -> %d\n", before, (int)all.size());
    if (!all.empty()) std::printf("[ScoreTest] linha: %s\n", op2::scoreLine(all.back()).c_str());
    op2::Config back("OUTPOST2.INI");                 // restaura o Count original
    back.load();
    back.setInt("ColonyGameScores", "Count", before);
    back.save();
    std::printf("[ScoreTest] restaurado Count=%d\n", before);
    return 0;
}

// --fonttest: métricas da fonte da interface (sub_415C00: tmHeight em [+0x44],
// tmAscent em [+0x48]) e a largura de algumas cadeias do original, para conferir
// o espaçamento das linhas dos painéis contra as posições fixas do original.
static int runFontTest() {
    std::printf("[FontTest] normal: altura=%d ascent=%d | negrito: altura=%d ascent=%d\n",
                op2::UiFont::lineHeight(false), op2::UiFont::ascent(false),
                op2::UiFont::lineHeight(true), op2::UiFont::ascent(true));
    std::printf("[FontTest] barra de estado (Arial +12 negrito, TFrame+0x14):"
                " altura=%d ascent=%d | \"FOOD STORES ARE PLENTIFUL\" = %d px"
                " (nos paineis daria %d)\n",
                op2::UiFont::lineHeight(op2::UiFont::kStatus),
                op2::UiFont::ascent(op2::UiFont::kStatus),
                op2::UiFont::width("FOOD STORES ARE PLENTIFUL", op2::UiFont::kStatus),
                op2::UiFont::width("FOOD STORES ARE PLENTIFUL"));
    const char* kSamples[] = {"Added to Storage", "Food in Storage", "Mission Objectives (Alt+O)",
                              "Display Completed Objectives", "0123456789"};
    for (const char* s : kSamples)
        std::printf("[FontTest] \"%s\" largura=%d (negrito %d)\n", s,
                    op2::UiFont::width(s, false), op2::UiFont::width(s, true));
    // A página Food desenha 8 linhas a partir do topo e "Added to Storage" em
    // +0x6E (sub_469FD0); com a altura de linha certa as duas não se tocam.
    const int lh = op2::UiFont::lineHeight(false);
    std::printf("[FontTest] página Food: 8 linhas = %d, \"Added to Storage\" em %d -> %s\n",
                8 * lh, 0x6E, 8 * lh <= 0x6E ? "OK" : "SOBREPOE");
    op2::uiFontDumpSizes();
    return 0;
}

// --minishot <map> <out.bmp> <zooms>: aplica N vezes o Zoom In do minimapa e
// grava a tela — confere a escala/rolagem de sub_48E7F0/48E090/48E390.
static int runMiniShot(const std::string& mapName, const std::string& outPath, int zooms,
                       bool tactical, int camTileX) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 minishot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    gs.debugMiniZoom(zooms);
    if (tactical) gs.debugMiniTactical();
    if (camTileX) gs.setCamera(camTileX * 32, gs.camY());
    for (int i = 0; i < 3; ++i) gs.onIdle(renderer, 0.05f);
    const bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                               renderer.getWidth(), renderer.getHeight());
    std::printf("[MiniShot] mapa %dx%d (interna %d, desl %d) zoom=%d -> %s\n",
                op2::g_gameMap.width(), op2::g_gameMap.height(), op2::g_gameMap.internalWidth(),
                op2::g_gameMap.internalXOffset(), gs.debugMiniZoomLevel(),
                tactical ? " tatica" : "", outPath.c_str());
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --globeshot <map> <out.bmp> <zooms> [quadros] [tac]: liga a visao do globo (o
// 4o botao do minimapa, so nos mapas de largura 512) e grava a tela.
static int runGlobeShot(const std::string& mapName, const std::string& outPath, int zooms,
                        int spin, bool tactical, int sun) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 globeshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    gs.debugMiniZoom(zooms);
    if (tactical) gs.debugMiniTactical();
    gs.debugMiniGlobe();
    // Posicao do sol: o campo [+0x48] do globo e a fase do dia (dword_5472A0).
    if (sun) op2::TethysGame::setDaylightPhase(sun, op2::g_gameMap.internalWidth());
    // O globo gira ate o alvo (a coluna da camera) com aceleracao, entao sao
    // precisos varios quadros ate ele se assentar.
    for (int i = 0; i < (spin > 0 ? spin : 3); ++i) gs.onIdle(renderer, 0.05f);
    const bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                               renderer.getWidth(), renderer.getHeight());
    // O topo e a base do MAPA projetados: tem de cair onde a textura deixa de
    // ser a margem preta (o alinhamento que o original erra).
    {
        int sx = 0, sy0 = 0, sy1 = 0, bx = 0, by = 0;
        gs.debugGlobeRoundTrip(0, 0, sx, sy0, bx, by);
        gs.debugGlobeRoundTrip(0, op2::g_gameMap.height() * 32 - 1, sx, sy1, bx, by);
        std::printf("[GlobeShot] topo do mapa -> y=%d; base -> y=%d%c", sy0, sy1, 10);
    }
    // Ida e volta: o clique no globo (sub_420750) tem de desfazer a projecao.
    for (int k = 0; k < 3; ++k) {
        const int wx = gs.camX() + (k - 1) * 64 * 32, wy = gs.camY() + 10 * 32;
        int sx = 0, sy = 0, bx = 0, by = 0;
        if (gs.debugGlobeRoundTrip(wx & (op2::g_gameMap.internalWidth() * 32 - 1), wy,
                                   sx, sy, bx, by))
            std::printf("[GlobeShot] mundo %d,%d -> tela %d,%d -> mundo %d,%d (erro %d,%d)",
                        wx, wy, sx, sy, bx, by, bx - (wx & (op2::g_gameMap.internalWidth() * 32 - 1)),
                        by - wy), std::printf("%c", 10);
    }
    std::printf("[GlobeShot] mapa %dx%d (interna %d, desl %d) zoom=%d raio=%d quadros=%d%s -> %s\n",
                op2::g_gameMap.width(), op2::g_gameMap.height(), op2::g_gameMap.internalWidth(),
                op2::g_gameMap.internalXOffset(), gs.debugMiniZoomLevel(),
                50 + gs.debugMiniZoomLevel() * 10, spin, tactical ? " tatica" : "",
                outPath.c_str());
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --placeshot <map> <out.bmp> <tipo>: ConVec com o kit do tipo, comando "Build
// structure kit" armado e o cursor no meio da vista — mostra o tingimento dos
// tiles do retângulo de posicionamento (sub_41BC50 / sub_451CA0).
static int runPlaceShot(const std::string& mapName, const std::string& outPath, int type,
                        int dx, int dy) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 placeshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    int cc = -1;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapCommandCenter) { cc = i; break; }
    if (cc < 0) { renderer.shutdown(); SDL_Quit(); return 3; }
    op2::Unit u;
    const op2::LOCATION l{(op2::g_units[cc].pixelX >> 5) + 4, (op2::g_units[cc].pixelY >> 5) + 4};
    if (!op2::TethysGame::CreateUnit(u, op2::mapConVec, l, op2::g_units[cc].owner & 0x0F,
                                     type < 0 ? 0x24 : type))
        { renderer.shutdown(); SDL_Quit(); return 4; }
    gs.setSelection({u.id()});
    gs.setMouse(op2::GameState::kViewX + gs.kViewW / 2 + dx,
                op2::GameState::kViewY + gs.kViewH / 2 + dy);
    // tipo < 0: em vez do kit, arrasta a área do comando −tipo (muro, terraplanar…)
    // a partir de 3 tiles acima e à esquerda do cursor.
    if (type < 0) {
        gs.debugAreaDrag(-type);
    } else {
        gs.debugArmCommand(op2::UC_BUILD_KIT);
    }
    for (int i = 0; i < 5; ++i) gs.onIdle(renderer, 0.05f);
    const bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                               renderer.getWidth(), renderer.getHeight());
    std::printf("[PlaceShot] kit 0x%02X -> %s\n", type, outPath.c_str());
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

static uint64_t simHash();

// --uitest: exercita a lista suspensa dos combos pelo caminho do mouse e do
// teclado, sem rede nenhuma.
// --helptest [topico]: le o OUTPOST2.HLP e imprime o que o leitor entendeu.
static int runHelpTest(const std::string& alvo) {
    op2::WinHelpFile hlp;
    if (!hlp.load("OUTPOST2.HLP")) {
        std::printf("[HelpTest] falhou: %s%c", hlp.error().c_str(), 10);
        return 2;
    }
    std::printf("[HelpTest] \"%s\": %d topicos%c", hlp.manualTitle().c_str(),
                (int)hlp.topics().size(), 10);
    int paras = 0, vazios = 0;
    for (const op2::HelpTopic& t : hlp.topics()) {
        paras += (int)t.paras.size();
        if (t.title.empty() || t.paras.empty()) ++vazios;
    }
    std::printf("[HelpTest] %d paragrafos; topicos sem titulo ou sem texto: %d%c",
                paras, vazios, 10);
    for (const op2::HelpTopic& t : hlp.topics()) {
        if (!alvo.empty() && t.title != alvo) continue;
        std::printf("[HelpTest] ----- %s (%d paragrafos)%c", t.title.c_str(),
                    (int)t.paras.size(), 10);
        int n = 0;
        for (const op2::HelpPara& par : t.paras) {
            if (++n > 12) break;
            std::string linha;
            for (size_t i = 0; i < par.cells.size(); ++i) {
                if (par.cells[i].empty()) continue;
                if (!linha.empty()) linha += par.table ? " | " : "";
                linha += par.cells[i];
            }
            if (linha.size() > 120) linha = linha.substr(0, 120) + "...";
            std::printf("   [%s] %s%c", par.table ? "tab" : "par", linha.c_str(), 10);
        }
        if (!alvo.empty()) break;
    }
    std::fflush(stdout);
    if (!alvo.empty()) return 0;

    // E a tela: abre o manual e percorre a lista e o texto pelo teclado.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 helptest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::ShellMenu menu;
    menu.init("OP2SHRES.DLL");
    std::string relato;
    const bool ok = menu.helpTest(relato);
    std::printf("[HelpTest] tela: %s%c", relato.c_str(), 10);
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --demotest: monta o pedido do demo de atracao tres vezes e confere a
// rotacao e o GameStartInfo, sem precisar esperar o minuto de ociosidade.
static int runDemoTest(const std::string& prefixo) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 demotest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::ShellMenu menu;
    menu.init("OP2SHRES.DLL");
    std::string relato;
    bool ok = menu.demoTest(relato);
    std::printf("[DemoTest] %s%c", relato.c_str(), 10);

    // O relogio da ociosidade recomeca ao voltar ao menu. Ele guardava o
    // instante de ANTES da partida, e depois de um minuto jogando o demo de
    // atracao entrava assim que o menu voltava -- o ESC caia direto no demo.
    {
        op2::Config ini("OUTPOST2.INI");
        ini.load();
        const int gravado = ini.getInt("Game", "Demo", 0);
        op2::ShellState sh;
        sh.init("OP2SHRES.DLL");
        const bool semVolta = sh.debugDemoWouldStart(false);
        const bool comVolta = sh.debugDemoWouldStart(true);
        std::printf("[DemoTest] relogio velho: sem voltar ao menu o demo %s; "
                    "voltando ao menu %s%c",
                    semVolta ? "entra" : "NAO entra",
                    comVolta ? "AINDA entra" : "nao entra", 10);
        if (!semVolta) {
            std::printf("[DemoTest] FALHOU: o relogio velho nao chamou o demo "
                        "(o caso nao mede nada)%c", 10);
            ok = false;
        }
        if (comVolta) {
            std::printf("[DemoTest] FALHOU: o demo entrou mesmo voltando ao menu%c", 10);
            ok = false;
        }
        op2::Config volta("OUTPOST2.INI");
        volta.load();
        volta.setInt("Game", "Demo", gravado);
        volta.save();
    }

    // E agora o demo rodando de verdade: carrega, anda alguns tiques e e
    // interrompido por uma tecla, como os ganchos do original fazem.
    if (ok) {
        op2::g_art.load();
        op2::GameStartInfo gsi;
        gsi.flags |= 1u | 2u;
        gsi.demo = true;
        gsi.dll = "ademo1.dll";
        op2::GameState gs;
        if (!gs.loadMission("ademo1.dll", &gsi)) {
            std::printf("[DemoTest] ademo1.dll nao carregou%c", 10);
            ok = false;
        } else {
            gs.onActivate();
            for (int k = 0; k < 40; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
            const int antes = op2::gameEndCode();
            std::printf("[DemoTest] ademo1.dll rodou 40 tiques (t=%d, fim=%d)%c",
                        op2::TethysGame::tick(), antes, 10);
            SDL_Event ev{};
            ev.type = SDL_KEYDOWN;
            ev.key.keysym.sym = SDLK_SPACE;
            gs.handleEvent(renderer, ev);
            const int depois = op2::gameEndCode();
            std::printf("[DemoTest] tecla durante o demo -> codigo %d (esperado 10)%c",
                        depois, 10);
            if (antes != 0 || depois != 10) ok = false;
        }
    }

    // E cada demo ate o proprio fim: o gatilho de tempo que os ademo armam
    // como condicao de vitoria tem de encerrar com o codigo 11 (fim do demo),
    // nao com o 3 (missao cumprida).
    if (ok) {
        for (int n = 1; n <= 3 && ok; ++n) {
            char nome[32];
            std::snprintf(nome, sizeof(nome), "ademo%d.dll", n);
            op2::GameStartInfo gsi;
            gsi.flags |= 1u | 2u;
            gsi.demo = true;
            gsi.dll = nome;
            op2::GameState gs;
            if (!gs.loadMission(nome, &gsi)) { ok = false; break; }
            gs.onActivate();
            int camX = gs.camX(), camY = gs.camY(), saltos = 0, fimTick = -1, quadro = 0;
            // O script parca a camera com CenterViewOn no InitProc; o centro
            // da vista tem de estar onde ele pediu. A vista do mapa tem 416x416
            // (o resto da tela e a moldura e o painel), entao o centro esta a
            // 208 px do canto -- dividir 640x480 daria um tile errado.
            std::printf("[DemoTest] %s: camera inicial centrada no tile (%d,%d)%c", nome,
                        (camX + 208) >> 5, (camY + 208) >> 5, 10);
            for (int k = 0; k < 20000 && fimTick < 0; ++k) {
                gs.onIdle(renderer, 0.2501f / 4.0f);
                if (gs.camX() != camX || gs.camY() != camY) {
                    ++saltos; camX = gs.camX(); camY = gs.camY();
                }
                if (op2::gameEndCode()) fimTick = op2::TethysGame::tick();
                // Alguns quadros ao longo do demo, para conferir a vista e o
                // minimapa com os proprios olhos.
                if (!prefixo.empty() && n == 1 && (k == 150 || k == 500 || k == 850)) {
                    char out[512];
                    std::snprintf(out, sizeof(out), "%s_%d.bmp", prefixo.c_str(), ++quadro);
                    writeBmp32(out, renderer.getFramebuffer(),
                               renderer.getWidth(), renderer.getHeight());
                }
            }
            // Quantos inimigos estariam escondidos pela cegueira noturna? Os
            // ademo chamam SetDaylightEverywhere(0), entao ha noite; no demo a
            // regra nao vale, senao a cena mostra os tiros sem quem atira.
            int noEscuro = 0;
            for (size_t u = 1; u < op2::g_units.size(); ++u) {
                const op2::UnitRecord& r = op2::g_units[u];
                if (r.id == -1 || (r.flags & op2::UNIT_DEAD)) continue;
                if (!(r.flags & op2::UNIT_VEHICLE) || (r.owner & 0x0F) == 0) continue;
                if (op2::g_gameMap.lightDarkness((r.pixelX >> 5) + op2::g_gameMap.internalXOffset()
                                                 - op2::TethysGame::daylightPos()) >= 16)
                    ++noEscuro;
            }
            // Ao sair do demo o audio da partida tem de ser cortado, como o
            // TApp::ActivateShell do original faz (sub_489FB0 + sub_47E0C0).
            if (n == 1 && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 && op2::soundInit()) {
                op2::soundPlay(0x57);
                op2::soundPlay(0x56);
                const int tocando = op2::soundActiveChannels();
                op2::musicEndGame();
                op2::soundStopAll();
                const int depois = op2::soundActiveChannels();
                std::printf("[DemoTest] audio: %d canais tocando -> %d depois de voltar"
                            " ao menu (esperado 0)%c", tocando, depois, 10);
                if (tocando <= 0 || depois != 0) ok = false;
                op2::soundShutdown();
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
            }
            const int codigo = op2::gameEndCode();
            std::printf("[DemoTest] %s: fim no tique %d com codigo %d (esperado 11),"
                        " camera movida %d vezes, %d veiculos inimigos no escuro"
                        " (visiveis no demo)%c",
                        nome, fimTick, codigo, saltos, noEscuro, 10);
            if (codigo != 11) ok = false;
        }
    }
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

static int runUiTest() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 uitest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::ShellMenu menu;
    menu.init("OP2SHRES.DLL");
    std::string relato;
    const bool ok = menu.comboTest(relato);
    std::printf("[UiTest] combo: %s%c", relato.c_str(), 10);
    std::string relatoBarra;
    const bool okBarra = menu.sliderTest(relatoBarra);
    std::printf("[UiTest] barra: %s%c", relatoBarra.c_str(), 10);
    std::string relatoPrefs;
    const bool okPrefs = menu.prefsTest(relatoPrefs);
    std::printf("[UiTest] velocidades: %s%c", relatoPrefs.c_str(), 10);
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return (ok && okBarra && okPrefs) ? 0 : 1;
}

// --mpui <host|join> [nome]: percorre as TELAS de multijogador (259 -> 260 ->
// 2012/2010+195+2013 -> 196 -> START) chamando o mesmo despacho dos botoes, e
// confere o GameStartInfo que o lobby monta contra os bits do original.
static int runMpUi(const std::string& mode, const std::string& nome) {
    const bool host = (mode == "host");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 mpui", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::ShellMenu menu;
    menu.init("OP2SHRES.DLL");

    std::string relato;
    menu.m_mpEjectTest = (nome == "EJECT");
    const bool ok = menu.mpFlowTest(host, nome, relato);
    std::printf("[MpUi] %s: %s%c", host ? "host" : "join", relato.c_str(), 10);

    if (ok && !menu.m_mpEjectTest) {
        op2::GameStartInfo gsi;
        menu.launchStartInfo(gsi);
        std::printf("[MpUi] flags=%08x (desastres=%d dia/noite=%d moral=%d maxjog=%d veic=%d)%c",
                    gsi.flags, int(gsi.flags & 1), int((gsi.flags >> 1) & 1),
                    int((gsi.flags >> 2) & 1), int((gsi.flags >> 6) & 7),
                    int((gsi.flags >> 17) & 0xF), 10);
        for (int i = 0; i < 2; ++i)
            std::printf("[MpUi] jogador %d: eden=%d recursos=%d cor=%d%c", i,
                        int((gsi.playerWord[i] >> 2) & 1), gsi.difficulty(i),
                        int((gsi.playerWord[i] >> 5) & 7), 10);
    }
    std::fflush(stdout);
    op2::lobbyHostStop();
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --mplobby host <nome> <maxjog> <segundos> : abre o lobby e anuncia a partida
// --mplobby find                            : procura partidas na rede local
// --mplobby join                            : procura e entra na primeira
// Exercita o protocolo de descoberta e entrada do original (consulta 0x1000 de
// 0x2A bytes, resposta 0x1001 de 0x51, pedido de entrada de 0x32, aceitacao de
// 0x26 e confirmacao de 0x14).
static int runMpLobby(int argc, char** argv, int i) {
    const std::string mode = argv[i + 1];
    std::string err;

    if (mode == "host") {
        const std::string nome = i + 2 < argc ? argv[i + 2] : "PORT TEST";
        const int maxjog = i + 3 < argc ? atoi(argv[i + 3]) : 2;
        const int segs   = i + 4 < argc ? atoi(argv[i + 4]) : 10;
        if (!op2::lobbyHostStart(nome, maxjog, "", err)) {
            std::printf("[Lobby] host falhou: %s%c", err.c_str(), 10);
            return 2;
        }
        std::printf("[Lobby] anunciando %s para %d jogadores%c", nome.c_str(), maxjog, 10);
        std::fflush(stdout);
        const uint32_t fim = SDL_GetTicks() + uint32_t(segs) * 1000;
        int ultimo = op2::lobbyHostActiveCount();
        while (SDL_GetTicks() < fim) {
            op2::lobbyHostPump();
            const int agora = op2::lobbyHostActiveCount();
            if (agora != ultimo) {
                std::printf("[Lobby] lugares ocupados: %d de %d%c", agora, maxjog, 10);
                std::fflush(stdout);
                ultimo = agora;
            }
            SDL_Delay(5);
        }
        int ativos = 0;
        for (int n = 0; n < 8; ++n) {
            const op2::LobbyPlayer* pl = op2::lobbyHostPlayer(n);
            if (!pl || pl->state != 2) continue;
            ++ativos;
            std::printf("[Lobby] jogador %d id=%08x porta=%u%c",
                        n, pl->id, unsigned(pl->port), 10);
        }
        std::printf("[Lobby] host encerrou com %d jogador(es)%c", ativos, 10);
        op2::lobbyHostStop();
        return ativos >= 2 ? 0 : 1;
    }

    std::vector<op2::LobbySession> ses;
    if (!op2::lobbyFind("", 8, ses, err)) {
        std::printf("[Lobby] busca falhou: %s%c", err.c_str(), 10);
        return 2;
    }
    std::printf("[Lobby] %d sessao(oes) encontrada(s)%c", int(ses.size()), 10);
    for (const op2::LobbySession& x : ses) {
        unsigned char ip[4];
        std::memcpy(ip, &x.addr, 4);
        std::printf("[Lobby]   %s  %u.%u.%u.%u  max=%d respostas=%d ida-volta=%dms%c",
                    x.name.c_str(), ip[0], ip[1], ip[2], ip[3],
                    x.maxPlayers, x.replies, x.replies ? x.pingMs / x.replies : 0, 10);
    }
    if (mode == "find") return ses.empty() ? 1 : 0;

    if (ses.empty()) { std::printf("[Lobby] nada para entrar%c", 10); return 1; }
    int jogador = -1;
    uint16_t portaHost = 0;
    if (!op2::lobbyJoin(ses[0], "", jogador, portaHost, err)) {
        std::printf("[Lobby] entrada recusada: %s%c", err.c_str(), 10);
        return 2;
    }
    std::printf("[Lobby] entrou como jogador %d (host em %u, nos em %u)%c",
                jogador, unsigned(portaHost), unsigned(op2::lobbyLocalGamePort()), 10);
    return 0;
}

// O miolo de uma partida em rede, comum a --mpnet e --mplan: roda ate o tique
// pedido e, no tique 12, manda UM veiculo do jogador local andar 6 tiles. Se
// os comandos atravessarem, os dois lados veem os DOIS movimentos e o hash
// bate; se um comando ficar so na maquina de origem, os hashes divergem.
static void netMatchLoop(const char* teste, const char* lado, op2::GameState& gs,
                         op2::Renderer& renderer, int ticks, int localPlayer) {
    int giros = 0;
    bool mandou = false;
    while (static_cast<int>(op2::TethysGame::tick()) < ticks && giros < ticks * 200) {
        if (!mandou && op2::TethysGame::tick() >= 12) {
            mandou = true;
            for (size_t i = 1; i < op2::g_units.size(); ++i) {
                const op2::UnitRecord& r = op2::g_units[i];
                if (r.id == -1 || (r.flags & op2::UNIT_DEAD)) continue;
                if ((r.owner & 0x0F) != localPlayer || !(r.flags & op2::UNIT_VEHICLE)) continue;
                op2::CommandPacket pkt;
                pkt.type = op2::CMD_MOVE;
                pkt.unitIds.push_back(static_cast<int>(i));
                pkt.waypoints.push_back(op2::packLocation((r.pixelX >> 5) + 6, r.pixelY >> 5));
                op2::commandSubmit(localPlayer, pkt);
                std::printf("[%s] %s mandou a unidade %d andar%c", teste, lado,
                            static_cast<int>(i), 10);
                break;
            }
        }
        // dt MENOR que o periodo do tique (1/(GameSpeed*4), 0,05 s na
        // velocidade 5): com um dt maior, uma chamada pode avancar DOIS tiques
        // e um lado para em 201 enquanto o outro para em 200 -- os hashes
        // entao diferem sem que haja dessincronizacao nenhuma.
        gs.onIdle(renderer, 0.01f);
        ++giros;
    }
    std::printf("[%s] %s jogador=%d t=%d esperas=%d hash=%016llx%c",
                teste, lado, localPlayer,
                static_cast<int>(op2::TethysGame::tick()), op2::netWaitCount(),
                (unsigned long long)simHash(), 10);
    std::fflush(stdout);
}

// --mpnet <host|join> <DLL> <ticks> [porta] [peer]: partida em REDE. Os dois
// lados carregam a mesma missao, trocam os turnos (pacote 0x33) e o tique so
// anda quando o turno de todos chegou. No fim imprime o hash da simulacao: se
// os dois lados derem o mesmo hash, ficaram em passo.
static int runMpNet(const std::string& mode, const std::string& dll, int ticks,
                    int port, const std::string& peer) {
    const bool host = (mode == "host");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init(host ? "OP2 mpnet host" : "OP2 mpnet join", 1280, 960, false)) { SDL_Quit(); return 1; }

    op2::NetConfig cfg;
    cfg.host = host;
    cfg.port = port;
    cfg.peerHost = peer;
    cfg.localPlayer = host ? 0 : 1;
    std::string err;
    if (!op2::netStart(cfg, err)) {
        std::printf("[MpNet] rede falhou: %s%c", err.c_str(), 10);
        renderer.shutdown(); SDL_Quit(); return 2;
    }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { op2::netStop(); renderer.shutdown(); SDL_Quit(); return 3; }
    op2::TethysGame::setLocalPlayer(cfg.localPlayer);
    gs.onActivate();

    netMatchLoop("MpNet", host ? "host" : "join", gs, renderer, ticks, cfg.localPlayer);
    op2::netStop();
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --mplan <host|join> <DLL> <ticks>: a mesma partida de --mpnet, mas com os
// dois lados se achando pelo LOBBY — o host anuncia e espera, o cliente
// descobre por broadcast e pede entrada. Nao ha endereco na linha de comando.
static int runMpLan(const std::string& mode, const std::string& dll, int ticks, int jogadores) {
    const bool host = (mode == "host");
    if (jogadores < 2) jogadores = 2;
    if (jogadores > 6) jogadores = 6;
    std::string err;
    int jogador = 0;
    uint32_t peerAddr = 0;
    uint16_t peerPort = 0;

    std::vector<op2::NetPeer> pares;
    if (host) {
        if (!op2::lobbyHostStart("OP2 LAN", jogadores, "", err)) {
            std::printf("[MpLan] lobby falhou: %s%c", err.c_str(), 10);
            return 2;
        }
        std::printf("[MpLan] host no ar, esperando %d jogador(es)%c", jogadores - 1, 10);
        std::fflush(stdout);
        const uint32_t fim = SDL_GetTicks() + 30000;
        // ATENCAO: lobbyHostActiveCount conta o lugar 0, que e o proprio host.
        while (SDL_GetTicks() < fim && op2::lobbyHostActiveCount() < jogadores) {
            op2::lobbyHostPump();
            SDL_Delay(5);
        }
        if (op2::lobbyHostActiveCount() < jogadores) {
            std::printf("[MpLan] so %d na mesa (com o host)%c", op2::lobbyHostActiveCount(), 10);
            op2::lobbyHostStop();
            return 2;
        }
        // O host fala com CADA lugar ocupado.
        for (int n = 1; n < 8; ++n) {
            const op2::LobbyPlayer* lp = op2::lobbyHostPlayer(n);
            if (lp && lp->state == 2) pares.push_back(op2::NetPeer{lp->addr, lp->port});
        }
        if (!pares.empty()) { peerAddr = pares[0].addr; peerPort = pares[0].port; }
        jogador = 0;
    } else {
        std::vector<op2::LobbySession> ses;
        if (!op2::lobbyFind("", 8, ses, err) || ses.empty()) {
            std::printf("[MpLan] nenhuma partida encontrada%c", 10);
            return 2;
        }
        uint16_t portaHost = 0;
        if (!op2::lobbyJoin(ses[0], "", jogador, portaHost, err)) {
            std::printf("[MpLan] entrada recusada: %s%c", err.c_str(), 10);
            return 2;
        }
        if (!op2::lobbyPeer(peerAddr, peerPort)) {
            std::printf("[MpLan] o lobby nao disse onde esta o host%c", 10);
            return 2;
        }
        pares.push_back(op2::NetPeer{peerAddr, peerPort});   // o cliente so fala com o host
    }

    unsigned char ip[4];
    std::memcpy(ip, &peerAddr, 4);
    std::printf("[MpLan] %s e o jogador %d; o outro lado esta em %u.%u.%u.%u:%u%c",
                host ? "host" : "join", jogador, ip[0], ip[1], ip[2], ip[3],
                unsigned(peerPort), 10);
    std::fflush(stdout);

    const uintptr_t sock = op2::lobbyTakeGameSocket();
    if (host) op2::lobbyHostStop();
    if (!op2::netStartJoined(sock, pares.data(), int(pares.size()), host, jogador,
                             jogadores, err)) {
        std::printf("[MpLan] rede falhou: %s%c", err.c_str(), 10);
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init(host ? "OP2 mplan host" : "OP2 mplan join", 1280, 960, false)) {
        SDL_Quit(); return 1;
    }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { op2::netStop(); renderer.shutdown(); SDL_Quit(); return 3; }
    op2::TethysGame::setLocalPlayer(jogador);
    gs.onActivate();

    netMatchLoop("MpLan", host ? "host" : "join", gs, renderer, ticks, jogador);
    op2::netStop();
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --mptest <DLL> [ticks]: roda uma missao MULTIJOGADOR e reporta, por jogador,
// se e humano, quantas unidades/predios tem e se o Command Center esta
// operante — que e o que o CreateOnePlayerLeftTrigger (sub_492970) olha.
// Serve para ver se a partida se sustenta em vez de acabar no primeiro tique.
// --aitest <DLL de campanha> <DLL multijogador>: confere o "AILevel" do INI
// pelo caminho de verdade — grava a chave, carrega a missao e le a dificuldade
// com que os scripts vao ser atendidos. Restaura o INI no fim.
static int runAiTest(const std::string& dll, const std::string& mpDll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 aitest", 1280, 960, false)) { SDL_Quit(); return 1; }

    op2::Config cfg("OUTPOST2.INI");
    const bool tinha = cfg.load();
    const int antes = tinha ? cfg.getInt("Game", "AILevel", 0) : 0;
    const bool haviaChave = tinha && cfg.hasKey("Game", "AILevel");

    static const char* kNome[4] = {"AUTO", "Easy", "Normal", "Hard"};
    bool ok = true;
    for (int n = 0; n < 4; ++n) {
        cfg.setInt("Game", "AILevel", n);
        cfg.save();
        op2::GameState gs;
        if (!gs.loadMission(dll)) { ok = false; break; }
        gs.onActivate();
        const int vista = op2::Player[0].Difficulty();
        // AUTO tem de deixar o que a partida escolheu (1 = Normal, sem
        // GameStartInfo); os outros forcam n-1.
        const int esperado = (n == 0) ? 1 : n - 1;
        if (vista != esperado) ok = false;
        std::printf("[AiTest] %-6s -> jogador 0 ve %d (esperado %d) %s%c",
                    kNome[n], vista, esperado, vista == esperado ? "ok" : "FALHOU", 10);
    }

    // Em multijogador a dificuldade vem do lobby e nao pode ser sobrescrita,
    // ou os dois lados simulariam diferente.
    if (ok && !mpDll.empty()) {
        cfg.setInt("Game", "AILevel", 3);        // Hard
        cfg.save();
        op2::GameState gs;
        if (gs.loadMission(mpDll)) {
            gs.onActivate();
            const int vista = op2::Player[0].Difficulty();
            const bool intacto = (vista != 2);
            if (!intacto) ok = false;
            std::printf("[AiTest] multijogador com Hard -> jogador 0 ve %d %s%c",
                        vista, intacto ? "(intacto, ok)" : "(FOI SOBRESCRITO)", 10);
        }
    }

    // Devolve o INI como estava.
    if (haviaChave) cfg.setInt("Game", "AILevel", antes);
    else cfg.setInt("Game", "AILevel", 0);
    cfg.save();

    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

static int runMpTest(const std::string& dll, int ticks) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 mptest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    const int n = op2::TethysGame::noPlayers();
    auto report = [&](const char* when) {
        std::printf("[MpTest] %s t=%d jogadores=%d", when,
                    static_cast<int>(op2::TethysGame::tick()), n);
        for (int p = 0; p < n; ++p) {
            int veh = 0, bld = 0;
            for (size_t i = 1; i < op2::g_units.size(); ++i) {
                const op2::UnitRecord& r = op2::g_units[i];
                if (r.id == -1 || (r.flags & op2::UNIT_DEAD)) continue;
                if ((r.owner & 0x0F) != p) continue;
                if (r.flags & op2::UNIT_VEHICLE) ++veh;
                else if (r.flags & op2::UNIT_BUILDING) ++bld;
            }
            std::printf(" | p%d %s v=%d e=%d cc=%d", p,
                        op2::g_playerData[p].isHuman ? "humano" : "IA", veh, bld,
                        op2::g_colony[p].stat(op2::ST_CC_OP));
        }
        std::printf("%c", 10);
    };
    if (const op2::MissionScript* ms = op2::currentMission())
        std::printf("[MpTest] desc: tipo=%d jogadores=%d tech=%d unitMission=%d%c",
                    ms->desc->missionType, ms->desc->numPlayers,
                    ms->desc->maxTechLevel, ms->desc->boolUnitMission, 10);
    report("inicio");
    for (int k = 0; k < ticks && !gs.gameEnded(); ++k) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
    }
    report(gs.gameEnded() ? "FIM  " : "fim  ");
    if (gs.gameEnded()) std::printf("[MpTest] a partida terminou antes do tempo%c", 10);
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --playshot <map> <out.bmp> [pass] [colunaDaCamera]: carrega o jogo, roda alguns
// frames e salva o framebuffer (terreno + unidades + HUD) para inspeção visual.
// A coluna da câmera serve para alcançar o dia ou a noite num mapa largo.
// Ciclo dia/noite da cena capturada: a escuridao (0..31) das colunas visiveis e
// a paleta que cada uma usa (sub_4A8BD0: base + escuridao << 9).
static void dumpDark(const op2::GameState& gs) {
    std::printf("[PlayShot] luz: anda %d, toda parte %d, fase %d, coluna %d; escuridao",
                op2::TethysGame::daylightMoves(), op2::TethysGame::daylightEverywhere(),
                op2::TethysGame::daylightPhase(), op2::TethysGame::daylightPos());
    for (int tx = 0; tx < 14; ++tx)
        std::printf(" %d", op2::GameState::tileDarkness(gs.camX() / 32 + tx));
    std::printf("%c", 10);
}

// --nightshot <DLL> <prefixo> [percentuais...]: carrega a missao, poe a base do
// jogador em plena noite (gira a faixa de luz ate a coluna da camera ficar na
// escuridao maxima) e salva um quadro para cada ajuste de "NightDarkness", para
// comparar as curvas lado a lado. Sem percentuais, usa 100 (o original) e o que
// estiver no INI.
static int runNightShot(const std::string& dll, const std::string& prefix,
                        const std::vector<int>& niveis) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 nightshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    for (int i = 0; i < 5; ++i) gs.onIdle(renderer, 0.1f);

    // Centra a camera no primeiro predio do jogador 0, que e onde o contraste
    // entre o terreno escuro e os sprites, que nao escurecem, aparece.
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || (r.flags & op2::UNIT_DEAD)) continue;
        if ((r.owner & 0x0F) != 0 || !(r.flags & op2::UNIT_BUILDING)) continue;
        gs.setCamera(r.pixelX - 300, r.pixelY - 220);
        break;
    }

    // Gira a faixa de luz ate a coluna do centro da vista ficar no escuro.
    const int w = op2::g_gameMap.internalWidth();
    const int alvo = (gs.camX() + 320) / 32;
    int melhorFase = 0, melhorEscuro = -1;
    for (int fase = 0; fase < 0x10000; fase += 256) {
        op2::TethysGame::setDaylightPhase(fase, w);
        const int d = op2::GameState::tileDarkness(alvo);
        if (d > melhorEscuro) { melhorEscuro = d; melhorFase = fase; }
    }
    op2::TethysGame::setDaylightPhase(melhorFase, w);
    std::printf("[NightShot] coluna %d com escuridao %d (fase 0x%04X)%c",
                alvo, melhorEscuro, melhorFase, 10);

    bool ok = true;
    for (int n : niveis) {
        op2::setNightDarkness(n);
        gs.onIdle(renderer, 0.0f);
        char out[512];
        std::snprintf(out, sizeof(out), "%s_%03d.bmp", prefix.c_str(), n);
        if (!writeBmp32(out, renderer.getFramebuffer(),
                        renderer.getWidth(), renderer.getHeight()))
            ok = false;
        std::printf("[NightShot] %d%%: brilho no escuro %d/32 -> %s%c",
                    n, op2::nightShadeMul(melhorEscuro), out, 10);
    }
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

static int runPlayShot(const std::string& mapName, const std::string& outPath, bool passOverlay,
                       int camTileX) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << std::endl; return 1; }
    op2::g_ResManager.init(".");
    op2::g_art.load();   // sprites (como TApp::init)
    op2::Renderer renderer;
    if (!renderer.init("OP2 playshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    gs.setShowPassability(passOverlay);
    if (camTileX >= 0) gs.setCamera(camTileX * 32, gs.camY());
    for (int i = 0; i < 5; ++i) gs.onIdle(renderer, 0.1f);   // avança alguns ticks
    bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                         renderer.getWidth(), renderer.getHeight());
    dumpDark(gs);
    std::cout << "[PlayShot] " << (ok ? "salvo em " : "falha ") << outPath << std::endl;
    renderer.shutdown();
    SDL_Quit();
    return ok ? 0 : 1;
}

// --structtest <map> <prefix>: conduz o 1º prédio pelos estados (construção,
// dano 1/2, reparo, destruição + entulho) e salva quadros prefix_<fase>.bmp.
static int runStructTest(const std::string& mapName, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << std::endl; return 1; }
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 structtest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    int bid = -1;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && (op2::g_units[i].flags & op2::UNIT_BUILDING)) { bid = i; break; }
    if (bid < 0) { std::cerr << "sem predio" << std::endl; return 3; }
    auto snap = [&](const char* tag) {
        const op2::UnitRecord& r = op2::g_units[bid];
        char path[256];
        std::snprintf(path, sizeof(path), "%s_%s.bmp", prefix.c_str(), tag);
        writeBmp32(path, renderer.getFramebuffer(),
                   renderer.getWidth(), renderer.getHeight());
        std::printf("[Struct] %-10s tick=%d id=%d estado=%u sub=%u dano=%d/%d anim=%d frame=%d acao=0x%X\n",
                    tag, op2::TethysGame::tick(), r.id, (r.state34 >> 16) & 0xFFFF,
                    r.state34 & 0xFFFF, r.damage, op2::structureMaxHP(r),
                    r.id == -1 ? -1 : op2::structureCurAnim(r),
                    r.id == -1 ? -1 : op2::structureFrame(r), r.action);
    };
    auto run = [&](int ticks) { for (int t = 0; t < ticks; ++t) gs.onIdle(renderer, 0.05f); };  // 1 tick (GameSpeed 5)
    run(2); snap("intacto");
    op2::structureStartBuild(bid, 0);
    const int dur = op2::g_units[bid].buildTime;
    std::printf("[Struct] construcao: duracao=%d ticks\n", dur);
    run(dur / 4); snap("const25");
    run(dur / 2); snap("const75");
    run(dur / 4 + 12); snap("pronto");
    const int hp = op2::structureMaxHP(op2::g_units[bid]);
    op2::g_units[bid].damage = static_cast<int16_t>(hp * 35 / 100); run(2); snap("dano35");
    op2::g_units[bid].damage = static_cast<int16_t>(hp * 65 / 100); run(2); snap("dano65");
    op2::g_units[bid].damage = 0; run(2); snap("reparado");
    // Animacao ambiente: predio operando (sub_483810) -> a cada 100 ticks
    // sub_4835C0 pode por o estado 5+ (frame = contador ate o fim).
    op2::structureTurnOn(op2::g_units[bid]);
    {
        int shots = 0;
        for (int t = 0; t < 1200 && shots < 3; ++t) {
            run(1);
            const op2::UnitRecord& r = op2::g_units[bid];
            int st = (r.state34 >> 16) & 0xFFFF, sub = r.state34 & 0xFFFF;
            if (st >= 5 && sub == 3) {
                char tag[16]; std::snprintf(tag, sizeof(tag), "amb%d", shots);
                snap(tag); ++shots;
            }
        }
        if (!shots) std::printf("[Struct] nenhuma animacao ambiente em 1200 ticks\n");
    }
    op2::Unit(bid).DoDeath();
    run(4); snap("morte04");
    run(8); snap("morte12");
    run(14); snap("morte26");
    run(40); snap("entulho");
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --paneshot <map> <out.bmp>: Tokamak ao lado do CC da cena, selecionado, e
// a tela com o quadro de informação (painel direito) p/ comparar com o original.
static int runPaneShot(const std::string& mapName, const std::string& outPath, int extraLight) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 paneshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    int cc = -1;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapCommandCenter) { cc = i; break; }
    int sel = cc;
    if (cc > 0) {
        op2::Unit tok;
        op2::LOCATION l{(op2::g_units[cc].pixelX >> 5) + 3, op2::g_units[cc].pixelY >> 5};
        if (op2::TethysGame::CreateUnit(tok, op2::mapTokamak, l, op2::g_units[cc].owner & 0x0F)) sel = tok.id();
    }
    for (int k = 0; k < extraLight; ++k) op2::TethysGame::advanceDaylight(op2::g_gameMap.internalWidth());
    gs.setSelection({sel});
    gs.setMouse(200, 150);
    for (int i = 0; i < 40; ++i) gs.onIdle(renderer, 0.05f);
    bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                         renderer.getWidth(), renderer.getHeight());
    std::printf("[PaneShot] %s\n", ok ? outPath.c_str() : "falha");
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --cmdshot <map> <out.bmp> <map_id> [qtd] [carga]: cria `qtd` unidades do tipo
// ao lado do CC, seleciona-as e grava o painel com a botoeira (mouse no botão 1).
static int runCmdShot(const std::string& mapName, const std::string& outPath, int type, int count, int cargo) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 cmdshot", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    int cc = -1;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapCommandCenter) { cc = i; break; }
    if (cc < 0) return 3;
    std::vector<int> sel;
    for (int k = 0; k < count; ++k) {
        op2::Unit u;
        op2::LOCATION l{(op2::g_units[cc].pixelX >> 5) + 3 + k, (op2::g_units[cc].pixelY >> 5) + 3};
        if (op2::TethysGame::CreateUnit(u, static_cast<op2::map_id>(type), l, op2::g_units[cc].owner & 0x0F, cargo))
            sel.push_back(u.id());
    }
    gs.setSelection(sel);
    gs.setMouse(gs.kColX + 28 + 22, op2::GameState::kPaneTop + 0x3C + 57 + 22);
    for (int i = 0; i < 10; ++i) gs.onIdle(renderer, 0.05f);
    bool ok = writeBmp32(outPath, renderer.getFramebuffer(),
                         renderer.getWidth(), renderer.getHeight());
    std::printf("[CmdShot] tipo 0x%02X x%d -> %s\n", type, (int)sel.size(), ok ? outPath.c_str() : "falha");
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --cmdtest <map>: Lynx anda por "Mover" (mira) e recebe "Parar" no caminho.
static int runCmdTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 cmdtest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    int cc = -1;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapCommandCenter) { cc = i; break; }
    if (cc < 0) return 3;
    op2::Unit u;
    const int sx = (op2::g_units[cc].pixelX >> 5) + 3, sy = (op2::g_units[cc].pixelY >> 5) + 4;
    op2::TethysGame::CreateUnit(u, op2::mapLynx, op2::LOCATION{sx, sy}, op2::g_units[cc].owner & 0x0F, 0x3D);
    const std::vector<int> sel{u.id()};
    std::printf("[CmdTest] Mover: %d\n", (int)op2::uiCommandClick(op2::UC_MOVE, sel));
    op2::uiCommandTarget(op2::UC_MOVE, sel, sx + 12, sy);
    for (int t = 0; t < 120; ++t) {
        if (t == 60) std::printf("[CmdTest] Parar: %d\n", (int)op2::uiCommandClick(op2::UC_STOP, sel));
        gs.onIdle(renderer, 0.2501f / 4.0f);
        const op2::UnitRecord& r = op2::g_units[u.id()];
        if (t % 10 == 9 || (t > 58 && t < 72))
            std::printf("[CmdTest] t=%3d tile=(%d,%d) px=%d acao=%d f20=%d sub=%d passo=%d\n", t, r.pixelX >> 5, r.pixelY >> 5,
                        r.pixelX, r.action, r.field20, r.field22, r.stepTicks);
    }
    // Espalhar: 3 Lynx lado a lado; Ficar parado: o 1º anda 6 tiles e segura.
    std::vector<int> grp;
    for (int k = 0; k < 3; ++k) {
        op2::Unit v;
        if (op2::TethysGame::CreateUnit(v, op2::mapLynx, op2::LOCATION{sx + k, sy + 3}, op2::g_units[cc].owner & 0x0F, 0x3D))
            grp.push_back(v.id());
    }
    std::printf("[CmdTest] Espalhar: %d\n", (int)op2::uiCommandClick(op2::UC_SCATTER, grp));
    for (int id : grp) {
        const op2::UnitRecord& r = op2::g_units[id];
        std::printf("[CmdTest]   %d de (%d,%d) -> (%d,%d)\n", id, r.pixelX >> 5, r.pixelY >> 5,
                    op2::unpackLocX(r.order.waypoints.back()), op2::unpackLocY(r.order.waypoints.back()));
    }
    for (int t = 0; t < 150; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    const std::vector<int> one{grp[0]};
    std::printf("[CmdTest] Ficar parado: %d\n", (int)op2::uiCommandClick(op2::UC_STAND_GROUND, one));
    op2::uiCommandTarget(op2::UC_STAND_GROUND, one, (op2::g_units[grp[0]].pixelX >> 5) - 6, op2::g_units[grp[0]].pixelY >> 5);
    for (int t = 0; t < 300; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    const op2::UnitRecord& g0 = op2::g_units[grp[0]];
    std::printf("[CmdTest] apos: tile=(%d,%d) acao=0x%X f20=%d sub=%d\n", g0.pixelX >> 5, g0.pixelY >> 5, g0.action, g0.field20, g0.field22);
    // Atacar: Lynx nosso contra um Scout do jogador 1 a 12 tiles.
    {
        op2::Unit me2, foe;
        op2::resetUnits();                                   // cena limpa: só os dois
        const int ax = sx, ay = sy - 4;
        op2::TethysGame::CreateUnit(me2, op2::mapLynx, op2::LOCATION{ax, ay}, op2::g_units[cc].owner & 0x0F, 0x3D);
        op2::TethysGame::CreateUnit(foe, op2::mapScout, op2::LOCATION{ax + 12, ay}, 1, 0);
        op2::g_units[me2.id()].flags &= ~op2::UNIT_AUTO_TARGET;   // so a ordem (sem alvo automatico)
        const std::vector<int> s1{me2.id()};
        std::printf("[CmdTest] Atacar: %d\n", (int)op2::uiCommandClick(op2::UC_ATTACK, s1));
        op2::uiCommandTarget(op2::UC_ATTACK, s1, ax + 12, ay);
        for (int t = 0; t < 600; ++t) {
            gs.onIdle(renderer, 0.2501f / 4.0f);
            const op2::UnitRecord& a = op2::g_units[me2.id()];
            const op2::UnitRecord& f = op2::g_units[foe.id()];
            if (t % 40 == 0 || (f.flags & op2::UNIT_DEAD))
                std::printf("[CmdTest] t=%3d lynx=(%d,%d) acao=0x%X sub=%d ordem=%08X arma=%08X | scout dano=%d%s\n", t,
                            a.pixelX >> 5, a.pixelY >> 5, a.action, a.field22, a.orderTarget(), a.target(), f.damage,
                            (f.flags & op2::UNIT_DEAD) ? " MORTO" : "");
            if (f.flags & op2::UNIT_DEAD) break;
        }
    }
    // Guardar: Lynx nosso guarda a ConVec; um Lynx do jogador 1 chega perto dela.
    {
        op2::resetUnits();
        const int gx = sx, gy = sy - 4;
        op2::Unit cv, gd, en;
        const int me = op2::TethysGame::localPlayer();
        op2::TethysGame::CreateUnit(cv, op2::mapConVec, op2::LOCATION{gx, gy}, me, 0);
        op2::TethysGame::CreateUnit(gd, op2::mapLynx, op2::LOCATION{gx - 8, gy}, me, 0x3D);
        const std::vector<int> s1{gd.id()};
        std::printf("[CmdTest] Guardar: %d\n", (int)op2::uiCommandClick(op2::UC_GUARD, s1));
        op2::uiCommandTarget(op2::UC_GUARD, s1, gx, gy);
        for (int t = 0; t < 900; ++t) {
            if (t == 200) op2::TethysGame::CreateUnit(en, op2::mapLynx, op2::LOCATION{gx + 3, gy}, 1, 0x3D);
            gs.onIdle(renderer, 0.2501f / 4.0f);
            const op2::UnitRecord& a = op2::g_units[gd.id()];
            const op2::UnitRecord& c = op2::g_units[cv.id()];
            if (t % 25 == 0 && t < 520) {
                const op2::UnitRecord* e = en.id() > 0 ? &op2::g_units[en.id()] : nullptr;
                std::printf("[CmdTest] t=%3d guarda id=%d (%d,%d) vivo=%d acao=0x%X sub=%d arma=%08X dano=%d | inimigo (%d,%d) arma=%08X dano=%d\n",
                            t, a.id, a.pixelX >> 5, a.pixelY >> 5, !(a.flags & op2::UNIT_DEAD), a.action, a.field22,
                            a.target(), a.damage, e ? e->pixelX >> 5 : -1, e ? e->pixelY >> 5 : -1,
                            e ? e->target() : 0, e ? e->damage : -1);
            }
        }
    }
    // Patrulhar: Lynx vai ao ponto inicial e fica indo e voltando até o final.
    {
        op2::resetUnits();
        const int px = sx, py = sy - 4;
        op2::Unit ly;
        op2::TethysGame::CreateUnit(ly, op2::mapLynx, op2::LOCATION{px, py}, op2::TethysGame::localPlayer(), 0x3D);
        const std::vector<int> s1{ly.id()};
        // Ponto final: 1ª célula enterável a partir de px+8 (indo p/ oeste).
        op2::pfSetContext(ly.id());
        int ex = px + 8;
        while (ex > px && !op2::pfCanEnter(ex, py)) --ex;
        const int r0 = (int)op2::uiCommandClick(op2::UC_PATROL, s1);
        std::printf("[CmdTest] Patrulhar: %d dica=\"%s\"\n", r0, op2::uiCommandHint().c_str());
        const int r1 = (int)op2::uiCommandTarget(op2::UC_PATROL, s1, px - 6, py);
        std::printf("[CmdTest] 1o ponto: %d dica=\"%s\"\n", r1, op2::uiCommandHint().c_str());
        const int r2 = (int)op2::uiCommandTarget(op2::UC_PATROL, s1, ex, py);
        const op2::UnitRecord& a = op2::g_units[ly.id()];
        std::printf("[CmdTest] 2o ponto: %d acao=0x%X pontos=%d inicio=%d | rota (%d,%d) <-> (%d,%d)\n", r2, a.action,
                    a.order.count, a.patrolStart, px - 6, py, ex, py);
        for (int t = 0; t < 1200; ++t) {
            gs.onIdle(renderer, 0.2501f / 4.0f);
            if (t % 40 == 0)
                std::printf("[CmdTest] t=%4d lynx=(%d,%d) acao=0x%X sub=%d wp=%d/%d\n", t, a.pixelX >> 5, a.pixelY >> 5,
                            a.action, a.field22, a.order.curWaypoint, a.order.countMinus1);
        }
    }
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --producetest <map> <prefix>: CC + Tokamak + Vehicle Factory + Structure
// Factory encostados; lista de produção (sem e com o atalho "toda tecnologia"),
// painel BUILD (screenshot), pacote 9 e entrega (veículo e kit na baia).
// Ciclo do minério: Robo-Miner instala a mina no beacon; caminhão faz a rota
// mina → smelter → mina; imprime cargas, estoque e estado do caminhão.
// Obras: Earthworker faz um tubo e um muro; Robo-Dozer terraplana 3x3.
static int runEarthTest(const std::string& mapName, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 earthtest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    {   // Minimapa: clique no canto inferior direito -> vista centrada lá.
        SDL_Event ev{};
        ev.type = SDL_MOUSEBUTTONDOWN; ev.button.button = SDL_BUTTON_LEFT; ev.button.clicks = 1;
        ev.button.x = (gs.kColX + op2::GameState::kColW - 4) * 2;
        ev.button.y = (op2::GameState::kMiniMapY + op2::GameState::kMiniMapH - 4) * 2;
        gs.handleEvent(renderer, ev);
        gs.onIdle(renderer, 0.0f);
        const int c1x = gs.camX(), c1y = gs.camY();
        ev.button.x = (gs.kColX + 60) * 2; ev.button.y = (op2::GameState::kMiniMapY + 64) * 2;
        gs.handleEvent(renderer, ev);
        gs.onIdle(renderer, 0.0f);
        std::printf("[Mini] canto -> cam (%d,%d) | meio -> cam (%d,%d) mapa %dx%d\n", c1x, c1y, gs.camX(), gs.camY(),
                    op2::g_gameMap.width(), op2::g_gameMap.height());
        gs.setCamera(0, 0);
    }
    {   // LocationEnumerator (sub_437890): CC 3x2 em (20,20) e caminhão em (30,20);
        // sonda uma grade de pontos e imprime o mapa de acertos.
        op2::Unit cc, tr;
        op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{20, 20}, 0);
        op2::TethysGame::CreateUnit(tr, op2::mapCargoTruck, op2::LOCATION{30, 20}, 0);
        for (int cx : {20 * 32, 30 * 32}) {
            std::printf("[Loc] ponto em torno de px (%d,%d), passo 8:\n", cx + 16, 20 * 32 + 16);
            for (int dy = -48; dy <= 48; dy += 8) {
                std::string row;
                for (int dx = -48; dx <= 64; dx += 8) {
                    op2::MapPointIter it;
                    op2::mapPointIterInit(it, cx + 16 + dx + op2::g_gameMap.internalXOffset() * 32, 20 * 32 + 16 + dy, 0xFFFFFFFFu, 0xFFFFFFFFu);
                    const int id = op2::mapPointIterNext(it);
                    row += id == cc.id() ? 'C' : id == tr.id() ? 'T' : id ? '?' : '.';
                }
                std::printf("[Loc] %s\n", row.c_str());
            }
        }
        op2::resetUnits();
    }
    {   // Luz de status (Overlay, fim de sub_408DB0): CC, Observatory e Agridome.
        op2::Unit a, b, c, d;
        op2::TethysGame::CreateUnit(a, op2::mapCommandCenter, op2::LOCATION{20, 20}, 0);
        op2::TethysGame::CreateUnit(b, static_cast<op2::map_id>(0x33), op2::LOCATION{24, 20}, 0);
        op2::TethysGame::CreateUnit(c, static_cast<op2::map_id>(0x27), op2::LOCATION{20, 23}, 0);
        op2::TethysGame::CreateUnit(d, static_cast<op2::map_id>(0x3A), op2::LOCATION{24, 23}, 0);
        op2::g_playerData[0].workers = 30; op2::g_playerData[0].scientists = 10;
        op2::colonyMarkDirty(0);
        gs.setCamera(17 * 32, 17 * 32);
        for (int t = 0; t < 20; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
        gs.setCamera(17 * 32, 17 * 32);
        gs.onIdle(renderer, 0.0f);
        writeBmp32(prefix + "_overlay.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                   renderer.getHeight());
        op2::resetUnits();
    }
    {   // Reparo (0x1C), desmonte (0x1E) e tomada (0x1D).
        op2::Player[1].GoPlymouth();
        op2::Unit cc, tk, lab, rv, cv, sp, enemy, trk;
        op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{12, 12}, 0);
        op2::TethysGame::CreateUnit(tk, static_cast<op2::map_id>(0x3A), op2::LOCATION{15, 12}, 0);
        lab = op2::Unit(op2::placeBuilding(0x21, 12, 15, 0));   // Basic Lab encostado
        op2::placeBuilding(0x36, 16, 15, 0);                     // Smelter (capacidade de minério)
        op2::TethysGame::CreateUnit(rv, op2::mapRepairVehicle, op2::LOCATION{20, 16}, 0);
        op2::TethysGame::CreateUnit(trk, op2::mapCargoTruck, op2::LOCATION{22, 16}, 0);
        op2::TethysGame::CreateUnit(cv, op2::mapConVec, op2::LOCATION{20, 18}, 0);
        op2::TethysGame::CreateUnit(sp, op2::mapSpider, op2::LOCATION{26, 14}, 0);
        op2::TethysGame::CreateUnit(enemy, op2::mapLynx, op2::LOCATION{29, 14}, 1, static_cast<op2::map_id>(0x41));
        op2::g_playerData[0].workers = 40; op2::g_playerData[0].scientists = 10; op2::g_playerData[0].ore = 3000;
        op2::colonyMarkDirty(0);
        for (int t = 0; t < 20; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
        op2::g_units[lab.id()].damage = 300;
        op2::g_units[trk.id()].damage = 150;
        op2::g_playerData[0].ore = 3000;
        const int ore0 = op2::g_playerData[0].ore;
        op2::processRepairPacket(0, 0x1C, {rv.id()}, {}, static_cast<uint32_t>(lab.id()) | 0xFFFF0000u);
        op2::unitApplyEMP(enemy.id(), 0x60);
        op2::processRepairPacket(0, 0x1D, {sp.id()}, {}, static_cast<uint32_t>(enemy.id()) | 0xFFFF0000u);
        int lastAct = -1, lastSub = -1;
        for (int t = 0; t < 900; ++t) {
            gs.onIdle(renderer, 0.2501f / 4.0f);
            const op2::UnitRecord& r = op2::g_units[rv.id()];
            if (r.action != lastAct || r.field22 != lastSub) {
                std::printf("[Rep] t=%d repair acao=0x%X sub=%d f3C=%d | lab dano=%d | minerio %d\n", t, r.action, r.field22,
                            r.field3C, op2::g_units[lab.id()].damage, op2::g_playerData[0].ore);
                lastAct = r.action; lastSub = r.field22;
            }
            if (t == 400) op2::processRepairPacket(0, 0x1C, {rv.id()}, {}, static_cast<uint32_t>(trk.id()) | 0xFFFF0000u);
        }
        std::printf("[Rep] fim: lab dano=%d caminhao dano=%d minerio %d -> %d | lynx dono=%d emp=%d | msg=%s\n",
                    op2::g_units[lab.id()].damage, op2::g_units[trk.id()].damage, ore0, op2::g_playerData[0].ore,
                    op2::g_units[enemy.id()].owner & 15, (op2::g_units[enemy.id()].flags & op2::UNIT_EMP) != 0,
                    op2::statusBarText().c_str());
        // Desmonte do Lab pela ConVec (sem GORF: entulho).
        op2::processRepairPacket(0, 0x1E, {cv.id()}, {}, static_cast<uint32_t>(lab.id()) | 0xFFFF0000u);
        int t = 0;
        for (; t < 3000 && op2::g_units[lab.id()].id != -1 && op2::g_units[lab.id()].type == 0x21; ++t)
            gs.onIdle(renderer, 0.2501f / 4.0f);
        const op2::UnitRecord& c = op2::g_units[cv.id()];
        std::printf("[Rep] desmonte: t=%d lab vivo=%d | convec acao=0x%X sub=%d danificavel=%d | celula (12,15) tipo=0x%X\n", t,
                    op2::g_units[lab.id()].id != -1 && op2::g_units[lab.id()].type == 0x21, c.action, c.field22,
                    (c.flags & op2::UNIT_DAMAGEABLE) != 0, op2::g_gameMap.getCellType(op2::LOCATION{12, 15}));
        op2::resetUnits();
    }
    {   // RLV: Spaceport do jogador 1 lança (pacote 0x19), o 0 captura em órbita
        // (CaptureRLV) e o RLV volta ao Spaceport do jogador 0.
        op2::Player[1].GoPlymouth();
        op2::Unit cc0, tk0, sp0, cc1, tk1, sp1;
        op2::TethysGame::CreateUnit(cc0, op2::mapCommandCenter, op2::LOCATION{12, 12}, 0);
        op2::TethysGame::CreateUnit(tk0, static_cast<op2::map_id>(0x3A), op2::LOCATION{15, 12}, 0);
        sp0 = op2::Unit(op2::placeBuilding(0x37, 12, 15, 0));
        op2::TethysGame::CreateUnit(cc1, op2::mapCommandCenter, op2::LOCATION{30, 12}, 1);
        op2::TethysGame::CreateUnit(tk1, static_cast<op2::map_id>(0x3A), op2::LOCATION{33, 12}, 1);
        sp1 = op2::Unit(op2::placeBuilding(0x37, 30, 15, 1));
        for (int p = 0; p < 2; ++p) { op2::g_playerData[p].workers = 60; op2::g_playerData[p].scientists = 20; op2::colonyMarkDirty(p); }
        for (int t = 0; t < 40; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
        {   // Painel LAUNCH PAD do jogador 0: SULV + módulo (Command Module 0x5C) na baia 0.
            op2::UnitRecord& s0 = op2::g_units[sp0.id()];
            s0.padCargo = 0x69;
            s0.bayType[0] = 0x5C;
            gs.setSelection({sp0.id()});
            gs.pressCommand(op2::UC_LAUNCH_PAD);
            for (int q = 0; q < 3; ++q) gs.onIdle(renderer, 0.05f);
            writeBmp32(prefix + "_launch1.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                       renderer.getHeight());
            std::printf("[RLV] painel: launch disponivel=%d troca baia0=%d\n", op2::spaceportLaunchAvailable(sp0.id()),
                        op2::spaceportPadSwapAvailable(sp0.id(), 0));
            op2::spaceportPadSwap(0, sp0.id(), 0);
            for (int q = 0; q < 3; ++q) gs.onIdle(renderer, 0.05f);
            writeBmp32(prefix + "_launch2.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                       renderer.getHeight());
            std::printf("[RLV] apos troca: carga=0x%X baia0=0x%X launch disponivel=%d\n", op2::g_units[sp0.id()].padPayload,
                        op2::g_units[sp0.id()].bayType[0], op2::spaceportLaunchAvailable(sp0.id()));
            gs.pressCommand(-14);                              // kCmdLaunch
            int sulv = 0;
            for (int i = 1; i < (int)op2::g_units.size(); ++i)
                if (op2::g_units[i].id != -1 && op2::g_units[i].type == 0x69) sulv = i;
            for (int q = 0; q < 300 && sulv && op2::g_units[sulv].id != -1; ++q) gs.onIdle(renderer, 0.2501f / 4.0f);
            std::printf("[RLV] SULV lancado=%d removido=%d | modulos j0=0x%X pad=0x%X\n", sulv != 0,
                        sulv && op2::g_units[sulv].id == -1, op2::g_playerData[0].spaceBits, op2::g_units[sp0.id()].padCargo);
            gs.setSelection({});
        }
        op2::g_units[sp1.id()].padCargo = 0x6A;
        std::printf("[RLV] spaceports operando: j0=%d j1=%d\n", op2::structureOperational(op2::g_units[sp0.id()]),
                    op2::structureOperational(op2::g_units[sp1.id()]));
        const bool launched = op2::spaceportLaunch(sp1.id(), 0, 0);
        int rlv = 0;
        for (int i = 1; i < (int)op2::g_units.size(); ++i)
            if (op2::g_units[i].id != -1 && op2::g_units[i].type == 0x6A) rlv = i;
        std::printf("[RLV] lancado=%d rlv=%d pad j1=0x%X\n", launched, rlv, op2::g_units[sp1.id()].padCargo);
        int lastSub = -1, lastOwner = -1;
        bool captured = false;
        for (int t = 0; t < 2400 && rlv; ++t) {
            gs.onIdle(renderer, 0.2501f / 4.0f);
            const op2::UnitRecord& r = op2::g_units[rlv];
            if (r.id == -1) { std::printf("[RLV] t=%d rlv removido\n", t); break; }
            if (r.field22 != lastSub || (r.owner & 15) != lastOwner) {
                std::printf("[RLV] t=%d sub=0x%X acao=0x%X dono=%d px(%d,%d) contagem j0=%d j1=%d\n", t, r.field22, r.action,
                            r.owner & 15, r.pixelX, r.pixelY, op2::g_playerData[0].rlvCount, op2::g_playerData[1].rlvCount);
                lastSub = r.field22; lastOwner = r.owner & 15;
            }
            if (!captured && op2::g_playerData[1].rlvCount) { op2::playerCaptureRLV(0, 1); captured = true; }
        }
        std::printf("[RLV] fim: pad j0=0x%X reservado=%d pad j1=0x%X msg=%s\n", op2::g_units[sp0.id()].padCargo,
                    op2::g_units[sp0.id()].padReserved, op2::g_units[sp1.id()].padCargo, op2::statusBarText().c_str());
        op2::resetUnits();
    }
    {   // Míssil EMP (ação 0x1A) do jogador 1 (Plymouth) sobre 3 veículos do 0.
        op2::Player[1].GoPlymouth();
        op2::Unit v[3];
        for (int k = 0; k < 3; ++k)
            op2::TethysGame::CreateUnit(v[k], op2::mapCargoTruck, op2::LOCATION{20 + k * 2, 20}, 0);
        const int mid = op2::setEMPMissile(10, 30, 1, 21, 20);
        int lastSub = -1, lastAct = -1, blast = 0;
        bool shot = false;
        for (int t = 0; t < 400; ++t) {
            gs.onIdle(renderer, 0.2501f / 4.0f);
            const op2::UnitRecord& m = op2::g_units[mid];
            if (m.id != -1 && (m.field22 != lastSub || m.action != lastAct)) {
                std::printf("[EMP] t=%d missil sub=0x%X acao=0x%X f3C=%d px(%d,%d) 0x4000=%d\n", t, m.field22, m.action,
                            m.field3C, m.pixelX, m.pixelY, (m.flags & 0x4000u) != 0);
                lastSub = m.field22; lastAct = m.action;
            }
            for (int i = 1; !blast && i < (int)op2::g_units.size(); ++i)
                if (op2::g_units[i].id != -1 && op2::isEmpBlast(op2::g_units[i].type)) {
                    blast = i;
                    std::printf("[EMP] t=%d EMP_BIG id=%d em px(%d,%d)\n", t, i, op2::g_units[i].pixelX, op2::g_units[i].pixelY);
                }
            if (!shot && blast && op2::g_units[blast].id != -1 && op2::g_units[blast].field3C <= 15) {
                shot = true;
                gs.setCamera(15 * 32, 15 * 32);
                gs.onIdle(renderer, 0.0f);
                writeBmp32(prefix + "_emp.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                           renderer.getHeight());
            }
        }
        for (int k = 0; k < 3; ++k)
            std::printf("[EMP] caminhao %d: emp=%d ticks=%d\n", k, (op2::g_units[v[k].id()].flags & op2::UNIT_EMP) != 0,
                        op2::g_units[v[k].id()].empTicks);
        op2::resetUnits();
    }
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int bx = -1, by = -1;
    for (int y = 3; y < H - 14 && bx < 0; ++y)
        for (int x = 3; x < W - 26 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 11 && ok; ++dy)
                for (int dx = -2; dx <= 22 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { std::printf("[Earth] sem area livre\n"); return 3; }
    op2::Unit cc, ew, dz;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);
    op2::TethysGame::CreateUnit(ew, op2::mapEarthworker, op2::LOCATION{bx + 4, by + 4}, 0);
    op2::TethysGame::CreateUnit(dz, op2::mapRoboDozer, op2::LOCATION{bx + 12, by + 6}, 0);
    op2::Unit tk;
    op2::TethysGame::CreateUnit(tk, op2::mapTokamak, op2::LOCATION{bx + 4, by + 8}, 0);
    op2::placeBuilding(0x19, bx, by + 2, 0);                  // Common Storage encostado (capacidade)
    op2::placeBuilding(0x1A, bx - 2, by + 2, 0);              // Rare Storage
    op2::PlayerData& pd = op2::g_playerData[0];
    pd.workers = 20; pd.scientists = 5;
    op2::TethysGame::setCheatFlag(1, 1);
    for (int t = 0; t < 10; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    pd.ore = 3000; pd.rareOre = 500;
    auto ct = [](int x, int y) { return (int)(op2::g_gameMap.rawCell(x, y) & 0x1F); };
    auto run = [&](const op2::UnitRecord& u, int maxT, const char* tag) {
        int t = 0;
        for (; t < maxT && u.action != 0; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
        std::printf("[Earth] %s: t=%d acao=%d pos=(%d,%d) minerio=%d raro=%d | %s\n", tag, t, u.action,
                    u.pixelX >> 5, u.pixelY >> 5, pd.ore, pd.rareOre, op2::statusBarText().c_str());
    };
    const op2::UnitRecord& e = op2::g_units[ew.id()];
    // Tubo: do lado direito do CC, 6 tiles p/ a direita (arrasto restringido).
    const std::vector<int> selE{ew.id()};
    const int tx0 = bx + 2, ty0 = by;
    int ex = (tx0 + 5) * 32 + 16, ey = ty0 * 32 + 20;
    op2::wallDragConstrain(tx0 * 32 + 16, ty0 * 32 + 16, ex, ey);
    std::printf("[Earth] arrasto tubo: fim (%d,%d) tiles\n", ex >> 5, ey >> 5);
    op2::uiCommandClick(op2::UC_TUBE, selE);
    std::printf("[Earth] tubo aceito=%d\n", (int)op2::uiCommandArea(op2::UC_TUBE, selE, tx0, ty0, (ex >> 5) + 1, ty0 + 1));
    for (int t = 0; t < 1200; ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if (t % 40 == 0 && t >= 440)
            std::printf("[Earth]   t=%d pos=(%d,%d) f20=%d f22=%d f23=%d ordem=%d cel=(%d,%d) cod=0x%X dirs=%d espera=%d\n", t,
                        e.pixelX >> 5, e.pixelY >> 5, e.field20, e.field22, e.field23, (int)e.hasOrder,
                        e.wallCurX, e.wallCurY, e.wallCode, (int)e.routeDirs.size(), e.field3C);
    }
    run(e, 6000, "tubo");
    std::printf("[Earth] celulas:");
    for (int x = tx0; x <= tx0 + 6; ++x) std::printf(" %02X", ct(x, ty0));
    std::printf("\n");
    // Muro: 4 tiles na vertical, longe dos prédios.
    const int wx = bx + 8, wy0 = by + 1;
    op2::uiCommandArea(op2::UC_WALL, selE, wx, wy0, wx + 1, wy0 + 4);
    run(e, 8000, "muro");
    std::printf("[Earth] celulas:");
    for (int y = wy0; y < wy0 + 4; ++y) std::printf(" %02X", ct(wx, y));
    std::printf("\n");
    // Dozer 3x3.
    const op2::UnitRecord& d = op2::g_units[dz.id()];
    const std::vector<int> selD{dz.id()};
    const int dx0 = bx + 14, dy0 = by + 2;
    std::printf("[Earth] antes:");
    for (int y = dy0; y < dy0 + 3; ++y) for (int x = dx0; x < dx0 + 3; ++x) std::printf(" %02X", ct(x, y));
    std::printf("\n");
    std::printf("[Earth] dozer aceito=%d\n", (int)op2::uiCommandArea(op2::UC_BULLDOZE, selD, dx0, dy0, dx0 + 3, dy0 + 3));
    run(d, 8000, "dozer");
    std::printf("[Earth] depois:");
    for (int y = dy0; y < dy0 + 3; ++y) for (int x = dx0; x < dx0 + 3; ++x) std::printf(" %02X", ct(x, y));
    std::printf("\n");
    // Remover: 1 célula do muro e a última do tubo.
    std::printf("[Earth] remover muro aceito=%d\n", (int)op2::uiCommandTarget(op2::UC_REMOVE_WALL, selE, wx, wy0 + 3));
    run(e, 3000, "remover muro");
    std::printf("[Earth] remover tubo aceito=%d\n", (int)op2::uiCommandTarget(op2::UC_REMOVE_WALL, selE, tx0 + 5, ty0));
    run(e, 3000, "remover tubo");
    std::printf("[Earth] muro: %02X %02X | tubo: %02X %02X | dano da maquina=%d\n", ct(wx, wy0 + 2), ct(wx, wy0 + 3),
                ct(tx0 + 4, ty0), ct(tx0 + 5, ty0), e.damage);
    gs.setCamera(bx * 32 - 64, by * 32 - 96);
    gs.onIdle(renderer, 0.0f);
    writeBmp32(prefix + "_earth.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --idletest <DLL>: o aviso de desligar um armazem/fundicao. A regra sai de
// sub_455CB0 e de sub_471F30: desligar tira a capacidade do predio e o que
// passar do novo teto e truncado, ou seja perdido -- entao o jogo pergunta
// antes. O teste poe o estoque nos dois lados da fronteira e confere.
static int runIdleTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 idletest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    // A capacidade so existe depois que a colonia conta os predios OPERANDO
    // (sub_471F30), entao deixa a partida andar um pouco.
    for (int k = 0; k < 120; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);

    bool ok = true;
    int testados = 0, pulados = 0;
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || (r.flags & op2::UNIT_DEAD)) continue;
        const int t = r.type;
        const bool comum = (t == 0x36 || t == 0x19), raro = (t == 0x38 || t == 0x1A);
        if (!comum && !raro) continue;
        const int p = r.owner & 0x0F;
        const op2::StructSheetDesc* d = op2::structSheet(t);
        if (!d) continue;
        // O indice do bloco e o nibble ALTO de [+0x1D] (sar al,4 no original).
        const int minha = d->player[(r.owner >> 4) & 0x0F].storageCapacity;
        const int teto = comum ? op2::g_colony[p].oreCapacity : op2::g_colony[p].rareCapacity;
        // Predio que ainda nao conta para a colonia (parado, sem energia) nao
        // tem o que perder: a capacidade dele nem esta no teto.
        if (teto <= 0 || minha <= 0) { ++pulados; continue; }
        int& estoque = comum ? op2::g_playerData[p].ore : op2::g_playerData[p].rareOre;
        const int guardado = estoque;
        const int sobra = teto - minha < 0 ? 0 : teto - minha;

        // Logo abaixo do que caberia sem este predio: nao perde nada.
        int perdeNada = -1;
        estoque = sobra;
        const int semPerda = op2::structureIdleMetalLoss((int)i, &perdeNada);
        // Um a mais: ja nao cabe, entao perde exatamente esse 1.
        int perdeUm = -1;
        estoque = sobra + 1;
        const int comPerda = op2::structureIdleMetalLoss((int)i, &perdeUm);
        // Estoque cheio: sai tudo o que este predio sustentava.
        int perdeCheio = -1;
        estoque = teto;
        const int cheio = op2::structureIdleMetalLoss((int)i, &perdeCheio);
        // E o caso que o ORIGINAL deixa passar em silencio: um SEGUNDO armazem
        // igual atras deste. O teto dobra, a sobra passa a ser `minha`, e
        // desligar com o estoque cheio perde so o excedente -- perda PARCIAL,
        // em que o dialogo poe o numero no lugar do "All".
        int& tetoCol = comum ? op2::g_colony[p].oreCapacity : op2::g_colony[p].rareCapacity;
        const int tetoGuardado = tetoCol;
        tetoCol = teto + minha;
        estoque = tetoCol;
        int perdeParcial = -1;
        const int parcial = op2::structureIdleMetalLoss((int)i, &perdeParcial);
        tetoCol = tetoGuardado;
        estoque = guardado;

        const int esperado = comum ? 1 : 2;
        // Com o predio sendo o UNICO armazem (sobra 0), perder 1 de 1 e perder
        // tudo: o dialogo diz "All", como o original. Com outro armazem atras,
        // a perda e parcial e o numero entra no lugar do "All".
        const char* texto = (sobra == 0) ? "All" : "numero";
        std::printf("[IdleTest] %s tipo 0x%02X do jogador %d: teto %d, da %d, sobra %d |"
                    " %d -> %d/%d (0/0) | %d -> %d/%d (%d/1) | %d -> %d/%d (%d/%d)"
                    " | 2 armazens: %d -> %d/%d (%d/%d) | texto: %s%c",
                    comum ? "comum" : "raro", t, p, teto, minha, sobra,
                    sobra, semPerda, perdeNada,
                    sobra + 1, comPerda, perdeUm, esperado,
                    teto, cheio, perdeCheio, esperado, teto - sobra,
                    teto + minha, parcial, perdeParcial, esperado, minha, texto, 10);
        if (semPerda != 0 || perdeNada != 0) ok = false;
        if (comPerda != esperado || perdeUm != 1) ok = false;
        if (cheio != esperado || perdeCheio != teto - sobra) ok = false;
        // Perda parcial: sai o excedente (o teto dobrado menos a nova sobra,
        // que e o proprio `teto` de antes), ou seja `minha`, e NAO o estoque todo.
        if (parcial != esperado || perdeParcial != minha) ok = false;
        ++testados;
        if (testados >= 4) break;
    }
    if (!testados)
        std::printf("[IdleTest] nenhum armazem/fundicao contando para a colonia"
                    " (%d pulados) -- tente E05.DLL ou P05.DLL%c", pulados, 10);
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return (ok && testados) ? 0 : 1;
}

// --takeovertest <map>: a Spider do COMPUTADOR tomando posse de um veiculo
// inimigo paralisado. No original e sub_428BB0 (classe 4 do no do grupo):
// sub_427CC0 escolhe o alvo -- veiculo com EMP (flag 0x80000) a ate 256 px --
// e sub_42A100 manda o pacote 0x1D com a Spider e o alvo.
static int runTakeOverTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 takeovertest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();

    // Chao limpo para os dois.
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int bx = -1, by = -1;
    for (int y = H / 2; y < H - 4 && bx < 0; ++y)
        for (int x = W / 2; x < W - 8 && bx < 0; ++x) {
            bool ok = true;
            for (int dx = 0; dx < 6 && ok; ++dx)
                if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { renderer.shutdown(); SDL_Quit(); return 3; }

    op2::Unit spider, vitima;
    if (!op2::TethysGame::CreateUnit(spider, op2::mapSpider, op2::LOCATION{bx, by}, 1) ||
        !op2::TethysGame::CreateUnit(vitima, op2::mapCargoTruck, op2::LOCATION{bx + 3, by}, 0)) {
        renderer.shutdown(); SDL_Quit(); return 4;
    }
    const int sid = spider.id(), vid = vitima.id();
    // O alvo tem de estar PARALISADO: sub_427CC0 so olha veiculo com 0x80000.
    op2::g_units[vid].flags |= op2::UNIT_EMP;
    op2::g_units[vid].empTicks = 2000;

    // Num mapa cru nao ha GameStartInfo: sem jogadores declarados, enemyMask
    // sai vazia e o grupo nao enxerga ninguem.
    op2::TethysGame::reset(0x12345678u, 2, 0);
    // Tem de ser FightGroup: so ele roda o fightThink (vt[13], sub_428560),
    // que e quem percorre os nos por classe e chega ao caso 4 da Spider.
    const int stub = op2::aiCreateGroup(op2::GK_FIGHT, 1);
    if (stub == 0xFF) { renderer.shutdown(); SDL_Quit(); return 5; }
    // Sem area guardada o grupo em modo 0 nao coleta alvo nenhum (modeCollect
    // nivel 1 varre justamente os retangulos guardados), e a Spider ficaria
    // parada -- que e o comportamento certo. Damos a ela o que vigiar.
    // ATENCAO as coordenadas: mapRectIterNext SOMA internalXOffset ao x da
    // unidade antes de comparar com o retangulo, enquanto bx veio de varrer o
    // mapa (tiles internos). O retangulo tem de ser dado no mesmo espaco, ou
    // seja com o deslocamento somado -- senao ele cai 32 tiles fora.
    const int offT = op2::g_gameMap.internalXOffset();
    op2::aiFightSetRect(stub, bx + offT - 2, by - 2, bx + offT + 6, by + 2);
    op2::aiTakeUnit(stub, sid);

    const int donoAntes = op2::g_units[vid].owner & 0x0F;
    int virouEm = -1;
    for (int t = 0; t < 1200 && virouEm < 0; ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if ((op2::g_units[vid].owner & 0x0F) != donoAntes) virouEm = (int)op2::TethysGame::tick();
    }
    const int donoDepois = op2::g_units[vid].owner & 0x0F;
    std::printf("[TakeOverTest] Spider do jogador 1 em (%d,%d), vitima paralisada do"
                " jogador %d em (%d,%d)%c", bx, by, donoAntes, bx + 3, by, 10);
    std::printf("[TakeOverTest] dono da vitima: %d -> %d%s (tique %d)%c",
                donoAntes, donoDepois, donoDepois == 1 ? " OK" : " FALHOU", virouEm, 10);
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return donoDepois == 1 ? 0 : 1;
}

static int runOreTest(const std::string& mapName, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 oretest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int bx = -1, by = -1;
    for (int y = 3; y < H - 14 && bx < 0; ++y)
        for (int x = 3; x < W - 26 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 11 && ok; ++dy)
                for (int dx = -2; dx <= 22 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { std::printf("[Ore] sem area livre\n\n"); return 3; }
    op2::Unit cc, tok, sm;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{bx + 3, by}, 0);
    sm = op2::Unit(op2::placeBuilding(0x36, bx, by + 2, 0));   // encostado no CC (rede por adjacência)
    op2::PlayerData& pd = op2::g_playerData[0];
    pd.workers = 40; pd.scientists = 10;
    op2::TethysGame::setCheatFlag(1, 1);                       // toda tecnologia (Build mine)
    op2::colonyMarkDirty(0);
    const int beaconX = bx + 16, beaconY = by + 6;
    op2::createBeacon(0x51, beaconX, beaconY, 0, 0, 0);         // comum, rendimento alto, variante 1
    op2::Unit miner, truck;
    op2::TethysGame::CreateUnit(miner, op2::mapRoboMiner, op2::LOCATION{beaconX - 4, beaconY + 3}, 0);
    for (int t = 0; t < 40; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::printf("[Ore] smelter operando=%d capacidade=%d flags=0x%X acao=%d | cc op=%d | tok op=%d | trab=%d\n",
                op2::structureOperational(op2::g_units[sm.id()]), op2::g_colony[0].oreCapacity,
                op2::g_units[sm.id()].flags, op2::g_units[sm.id()].action,
                op2::structureOperational(op2::g_units[cc.id()]),
                op2::structureOperational(op2::g_units[tok.id()]), pd.workers);
    {
        int w1, h1, w2, h2;
        op2::buildingFootprint(op2::mapCommandCenter, w1, h1);
        op2::buildingFootprint(static_cast<op2::map_id>(0x36), w2, h2);
        std::printf("[Ore] cc (%d,%d) %dx%d | smelter (%d,%d) %dx%d | base (%d,%d)\n",
                    op2::g_units[cc.id()].pixelX >> 5, op2::g_units[cc.id()].pixelY >> 5, w1, h1,
                    op2::g_units[sm.id()].pixelX >> 5, op2::g_units[sm.id()].pixelY >> 5, w2, h2, bx, by);
    }
    // Robo-Miner: Build mine no beacon.
    const std::vector<int> selM{miner.id()};
    op2::uiCommandClick(op2::UC_BUILD_MINE, selM);
    op2::uiCommandTarget(op2::UC_BUILD_MINE, selM, beaconX, beaconY);
    int mine = 0, t = 0;
    for (; t < 6000; ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        for (int i = 1; i < (int)op2::g_units.size(); ++i) {
            const op2::UnitRecord& r = op2::g_units[i];
            if (r.id != -1 && (r.type == 0x15 || r.type == 0x16) && r.action == 0) { mine = i; break; }
        }
        if (mine) break;
    }
    const op2::UnitRecord& mm = op2::g_units[miner.id()];
    std::printf("[Ore] t=%d mina=%d (%s) | robo-miner id=%d vivo=%d\n\n", t, mine,
                mine ? op2::unitTitle(op2::g_units[mine]).c_str() : "-", mm.id,
                mm.id != -1 && !(mm.flags & op2::UNIT_DEAD));
    if (!mine) return 4;
    for (t = 0; t < 40; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::printf("[Ore] mina operando=%d rendimento=%d variante=%d cargas=%d\n\n",
                op2::structureOperational(op2::g_units[mine]), op2::g_units[mine].mineYield,
                op2::g_units[mine].mineVariant, op2::g_units[mine].mineLink);
    // Caminhão + rota (3 cliques nas docas).
    int mdx, mdy, sdx, sdy;
    op2::dockCell(mine, mdx, mdy);
    op2::dockCell(sm.id(), sdx, sdy);
    op2::TethysGame::CreateUnit(truck, op2::mapCargoTruck, op2::LOCATION{bx + 8, by + 9}, 0);
    const std::vector<int> selT{truck.id()};
    op2::uiCommandClick(op2::UC_ORE_ROUTE, selT);
    std::printf("[Ore] clique fora da doca: %d\n\n",
                (int)op2::uiCommandTarget(op2::UC_ORE_ROUTE, selT, mdx + 1, mdy - 1));
    op2::uiCommandTarget(op2::UC_ORE_ROUTE, selT, mdx, mdy);
    op2::uiCommandTarget(op2::UC_ORE_ROUTE, selT, sdx, sdy);
    op2::uiCommandTarget(op2::UC_ORE_ROUTE, selT, mdx, mdy);
    const op2::UnitRecord& tr = op2::g_units[truck.id()];
    std::printf("[Ore] rota: acao=0x%X pontos=%d mina(%d,%d) smelter(%d,%d)\n\n", tr.action,
                (int)tr.order.waypoints.size(), mdx, mdy, sdx, sdy);
    int lastCargo = -1, lastSub = -1;
    const int ore0 = pd.ore;
    for (t = 0; t < 6000; ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if (tr.cargo != lastCargo || (tr.field22 == 0x0D) != (lastSub == 0x0D)) {
            std::printf("[Ore] t=%d caminhao (%d,%d) px(%d,%d) rot=%d acao=0x%X sub=%d carga=%d tipo=%d | estoque=%d cargas=%d\n\n", t,
                        tr.pixelX >> 5, tr.pixelY >> 5, tr.pixelX, tr.pixelY, tr.rotation, tr.action, tr.field22, (tr.cargo >> 4) & 0xFFF, tr.cargo & 15,
                        pd.ore, op2::g_units[mine].mineLink);
            lastCargo = tr.cargo; lastSub = tr.field22;
        }
        if (t % 500 == 0)
            std::printf("[Ore]   t=%d smelterOp=%d (%d,%d) f20=%d f22=%d f23=%d cur=%d end=%d dirs=%d\n", t, (int)op2::structureOperational(op2::g_units[sm.id()]),
                        tr.pixelX >> 5, tr.pixelY >> 5, tr.field20, tr.field22, tr.field23,
                        tr.order.curWaypoint, tr.order.countMinus1, (int)tr.routeDirs.size());
        if (t == 290 || t == 600 || t == 850) {               // na mina / carregado / no smelter
            gs.setCamera(tr.pixelX - 200, tr.pixelY - 200);
            gs.onIdle(renderer, 0.0f);
            writeBmp32(prefix + "_route" + std::to_string(t) + ".bmp", renderer.getFramebuffer(),
                       renderer.getWidth(), renderer.getHeight());
        }
    }
    // STORAGE de recursos: 2º caminhão parado na doca do smelter → LOAD / UNLOAD.
    bool parcialOk = true;
    {
        op2::UnitRecord& t1 = op2::g_units[truck.id()];
        t1.action = 0; t1.field20 = 0; t1.field22 = 0; t1.hasOrder = false;   // tira o 1º da rota
        for (int k = 0; k < 400 && t1.field22 != 0; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
        op2::g_gameMap.clearUnitOccupancy(t1.pixelX >> 5, t1.pixelY >> 5);
        t1.pixelX = (sdx + 6) << 5;                                     // afasta o 1º
        op2::Unit t2;
        op2::TethysGame::CreateUnit(t2, op2::mapCargoTruck, op2::LOCATION{sdx + 3, sdy + 2}, 0);
        op2::UnitRecord& tt = op2::g_units[t2.id()];
        op2::g_gameMap.clearUnitOccupancy(tt.pixelX >> 5, tt.pixelY >> 5);
        tt.pixelX = sdx << 5; tt.pixelY = sdy << 5;
        op2::g_gameMap.setUnitOccupancy(sdx, sdy, t2.id());
        std::printf("[Ore] doca do smelter: veiculo=%d (2o caminhao=%d)\n", op2::dockedVehicle(sm.id()), t2.id());
        gs.setSelection({sm.id()});
        gs.pressCommand(op2::UC_STORAGE);
        for (int k = 0; k < 4; ++k) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_storage.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                   renderer.getHeight());
        const int s0 = pd.ore;
        std::printf("[Ore] LOAD disponivel=%d UNLOAD=%d estoque=%d\n", (int)op2::storageLoadAvailable(sm.id()),
                    (int)op2::storageUnloadAvailable(sm.id()), s0);
        gs.pressCommand(-12);                               // kCmdLoad
        int k = 0;
        for (; k < 600 && (tt.field22 == 0x0D || k == 0); ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
        std::printf("[Ore] apos LOAD (t=%d): carga=%d tipo=%d estoque %d -> %d\n", k, (tt.cargo >> 4) & 0xFFF,
                    tt.cargo & 15, s0, pd.ore);
        gs.pressCommand(-13);                               // kCmdUnload
        for (k = 0; k < 600 && (tt.field22 == 0x0D || k == 0); ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
        std::printf("[Ore] apos UNLOAD (t=%d): carga=%d estoque=%d\n", k, (tt.cargo >> 4) & 0xFFF, pd.ore);
        for (int q = 0; q < 4; ++q) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_storage2.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                   renderer.getHeight());
        // Despejar: carrega de novo, DUMP CARGO → confirmação → YES.
        gs.pressCommand(-12);
        for (k = 0; k < 600 && (tt.field22 == 0x0D || k == 0); ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
        const int s1 = pd.ore;
        gs.setSelection({t2.id()});
        gs.pressCommand(op2::UC_DUMP_CARGO);
        for (int q = 0; q < 4; ++q) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_dump.bmp", renderer.getFramebuffer(), renderer.getWidth(),
                   renderer.getHeight());
        gs.pressCommand(-9);                                   // kCmdYes
        for (k = 0; k < 600 && (tt.action == 0x0E || k == 0); ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
        std::printf("[Ore] despejo (t=%d): carga=%d acao=%d estoque=%d (antes %d)\n", k, (tt.cargo >> 4) & 0xFFF,
                    tt.action, pd.ore, s1);
        // --- carga parcial (como no OPUPatch) --------------------------------
        // O original so deixa carregar com estoque para ENCHER o caminhao, entao
        // os ultimos 900 ficavam presos no deposito. Agora leva o que tem.
        {
            const int cap = op2::vehicleSheet(op2::mapCargoTruck, 0)->cargo;
            pd.ore = cap / 2;                       // metade de um caminhao
            const int antes = pd.ore;
            const bool acendeu = op2::storageLoadAvailable(sm.id());
            op2::applyLoadCargo(0, sm.id());
            for (int k = 0; k < 600 && (tt.field22 == 0x0D || k == 0); ++k)
                gs.onIdle(renderer, 0.2501f / 4.0f);
            const int carga = (tt.cargo >> 4) & 0xFFF;
            parcialOk = acendeu && carga == antes && pd.ore == 0;
            std::printf("[Ore] parcial: L&OAD aceso=%d carga=%d (esperado %d) estoque=%d (esperado 0) %s\n",
                        acendeu ? 1 : 0, carga, antes, pd.ore, parcialOk ? "ok" : "FALHOU");
        }

    }
    std::printf("[Ore] fim: estoque %d -> %d, cargas da mina=%d, caminhao acao=0x%X\n\n", ore0, pd.ore,
                op2::g_units[mine].mineLink, tr.action);

    renderer.shutdown();
    SDL_Quit();
    return parcialOk ? 0 : 1;
}

static int runProduceTest(const std::string& mapName, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 producetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int bx = -1, by = -1;
    for (int y = 3; y < H - 10 && bx < 0; ++y)
        for (int x = 3; x < W - 16 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 7 && ok; ++dy)
                for (int dx = -2; dx <= 12 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { std::printf("[Produce] sem area livre\n"); return 3; }
    op2::Unit cc, tok, vf, sf;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{bx + 3, by}, 0);
    op2::TethysGame::CreateUnit(vf, op2::mapVehicleFactory, op2::LOCATION{bx + 1, by + 2}, 0);
    op2::TethysGame::CreateUnit(sf, op2::mapStructureFactory, op2::LOCATION{bx + 5, by + 2}, 0);
    op2::PlayerData& pd = op2::g_playerData[0];
    pd.workers = 40; pd.scientists = 10;
    op2::colonyMarkDirty(0);
    for (int t = 0; t < 60; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    pd.ore = 5000; pd.rareOre = 1000;            // (a colônia limita pela capacidade a cada recálculo)
    std::printf("[Produce] VF operando=%d SF operando=%d\n", op2::structureOperational(op2::g_units[vf.id()]),
                op2::structureOperational(op2::g_units[sf.id()]));
    std::printf("[Produce] sem pesquisa: VF %d itens, SF %d itens\n", (int)op2::factoryProduceList(vf.id()).size(),
                (int)op2::factoryProduceList(sf.id()).size());
    op2::TethysGame::setCheatFlag(1, 1);                     // dword_56DE5C: toda tecnologia
    for (int f : {vf.id(), sf.id()}) {
        const std::vector<uint32_t> items = op2::factoryProduceList(f);
        std::printf("[Produce] %s (%d itens):\n", op2::unitTitle(op2::g_units[f]).c_str(), (int)items.size());
        for (uint32_t it : items)
            std::printf("    %-34s pagavel=%d  %s\n", op2::produceItemName(it).c_str(),
                        (int)op2::produceAffordable(f, it), op2::produceItemCost(it, 0).c_str());
    }
    // Painel BUILD da Vehicle Factory, com o 2o item selecionado e o mouse no 3o.
    gs.setSelection({vf.id()});
    gs.pressCommand(op2::UC_BUILD);
    const std::vector<uint32_t> vitems = op2::factoryProduceList(vf.id());
    const int listY0 = op2::GameState::kPaneTop + 0x21 + 2;   // topo do quadro + 33 + 2
    gs.setMouse(gs.kColX + 40, listY0 + 2 * 15 + 5);
    for (int i = 0; i < 4; ++i) gs.onIdle(renderer, 0.05f);
    writeBmp32(prefix + "_list.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    // Produz o Lynx/Laser (1o combo de chassi) pelo pacote 9.
    uint32_t lynx = 0;
    for (uint32_t it : vitems) if ((it & 0xFFFF) == op2::mapLynx) { lynx = it; break; }
    const int ore0 = pd.ore, rare0 = pd.rareOre;
    op2::sendProduce(vf.id(), lynx);
    const op2::UnitRecord& v = op2::g_units[vf.id()];
    std::printf("[Produce] pacote 9 %s: acao=%d minerio %d->%d raro %d->%d\n", op2::produceItemName(lynx).c_str(),
                v.action, ore0, pd.ore, rare0, pd.rareOre);
    int t = 0;
    for (; t < 3000 && v.action == 9; ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if (t == 1) std::printf("[Produce] t=1 sub=%d duracao=%d\n", v.field22, v.produceTotal);
    }
    int made = 0;
    for (int i = 1; i < (int)op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id != -1 && r.type == op2::mapLynx && !(r.flags & op2::UNIT_DEAD)) {
            made = i;
            std::printf("[Produce] t=%d Lynx id=%d em (%d,%d) arma=0x%X rot=%d (fabrica em %d,%d)\n", t, i,
                        r.pixelX >> 5, r.pixelY >> 5, r.cargo, r.rotation, v.pixelX >> 5, v.pixelY >> 5);
        }
    }
    std::printf("[Produce] mensagem: %s\n", op2::statusBarText().c_str());
    // Kit na Structure Factory: Tokamak.
    pd.ore = 5000; pd.rareOre = 1000;
    op2::sendProduce(sf.id(), 0x3A);
    const op2::UnitRecord& s = op2::g_units[sf.id()];
    for (t = 0; t < 6000 && s.action == 9; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::printf("[Produce] kit pronto em t=%d: baias %d %d %d %d %d %d | %s\n", t, s.bayType[0], s.bayType[1],
                s.bayType[2], s.bayType[3], s.bayType[4], s.bayType[5], op2::statusBarText().c_str());
    // Doca: ConVec parada na célula da doca da Structure Factory pega o kit da baia 0.
    {
        int dx = 0, dy = 0;
        const bool hasDock = op2::dockCell(sf.id(), dx, dy);
        op2::Unit cv;
        op2::TethysGame::CreateUnit(cv, op2::mapConVec, op2::LOCATION{dx, dy}, 0);
        const op2::UnitRecord& c = op2::g_units[cv.id()];
        std::printf("[Produce] doca=%d (%d,%d) convec em (%d,%d) na doca=%d disponivel=%d\n", hasDock, dx, dy,
                    c.pixelX >> 5, c.pixelY >> 5, op2::dockedVehicle(sf.id()), (int)op2::bayTransferAvailable(sf.id(), 0));
        gs.setSelection({sf.id()});
        gs.pressCommand(op2::UC_STORAGE);
        for (int i = 0; i < 3; ++i) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_storage.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        op2::sendBayTransfer(sf.id(), 0);
        std::printf("[Produce] troca: convec sub=0x%X tempo=%d\n", c.field22, c.field3C);
        int tt = 0;
        for (; tt < 400 && c.field22 == 0x0D; ++tt) gs.onIdle(renderer, 0.2501f / 4.0f);
        std::printf("[Produce] apos %d: convec carga=0x%X sub=0x%X | baia0=0x%X\n", tt, c.cargo, c.field22, s.bayType[0]);
        for (int i = 0; i < 3; ++i) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_storage2.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        // ConVec constrói o Tokamak (kit) num local livre perto dela.
        int w = 0, h = 0;
        op2::kitFootprint(cv.id(), w, h);
        int px = -1, py = -1;
        for (int r = 4; r < 20 && px < 0; ++r)
            for (int oy = -r; oy <= r && px < 0; ++oy)
                for (int ox = -r; ox <= r && px < 0; ++ox) {
                    const int cxp = (c.pixelX >> 5) + ox, cyp = (c.pixelY >> 5) + oy;
                    int l, t, rr, bb;
                    op2::kitPlacementRect(w, h, cxp, cyp, l, t, rr, bb);
                    bool ok = t >= 0 && bb < op2::g_gameMap.height();
                    for (int yy = t; ok && yy <= bb; ++yy)
                        for (int xx = l; ok && xx <= rr; ++xx)
                            ok = op2::g_gameMap.isPassable(op2::LOCATION{xx, yy}) && !op2::g_gameMap.cellOccupant(xx, yy);
                    if (ok) { px = cxp; py = cyp; }
                }
        std::printf("[Produce] obra: kit %dx%d em (%d,%d)\n", w, h, px, py);
        gs.setSelection({cv.id()});
        gs.pressCommand(op2::UC_BUILD_KIT);
        gs.setCamera(px * 32 - 200, py * 32 - 200 < 0 ? 0 : py * 32 - 200);
        gs.setMouse(op2::GameState::kViewX + 200 + 16, op2::GameState::kViewY + (py * 32 - 200 < 0 ? py * 32 : 200) + 16);
        for (int i = 0; i < 2; ++i) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_place.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        op2::sendBuildKit({cv.id()}, px, py);
        std::printf("[Produce] convec acao=%d sub=%d destino retangulo=(%d,%d)-(%d,%d)\n", c.action, c.field22,
                    c.buildL, c.buildT, c.buildR, c.buildB);
        int bld = 0, tb = 0;
        for (; tb < 6000; ++tb) {
            gs.onIdle(renderer, 0.2501f / 4.0f);
            if (!bld)
                for (int i = 1; i < (int)op2::g_units.size(); ++i)
                    if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapTokamak && i != tok.id()) {
                        bld = i;
                        std::printf("[Produce] t=%d Tokamak criado id=%d em (%d,%d) acao=0x%X duracao=%d | convec sub=%d espera=%d\n",
                                    tb, i, op2::g_units[i].pixelX >> 5, op2::g_units[i].pixelY >> 5, op2::g_units[i].action,
                                    op2::g_units[i].buildTime, c.field22, c.field3C);
                    }
            if (bld && op2::g_units[bld].action == 0 && c.action == 0) break;
        }
        std::printf("[Produce] t=%d fim: tokamak acao=0x%X | convec acao=%d sub=%d carga=0x%X | %s\n", tb,
                    bld ? op2::g_units[bld].action : -1, c.action, c.field22, c.cargo, op2::statusBarText().c_str());
    }
    renderer.shutdown(); SDL_Quit();
    return made ? 0 : 4;
}

// --researchtest <dll> <prefix>: laboratório do jogador 0 da missão — lista de
// tópicos, painel ASSIGN (modos 0 e 1, screenshots), pacote 0x16 e conclusão.
static int runResearchTest(const std::string& dll, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 researchtest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    for (int t = 0; t < 40; ++t) gs.onIdle(renderer, 0.2501f / 4.0f);
    int lab = 0;
    for (int i = 1; i < (int)op2::g_units.size() && !lab; ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id != -1 && (r.owner & 0x0F) == 0 && op2::labResearchKind(r.type)) lab = i;
    }
    if (!lab) { std::printf("[Research] sem laboratorio\n"); return 3; }
    const op2::UnitRecord& L = op2::g_units[lab];
    std::printf("[Research] lab id=%d tipo 0x%X operando=%d cientistas livres=%d\n", lab, L.type,
                (int)op2::structureOperational(L), op2::g_colony[0].scientistsLeft);
    const std::vector<int> topics = op2::labTopics(lab);
    for (int t : topics)
        std::printf("    %-40s custo=%d max=%d\n", op2::techAt(t)->name.c_str(), op2::techAt(t)->plymouthCost,
                    op2::techAt(t)->maxScientists);
    if (topics.empty()) return 4;
    gs.setSelection({lab});
    gs.pressCommand(op2::UC_ASSIGN_SCIENTISTS);
    for (int i = 0; i < 2; ++i) gs.onIdle(renderer, 0.05f);
    writeBmp32(prefix + "_topics.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    gs.researchPaneForTest().open(lab, 0);
    // escolhe o 1o tópico (como o clique na lista) e passa ao modo 1
    gs.researchPaneForTest().selectForTest(topics[0]);
    gs.pressCommand(-5);   // kCmdAssign
    for (int i = 0; i < 2; ++i) gs.onIdle(renderer, 0.05f);
    writeBmp32(prefix + "_assign.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    gs.researchPaneForTest().more();
    gs.pressCommand(-7);   // kCmdStart
    std::printf("[Research] START %s: lab acao=0x%X cientistas=%d\n", op2::techAt(topics[0])->name.c_str(), L.action,
                (int)L.extraScientists);
    // STOP com confirmação: abre, screenshot, YES → lab parado e sem cientistas; depois recomeça.
    if (L.action != 0x16) op2::sendStartResearch(lab, topics[0], 1);
    for (int i = 0; i < 20; ++i) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::printf("[Research] antes do STOP: lab acao=0x%X sub=0x%X cientistas=%d\n", L.action, L.field22, (int)L.extraScientists);
    gs.setSelection({lab});
    gs.pressCommand(op2::UC_STOP_RESEARCH);
    for (int i = 0; i < 2; ++i) gs.onIdle(renderer, 0.05f);
    writeBmp32(prefix + "_stop.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    gs.pressCommand(-9);   // kCmdYes
    for (int i = 0; i < 3; ++i) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::printf("[Research] STOP+YES: lab acao=0x%X sub=0x%X topico=%d cientistas=%d\n", L.action, L.field22, L.labTopic,
                (int)L.extraScientists);
    op2::sendStartResearch(lab, topics[0], 5);
    // University: treino de alunos.
    int uni = 0;
    for (int i = 1; i < (int)op2::g_units.size() && !uni; ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id != -1 && (r.owner & 0x0F) == 0 && r.type == 0x26) uni = i;
    }
    if (uni) {
        const op2::UnitRecord& U = op2::g_units[uni];
        gs.setSelection({uni});
        gs.pressCommand(op2::UC_ASSIGN_STUDENTS);
        for (int i = 0; i < 2; ++i) gs.onIdle(renderer, 0.05f);
        writeBmp32(prefix + "_students.bmp", renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        const int w0 = op2::g_playerData[0].workers, s0 = op2::g_playerData[0].scientists;
        gs.pressCommand(-11);  // kCmdStartClass
        if (U.action != 0x17) op2::sendTrain(uni, 2);   // sem trabalhadores livres: força uma turma de 2
        std::printf("[Research] University id=%d operando=%d: acao=0x%X alunos=%d tempo=%d\n", uni,
                    (int)op2::structureOperational(U), U.action, U.extraWorkers, U.trainTime);
        int tu = 0;
        for (; tu < 20000 && U.action == 0x17; ++tu) gs.onIdle(renderer, 0.2501f / 4.0f);
        std::printf("[Research] treino fim t=%d: trabalhadores %d->%d cientistas %d->%d | %s\n", tu, w0,
                    op2::g_playerData[0].workers, s0, op2::g_playerData[0].scientists, op2::statusBarText().c_str());
        gs.setSelection({lab});
    }
    int t = 0;
    for (; t < 60000 && (L.action == 0x16); ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if (t == 3) std::printf("[Research] t=3 sub=0x%X pontos=%d\n", L.field22, L.field3C);
    }
    std::printf("[Research] t=%d fim: acao=0x%X pesquisado=%d | %s\n", t, L.action,
                (int)op2::researchHas(0, topics[0]), op2::statusBarText().c_str());
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --colonytest <map>: base mínima (CC + Tokamak e Agridome encostados + Lab
// isolado) → economia (sub_471F30) → liga o Lab por tubos (cellType 0x1A).
static int runColonyTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 colonytest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int bx = -1, by = -1;
    for (int y = 3; y < H - 6 && bx < 0; ++y)
        for (int x = 3; x < W - 16 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 3 && ok; ++dy)
                for (int dx = -2; dx <= 13 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { std::printf("[Colony] sem area livre\n"); return 3; }
    op2::Unit cc, tok, agr, lab;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{bx + 3, by}, 0);
    op2::TethysGame::CreateUnit(agr, 0x27 /*AGRIDOME*/, op2::LOCATION{bx, by + 2}, 0);
    op2::TethysGame::CreateUnit(lab, 0x31 /*LAB_STANDARD*/, op2::LOCATION{bx + 10, by}, 0);
    auto dump = [&](const char* fase) {
        const op2::ColonyState& c = op2::g_colony[0];
        std::printf("[Colony] %s: energia ger=%d exig=%d sobra=%d  trab sobra=%d cient sobra=%d  operando=%d/%d\n",
                    fase, c.powerGenerated, c.powerRequired, c.powerLeft,
                    c.workersLeft, c.scientistsLeft, c.activeCount, c.structCount);
        const char* nm[] = {"CC", "Tokamak", "Agridome", "Lab"};
        op2::Unit* us[] = {&cc, &tok, &agr, &lab};
        for (int i = 0; i < 4; ++i) {
            const op2::UnitRecord& r = op2::g_units[us[i]->id()];
            std::printf("    %-9s energia=%d trab=%d cient=%d operando=%d\n", nm[i],
                        (r.flags & op2::UNIT_POWERED) != 0, (r.flags & op2::UNIT_WORKERS) != 0,
                        (r.flags & op2::UNIT_SCIENTISTS) != 0, op2::structureOperational(r));
        }
    };
    std::printf("[Colony] ordem da lista (prioridade):");
    for (int id : op2::colonyStructures(0))
        std::printf(" %s", op2::structureCodeName(op2::g_units[id].type));
    std::printf("\n");
    for (int t = 0; t < 40; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
    dump("sem tubo");
    // Tubos ligando o Tokamak (x bx+2..bx+3) ao Lab (x bx+9..bx+11) na linha by.
    for (int x = bx + 4; x <= bx + 8; ++x)
        op2::g_gameMap.setTileAndCellType(x, by, op2::g_gameMap.getTile(op2::LOCATION{x, by}), 0x1A);
    op2::colonyMarkDirty(0);
    for (int t = 0; t < 2; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
    dump("com tubo");
    // Comida/populacao/moral (vez do jogador 0 a cada 256 ticks): 10 ciclos.
    op2::PlayerData& pd = op2::g_playerData[0];
    std::printf("[Colony] antes: comida=%d  trab=%d cient=%d criancas=%d  minerio=%d (cap %d)\n",
                pd.foodStored, pd.workers, pd.scientists, pd.kids, pd.ore, op2::g_colony[0].oreCapacity);
    for (int k = 0; k < 10; ++k) {
        // A vez de cada jogador (comida/moral) vem a cada 8 slots de 32 = 256 ticks.
        for (int t = 0; t < 256; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
        if (k == 4) {   // a partir do 5o ciclo: residencia encostada no Agridome
            op2::Unit res;
            op2::TethysGame::CreateUnit(res, 0x1E /*RESIDENCE*/, op2::LOCATION{bx + 3, by + 2}, 0);
            std::printf("[Colony]   + Residence\n");
        }
        const op2::ColonyState& m = op2::g_colony[0];
        std::printf("[Colony]   ciclo %2d: moral valor=%d nivel=%d tendencia=%+d  eventos=%d lotacao=%d comida=%d deslig=%d\n",
                    k + 1, m.moraleValue, pd.morale, m.moraleTrend, m.moraleEvents, m.crowding, m.foodState, m.disabledPct);
    }
    bool moralOk = true;
    {   // Moral forcada. O original liga o bit do jogador nos CINCO niveis na
        // primeira chamada (XOR) e quem le pega o primeiro, entao um
        // ForceMoraleGood sozinho valia GREAT e so a segunda chamada acertava.
        // Corrigido: a PRIMEIRA chamada ja da o nivel pedido (1 = Good).
        op2::TethysGame::ForceMoraleGood(0);
        for (int t = 0; t < 256; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
        moralOk = (pd.morale == 1);
        std::printf("[Colony] ForceMoraleGood: valor=%d nivel=%d (esperado 1) %s\n",
                    op2::g_colony[0].moraleValue, pd.morale, moralOk ? "ok" : "FALHOU");
        op2::TethysGame::FreeMoraleLevel(0);
        for (int t = 0; t < 256; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
        std::printf("[Colony] FreeMoraleLevel:  valor=%d nivel=%d\n", op2::g_colony[0].moraleValue, pd.morale);
    }
    const op2::ColonyState& c = op2::g_colony[0];
    std::printf("[Colony] 10 ciclos: comida=%d (produz %d, come %d, saldo %d)  trab=%d cient=%d criancas=%d\n",
                pd.foodStored, c.foodProduced, c.foodEaten, c.foodNet, pd.workers, pd.scientists, pd.kids);

    // CORRECAO: o "Storage Needed" so sai quando o estoque ACABA de encher,
    // e nao a cada recalculo. O filtro do log (messages.cpp) ja barra o
    // repetido por DOIS MINUTOS de tempo real, entao um teste rapido passaria
    // de graca: por isso o resetMessages() entre as medidas, que zera o filtro
    // e deixa so a memoria da colonia em jogo -- que e o que eu mudei.
    bool avisoOk = true;
    {
        auto conta = [](const char* txt) {
            int n = 0;
            for (int i = 0; i < op2::messageCount(); ++i)
                if (op2::messageAt(i).text.find(txt) != std::string::npos) ++n;
            return n;
        };
        auto roda = [&](int ticks) {
            for (int t = 0; t < ticks; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
        };
        // Sem fundicao nao ha capacidade nem acumulo: a condicao do aviso
        // (canAccumulateOre && cap-300 <= ore) nunca seria verdade, e o
        // controle abaixo acusou isso na primeira rodada.
        op2::Unit smelt;
        op2::TethysGame::CreateUnit(smelt, static_cast<op2::map_id>(0x36), op2::LOCATION{bx + 6, by + 2}, 0);
        pd.canAccumulateOre = 1;
        for (int t = 0; t < 64; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
        const int cap = op2::g_colony[0].oreCapacity;
        // (a) estoque LONGE do teto: nada a avisar, e zera a memoria.
        op2::resetMessages();
        pd.ore = 0;
        roda(300);    // a vez do jogador 0 vem a cada 256 ticks
        const int vazio = conta("Common Metals Storage Needed");
        // (b) enche: o aviso TEM de sair (controle -- sem isto o resto nao mede nada).
        op2::resetMessages();
        pd.ore = cap;
        roda(300);    // a vez do jogador 0 vem a cada 256 ticks
        const int encheu = conta("Common Metals Storage Needed");
        // (c) segue cheio, com o filtro de tempo real LIMPO: nao repete.
        op2::resetMessages();
        pd.ore = cap;
        roda(300);    // a vez do jogador 0 vem a cada 256 ticks
        const int repetiu = conta("Common Metals Storage Needed");
        const bool estoqueOk = (vazio == 0 && encheu >= 1 && repetiu == 0);
        if (!estoqueOk) avisoOk = false;

        // Mesma conta para a energia. Desligo os geradores (fica em
        // Shortage, que CONTINUA insistindo -- aviso de falta deve
        // insistir), religo (entra em Optimal: tem de avisar) e deixo
        // parado com o filtro limpo (nao repete).
        op2::structureDoIdle(tok.id());
        op2::colonyMarkDirty(0);
        roda(300);
        op2::resetMessages();
        roda(300);
        const int faltando = conta("Power Levels");
        op2::structureDoActivate(tok.id());
        op2::colonyMarkDirty(0);
        op2::resetMessages();
        roda(300);
        const int voltou = conta("Power Levels: Optimal");
        op2::resetMessages();
        roda(300);
        const int repetiuPwr = conta("Power Levels: Optimal");
        const bool pwrOk = faltando >= 1 && voltou >= 1 && repetiuPwr == 0;
        if (!pwrOk) avisoOk = false;
        std::printf("[Colony] energia: faltando=%d (esp >=1) voltou=%d (esp >=1) repetiu=%d (esp 0) %s\n",
                    faltando, voltou, repetiuPwr, pwrOk ? "ok" : "FALHOU");
        std::printf("[Colony] estoque: vazio=%d (esp 0) encheu=%d (esp >=1) repetiu=%d (esp 0) %s\n",
                    vazio, encheu, repetiu, estoqueOk ? "ok" : "FALHOU");
    }
    renderer.shutdown(); SDL_Quit();
    // Item 9 da lista do OPUPatch: o DoInfect das missoes nao fazia NADA
    // quando a partida tinha mais de um jogador (o original sai com
    // `cmp dword_56DEC0, 1 / jg`), e quase toda missao de campanha tem dois ou
    // tres. O controle aqui e o numero de jogadores: sem ele o caso nao
    // mediria nada.
    bool infectOk = true;
    {
        op2::resetPlayers(3);
        const int jogadores = op2::TethysGame::noPlayers();
        op2::Unit alvo;
        op2::TethysGame::CreateUnit(alvo, 0x1E /*RESIDENCE*/, op2::LOCATION{bx + 6, by + 4}, 0);
        const int id = alvo.id();
        bool pegou = false;
        if (id) {
            op2::g_units[id].action = 0;
            op2::g_units[id].damage = 0;
            const int antes = op2::g_units[id].damage;
            op2::unitDoInfect(id);
            pegou = op2::g_units[id].damage > antes &&
                    (op2::g_units[id].flags & 0x40000u) != 0;
        }
        infectOk = jogadores > 1 && pegou;
        std::printf("[Colony] DoInfect com %d jogadores: pegou=%d (esperado 1) %s\n",
                    jogadores, pegou ? 1 : 0, infectOk ? "ok" : "FALHOU");
    }
    return (avisoOk && moralOk && infectOk) ? 0 : 1;
}

// --combattest <map> <prefix> [w0 w1 w2 w3]: Laser Lynx + Rail Gun Tiger
// (jogador 0) contra Microwave Lynx + RPG Tiger (jogador 1, IA) a 3 tiles — ou
// as armas dadas em hex (0x49 vai num Scorpion); roda o laço real e registra
// alvos, disparos, dano e mortes; grava quadros <prefix>_tNNN.bmp (e _beam /
// _boom no 1º feixe/raio e na 1ª explosão).
static int runCombatTest(const std::string& mapName, const std::string& prefix, const int* wpn = nullptr) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 combattest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)                      // tira a ocupação da cena demo
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);
    int bx = -1, by = -1;
    for (int y = H / 2; y < H - 6 && bx < 0; ++y)
        for (int x = W / 2; x < W - 8 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 3 && ok; ++dy)
                for (int dx = -1; dx <= 5 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { std::printf("[Combat] sem area livre\n"); return 3; }
    op2::Unit a, b, c, d;
    int w[4] = {wpn ? wpn[0] : 0x3D, wpn ? wpn[1] : 0x3F, wpn ? wpn[2] : 0x3E, wpn ? wpn[3] : 0x40};
    // 0x41/0x42 na unidade 0: detona em t=30; 0x45: um Laser Lynx que se afunda (scuttle).
    const int selfDestruct = (w[0] == 0x41 || w[0] == 0x42 || w[0] == 0x45) ? w[0] : 0;
    if (w[0] == 0x45) w[0] = 0x3D;
    auto chassis = [&](int k) { return w[k] == 0x49 ? op2::mapScorpion : (k & 1) ? op2::mapTiger : op2::mapLynx; };
    op2::TethysGame::CreateUnit(a, chassis(0), op2::LOCATION{bx, by}, 0, w[0]);
    op2::TethysGame::CreateUnit(b, chassis(1), op2::LOCATION{bx, by + 2}, 0, w[1]);
    op2::TethysGame::CreateUnit(c, chassis(2), op2::LOCATION{bx + 3, by}, 1, w[2]);
    op2::TethysGame::CreateUnit(d, chassis(3), op2::LOCATION{bx + 3, by + 2}, 1, w[3]);
    const int ids[4] = {a.id(), b.id(), c.id(), d.id()};
    for (int id : ids)
        std::printf("[Combat] %d: %s  HP=%d alcance=%d\n", id, op2::unitTitle(op2::g_units[id]).c_str(),
                    op2::vehicleSheet(op2::g_units[id].type, 0)->hitPoints,
                    op2::weaponSheet(static_cast<int16_t>(op2::g_units[id].cargo)) ?
                    op2::weaponSheet(static_cast<int16_t>(op2::g_units[id].cargo))->player[0].range : -1);
    gs.setMouse(200, 150);
    gs.setCamera(bx * 32 - 200 + 48, by * 32 - 240 + 48);
    gs.setSelection({ids[0], ids[1]});
    int lastDmg[4] = {0, 0, 0, 0}, lastTgt[4] = {0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF};
    uint32_t lastFx[4] = {0, 0, 0, 0};
    int shots = 0;
    for (int t = 1; t <= 600; ++t) {
        op2::TethysGame::advanceTick();
        op2::TethysGame::advanceDaylight(op2::g_gameMap.internalWidth());
        op2::TethysGame::getRand(100);
        if (selfDestruct && t == 30 && op2::g_units[ids[0]].id != -1) {
            std::printf("[Combat] t=%d  %d autodestruicao\n", t, ids[0]);
            op2::unitSelfDestruct(ids[0]);
        }
        op2::tickColony();
        op2::tickMovement();
        const size_t before = op2::g_units.size();
        int projBefore = 0;
        for (size_t i = 1; i < before; ++i)
            if (op2::g_units[i].id != -1 && op2::isWeaponProjectile(op2::g_units[i].type) &&
                op2::g_units[i].field22 == 0x12) ++projBefore;
        op2::tickUnits();
        for (int k = 0; k < 4; ++k) {
            const op2::UnitRecord& r = op2::g_units[ids[k]];
            if (r.id == -1) { if (lastDmg[k] >= 0) std::printf("[Combat] t=%d  %d removido\n", t, ids[k]); lastDmg[k] = -1; continue; }
            const uint32_t fx = r.flags & 0x1C0000u;             // espuma / EMP / ESG
            if (fx != lastFx[k]) {
                std::printf("[Combat] t=%d  %d efeitos 0x%X (emp=%d espuma=%d esg=%d)\n", t, ids[k], fx,
                            r.empTicks, r.foamTicks, r.esgTicks);
                lastFx[k] = fx;
            }
            const int tg = r.hasTarget() ? r.target4C : 0x7FFF;
            if (tg != lastTgt[k]) {
                std::printf("[Combat] t=%d  %d alvo -> %d (torre %d)\n", t, ids[k], tg == 0x7FFF ? -1 : tg, r.turretDir);
                lastTgt[k] = tg;
            }
            if (r.damage != lastDmg[k] && lastDmg[k] >= 0) {
                std::printf("[Combat] t=%d  %d dano %d -> %d%s\n", t, ids[k], lastDmg[k], r.damage,
                            (r.flags & op2::UNIT_DEAD) ? " (MORTO)" : "");
                lastDmg[k] = r.damage;
            }
        }
        int projNow = 0;
        for (size_t i = 1; i < op2::g_units.size(); ++i)
            if (op2::g_units[i].id != -1 && op2::isWeaponProjectile(op2::g_units[i].type)) ++projNow;
        (void)projBefore;
        static int lastProj = 0;
        if (projNow > lastProj) shots += projNow - lastProj;
        lastProj = projNow;
        static bool beamShot = false, boomShot = false;
        const char* tag = nullptr;
        for (size_t i = 1; i < op2::g_units.size() && !tag; ++i) {
            const op2::UnitRecord& p = op2::g_units[i];
            if (p.id == -1 || !op2::isWeaponProjectile(p.type)) continue;
            if (!beamShot && p.type == w[0] && p.field22 == op2::SUB_WAIT && !p.exploded && p.field3C < 15) { beamShot = true; tag = "beam"; }
            else if (!boomShot && p.exploded && p.field22 == op2::SUB_WAIT) { boomShot = true; tag = "boom"; }
            if (tag) std::printf("[Combat] t=%d  projetil %d tipo 0x%X sub=0x%X f3C=%d impacto(%d,%d)\n", t, (int)i, p.type,
                                 p.field22, p.field3C, p.impactX, p.impactY);
        }
        if (tag || t == 8 || t == 12 || t == 18 || t == 40 || t == 60 || t == 120 || t == 136 || t == 200 || t == 400) {
            gs.onIdle(renderer, 0.0f);
            char fn[256];
            if (tag) std::snprintf(fn, sizeof(fn), "%s_%s.bmp", prefix.c_str(), tag);
            else std::snprintf(fn, sizeof(fn), "%s_t%03d.bmp", prefix.c_str(), t);
            writeBmp32(fn, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        }
    }
    std::printf("[Combat] projeteis criados (aprox.): %d\n", shots);
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --missiontest <DLL> <out.bmp>: carrega a missão pela DLL (loadMission),
// roda ~200 ticks e grava a tela; lista unidades e objetivos.
// IA: carrega a missão, manda os veículos do jogador 0 a um ponto (tile do
// port) e acompanha os grupos inimigos.
// --routetest <DLL> <x> <y> <ticks> <opção>: até 4 veículos de cada jogador
// (0 e 1) andam até (x, y); opção 0 = regra normal do +0x1B8, 14 = refinamento
// desligado p/ todos, 15 = ligado p/ todos. Mostra posição e chegada.
static int runRouteTest(const std::string& dll, int tx, int ty, int ticks, int opt) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 routetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    if (opt) op2::TethysGame::setCheatFlag(opt, 1);
    for (int k = 0; k < 40; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::vector<int> ids;
    for (int p = 0; p < 2; ++p) {
        op2::CommandPacket pkt;
        pkt.type = op2::CMD_MOVE;
        pkt.waypoints.push_back(op2::packLocation(tx, ty));
        for (size_t i = 1; i < op2::g_units.size() && pkt.unitIds.size() < 4; ++i) {
            const op2::UnitRecord& r = op2::g_units[i];
            if (r.id != -1 && (r.owner & 0x0F) == p && (r.flags & op2::UNIT_VEHICLE) && !(r.flags & op2::UNIT_DEAD))
                pkt.unitIds.push_back(static_cast<int>(i));
        }
        for (int id : pkt.unitIds) {
            ids.push_back(id);
            const op2::UnitRecord& r = op2::g_units[id];
            std::printf("[Route] j%d unidade %d tipo 0x%X em (%d,%d) refino=%d\n", p, id, r.type, r.pixelX >> 5,
                        r.pixelY >> 5, op2::g_playerData[p].pathRefine);
        }
        if (!pkt.unitIds.empty()) op2::processCommandPacket(p, pkt);
    }
    std::vector<int> arrived(ids.size(), -1), steps(ids.size(), 0), lastX(ids.size()), lastY(ids.size()),
        lastDmg(ids.size(), 0);
    for (size_t k = 0; k < ids.size(); ++k) {
        lastX[k] = op2::g_units[ids[k]].pixelX >> 5;
        lastY[k] = op2::g_units[ids[k]].pixelY >> 5;
    }
    for (int t = 0; t < ticks; ++t) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        for (size_t k = 0; k < ids.size(); ++k) {
            const op2::UnitRecord& r = op2::g_units[ids[k]];
            const int x = r.pixelX >> 5, y = r.pixelY >> 5;
            if (x != lastX[k] || y != lastY[k]) { ++steps[k]; lastX[k] = x; lastY[k] = y; }
            if (r.id != -1 && r.damage != lastDmg[k]) {
                std::printf("[Route] unidade %d t=%d (%d,%d) dano %d -> %d acao=%d\n", ids[k], t, x, y, lastDmg[k], r.damage,
                            r.action);
                lastDmg[k] = r.damage;
            }
            if (arrived[k] < 0 && !r.hasOrder) {
                arrived[k] = t;
                std::printf("[Route] unidade %d parou em t=%d (%d,%d) apos %d casas id=%d flags=%X acao=%d dano=%d\n",
                            ids[k], t, x, y, steps[k], r.id, r.flags, r.action, r.damage);
            }
        }
        if (t % 400 == 399) {
            std::printf("[Route] t=%d", t + 1);
            for (size_t k = 0; k < ids.size(); ++k) std::printf(" %d:(%d,%d)", ids[k], lastX[k], lastY[k]);
            std::printf("\n");
        }
    }
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --campaigntest <DLL1> <DLL2> <ticks>: campanha (GameStartInfo flag 8, Eden,
// normal) na DLL1; depois de <ticks>, resultados como se cumprida (sub_489A30)
// e a DLL2 começa com eles (sub_4898F0). Mostra recursos e pesquisa.
static int runCampaignTest(const std::string& dll1, const std::string& dll2, int ticks) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 campaigntest", 1280, 960, false)) { SDL_Quit(); return 1; }
    auto show = [](const char* tag) {
        const op2::PlayerData& d = op2::g_playerData[0];
        int techs = 0;
        for (int i = 0; i < op2::techCount(); ++i) if (op2::researchHas(0, i)) ++techs;
        std::printf("[Camp] %s: dif=%d minerio=%d raro=%d comida=%d trab=%d cient=%d criancas=%d rlv=%d moral=%d tecnologias=%d\n",
                    tag, d.difficulty, d.ore, d.rareOre, d.foodStored, d.workers, d.scientists, d.kids, d.rlvCount,
                    d.morale, techs);
    };
    op2::GameStartInfo gsi;
    gsi.flags = 8;
    gsi.playerWord[0] = 4 | 8;
    op2::GameState gs;
    if (!gs.loadMission(dll1, &gsi)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    show("inicio 1");
    for (int k = 0; k < ticks; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
    show("fim 1");
    op2::startInfoCollectResults(gsi, op2::currentMission() ? op2::currentMission()->desc : nullptr, 3);
    ++gsi.missionNumber;
    std::printf("[Camp] resultados: missao=%d minerio=%d comida=%d trab=%d espaco=%X tecnicas=%d/%d\n", gsi.missionNumber,
                gsi.ore, gsi.food, gsi.workers, gsi.spaceWord, static_cast<int>(gsi.techIdx.size()), gsi.techCount);
    op2::GameState gs2;
    if (!gs2.loadMission(dll2, &gsi)) { renderer.shutdown(); SDL_Quit(); return 3; }
    gs2.onActivate();
    show("inicio 2");
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// Hash do estado da simulação (unidades, jogadores, colônia, RNG, relógio).
static uint64_t simHash() {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](int64_t v) { h = (h ^ static_cast<uint64_t>(v)) * 1099511628211ull; };
    mix(op2::TethysGame::tick());
    mix(op2::TethysGame::getRandState());
    // O ciclo dia/noite e estado de simulacao (escuridao por coluna): entra no
    // hash para o --savetest cobrir o bloco de sub_420580.
    mix(op2::TethysGame::daylightPhase());
    mix(op2::TethysGame::daylightPos());
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1) continue;
        mix(static_cast<int64_t>(i)); mix(r.type); mix(r.owner); mix(r.pixelX); mix(r.pixelY); mix(r.damage);
        mix(r.action); mix(r.field22); mix(r.flags); mix(r.cargo);
    }
    for (int p = 0; p < 7; ++p) {
        const op2::PlayerData& d = op2::g_playerData[p];
        mix(d.ore); mix(d.rareOre); mix(d.foodStored); mix(d.workers); mix(d.scientists); mix(d.kids); mix(d.morale);
        mix(op2::g_colony[p].moraleValue); mix(op2::g_colony[p].powerGenerated);
    }
    return h;
}

// --savetest <DLL> <t1> <t2>: roda t1 ticks, salva (SGAME9.OP2), roda t2 e
// anota o hash; recarrega o arquivo num GameState novo, roda t2 e compara.
static int runSaveTest(const std::string& dll, int t1, int t2) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 savetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    const std::string path = op2::saveSlotPath(9);
    uint64_t hSaved = 0, hA = 0, hB = 0;
    int tickA = 0, tickB = 0;
    {
        op2::GameState gs;
        if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
        gs.onActivate();
        while (static_cast<int>(op2::TethysGame::tick()) < t1 && !gs.gameEnded()) gs.onIdle(renderer, 0.02f);
        if (gs.gameEnded()) { std::printf("[SaveTest] partida encerrada antes do tick %d\n", t1); return 6; }
        // Marca o estado do minimapa (bloco de sub_420580) para conferir depois.
        gs.debugMiniTactical();
        gs.debugGlobeSpin(0x1234);
        if (!gs.saveToFile(path, "savetest")) { std::printf("[SaveTest] falhou ao gravar\n"); return 3; }
        hSaved = simHash();
        const int stop = static_cast<int>(op2::TethysGame::tick()) + t2;
        while (static_cast<int>(op2::TethysGame::tick()) < stop && !gs.gameEnded()) gs.onIdle(renderer, 0.02f);
        hA = simHash();
        tickA = static_cast<int>(op2::TethysGame::tick());
    }
    {
        op2::GameState gs;
        if (!gs.loadSavedGame(path)) { std::printf("[SaveTest] falhou ao carregar\n"); return 4; }
        gs.onActivate();
        const uint64_t hLoaded = simHash();
        std::printf("[SaveTest] hash ao gravar %016llx, ao carregar %016llx (%s); fase do dia %d, coluna %d\n",
                    (unsigned long long)hSaved, (unsigned long long)hLoaded,
                    hSaved == hLoaded ? "igual" : "DIFERENTE",
                    op2::TethysGame::daylightPhase(), op2::TethysGame::daylightPos());
        std::printf("[SaveTest] luz: anda %d, em toda parte %d, escuridao da coluna 40 = %d\n",
                    op2::TethysGame::daylightMoves(), op2::TethysGame::daylightEverywhere(),
                    op2::g_gameMap.lightDarkness(40 + op2::g_gameMap.internalXOffset()
                                                 - op2::TethysGame::daylightPos()));
        std::printf("[SaveTest] minimapa: tatica %d (esperado 1), globo %d, giro 0x%X (esperado 0x1234)\n",
                    gs.debugMiniTacticalOn() ? 1 : 0, gs.debugMiniGlobeOn() ? 1 : 0, gs.debugGlobeRotation());
        const int stop = static_cast<int>(op2::TethysGame::tick()) + t2;
        while (static_cast<int>(op2::TethysGame::tick()) < stop && !gs.gameEnded()) gs.onIdle(renderer, 0.02f);
        hB = simHash();
        tickB = static_cast<int>(op2::TethysGame::tick());
    }
    std::printf("[SaveTest] depois de +%d ticks: original %016llx (t=%d), recarregado %016llx (t=%d) -> %s\n", t2,
                (unsigned long long)hA, tickA, (unsigned long long)hB, tickB, hA == hB ? "IGUAL" : "DIVERGIU");
    renderer.shutdown();
    SDL_Quit();
    return hA == hB ? 0 : 5;
}

static int runAiTest(const std::string& dll, int tx, int ty, int ticks) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 aitest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    for (int k = 0; k < 40; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
    op2::CommandPacket pkt;
    pkt.type = op2::CMD_MOVE;
    pkt.waypoints.push_back(op2::packLocation(tx, ty));
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id != -1 && (r.owner & 0x0F) == 0 && (r.flags & op2::UNIT_VEHICLE) && (r.flags & op2::UNIT_ARMED))
            pkt.unitIds.push_back(static_cast<int>(i));
    }
    std::printf("[AiTest] %d veiculos armados do jogador 0 -> (%d,%d)\n", (int)pkt.unitIds.size(), tx, ty);
    op2::processCommandPacket(0, pkt);
    for (int k = 0; k < ticks; ++k) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if (k % 500 == 499) {
            int hp0 = 0, dead0 = 0, hp2 = 0, dead2 = 0;
            for (size_t i = 1; i < op2::g_units.size(); ++i) {
                const op2::UnitRecord& r = op2::g_units[i];
                if (r.id == -1 || !(r.flags & op2::UNIT_VEHICLE)) continue;
                const int o = r.owner & 0x0F;
                if (o == 0) { if (r.flags & op2::UNIT_DEAD) ++dead0; else hp0 += r.damage; }
                if (o == 2) { if (r.flags & op2::UNIT_DEAD) ++dead2; else hp2 += r.damage; }
            }
            std::printf("[AiTest] t=%d jogador0 dano=%d mortos=%d | jogador2 dano=%d mortos=%d\n", k + 1, hp0, dead0, hp2,
                        dead2);
            op2::aiGroupsDump();
        }
    }
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --pinwheeltest <DLL> <chamadas> <ticks>: carrega o jogo de colônia, chama o
// AIStateChange exportado pela DLL <chamadas> vezes (avança o estado da IA sem
// esperar os prazos) e acompanha o Pinwheel e os grupos a cada 500 ticks.
static int runPinwheelTest(const std::string& dll, int calls, int ticks) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 pinwheeltest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { std::printf("[PW] falhou\n"); renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::MissionScript* m = op2::currentMission();
    auto fn = m ? reinterpret_cast<void (*)()>(m->proc("AIStateChange")) : nullptr;
    if (!fn) { std::printf("[PW] sem AIStateChange\n"); renderer.shutdown(); SDL_Quit(); return 3; }
    for (int k = 0; k < calls; ++k) fn();
    op2::pinwheelDump();
    std::fflush(stdout);
    int last = op2::TethysGame::tick();
    while (op2::TethysGame::tick() < ticks) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        if (op2::TethysGame::tick() - last >= 500) {
            last = op2::TethysGame::tick();
            int enemies = 0;
            for (size_t i = 1; i < op2::g_units.size(); ++i) {
                const op2::UnitRecord& r = op2::g_units[i];
                if (r.id != -1 && !(r.flags & op2::UNIT_DEAD) && (r.owner & 0x0F) == 1 && (r.flags & op2::UNIT_VEHICLE)) ++enemies;
            }
            std::printf("[PW] t=%d veiculos do j1=%d\n", last, enemies);
            op2::pinwheelDump();
            std::fflush(stdout);
        }
    }
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// Fork: --dockclicktest <DLL>: o CLIQUE DIREITO de mover, pelo caminho real de
// entrada (handleEvent). Seleciona um Cargo Truck do jogador local, clica com o
// botão direito na parte de BAIXO da doca de um Agridome (onde o alvo antigo,
// em coordenadas do quadro e não da vista, caía no tile de baixo) e confere que
// o caminhão ATRACA; depois clica num tile livre e confere que ele chega
// exatamente lá. Sai 0 se os dois casos passam.
static int runDockClickTest(const std::string& dll, int bldType = 0x27) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 dockclicktest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { std::printf("[DockClick] missao falhou\n"); renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    gs.onIdle(renderer, 0.0f);                       // quadro do tamanho da janela + layout
    const int me = op2::TethysGame::localPlayer();
    int agri = 0, truck = 0;
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || (r.owner & 0x0F) != me) continue;
        if (!agri && r.type == bldType) agri = static_cast<int>(i);
    }
    // Um caminhão que PODE atracar no Agridome (vazio ou com comida, sub_44DA80);
    // com outra carga o original não faz nada ao clicar na doca.
    for (size_t i = 1; i < op2::g_units.size() && agri; ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || (r.owner & 0x0F) != me || r.type != op2::mapCargoTruck) continue;
        const bool can = op2::vehicleCanDock(static_cast<int>(i), agri, true);
        std::printf("[DockClick] caminhao %d carga '%s' pode atracar=%s\n", static_cast<int>(i),
                    op2::truckCargoName(r), can ? "sim" : "nao");
        if (!truck && can) truck = static_cast<int>(i);
    }
    int dx = 0, dy = 0;
    if (!agri || !truck || !op2::dockCell(agri, dx, dy)) {
        std::printf("[DockClick] sem predio 0x%02X/caminhao/doca (bld=%d truck=%d)\n", bldType, agri, truck);
        renderer.shutdown(); SDL_Quit(); return 3;
    }
    auto tileOf = [](int id, int& x, int& y) {
        const op2::UnitRecord& r = op2::g_units[id];
        x = op2::unitOX(r) >> 5; y = op2::unitOY(r) >> 5;
    };
    // Clique direito no ponto (px, py) do mapa, pela tela: a câmera põe o ponto
    // dentro da vista; o quadro é 1:1 com a janela.
    auto rightClick = [&](int px, int py) {
        gs.setCamera(std::max(0, px - 200), std::max(0, py - 200));
        gs.onIdle(renderer, 0.0f);
        SDL_Event ev{};
        ev.type = SDL_MOUSEBUTTONDOWN;
        ev.button.button = SDL_BUTTON_RIGHT;
        ev.button.x = op2::GameState::kViewX + (px - gs.camX());
        ev.button.y = op2::GameState::kViewY + (py - gs.camY());
        gs.handleEvent(renderer, ev);
    };
    auto run = [&](int ticks) { for (int k = 0; k < ticks; ++k) gs.onIdle(renderer, 0.2501f / 4.0f); };
    int tx, ty;
    tileOf(truck, tx, ty);
    const int sx0 = tx, sy0 = ty;                    // tile de partida (fica livre)
    std::printf("[DockClick] predio 0x%02X %d doca (%d,%d); caminhao %d em (%d,%d)\n", bldType, agri, dx, dy, truck, tx, ty);

    // 1) doca: clique 26 px abaixo do topo do tile (parte de baixo).
    gs.setSelection({truck});
    rightClick(dx * 32 + 16, dy * 32 + 26);
    int docked = 0;
    // Atracou = ficou na doca (dockedVehicle) ou já carregou nela: na mina o
    // caminhão sai cheio de minério, e aí dockedVehicle (só aceita vazio) é 0.
    const uint16_t cargo0 = op2::g_units[truck].cargo;
    for (int k = 0; k < 1200 && !docked; k += 25) {
        run(25);
        docked = op2::dockedVehicle(agri) == truck || op2::g_units[truck].cargo != cargo0;
    }
    tileOf(truck, tx, ty);
    std::printf("[DockClick] carga final '%s' acao %d\n", op2::truckCargoName(op2::g_units[truck]),
                op2::g_units[truck].action);
    // A animação de carga ([+0x22] = 0x0D) trava o caminhão, como no original;
    // espera ela acabar antes do próximo clique.
    for (int k = 0; k < 400 && op2::g_units[truck].field22 == 0x0D; ++k) run(1);
    const bool okDock = docked != 0;
    std::printf("[DockClick] doca: caminhao em (%d,%d), atracado=%s -> %s\n", tx, ty, docked ? "sim" : "nao",
                okDock ? "PASSOU" : "FALHOU");

    // 2) tile livre: volta ao tile de partida, clique na parte de baixo do tile.
    const int gx = sx0, gy = sy0;
    rightClick(gx * 32 + 16, gy * 32 + 26);
    bool arrived = false;
    for (int k = 0; k < 1200 && !arrived; k += 25) { run(25); tileOf(truck, tx, ty); arrived = tx == gx && ty == gy; }
    std::printf("[DockClick] mover p/ (%d,%d): caminhao em (%d,%d) -> %s\n", gx, gy, tx, ty,
                arrived ? "PASSOU" : "FALHOU");

    // 3) fork: um caminhão que NÃO pode atracar (carregado) vai até a doca
    // mesmo assim (o original ignorava o clique).
    int other = 0;
    for (size_t i = 1; i < op2::g_units.size() && !other; ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id != -1 && (r.owner & 0x0F) == me && r.type == op2::mapCargoTruck &&
            !op2::vehicleCanDock(static_cast<int>(i), agri, true)) other = static_cast<int>(i);
    }
    bool reached = true;
    if (other) {
        gs.setSelection({other});
        rightClick(dx * 32 + 16, dy * 32 + 16);
        reached = false;
        for (int k = 0; k < 1600 && !reached; k += 25) { run(25); tileOf(other, tx, ty); reached = tx == dx && ty == dy; }
        std::printf("[DockClick] carregado '%s' p/ a doca: em (%d,%d) -> %s\n",
                    op2::truckCargoName(op2::g_units[other]), tx, ty, reached ? "PASSOU" : "FALHOU");
    }
    renderer.shutdown(); SDL_Quit();
    return (okDock && arrived && reached) ? 0 : 4;
}

static int runMissionTest(const std::string& dll, const std::string& outPath, int ticks,
                          int camTileX = -1, int camTileY = -1) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 missiontest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { std::printf("[MissionTest] falhou\n"); renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1) continue;
        std::printf("[MissionTest] %2d tipo 0x%02X dono %d tile (%d,%d)", r.id, r.type, r.owner & 0x0F,
                    op2::unitOX(r) >> 5, op2::unitOY(r) >> 5);
        if (r.flags & op2::UNIT_BUILDING)
            std::printf(" estado %d anim %d", static_cast<int>((r.state34 >> 16) & 0xFFFF), op2::structureCurAnim(r));
        std::printf("\n");
    }
    std::string lastMsg;
    for (int k = 0; k < ticks; ++k) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        const std::string m = op2::statusBarText();
        if (m != lastMsg) { std::printf("[MissionTest] t=%d msg: %s\n", op2::TethysGame::tick(), m.c_str()); lastMsg = m; }
        if (k % 2500 == 2499) {
            std::printf("[MissionTest] --- t=%d\n", op2::TethysGame::tick());
            op2::aiGroupsDump();
            op2::pinwheelDump();
        }
    }
    {   int hurt = 0, dead = 0;
        for (size_t i = 1; i < op2::g_units.size(); ++i) {
            const op2::UnitRecord& r = op2::g_units[i];
            if (r.id == -1) continue;
            if (r.flags & op2::UNIT_DEAD) ++dead; else if (r.damage > 0 && (r.flags & op2::UNIT_DAMAGEABLE)) ++hurt;
        }
        std::printf("[MissionTest] unidades danificadas=%d mortas=%d\n", hurt, dead);
    }
    op2::aiGroupsDump();
    for (const std::string& o : op2::missionObjectives()) std::printf("[MissionTest] objetivo: %s\n", o.c_str());
    {   // pesquisa do jogador 0: nº de tecnologias e alguns blocos melhorados
        int n = 0, up = 0;
        for (int i = 1; i < op2::techCount(); ++i)
            if (op2::techAt(i)->researched & 1u) { ++n; up += static_cast<int>(op2::techAt(i)->upgrades.size()); }
        const op2::VehPlayerStats* cv0 = op2::vehicleSheet(op2::mapConVec, 0);
        const op2::VehPlayerStats* cv1 = op2::vehicleSheet(op2::mapConVec, 1);
        std::printf("[MissionTest] pesquisa j0: %d tecnologias (%d melhorias); ConVec HP j0=%d j1=%d prodRate j0=%d j1=%d\n",
                    n, up, cv0->hitPoints, cv1->hitPoints, cv0->productionRate, cv1->productionRate);
    }
    std::printf("[MissionTest] tick=%d fim=%d\n", op2::TethysGame::tick(), op2::gameEndCode());
    if (camTileX >= 0 && camTileY >= 0) {      // fork: câmera opcional p/ a captura
        gs.setCamera(camTileX * 32, camTileY * 32);
        gs.onIdle(renderer, 0.0f);
    }
    bool ok = writeBmp32(outPath, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --gptest <map> <prefix> <arma>: CC + Tokamak + Guard Post (jogador 0, arma em
// hex) encostados contra dois Laser Lynx do jogador 1 a 3 tiles; registra alvo,
// torre, disparos e dano; grava quadros <prefix>_tNNN.bmp.
static int runGuardPostTest(const std::string& mapName, const std::string& prefix, int weapon) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 gptest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);
    int bx = -1, by = -1;
    for (int y = H / 2; y < H - 8 && bx < 0; ++y)
        for (int x = W / 2; x < W - 8 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 6 && ok; ++dy)
                for (int dx = -2; dx <= 6 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { std::printf("[GP] sem area livre\n"); return 3; }
    op2::g_playerData[0].workers = 40; op2::g_playerData[0].scientists = 10;
    op2::Unit cc, tok, gp, e1, e2;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{bx + 3, by}, 0);
    op2::TethysGame::CreateUnit(gp, 0x17, op2::LOCATION{bx, by + 2}, 0, weapon);
    op2::TethysGame::CreateUnit(e1, op2::mapLynx, op2::LOCATION{bx - 1, by + 5}, 1, 0x3D);
    op2::TethysGame::CreateUnit(e2, op2::mapLynx, op2::LOCATION{bx + 2, by + 5}, 1, 0x3D);
    const int g = gp.id();
    std::printf("[GP] %s alcance=%d\n", op2::unitTitle(op2::g_units[g]).c_str(),
                op2::weaponSheet(weapon) ? op2::weaponSheet(weapon)->player[0].range : -1);
    gs.setCamera(bx * 32 - 200, by * 32 - 150);
    int lastTgt = -2, lastD1 = 0, lastD2 = 0, lastGd = 0, lastOp = -1;
    for (int t = 1; t <= 500; ++t) {
        if ((weapon == 0x43 || weapon == 0x44) && t == 30) {          // DETONATE (sub_457E40)
            std::printf("[GP] t=%d DETONATE disponivel=%d\n", t,
                        op2::uiCommandAvailable(op2::UC_DETONATE, {g}) ? 1 : 0);
            op2::uiCommandClick(op2::UC_DETONATE, {g});
        }
        op2::TethysGame::advanceTick();
        op2::TethysGame::advanceDaylight(op2::g_gameMap.internalWidth());
        op2::tickColony();
        op2::tickMovement();
        op2::tickUnits();
        const op2::UnitRecord& r = op2::g_units[g];
        const int op = op2::structureOperational(r) ? 1 : 0;
        if (op != lastOp) { std::printf("[GP] t=%d operando=%d rede=%d\n", t, op, r.netConnected); lastOp = op; }
        const int tg = r.hasTarget() ? r.target4C : -1;
        if (tg != lastTgt) { std::printf("[GP] t=%d alvo -> %d (torre %d)\n", t, tg, r.turretDir); lastTgt = tg; }
        if (r.damage != lastGd) { std::printf("[GP] t=%d GP dano %d -> %d\n", t, lastGd, r.damage); lastGd = r.damage; }
        const op2::UnitRecord& a = op2::g_units[e1.id()];
        const op2::UnitRecord& b = op2::g_units[e2.id()];
        if (a.id != -1 && a.damage != lastD1) { std::printf("[GP] t=%d Lynx1 dano %d -> %d\n", t, lastD1, a.damage); lastD1 = a.damage; }
        if (b.id != -1 && b.damage != lastD2) { std::printf("[GP] t=%d Lynx2 dano %d -> %d\n", t, lastD2, b.damage); lastD2 = b.damage; }
        if (t == 20 || t == 60 || t == 200) {
            gs.onIdle(renderer, 0.0f);
            char fn[256]; std::snprintf(fn, sizeof(fn), "%s_t%03d.bmp", prefix.c_str(), t);
            writeBmp32(fn, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        }
    }
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --salvagetest <map>: destroço (descoberto, carregado pelo caminhão no tile —
// ação 0x0F — e entregue na baia do Spaceport → tecnologia pesquisada), entulho
// carregado no tile, e o Robo-Surveyor pesquisando um beacon (ação 0x11).
static int runSalvageTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 salvagetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::loadTechTree("MULTITEK.TXT", 12);
    op2::resetUnits();
    // eden04 esta em noite plena (a missao nao chama SetInitialLightLevel) e o
    // teste mede a VARREDURA, nao o ciclo dia/noite: sem isto o caminhao sem
    // farois anda ~2x mais devagar (termo de escuridao de sub_44B650).
    op2::TethysGame::SetDaylightEverywhere(1);
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);
    // A área precisa caber TUDO: as docas/veículos da 1ª parte (dx −2..10) e,
    // na parte da região, o entulho em bx+12..bx+14 e o GORF em bx+18 com a sua
    // doca — num terreno impassável o caminhão cheio não chega ao GORF e fica
    // parado (o motor, como o original, apenas para quando não há rota).
    int bx = -1, by = -1;
    for (int y = 4; y < H - 12 && bx < 0; ++y)
        for (int x = 4; x < W - 24 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 9 && ok; ++dy)
                for (int dx = -2; dx <= 21 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) return 3;
    auto tick = [&](int n) {
        for (int k = 0; k < n; ++k) {
            op2::TethysGame::advanceTick();
            op2::tickColony();
            op2::tickMovement();
            op2::tickUnits();
        }
    };
    // Destroço com a tecnologia 10303 (ver MULTITEK) em (bx+6, by+6), visível.
    int tech = 0;
    for (int k = 0; op2::techAt(k) && !tech; ++k) if (op2::techAt(k)->id > 8000) tech = op2::techAt(k)->id;
    op2::Unit w;
    op2::createUnit(w, static_cast<op2::map_id>(0x54), op2::LOCATION{bx + 6, by + 6}, 6);
    op2::g_units[w.id()].wreckTech = tech;
    std::printf("[Salvage] destroço %d tecnologia %d; conhecido por 0: %d\n", w.id(), tech, op2::wreckKnownBy(op2::g_units[w.id()], 0));
    op2::Unit scout;
    op2::TethysGame::CreateUnit(scout, op2::mapScout, op2::LOCATION{bx + 8, by + 6}, 0);
    tick(2);
    std::printf("[Salvage] após o Scout: conhecido por 0: %d (mascara 0x%X)\n", op2::wreckKnownBy(op2::g_units[w.id()], 0),
                op2::g_units[w.id()].wreckMask);
    // Caminhão sobre o destroço: chegada → ação 0x0F → carga 9.
    op2::Unit truck;
    op2::TethysGame::CreateUnit(truck, op2::mapCargoTruck, op2::LOCATION{bx + 6, by + 6}, 0);
    op2::unitArrived(truck.id());
    std::printf("[Salvage] caminhão acao=0x%X\n", op2::g_units[truck.id()].action);
    tick(40);
    const op2::UnitRecord& t = op2::g_units[truck.id()];
    std::printf("[Salvage] caminhão carga tipo %d qtd %d; destroço vivo=%d\n", t.cargo & 0xF, (t.cargo >> 4) & 0xFFF,
                op2::g_units[w.id()].id != -1 && op2::g_units[w.id()].type == 0x54);
    // Spaceport: põe o caminhão na doca e troca com a baia 0.
    op2::Unit cc, tok, sp;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{bx + 3, by}, 0);
    op2::TethysGame::CreateUnit(sp, static_cast<op2::map_id>(0x37), op2::LOCATION{bx - 1, by + 3}, 0);
    int dx, dy;
    if (op2::dockCell(sp.id(), dx, dy)) {
        op2::UnitRecord& tr = op2::g_units[truck.id()];
        op2::g_gameMap.clearUnitOccupancy(tr.pixelX >> 5, tr.pixelY >> 5);
        tr.pixelX = dx << 5; tr.pixelY = dy << 5;
        op2::g_gameMap.setUnitOccupancy(dx, dy, truck.id());
        tr.field22 = 0; tr.action = 0;
    }
    const int idx = op2::techIndex(tech);
    std::printf("[Salvage] doca (%d,%d) veiculo na doca=%d; pesquisada antes=%d\n", dx, dy, op2::dockedVehicle(sp.id()),
                op2::researchHas(0, idx));
    const bool ok = op2::processBayTransfer(0, sp.id(), 0);
    tick(400);
    std::printf("[Salvage] troca=%d pesquisada depois=%d; caminhão carga %d sub=0x%X\n", ok, op2::researchHas(0, idx),
                op2::g_units[truck.id()].cargo, op2::g_units[truck.id()].field22);
    // Entulho: tile da 1ª faixa do 1º tipo de terreno, célula 0x16.
    const auto& types = op2::g_gameMap.terrainTypes();
    const int rx = bx + 9, ry = by + 7;
    if (!types.empty()) op2::g_gameMap.setTileAndCellType(rx, ry, types[0].word(6), 0x16);
    op2::Unit t2;
    op2::TethysGame::CreateUnit(t2, op2::mapCargoTruck, op2::LOCATION{rx, ry}, 0);
    op2::unitArrived(t2.id());
    tick(40);
    const uint32_t c = op2::g_gameMap.rawCell(rx, ry);
    std::printf("[Salvage] entulho: carga tipo %d qtd %d; tile agora tipo 0x%X\n", op2::g_units[t2.id()].cargo & 0xF,
                (op2::g_units[t2.id()].cargo >> 4) & 0xFFF, c & 0x1F);

    // Destroco EM CIMA de entulho: o original testava o entulho primeiro e
    // carregava entulho, terraplanando o tile -- o destroco ia junto. Agora o
    // destroco vem antes. Controle: o caso de cima, so entulho, segue dando 6.
    bool wreckOk = true;
    {
        const int wx = bx + 9, wy = by + 9;
        if (!types.empty()) op2::g_gameMap.setTileAndCellType(wx, wy, types[0].word(6), 0x16);
        op2::Unit wk;                       // o que o T_CreateWreck faz: tipo 0x54, dono 6
        op2::TethysGame::CreateUnit(wk, static_cast<op2::map_id>(0x54), op2::LOCATION{wx, wy}, 6);
        op2::g_units[wk.id()].wreckTech = 0x1F4A;
        op2::wreckDiscover(wk.id(), 0);
        op2::Unit t3;
        op2::TethysGame::CreateUnit(t3, op2::mapCargoTruck, op2::LOCATION{wx, wy}, 0);
        op2::unitArrived(t3.id());
        tick(40);
        const int tipo = op2::g_units[t3.id()].cargo & 0xF;
        const int celula = static_cast<int>(op2::g_gameMap.rawCell(wx, wy) & 0x1F);
        const int entulhoSo = op2::g_units[t2.id()].cargo & 0xF;
        wreckOk = (tipo == 9) && (celula == 0x16) && (entulhoSo == 6);
        std::printf("[Salvage] destroco sobre entulho: carga tipo=%d (esperado 9) tile=0x%X (esperado 0x16) | controle entulho=%d (esperado 6) %s\n",
                    tipo, celula, entulhoSo, wreckOk ? "ok" : "FALHOU");
    }
    // Beacon + Robo-Surveyor.
    const int beacon = op2::createBeacon(0x51, bx + 2, by + 8, 0, 1, 1);
    op2::Unit sv;
    op2::TethysGame::CreateUnit(sv, op2::mapRoboSurveyor, op2::LOCATION{bx + 2, by + 8}, 0);
    op2::unitArrived(sv.id());
    std::printf("[Salvage] surveyor acao=0x%X\n", op2::g_units[sv.id()].action);
    tick(40);
    const op2::UnitRecord& s = op2::g_units[sv.id()];
    std::printf("[Salvage] beacon pesquisado por 0: %d; surveyor em (%d,%d) msg=%s\n",
                [&] { for (size_t k = 1; k < op2::g_units.size(); ++k) if (op2::g_units[k].id != -1 && op2::g_units[k].type == 0x51) return (int)(op2::g_units[k].surveyedMask & 1); return -1; }(), s.pixelX >> 5, s.pixelY >> 5,
                op2::statusBarText().c_str());
    // --- Comando SALVAGE de região (ação 0x1F): varredura + coleta + GORF ---
    const int sx0 = bx + 12, sy0 = by + 2;                 // região de 3x3 com entulho
    if (!types.empty())
        for (int dy = 0; dy < 3; ++dy)
            for (int dx = 0; dx < 3; ++dx)
                op2::g_gameMap.setTileAndCellType(sx0 + dx, sy0 + dy, types[0].word(6), 0x16);
    op2::Unit gorf;
    op2::TethysGame::CreateUnit(gorf, static_cast<op2::map_id>(0x39), op2::LOCATION{sx0 + 6, sy0}, 0);
    op2::g_units[gorf.id()].flags |= 0xE000u;              // POWERED|WORKERS|SCIENTISTS (operando)
    op2::Unit st;
    op2::TethysGame::CreateUnit(st, op2::mapCargoTruck, op2::LOCATION{sx0 - 2, sy0 + 1}, 0);
    const int oreBefore = op2::g_playerData[0].ore;
    op2::sendSalvage({st.id()}, sx0, sy0, sx0 + 3, sy0 + 3);
    int gdx = -1, gdy = -1; const bool gdok = op2::dockCell(gorf.id(), gdx, gdy);
    std::printf("[Salvage] REGIAO: caminhao %d acao=0x%X; GORF %d operando=%d doca ok=%d (%d,%d)\n", st.id(),
                op2::g_units[st.id()].action, gorf.id(), op2::structureOperational(op2::g_units[gorf.id()]), gdok, gdx, gdy);
    (void)oreBefore;
    int delivered = 0;
    for (int k = 0; k < 4000; ++k) {
        op2::g_units[gorf.id()].flags |= 0xE000u;         // mantém o GORF operando (sem rede no teste)
        const int oreNow = op2::g_playerData[0].ore;
        op2::TethysGame::advanceTick();
        op2::tickColony();
        op2::tickMovement();
        op2::tickUnits();
        if (op2::g_playerData[0].ore > oreNow) delivered += op2::g_playerData[0].ore - oreNow;   // antes do recalc
    }
    int rubbleLeft = 0;
    for (int dy = 0; dy < 3; ++dy)
        for (int dx = 0; dx < 3; ++dx)
            if ((op2::g_gameMap.rawCell(sx0 + dx, sy0 + dy) & 0x1F) == 0x16) ++rubbleLeft;
    std::printf("[Salvage] REGIAO: entulho restante %d/9; minerio entregue ao GORF +%d; caminhao carga %d acao 0x%X\n",
                rubbleLeft, delivered, op2::g_units[st.id()].cargo, op2::g_units[st.id()].action);
    renderer.shutdown(); SDL_Quit();
    // (antes devolvia 0 sempre: teste que imprime FALHOU e nao reprova nao
    //  protege nada -- a mesma armadilha do --chattest)
    return wreckOk ? 0 : 1;
}

// --panetest <DLL> <prefix>: carrega a missão, roda 300 ticks e grava o quadro
// com o painel de informações de uma unidade de cada tipo do jogador 0
// (<prefix>_<tipo>.bmp).
// --chattest <DLL>: a linha de conversa do jogo (sub_4115F0) e o pacote 0x30.
// Abre com Enter, digita, manda com Enter e confere que a mensagem entrou no
// registro; depois confere que o Esc desiste e que a mascara de destinatarios
// (as caixas da tela ALLIES/CHAT) cala quem foi desmarcado.
static int runChatTest(const std::string& dll, const char* shot) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 chattest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::setPlayerName(op2::TethysGame::localPlayer(), "Marcelo");

    auto tecla = [&](SDL_Keycode k) {
        SDL_Event e{};
        e.type = SDL_KEYDOWN;
        e.key.keysym.sym = k;
        gs.handleEvent(renderer, e);
    };
    auto escreve = [&](const char* t) {
        SDL_Event e{};
        e.type = SDL_TEXTINPUT;
        std::snprintf(e.text.text, sizeof(e.text.text), "%s", t);
        gs.handleEvent(renderer, e);
    };

    const int antes = op2::messageCount();
    tecla(SDLK_RETURN);
    escreve("bom dia");
    tecla(SDLK_BACKSPACE);
    tecla(SDLK_RETURN);
    // postMessage poe a marca de tempo "<N>%d: " na frente (sub_438F70), entao
    // o que se confere e o fim da linha.
    const std::string ultima = op2::messageCount() ? op2::messageAt(op2::messageCount() - 1).text : "";
    const std::string esperado = "Marcelo: bom di";
    const bool entrou = op2::messageCount() == antes + 1 &&
                        ultima.size() >= esperado.size() &&
                        ultima.compare(ultima.size() - esperado.size(), esperado.size(), esperado) == 0 &&
                        op2::messageAt(op2::messageCount() - 1).x == -2;
    std::printf("[ChatTest] mandar: %s (%s)\n", entrou ? "ok" : "FALHOU",
                ultima.c_str());
    bool tudoOk = entrou;

    // A linha e a porta dos cheats, e e em jogo solo que eles servem: digita o
    // primeiro e ve o estoque encher. (O hash e o efeito tem o seu proprio
    // teste no --cheattest; aqui o que se prova e o CAMINHO: tecla -> linha ->
    // pacote 0x30 -> sub_58642A.)
    const int eu = op2::TethysGame::localPlayer();
    // A missao comeca com a colonia ainda nao contada; o teto do estoque aqui
    // e so o que torna o efeito observavel.
    if (op2::g_colony[eu].oreCapacity <= 0) op2::g_colony[eu].oreCapacity = 9000;
    {
        op2::g_playerData[eu].ore = 0;
        tecla(SDLK_RETURN);
        escreve("Dan's dog could make more ore.");
        tecla(SDLK_RETURN);
        // Em jogo solo o estoque enche; em cenario multijogador (ou em rede) a
        // frase nao faz nada e segue como mensagem comum -- a regra nova, igual
        // a do OPUPatch. O --cheattest cobre o reconhecedor; aqui se prova o
        // CAMINHO inteiro, da tecla ao efeito.
        const bool liberado = op2::cheatsAllowed();
        const int cheio = op2::g_colony[eu].oreCapacity;
        const bool cheatOk = liberado ? (op2::g_playerData[eu].ore == cheio)
                                      : (op2::g_playerData[eu].ore == 0);
        std::printf("[ChatTest] cheat pela linha (%s): %s (minerio %d de %d)\n",
                    liberado ? "solo, tem de encher" : "multijogador, nao pode valer",
                    cheatOk ? "ok" : "FALHOU", op2::g_playerData[eu].ore, cheio);
        tudoOk = tudoOk && cheatOk;
    }

    const int meio = op2::messageCount();
    tecla(SDLK_RETURN);
    escreve("nao vai");
    if (shot) {                                   // a linha aberta, para conferir o desenho
        gs.onIdle(renderer, 0.0f);
        writeBmp32(shot, renderer.getFramebuffer(), renderer.getWidth(),
                   renderer.getHeight());
        std::printf("[ChatTest] tela -> %s\n", shot);
    }
    tecla(SDLK_ESCAPE);
    const bool esc = op2::messageCount() == meio;
    std::printf("[ChatTest] Esc desiste: %s\n", esc ? "ok" : "FALHOU");
    tudoOk = tudoOk && esc;

    // Tirar-me da mascara cala a minha propria copia da linha.
    op2::chatMaskToggle(op2::TethysGame::localPlayer());
    const int depois = op2::messageCount();
    tecla(SDLK_RETURN);
    escreve("silencio");
    tecla(SDLK_RETURN);
    const bool calado = op2::messageCount() == depois;
    std::printf("[ChatTest] fora da mascara: %s\n", calado ? "ok" : "FALHOU");
    tudoOk = tudoOk && calado;

    renderer.shutdown();
    SDL_Quit();
    return tudoOk ? 0 : 1;        // antes devolvia 0 sempre, e a falha passava
}

// --transfertest <DLL>: o TRANSFER (sub_455A80 -> escolhedor -> pacote 0x18).
// O botao so existe com trapaca ou com um Trade Center operando numa partida
// multijogador; aqui a trapaca abre o caminho. Confere que o escolhedor
// aparece, que escolher um jogador troca o dono das unidades e que o CANCEL
// nao mexe em nada.
static int runTransferTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 transfertest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    const int me = op2::TethysGame::localPlayer();

    // Dois veiculos meus para doar.
    std::vector<int> meus;
    for (size_t i = 1; i < op2::g_units.size() && meus.size() < 2; ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || (r.owner & 0x0F) != me || !(r.flags & op2::UNIT_VEHICLE)) continue;
        meus.push_back(static_cast<int>(i));
    }
    if (meus.size() < 2) { std::printf("[Transfer] sem veiculos\n"); return 3; }

    op2::TethysGame::setCheatFlag(0, true);          // dword_56DE58
    gs.setSelection(meus);
    gs.pressCommand(op2::UC_TRANSFER);
    std::printf("[Transfer] escolhedor abriu: %s\n", gs.pickingPlayer() ? "ok" : "FALHOU");

    // Desistir nao muda dono nenhum.
    gs.pickCancel();
    const bool intacto = (op2::g_units[meus[0]].owner & 0x0F) == me && !gs.pickingPlayer();
    std::printf("[Transfer] CANCEL nao mexe: %s\n", intacto ? "ok" : "FALHOU");

    // Doar para o jogador 1 (ou 0, se eu for o 1).
    const int destino = me == 0 ? 1 : 0;
    gs.setSelection(meus);
    gs.pressCommand(op2::UC_TRANSFER);
    gs.pickPlayer(destino);
    int trocados = 0;
    for (int id : meus) if ((op2::g_units[id].owner & 0x0F) == destino) ++trocados;
    std::printf("[Transfer] doados: %d de %d (%s)\n", trocados, static_cast<int>(meus.size()),
                trocados == static_cast<int>(meus.size()) ? "ok" : "FALHOU");
    const std::string ultima = op2::messageCount() ? op2::messageAt(op2::messageCount() - 1).text : "";
    std::printf("[Transfer] aviso: %s\n", ultima.c_str());

    // CORRECAO: transferencia de/para jogador de PC (ou Gaia) nao avisa. O
    // escolhedor da UI so lista humanos, entao o caminho que importa e o dos
    // scripts: U_DoTransfer -> pacote 0x18 -> applyTransfer.
    bool avisoOk = true;
    {
        op2::resetMessages();
        const int serie = op2::messageSerial();
        op2::g_playerData[destino].isHuman = 0;              // vira PC
        op2::applyTransfer(me, {meus[0]}, destino);
        const bool calouPC = op2::messageSerial() == serie;
        op2::g_playerData[destino].isHuman = 1;              // humano de novo
        op2::applyTransfer(me, {meus[1]}, destino);
        const bool avisouHumano = op2::messageSerial() > serie;
        if (!calouPC || !avisouHumano) avisoOk = false;
        std::printf("[Transfer] para PC: calou=%d | para humano: avisou=%d %s\n",
                    calouPC ? 1 : 0, avisouHumano ? 1 : 0, avisoOk ? "ok" : "FALHOU");
    }

    // Item 6 da lista do OPUPatch: no original, transferir unidade para a
    // GAIA (jogador 6, dono dos projeteis e efeitos) trava o jogo. Aqui o
    // caminho e o mesmo dos outros jogadores e os vetores tem kMaxPlayers=7
    // posicoes, entao 6 e indice valido -- o caso existe para travar isso.
    bool gaiaOk = true;
    {
        const int alvo = meus[0];
        op2::applyTransfer(me, {alvo}, 6);
        const int dono = op2::g_units[alvo].owner & 0x0F;
        // E ainda tem de ser possivel seguir simulando sem quebrar.
        for (int t = 0; t < 60; ++t) { op2::TethysGame::advanceTick(); op2::tickColony(); }
        gaiaOk = (dono == 6) && op2::g_units[alvo].id != -1;
        std::printf("[Transfer] para a Gaia: dono=%d (esperado 6) e o jogo seguiu %s\n",
                    dono, gaiaOk ? "ok" : "FALHOU");
    }

    renderer.shutdown();
    SDL_Quit();
    return (avisoOk && gaiaOk) ? 0 : 1;
}
static int runPaneTest(const std::string& dll, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 panetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    for (int k = 0; k < 300; ++k) gs.onIdle(renderer, 0.2501f / 4.0f);
    std::vector<int> done;
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || (r.owner & 0x0F) != 0 || !(r.flags & (op2::UNIT_BUILDING | op2::UNIT_VEHICLE))) continue;
        if (std::find(done.begin(), done.end(), (int)r.type) != done.end()) continue;
        done.push_back(r.type);
        {   // Progresso só p/ a captura (sub_45B850): lab pesquisando, universidade treinando, VF produzindo.
            op2::UnitRecord& w = op2::g_units[i];
            if (w.type == 0x31 && op2::techAt(5)) { w.action = 0x16; w.labTopic = 5; w.field3C = (op2::techAt(5)->edenCost / 3) << 8; }
            if (w.type == 0x26) { w.action = 0x17; w.extraWorkers = 3; w.trainTime = 400; w.field3C = 150; }
            if (w.type == 0x30) { w.action = 9; w.produceType = op2::mapTiger; w.kitWeapon = 0x3F; w.produceTotal = 600; w.field3C = 200; }
            if (w.type == 0x2F) { w.action = 9; w.produceType = 0x31; w.produceTotal = 600; w.field3C = 450; }
        }
        gs.setSelection({(int)i});
        gs.setCamera(op2::unitOX(r) - 200, op2::unitOY(r) - 200);
        gs.onIdle(renderer, 0.0f);
        char fn[256]; std::snprintf(fn, sizeof(fn), "%s_%02X.bmp", prefix.c_str(), r.type);
        writeBmp32(fn, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        std::printf("[Pane] %s -> %s\n", op2::unitTitle(r).c_str(), fn);
        if (r.type == 0x1F) {                                    // &VEHICLES (lista de veículos)
            gs.debugVehicleList(1, false);
            gs.onIdle(renderer, 0.0f);
            char f2[256]; std::snprintf(f2, sizeof(f2), "%s_vlist.bmp", prefix.c_str());
            writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
            gs.debugVehicleList(-1, true);
            gs.onIdle(renderer, 0.0f);
            std::snprintf(f2, sizeof(f2), "%s_vjump.bmp", prefix.c_str());
            writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
            gs.setSelection({});
            gs.debugVehicleList(-1, false);                      // (fecha via STATUS na próxima)
        }
        if (r.type == 0x15 || r.type == 0x16)
            std::printf("[Pane]   mina rend=%d var=%d cargas=%d carga=%d\n", r.mineYield, r.mineVariant, r.mineLink,
                        op2::mineLoadAmount((int)i));
    }
    {   // Aba Communications (dword_566510): mensagens com e sem posição, a mais nova escolhida.
        gs.setSelection({});
        op2::postMessage(-1, -1, "Mensagem sem posicao no log de teste", 0);
        op2::postMessage(-2, -1, "Mensagem de chat (x = -2)", 0);
        op2::postMessage(64 * 32, 40 * 32, "Mensagem com posicao: uma linha bem comprida para testar a quebra", 0);
        gs.debugTab(3, 0);
        gs.onIdle(renderer, 0.0f);
        char f2[256]; std::snprintf(f2, sizeof(f2), "%s_comms.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        std::printf("[Pane] Communications -> %s\n", f2);
        // A tela "Set Up Allies and Chat Partners": precisa de outros
        // jogadores HUMANOS na mesa, entao a captura liga os que houver.
        for (int q = 1; q < op2::TethysGame::noPlayers() && q < op2::kMaxPlayers; ++q)
            op2::g_playerData[q].isHuman = 1;
        gs.debugAlliesChat();
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_allies.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        std::printf("[Pane] Allies/Chat -> %s\n", f2);
        gs.debugNetStats();
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_netstats.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        std::printf("[Pane] Network Statistics -> %s\n", f2);
        gs.debugTab(0, 0);
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_factories.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        gs.debugTab(1, 0);
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_labs0.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        gs.debugLabs(1, 2);
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_labs1.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        gs.debugLabs(2, -1);
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_labs2.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        for (int md = 0; md <= 12; ++md) {
            gs.debugResources(md, md == 0 ? 1 : md >= 7 ? 0 : -1);
            gs.onIdle(renderer, 0.0f);
            std::snprintf(f2, sizeof(f2), "%s_res%02d.bmp", prefix.c_str(), md);
            writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        }
        for (int md = 0; md <= 1; ++md) {
            gs.debugSpace(md);
            gs.onIdle(renderer, 0.0f);
            std::snprintf(f2, sizeof(f2), "%s_space%d.bmp", prefix.c_str(), md);
            writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        }
        gs.debugTab(5);
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_options.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        // As cinco telas da aba: menu, preferencias, fim de missao, manual e
        // um topico do manual.
        static const char* kTelas[5] = {"gameopt", "gameprefs", "gameend", "manual", "manualtxt"};
        for (int q = 1; q < 5; ++q) {
            gs.debugOptionsScreen(q);
            gs.onIdle(renderer, 0.0f);
            char fq[512];
            std::snprintf(fq, sizeof(fq), "%s_%s.bmp", prefix.c_str(), kTelas[q]);
            writeBmp32(fq, renderer.getFramebuffer(), renderer.getWidth(),
                       renderer.getHeight());
        }
        // O fim da lista do PREFERENCES: e la que fica o radio do modo de
        // tela, que nao cabe na primeira pagina.
        gs.debugOptionsScreen(1);
        gs.debugOptScroll(13);
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_gameprefs2.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), op2::Renderer::NATIVE_WIDTH, op2::Renderer::NATIVE_HEIGHT);
        gs.debugOptionsScreen(0);                    // painel "Game Options"
        gs.onIdle(renderer, 0.0f);
        std::snprintf(f2, sizeof(f2), "%s_gameopt.bmp", prefix.c_str());
        writeBmp32(f2, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
    }
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --musictest <clm>: abre o CLM dado, inicia a partida (lista padrão), troca a
// lista como uma missão (SetMusicPlayList) e imprime a sequência de blocos de
// 0x8000 bytes (valor da 1ª amostra de cada bloco).
static int runMusicTest(const std::string& clm) {
    op2::musicSetClmPath(clm.c_str());
    op2::musicStartGame(-1, false);
    std::printf("[Music] tocando=%d (lista padrao)\n", op2::musicPlaying());
    const int ids[4] = {0, 3, 4, 2};                          // EDEN11, EDEN31, EDEN32, EDEN22
    op2::musicSetPlayList(4, 2, ids);
    std::vector<int16_t> buf(0x4000);
    std::printf("[Music] blocos:");
    for (int b = 0; b < 24; ++b) {
        const int n = op2::musicDebugPull(buf.data(), 0x4000);
        std::printf(" %d", n ? buf[0] / 100 : -1);
    }
    std::printf("\n");
    op2::musicEndGame();
    std::printf("[Music] depois do fim: tocando=%d\n", op2::musicPlaying());
    return 0;
}

// --garagetest <map>: CC + Tokamak + Garage; um Laser Lynx danificado na doca é
// guardado (baia 0), consertado (ação 0x1B, custo em minério) e solto de novo.
static int runGarageTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 garagetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);
    int bx = -1, by = -1;
    for (int y = 4; y < H - 10 && bx < 0; ++y)
        for (int x = 4; x < W - 12 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = -1; dy <= 8 && ok; ++dy)
                for (int dx = -2; dx <= 10 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) return 3;
    op2::g_playerData[0].workers = 40; op2::g_playerData[0].scientists = 10;
    op2::g_playerData[0].ore = 3000; op2::g_playerData[0].rareOre = 1000;
    (void)bx; (void)by;
    op2::Unit cc, tok, lynx;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{12, 12}, 0);   // (arranjo do [Rep])
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{15, 12}, 0);
    op2::Unit gar(op2::placeBuilding(0x29, 12, 14, 0));                // garagem encostada
    op2::Unit st1(op2::placeBuilding(0x36, 17, 14, 0));                // smelter comum (capacidade)
    op2::Unit st2(op2::placeBuilding(0x38, 21, 14, 0));                // smelter raro
    op2::colonyMarkDirty(0);
    auto tick = [&](int n) {
        for (int k = 0; k < n; ++k) {
            op2::TethysGame::advanceTick();
            op2::tickColony();
            op2::tickMovement();
            op2::tickUnits();
        }
    };
    tick(20);
    op2::g_playerData[0].ore = 3000; op2::g_playerData[0].rareOre = 1000;
    std::printf("[Garage] armazens operando=%d/%d capacidade %d/%d\n", op2::structureOperational(op2::g_units[st1.id()]),
                op2::structureOperational(op2::g_units[st2.id()]), op2::g_colony[0].oreCapacity, op2::g_colony[0].rareCapacity);
    const int g = gar.id();
    for (int id : {cc.id(), tok.id(), g, st1.id()}) {
        const op2::UnitRecord& b = op2::g_units[id];
        int fw, fh; op2::buildingFootprint(b.type, fw, fh);
        std::printf("[Garage]   pos px (%d,%d) tile (%d,%d) tamanho %dx%d\n", op2::unitOX(b), op2::unitOY(b), b.pixelX >> 5, b.pixelY >> 5, fw, fh);
        std::printf("[Garage]   %s energia=%d trab=%d rede=%d acao=0x%X dano=%d operando=%d\n", op2::unitTitle(b).c_str(),
                    (b.flags & op2::UNIT_POWERED) != 0, (b.flags & op2::UNIT_WORKERS) != 0, b.netConnected, b.action, b.damage,
                    op2::structureOperational(b));
    }
    int dx, dy;
    if (!op2::dockCell(g, dx, dy)) { std::printf("[Garage] sem doca\n"); return 4; }
    op2::TethysGame::CreateUnit(lynx, op2::mapLynx, op2::LOCATION{dx, dy}, 0, 0x3D);
    op2::g_units[lynx.id()].damage = 200;
    const int v = lynx.id();
    std::printf("[Garage] garagem %d operando=%d baias=%d doca (%d,%d) veiculo na doca=%d\n", g,
                op2::structureOperational(op2::g_units[g]), op2::garageBays(op2::g_units[g]), dx, dy, op2::dockedVehicle(g));
    std::printf("[Garage] baia 0 disponivel=%d\n", op2::garageBayAvailable(g, 0));
    const int ore0 = op2::g_playerData[0].ore, rare0 = op2::g_playerData[0].rareOre;
    op2::processBayTransfer(0, g, 0);
    std::printf("[Garage] guardou: baia0=%d sub=0x%X morto=%d acao garagem=0x%X\n", op2::garageVehicle(g, 0),
                op2::g_units[v].field22, (op2::g_units[v].flags & op2::UNIT_DEAD) != 0, op2::g_units[g].action);
    for (int k = 0; k < 8; ++k) {
        tick(50);
        std::printf("[Garage] t+%d: dano=%d sub=0x%X minerio %d->%d raro %d->%d acao=0x%X msg=%s\n", (k + 1) * 50,
                    op2::g_units[v].damage, op2::g_units[v].field22, ore0, op2::g_playerData[0].ore, rare0,
                    op2::g_playerData[0].rareOre, op2::g_units[g].action, op2::statusBarText().c_str());
    }
    std::printf("[Garage] soltar: disponivel=%d\n", op2::garageBayAvailable(g, 0));
    op2::processBayTransfer(0, g, 0);
    tick(70);
    const op2::UnitRecord& r = op2::g_units[v];
    std::printf("[Garage] solto: baia0=%d sub=0x%X morto=%d em (%d,%d) ocupante=%d contagem=%d\n", op2::garageVehicle(g, 0),
                r.field22, (r.flags & op2::UNIT_DEAD) != 0, r.pixelX >> 5, r.pixelY >> 5,
                op2::g_gameMap.cellOccupant(r.pixelX >> 5, r.pixelY >> 5), op2::garageCount(g));
    // Mover p/ a doca (sub_451280 → pacote 3/4): o Lynx sai da doca, um Panther vai até ela.
    op2::uiCommandTarget(op2::UC_MOVE, {v}, dx + 4, dy + 4);
    tick(120);
    op2::Unit pan;
    op2::TethysGame::CreateUnit(pan, op2::mapPanther, op2::LOCATION{dx + 6, dy + 2}, 0, 0x40);
    op2::g_units[pan.id()].damage = 300;
    op2::uiCommandTarget(op2::UC_MOVE, {pan.id()}, dx, dy);
    std::printf("[Garage] panther acao=0x%X (3/4 = doca)\n", op2::g_units[pan.id()].action);
    tick(700);
    const op2::UnitRecord& pr = op2::g_units[pan.id()];
    std::printf("[Garage] panther f20=%d f22=%d ordem=%d passo=%d\n", pr.field20, pr.field22, (int)pr.hasOrder, pr.stepTicks);
    std::printf("[Garage] panther em (%d,%d) rot=0x%X acao=0x%X; na doca=%d\n", pr.pixelX >> 5, pr.pixelY >> 5, pr.rotation,
                pr.action, op2::dockedVehicle(g));
    op2::processBayTransfer(0, g, 2);
    tick(300);
    std::printf("[Garage] panther guardado na baia 2=%d dano=%d\n", op2::garageVehicle(g, 2), op2::g_units[pan.id()].damage);
    // Garagem destruída com o veículo dentro (sub_41C570).
    op2::g_units[g].damage = 3000;
    op2::killUnit(g, 1);
    tick(200);
    std::printf("[Garage] garagem destruida: panther vivo=%d\n",
                op2::g_units[pan.id()].id != -1 && !(op2::g_units[pan.id()].flags & op2::UNIT_DEAD));
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --garagecounttest <DLL>: o item 16 -- veiculo GUARDADO em garagem tem de
// contar. Carrega uma missao (so com ela os gatilhos resolvem o callback),
// limpa o mapa, levanta CC + Tokamak + Garagem e guarda tres veiculos: um Lynx
// com Laser, um ConVec com o kit de Agridome e um Evacuation Transport. Depois
// cobra os SEIS pontos que o original trata com o 0x29 -- se algum ignorar a
// garagem, ele conta zero e o caso falha.
static int runGarageCountTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 garagecounttest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    // Fora as unidades da missao: a conta tem de ser so o que este teste pos.
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);
    op2::g_playerData[0].workers = 1;          // um colono, p/ o gatilho EVAC valer algo
    op2::g_playerData[0].scientists = 0;
    op2::g_playerData[0].kids = 0;
    op2::g_playerData[0].ore = 5000;

    op2::Unit cc, tok;
    op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{12, 12}, 0);
    op2::TethysGame::CreateUnit(tok, op2::mapTokamak, op2::LOCATION{15, 12}, 0);
    op2::Unit gar(op2::placeBuilding(0x29, 12, 14, 0));
    op2::colonyMarkDirty(0);
    auto tick = [&](int n) {
        for (int k = 0; k < n; ++k) {
            op2::TethysGame::advanceTick();
            op2::tickColony();
            op2::tickMovement();
            op2::tickUnits();
        }
    };
    tick(20);
    const int g = gar.id();
    int dx, dy;
    if (!op2::dockCell(g, dx, dy)) { std::printf("[GarCount] sem doca\n"); return 3; }
    std::printf("[GarCount] garagem %d operando=%d baias=%d doca (%d,%d)\n", g,
                op2::structureOperational(op2::g_units[g]), op2::garageBays(op2::g_units[g]), dx, dy);

    // Guarda um veiculo por baia. O slot so e preenchido depois do tempo da
    // porta (vt[0xC4]), entao cada um leva seus tiques.
    const int kit = 0x27;                      // Agridome (o kit que o ConVec leva)
    struct Posto { op2::map_id tipo; int carga; int baia; };
    const Posto postos[] = {
        {op2::mapLynx, 0x3D, 0},                  // Laser Lynx
        {op2::mapConVec, kit, 1},                 // ConVec com o kit de Agridome
        {op2::mapEvacuationTransport, 0, 2},      // transporte de evacuacao
    };
    int guardados = 0;
    for (const Posto& p : postos) {
        op2::Unit v;
        op2::TethysGame::CreateUnit(v, p.tipo, op2::LOCATION{dx, dy}, 0, p.carga);
        if (!op2::processBayTransfer(0, g, p.baia)) {
            std::printf("[GarCount] baia %d recusou o tipo 0x%02X\n", p.baia, (int)p.tipo);
            continue;
        }
        tick(120);
        const int dentro = op2::garageVehicle(g, p.baia);
        std::printf("[GarCount] baia %d: guardado=%d (esperado %d) carga=0x%X\n", p.baia, dentro, v.id(),
                    dentro ? (int)(int16_t)op2::g_units[dentro].cargo : 0);
        if (dentro == v.id()) ++guardados;
    }
    if (guardados != 3) {
        std::printf("[GarCount] FALHOU o arranjo: %d de 3 guardados\n", guardados);
        renderer.shutdown(); SDL_Quit(); return 4;
    }
    // Premissa do teste: guardado = fora da lista de veiculos do jogador.
    const int naLista = (int)op2::colonyVehicles(0).size();
    std::printf("[GarCount] veiculos na lista do jogador=%d (esperado 0, estao todos dentro)\n", naLista);

    struct Caso { const char* nome; int obtido; int esperado; };
    std::vector<Caso> casos;
    auto gatilho = [&](int stub) { return (stub != 0xFF && op2::triggerCheck(stub)) ? 1 : 0; };

    // 1-3. ?hasVehicle (loc_4775BD): seis baias, carga = palavra inteira.
    casos.push_back({"hasVehicle(Lynx, qualquer carga)", op2::playerHasVehicle(0, op2::mapLynx, -1), 1});
    casos.push_back({"hasVehicle(Lynx, Laser 0x3D)",     op2::playerHasVehicle(0, op2::mapLynx, 0x3D), 1});
    casos.push_back({"hasVehicle(Lynx, Microwave 0x3E)", op2::playerHasVehicle(0, op2::mapLynx, 0x3E), 0});
    // 4. ?HasOccupiedBay (loc_475FEE): devolve a CONTAGEM, nao um 0/1.
    casos.push_back({"HasOccupiedBay(garagem) = contagem", op2::unitHasOccupiedBay(g), 3});
    // 5-7. CountTrigger (sub_493AF0, loc_493CC9): tipo + filtro de carga.
    casos.push_back({"CountTrigger Lynx/Laser == 1",
                     gatilho(op2::createCountTrigger(1, 0, 0, op2::mapLynx, 0x3D, 1, 0, "AIProc")), 1});
    casos.push_back({"CountTrigger Lynx/Microwave == 0",
                     gatilho(op2::createCountTrigger(1, 0, 0, op2::mapLynx, 0x3E, 0, 0, "AIProc")), 1});
    casos.push_back({"CountTrigger ConVec/qualquer == 1",
                     gatilho(op2::createCountTrigger(1, 0, 0, op2::mapConVec, -1, 1, 0, "AIProc")), 1});
    // 8. KitTrigger (sub_494280, loc_4942D8): ConVec guardado carregando o kit.
    casos.push_back({"KitTrigger Agridome >= 1",
                     gatilho(op2::createKitTrigger(1, 0, 0, kit, 1, "AIProc")), 1});
    // 9. VehicleCountTrigger (sub_494E70, loc_494EAB): lista + vt[63].
    casos.push_back({"VehicleCountTrigger == 3",
                     gatilho(op2::createVehicleCountTrigger(1, 0, 0, 3, 0, "AIProc")), 1});
    // 10. EvacTrigger (sub_494600, loc_49469F): transporte guardado evacua.
    casos.push_back({"EvacTrigger com 1 colono",
                     gatilho(op2::createEvacTrigger(1, 0, 0, "AIProc")), 1});

    // POR ULTIMO: o objetivo de carga passa a valer pela QUANTIDADE (como no OPUPatch):
    // com a carga parcial, cinco caminhoes podem somar 4900 -- e "ter 5
    // caminhoes de metal" quer dizer cinco CHEIOS. A conta e
    // soma/capacidade, entao 4900 da 4 e nao dispara; completando, dispara.
    {
        const int cap = op2::vehicleSheet(op2::mapCargoTruck, 0)->cargo;
        std::vector<int> frota;
        for (int i = 0; i < 5; ++i) {
            op2::Unit c;
            if (!op2::TethysGame::CreateUnit(c, op2::mapCargoTruck,
                                             op2::LOCATION{3 + i, 3}, 0)) break;
            op2::unitSetTruckCargo(c.id(), 4, i == 0 ? cap - 100 : cap);   // a um falta 100
            frota.push_back(c.id());
        }
        const int t5 = op2::createCountTrigger(1, 0, 0, op2::mapCargoTruck, 4, 5, 0, "AIProc");
        casos.push_back({"frota de 5 caminhoes criada", (int)frota.size(), 5});
        casos.push_back({"objetivo com 4900: NAO dispara", gatilho(t5), 0});
        if (!frota.empty()) op2::unitSetTruckCargo(frota[0], 4, cap);      // completa os 5000
        casos.push_back({"objetivo com 5000: dispara", gatilho(t5), 1});
        for (int id : frota) op2::g_units[id].flags |= op2::UNIT_DEAD;     // some com a frota
    }



    bool ok = true;
    for (const Caso& c : casos) {
        const bool bom = c.obtido == c.esperado;
        if (!bom) ok = false;
        std::printf("[GarCount] %-38s %d (esperado %d) %s\n", c.nome, c.obtido, c.esperado,
                    bom ? "ok" : "FALHOU");
    }
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --alerttest <DLL>: o item 18 -- sub_44DE10. Uma unidade atacada nos ultimos
// 12 ticks ALERTA a vizinhanca: todo veiculo armado do proprio lado a menos de
// 96 px em x e y, parado e sem agressor, fica com o MESMO agressor datado de 25
// ticks atras. Monta um pelotao em volta da vitima com um caso de cada regra
// (inclusive a borda exata dos 96 px) e confere quem foi alertado e quem nao.
static int runAlertTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 alerttest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);
    op2::g_playerData[0].isHuman = 1;
    op2::g_playerData[1].isHuman = 0;

    auto lynx = [&](int tx, int ty, int dono) {
        op2::Unit v;
        op2::TethysGame::CreateUnit(v, op2::mapLynx, op2::LOCATION{tx, ty}, dono, 0x3D);
        return v.id();
    };
    // A vitima e o agressor (longe, so para ter um id valido com dono inimigo).
    const int vitima = lynx(20, 20, 0);
    const int agressor = lynx(30, 30, 1);
    // Vizinhos: 32 px, 64 px em x, 64 px em y -> alertados.
    const int perto    = lynx(21, 20, 0);
    const int meio     = lynx(22, 20, 0);
    const int abaixo   = lynx(20, 22, 0);
    // Borda EXATA dos 96 px (3 tiles): o asm usa "<" estrito -> FORA.
    const int borda    = lynx(23, 20, 0);
    // Inimigo ao lado: a mascara de alianca nao bate -> fora.
    const int inimigo  = lynx(21, 21, 1);
    // Ja tem agressor proprio: nao e sobrescrito.
    const int ocupado  = lynx(20, 19, 0);
    // Sem arma: o bit 0x10 de [+0x44] nao esta la -> fora.
    op2::Unit truck;
    op2::TethysGame::CreateUnit(truck, op2::mapCargoTruck, op2::LOCATION{19, 20}, 0);
    const int caminhao = truck.id();
    for (int id : {vitima, agressor, perto, meio, abaixo, borda, inimigo, ocupado, caminhao})
        if (!id) { std::printf("[Alerta] falhou criar o pelotao\n"); renderer.shutdown(); SDL_Quit(); return 3; }

    const int t = op2::TethysGame::tick();
    op2::g_units[ocupado].lastAttacker = static_cast<int16_t>(vitima);
    op2::g_units[ocupado].lastAttackTick = t;
    op2::g_units[vitima].lastAttacker = static_cast<int16_t>(agressor);
    op2::g_units[vitima].lastAttackTick = t;
    const int refAntes = op2::g_units[agressor].refCount;
    std::printf("[Alerta] vitima px=%d py=%d; borda px=%d (esperado px+96=%d)\n",
                op2::g_units[vitima].pixelX, op2::g_units[vitima].pixelY,
                op2::g_units[borda].pixelX, op2::g_units[vitima].pixelX + 96);

    op2::weaponAutoEngage(vitima);

    struct Caso { const char* nome; int obtido; int esperado; };
    auto alertado = [&](int id) { return op2::g_units[id].lastAttacker == agressor ? 1 : 0; };
    auto datado = [&](int id) {
        return op2::g_units[id].lastAttacker == agressor &&
               op2::g_units[id].lastAttackTick == t - 0x19 ? 1 : 0;
    };
    std::vector<Caso> casos = {
        {"vizinho a 32 px alertado",        alertado(perto), 1},
        {"vizinho a 64 px em x alertado",   alertado(meio), 1},
        {"vizinho a 64 px em y alertado",   alertado(abaixo), 1},
        {"o alerta data de 25 ticks atras", datado(perto), 1},
        {"borda exata de 96 px NAO entra",  alertado(borda), 0},
        {"inimigo ao lado NAO entra",       alertado(inimigo), 0},
        {"caminhao (sem arma) NAO entra",   alertado(caminhao), 0},
        {"quem ja tinha agressor mantem",   op2::g_units[ocupado].lastAttacker == vitima ? 1 : 0, 1},
        // Quatro referencias e o certo: tres dos alertas (inc byte [+0x32] em
        // sub_44DE10) mais uma da ordem de revide que a propria vitima emite.
        {"quatro referencias no agressor",  op2::g_units[agressor].refCount - refAntes, 4},
        // A quarta e da ordem de revide: acao 0x24 com o agressor no alvo da ordem.
        {"a vitima revidou (ordem 0x24 no agressor)",
         op2::g_units[vitima].action == 0x24 &&
         op2::g_units[vitima].orderTarget() == (0xFFFF0000u | static_cast<uint32_t>(agressor)) ? 1 : 0, 1},
    };

    // Dono do PC: o gate [jogador+0x28] (isHuman) corta tudo.
    for (int id : {perto, meio, abaixo}) {
        op2::g_units[id].lastAttacker = 0;
        op2::g_units[id].lastAttackTick = 0;
    }
    op2::g_playerData[0].isHuman = 0;
    op2::g_units[vitima].lastAttacker = static_cast<int16_t>(agressor);
    op2::g_units[vitima].lastAttackTick = op2::TethysGame::tick();
    op2::weaponAutoEngage(vitima);
    casos.push_back({"dono do PC nao alerta ninguem",
                     alertado(perto) + alertado(meio) + alertado(abaixo), 0});

    bool ok = true;
    for (const Caso& c : casos) {
        const bool bom = c.obtido == c.esperado;
        if (!bom) ok = false;
        std::printf("[Alerta] %-38s %d (esperado %d) %s\n", c.nome, c.obtido, c.esperado,
                    bom ? "ok" : "FALHOU");
    }
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --deletetest <DLL>: o item 20 -- sub_42B360 (DeleteWhenEmpty). Vencido o prazo
// do grupo, cada membro leva a ordem de AUTODESTRUICAO (0x14) e, 400 ticks
// depois, quem ainda estiver na lista leva a de SAIR DO MAPA (0x2A). Monta um
// grupo com os tres casos: um Lynx comum (a autodestruicao pega), um Lynx sob
// EMP (sub_40E210 recusa o 0x14, entao sobra para o 0x2A) e um predio (sem o
// bit 1 de [+0x44]: sai do grupo pelo vt[11]).
static int runDeleteTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 deletetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);

    op2::Unit a, b;
    op2::TethysGame::CreateUnit(a, op2::mapLynx, op2::LOCATION{20, 20}, 1, 0x3D);
    op2::TethysGame::CreateUnit(b, op2::mapLynx, op2::LOCATION{22, 20}, 1, 0x3D);
    const int comum = a.id(), sobEmp = b.id();
    const int predio = op2::placeBuilding(0x27, 24, 20, 1);   // Agridome
    if (!comum || !sobEmp || !predio) {
        std::printf("[Delete] falhou montar o grupo\n"); renderer.shutdown(); SDL_Quit(); return 3;
    }
    op2::g_units[sobEmp].flags |= op2::UNIT_EMP;          // o 0x14 vai recusar este

    const int stub = op2::aiCreateGroup(op2::GK_FIGHT, 1);
    if (stub == 0xFF) { std::printf("[Delete] sem grupo\n"); renderer.shutdown(); SDL_Quit(); return 4; }
    for (int id : {comum, sobEmp, predio}) op2::aiTakeUnit(stub, id);

    auto passo = [&](int n) {
        for (int k = 0; k < n; ++k) { op2::TethysGame::advanceTick(); op2::aiGroupsTick(); }
    };
    auto vivo = [&](int id) { return op2::g_units[id].id != -1 && !(op2::g_units[id].flags & op2::UNIT_DEAD) ? 1 : 0; };

    struct Caso { const char* nome; int obtido; int esperado; };
    std::vector<Caso> casos;

    // Sem prazo o grupo nao mexe em ninguem.
    passo(50);
    casos.push_back({"sem prazo: ninguem mexido", vivo(comum) + vivo(sobEmp) + vivo(predio), 3});

    op2::aiSetDeleteWhenEmpty(stub, 1);                   // deleteAt = tick + 10000
    passo(9990);
    casos.push_back({"prazo no futuro: nada ainda", vivo(comum) + vivo(sobEmp) + vivo(predio), 3});
    casos.push_back({"predio ainda no grupo", op2::g_units[predio].groupStub == stub ? 1 : 0, 1});

    // Cruza o prazo: a autodestruicao e emitida para os tres.
    passo(20);
    casos.push_back({"vencido: Lynx comum autodestruido", vivo(comum) ? 0 : 1, 1});
    casos.push_back({"vencido: Lynx sob EMP recusou o 0x14", vivo(sobEmp), 1});
    casos.push_back({"vencido: predio saiu do grupo (vt[11])",
                     op2::g_units[predio].groupStub == 0xFF ? 1 : 0, 1});
    casos.push_back({"vencido: o Lynx sob EMP nao saiu do mapa",
                     op2::g_units[sobEmp].action == 0x2A ? 1 : 0, 0});

    // 400 ticks depois do prazo, quem sobrou na lista sai do mapa.
    passo(400);
    casos.push_back({"+400: o Lynx sob EMP saiu do mapa (0x2A)",
                     op2::g_units[sobEmp].action == 0x2A &&
                     (op2::g_units[sobEmp].flags & op2::UNIT_DEAD) ? 1 : 0, 1});

    bool ok = true;
    for (const Caso& c : casos) {
        const bool bom = c.obtido == c.esperado;
        if (!bom) ok = false;
        std::printf("[Delete] %-42s %d (esperado %d) %s\n", c.nome, c.obtido, c.esperado,
                    bom ? "ok" : "FALHOU");
    }
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --cheattest <DLL>: os cheats digitados na linha de conversa (sub_58642A).
// Confere o hash contra os quatro valores que estao gravados no OUTPOST2.EXE
// (VA 0x5863B8) e depois o efeito de cada um, lido do disassembly. Os controles
// negativos sao as frases com a pontuacao errada: no original a pontuacao faz
// parte do hash, entao elas nao podem fazer nada.
static int runCheatTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 cheattest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();

    struct Caso { const char* nome; long long obtido; long long esperado; };
    std::vector<Caso> casos;

    // --- 1. o hash bate com a tabela do executavel ---------------------------
    casos.push_back({"hash 'dog ... ore.'      = 0xFF27BC04",
                     op2::cheatHash("Dan's dog could make more ore."), 0xFF27BC04LL});
    casos.push_back({"hash 'car ... people.'   = 0x48D60158",
                     op2::cheatHash("Dan's car could hold more people."), 0x48D60158LL});
    casos.push_back({"hash 'bike ... often'    = 0xAC0FF0E5",
                     op2::cheatHash("Dan's bike could backfire more often"), 0xAC0FF0E5LL});
    casos.push_back({"hash da frase extra      = 0x9AC1C27E",
                     op2::cheatHash("Dan's gun could die more people."), 0x9AC1C27ELL});

    auto digita = [&](const char* frase) {
        std::string linha = frase;
        const bool bateu = op2::cheatCheckChat(0, linha);
        return std::make_pair(bateu, linha);
    };

    // --- 1b. o porteiro: cheat so vale em JOGO SOLO --------------------------
    // Como no OPUPatch, em rede ou em cenario multijogador as frases nao fazem
    // nada e seguem como mensagem comum. A resposta e a mesma em todas as
    // maquinas (so olha o tipo de partida), entao o lockstep nao corre risco.
    const bool liberado = op2::cheatsAllowed();
    casos.push_back({"cheats liberados nesta partida", liberado ? 1 : 0,
                     op2::multiplayerMission() ? 0 : 1});

    if (!liberado) {
        // Cenario multijogador: as quatro frases tem de passar batido.
        op2::g_colony[0].oreCapacity = 9000;
        op2::g_playerData[0].ore = 10;
        op2::g_playerData[0].workers = 100;
        static const char* kFrases[4] = {
            "Dan's dog could make more ore.",
            "Dan's car could hold more people.",
            "Dan's bike could backfire more often",
            "Dan's gun could die more people.",
        };
        int bateu = 0, mudouTexto = 0;
        for (const char* f : kFrases) {
            auto r = digita(f);
            if (r.first) ++bateu;
            if (r.second != f) ++mudouTexto;
        }
        casos.push_back({"multijogador: nenhuma frase e reconhecida", bateu, 0});
        casos.push_back({"multijogador: o texto do chat NAO muda", mudouTexto, 0});
        casos.push_back({"multijogador: o estoque fica onde estava", op2::g_playerData[0].ore, 10});
        casos.push_back({"multijogador: os trabalhadores ficam", op2::g_playerData[0].workers, 100});

        bool okMp = true;
        for (const Caso& c : casos) {
            const bool bom = c.obtido == c.esperado;
            if (!bom) okMp = false;
            std::printf("[Cheat] %-40s %lld (esperado %lld) %s%c", c.nome, c.obtido, c.esperado,
                        bom ? "ok" : "FALHOU", 10);
        }
        std::fflush(stdout);
        renderer.shutdown(); SDL_Quit();
        return okMp ? 0 : 1;
    }

    // --- 2. cheat 0: estoques vao para a capacidade --------------------------
    op2::g_colony[0].oreCapacity = 9000;
    op2::g_colony[0].rareCapacity = 4000;
    op2::g_playerData[0].ore = 10;
    op2::g_playerData[0].rareOre = 20;
    auto r0 = digita("Dan's dog could make more ore.");
    casos.push_back({"cheat 0: minerio comum = capacidade", op2::g_playerData[0].ore, 9000});
    casos.push_back({"cheat 0: minerio raro  = capacidade", op2::g_playerData[0].rareOre, 4000});
    casos.push_back({"cheat 0: o chat virou 'Woof Woof!'", r0.second == "Woof Woof!" ? 1 : 0, 1});

    // --- 3. cheat 1: trabalhadores e cientistas +25% -------------------------
    op2::g_playerData[0].workers = 100;
    op2::g_playerData[0].scientists = 41;      // 41 + 41/4 = 51 (divisao truncada)
    digita("Dan's car could hold more people.");
    casos.push_back({"cheat 1: trabalhadores 100 -> 125", op2::g_playerData[0].workers, 125});
    casos.push_back({"cheat 1: cientistas 41 -> 51", op2::g_playerData[0].scientists, 51});

    // --- 4. cheat 2: recarga = min(v-2, 20) nas armas 0x3B..0x4A ------------
    auto bloco = [&](int t, int off) {
        int v = 0;
        if (const uint8_t* b = op2::unitStatBlock(t, 0)) std::memcpy(&v, b + off, 4);
        return v;
    };
    auto poeBloco = [&](int t, int off, int v) {
        if (uint8_t* b = op2::unitStatBlock(t, 0)) std::memcpy(b + off, &v, 4);
    };
    poeBloco(0x3D, 0x30, 100);                 // recarga alta: o teto de 20 tem de pegar
    poeBloco(0x3B, 0x30, 15);                  // recarga baixa: cai 2, sem teto
    digita("Dan's bike could backfire more often");
    casos.push_back({"cheat 2: recarga 100 -> 20 (teto)", bloco(0x3D, 0x30), 20});
    casos.push_back({"cheat 2: recarga 15 -> 13 (sem teto)", bloco(0x3B, 0x30), 13});

    // --- 5. cheat 3 (frase do porte): penetracao = max(v*33/32, 500) --------
    poeBloco(0x3D, 0x2C, 64);                  // 64*33/32 = 66 -> piso de 500 vence
    poeBloco(0x4A, 0x2C, 1024);                // 1024*33/32 = 1056 -> passa do piso
    poeBloco(0x3C, 0x2C, 77);                  // EMP: FORA da faixa, nao pode mudar
    digita("Dan's gun could die more people.");
    casos.push_back({"cheat 3: penetracao 64 -> 500 (piso)", bloco(0x3D, 0x2C), 500});
    casos.push_back({"cheat 3: penetracao 1024 -> 1056", bloco(0x4A, 0x2C), 1056});
    casos.push_back({"cheat 3: EMP (0x3C) intocada", bloco(0x3C, 0x2C), 77});

    // --- 6. controles negativos: pontuacao errada nao faz nada --------------
    op2::g_playerData[0].ore = 7;
    auto n1 = digita("Dan's dog could make more ore");          // falta o ponto
    casos.push_back({"sem ponto: nao e cheat", n1.first ? 1 : 0, 0});
    casos.push_back({"sem ponto: estoque intacto", op2::g_playerData[0].ore, 7});
    casos.push_back({"sem ponto: chat preservado",
                     n1.second == "Dan's dog could make more ore" ? 1 : 0, 1});
    auto n2 = digita("Dan's bike could backfire more often.");  // ponto a mais
    casos.push_back({"ponto a mais: nao e cheat", n2.first ? 1 : 0, 0});
    auto n3 = digita("ola pessoal");
    casos.push_back({"conversa normal: nao e cheat", n3.first ? 1 : 0, 0});

    bool ok = true;
    for (const Caso& c : casos) {
        const bool bom = c.obtido == c.esperado;
        if (!bom) ok = false;
        std::printf("[Cheat] %-40s %lld (esperado %lld) %s\n", c.nome, c.obtido, c.esperado,
                    bom ? "ok" : "FALHOU");
    }
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --bannertest <DLL>: o letreiro de fim de missao ("MISSION ACCOMPLISHED",
// "MISSION FAILED"). O porte o escrevia com a fontinha de depuracao, AMARELO e
// pequeno; o original (sub_4084D0/sub_4085C0) usa Arial negrito de corpo 48,
// VERMELHO, com contorno preto nos quatro cantos diagonais de +-2 px.
//
// A medicao e por DIFERENCA de quadros: desenha um quadro sem o letreiro,
// guarda, poe o codigo de fim e desenha outro. So os pixels que MUDARAM sao o
// letreiro -- assim nem o terreno nem o painel entram na conta.
static int runBannerTest(const std::string& dll, const std::string& shot) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 bannertest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();

    // O primeiro quadro e quem acerta o tamanho (o fitToWindow do onIdle):
    // medir antes dele daria 640x480 e a varredura cairia no lugar errado.
    gs.onIdle(renderer, 0.0f);
    const int W = renderer.getWidth(), H = renderer.getHeight();
    int vx = 0, vy = 0, vw = 0, vh = 0;
    gs.debugViewRect(vx, vy, vw, vh);

    struct Caixa { int l = 1 << 20, t = 1 << 20, r = -1, b = -1; int n = 0;
        void add(int x, int y) { if (x < l) l = x; if (x > r) r = x;
                                 if (y < t) t = y; if (y > b) b = y; ++n; }
        int w() const { return n ? r - l + 1 : 0; }
        int h() const { return n ? b - t + 1 : 0; }
    };

    std::vector<uint32_t> base(static_cast<size_t>(W) * H);
    // dt = 0: nenhum turno avanca, entao dois quadros seguidos sao iguais --
    // e o que torna a diferenca confiavel.
    gs.onIdle(renderer, 0.0f);
    std::memcpy(base.data(), renderer.getFramebuffer(), base.size() * sizeof(uint32_t));
    gs.onIdle(renderer, 0.0f);
    int instaveis = 0;
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] != renderer.getFramebuffer()[i]) ++instaveis;

    struct Caso { std::string nome; long long obtido; long long esperado; };
    std::vector<Caso> casos;
    casos.push_back({"dois quadros parados sao iguais (premissa)", instaveis, 0});

    Caixa anterior;
    for (int code : {3, 4}) {
        op2::debugSetGameEnd(code);
        gs.onIdle(renderer, 0.0f);
        const uint32_t* fb = renderer.getFramebuffer();
        Caixa vermelho, preto, outros;
        int amarelo = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                if (fb[i] == base[i]) continue;              // nao mexeu: nao e o letreiro
                if (fb[i] == 0xFFFF0000u) vermelho.add(x, y);
                else if (fb[i] == 0xFF000000u) preto.add(x, y);
                else { outros.add(x, y); if (fb[i] == 0xFFFFE070u) ++amarelo; }
            }
        const char* txt = op2::gameEndText(code);
        std::printf("[Banner] codigo %d \"%s\": vermelho %dx%d em (%d,%d) %d px; "
                    "preto %dx%d em (%d,%d); outras cores %d px\n",
                    code, txt, vermelho.w(), vermelho.h(), vermelho.l, vermelho.t, vermelho.n,
                    preto.w(), preto.h(), preto.l, preto.t, outros.n);

        const std::string tag = std::string("codigo ") + std::to_string(code) + ": ";
        casos.push_back({tag + "o letreiro e desenhado em VERMELHO", vermelho.n > 0 ? 1 : 0, 1});
        // Arial negrito de corpo 48: as maiusculas passam de 30 px de altura.
        // A fontinha do porte dava 10 -- e e isso que este caso pega.
        casos.push_back({tag + "... com letras grandes (altura >= 30 px)",
                         vermelho.h() >= 30 ? 1 : 0, 1});
        // O contorno preto: a caixa preta e a vermelha dilatada em 2 px de cada
        // lado, que e exatamente o que as quatro passadas de +-2 px produzem.
        casos.push_back({tag + "... e o contorno preto 2 px em volta",
                         (preto.n > 0 && preto.l == vermelho.l - 2 && preto.t == vermelho.t - 2 &&
                          preto.r == vermelho.r + 2 && preto.b == vermelho.b + 2) ? 1 : 0, 1});
        casos.push_back({tag + "nada do amarelo antigo", amarelo, 0});
        // Centrado na vista, como o DT_CENTER num retangulo que e a superficie
        // inteira e a centragem vertical pela altura medida.
        casos.push_back({tag + "centrado na horizontal",
                         std::abs((vermelho.l + vermelho.r) / 2 - (vx + vw / 2)) <= 3 ? 1 : 0, 1});
        // Na vertical quem e centrado e a CELULA da fonte (o DT_CALCRECT do
        // original devolve a altura da linha), e as maiusculas ocupam so uma
        // parte dela -- por isso a folga de meia linha.
        casos.push_back({tag + "centrado na vertical",
                         std::abs((vermelho.t + vermelho.b) / 2 - (vy + vh / 2)) <=
                             op2::UiFont::lineHeight(op2::UiFont::kBanner) / 2 ? 1 : 0, 1});
        if (!shot.empty() && code == 3 && writeBmp32(shot, fb, W, H))
            std::printf("[Banner] quadro salvo em %s\n", shot.c_str());
        if (code == 4)
            casos.push_back({"os dois codigos dao textos diferentes",
                             vermelho.w() != anterior.w() ? 1 : 0, 1});
        anterior = vermelho;
    }

    int ruins = 0;
    for (const Caso& c : casos) {
        const bool ok = c.obtido == c.esperado;
        if (!ok) ++ruins;
        std::printf("[Banner] %-52s %lld (esperado %lld) %s\n", c.nome.c_str(), c.obtido,
                    c.esperado, ok ? "ok" : "FALHOU");
    }
    std::printf("[Banner] %d casos, %d falhas\n", (int)casos.size(), ruins);
    renderer.shutdown();
    SDL_Quit();
    return ruins ? 1 : 0;
}

// --pausetest <DLL>: o Ctrl+P (comando 40001 do acelerador 130). O porte nao
// tinha pausa nenhuma: a tecla nao fazia nada e o letreiro "Game Paused" do
// controle TFrame+0x1F4 nunca aparecia.
static int runPauseTest(const std::string& dll, const std::string& shot) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 pausetest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }

    struct Caso { std::string nome; long long obtido; long long esperado; };
    std::vector<Caso> casos;
    const bool rede = op2::multiplayerMission();

    // Antes do primeiro tique (dword_56DEDC == 0) a tecla nao faz nada.
    casos.push_back({"antes do 1o tique o relogio esta em zero", op2::TethysGame::tick(), 0});
    gs.debugCtrlP(renderer);
    casos.push_back({"... e o Ctrl+P nao pausa", op2::gTApp.paused() ? 1 : 0, 0});

    gs.onActivate();
    // O tamanho do quadro so vale depois do PRIMEIRO onIdle: e o fitToWindow
    // que o acerta, e antes dele o quadro ainda e o de 640x480.
    gs.onIdle(renderer, 0.0f);
    const int W = renderer.getWidth(), H = renderer.getHeight();
    std::vector<uint32_t> antes(static_cast<size_t>(W) * H);

    // O relogio anda: sem isto os casos abaixo nao mediriam nada.
    const int t0 = op2::TethysGame::tick();
    for (int k = 0; k < 20; ++k) gs.onIdle(renderer, 0.2f);
    const int t1 = op2::TethysGame::tick();
    casos.push_back({"o relogio anda com o jogo solto", t1 > t0 ? 1 : 0, 1});

    // Um arrasto no ar: o ReleaseCapture do OnPauseGame tem de acabar com ele.
    gs.debugAreaDrag(op2::UC_WALL);
    casos.push_back({"ha um arrasto no ar (premissa)", gs.debugAnyDrag() ? 1 : 0, 1});

    gs.onIdle(renderer, 0.0f);
    std::memcpy(antes.data(), renderer.getFramebuffer(), antes.size() * sizeof(uint32_t));

    gs.debugCtrlP(renderer);
    casos.push_back({rede ? "em rede o Ctrl+P nao pausa" : "o Ctrl+P pausa",
                     op2::gTApp.paused() ? 1 : 0, rede ? 0 : 1});
    casos.push_back({"... e empilha uma pausa so", gs.debugPauseCount(), rede ? 0 : 1});
    if (rede) {
        // O aviso vai para a barra de estado, em maiusculas (sub_49BF10).
        casos.push_back({"em rede sai o aviso na barra",
                         op2::statusBarText() == "CANNOT PAUSE IN MULTIPLAYER GAMES" ? 1 : 0, 1});
    } else {
        casos.push_back({"... o arrasto foi solto (ReleaseCapture)", gs.debugAnyDrag() ? 1 : 0, 0});
        casos.push_back({"... o som foi suspenso (sub_47E6F0)", op2::soundSuspendCount(), 1});
    }

    // O relogio PARA. E com dt generoso: se a pausa so zerasse o acumulador no
    // quadro, um dt grande ainda passaria.
    const int t2 = op2::TethysGame::tick();
    for (int k = 0; k < 10; ++k) gs.onIdle(renderer, 5.0f);
    const int t3 = op2::TethysGame::tick();
    casos.push_back({rede ? "em rede o relogio continua andando" : "o relogio para na pausa",
                     t3 == t2 ? 1 : 0, rede ? 0 : 1});

    // O letreiro, medido por diferenca de quadros como no --bannertest.
    {
        gs.onIdle(renderer, 0.0f);
        const uint32_t* fb = renderer.getFramebuffer();
        int vermelho = 0, preto = 0, l = 1 << 20, r = -1, t = 1 << 20, b = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                if (fb[i] == antes[i]) continue;
                if (fb[i] == 0xFFFF0000u) { ++vermelho;
                    if (x < l) l = x; if (x > r) r = x; if (y < t) t = y; if (y > b) b = y; }
                else if (fb[i] == 0xFF000000u) ++preto;
            }
        if (!shot.empty() && writeBmp32(shot, fb, W, H))
            std::printf("[Pause] quadro salvo em %s\n", shot.c_str());
        std::printf("[Pause] letreiro: vermelho %d px, caixa %dx%d; preto %d px\n",
                    vermelho, r >= l ? r - l + 1 : 0, b >= t ? b - t + 1 : 0, preto);
        casos.push_back({rede ? "em rede nao ha letreiro" : "o letreiro \"Game Paused\" aparece",
                         vermelho > 0 ? 1 : 0, rede ? 0 : 1});
        if (!rede) {
            casos.push_back({"... grande, como o de fim de missao", (b - t + 1) >= 30 ? 1 : 0, 1});
            casos.push_back({"... com o contorno preto", preto > 0 ? 1 : 0, 1});
            // O texto da pausa e MAIOR que "MISSION FAILED": se o letreiro
            // estivesse pegando o texto errado, a largura entregaria.
            casos.push_back({"... e e o texto da pausa, nao o do fim",
                             (r - l + 1) > 600 ? 1 : 0, 1});
        }
    }

    if (!rede) {
        // O CONTADOR ([+0x1E0]): o jogo tambem se pausa por outros motivos,
        // entao a pausa empilha e so a subida de 0 para 1 e a descida de volta
        // a 0 e que agem -- o som nao pode ser suspenso duas vezes, e uma
        // despausa so nao basta para soltar o jogo.
        gs.debugPauseGame(true);
        casos.push_back({"uma segunda pausa empilha", gs.debugPauseCount(), 2});
        casos.push_back({"... sem suspender o som de novo", op2::soundSuspendCount(), 1});
        gs.debugPauseGame(false);
        casos.push_back({"uma despausa so nao solta o jogo", op2::gTApp.paused() ? 1 : 0, 1});
        casos.push_back({"... e a pilha desce para uma", gs.debugPauseCount(), 1});

        // De volta: o Ctrl+P despausa e o relogio anda de novo -- sem disparar.
        const int t4 = op2::TethysGame::tick();
        gs.debugCtrlP(renderer);
        casos.push_back({"o Ctrl+P de novo solta o jogo", op2::gTApp.paused() ? 1 : 0, 0});
        casos.push_back({"... e a pilha volta a zero", gs.debugPauseCount(), 0});
        casos.push_back({"... o som volta", op2::soundSuspendCount(), 0});
        gs.onIdle(renderer, 0.0f);
        casos.push_back({"... sem disparar os tiques acumulados",
                         op2::TethysGame::tick() - t4, 0});
        const int t5 = op2::TethysGame::tick();
        for (int k = 0; k < 20; ++k) gs.onIdle(renderer, 0.2f);
        casos.push_back({"... e o relogio anda outra vez",
                         op2::TethysGame::tick() > t5 ? 1 : 0, 1});

        // O fim de missao tem preferencia sobre a pausa no mesmo controle.
        gs.debugCtrlP(renderer);
        op2::debugSetGameEnd(4);                      // "MISSION FAILED"
        gs.onIdle(renderer, 0.0f);
        // So DENTRO da vista: o painel da direita tem vermelho proprio, que
        // alargaria a caixa.
        int vx = 0, vy = 0, vw = 0, vh = 0;
        gs.debugViewRect(vx, vy, vw, vh);
        const uint32_t* fb = renderer.getFramebuffer();
        int l = 1 << 20, r = -1, n = 0;
        for (int y = vy; y < vy + vh && y < H; ++y)
            for (int x = vx; x < vx + vw && x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                if (fb[i] == 0xFFFF0000u) { ++n; if (x < l) l = x; if (x > r) r = x; }
            }
        std::printf("[Pause] com fim de missao: vermelho %d px, largura %d\n",
                    n, r >= l ? r - l + 1 : 0);
        // Tem de HAVER letreiro, e ser o CURTO: sem a primeira metade, uma tela
        // sem letreiro nenhum passaria.
        casos.push_back({"pausado e com fim de missao, o letreiro e o do fim",
                         (n > 1000 && (r - l + 1) < 600) ? 1 : 0, 1});
    }

    int ruins = 0;
    for (const Caso& c : casos) {
        const bool ok = c.obtido == c.esperado;
        if (!ok) ++ruins;
        std::printf("[Pause] %-52s %lld (esperado %lld) %s\n", c.nome.c_str(), c.obtido,
                    c.esperado, ok ? "ok" : "FALHOU");
    }
    std::printf("[Pause] %d casos, %d falhas\n", (int)casos.size(), ruins);
    renderer.shutdown();
    SDL_Quit();
    return ruins ? 1 : 0;
}

// --optmenutest <DLL>: a aba Options de ponta a ponta. Dois bugs moravam aqui:
//   o L&ESS e o &MORE do PREFERENCES tinham o id de OUTRO comando (kCmdLabsCurrent
//   e kCmdResStructures), cujo tratador vem antes e sai com `return`; e a tela de
//   fim de missao abria a confirmacao, que se fechava sozinha no quadro seguinte
//   por falta de selecao -- nenhum dos itens fazia nada.
static int runOptMenuTest(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 optmenutest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();

    const bool rede = op2::multiplayerMission();
    std::printf("[opt] %s: %s\n", dll.c_str(), rede ? "multijogador" : "jogo solo");

    struct Caso { std::string nome; long long obtido; long long esperado; };
    std::vector<Caso> casos;
    auto texto = [&](const char* nome, const std::string& a, const std::string& b) {
        casos.push_back({nome, a == b ? 1 : 0, 1});
        if (a != b) std::printf("[opt]   \"%s\" != \"%s\"\n", a.c_str(), b.c_str());
    };

    // ---- PREFERENCES: os botoes L&ESS e &MORE ---------------------------
    gs.debugTab(5);
    gs.debugOptionsScreen(1);                        // PREFERENCES
    texto("o titulo do PREFERENCES", gs.debugOptTitle(), "Game Preferences");
    gs.debugOptSelect(2);                            // "Game Speed"
    // O valor vem do INI do usuario, que pode estar no TETO (10) ou no piso --
    // e ai o &MORE (ou o L&ESS) nao tem para onde ir e o caso reprova sem ter
    // defeito nenhum. Leva ao piso e sobe um: dai os dois botoes tem folga,
    // venha o INI como vier.
    for (int k = 0; k < 12; ++k) gs.debugOptClick("L&ESS");
    gs.debugOptClick("&MORE");
    const std::string antes = gs.debugOptItem(2);
    casos.push_back({"o &MORE existe e responde", gs.debugOptClick("&MORE") ? 1 : 0, 1});
    casos.push_back({"... e o valor do item mudou", gs.debugOptItem(2) != antes ? 1 : 0, 1});
    casos.push_back({"o L&ESS responde", gs.debugOptClick("L&ESS") ? 1 : 0, 1});
    texto("... e devolve o valor", gs.debugOptItem(2), antes);
    // De novo, agora para BAIXO: sem isto, com os dois botoes mortos o caso de
    // cima passaria sozinho -- nada mudou, logo "voltou ao valor de antes".
    gs.debugOptClick("L&ESS");
    casos.push_back({"o L&ESS sozinho tambem mexe no valor",
                     gs.debugOptItem(2) != antes ? 1 : 0, 1});
    gs.debugOptClick("&MORE");
    // Controle: em item de liga/desliga o original nao monta L&ESS/&MORE.
    gs.debugOptSelect(5);                            // "Play Music"
    casos.push_back({"controle: alternancia nao tem &MORE",
                     gs.debugOptClick("&MORE") ? 1 : 0, 0});

    // As dicas dos dois botoes sao POR ITEM (tabela de 7 dwords em 0x4E3118),
    // e nao uma frase generica: a do item escolhido diz de QUE opcao se trata.
    static const char* kDicas[5][2] = {
        {"Turn music volume down (L&ess)",         "Turn music volume up (&More)"},
        {"Turn sound effects volume down (L&ess)", "Turn sound effects volume up (&More)"},
        {"Turn game speed down (L&ess)",           "Turn game speed up (&More)"},
        {"Turn scroll speed down (L&ess)",         "Turn scroll speed up (&More)"},
        {"Turn detail level down (L&ess)",         "Turn detail level up (&More)"},
    };
    for (int i = 0; i < 5; ++i) {
        gs.debugOptSelect(i);
        char nome[96];
        std::snprintf(nome, sizeof(nome), "a dica do L&ESS do item %d", i);
        texto(nome, gs.debugOptTip("L&ESS"), kDicas[i][0]);
        std::snprintf(nome, sizeof(nome), "a dica do &MORE do item %d", i);
        texto(nome, gs.debugOptTip("&MORE"), kDicas[i][1]);
    }

    // As setas de rolagem: os botoes sao desenhados por cima do pe da lista,
    // entao a lista TEM de recuar -- senao a seta nasce debaixo do botao e o
    // fim da lista (onde mora o radio, e antes dele o "Skip Frames") fica
    // inalcancavel.
    gs.debugOptSelect(0);                            // item com L&ESS/&MORE: 3 botoes
    casos.push_back({"as setas nao ficam debaixo dos botoes",
                     gs.debugOptArrowsClear() ? 1 : 0, 1});
    gs.debugOptScroll(0);
    const int linhas = gs.debugOptLines(), cabem = gs.debugOptRows();
    casos.push_back({"a lista nao cabe inteira (senao o resto nao mede nada)",
                     linhas > cabem ? 1 : 0, 1});
    int topo = 0, voltas = 0;
    while (topo + cabem < linhas && voltas < 10) { topo = gs.debugOptPageDown(); ++voltas; }
    casos.push_back({"clicando na seta chega-se a ultima linha",
                     topo + cabem >= linhas ? 1 : 0, 1});
    casos.push_back({"... e foi a seta que rolou, nao o acaso", topo > 0 ? 1 : 0, 1});
    // O clique vai ao painel ANTES dos botoes: com a lista chegando ate la, ela
    // comia o clique do L&ESS e o botao nao respondia.
    gs.debugOptSelect(0);
    casos.push_back({"a lista nao come o clique do L&ESS",
                     gs.debugOptPaneEats("L&ESS") ? 1 : 0, 0});
    casos.push_back({"nem o do &APPLY", gs.debugOptPaneEats("&APPLY") ? 1 : 0, 0});
    // De novo com o HUD do tamanho da JANELA (o quadro do fork redimensiona):
    // a conta do recuo tem de valer em qualquer resolucao, nao so em 640x480.
    gs.onIdle(renderer, 0.0f);
    casos.push_back({"o quadro cresceu mesmo (senao o caso abaixo nao mede nada)",
                     renderer.getHeight() > 480 ? 1 : 0, 1});
    casos.push_back({"... e as setas seguem livres no tamanho da janela",
                     gs.debugOptArrowsClear() ? 1 : 0, 1});
    gs.debugOptScroll(0);

    // ---- extra do porte: o radio "Screen Mode" --------------------------
    // A janela deste teste e JANELA (init com fullscreen=false). Entrar no
    // PREFERENCES tem de acertar o radio com ELA, e nao com o INI -- o
    // Alt+Enter troca a tela sem gravar nada no arquivo.
    gs.onIdle(renderer, 0.0f);                       // o quadro de verdade
    casos.push_back({"o radio nasce no modo da janela de verdade",
                     gs.debugWantFullscreen() ? 1 : 0, 0});
    const int kCheia = op2::GameOptionsPane::OPT_SCREEN_FULL;
    const int kJanela = op2::GameOptionsPane::OPT_SCREEN_WINDOW;
    texto("a 1a linha do radio", gs.debugOptItem(kCheia),
          "Screen Mode\n\t( ) Full Screen");
    texto("a 2a linha do radio", gs.debugOptItem(kJanela), "\t(o) Window");
    gs.debugOptRow(kCheia);                          // clique em "Full Screen"
    casos.push_back({"clicar em Full Screen escolhe tela cheia",
                     gs.debugWantFullscreen() ? 1 : 0, 1});
    texto("... e a bolinha troca de linha", gs.debugOptItem(kCheia),
          "Screen Mode\n\t(o) Full Screen");
    texto("... saindo da outra", gs.debugOptItem(kJanela), "\t( ) Window");
    gs.debugOptRow(kJanela);                         // clique em "Window"
    casos.push_back({"clicar em Window escolhe janela",
                     gs.debugWantFullscreen() ? 1 : 0, 0});
    // Radio, nao alternancia: bater de novo na MESMA linha nao inverte nada.
    gs.debugOptRow(kJanela);
    casos.push_back({"clicar duas vezes na mesma linha nao inverte",
                     gs.debugWantFullscreen() ? 1 : 0, 0});

    // O clique nao mexe na janela nem no INI: quem faz isso e o &APPLY, como
    // em todo o resto desta tela. Guardo o INI em bytes e devolvo no fim.
    std::string iniAntes;
    {
        std::ifstream f("OUTPOST2.INI", std::ios::binary);
        if (f) iniAntes.assign(std::istreambuf_iterator<char>(f),
                               std::istreambuf_iterator<char>());
    }
    {   // o arquivo no valor OPOSTO do que o APPLY vai gravar: sem isto o caso
        // de baixo passa de graca quando o INI do usuario ja tem Fullscreen=1.
        op2::Config pre("OUTPOST2.INI");
        pre.load();
        pre.setInt("Game", "Fullscreen", 0);
        pre.save();
    }
    gs.debugOptRow(kCheia);
    gs.onIdle(renderer, 0.0f);
    casos.push_back({"o clique sozinho NAO troca a tela",
                     renderer.fullscreen() ? 1 : 0, 0});
    gs.debugOptClick("&APPLY");
    gs.onIdle(renderer, 0.0f);
    casos.push_back({"o &APPLY troca a tela", renderer.fullscreen() ? 1 : 0, 1});
    {
        op2::Config lido("OUTPOST2.INI");
        casos.push_back({"... e grava Fullscreen=1 no INI",
                         lido.load() ? lido.getInt("Game", "Fullscreen", -1) : -1, 1});
    }
    {   // idem, agora ao contrario, para o caso de baixo tambem medir algo.
        op2::Config pre("OUTPOST2.INI");
        pre.load();
        pre.setInt("Game", "Fullscreen", 1);
        pre.save();
    }
    gs.debugOptRow(kJanela);
    gs.debugOptClick("&APPLY");
    gs.onIdle(renderer, 0.0f);
    casos.push_back({"o &APPLY devolve a janela", renderer.fullscreen() ? 1 : 0, 0});
    {
        op2::Config lido("OUTPOST2.INI");
        casos.push_back({"... e grava Fullscreen=0 no INI",
                         lido.load() ? lido.getInt("Game", "Fullscreen", -1) : -1, 0});
    }
    if (!iniAntes.empty()) {                         // o INI como estava, byte a byte
        std::ofstream f("OUTPOST2.INI", std::ios::binary | std::ios::trunc);
        f.write(iniAntes.data(), static_cast<std::streamsize>(iniAntes.size()));
    }
    renderer.setFullscreen(false);
    gs.debugOptionsScreen(1);                        // volta ao PREFERENCES limpo

    // ---- o ESC dentro da partida ----------------------------------------
    // Ele largava a partida na HORA, sem perguntar. Agora desiste do que
    // estiver no ar e, se nao houver nada, pergunta -- com a frase do proprio
    // executavel e pelo caminho do "&BACK TO MAIN MENU".
    {
        gs.debugOptionsScreen(0);
        gs.debugTab(0);
        gs.debugEscape(renderer);
        const bool pergunta = gs.debugConfirmOpen();
        casos.push_back({"o ESC leva para a aba Options", gs.debugTabNow(), 5});
        texto("... na tela de fim de missao", gs.debugOptTitle(),
              rede ? "Game End: Multiplayer" : "Game End: Single Player");
        // Em multijogador nao existe "voltar ao menu" (a tela de fim e outra,
        // so com autodestruir e transferir): ali o ESC so abre a tela.
        casos.push_back({rede ? "em rede o ESC nao pergunta" : "em jogo solo o ESC pergunta",
                         pergunta ? 1 : 0, rede ? 0 : 1});
        texto("a pergunta e a do executavel", gs.debugConfirmText(),
              rede ? std::string() : std::string("Are you sure you want to abort the mission?"));
        if (!rede) {
            // A pergunta tem de sobreviver ao quadro seguinte, como a dos itens.
            gs.debugPaneFrame();
            casos.push_back({"... e continua no ar no quadro seguinte",
                             gs.debugConfirmOpen() ? 1 : 0, 1});
            const std::vector<std::string> yn = gs.debugOptLabels();
            casos.push_back({"... com os botoes &YES/&NO",
                             (yn.size() == 2 && yn[0] == "&YES" && yn[1] == "&NO") ? 1 : 0, 1});
            // Com a pergunta no ar, o ESC e um "NAO".
            gs.debugEscape(renderer);
            casos.push_back({"o ESC de novo fecha a pergunta",
                             gs.debugConfirmOpen() ? 1 : 0, 0});
        }
        // Com uma mira em curso, o ESC desiste DELA e nao pergunta nada.
        gs.debugArmCommand(op2::UC_MOVE);
        gs.debugEscape(renderer);
        casos.push_back({"com mira em curso, o ESC so desiste dela",
                         (gs.debugArmed() < 0 && !gs.debugConfirmOpen()) ? 1 : 0, 1});
    }

    // ---- a tela de fim de missao ----------------------------------------
    gs.debugOptionsScreen(0);                        // o menu
    texto("o titulo do menu", gs.debugOptTitle(), "Game Options");
    casos.push_back({"E&ND MISSION abre a tela", gs.debugOptClick("E&ND MISSION") ? 1 : 0, 1});
    texto("o titulo da tela de fim", gs.debugOptTitle(),
          rede ? "Game End: Multiplayer" : "Game End: Single Player");
    const std::vector<std::string> esperados = rede
        ? std::vector<std::string>{"SELF-&DESTRUCT", "&TRANSFER", "&GAME OPTIONS"}
        : std::vector<std::string>{"RESTART MISSIO&N", "&BACK TO MAIN MENU",
                                   "E&XIT OUTPOST 2", "&GAME OPTIONS"};
    const std::vector<std::string> itens = gs.debugOptLabels();
    for (const std::string& r : itens) std::printf("[opt]   item: %s\n", r.c_str());
    casos.push_back({"os itens certos para o tipo de partida", itens == esperados ? 1 : 0, 1});
    if (rede)
        casos.push_back({"&TRANSFER acompanha o sub_4909F0",
                         gs.debugOptDisabled("&TRANSFER") == !op2::quitTransferAvailable() ? 1 : 0, 1});

    // A confirmacao: abre, SOBREVIVE ao quadro seguinte sem selecao nenhuma,
    // troca os botoes por &YES/&NO, e o &NO devolve a tela de fim.
    casos.push_back({"o item abre a confirmacao",
                     gs.debugOptClick(esperados[0]) && gs.debugConfirmOpen() ? 1 : 0, 1});
    gs.debugPaneFrame();
    casos.push_back({"e ela continua no ar no quadro seguinte",
                     gs.debugConfirmOpen() ? 1 : 0, 1});
    const std::vector<std::string> yn = gs.debugOptLabels();
    casos.push_back({"os botoes viraram &YES/&NO",
                     (yn.size() == 2 && yn[0] == "&YES" && yn[1] == "&NO") ? 1 : 0, 1});
    casos.push_back({"o &NO fecha a confirmacao",
                     gs.debugOptClick("&NO") || gs.debugConfirmOpen() ? 1 : 0, 1});
    casos.push_back({"... sem fechar a confirmacao de vez", gs.debugConfirmOpen() ? 1 : 0, 0});
    casos.push_back({"... e a tela de fim volta", gs.debugOptLabels() == esperados ? 1 : 0, 1});

    // O E&XIT e o unico cujo efeito da para observar sem desmontar o jogo:
    // o original posta WM_QUIT, e aqui um SDL_QUIT.
    if (!rede) {
        SDL_FlushEvent(SDL_QUIT);
        gs.debugOptClick("E&XIT OUTPOST 2");
        gs.debugPaneFrame();
        gs.debugOptClick("&YES");
        casos.push_back({"&YES no E&XIT pede o fim do programa",
                         SDL_HasEvent(SDL_QUIT) ? 1 : 0, 1});
        SDL_FlushEvent(SDL_QUIT);
        gs.debugPaneFrame();
    }

    // ---- quem pode transferir (sub_4909F0) ------------------------------
    const int me = op2::TethysGame::localPlayer();
    if (op2::TethysGame::noPlayers() > 1) {
        const int outro = me == 0 ? 1 : 0;
        const uint32_t alianca = op2::g_playerData[outro].allyMask;
        const int humano = op2::g_playerData[outro].isHuman;
        op2::g_playerData[outro].allyMask = alianca | (1u << me);
        op2::g_playerData[outro].isHuman = 0;
        casos.push_back({"aliado que nao e humano nao conta",
                         op2::quitTransferAvailable() ? 1 : 0, 0});
        if (rede) casos.push_back({"... e o botao nasce desabilitado",
                                   gs.debugOptDisabled("&TRANSFER") ? 1 : 0, 1});
        op2::g_playerData[outro].isHuman = 1;
        casos.push_back({"aliado humano habilita o &TRANSFER",
                         op2::quitTransferAvailable() ? 1 : 0, 1});
        if (rede) casos.push_back({"... e o botao fica clicavel",
                                   gs.debugOptDisabled("&TRANSFER") ? 1 : 0, 0});
        op2::g_playerData[outro].allyMask = alianca & ~(1u << me);
        casos.push_back({"humano sem alianca, nao",
                         op2::quitTransferAvailable() ? 1 : 0, 0});
        op2::g_playerData[outro].allyMask = alianca;
        op2::g_playerData[outro].isHuman = humano;
    } else {
        std::printf("[opt] (so um jogador: pulei o grupo do &TRANSFER)\n");
    }

    // ---- o pacote 0x31: quem saiu encerra, em QUALQUER metodo ------------
    op2::resetTriggers();
    op2::applyQuit(me, op2::QUIT_GO_AI, 2);
    casos.push_back({"GoAI entrega a colonia a IA", op2::g_playerData[me].isHuman, 0});
    casos.push_back({"... e encerra a partida de quem saiu",
                     op2::gameEndCode(), rede ? 9 : 6});
    const long long fim2 = op2::gameEndTick();
    op2::resetTriggers();
    op2::applyQuit(me, op2::QUIT_SELF_DESTRUCT, 4);
    casos.push_back({"o atraso da autodestruicao e o dobro",
                     op2::gameEndTick() - op2::TethysGame::tick() ==
                         2 * (fim2 - op2::TethysGame::tick()) ? 1 : 0, 1});

    // ---- do clique ao pacote 0x31 ---------------------------------------
    // Fora da rede o commandSubmit aplica na hora, entao o fim de jogo -- e o
    // atraso que sub_48A5D0 manda junto do metodo -- da para medir aqui.
    const long long agora = op2::TethysGame::tick();
    const long long velocidade = (fim2 - agora) / 2;        // o caso acima usou atraso 2
    gs.debugOptionsScreen(0);
    gs.debugOptClick("E&ND MISSION");
    op2::resetTriggers();
    casos.push_back({"o &YES executa o item",
                     gs.debugOptClick(rede ? "SELF-&DESTRUCT" : "&BACK TO MAIN MENU") &&
                         (gs.debugPaneFrame(), gs.debugOptClick("&YES")) ? 1 : 0, 1});
    casos.push_back({"... e o pacote 0x31 encerrou a partida",
                     op2::gameEndCode(), rede ? 9 : 6});
    casos.push_back({"... com o atraso do metodo (4 na autodestruicao, 2 nos outros)",
                     op2::gameEndTick() - agora, (rede ? 4 : 2) * velocidade});

    bool ok = true;
    for (const Caso& c : casos) {
        const bool bom = c.obtido == c.esperado;
        if (!bom) ok = false;
        std::printf("[opt] %-56s %lld (esperado %lld) %s\n", c.nome.c_str(), c.obtido,
                    c.esperado, bom ? "ok" : "FALHOU");
    }
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --item21test <DLL>: as tres pecas do item 21.
//   21a as vozes da plataforma do Spaceport (sub_47F600);
//   21b o icone do veiculo no painel STORAGE, agora com a arma (sub_458EF0);
//   21c o anel anti-duplicata de comandos ([_Player+0xBD8], sub_40E210).
static int runItem21Test(const std::string& dll) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 item21test", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.loadMission(dll)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);

    struct Caso { const char* nome; long long obtido; long long esperado; };
    std::vector<Caso> casos;

    // ================= 21a: as vozes da plataforma =======================
    const int sp = op2::placeBuilding(0x37, 20, 20, 0);          // Spaceport
    if (!sp) { std::printf("[21] sem Spaceport\n"); renderer.shutdown(); SDL_Quit(); return 3; }
    struct Voz { int tipo; int voz; const char* nome; };
    const Voz vozes[] = {
        {0x69, 0x80, "21a: SULV anuncia com a voz 0x80"},
        {0x6A, 0x7F, "21a: RLV anuncia com a voz 0x7F"},
        {0x6B, 0x80, "21a: missil anuncia com a voz 0x80"},
    };
    for (const Voz& v : vozes) {
        op2::soundClearVoiceRequest();
        op2::g_units[sp].produceType = v.tipo;
        op2::g_units[sp].padCargo = 0;
        op2::factoryProduceFinish(sp);
        casos.push_back({v.nome, op2::soundLastVoiceRequest(), v.voz});
        casos.push_back({"21a:   ... e a carga foi para a plataforma",
                         op2::g_units[sp].padCargo, v.tipo});
    }
    // Dono que nao e o jogador local nao ouve nada (cmp eax, dword_56DEE8).
    op2::g_units[sp].owner = static_cast<uint8_t>((op2::g_units[sp].owner & 0xF0) | 1);
    op2::soundClearVoiceRequest();
    op2::g_units[sp].produceType = 0x69;
    op2::g_units[sp].padCargo = 0;
    op2::factoryProduceFinish(sp);
    casos.push_back({"21a: dono alheio nao ouve", op2::soundLastVoiceRequest(), -1});

    // ================= 21b: o icone do STORAGE mostra a arma ==============
    // Duas renderizacoes do MESMO Lynx trocando so a arma. Com o codigo antigo
    // (quadro 0 do corpo) as duas sairiam identicas.
    const int gar = op2::placeBuilding(0x29, 24, 20, 0);          // Garagem
    int dx = 0, dy = 0;
    if (!gar || !op2::dockCell(gar, dx, dy)) {
        std::printf("[21] sem garagem/doca\n"); renderer.shutdown(); SDL_Quit(); return 4;
    }
    op2::Unit ly;
    op2::TethysGame::CreateUnit(ly, op2::mapLynx, op2::LOCATION{dx, dy}, 0, 0x3D);
    casos.push_back({"21b: o Lynx esta na doca", op2::dockedVehicle(gar) == ly.id() ? 1 : 0, 1});

    op2::StoragePane painel;
    painel.open(gar, false);
    const int FW = 640, FH = 480, winL = 0, statusTop = 0x60;
    const int bx = winL + 0xD, by = statusTop - 0x33;             // a caixa da doca
    std::vector<uint32_t> fb1(static_cast<size_t>(FW) * FH, 0), fb2(fb1.size(), 0);
    const op2::PaneRect pane{0, 0, FW, FH};
    auto desenha = [&](std::vector<uint32_t>& fb, int arma) {
        op2::g_units[ly.id()].cargo = static_cast<int16_t>(arma);
        std::fill(fb.begin(), fb.end(), 0u);
        painel.draw(fb.data(), FW, FH, winL, 0, pane, statusTop, -1, -1);
    };
    desenha(fb1, 0x3D);                                           // Laser
    desenha(fb2, 0x3E);                                           // Microwave
    int difer = 0, pintados = 0;
    for (int y = by; y < by + 0x2C; ++y)
        for (int x = bx; x < bx + 0x2C; ++x) {
            const size_t k = static_cast<size_t>(y) * FW + x;
            if (fb1[k] != fb2[k]) ++difer;
            if (fb1[k] != 0u) ++pintados;
        }
    std::printf("[21] caixa do STORAGE: %d pixels pintados, %d diferentes entre as armas\n",
                pintados, difer);
    casos.push_back({"21b: a caixa foi pintada", pintados > 0 ? 1 : 0, 1});
    casos.push_back({"21b: trocar a arma muda o icone", difer > 0 ? 1 : 0, 1});

    // ================= 21c: o anel anti-duplicata ========================
    const int ag = op2::placeBuilding(0x27, 28, 20, 0);           // Agridome
    if (!ag) { std::printf("[21] sem Agridome\n"); renderer.shutdown(); SDL_Quit(); return 5; }
    op2::g_playerData[0].isHuman = 1;
    auto mandaIdle = [&](int id) {
        op2::CommandPacket pk;
        pk.type = op2::CMD_IDLE;
        pk.unitIds = {id};
        op2::dispatchCommandPacket(0, pk);
    };
    auto ocioso = [&](int id) { return op2::g_units[id].action == 0x12 ? 1 : 0; };
    auto andaTurnos = [&](int n) {
        for (int k = 0; k < n * op2::kTurnPeriod; ++k) op2::TethysGame::advanceTick();
    };

    mandaIdle(ag);
    casos.push_back({"21c: o 1o IDLE foi aplicado", ocioso(ag), 1});
    // Desliga a marca na mao: structureDoActivate exige trabalhadores, energia e
    // ligação ao CC, que este arranjo não tem, e devolveria false deixando o
    // prédio ocioso -- o observável ficaria cego e o caso passaria sozinho.
    op2::g_units[ag].action = 0;
    casos.push_back({"21c: a marca foi desligada p/ o próximo caso", ocioso(ag), 0});
    andaTurnos(2);                                   // entra na janela de conferencia
    mandaIdle(ag);
    casos.push_back({"21c: repetido 2 turnos depois, DESCARTADO", ocioso(ag), 0});
    andaTurnos(4);                                   // a janela passou do slot
    mandaIdle(ag);
    casos.push_back({"21c: fora da janela, aplicado de novo", ocioso(ag), 1});
    op2::g_units[ag].action = 0;
    op2::g_playerData[0].isHuman = 0;                // o PC nao passa pelo anel
    andaTurnos(2);
    mandaIdle(ag);
    casos.push_back({"21c: jogador do PC ignora o anel", ocioso(ag), 1});

    bool ok = true;
    for (const Caso& c : casos) {
        const bool bom = c.obtido == c.esperado;
        if (!bom) ok = false;
        std::printf("[21] %-44s %lld (esperado %lld) %s\n", c.nome, c.obtido, c.esperado,
                    bom ? "ok" : "FALHOU");
    }
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ok ? 0 : 1;
}

// --walltest <map>: parede comum, de lava, de micróbio e um tubo lado a lado;
// sub_4A23F0 (damageWall) com dano 1000 e depois 20, 6x cada, imprimindo o tile
// e o tipo da célula — a parede comum passa pelos estágios +0x54/+0x74/+0x94.
// --mdtest <mapa>: a Meteor Defense diante do missil de um ALIADO. O original
// (sub_4802A0) so pula o PROPRIO dono da defesa, entao numa partida aliada a
// defesa do amigo abatia o missil que ia no inimigo -- defeito que o porte
// herdou e que esta corrigido em missile.cpp.
//
// O observavel e a flag 0x40000, posta em QUALQUER engajamento (derrubando ou
// nao): assim o caso nao depende do dado de 50/75% da Observatory.
static int runMdTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 mdtest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);

    // Um canto passavel para os dois predios.
    int bx = -1, by = -1;
    for (int y = H / 2; y < H - 8 && bx < 0; ++y)
        for (int x = W / 2; x < W - 12 && bx < 0; ++x) {
            bool ok = true;
            for (int dy = 0; dy < 4 && ok; ++dy)
                for (int dx = 0; dx < 10 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) { renderer.shutdown(); SDL_Quit(); return 3; }

    op2::g_playerData[0].isHuman = 1;
    op2::g_playerData[1].isHuman = 1;

    // Observatory (0x33) e Meteor Defense (0x2B) do jogador 1, operacionais.
    op2::Unit obs, def;
    op2::TethysGame::CreateUnit(obs, static_cast<op2::map_id>(0x33), op2::LOCATION{bx, by + 2}, 1);
    op2::TethysGame::CreateUnit(def, static_cast<op2::map_id>(0x2B), op2::LOCATION{bx + 6, by + 2}, 1);
    if (!obs.id() || !def.id()) { renderer.shutdown(); SDL_Quit(); return 4; }
    // structureIsOperational (sub_483750) pede os TRES bits e a acao fora de
    // obra; sem isso o sub_4802A0 pula a defesa e o teste nao mede nada.
    for (int id : {obs.id(), def.id()}) {
        op2::g_units[id].flags |= op2::UNIT_POWERED | op2::UNIT_WORKERS | op2::UNIT_SCIENTISTS;
        op2::g_units[id].action = 0;
        op2::g_units[id].damage = 0;
    }
    op2::g_colony[1].stats[op2::ST_OBSERVATORY / 4] = 1;
    op2::g_colony[1].stats[op2::ST_METEOR_DEF / 4] = 1;

    struct Caso { std::string nome; long long obtido, esperado; };
    std::vector<Caso> casos;
    // Sem isto o resto nao mede nada: o sub_4802A0 pula defesa nao operacional.
    casos.push_back({"a defesa esta operacional", op2::structureOperational(op2::g_units[def.id()]) ? 1 : 0, 1});
    casos.push_back({"a defesa entrou na lista da colonia",
                     [&]{ for (int b : op2::colonyStructures(1)) if (b == def.id()) return 1; return 0; }(), 1});

    // O missil do jogador 0 descendo ao lado da defesa (0x0C < [+0x3C] < 0x14).
    auto engajou = [&](bool aliados) {
        op2::g_playerData[0].allyMask = (1u << 0) | 0x40u | (aliados ? (1u << 1) : 0u);
        const int m = op2::setEMPMissile(bx + 6, by + 6, 0, bx + 6, by + 2);
        if (!m) return -1;
        op2::g_units[m].field22 = 9;
        op2::g_units[m].field3C = 0x10;
        op2::missilePeriodicTick(m);
        return (op2::g_units[m].flags & 0x40000u) ? 1 : 0;
    };
    casos.push_back({"inimigo: a defesa engaja o missil", engajou(false), 1});
    casos.push_back({"ALIADO: a defesa nao engaja", engajou(true), 0});

    // Item 2 da lista do OPUPatch: no original, missil mirado EM CIMA de uma
    // Meteor Defense passava batido pelas defesas vizinhas. Aqui a conta de
    // distancia e feita da posicao do MISSIL, entao em cima da defesa da zero
    // e ela engaja -- o caso trava isso.
    {
        op2::g_playerData[0].allyMask = (1u << 0) | 0x40u;        // inimigos
        const int m = op2::setEMPMissile(bx + 6, by + 2, 0, bx + 6, by + 2);  // o tile DA defesa
        if (m) {
            op2::g_units[m].field22 = 9;
            op2::g_units[m].field3C = 0x10;
            op2::missilePeriodicTick(m);
            casos.push_back({"missil EM CIMA da defesa: ela engaja",
                             (op2::g_units[m].flags & 0x40000u) ? 1 : 0, 1});
        }
    }

    int ruins = 0;
    for (const Caso& c : casos) {
        const bool ok = c.obtido == c.esperado;
        if (!ok) ++ruins;
        std::printf("[md] %-44s %lld (esperado %lld) %s%c", c.nome.c_str(), c.obtido,
                    c.esperado, ok ? "ok" : "FALHOU", 10);
    }
    std::printf("[md] %d caso(s) com falha%c", ruins, 10);
    std::fflush(stdout);
    renderer.shutdown();
    SDL_Quit();
    return ruins ? 1 : 0;
}

// --scrolltest <mapa>: a rolagem da vista (sub_407A10). O passo base e
// (FrameSkip * ScrollRate * 20) / (GameSpeed*4), no minimo 1; a velocidade
// acelera de um passo por quadro ate passo*32, salta passo*4 na direcao do
// zero ao trocar de lado, e volta a zero de passo*4 por quadro quando ninguem
// pede. O minimapa segue sozinho -- ele e desenhado da camera --, e o ultimo
// caso prova isso comparando os pixels do minimapa antes e depois.
static int runScrollTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 scrolltest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();

    struct Caso { std::string nome; long long obtido, esperado; };
    std::vector<Caso> casos;
    const int passo = gs.debugScrollStep();
    casos.push_back({"o passo sai da formula e e >= 1", passo >= 1 ? 1 : 0, 1});

    // --- a regra da BORDA (2 px da janela) e das setas --------------------
    {
        const int W = 1280, H = 960;
        struct B { const char* nome; int mx, my; bool naJanela, kE, kD, kC, kB;
                   int esq, dir, cima, baixo; };
        const B bs[] = {
            { "meio da janela: nada",            640, 480, true,  0,0,0,0,  0,0,0,0 },
            { "x=0: esquerda",                     0, 480, true,  0,0,0,0,  1,0,0,0 },
            { "x=2: ainda esquerda (margem 2)",    2, 480, true,  0,0,0,0,  1,0,0,0 },
            { "x=3: ja nao e borda",               3, 480, true,  0,0,0,0,  0,0,0,0 },
            { "x=W-3: direita",             W - 3, 480, true,  0,0,0,0,  0,1,0,0 },
            { "x=W-1: direita",             W - 1, 480, true,  0,0,0,0,  0,1,0,0 },
            { "y=0: cima",                       640,   0, true,  0,0,0,0,  0,0,1,0 },
            { "y=H-1: baixo",               640, H - 1, true,  0,0,0,0,  0,0,0,1 },
            { "canto: esquerda e cima",            0,   0, true,  0,0,0,0,  1,0,1,0 },
            { "ponteiro fora da janela: nada",     0,   0, false, 0,0,0,0,  0,0,0,0 },
            { "seta esquerda, ponteiro no meio", 640, 480, true,  1,0,0,0,  1,0,0,0 },
            { "seta direita",                    640, 480, true,  0,1,0,0,  0,1,0,0 },
            { "setas cima e baixo juntas",       640, 480, true,  0,0,1,1,  0,0,1,0 },
            { "borda esquerda ganha da seta direita", 0, 480, true, 0,1,0,0,  1,0,0,0 },
        };
        for (const B& b : bs) {
            bool e = false, d = false, c = false, x = false;
            op2::GameState::scrollWanted(b.mx, b.my, W, H, b.naJanela,
                                         b.kE != 0, b.kD != 0, b.kC != 0, b.kB != 0,
                                         e, d, c, x);
            const int got = (e ? 1 : 0) | (d ? 2 : 0) | (c ? 4 : 0) | (x ? 8 : 0);
            const int esp = b.esq | (b.dir << 1) | (b.cima << 2) | (b.baixo << 3);
            casos.push_back({std::string("borda: ") + b.nome, got, esp});
        }
    }

    // Um quadro pedindo ESQUERDA anda exatamente um passo.
    // Longe das bordas: o clampCamera prende a camera no limite do mapa, e
    // ai um passo nao anda nada (foi o que esta linha acusou na 1a rodada).
    gs.setCamera(300, 300);
    gs.debugScrollFrame(true, false, false, false);
    casos.push_back({"um quadro a esquerda anda um passo", 300 - gs.camX(), passo});
    casos.push_back({"... e a velocidade ficou negativa", gs.debugScrollVX(), -passo});

    // Segurando, acelera e satura em passo*32.
    for (int k = 0; k < 60; ++k) gs.debugScrollFrame(true, false, false, false);
    casos.push_back({"segurando, satura em passo*32", gs.debugScrollVX(), -passo * 32});

    // Trocando de lado, a velocidade salta passo*4 na direcao do zero antes
    // de somar o passo -- freia rapido em vez de virar de uma vez.
    const int antes = gs.debugScrollVX();
    gs.debugScrollFrame(false, true, false, false);
    casos.push_back({"trocando de lado, freia passo*4 + passo",
                     gs.debugScrollVX(), antes + passo * 4 + passo});

    // Soltando tudo, desacelera ate parar -- e a camera ainda desliza.
    // Antes, deixa a vista PARAR: a velocidade vinha negativa do caso de cima,
    // e os quadros a direita so freavam (foi o que este caso acusou).
    for (int k = 0; k < 100 && gs.debugScrollVX() != 0; ++k)
        gs.debugScrollFrame(false, false, false, false);
    gs.setCamera(0, 0);
    // Poucos quadros: com muitos a camera chega no limite do mapa e nao sobra
    // para onde deslizar -- foi o que este caso acusou na 2a rodada.
    for (int k = 0; k < 3; ++k) gs.debugScrollFrame(false, true, false, false);
    const int xDeslize = gs.camX();
    int quadros = 0;
    while (gs.debugScrollVX() != 0 && quadros < 200) { gs.debugScrollFrame(false, false, false, false); ++quadros; }
    casos.push_back({"soltando, a velocidade zera", gs.debugScrollVX(), 0});
    casos.push_back({"... depois de deslizar mais um pouco", gs.camX() > xDeslize ? 1 : 0, 1});

    // Os quatro sentidos.
    gs.setCamera(300, 300);
    gs.debugScrollFrame(false, false, true, false);
    casos.push_back({"cima sobe", gs.camY() < 300 ? 1 : 0, 1});
    gs.setCamera(300, 300);
    gs.debugScrollFrame(false, false, false, true);
    casos.push_back({"baixo desce", gs.camY() > 300 ? 1 : 0, 1});

    // O MINIMAPA acompanha: desenha um quadro, rola bastante, desenha outro
    // e compara os pixels da caixa do minimapa.
    auto minimapa = [&]() {
        gs.onIdle(renderer, 0.0f);
        const uint32_t* fb = renderer.getFramebuffer();
        const int W = renderer.getWidth();
        std::vector<uint32_t> px;
        // kColX e de instancia desde que o HUD passou a seguir a janela.
        for (int y = op2::GameState::kMiniMapY; y < op2::GameState::kMiniMapY + op2::GameState::kMiniMapH; ++y)
            for (int x = gs.kColX; x < gs.kColX + op2::GameState::kColW; ++x)
                px.push_back(fb[y * W + x]);
        return px;
    };
    gs.setCamera(0, 0);
    const std::vector<uint32_t> antesMini = minimapa();
    for (int k = 0; k < 60; ++k) gs.debugScrollFrame(false, true, false, true);
    const std::vector<uint32_t> depoisMini = minimapa();
    int diferentes = 0;
    for (size_t i = 0; i < antesMini.size() && i < depoisMini.size(); ++i)
        if (antesMini[i] != depoisMini[i]) ++diferentes;
    casos.push_back({"o minimapa acompanha (pixels mudaram)", diferentes > 0 ? 1 : 0, 1});

    int ruins = 0;
    for (const Caso& c : casos) {
        const bool ok = c.obtido == c.esperado;
        if (!ok) ++ruins;
        std::printf("[scroll] %-44s %lld (esperado %lld) %s%c", c.nome.c_str(), c.obtido,
                    c.esperado, ok ? "ok" : "FALHOU", 10);
    }
    std::printf("[scroll] passo=%d  %d caso(s) com falha%c", passo, ruins, 10);
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ruins ? 1 : 0;
}

// --selecttest <mapa>: a area em que o clique marca a unidade. O porte usava
// um raio de 20 px do centro para tudo, entao num predio de 3x3 (96 px) so o
// miolo respondia. Agora o predio responde na PEGADA inteira e o veiculo numa
// caixa do tamanho do sprite -- ver GameState::unitHitBox.
static int runSelectTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 selecttest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);

    struct Caso { std::string nome; long long obtido, esperado; };
    std::vector<Caso> casos;

    // Um Command Center (pegada maior que um tile) num canto livre.
    const int bx = 12, by = 12;
    op2::Unit cc;
    if (!op2::TethysGame::CreateUnit(cc, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0)) {
        renderer.shutdown(); SDL_Quit(); return 3;
    }
    int tw = 1, th = 1;
    op2::buildingFootprint(op2::mapCommandCenter, tw, th);
    casos.push_back({"o predio tem pegada maior que 1 tile", tw * th > 1 ? 1 : 0, 1});

    // A camera no canto: a vista comeca no pixel (0,0) do mapa.
    gs.setCamera(0, 0);
    const int tlx = bx - (tw / 2), tly = by - (th / 2);

    // Clicar em CADA tile da pegada tem de marcar o predio.
    int acertos = 0;
    for (int dy = 0; dy < th; ++dy)
        for (int dx = 0; dx < tw; ++dx) {
            gs.debugClearSelection();
            gs.debugSelectAt(((tlx + dx) << 5) + 16, ((tly + dy) << 5) + 16);
            if (gs.debugSelectedCount() == 1 && gs.debugSelectedId(0) == cc.id()) ++acertos;
        }
    casos.push_back({"clicar em qualquer tile do predio marca", acertos, tw * th});

    // Um tile FORA da pegada nao marca nada (controle negativo).
    gs.debugClearSelection();
    gs.debugSelectAt(((tlx + tw + 1) << 5) + 16, ((tly) << 5) + 16);
    casos.push_back({"controle: tile fora nao marca", gs.debugSelectedCount(), 0});

    // O canto do predio tambem responde (era o que mais falhava).
    gs.debugClearSelection();
    gs.debugSelectAt((tlx << 5) + 2, (tly << 5) + 2);
    casos.push_back({"o canto do predio marca", gs.debugSelectedCount(), 1});

    // Veiculo: o centro e uma borda do sprite marcam.
    op2::Unit lynx;
    op2::TethysGame::CreateUnit(lynx, op2::mapLynx, op2::LOCATION{bx + 6, by}, 0, 0x3D);
    const int vcx = ((bx + 6) << 5) + 16, vcy = (by << 5) + 16;
    gs.debugClearSelection();
    gs.debugSelectAt(vcx, vcy);
    casos.push_back({"veiculo: o centro marca", gs.debugSelectedId(0) == lynx.id() ? 1 : 0, 1});
    gs.debugClearSelection();
    gs.debugSelectAt(vcx + 20, vcy + 20);
    casos.push_back({"veiculo: a borda do sprite marca", gs.debugSelectedId(0) == lynx.id() ? 1 : 0, 1});
    gs.debugClearSelection();
    gs.debugSelectAt(vcx + 60, vcy + 60);
    casos.push_back({"controle: longe do veiculo nao marca", gs.debugSelectedCount(), 0});

    // O realce (hover) usa a mesma caixa.
    casos.push_back({"o realce ve o predio no canto",
                     gs.debugUnitAtView((tlx << 5) + 2, (tly << 5) + 2) == cc.id() ? 1 : 0, 1});

    int ruins = 0;
    for (const Caso& c : casos) {
        const bool ok = c.obtido == c.esperado;
        if (!ok) ++ruins;
        std::printf("[sel] %-44s %lld (esperado %lld) %s%c", c.nome.c_str(), c.obtido,
                    c.esperado, ok ? "ok" : "FALHOU", 10);
    }
    std::printf("[sel] pegada %dx%d; %d caso(s) com falha%c", tw, th, ruins, 10);
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ruins ? 1 : 0;
}

// --anitest: le os cursores .ANI da pasta do jogo (RIFF/ACON) e confere o que
// saiu de cada um: quadros, tamanho, ponto quente e cadencia. O original os
// carrega com LoadCursorFromFileA (sub_488900) pela tabela de 0x4E9840.
static int runAniTest() {
    op2::g_ResManager.init(".");
    static const char* kNomes[] = {
        "move.ani", "no_move.ani", "attack.ani", "noattack.ani", "bulldoze.ani",
        "build.ani", "nobuild.ani", "dock.ani", "nodock.ani", "repair.ani",
        "norepair.ani", "reprog.ani", "no_repro.ani", "guard.ani", "salvage.ani",
        "waypoint.ani", "destruct.ani",
    };
    int lidos = 0, ruins = 0;
    for (const char* nome : kNomes) {
        auto st = op2::g_ResManager.openStream(nome);
        if (!st) {
            std::printf("[ani] %-14s NAO ACHEI o arquivo%c", nome, 10);
            ++ruins;
            continue;
        }
        op2::AniCursor cur;
        const bool ok = op2::aniLoad(st->data(), st->size(), cur);
        const bool sano = ok && cur.width > 0 && cur.height > 0 &&
                          !cur.frames.empty() &&
                          cur.hotX >= 0 && cur.hotX < cur.width &&
                          cur.hotY >= 0 && cur.hotY < cur.height;
        // Cada quadro precisa de pixel OPACO (senao o cursor some) e de pixel
        // TRANSPARENTE (senao virou um quadrado solido -- e assim que se
        // percebe a mascara AND do .CUR ignorada).
        bool temPixel = sano;
        for (const auto& f : cur.frames) {
            bool opaco = false, vazio = false;
            for (uint32_t c : f) {
                if (c >> 24) opaco = true; else vazio = true;
                if (opaco && vazio) break;
            }
            if (!opaco || !vazio) temPixel = false;
        }
        // Todo passo tem de cair num quadro que existe; e quando ha mais passos
        // que quadros (attack, repair, waypoint...), o `seq ` e que diz a ordem
        // -- sem ele a conta estoura ou repete errado.
        bool seqOk = sano;
        for (int i = 0; i < cur.steps(); ++i) {
            // Compara com a sequencia DO ARQUIVO: so olhar se o quadro existe
            // nao basta, porque o frameOfStep tranca em 0 o indice invalido --
            // e ai um `seq` ignorado passaria batido.
            const int esperado = cur.seq.empty() ? (i % static_cast<int>(cur.frames.size()))
                                                 : cur.seq[static_cast<size_t>(i)];
            const int f = cur.frameOfStep(i);
            if (f < 0 || f >= static_cast<int>(cur.frames.size())) seqOk = false;
            if (esperado >= 0 && esperado < static_cast<int>(cur.frames.size()) && f != esperado)
                seqOk = false;
        }
        if (cur.steps() > static_cast<int>(cur.frames.size()) && cur.seq.empty()) seqOk = false;
        if (sano && temPixel && seqOk) ++lidos; else ++ruins;
        std::printf("[ani] %-14s %dx%d quadros=%d passos=%d quente=(%d,%d) jiffies=%d %s%c",
                    nome, cur.width, cur.height, (int)cur.frames.size(), cur.steps(),
                    cur.hotX, cur.hotY, cur.jiffies,
                    (sano && temPixel && seqOk) ? "ok" : "FALHOU", 10);
    }
    std::printf("[ani] %d de %d cursores lidos%c", lidos, (int)(sizeof(kNomes) / sizeof(kNomes[0])), 10);
    std::fflush(stdout);
    return ruins ? 1 : 0;
}

// --cursortest <mapa>: o cursor de comando que aparece em cada situacao. A
// tabela do original (0x4E9840) da os PARES "pode"/"nao pode" e o porte
// escolhe com o mesmo teste que o comando usa -- ver GameState::cursorForHover.
static int runCursorTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 cursortest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::cursorsInit();
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    op2::resetUnits();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) op2::g_gameMap.clearUnitOccupancy(x, y);

    struct Caso { std::string nome; long long obtido, esperado; };
    std::vector<Caso> casos;
    casos.push_back({"os 17 .ANI entraram", op2::cursorsLoaded(), 17});

    // Um canto livre e um tile intransponivel, para os dois lados do par.
    int lx = -1, ly = -1, bx = -1, by = -1;
    for (int y = 4; y < H - 6 && lx < 0; ++y)
        for (int x = 4; x < W - 10 && lx < 0; ++x) {
            bool ok = true;
            for (int dy = 0; dy < 4 && ok; ++dy)
                for (int dx = 0; dx < 10 && ok; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) ok = false;
            if (ok) { lx = x; ly = y; }
        }
    // Um tile FECHADO ao lado da faixa: procurar um no mapa daria um tile longe
    // da camera, e fora da vista o cursor e a seta -- com razao.
    if (lx >= 0) {
        bx = lx + 1; by = ly + 3;
        op2::createWallOrTube(bx, by, 0x12);
    }
    if (lx < 0) { renderer.shutdown(); SDL_Quit(); return 3; }

    // A camera perto da faixa livre: ela pode comecar longe da origem, e ai os
    // tiles do teste caem FORA da vista -- onde o cursor e a seta, com razao.
    gs.setCamera((lx << 5) - 64, (ly << 5) - 64);
    auto emTile = [&](int tx, int ty, int cmd) {
        return gs.debugCursor((tx << 5) + 16 - gs.camX(), (ty << 5) + 16 - gs.camY(), cmd);
    };

    casos.push_back({"sem mira: a seta", emTile(lx, ly, -1), op2::CUR_ARROW});
    casos.push_back({"MOVE em chao livre: move", emTile(lx, ly, op2::UC_MOVE), op2::CUR_MOVE});
    if (bx >= 0)
        casos.push_back({"MOVE em chao fechado: no_move", emTile(bx, by, op2::UC_MOVE), op2::CUR_NOMOVE});
    casos.push_back({"BULLDOZE: bulldoze", emTile(lx, ly, op2::UC_BULLDOZE), op2::CUR_BULLDOZE});
    casos.push_back({"GUARD: guard", emTile(lx, ly, op2::UC_GUARD), op2::CUR_GUARD});
    casos.push_back({"PATROL: waypoint", emTile(lx, ly, op2::UC_PATROL), op2::CUR_WAYPOINT});
    casos.push_back({"SELF-DESTRUCT: destruct", emTile(lx, ly, op2::UC_SELF_DESTRUCT), op2::CUR_DESTRUCT});
    casos.push_back({"MURO em celula livre: build", emTile(lx, ly, op2::UC_WALL), op2::CUR_BUILD});

    // Inimigo sob o ponteiro: atacar pode; chao vazio, nao.
    op2::Unit ini;
    op2::TethysGame::CreateUnit(ini, op2::mapLynx, op2::LOCATION{lx + 4, ly}, 1, 0x3D);
    casos.push_back({"ATTACK sobre inimigo: attack",
                     emTile(lx + 4, ly, op2::UC_ATTACK), op2::CUR_ATTACK});
    casos.push_back({"ATTACK em chao vazio: noattack",
                     emTile(lx, ly, op2::UC_ATTACK), op2::CUR_NOATTACK});
    casos.push_back({"TAKE OVER sobre inimigo: reprog",
                     emTile(lx + 4, ly, op2::UC_TAKE_OVER), op2::CUR_REPROG});

    // Reparar: o meu, machucado, pode; inteiro, nao.
    op2::Unit meu;
    op2::TethysGame::CreateUnit(meu, op2::mapLynx, op2::LOCATION{lx + 7, ly}, 0, 0x3D);
    op2::g_units[meu.id()].damage = 50;
    casos.push_back({"REPAIR sobre o meu machucado: repair",
                     emTile(lx + 7, ly, op2::UC_REPAIR), op2::CUR_REPAIR});
    op2::g_units[meu.id()].damage = 0;
    casos.push_back({"REPAIR sobre o meu inteiro: norepair",
                     emTile(lx + 7, ly, op2::UC_REPAIR), op2::CUR_NOREPAIR});

    int ruins = 0;
    for (const Caso& c : casos) {
        const bool ok = c.obtido == c.esperado;
        if (!ok) ++ruins;
        std::printf("[cur] %-40s %lld (esperado %lld) %s%c", c.nome.c_str(), c.obtido,
                    c.esperado, ok ? "ok" : "FALHOU", 10);
    }
    std::printf("[cur] %d caso(s) com falha%c", ruins, 10);
    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return ruins ? 1 : 0;
}

static int runWallTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 walltest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int bx = -1, by = -1;
    for (int y = H / 2; y < H - 2 && bx < 0; ++y)
        for (int x = W / 2; x < W - 10 && bx < 0; ++x) {
            bool ok = true;
            for (int dx = 0; dx < 8 && ok; ++dx)
                if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y}) ||
                    (op2::g_gameMap.rawCell(x + dx, y) & 0x7FF0000u)) ok = false;
            if (ok) { bx = x; by = y; }
        }
    if (bx < 0) return 3;
    const int types[4] = {0x12, 0x13, 0x14, 0x11};
    for (int k = 0; k < 4; ++k) op2::createWallOrTube(bx + 2 * k, by, types[k]);
    for (int dmg : {1000, 20}) {
        if (dmg == 20) for (int k = 0; k < 4; ++k) op2::createWallOrTube(bx + 2 * k, by, types[k]);
        for (int pass = 0; pass < 6; ++pass) {
            std::printf("[WallTest] dano=%4d passada %d:", dmg, pass);
            for (int k = 0; k < 4; ++k) {
                const int x = bx + 2 * k;
                op2::damageWall(x, by, dmg);
                const uint32_t c = op2::g_gameMap.rawCell(x, by);
                std::printf("  0x%02X tile=%4u tipo=0x%02X%s", types[k], (c & 0xFFE0u) >> 5, c & 0x1Fu,
                            (c & 0x80000000u) ? " bloq" : "");
            }
            std::printf("\n");
        }
    }

    // --- 2a parte: a IA atira na parede que bloqueia o caminho ---
    // loc_44B8F0 de sub_44B650 so liga o flag 0x2000 para jogador NAO humano,
    // armado de Laser (0x3D) ou Microwave (0x3E). Montamos os dois casos: o
    // tanque do computador deve mirar o TILE da parede; o do humano, nao.
    bool fgOk = true;
    for (int humano = 0; humano <= 1; ++humano) {
        for (int k = 0; k < 4; ++k) op2::createWallOrTube(bx + 2 * k, by, types[k]);
        const int px = bx + 2, py = by;            // parede em (px,py)
        op2::g_playerData[1].isHuman = humano;
        op2::Unit tank;
        if (!op2::TethysGame::CreateUnit(tank, static_cast<op2::map_id>(1),
                                         op2::LOCATION{px, py - 1}, 1, 0x3D)) { fgOk = false; break; }
        const int tid = tank.id();
        op2::g_units[tid].flags |= op2::UNIT_ARMED;
        op2::g_units[tid].cargo = 0x3D;            // Laser
        op2::g_units[tid].field4E = 0x7FFF;        // sem alvo
        op2::g_units[tid].flags &= ~op2::UNIT_FIRE_GROUND;
        op2::wallBlockTarget(op2::g_units[tid], px, py);
        const op2::UnitRecord& r = op2::g_units[tid];
        const bool marcou = (r.flags & op2::UNIT_FIRE_GROUND) != 0;
        const bool noTile = !r.targetIsUnit() && r.target4C == px && r.field4E == py;
        std::printf("[WallTest] fire-ground: jogador %s -> flag=%d alvo=(%d,%d) %s%c",
                    humano ? "HUMANO " : "do PC  ", marcou ? 1 : 0,
                    (int)r.target4C, (int)r.field4E,
                    humano ? "(esperado: nada)" : "(esperado: flag e o tile da parede)", 10);
        if (humano) { if (marcou) fgOk = false; }
        else if (!marcou || !noTile) fgOk = false;
        op2::g_units[tid].flags |= op2::UNIT_DEAD;     // tira da cena
    }
    op2::g_playerData[1].isHuman = 1;

    // --- a vizinhanca do muro (sub_438D70) -------------------------------
    // O teste dos 8 vizinhos nao pode reprovar por LAVA (bit 27), PRAGA (bit
    // 29) nem por um muro EM OBRA (a reserva do bit 31). Mexo direto na celula
    // vizinha, ponho o bit e pergunto ao wallCellOk; devolvo a celula depois.
    bool vizOk = true;
    {
        int cx = -1, cy = -1;
        for (int y = by - 2; y <= by + 2 && cx < 0; ++y)
            for (int x = bx - 2; x <= bx + 6 && cx < 0; ++x) {
                if (x < 1 || y < 1 || x >= W - 1 || y >= H - 1) continue;
                bool livre = true;
                for (int dy = -1; dy <= 1 && livre; ++dy)
                    for (int dx = -1; dx <= 1 && livre; ++dx)
                        if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy}) ||
                            (op2::g_gameMap.rawCell(x + dx, y + dy) & 0xAFFF0000u)) livre = false;
                if (livre) { cx = x; cy = y; }
            }
        op2::pfSetContext(0);
        auto podeCom = [&](uint32_t bit) {
            const uint32_t salvo = op2::g_gameMap.rawCell(cx + 1, cy);
            op2::g_gameMap.setRawCell(cx + 1, cy, salvo | bit);
            const bool r = op2::wallCellOk(cx, cy, 0x12);
            op2::g_gameMap.setRawCell(cx + 1, cy, salvo);
            return r;
        };
        struct V { const char* nome; uint32_t bit; bool esperado; };
        const V vs[] = {
            { "controle: vizinho limpo",        0u,          true  },
            { "vizinho com LAVA (bit 27)",      0x08000000u, true  },
            { "vizinho com PRAGA (bit 29)",     0x20000000u, true  },
            { "vizinho com MURO EM OBRA (31)",  0x80000000u, true  },
        };
        if (cx < 0) { std::printf("[WallTest] vizinhanca: nao achei celula livre%c", 10); vizOk = false; }
        for (const V& v : vs) {
            if (cx < 0) break;
            const bool r = podeCom(v.bit);
            if (r != v.esperado) vizOk = false;
            std::printf("[WallTest] vizinhanca %-32s pode=%d (esperado %d) %s%c",
                        v.nome, r ? 1 : 0, v.esperado ? 1 : 0, r == v.esperado ? "ok" : "FALHOU", 10);
        }
        // Controle negativo: PREDIO vizinho que nao e Guard Post reprova.
        if (cx >= 0) {
            op2::Unit b;
            if (op2::TethysGame::CreateUnit(b, op2::mapCommandCenter, op2::LOCATION{cx + 2, cy}, 0)) {
                const bool r = op2::wallCellOk(cx, cy, 0x12);
                if (r) vizOk = false;
                std::printf("[WallTest] vizinhanca %-32s pode=%d (esperado 0) %s%c",
                            "controle: PREDIO vizinho", r ? 1 : 0, r ? "FALHOU" : "ok", 10);
            }
        }
    }

    // --- a reserva da celula do muro -------------------------------------
    // O Earthworker marca o bit 31 da celula quando comeca a levantar o muro e
    // so o tira ao assentar. Dois buracos vinham dai: morrer no meio deixava a
    // reserva para sempre (um bloqueio invisivel), e o muro era assentado por
    // cima de quem estivesse no tile, sem devolver o metal.
    bool resOk = true;
    {
        int wx = -1, wy = -1;
        for (int y = by + 3; y < H - 3 && wx < 0; ++y)
            for (int x = bx; x < bx + 8 && wx < 0; ++x)
                if (op2::g_gameMap.isPassable(op2::LOCATION{x, y}) &&
                    !(op2::g_gameMap.rawCell(x, y) & 0xAFFF0000u) &&
                    op2::g_gameMap.isPassable(op2::LOCATION{x, y + 1})) { wx = x; wy = y; }
        auto temReserva = [&]() { return (op2::g_gameMap.rawCell(wx, wy) & 0x80000000u) != 0; };
        auto novoEw = [&](int tx, int ty) {
            op2::Unit u;
            op2::TethysGame::CreateUnit(u, op2::mapEarthworker, op2::LOCATION{tx, ty}, 0);
            return u.id();
        };
        auto reserva = [&](int id) {
            op2::g_gameMap.setRawCell(wx, wy, op2::g_gameMap.rawCell(wx, wy) | 0x80000000u);
            op2::g_units[id].wallCurX = static_cast<int16_t>(wx);
            op2::g_units[id].wallCurY = static_cast<int16_t>(wy);
        };

        // (3) morrer no meio da obra larga a celula.
        const int ew1 = novoEw(wx, wy + 1);
        reserva(ew1);
        const bool antes = temReserva();
        op2::killUnit(ew1, 0);
        const bool depois = temReserva();
        if (!antes || depois) resOk = false;
        std::printf("[WallTest] reserva: antes da morte=%d depois=%d (esperado 1/0) %s\n",
                    antes ? 1 : 0, depois ? 1 : 0, (antes && !depois) ? "ok" : "FALHOU");

        // Controle: se a celula JA virou muro, o bit 31 e do muro e tem de ficar.
        op2::createWallOrTube(wx, wy, 0x12);
        const int ew2 = novoEw(wx, wy + 1);
        reserva(ew2);
        op2::killUnit(ew2, 0);
        const bool muroIntacto = temReserva() &&
                                 (op2::g_gameMap.rawCell(wx, wy) & 0x1F) >= 0x17;
        if (!muroIntacto) resOk = false;
        std::printf("[WallTest] controle: muro pronto sobrevive a morte=%d (esperado 1) %s\n",
                    muroIntacto ? 1 : 0, muroIntacto ? "ok" : "FALHOU");

        // (4) o tile encheu na hora de assentar: cancela e DEVOLVE o metal.
        op2::g_gameMap.setRawCell(wx, wy, op2::g_gameMap.rawCell(wx, wy) & 0xFFFF0000u);  // limpa o muro
        op2::g_gameMap.clearUnitOccupancy(wx, wy);
        const int ew3 = novoEw(wx, wy + 1);
        reserva(ew3);
        op2::g_units[ew3].cargo = 0x12;        // muro comum
        op2::g_units[ew3].field23 = 0x0A;      // estado "assenta"
        const int intruso = novoEw(wx + 3, wy + 3);
        op2::g_gameMap.setUnitOccupancy(wx, wy, intruso);   // alguem no tile
        const int oreAntes = op2::g_playerData[0].ore;
        op2::wallBuildTick(ew3);
        const int cell = static_cast<int>(op2::g_gameMap.rawCell(wx, wy) & 0x1F);
        const bool naoAssentou = cell < 0x17 || cell > 0x1A;
        const bool devolveu = op2::g_playerData[0].ore > oreAntes;
        const bool largou = !temReserva();
        if (!naoAssentou || !devolveu || !largou) resOk = false;
        std::printf("[WallTest] tile ocupado: nao assentou=%d devolveu=%d largou a reserva=%d %s\n",
                    naoAssentou ? 1 : 0, devolveu ? 1 : 0, largou ? 1 : 0,
                    (naoAssentou && devolveu && largou) ? "ok" : "FALHOU");
    }

    std::fflush(stdout);
    renderer.shutdown(); SDL_Quit();
    return (fgOk && vizOk && resOk) ? 0 : 1;
}

// --quaketest <map>: terremoto de magnitude 3 no CC da cena demo; imprime o
// dano por unidade e o tremor de tela.
static int runQuakeTest(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 quaketest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    int cc = 0;
    for (size_t i = 1; i < op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapCommandCenter) { cc = (int)i; break; }
    if (!cc) return 3;
    op2::setEarthquake(op2::g_units[cc].pixelX >> 5, op2::g_units[cc].pixelY >> 5, 3);
    int maxShake = 0;
    for (int k = 0; k < 1300; ++k) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        const int s = std::abs(gs.shakeX()) + std::abs(gs.shakeY());
        if (s > maxShake) maxShake = s;
    }
    for (size_t i = 1; i < op2::g_units.size(); ++i) {
        const op2::UnitRecord& r = op2::g_units[i];
        if (r.id == -1 || !(r.flags & op2::UNIT_DAMAGEABLE)) continue;
        std::printf("[QuakeTest] %2d tipo 0x%02X dano=%d%s\n", r.id, r.type, r.damage, (r.flags & op2::UNIT_DEAD) ? " (morto)" : "");
    }
    std::printf("[QuakeTest] tick=%d tremor max=%d msg=%s\n", op2::TethysGame::tick(), maxShake, op2::statusBarText().c_str());
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --disastertest <map> <meteor|lightning|tornado|eruption> <prefix>: desastre
// perto do CC da cena demo; imprime o dano por unidade, a lava e grava quadros
// <prefix>_NN.bmp com a câmera no desastre.
static int runDisasterTest(const std::string& mapName, const std::string& kind, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::g_art.load();
    op2::Renderer renderer;
    if (!renderer.init("OP2 disastertest", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();
    int cc = 0;
    for (size_t i = 1; i < op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && op2::g_units[i].type == op2::mapCommandCenter) { cc = (int)i; break; }
    if (!cc) return 3;
    const int cx = op2::g_units[cc].pixelX >> 5, cy = op2::g_units[cc].pixelY >> 5;
    const int W = op2::g_gameMap.width(), H = op2::g_gameMap.height();
    int type = 0;
    if (kind == "meteor")    { type = 0x50; op2::setMeteor(cx, cy, 2); }
    if (kind == "lightning") { type = 0x4C; op2::setLightning((cx - 6 + W) % W, cy, 3, (cx + 12) % W, cy); }
    if (kind == "tornado")   { type = 0x4D; op2::setTornado((cx - 8 + W) % W, cy, 25, (cx + 12) % W, cy, 1); }
    if (kind == "eruption") {
        type = 0x4F;
        for (int y = cy - 3; y <= cy + 12; ++y)
            for (int x = cx + 2; x <= cx + 16; ++x)
                if (y >= 0 && y < H) op2::setLavaPossible(x % W, y, true);
        op2::setEruption((cx + 8) % W, cy + 4, 40);
    }
    if (!type) return 4;
    auto lavaCells = [&]() {
        int n = 0;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if (op2::g_gameMap.rawCell(x, y) & 0x8000000u) ++n;
        return n;
    };
    std::vector<int> lastDmg(op2::g_units.size(), 0);
    int start = -1, shots = 0, lastX = 0, lastY = 0;
    for (int k = 0; k < 2600; ++k) {
        gs.onIdle(renderer, 0.2501f / 4.0f);
        int did = 0;
        for (size_t i = 1; i < op2::g_units.size(); ++i)
            if (op2::g_units[i].id != -1 && op2::g_units[i].type == type) { did = (int)i; break; }
        if (type == 0x4F && start < 0 && lavaCells() > 0) {        // erupção: a 1ª lava
            start = op2::TethysGame::tick();
            lastX = (((cx + 8) % W) << 5) + 16; lastY = ((cy + 4) << 5) + 16;
            std::printf("[Disaster] lava comecou tick=%d\n", start);
        }
        if (did) {
            const op2::UnitRecord& d = op2::g_units[did];
            lastX = op2::unitOX(d); lastY = op2::unitOY(d);
            if (start < 0 && (d.flags & 0x2000u)) {
                start = op2::TethysGame::tick();
                std::printf("[Disaster] %s comecou tick=%d em (%d,%d) px\n", kind.c_str(), start, lastX, lastY);
            }
        }
        if (lastDmg.size() < op2::g_units.size()) lastDmg.resize(op2::g_units.size(), 0);
        for (size_t i = 1; i < op2::g_units.size(); ++i) {
            const op2::UnitRecord& r = op2::g_units[i];
            if (r.id == -1 || !(r.flags & op2::UNIT_DAMAGEABLE) || r.damage == lastDmg[i]) continue;
            std::printf("[Disaster] t=%d unidade %d tipo 0x%02X dano %d -> %d%s\n", op2::TethysGame::tick(), (int)i,
                        r.type, lastDmg[i], r.damage, (r.flags & op2::UNIT_DEAD) ? " (MORTA)" : "");
            lastDmg[i] = r.damage;
        }
        static int nextSnap = 0;
        if (start >= 0 && nextSnap == 0) nextSnap = start + 4;
        const int every = type == 0x4F ? 120 : type == 0x50 ? 8 : 24;
        if (start >= 0 && shots < 12 && op2::TethysGame::tick() >= nextSnap) {
            nextSnap += every;
            gs.setCamera(lastX - 240, lastY - 220);
            gs.onIdle(renderer, 0.0f);
            char fn[256]; std::snprintf(fn, sizeof(fn), "%s_%02d.bmp", prefix.c_str(), shots++);
            writeBmp32(fn, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
        }
        if (start >= 0 && !did && type != 0x4F && op2::TethysGame::tick() > start + 40) break;
    }
    std::printf("[Disaster] fim tick=%d lava=%d msg=%s\n", op2::TethysGame::tick(), lavaCells(),
                op2::statusBarText().c_str());
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --movelog <map>: manda o 1º veículo andar ~10 células e imprime o
// deslocamento (px) por tick — prova que o deslize é contínuo (sem tick parado).
static int runMoveLog(const std::string& mapName) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 movelog", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    int vid = -1;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && (op2::g_units[i].flags & op2::UNIT_VEHICLE)) { vid = i; break; }
    if (vid < 0) return 3;
    op2::UnitRecord& r = op2::g_units[vid];
    int mask = op2::g_gameMap.width() - 1;
    op2::LOCATION dst = op2::g_gameMap.findPassableNear(
        op2::LOCATION{((r.pixelX >> 5) + 10) & mask, r.pixelY >> 5});
    op2::Unit(vid).DoMove(dst);
    std::printf("[MoveLog] veiculo %d tipo %d -> (%d,%d)\n", vid, r.type, dst.x, dst.y);
    int px = r.pixelX, py = r.pixelY;
    std::string line;
    for (int t = 0; t < 400 && op2::g_units[vid].hasOrder; ++t) {
        op2::tickMovement();
        op2::TethysGame::advanceTick();
        const op2::UnitRecord& u = op2::g_units[vid];
        int dx = u.pixelX - px, dy = u.pixelY - py;
        if (dx > 1000) dx -= op2::g_gameMap.width() * 32;
        if (dx < -1000) dx += op2::g_gameMap.width() * 32;
        char b[32]; std::snprintf(b, sizeof(b), "%d,%d r%d ", dx, dy, u.rotation);
        line += b;
        px = u.pixelX; py = u.pixelY;
    }
    std::printf("[MoveLog] dx,dy por tick: %s\n", line.c_str());
    renderer.shutdown(); SDL_Quit();
    return 0;
}

// --movecap <map> <prefix>: carrega o jogo, manda os veículos do jogador moverem
// e salva frames (prefix_NN.bmp) durante o movimento, para inspeção visual.
static int runMoveCap(const std::string& mapName, const std::string& prefix) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "SDL_Init: " << SDL_GetError() << std::endl; return 1; }
    op2::g_ResManager.init(".");
    op2::Renderer renderer;
    if (!renderer.init("OP2 movecap", 1280, 960, false)) { SDL_Quit(); return 1; }
    op2::GameState gs;
    if (!gs.load(mapName)) { renderer.shutdown(); SDL_Quit(); return 2; }
    gs.onActivate();

    // Destino ~12 células a leste do 1º veículo; manda todos os veículos p/ lá.
    int mask = op2::g_gameMap.width() - 1;
    int refX = 0, refY = 0, found = 0;
    for (int i = 1; i < (int)op2::g_units.size(); ++i)
        if (op2::g_units[i].id != -1 && (op2::g_units[i].flags & op2::UNIT_VEHICLE)) {
            refX = op2::g_units[i].pixelX >> 5; refY = op2::g_units[i].pixelY >> 5; found = 1; break;
        }
    if (found) {
        op2::LOCATION dst = op2::g_gameMap.findPassableNear(op2::LOCATION{ (refX + 12) & mask, refY + 4 });
        dst.x &= mask;
        for (int i = 1; i < (int)op2::g_units.size(); ++i) {
            op2::UnitRecord& r = op2::g_units[i];
            if (r.id == -1 || !(r.flags & op2::UNIT_VEHICLE)) continue;
            op2::Unit(i).DoMove(dst);
        }
        std::cout << "[MoveCap] destino (" << dst.x << "," << dst.y << ")" << std::endl;
    }

    for (int f = 0; f <= 80; ++f) {
        gs.onIdle(renderer, 0.1f);     // 1 tick + render por frame
        if (f == 0 || f == 20 || f == 40 || f == 60 || f == 80) {
            char path[256];
            std::snprintf(path, sizeof(path), "%s_%02d.bmp", prefix.c_str(), f);
            writeBmp32(path, renderer.getFramebuffer(),
                       renderer.getWidth(), renderer.getHeight());
            std::cout << "[MoveCap] frame " << f << " -> " << path << std::endl;
        }
    }
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

// --colltest <map>: verifica colisao com terreno impassavel (pathfinder + passo).
static int runCollTest(const std::string& mapName) {
    if (!op2::g_ResManager.init(".")) return 2;
    auto ms = op2::g_ResManager.openStream(mapName);
    if (!ms || !op2::g_gameMap.load(ms->data(), ms->size())) return 3;
    int W = op2::g_gameMap.width(), H = op2::g_gameMap.height(), mask = W - 1;

    // 1) Conta celulas impassaveis e amostra os cellTypes.
    int imp = 0, pass = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        (op2::g_gameMap.isPassable(op2::LOCATION{x, y}) ? pass : imp)++;
    std::cout << "[COLL] mapa " << W << "x" << H << ": passaveis=" << pass
              << " impassaveis=" << imp << std::endl;

    // 2) pfCanEnter deve ser 0 em TODA celula impassavel e !=0 em passavel.
    op2::resetUnits();
    op2::Unit u; op2::TethysGame::CreateUnit(u, 1, op2::LOCATION{0, 0}, 0);  // veiculo dummy
    op2::pfSetContext(u.id());
    int badBlock = 0, badPass = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        bool p = op2::g_gameMap.isPassable(op2::LOCATION{x, y});
        int ce = op2::pfCanEnter(x, y);
        if (p && ce == 0) ++badPass;        // passavel mas canEnter diz nao (ex.: ocupada)
        if (!p && ce != 0) ++badBlock;      // impassavel mas canEnter deixou entrar (ERRO)
    }
    std::cout << "[COLL] pfCanEnter: impassaveis-que-deixaram-entrar=" << badBlock
              << " (deve ser 0)" << std::endl;

    // 3) Acha um par de celulas passaveis com impassavel no caminho reto e roteia.
    //    Procura uma impassavel, pega passaveis a oeste e leste dela na mesma linha.
    int tested = 0, crossings = 0;
    for (int y = 2; y < H - 2 && tested < 5; ++y) {
        for (int x = 2; x < W - 6 && tested < 5; ++x) {
            if (op2::g_gameMap.isPassable(op2::LOCATION{x, y})) continue;      // acha impassavel
            // start a oeste, dest a leste da barreira, ambos passaveis
            int sx = x - 2, ex = x + 3;
            if (sx < 0 || ex >= W) continue;
            if (!op2::g_gameMap.isPassable(op2::LOCATION{sx, y})) continue;
            if (!op2::g_gameMap.isPassable(op2::LOCATION{ex, y})) continue;
            ++tested;
            op2::pfSetTarget(ex, y);
            op2::SearchContext sc; sc.unitId = u.id();
            sc.startX = sx; sc.startY = y; sc.destX = ex; sc.destY = y;
            int dirs[600]; int reached = 0;
            int n = op2::pfSearchRoute(sc, dirs, 600, &reached);
            // re-executa e confere que NENHUMA celula da rota e impassavel
            int cx = sx, cy = y; bool hitImp = false;
            for (int i = 0; i < n; ++i) {
                cx = (cx + op2::kDir8[dirs[i]].dx) & mask;
                cy = cy + op2::kDir8[dirs[i]].dy;
                if (!op2::g_gameMap.isPassable(op2::LOCATION{cx, cy})) { hitImp = true; break; }
            }
            std::cout << "[COLL] barreira em (" << x << "," << y << "): ("
                      << sx << "," << y << ")->(" << ex << "," << y << ") rota=" << n
                      << " reached=" << reached << " pisou-impassavel=" << (hitImp ? "SIM(ERRO)" : "nao")
                      << std::endl;
            if (!hitImp && n > 5) ++crossings;   // rota mais longa que a reta = desviou
        }
    }
    // 4) PRÉDIO como obstáculo: coloca um prédio e roteia um veículo atravessando-o.
    //    Acha uma área passável 6x6 para o prédio + rota.
    int bx = -1, by = -1;
    for (int y = 4; y < H - 4 && bx < 0; ++y)
        for (int x = 6; x < W - 6 && bx < 0; ++x) {
            bool clear = true;
            for (int dy = -2; dy <= 2 && clear; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    if (!op2::g_gameMap.isPassable(op2::LOCATION{x + dx, y + dy})) { clear = false; break; }
            if (clear) { bx = x; by = y; }
        }
    if (bx >= 0) {
        op2::Unit bld;
        op2::TethysGame::CreateUnit(bld, op2::mapCommandCenter, op2::LOCATION{bx, by}, 0);  // 3x2
        int fw, fh; op2::buildingFootprint(op2::mapCommandCenter, fw, fh);
        int tlx = bx - (fw >> 1), tly = by - (fh >> 1), occOK = 0, occTot = fw * fh;
        for (int dy = 0; dy < fh; ++dy) for (int dx = 0; dx < fw; ++dx)
            if (op2::g_gameMap.cellOccupant(tlx + dx, tly + dy) == bld.id()) ++occOK;
        // veículo atravessando o prédio (oeste -> leste)
        op2::Unit veh; op2::TethysGame::CreateUnit(veh, 1, op2::LOCATION{bx - 4, by}, 0);
        op2::pfSetContext(veh.id());
        op2::pfSetTarget((bx + 4) & mask, by);
        op2::SearchContext sc; sc.unitId = veh.id();
        sc.startX = (bx - 4) & mask; sc.startY = by; sc.destX = (bx + 4) & mask; sc.destY = by;
        int dirs[600]; int reached = 0;
        int n = op2::pfSearchRoute(sc, dirs, 600, &reached);
        int cx = sc.startX, cy = sc.startY; bool hitBld = false;
        for (int i = 0; i < n; ++i) {
            cx = (cx + op2::kDir8[dirs[i]].dx) & mask; cy += op2::kDir8[dirs[i]].dy;
            int occ = op2::g_gameMap.cellOccupant(cx, cy);
            if (occ == bld.id()) { hitBld = true; break; }
        }
        std::cout << "[COLL] predio COMMAND " << fw << "x" << fh << " em (" << bx << "," << by
                  << "): footprint-ocupado=" << occOK << "/" << occTot
                  << "; veiculo atravessou (" << sc.startX << "->" << sc.destX << ",y" << by
                  << ") rota=" << n << " reached=" << reached
                  << " pisou-no-predio=" << (hitBld ? "SIM(ERRO)" : "nao") << std::endl;
    }

    // 5) ESPIRAL: grupo de veiculos -> 1 CommandPacket -> destinos distintos.
    {
        op2::resetUnits();
        // acha area passavel p/ spawnar e p/ o alvo
        int ax = -1, ay = -1;
        for (int y = 6; y < H - 6 && ax < 0; ++y)
            for (int x = 6; x < W - 6 && ax < 0; ++x)
                if (op2::g_gameMap.isPassable(op2::LOCATION{x, y})) { ax = x; ay = y; }
        op2::CommandPacket pkt; pkt.type = op2::CMD_MOVE;
        pkt.waypoints.push_back(op2::packLocation(ax, ay));
        for (int k = 0; k < 6; ++k) {
            op2::Unit v;
            op2::LOCATION sp = op2::g_gameMap.findPassableNear(op2::LOCATION{(ax + 8 + k) & mask, ay});
            op2::TethysGame::CreateUnit(v, 1, sp, 0);
            pkt.unitIds.push_back(v.id());
        }
        op2::processCommandPacket(0, pkt);
        std::cout << "[COLL] espiral: " << pkt.unitIds.size() << " veiculos -> alvo (" << ax << "," << ay << "):";
        std::set<int> cells; int enter = 0;
        for (int id : pkt.unitIds) {
            const op2::UnitRecord& r = op2::g_units[id];
            uint32_t d = r.order.waypoints.empty() ? 0 : r.order.waypoints.back();
            int dx = op2::unpackLocX(d), dy = op2::unpackLocY(d);
            cells.insert(dy * 1000 + dx);
            op2::pfSetContext(id);
            if (op2::pfCanEnter(dx, dy)) ++enter;
            std::cout << " (" << dx << "," << dy << ")";
        }
        std::cout << " | destinos-distintos=" << cells.size() << "/" << pkt.unitIds.size()
                  << " enteraveis=" << enter << "/" << pkt.unitIds.size() << std::endl;
    }

    std::cout << "[COLL] resultado: " << (badBlock == 0 ? "OK terreno bloqueia" : "FALHA")
              << "; desvios validados=" << crossings << std::endl;
    return 0;
}

// --artdump <imgIndex> <out.bmp>: carrega op2_art e grava a imagem em BMP.
static int runArtDump(int idx, const std::string& outPath) {
    if (!op2::g_ResManager.init(".")) return 2;
    if (!op2::g_art.load()) { std::cerr << "[ArtDump] falha ao carregar op2_art" << std::endl; return 3; }
    std::cout << "[ArtDump] " << op2::g_art.numImages() << " imagens, "
              << op2::g_art.numAnimations() << " animacoes" << std::endl;
    if (idx < 0 || idx >= op2::g_art.numImages()) { std::cerr << "indice fora da faixa" << std::endl; return 4; }
    const op2::ArtImage& m = op2::g_art.image(idx);
    std::vector<uint32_t> px;
    op2::g_art.decode(idx, px, /*transparent*/ false);
    // fundo rosa p/ enxergar o recorte
    std::vector<uint32_t> rgb(static_cast<size_t>(m.width) * m.height);
    for (size_t k = 0; k < rgb.size(); ++k) rgb[k] = px[k] ? px[k] : 0xFFFF00FFu;
    writeBmp32(outPath, rgb.data(), m.width, m.height);
    std::cout << "[ArtDump] img " << idx << " " << m.width << "x" << m.height
              << " pal=" << m.pal << " type=" << m.type << " -> " << outPath << std::endl;
    return 0;
}

// --animdump <anim> <frame> <out.bmp>: compoe um frame de animacao (pilha de
// pictures do atlas, posicionadas por x,y na ordem slot) e grava em BMP.
static int runAnimDump(int ai, int fi, const std::string& outPath) {
    if (!op2::g_ResManager.init(".")) return 2;
    if (!op2::g_art.load()) return 3;
    if (ai < 0 || ai >= op2::g_art.numAnimations()) { std::cerr << "anim fora da faixa\n"; return 4; }
    const op2::ArtAnimation& an = op2::g_art.animation(ai);
    if (fi < 0 || fi >= (int)an.frames.size()) { std::cerr << "frame fora da faixa\n"; return 5; }
    const op2::ArtFrame& fr = an.frames[fi];
    // bounding box das pictures
    int minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const auto& p : fr.pics) {
        const op2::ArtImage& m = op2::g_art.image(p.image);
        minX = std::min(minX, (int)p.x); minY = std::min(minY, (int)p.y);
        maxX = std::max(maxX, (int)p.x + (int)m.width); maxY = std::max(maxY, (int)p.y + (int)m.height);
    }
    if (fr.pics.empty()) { minX = minY = 0; maxX = maxY = 1; }
    int W = maxX - minX, H = maxY - minY;
    if (W < 1) W = 1; if (H < 1) H = 1;
    std::vector<uint32_t> canvas((size_t)W * H, 0xFFFF00FFu);   // fundo rosa
    // ordena por slot e blita
    std::vector<op2::ArtPicture> pics = fr.pics;
    std::sort(pics.begin(), pics.end(), [](const op2::ArtPicture& a, const op2::ArtPicture& b){ return a.slot < b.slot; });
    for (const auto& p : pics) {
        const op2::ArtImage& m = op2::g_art.image(p.image);
        std::vector<uint32_t> px; op2::g_art.decode(p.image, px, /*transparent*/ true);
        for (uint32_t y = 0; y < m.height; ++y) for (uint32_t x = 0; x < m.width; ++x) {
            uint32_t c = px[y * m.width + x]; if (!(c >> 24)) continue;   // alpha 0 = pula
            int cx = (p.x - minX) + (int)x, cy = (p.y - minY) + (int)y;
            if (cx >= 0 && cx < W && cy >= 0 && cy < H) canvas[(size_t)cy * W + cx] = c | 0xFF000000u;
        }
    }
    writeBmp32(outPath, canvas.data(), W, H);
    std::cout << "[AnimDump] anim " << ai << " (" << an.frames.size() << " frames, hdr8="
              << an.header[8] << ") frame " << fi << ": " << fr.pics.size() << " pics, "
              << W << "x" << H << " -> " << outPath << std::endl;
    std::cout << "   header[0..8] =";
    for (int k = 0; k < 9; ++k) std::cout << " " << (int32_t)an.header[k];
    std::cout << "   bbox(pics)=(" << minX << "," << minY << ")..(" << maxX << "," << maxY << ")"
              << std::endl;
    for (const auto& p : fr.pics) {
        const op2::ArtImage& m = op2::g_art.image(p.image);
        std::cout << "   pic img=" << p.image << " type=0x" << std::hex << m.type
                  << std::dec << " pal=" << m.pal << " (type&1="
                  << (m.type & 1) << " shadow=" << (int)m.isShadow() << ") "
                  << m.width << "x" << m.height
                  << " stride=" << m.stride << " rowBytes=" << m.rowBytes()
                  << " slot=" << (int)p.slot << " @(" << p.x << "," << p.y << ")" << std::endl;
    }
    return 0;
}

// --contactsheet <start> <count> <out.bmp>: grade com o frame 0 de varias animacoes
// (celula 80x80), p/ identificar visualmente unidades/estruturas e seus indices.
static int runContactSheet(int start, int count, const std::string& outPath) {
    if (!op2::g_ResManager.init(".")) return 2;
    if (!op2::g_art.load()) return 3;
    const int CELL = 80, COLS = 8;
    int rows = (count + COLS - 1) / COLS;
    int W = COLS * CELL, H = rows * CELL;
    std::vector<uint32_t> cv((size_t)W * H, 0xFF202020u);
    for (int k = 0; k < count; ++k) {
        int ai = start + k;
        if (ai < 0 || ai >= op2::g_art.numAnimations()) continue;
        const op2::ArtAnimation& an = op2::g_art.animation(ai);
        if (an.frames.empty()) continue;
        const op2::ArtFrame& fr = an.frames[0];
        // bbox
        int minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto& p : fr.pics) {
            const op2::ArtImage& m = op2::g_art.image(p.image);
            minX = std::min(minX, (int)p.x); minY = std::min(minY, (int)p.y);
            maxX = std::max(maxX, (int)p.x + (int)m.width); maxY = std::max(maxY, (int)p.y + (int)m.height);
        }
        if (fr.pics.empty()) continue;
        int bw = maxX - minX, bh = maxY - minY;
        int cellX = (k % COLS) * CELL, cellY = (k / COLS) * CELL;
        int ox = cellX + (CELL - bw) / 2 - minX, oy = cellY + (CELL - bh) / 2 - minY;
        std::vector<op2::ArtPicture> pics = fr.pics;
        std::sort(pics.begin(), pics.end(), [](const op2::ArtPicture& a, const op2::ArtPicture& b){ return a.slot < b.slot; });
        for (const auto& p : pics) {
            const op2::ArtImage& m = op2::g_art.image(p.image);
            std::vector<uint32_t> px; op2::g_art.decode(p.image, px, true);
            for (uint32_t y = 0; y < m.height; ++y) for (uint32_t x = 0; x < m.width; ++x) {
                uint32_t c = px[y * m.width + x]; if (!(c >> 24)) continue;
                int X = ox + p.x + (int)x, Y = oy + p.y + (int)y;
                if (X >= cellX && X < cellX + CELL && Y >= cellY && Y < cellY + CELL && X < W && Y < H)
                    cv[(size_t)Y * W + X] = c | 0xFF000000u;
            }
        }
    }
    writeBmp32(outPath, cv.data(), W, H);
    std::cout << "[Sheet] anims " << start << ".." << (start + count - 1) << " ("
              << COLS << " por linha) -> " << outPath << std::endl;
    return 0;
}

// --animlist "i1,i2,..." <out.bmp>: grade com o frame 0 das animacoes da lista.
static int runAnimList(const std::string& list, const std::string& outPath) {
    if (!op2::g_ResManager.init(".")) return 2;
    if (!op2::g_art.load()) return 3;
    std::vector<int> idx; { std::string t; for (char c : list) { if (c==',') { idx.push_back(atoi(t.c_str())); t.clear(); } else t+=c; } if(!t.empty()) idx.push_back(atoi(t.c_str())); }
    const int CELL = 90, COLS = 6;
    int rows = ((int)idx.size() + COLS - 1) / COLS;
    int W = COLS * CELL, H = rows * CELL;
    std::vector<uint32_t> cv((size_t)W * H, 0xFF202020u);
    for (size_t k = 0; k < idx.size(); ++k) {
        int ai = idx[k];
        if (ai < 0 || ai >= op2::g_art.numAnimations()) continue;
        const op2::ArtAnimation& an = op2::g_art.animation(ai);
        if (an.frames.empty()) continue;
        const op2::ArtFrame& fr = an.frames[0];
        int minX=1e9,minY=1e9,maxX=-1e9,maxY=-1e9;
        for (auto&p:fr.pics){const auto&m=op2::g_art.image(p.image);minX=std::min(minX,(int)p.x);minY=std::min(minY,(int)p.y);maxX=std::max(maxX,(int)p.x+(int)m.width);maxY=std::max(maxY,(int)p.y+(int)m.height);}
        if (fr.pics.empty()) continue;
        int bw=maxX-minX,bh=maxY-minY,cx=((int)k%COLS)*CELL,cy=((int)k/COLS)*CELL;
        int ox=cx+(CELL-bw)/2-minX,oy=cy+(CELL-bh)/2-minY;
        std::vector<op2::ArtPicture> pics=fr.pics; std::sort(pics.begin(),pics.end(),[](const op2::ArtPicture&a,const op2::ArtPicture&b){return a.slot<b.slot;});
        for (auto&p:pics){const auto&m=op2::g_art.image(p.image);std::vector<uint32_t> px;op2::g_art.decode(p.image,px,true);
            for(uint32_t y=0;y<m.height;++y)for(uint32_t x=0;x<m.width;++x){uint32_t c=px[y*m.width+x];if(!(c>>24))continue;int X=ox+p.x+(int)x,Y=oy+p.y+(int)y;if(X>=cx&&X<cx+CELL&&Y>=cy&&Y<cy+CELL&&X<W&&Y<H)cv[(size_t)Y*W+X]=c|0xFF000000u;}}
    }
    writeBmp32(outPath, cv.data(), W, H);
    std::cout << "[AnimList] " << idx.size() << " anims (" << COLS << "/linha) -> " << outPath << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    // Saída sem buffer: os modos de teste rodam com stdout em pipe e um crash
    // não pode engolir o log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Modos utilitários (sem o loop interativo).
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--harness") == 0)
            return op2::runHarness(argc, argv);
        if (std::strcmp(argv[i], "--extract") == 0 && i + 2 < argc)
            return runExtract(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--rendermap") == 0 && i + 2 < argc)
            return runRenderMap(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            return runGameView(argv[i + 1]);
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            return runScreenshot(argc, argv, argv[i + 1]);
        if (std::strcmp(argv[i], "--rngtest") == 0 && i + 2 < argc)
            return runRngTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--playertest") == 0)
            return runPlayerTest();
        if (std::strcmp(argv[i], "--unittest") == 0)
            return runUnitTest();
        if (std::strcmp(argv[i], "--aitest") == 0 && i + 1 < argc)
            return runAiTest(argv[i + 1], i + 2 < argc ? argv[i + 2] : "");
        if (std::strcmp(argv[i], "--takeovertest") == 0 && i + 1 < argc)
            return runTakeOverTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--idletest") == 0 && i + 1 < argc)
            return runIdleTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--demotest") == 0)
            return runDemoTest(i + 1 < argc ? argv[i + 1] : "");
        if (std::strcmp(argv[i], "--helptest") == 0)
            return runHelpTest(i + 1 < argc ? argv[i + 1] : "");
        if (std::strcmp(argv[i], "--uitest") == 0)
            return runUiTest();
        if (std::strcmp(argv[i], "--mpui") == 0 && i + 1 < argc)
            return runMpUi(argv[i + 1], i + 2 < argc ? argv[i + 2] : "PLAYER");
        if (std::strcmp(argv[i], "--mplan") == 0 && i + 3 < argc)
            return runMpLan(argv[i + 1], argv[i + 2], atoi(argv[i + 3]),
                            i + 4 < argc ? atoi(argv[i + 4]) : 2);
        if (std::strcmp(argv[i], "--mplobby") == 0 && i + 1 < argc)
            return runMpLobby(argc, argv, i);
        if (std::strcmp(argv[i], "--mpnet") == 0 && i + 3 < argc)
            return runMpNet(argv[i + 1], argv[i + 2], atoi(argv[i + 3]),
                            i + 4 < argc ? atoi(argv[i + 4]) : 47800,
                            i + 5 < argc ? argv[i + 5] : "127.0.0.1");
        if (std::strcmp(argv[i], "--mptest") == 0 && i + 1 < argc)
            return runMpTest(argv[i + 1], i + 2 < argc ? atoi(argv[i + 2]) : 200);
        if (std::strcmp(argv[i], "--nightshot") == 0 && i + 2 < argc) {
            std::vector<int> niveis;
            for (int k = i + 3; k < argc; ++k) niveis.push_back(atoi(argv[k]));
            if (niveis.empty()) { niveis.push_back(100); niveis.push_back(op2::nightDarkness()); }
            return runNightShot(argv[i + 1], argv[i + 2], niveis);
        }
        if (std::strcmp(argv[i], "--playshot") == 0 && i + 2 < argc) {
            bool pass = (i + 3 < argc && std::strcmp(argv[i + 3], "pass") == 0);
            return runPlayShot(argv[i + 1], argv[i + 2], pass,
                               i + 4 < argc ? atoi(argv[i + 4]) : -1);
        }
        if (std::strcmp(argv[i], "--scoretest") == 0) return runScoreTest();
        if (std::strcmp(argv[i], "--fonttest") == 0) return runFontTest();
        if (std::strcmp(argv[i], "--minishot") == 0 && i + 3 < argc)
            return runMiniShot(argv[i + 1], argv[i + 2], atoi(argv[i + 3]),
                               i + 4 < argc && std::strcmp(argv[i + 4], "tac") == 0,
                               i + 5 < argc ? atoi(argv[i + 5]) : 0);
        if (std::strcmp(argv[i], "--globeshot") == 0 && i + 3 < argc)
            return runGlobeShot(argv[i + 1], argv[i + 2], atoi(argv[i + 3]),
                                i + 4 < argc ? (int)std::strtol(argv[i + 4], nullptr, 0) : 0,
                                i + 5 < argc && std::strcmp(argv[i + 5], "tac") == 0,
                                i + 6 < argc ? (int)std::strtol(argv[i + 6], nullptr, 0) : 0);
        if (std::strcmp(argv[i], "--placeshot") == 0 && i + 3 < argc)
            return runPlaceShot(argv[i + 1], argv[i + 2], (int)std::strtol(argv[i + 3], nullptr, 0),
                                i + 4 < argc ? atoi(argv[i + 4]) : 0,
                                i + 5 < argc ? atoi(argv[i + 5]) : 0);
        if (std::strcmp(argv[i], "--selshot") == 0 && i + 2 < argc)
            return runSelShot(argv[i + 1], argv[i + 2], i + 3 < argc && std::strcmp(argv[i + 3], "hover") == 0);
        if (std::strcmp(argv[i], "--structtest") == 0 && i + 2 < argc)
            return runStructTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--paneshot") == 0 && i + 2 < argc)
            return runPaneShot(argv[i + 1], argv[i + 2], i + 3 < argc ? atoi(argv[i + 3]) : 0);
        if (std::strcmp(argv[i], "--cmdshot") == 0 && i + 3 < argc)
            return runCmdShot(argv[i + 1], argv[i + 2], (int)std::strtol(argv[i + 3], nullptr, 0),
                              i + 4 < argc ? atoi(argv[i + 4]) : 1, i + 5 < argc ? (int)std::strtol(argv[i + 5], nullptr, 0) : 0);
        if (std::strcmp(argv[i], "--cmdtest") == 0 && i + 1 < argc)
            return runCmdTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--gptest") == 0 && i + 3 < argc)
            return runGuardPostTest(argv[i + 1], argv[i + 2], (int)std::strtol(argv[i + 3], nullptr, 16));
        if (std::strcmp(argv[i], "--salvagetest") == 0 && i + 1 < argc)
            return runSalvageTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--panetest") == 0 && i + 2 < argc)
            return runPaneTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--musictest") == 0 && i + 1 < argc)
            return runMusicTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--optmenutest") == 0 && i + 1 < argc)
            return runOptMenuTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--pausetest") == 0 && i + 1 < argc)
            return runPauseTest(argv[i + 1], i + 2 < argc ? argv[i + 2] : "");
        if (std::strcmp(argv[i], "--bannertest") == 0 && i + 1 < argc)
            return runBannerTest(argv[i + 1], i + 2 < argc ? argv[i + 2] : "");
        if (std::strcmp(argv[i], "--item21test") == 0 && i + 1 < argc)
            return runItem21Test(argv[i + 1]);
        if (std::strcmp(argv[i], "--cheattest") == 0 && i + 1 < argc)
            return runCheatTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--deletetest") == 0 && i + 1 < argc)
            return runDeleteTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--alerttest") == 0 && i + 1 < argc)
            return runAlertTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--garagecounttest") == 0 && i + 1 < argc)
            return runGarageCountTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--garagetest") == 0 && i + 1 < argc)
            return runGarageTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--mdtest") == 0 && i + 1 < argc)
            return runMdTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--cursortest") == 0 && i + 1 < argc)
            return runCursorTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--anitest") == 0)
            return runAniTest();
        if (std::strcmp(argv[i], "--selecttest") == 0 && i + 1 < argc)
            return runSelectTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--scrolltest") == 0 && i + 1 < argc)
            return runScrollTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--walltest") == 0 && i + 1 < argc)
            return runWallTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--quaketest") == 0 && i + 1 < argc)
            return runQuakeTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--disastertest") == 0 && i + 3 < argc)
            return runDisasterTest(argv[i + 1], argv[i + 2], argv[i + 3]);
        if (std::strcmp(argv[i], "--transfertest") == 0 && i + 1 < argc)
            return runTransferTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--chattest") == 0 && i + 1 < argc)
            return runChatTest(argv[i + 1], i + 2 < argc ? argv[i + 2] : nullptr);
        if (std::strcmp(argv[i], "--savetest") == 0 && i + 3 < argc)
            return runSaveTest(argv[i + 1], atoi(argv[i + 2]), atoi(argv[i + 3]));
        if (std::strcmp(argv[i], "--campaigntest") == 0 && i + 3 < argc)
            return runCampaignTest(argv[i + 1], argv[i + 2], atoi(argv[i + 3]));
        if (std::strcmp(argv[i], "--routetest") == 0 && i + 5 < argc)
            return runRouteTest(argv[i + 1], atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4]), atoi(argv[i + 5]));
        if (std::strcmp(argv[i], "--aitest") == 0 && i + 4 < argc)
            return runAiTest(argv[i + 1], atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4]));
        if (std::strcmp(argv[i], "--pinwheeltest") == 0 && i + 3 < argc)
            return runPinwheelTest(argv[i + 1], atoi(argv[i + 2]), atoi(argv[i + 3]));
        if (std::strcmp(argv[i], "--dockclicktest") == 0 && i + 1 < argc)
            return runDockClickTest(argv[i + 1], i + 2 < argc ? (int)std::strtol(argv[i + 2], nullptr, 16) : 0x27);
        if (std::strcmp(argv[i], "--missiontest") == 0 && i + 2 < argc)
            return runMissionTest(argv[i + 1], argv[i + 2], i + 3 < argc ? atoi(argv[i + 3]) : 200,
                                  i + 5 < argc ? atoi(argv[i + 4]) : -1,
                                  i + 5 < argc ? atoi(argv[i + 5]) : -1);
        if (std::strcmp(argv[i], "--combattest") == 0 && i + 6 < argc) {
            const int wp[4] = {(int)std::strtol(argv[i + 3], nullptr, 16), (int)std::strtol(argv[i + 4], nullptr, 16),
                               (int)std::strtol(argv[i + 5], nullptr, 16), (int)std::strtol(argv[i + 6], nullptr, 16)};
            return runCombatTest(argv[i + 1], argv[i + 2], wp);
        }
        if (std::strcmp(argv[i], "--combattest") == 0 && i + 2 < argc)
            return runCombatTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--earthtest") == 0 && i + 2 < argc)
            return runEarthTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--oretest") == 0 && i + 2 < argc)
            return runOreTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--producetest") == 0 && i + 2 < argc)
            return runProduceTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--researchtest") == 0 && i + 2 < argc)
            return runResearchTest(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--colonytest") == 0 && i + 1 < argc)
            return runColonyTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--movelog") == 0 && i + 1 < argc)
            return runMoveLog(argv[i + 1]);
        if (std::strcmp(argv[i], "--movecap") == 0 && i + 2 < argc)
            return runMoveCap(argv[i + 1], argv[i + 2]);
        if (std::strcmp(argv[i], "--colltest") == 0 && i + 1 < argc)
            return runCollTest(argv[i + 1]);
        if (std::strcmp(argv[i], "--artdump") == 0 && i + 2 < argc)
            return runArtDump(atoi(argv[i + 1]), argv[i + 2]);
        if (std::strcmp(argv[i], "--animdump") == 0 && i + 3 < argc)
            return runAnimDump(atoi(argv[i + 1]), atoi(argv[i + 2]), argv[i + 3]);
        if (std::strcmp(argv[i], "--contactsheet") == 0 && i + 3 < argc)
            return runContactSheet(atoi(argv[i + 1]), atoi(argv[i + 2]), argv[i + 3]);
        if (std::strcmp(argv[i], "--animlist") == 0 && i + 2 < argc)
            return runAnimList(argv[i + 1], argv[i + 2]);
    }

    // --play <map>: entra direto no jogo com o mapa (via o backbone TApp).
    // --mission <DLL>: entra direto numa missão (ex.: T01.DLL).
    std::string initialMap, initialMission;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--play") == 0 && i + 1 < argc) initialMap = argv[i + 1];
        if (std::strcmp(argv[i], "--mission") == 0 && i + 1 < argc) initialMission = argv[i + 1];
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  Outpost 2: Divided Destiny Modern Port " << std::endl;
    std::cout << "  Engine Version: 1.0.0 (MSVC + SDL2)   " << std::endl;
    std::cout << "========================================" << std::endl;

    // Backbone TApp (asm): Init -> CreateFrames -> Run -> ShutDown.
    if (!op2::gTApp.init())                   { op2::gTApp.shutdown(); return 1; }
    if (!op2::gTApp.createFrames(initialMap, initialMission)) { op2::gTApp.shutdown(); return 1; }
    op2::gTApp.run();
    op2::gTApp.shutdown();
    return 0;
}

// (código antigo abaixo removido: o loop interativo agora vive em TApp::run)
#if 0
int main_old(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {}

    std::cout << "========================================" << std::endl;
    std::cout << "  Outpost 2: Divided Destiny Modern Port " << std::endl;
    std::cout << "  Engine Version: 1.0.0 (MSVC + SDL2)   " << std::endl;
    std::cout << "========================================" << std::endl;

    // Carrega as configurações do jogo (OUTPOST2.INI), como o executável original.
    op2::Config config("OUTPOST2.INI");
    op2::GameSettings settings;
    if (config.load()) {
        settings.loadFrom(config);
        std::cout << "[Config] OUTPOST2.INI carregado (jogador='" << settings.name
                  << "', GameSpeed=" << settings.gameSpeed
                  << ", ScrollRate=" << settings.scrollRate << ")" << std::endl;
    } else {
        std::cout << "[Config] OUTPOST2.INI não encontrado; usando defaults." << std::endl;
    }

    // Gateway de assets (VOLs + arquivos soltos), como o g_ResManager original.
    op2::g_ResManager.init(".");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "Failed to initialize SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }

    op2::Renderer renderer;
    // 640x480 native, scaled up to 960x720 (1.5x) or 1280x960 (2x) window for crisp modern displays
    if (!renderer.init("Outpost 2: Divided Destiny", 1280, 960, false)) {
        std::cerr << "Failed to initialize Renderer!" << std::endl;
        SDL_Quit();
        return 1;
    }

    op2::ShellMenu shellMenu;
    if (!shellMenu.init("OP2SHRES.DLL")) {
        std::cerr << "Warning: OP2SHRES.DLL resources could not be loaded directly." << std::endl;
    }

    // Check for --screenshot mode
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--tutorials") {
            shellMenu.showTutorials();
        }
        if (std::string(argv[i]) == "--menu") {
            shellMenu.showMainMenu();
        }
        if (std::string(argv[i]) == "--campaign") shellMenu.showNewCampaign();
        if (std::string(argv[i]) == "--prefs")    shellMenu.showPreferences();
        if (std::string(argv[i]) == "--colony")   shellMenu.showColonyGames();
        if (std::string(argv[i]) == "--about")    shellMenu.showAbout();
        if (std::string(argv[i]) == "--screenshot" && i + 1 < argc) {
            std::string outPath = argv[i + 1];
            renderer.clear(0xFF000000);
            shellMenu.render(renderer);
            
            // Save framebuffer as 32-bit BMP
            FILE* f = fopen(outPath.c_str(), "wb");
            if (f) {
                int w = op2::Renderer::NATIVE_WIDTH;
                int h = op2::Renderer::NATIVE_HEIGHT;
                uint8_t bmpHdr[54] = {
                    'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0,
                    40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 32,0,
                    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
                };
                uint32_t fileSize = 54 + w * h * 4;
                memcpy(&bmpHdr[2], &fileSize, 4);
                memcpy(&bmpHdr[18], &w, 4);
                int negH = -h; // top-down
                memcpy(&bmpHdr[22], &negH, 4);
                fwrite(bmpHdr, 1, 54, f);
                fwrite(renderer.getFramebuffer(), 4, w * h, f);
                fclose(f);
                std::cout << "[Screenshot] Saved screenshot to: " << outPath << std::endl;
            }
            renderer.shutdown();
            SDL_Quit();
            return 0;
        }
    }

    bool running = true;
    uint32_t lastTicks = SDL_GetTicks();

    while (running && !shellMenu.shouldQuit()) {
        uint32_t currentTicks = SDL_GetTicks();
        float dt = (currentTicks - lastTicks) / 1000.0f;
        lastTicks = currentTicks;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEMOTION: {
                    int nx, ny;
                    renderer.screenToNative(event.motion.x, event.motion.y, nx, ny);
                    shellMenu.handleMouseMove(nx, ny);
                    break;
                }

                case SDL_MOUSEBUTTONDOWN: {
                    int nx, ny;
                    renderer.screenToNative(event.button.x, event.button.y, nx, ny);
                    shellMenu.handleMouseDown(nx, ny, event.button.button);
                    break;
                }

                case SDL_MOUSEBUTTONUP: {
                    int nx, ny;
                    renderer.screenToNative(event.button.x, event.button.y, nx, ny);
                    shellMenu.handleMouseUp(nx, ny, event.button.button);
                    break;
                }

                case SDL_MOUSEWHEEL:
                    shellMenu.handleMouseWheel(event.wheel.y);
                    break;

                case SDL_KEYDOWN:
                    shellMenu.handleKeyDown(event.key.keysym.sym);
                    break;

                default:
                    break;
            }
        }

        shellMenu.update(dt);

        renderer.clear(0xFF000000);
        shellMenu.render(renderer);
        renderer.present();

        // Target ~60 FPS cap
        uint32_t frameElapsed = SDL_GetTicks() - currentTicks;
        if (frameElapsed < 16) {
            SDL_Delay(16 - frameElapsed);
        }
    }

    std::cout << "[Main] Exiting cleanly." << std::endl;
    renderer.shutdown();
    SDL_Quit();
    return 0;
}
#endif // 0
