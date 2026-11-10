#include "config.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>

namespace op2 {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

Config::Config(std::string path) : m_path(std::move(path)) {}

bool Config::iEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

Config::Section* Config::findSection(const std::string& name) {
    for (auto& s : m_sections)
        if (iEquals(s.name, name)) return &s;
    return nullptr;
}

const Config::Section* Config::findSection(const std::string& name) const {
    for (const auto& s : m_sections)
        if (iEquals(s.name, name)) return &s;
    return nullptr;
}

Config::Section& Config::ensureSection(const std::string& name) {
    if (Section* s = findSection(name)) return *s;
    m_sections.push_back(Section{name, {}});
    return m_sections.back();
}

bool Config::load(const std::string& path) {
    m_path = path;
    return load();
}

bool Config::load() {
    m_sections.clear();
    std::ifstream f(m_path);
    if (!f) return false;

    Section* cur = nullptr;
    std::string line;
    while (std::getline(f, line)) {
        // remove CR de finais de linha do Windows
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;

        if (t.front() == '[' && t.back() == ']') {
            std::string name = trim(t.substr(1, t.size() - 2));
            cur = &ensureSection(name);
            continue;
        }

        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        if (key.empty()) continue;
        if (!cur) cur = &ensureSection("");  // chaves sem seção
        cur->entries.push_back(Entry{key, val});
    }
    return true;
}

bool Config::saveAs(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    for (size_t i = 0; i < m_sections.size(); ++i) {
        const Section& s = m_sections[i];
        if (!s.name.empty())
            f << '[' << s.name << "]\r\n";
        for (const auto& e : s.entries)
            f << e.key << '=' << e.value << "\r\n";
        f << "\r\n";  // linha em branco entre seções (como o INI original)
    }
    return true;
}

bool Config::save() const {
    return saveAs(m_path);
}

int Config::getInt(const std::string& section, const std::string& key, int def) const {
    const Section* s = findSection(section);
    if (!s) return def;
    for (const auto& e : s->entries) {
        if (iEquals(e.key, key)) {
            // semântica do GetPrivateProfileInt: converte os dígitos iniciais
            const char* p = e.value.c_str();
            char* end = nullptr;
            long v = std::strtol(p, &end, 10);
            if (end == p) return def;  // não era número
            return static_cast<int>(v);
        }
    }
    return def;
}

std::string Config::getString(const std::string& section, const std::string& key,
                              const std::string& def) const {
    const Section* s = findSection(section);
    if (!s) return def;
    for (const auto& e : s->entries)
        if (iEquals(e.key, key)) return e.value;
    return def;
}

void Config::setString(const std::string& section, const std::string& key,
                       const std::string& value) {
    Section& s = ensureSection(section);
    for (auto& e : s.entries) {
        if (iEquals(e.key, key)) {  // atualiza no lugar
            e.value = value;
            return;
        }
    }
    s.entries.push_back(Entry{key, value});  // cria a chave
}

void Config::setInt(const std::string& section, const std::string& key, int value) {
    setString(section, key, std::to_string(value));
}

void Config::setIntPath(const std::string& sectionKeyPath, int value) {
    // Divide em "Seção\Chave" ou "Seção/Chave" (delimitadores do CConfig original).
    size_t pos = sectionKeyPath.find_first_of("\\/");
    if (pos == std::string::npos) {
        setInt("", sectionKeyPath, value);
        return;
    }
    setInt(sectionKeyPath.substr(0, pos), sectionKeyPath.substr(pos + 1), value);
}

bool Config::hasKey(const std::string& section, const std::string& key) const {
    const Section* s = findSection(section);
    if (!s) return false;
    for (const auto& e : s->entries)
        if (iEquals(e.key, key)) return true;
    return false;
}

// ---- GameSettings ----

void GameSettings::loadFrom(const Config& cfg) {
    music             = cfg.getInt("Game", "Music", music);
    musicVol          = cfg.getInt("Game", "MusicVol", musicVol);
    sound             = cfg.getInt("Game", "Sound", sound);
    soundVol          = cfg.getInt("Game", "SoundVol", soundVol);
    gameSpeed         = cfg.getInt("Game", "GameSpeed", gameSpeed);
    scrollRate        = cfg.getInt("Game", "ScrollRate", scrollRate);
    overlay           = cfg.getInt("Game", "Overlay", overlay);
    ambient           = cfg.getInt("Game", "Ambient", ambient);
    stationaryShadows = cfg.getInt("Game", "StationaryShadows", stationaryShadows);
    mobileShadows     = cfg.getInt("Game", "MobileShadows", mobileShadows);
    frameSkip         = cfg.getInt("Game", "FrameSkip", 1);
    if (static_cast<unsigned>(frameSkip) > 2u) frameSkip = 2;
    introAVI          = cfg.getInt("Game", "IntroAVI", introAVI);
    showMovies        = cfg.getInt("Game", "ShowMovies", showMovies);
    spiffyDraw        = cfg.getInt("Game", "SpiffyDraw", spiffyDraw);
    demo              = cfg.getInt("Game", "Demo", demo);
    aiLevel           = cfg.getInt("Game", "AILevel", aiLevel);
    nightDarkness     = cfg.getInt("Game", "NightDarkness", nightDarkness);
    audioVolume       = cfg.getInt("Game", "AudioVolume", audioVolume);
    demoCamera        = cfg.getInt("Game", "DemoCamera", demoCamera);
    fullscreen        = cfg.getInt("Game", "Fullscreen", fullscreen);
    menuSkin          = cfg.getInt("Game", "MenuSkin", menuSkin);
    mapWrap           = cfg.getInt("Game", "MapWrap", mapWrap);
    showBottomRow     = cfg.getInt("Game", "ShowBottomRow", showBottomRow);
    name              = cfg.getString("Game", "Name", name);

    miniMapButtons    = cfg.getInt("WindowInfo", "MiniMapButtons", miniMapButtons);
    desktopWidth      = cfg.getInt("DESKTOP", "H_SIZE", desktopWidth);
    desktopHeight     = cfg.getInt("DESKTOP", "V_SIZE", desktopHeight);
}

void GameSettings::saveTo(Config& cfg) const {
    cfg.setInt("Game", "Music", music);
    cfg.setInt("Game", "MusicVol", musicVol);
    cfg.setInt("Game", "Sound", sound);
    cfg.setInt("Game", "SoundVol", soundVol);
    cfg.setInt("Game", "GameSpeed", gameSpeed);
    cfg.setInt("Game", "ScrollRate", scrollRate);
    cfg.setInt("Game", "Overlay", overlay);
    cfg.setInt("Game", "Ambient", ambient);
    cfg.setInt("Game", "StationaryShadows", stationaryShadows);
    cfg.setInt("Game", "MobileShadows", mobileShadows);
    cfg.setInt("Game", "IntroAVI", introAVI);
    cfg.setInt("Game", "ShowMovies", showMovies);
    cfg.setInt("Game", "SpiffyDraw", spiffyDraw);
    cfg.setInt("Game", "Demo", demo);
    cfg.setInt("Game", "AILevel", aiLevel);
    cfg.setInt("Game", "NightDarkness", nightDarkness);
    cfg.setInt("Game", "AudioVolume", audioVolume);
    cfg.setInt("Game", "DemoCamera", demoCamera);
    cfg.setInt("Game", "Fullscreen", fullscreen);
    cfg.setInt("Game", "MenuSkin", menuSkin);
    cfg.setInt("Game", "MapWrap", mapWrap);
    cfg.setInt("Game", "ShowBottomRow", showBottomRow);
    cfg.setString("Game", "Name", name);

    cfg.setInt("WindowInfo", "MiniMapButtons", miniMapButtons);
    cfg.setInt("DESKTOP", "H_SIZE", desktopWidth);
    cfg.setInt("DESKTOP", "V_SIZE", desktopHeight);
}

} // namespace op2
