#pragma once

#include <string>
#include <vector>

namespace op2 {

// Leitor/gravador do OUTPOST2.INI, equivalente à classe CConfig do jogo original
// (que era um wrapper sobre GetPrivateProfileInt/String + WritePrivateProfileString
// do Windows). Esta versão é cross-platform e reproduz a mesma semântica:
//   * nomes de seção e chave são case-INSENSITIVE (como no Windows);
//   * getInt lê os dígitos iniciais do valor e cai no default se a chave faltar;
//   * setInt/setString criam a seção/chave se não existirem, ou atualizam no lugar.
// As seções/chaves usadas pelo jogo estão em GameSettings (abaixo).
class Config {
public:
    Config() = default;
    explicit Config(std::string path);

    // Carrega o arquivo INI. Retorna false se não conseguir abrir (mantém vazio,
    // o que é aceitável: os getters devolvem os defaults).
    bool load();
    bool load(const std::string& path);

    // Grava o INI de volta no arquivo (formato compatível com o original).
    bool save() const;
    bool saveAs(const std::string& path) const;

    int getInt(const std::string& section, const std::string& key, int def = 0) const;
    std::string getString(const std::string& section, const std::string& key,
                          const std::string& def = "") const;

    void setInt(const std::string& section, const std::string& key, int value);
    void setString(const std::string& section, const std::string& key,
                   const std::string& value);

    // Equivale a CConfig::SetInt: aceita "Seção\\Chave" ou "Seção/Chave".
    void setIntPath(const std::string& sectionKeyPath, int value);

    bool hasKey(const std::string& section, const std::string& key) const;
    const std::string& path() const { return m_path; }

private:
    struct Entry {
        std::string key;
        std::string value;
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };

    std::vector<Section> m_sections;
    std::string m_path;

    Section* findSection(const std::string& name);
    const Section* findSection(const std::string& name) const;
    Section& ensureSection(const std::string& name);
    static bool iEquals(const std::string& a, const std::string& b);
};

// Configurações do jogo (seção [Game] e afins), com os defaults do OUTPOST2.INI.
// Espelha exatamente as chaves lidas pelo executável original.
struct GameSettings {
    // [Game]
    int music = 1;
    int musicVol = 1;
    int sound = 1;
    int soundVol = 0;
    int gameSpeed = 5;
    int scrollRate = 4;
    int overlay = 1;
    int ambient = 1;
    int stationaryShadows = 0;
    int mobileShadows = 0;
    int frameSkip = 1;          // "FrameSkip" (1..2; sub_49AD05) — fase do tremor de tela
    int introAVI = 1;
    int showMovies = 1;
    int spiffyDraw = 1;
    int demo = 1;
    std::string name = "";
    // "AILevel": acrescimo DESTE porte, nao existe no INI original (o jogo so
    // escolhe o nivel ao comecar: Dialog 140 na campanha, o combo Resources no
    // multijogador). 0 = AUTO, que deixa valer o que a partida escolheu; 1..3
    // forcam Easy/Normal/Hard. O original ignora chaves que nao conhece.
    int aiLevel = 0;
    // "NightDarkness": tambem so deste porte. Quanto da curva de escurecimento
    // noturno do original se aplica, em porcento (100 = o jogo tal como e,
    // 0 = sem noite). Ver op2::setNightDarkness.
    int nightDarkness = 75;
    // "AudioVolume": idem, 0..100 por cento. Volume MESTRE, que multiplica a
    // mistura final (sons e musica juntos) sem mexer nas atenuacoes do
    // original -- o jogo so tem MusicVol/SoundVol, cada um em 6 degraus e cada
    // um no seu canal, e nada que regule o conjunto. Ver soundSetMasterVolume.
    int audioVolume = 50;
    // "DemoCamera": idem, so deste porte. 1 = no demo de atracao a camera
    // passeia pelos pontos de acao; 0 = fica parada onde o InitProc do ademo
    // a pos, que e o comportamento do original. Ver GameState::demoCameraTick.
    int demoCamera = 1;
    // "Fullscreen": idem, so deste porte. 1 = a janela abre em tela cheia
    // (FULLSCREEN_DESKTOP: sem troca de modo de video, a vista de 640x480
    // escalada mantendo a proporcao); 0 = janela. Alt+Enter alterna em
    // qualquer um dos dois, e o botao de maximizar da barra de titulo tambem.
    // Tambem da para escolher pelo jogo: o par de radio "Screen Mode" no fim
    // da lista do PREFERENCES (Options > PREFERENCES), que grava aqui no
    // "&APPLY" -- ver GameOptionsPane e GameState::applyScreenMode.
    int fullscreen = 1;
    // Fork: "MenuSkin". 0 = the port's own menu look (dark blue panels, the
    // main menu on the right); 1 = the original OP2 dialog skin (blueprint
    // background, red frames, green text -- see DialogSkin).
    int menuSkin = 0;
    // Fork: "MapWrap". 0 = like the original: the map view stops at the map's
    // left/right edges; only "around the world" maps (512 tiles wide) wrap.
    // 1 = every map wraps horizontally (the port's old behaviour).
    int mapWrap = 0;
    // Fork: "ShowBottomRow". 0 = like the original: the view stops at the
    // bottom of the map's clip rect, so the map's last tile row is hidden.
    // 1 = the view can scroll down to show that row.
    int showBottomRow = 0;

    // [WindowInfo]
    int miniMapButtons = 0;

    // [DESKTOP]
    int desktopWidth = 640;
    int desktopHeight = 480;

    // Lê de um Config já carregado (usa os defaults acima quando a chave falta).
    void loadFrom(const Config& cfg);
    // Escreve de volta no Config (para depois chamar Config::save()).
    void saveTo(Config& cfg) const;
};

} // namespace op2
