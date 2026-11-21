// --harness: plays a scenario headless, tick by tick, and writes what happened in the
// file formats of the op2-rs test harness (gitlab.com/bufke/op2-rs, harness/), so the
// port can be compared field by field with the original game playing the same recipe.
//
//   outpost2_modern.exe --harness --script S --out DIR --map cm01.map --ticks N
//                       [--seed N] [--techtree multitek.txt] [--trace-every N]
//                       [--shot-every N] [--telemetry-every N] [--eco] [--audio] [--size WxH] [--show]
//
// The script is the harness's: one `<tick> <verb> <args...>` per line, '#' starts a
// comment; lines at tick T run after the cycle of tick T, as the harness runs them
// (the first cycle is tick 1, so tick 0 lines run there). Coordinates are "file"
// tiles after `coords file` (the port's own tiles) or the original's internal tiles
// otherwise. A unit is `@N`, the Nth spawn, or a unit id. The mouse and keyboard verbs
// (click, drag, key, type, select...) are real input to the game's UI (see "Mouse and
// keyboard" below); --size 1056x800 gives the original's 1032x766 client for them.
//
// Outputs in DIR:
//   trace.jsonl   at tick 1 and every --trace-every ticks, plus the end: each spawn still
//                 alive with the harness's fields (position, command and action, path,
//                 weapon, status effects, tiles), the spawns' weapon fire (fx), every
//                 other live object (ents), the object allocator (alloc) and the RNG;
//                 the harness's frame/anim and the alloc ring's [head, tail] aren't modeled
//   telemetry.jsonl  with --telemetry-every N: each player's colony and every building and
//                 vehicle at tick 1 and every N ticks, plus the end, as the harness writes it;
//                 --eco adds the colony internals and the mining beacons
//   gamedata.json at the first cycle: the map geometry, the cell types' speeds, every unit
//                 type's stats, the cell type of every tile; --eco adds the morale, mine
//                 and tech tables (the harness's start_info.json is a network game's, which
//                 the port doesn't play)
//   audio.jsonl   with --audio: every sound the game asks for (see "Audio log" below)
//   harness.log   what ran, spawns, unsupported verbs, the end of the run
//   shot_<tick>.bmp  the game view every --shot-every ticks and at each `shot` verb
//
// The start is the harness's scenario mission (its zScenario.dll), built in: colony
// type, 2 players (0 Eden human, 1 Plymouth computer), tech level 12, daylight
// everywhere. The DLL itself can't run here: it calls Outpost2.exe by address.
#include "harness_mode.h"

#include <SDL.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app/game_state.h"
#include "game/ai_group.h"
#include "game/building_sheet.h"
#include "game/colony.h"
#include "game/disaster.h"
#include "game/docking.h"
#include "game/messages.h"
#include "game/mining.h"
#include "game/missile.h"
#include "game/research.h"
#include "game/trigger.h"
#include "game/walls.h"
#include "game/weapon.h"
#include "game/terrain_cost.h"
#include "game/command.h"
#include "game/game_map.h"
#include "game/player.h"
#include "game/scenario.h"
#include "game/tethys_game.h"
#include "game/unit.h"
#include "game/unit_names.h"
#include "audio/music.h"
#include "audio/sound.h"
#include "gfx/artfile.h"
#include "gfx/renderer.h"
#include "mission/mission.h"
#include "res/res_manager.h"

namespace op2 {
namespace {

struct Options {
    std::string script, out = ".", map = "cm01.map", techtree = "multitek.txt";
    int ticks = 3000, seed = 1, traceEvery = 1, shotEvery = 0, telemEvery = 0, width = 1024, height = 768;
    bool show = false, eco = false, audio = false;
};

struct Line {
    int tick = 0;
    std::string verb;
    std::vector<std::string> args;
    bool fileCoords = false;
    int number = 0;
};

FILE* g_log = nullptr;

void logf(const char* fmt, ...) {
    if (!g_log) return;
    va_list a;
    va_start(a, fmt);
    std::vfprintf(g_log, fmt, a);
    va_end(a);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

// ---- The scenario mission ------------------------------------------------------------

AIModDesc g_desc = { -1, 2, 12, 0, nullptr, "Harness scenario", nullptr, 0 };

int scenarioInitProc() {
    Player[0].GoEden();
    Player[0].GoHuman();
    Player[1].GoPlymouth();
    Player[1].GoAI();
    TethysGame::SetDaylightEverywhere(1);
    return 1;
}
void scenarioAIProc() {}

// ---- Script ----------------------------------------------------------------------------

bool loadScript(const std::string& path, std::vector<Line>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string text;
    bool fileCoords = false;
    for (int n = 1; std::getline(f, text); ++n) {
        if (const size_t hash = text.find('#'); hash != std::string::npos) text.resize(hash);
        std::istringstream in(text);
        Line l;
        if (!(in >> l.tick >> l.verb)) continue;
        for (std::string a; in >> a;) l.args.push_back(a);
        if (l.verb == "coords") {
            fileCoords = !l.args.empty() && l.args[0] == "file";
            continue;
        }
        l.fileCoords = fileCoords;
        l.number = n;
        out.push_back(l);
    }
    // In tick order, lines of one tick in file order, as the harness sorts its script: a
    // scenario's translation can list a tick-1 line after lines for later ticks.
    std::stable_sort(out.begin(), out.end(), [](const Line& a, const Line& b) { return a.tick < b.tick; });
    return true;
}

std::vector<int> g_spawned;   // spawn index -> unit id (0 = failed)
std::vector<int> g_spawnType;
std::vector<bool> g_spawnGone;

FILE* g_telem = nullptr;   // telemetry.jsonl (--telemetry-every)
bool g_eco = false;        // --eco: the colony and unit internals too
void writeTelemetry(int tick);
void writeTrace(int tick);

// Port tile x from a script x.
int tileX(const Line& l, int x) {
    if (l.fileCoords) return x;
    const int off = g_gameMap.internalXOffset();
    return (off > 0 ? x - off : x) & (g_gameMap.width() - 1);
}

int unitArg(const std::string& a) {
    if (!a.empty() && a[0] == '@') {
        const size_t n = std::strtoul(a.c_str() + 1, nullptr, 10);
        return n < g_spawned.size() ? g_spawned[n] : 0;
    }
    return std::atoi(a.c_str());
}

bool alive(int id) {
    if (id <= 0 || id >= static_cast<int>(g_units.size())) return false;
    const UnitRecord& r = g_units[id];
    return r.id != -1 && !(r.flags & UNIT_DEAD);
}

int typeByName(const std::string& name) {
    if (name.empty() || name == "-") return kMapIdNone;
    const int t = unitTypeFromCodeName(name.c_str());
    return t > 0 ? t : kMapIdNone;
}

// A packed waypoint with the range (how near counts as reached) in its top 3 bits.
uint32_t waypoint(int x, int y, int range) {
    return packLocation(x, y) | (static_cast<uint32_t>(range & 7) << 29);
}

void sendPacket(int id, CommandPacket& p) {
    p.unitIds.assign(1, id);
    processCommandPacket(g_units[id].owner & 0x0F, p);
}

// Internal tile x (the original's) from a script x, as the original's mission API takes it.
int internalX(const Line& l, int x) {
    return l.fileCoords ? x + std::max(0, g_gameMap.internalXOffset()) : x;
}

// The owner's packet, run now (the harness calls ProcessCommandPacket directly, as here).
void runPacket(int id, CommandPacket& p) {
    if (p.unitIds.empty()) p.unitIds.assign(1, id);
    processCommandPacket(g_units[id].owner & 0x0F, p);
}

// A spawn (@N) the trace has seen die, or that failed: the original gives it no more orders.
bool spawnGone(const std::string& a) {
    if (a.empty() || a[0] != '@') return false;
    const size_t n = std::strtoul(a.c_str() + 1, nullptr, 10);
    return n >= g_spawned.size() || (n < g_spawnGone.size() && g_spawnGone[n]);
}

const char* codeNameOf(int type) {
    const char* n = unitCodeName(type);
    return n ? n : "?";
}

int numArg(const std::string& a) { return static_cast<int>(std::strtol(a.c_str(), nullptr, 0)); }

// Script names of mission AI groups (`group <name> ...`): stub and kind.
struct NamedGroup { std::string name; int stub, kind; };
std::vector<NamedGroup> g_groups;
const NamedGroup* findGroup(const std::string& name) {
    for (const NamedGroup& g : g_groups) if (g.name == name) return &g;
    return nullptr;
}

// The live units of a type (a disaster line's new one is the one not there before, for `now`).
std::vector<int> liveOfType(int type) {
    std::vector<int> ids;
    for (size_t i = 1; i < g_units.size(); ++i)
        if (g_units[i].id != -1 && g_units[i].type == type && !(g_units[i].flags & UNIT_DEAD)) ids.push_back(static_cast<int>(i));
    return ids;
}

// `group <name> <op> [args]`: the mission API's ScGroup / FightGroup / MiningGroup / BuildingGroup calls (OP2TestHarness
// README "Group verbs"), through the port functions the mission DLLs reach (mission/op2_api.cpp). Tiles as there:
// internal x, except patrol points (port x, as FG_SetPatrolMode converts them).
void runGroup(const Line& l) {
    auto arg = [&](size_t i) { return i < l.args.size() ? l.args[i] : std::string(); };
    auto num = [&](size_t i) { return numArg(arg(i)); };
    const std::string name = arg(0), op = arg(1);
    if (op == "new") {
        const std::string kind = arg(2);
        const int k = kind == "mining" ? GK_MINE : kind == "building" ? GK_BUILD : GK_FIGHT;
        const int stub = aiCreateGroup(k, num(3));
        g_groups.push_back({name, stub, k});
        logf("group %s: %s group of player %d -> stub %d", name.c_str(), kind.c_str(), num(3), stub);
        return;
    }
    const NamedGroup* g = findGroup(name);
    if (!g) { logf("script: group %s: unknown group", name.c_str()); return; }
    const int id = g->stub;
    auto ix = [&](size_t i) { return internalX(l, num(i)); };
    auto groupArg = [&](size_t i) { const NamedGroup* o = findGroup(arg(i)); return o ? o->stub : 0xFF; };
    if (op == "take") { for (size_t i = 2; i < l.args.size(); ++i) aiTakeUnit(id, unitArg(arg(i))); }
    else if (op == "remove") aiRemoveUnit(id, unitArg(arg(2)));
    else if (op == "lights") aiSetLights(id, num(2) != 0);
    else if (op == "enable") stubEnable(id, true);
    else if (op == "disable") stubEnable(id, false);
    else if (op == "destroy") { aiGroupDestroyed(id); stubDestroy(id); }
    else if (op == "rect") {
        if (g->kind == GK_FIGHT) aiFightSetRect(id, ix(2), num(3), ix(4), num(5));
        else aiBuildSetRect(id, ix(2), num(3), ix(4), num(5));
    }
    else if (op == "guardedrect") aiFightAddGuardedRect(id, ix(2), num(3), ix(4), num(5));
    else if (op == "clearguarded") aiFightClearGuardedRects(id);
    else if (op == "guardrect") aiFightSetMode(id, 0);
    else if (op == "patrolonly") aiFightSetMode(id, 1);
    else if (op == "guardgroup") aiFightSetMode(id, 2);
    else if (op == "guardunit") aiFightSetMode(id, 3);
    else if (op == "attackunit") aiFightSetMode(id, 4);
    else if (op == "attackenemy") aiFightSetMode(id, 5);
    else if (op == "exitmap") aiFightSetMode(id, 6);
    else if (op == "clearpatrol") aiFightClearPatrol(id);
    else if (op == "combine") aiFightSetCombineFire(id, num(2) != 0);
    else if (op == "follow") aiFightSetFollowMode(id, num(2));
    else if (op == "targetunit") aiFightSetTargetUnit(id, unitArg(arg(2)));
    else if (op == "targetgroup") aiFightSetTargetGroup(id, groupArg(2));
    else if (op == "attacktype") aiFightSetAttackType(id, typeByName(arg(2)));
    else if (op == "patrol") {
        int xs[8], ys[8], n = 0;
        for (size_t i = 3; i < l.args.size() && n < 8; ++i) {
            int x = 0, y = 0;
            if (std::sscanf(l.args[i].c_str(), "%d,%d", &x, &y) == 2) { xs[n] = tileX(l, x); ys[n] = y; ++n; }
        }
        aiFightSetPatrol(id, n, num(2), xs, ys);
    }
    else if (op == "mine") aiMineSetup(id, unitArg(arg(2)), unitArg(arg(3)), ix(4), num(5), ix(6), num(7));
    else if (op == "targcount") aiSetTargCount(id, typeByName(arg(2)), typeByName(arg(3)), num(4));
    else if (op == "cleartargcount") aiClearTargCount(id);
    else if (op == "reinforce") aiBuildRecordReinforce(id, groupArg(2), num(3));
    else if (op == "unreinforce") aiBuildUnRecordReinforce(id, groupArg(2));
    else if (op == "building")
        aiBuildRecordBuilding(id, ix(2), num(3), typeByName(arg(4)), typeByName(arg(5)), l.args.size() > 6 ? groupArg(6) : 0xFF);
    else if (op == "tube") aiBuildRecordTube(id, ix(2), num(3));
    else if (op == "wall") aiBuildRecordWall(id, ix(2), num(3), l.args.size() > 4 ? typeByName(arg(4)) : 0x12);
    else if (op == "dump") {
        std::string members;
        for (int i = 0;; ++i) {
            const int u = aiGroupMemberAt(id, i);
            if (!u) break;
            members += " " + std::to_string(u);
        }
        logf("group %s (stub %d): %d units:%s", name.c_str(), id, aiTotalUnitCount(id), members.c_str());
    }
    else logf("script: group: unknown op %s", op.c_str());
}

// The colony and world verbs (OP2TestHarness README "Economy verbs", "Disasters"). False for a verb it doesn't know.
bool runEco(const Line& l, int tick) {
    auto arg = [&](size_t i) { return i < l.args.size() ? l.args[i] : std::string(); };
    auto num = [&](size_t i) { return numArg(arg(i)); };
    const std::string& v = l.verb;
    if (v == "tube" || v == "wall") {      // tube|wall x1 y1 x2 y2 [TYPE]: Game::CreateWall over the inclusive rectangle
        const int type = v == "tube" ? 0x11 : (l.args.size() > 4 ? typeByName(arg(4)) : 0x12);
        for (int y = num(1); y <= num(3); ++y)
            for (int x = num(0); x <= num(2); ++x) createWallOrTube(tileX(l, x), y, type);
        return true;
    }
    if (v == "mem") {   // the original's addresses (OPU 1.4.1) that the oracle scripts write, as op2-sim models them
        const uint32_t addr = static_cast<uint32_t>(std::strtoul(arg(0).c_str(), nullptr, 0));
        const int value = num(2);
        if (addr >= 0x56EAEC && addr <= 0x56EAFC && (addr - 0x56EAEC) % 4 == 0) {   // the forced-morale masks
            colonyForceMasks()[(addr - 0x56EAEC) / 4] = static_cast<uint32_t>(value);
            for (int p = 0; p < kMaxPlayers; ++p) colonyMarkDirty(p);
        }
        else if (addr == 0x56EAE0) TethysGame::SetDaylightMoves(value != 0);       // GameImpl option 0x12
        else if (addr == 0x56EAE4) TethysGame::SetDaylightEverywhere(value != 0);  // option 0x13
        else if (addr == 0x5472DC) TethysGame::setDaylightPosRaw(value);           // the light band's position
        else if (addr == 0x5472E0) TethysGame::setDaylightPhaseRaw(value);         // and its phase
        else if (addr == 0x56EAB4) {}                                              // the hidden fog: display only
        else logf("script: tick %d line %d: mem %s is not supported here", tick, l.number, arg(0).c_str());
        return true;
    }
    if (v == "peek") {
        logf("script: tick %d line %d: peek reads the original's memory: not supported here", tick, l.number);
        return true;
    }
    if (v == "tech") {                     // tech <player> <techID>: _Player::MarkResearchComplete
        if (techIndex(num(1)) < 0 || num(0) < 0 || num(0) >= kMaxPlayers)
            logf("script: tech %s %s: no such player or topic in this tech tree", arg(0).c_str(), arg(1).c_str());
        else Player[num(0)].MarkResearchComplete(num(1));
        return true;
    }
    if (v == "player") {                   // player <n> <field> <value>: a PlayerImpl field
        const int p = num(0), value = num(2);
        const std::string f = arg(1);
        if (p < 0 || p >= kMaxPlayers) { logf("script: player: no player %s", arg(0).c_str()); return true; }
        PlayerData& d = g_playerData[p];
        if (f == "food") d.foodStored = value;
        else if (f == "common") d.ore = value;
        else if (f == "rare") d.rareOre = value;
        else if (f == "moraleLevel") d.morale = value;
        else if (f == "morale") g_colony[p].moraleValue = value;
        else if (f == "workers") d.workers = value;
        else if (f == "scientists") d.scientists = value;
        else if (f == "kids") d.kids = value;
        else if (f == "solarsat") Player[p].SetSolarSat(value);
        else if (!f.empty() && f[0] >= '0' && f[0] <= '9') {
            const int off = numArg(f);
            if (off == 0x2C) d.isEden = value;
            else if (off == 0x04) d.allyMask = static_cast<uint32_t>(value);
            else if (off == 0x08) {        // RLVs bits 0-3, solar satellites 4-7, the space bits from 8
                d.rlvCount = value & 0xF;
                d.solarSat = (value >> 4) & 0xF;
                d.spaceBits = static_cast<uint32_t>(value) >> 8;
            }
            else { logf("script: tick %d line %d: player field %s is not supported here", tick, l.number, f.c_str()); return true; }
        } else { logf("script: player: bad field %s", f.c_str()); return true; }
        colonyMarkDirty(p);                // recalculateValues_
        return true;
    }
    if (v == "meteor" || v == "quake" || v == "eruption" || v == "lightning" || v == "tornado") {
        const bool now = !l.args.empty() && l.args.back() == "now";
        const int x = tileX(l, num(0)), y = num(1);
        const int type = v == "meteor" ? 0x50 : v == "quake" ? 0x4E : v == "eruption" ? 0x4F : v == "lightning" ? 0x4C : 0x4D;
        const std::vector<int> before = liveOfType(type);
        const int endX = num(3) == -1 ? -1 : tileX(l, num(3));
        if (v == "meteor") setMeteor(x, y, num(2));
        else if (v == "quake") setEarthquake(x, y, num(2));
        else if (v == "eruption") setEruption(x, y, num(2));
        else if (v == "lightning") setLightning(x, y, num(2), endX, num(4));
        else setTornado(x, y, num(2), endX, num(4), now);
        int made = 0;
        for (int id : liveOfType(type))
            if (std::find(before.begin(), before.end(), id) == before.end()) {
                made = id;
                if (now) g_units[id].flags |= 0x8000u | 0x4000u;   // skips the two warning waits
            }
        logf("%s at (%s,%s): unit %d", v.c_str(), arg(0).c_str(), arg(1).c_str(), made);
        return true;
    }
    if (v == "empmissile") {               // empmissile x y owner tx ty
        setEMPMissile(tileX(l, num(0)), num(1), num(2), tileX(l, num(3)), num(4));
        return true;
    }
    if (v == "blight") {                   // blight x y [speed]: SetMicrobeSpreadSpeed, then GameMap::SetVirusUL
        if (l.args.size() > 2 && num(2) >= 0) setMicrobeSpreadSpeed(num(2));
        setVirusUL(internalX(l, num(0)), num(1), 1);
        return true;
    }
    if (v == "lavaspeed") { setLavaSpeed(num(0)); return true; }
    if (v == "lavapossible") {             // lavapossible x1 y1 x2 y2 0|1
        for (int y = num(1); y <= num(3); ++y)
            for (int x = num(0); x <= num(2); ++x) setLavaPossible(tileX(l, x), y, num(4) != 0);
        return true;
    }
    if (v == "beacon") {                   // beacon x y ore bar variant | beacon x y vent|fumarole
        const std::string kind = arg(2);
        const int type = kind == "vent" ? 0x52 : kind == "fumarole" ? 0x53 : 0x51;
        const bool mine = type == 0x51;
        const int ok = createBeacon(type, tileX(l, num(0)), num(1), mine ? num(2) : -1, mine ? num(3) : -1, mine ? num(4) : -1);
        logf("beacon %s at (%s,%s): %d", kind.c_str(), arg(0).c_str(), arg(1).c_str(), ok);
        return true;
    }

    // Verbs on a unit (the first argument).
    static const char* const unitVerbs[] = {
        "poke", "route", "produce", "idle", "unidle", "train", "load", "unload", "kit", "cargo", "bay", "transfercargo",
        "launch", "build", "doze", "buildwall", "removewall", "repairobj", "reprogram", "dismantle", "research", "scatter" };
    if (std::none_of(std::begin(unitVerbs), std::end(unitVerbs), [&](const char* n) { return v == n; })) return false;
    if (v == "scatter") {                  // one ctMoScatter packet for the live units (dead spawns left out)
        CommandPacket p;
        p.type = CMD_SCATTER;
        int owner = -1;
        for (const std::string& a : l.args) {
            if (spawnGone(a)) continue;
            const int id = unitArg(a);
            if (!alive(id)) continue;
            p.unitIds.push_back(id);
            owner = g_units[id].owner & 0x0F;
        }
        if (!p.unitIds.empty()) processCommandPacket(owner, p);
        return true;
    }
    const int id = unitArg(arg(0));
    if (!alive(id)) {
        logf("script: tick %d line %d: no live unit %s: skipped", tick, l.number, arg(0).c_str());
        return true;
    }
    UnitRecord& u = g_units[id];
    CommandPacket p;
    if (v == "poke") {                     // poke <unit> <offset> <size> <value>: the MapObject fields the scripts use
        const int off = num(1), value = num(3);
        if (off == 0x1E) u.damage = static_cast<int16_t>(value);
        else if (off == 0x68 && (u.type == 0x15 || u.type == 0x16 || u.type == 0x2A)) u.mineLink = value;   // a mine's loads
        else logf("script: tick %d line %d: poke offset %s is not supported here", tick, l.number, arg(1).c_str());
    } else if (v == "route") {             // route <truck> <mine> <smelter>: the packet the UI sends
        const int mine = unitArg(arg(1)), smelter = unitArg(arg(2));
        int mx, my, sx, sy;
        if (!alive(mine) || !alive(smelter) || !dockCell(mine, mx, my) || !dockCell(smelter, sx, sy)) return true;
        p.type = CMD_CARGO_ROUTE;
        p.waypoints = {packLocation(mx, my), packLocation(sx, sy), packLocation(mx, my)};
        p.routeMineIdx = 0;
        p.routeSmelterIdx = 1;
        p.routeMine = mine;
        p.routeSmelter = smelter;
        runPacket(id, p);
    } else if (v == "produce") {           // produce <factory> TYPE [weapon]: Unit::DoProduce's packet
        p.type = CMD_PRODUCE;
        p.arg0 = typeByName(arg(1));
        p.arg1 = l.args.size() > 2 ? typeByName(arg(2)) : kMapIdNone;
        p.arg2 = 0xFFFF;
        runPacket(id, p);
    } else if (v == "idle" || v == "unidle" || v == "load" || v == "unload") {   // Unit::DoSimpleCommand
        p.type = v == "idle" ? CMD_IDLE : v == "unidle" ? CMD_UNIDLE : v == "load" ? CMD_LOAD_CARGO : CMD_UNLOAD_CARGO;
        runPacket(id, p);
    } else if (v == "train") {
        p.type = CMD_TRAIN;
        p.arg0 = num(1);
        runPacket(id, p);
    } else if (v == "research") {          // research <lab> <techID> <scientists>: the packet takes the topic's index
        const int topic = techIndex(num(1));
        if (topic < 0) { logf("script: research: no topic %s", arg(1).c_str()); return true; }
        p.type = CMD_RESEARCH;
        p.arg0 = topic;
        p.arg1 = num(2);
        runPacket(id, p);
    } else if (v == "kit") {               // kit <convec> TYPE [weapon]: Unit::SetCargo(MapID, MapID)
        if (u.flags & UNIT_VEHICLE) {
            u.cargo = static_cast<uint16_t>(typeByName(arg(1)));
            u.kitWeapon = static_cast<uint8_t>(l.args.size() > 2 ? typeByName(arg(2)) : kMapIdNone);
        }
    } else if (v == "cargo") {             // cargo <truck> <CargoType> <amount>: Unit::SetCargo(CargoType, int)
        if (u.flags & UNIT_VEHICLE)
            u.cargo = static_cast<uint16_t>((static_cast<uint16_t>(num(2) << 4) & ~0xFu) | (num(1) & 0xF));
    } else if (v == "bay") {               // bay <factory> <bay> TYPE [weapon]: Unit::SetFactoryCargo
        unitSetFactoryCargo(id, num(1), typeByName(arg(2)), l.args.size() > 3 ? typeByName(arg(3)) : kMapIdNone);
    } else if (v == "transfercargo") {     // transfercargo <factory> <bay> [1]
        p.type = CMD_XFER_CARGO;
        p.arg0 = num(1);
        p.arg1 = l.args.size() > 2 ? num(2) : 0;
        runPacket(id, p);
    } else if (v == "launch") {            // launch <spaceport> [x y]: a target in the original's (internal) pixels
        p.type = CMD_LAUNCH;
        if (l.args.size() > 2) {
            p.arg0 = num(1) - std::max(0, g_gameMap.internalXOffset()) * 32;
            p.arg1 = num(2);
        }
        runPacket(id, p);
    } else if (v == "build") {             // build <convec> x y [TYPE]: the bottom-right tile of footprint plus margin
        const int kit = l.args.size() > 3 ? typeByName(arg(3)) : static_cast<int>(u.cargo);
        const StructSheetDesc* d = kit != kMapIdNone ? structSheet(kit) : nullptr;
        if (!d) { logf("script: build: no ConVec kit"); return true; }
        const int mask = g_gameMap.width() - 1, bx = tileX(l, num(1)), by = num(2);
        p.type = CMD_BUILD;
        p.waypoints.push_back(packLocation(bx, by));
        p.rectL = (bx - d->sizeX - 1) & mask;
        p.rectT = by - d->sizeY - 1;
        p.rectR = bx;
        p.rectB = by;
        p.arg0 = -1;
        runPacket(id, p);
        logf("build: %s at (%s,%s)", codeNameOf(kit), arg(1).c_str(), arg(2).c_str());
    } else if (v == "doze" || v == "buildwall") {   // doze <dozer> x1 y1 x2 y2 | buildwall <earthworker> x1 y1 x2 y2 [TYPE]
        const int x1 = tileX(l, num(1)), y1 = num(2), x2 = tileX(l, num(3)), y2 = num(4);
        p.rectL = x1;
        p.rectT = y1;
        if (v == "doze") {
            p.type = CMD_DOZE;
            p.rectR = x2;
            p.rectB = y2;
        } else {
            p.type = CMD_BUILD_WALL;
            p.rectR = x2 + 1;              // the packet's bottom-right is exclusive
            p.rectB = y2 + 1;
            p.arg0 = l.args.size() > 5 ? typeByName(arg(5)) : 0x11;
        }
        runPacket(id, p);
    } else if (v == "removewall") {        // removewall <earthworker> x y
        p.type = CMD_REMOVE_WALL;
        p.rectL = p.rectR = tileX(l, num(1));
        p.rectT = p.rectB = num(2);
        runPacket(id, p);
    } else if (v == "repairobj" || v == "reprogram" || v == "dismantle") {   // <unit> <target|none> [unit...]
        p.type = v == "repairobj" ? CMD_REPAIR_OBJ : v == "reprogram" ? CMD_REPROGRAM : CMD_DISMANTLE;
        p.unitIds.push_back(id);
        for (size_t i = 2; i < l.args.size(); ++i) {
            const int more = unitArg(arg(i));
            if (more > 0 && more < static_cast<int>(g_units.size()) && g_units[more].id != -1) p.unitIds.push_back(more);
        }
        p.target = 0xFFFF0000u | static_cast<uint16_t>(arg(1) == "none" ? 0 : unitArg(arg(1)));
        runPacket(id, p);
    }
    return true;
}

bool isInputVerb(const std::string& v);
void shotNow(int tick);
void runInput(GameState& gs, const Line& l, int tick);

void runLine(GameState& gs, const Line& l, int tick) {
    std::string all;
    for (const std::string& a : l.args) all += " " + a;
    logf("script: tick %d: %s%s", tick, l.verb.c_str(), all.c_str());
    auto arg = [&](size_t i) { return i < l.args.size() ? l.args[i] : std::string(); };
    auto num = [&](size_t i) { return std::atoi(arg(i).c_str()); };

    // A spawn that has died (or failed) gets no more orders: its index may belong to something else by then.
    if (l.verb != "scatter" && spawnGone(arg(0))) {
        logf("script: spawn %s is gone: skipped", arg(0).c_str());
        return;
    }
    if (l.verb == "spawn") {     // spawn TYPE owner x y [weapon|-] [dir 0-255]
        const int type = typeByName(arg(0)), weapon = typeByName(arg(4));
        const int dir = l.args.size() > 5 ? (num(5) & 0xFF) : 0;
        int id = 0;
        if (type != kMapIdNone) id = scenarioCreateUnit(type, tileX(l, num(2)), num(3), num(1), weapon, dir / 32);
        if (id && dir % 32) g_units[id].rotation = static_cast<uint8_t>(dir);
        // The harness spawns through TethysAPI's Game::CreateUnit, which turns a vehicle's headlights on after the
        // game's CreateUnit (its lightsOn argument defaults to true).
        if (id && (g_units[id].flags & UNIT_VEHICLE)) unitSetLights(id, 1);
        logf("spawn %u: %s -> unit %d", static_cast<unsigned>(g_spawned.size()), arg(0).c_str(), id);
        g_spawned.push_back(id);
        g_spawnType.push_back(id ? g_units[id].type : type);
        g_spawnGone.push_back(id == 0);
        return;
    }
    if (l.verb == "setcell") {   // setcell x1 y1 x2 y2 cellType (inclusive)
        for (int y = num(1); y <= num(3); ++y)
            for (int x = num(0); x <= num(2); ++x) {
                const int px = tileX(l, x);
                const uint32_t c = g_gameMap.rawCell(px, y);
                g_gameMap.setRawCell(px, y, (c & ~0x1Fu) | (static_cast<uint32_t>(num(4)) & 0x1Fu));
            }
        return;
    }
    if (l.verb == "view") {      // centre player 0's view on a tile
        requestCenterView(tileX(l, num(0)) << 5, num(1) << 5);
        return;
    }
    if (l.verb == "telemetry") { writeTelemetry(tick); return; }
    if (l.verb == "trace") { writeTrace(tick); return; }
    if (l.verb == "shot") { shotNow(tick); return; }
    if (l.verb == "group") { runGroup(l); return; }
    if (isInputVerb(l.verb)) { runInput(gs, l, tick); return; }
    if (runEco(l, tick)) return;

    // Orders: the first argument is the unit.
    static const char* const orders[] = {
        "move", "dock", "dockeg", "movewp", "standground", "patrol", "attack", "guard", "attackground", "lights", "stop",
        "selfdestruct" };
    if (std::none_of(std::begin(orders), std::end(orders), [&](const char* n) { return l.verb == n; })) {
        // Verbs the original harness has that need what this mode doesn't do yet: recorded packets, saves. Any other verb is unknown to the original harness too, which logs
        // it and goes on.
        static const char* const later[] = {
            "packet", "savegame", "loadgame", "gamespeed", "name", "quit" };
        if (std::any_of(std::begin(later), std::end(later), [&](const char* n) { return l.verb == n; }))
            logf("script: tick %d line %d: verb %s is not supported here", tick, l.number, l.verb.c_str());
        else
            logf("script: unknown verb %s", l.verb.c_str());
        return;
    }
    const int id = unitArg(arg(0));
    if (!alive(id)) {
        logf("script: tick %d line %d: no live unit %s: skipped", tick, l.number, arg(0).c_str());
        return;
    }
    CommandPacket p;
    if (l.verb == "move" || l.verb == "dock" || l.verb == "dockeg") {
        p.type = l.verb == "move" ? CMD_MOVE : l.verb == "dock" ? CMD_DOCK : CMD_DOCK_EG;
        p.waypoints.push_back(waypoint(tileX(l, num(1)), num(2), 0));
        sendPacket(id, p);
    } else if (l.verb == "movewp" || l.verb == "standground" || l.verb == "patrol") {
        // movewp/standground <unit> x,y[,range] ...; patrol <unit> <loop index> x,y[,range] ...
        const bool patrol = l.verb == "patrol";
        p.type = patrol ? CMD_PATROL : l.verb == "standground" ? CMD_STAND_GROUND : CMD_MOVE;
        for (size_t i = patrol ? 2 : 1; i < l.args.size(); ++i) {
            int x = 0, y = 0, r = 0;
            if (std::sscanf(l.args[i].c_str(), "%d,%d,%d", &x, &y, &r) < 2) continue;
            p.waypoints.push_back(waypoint(tileX(l, x), y, r));
        }
        if (patrol) p.patrolStart = num(1);
        if (!p.waypoints.empty() && p.waypoints.size() <= 8) sendPacket(id, p);
    } else if (l.verb == "attack" || l.verb == "guard") {
        p.type = l.verb == "attack" ? CMD_ATTACK : CMD_GUARD;
        p.target = 0xFFFF0000u | static_cast<uint16_t>(unitArg(arg(1)));
        sendPacket(id, p);
    } else if (l.verb == "attackground") {
        p.type = CMD_ATTACK;
        p.target = (static_cast<uint32_t>(static_cast<uint16_t>(num(2))) << 16) | static_cast<uint16_t>(tileX(l, num(1)));
        sendPacket(id, p);
    } else if (l.verb == "lights") {
        unitSetLights(id, num(1) != 0);
    } else if (l.verb == "stop") {
        p.type = CMD_STOP;
        sendPacket(id, p);
    } else if (l.verb == "selfdestruct") {
        unitDoSelfDestruct(id);
    }
    (void)gs;
}

// ---- Trace -----------------------------------------------------------------------------

const char* commandName(int c) {
    static const char* const names[] = {
        "Nop", "Doze", "Move", "Dock", "DockEG", "Stop", "Build", "BuildWall", "RemoveWall", "Produce", "TransferCargo",
        "LoadCargo", "UnloadCargo", "Recycle", "DumpCargo", "Scavenge", "SpecialWait", "Survey", "Idle", "Unidle",
        "SelfDestruct", "Scatter", "Research", "TrainScientists", "Transfer", "Launch", "FlyInSpace", "Repair", "RepairObj",
        "Reprogram", "Dismantle", "Salvage", "Create", "Develop", "UnDevelop", "LightToggle", "Attack", "Guard",
        "StandGround", "CargoRoute", "Patrol", "MapChange", "Poof", "GameOpt", "GodWeapon", "ChatText", "ChatSFX", "Death",
        "Chat", "Quit", "Ally", "GoAI", "MachineSettings", "InvalidCommand" };
    return c >= 0 && c < static_cast<int>(sizeof(names) / sizeof(names[0])) ? names[c] : "?";
}

const char* actionName(int a) {
    static const char* const names[] = {
        "Done", "Move", "Build", "BuildMine", "Repair", "Develop", "ObjWait", "OperationalWait", "EMPWait", "Landing",
        "ObjFroze", "DelSelf", "DoResearch", "ObjDocking", "Scavenge", "WeaponMove", "WeatherMove", "EMPRecover",
        "WeaponAimCoarse", "WeaponAimFine", "SantaWalking", "Invalid" };
    return a >= 0 && a < static_cast<int>(sizeof(names) / sizeof(names[0])) ? names[a] : "?";
}

int xOffset() { return std::max(0, g_gameMap.internalXOffset()); }


int maxHP(const UnitRecord& r) {
    const int p = (r.owner >> 4) & 0x0F;
    if (r.flags & UNIT_VEHICLE) { const VehPlayerStats* v = vehicleSheet(r.type, p); return v ? v->hitPoints : 0; }
    if (r.flags & UNIT_BUILDING) { const StructSheetDesc* d = structSheet(r.type); return d ? d->player[p].hitPoints : 0; }
    return 0;
}

const char* codeName(int type) {
    const char* n = unitCodeName(type);
    return n ? n : "?";
}

// Spawns that died: the object index they held, as the harness's `reused` reads it. 0 still
// held by the dead object, 1 freed, 2 handed out again (an order naming the spawn now reaches
// another object), 3 never had one (a spawn that failed).
std::vector<uint8_t> g_spawnSlot;
FILE* g_trace = nullptr;
int g_lastTrace = -1;

bool isWeaponFire(int type) { return type >= 0x3B && type <= 0x4B; }

int spawnOf(int id) {
    for (size_t k = 0; k < g_spawned.size(); ++k) if (g_spawned[k] == id) return static_cast<int>(k);
    return -1;
}

void writeUnit(FILE* f, bool& first, int id, int spawn) {
    const UnitRecord& r = g_units[id];
    const int px = unitOX(r), py = unitOY(r);
    const bool veh = (r.flags & UNIT_VEHICLE) != 0;
    // 16.16 position: the step's 16-bit fraction is biased so 0x8000 is "on px"; it reads 0 before the first step.
    // It lives in the order block (+0x38, the original's path context), so it only counts while there is one.
    long long fx = static_cast<long long>(px) << 16, fy = static_cast<long long>(py) << 16;
    if (veh && r.hasOrder && (r.fracX | r.fracY)) {
        fx += static_cast<int>(r.fracX) - 0x8000;
        fy += static_cast<int>(r.fracY) - 0x8000;
    }
    std::fprintf(f, "%s{\"id\":%d,", first ? "" : ",", id);
    if (spawn >= 0) std::fprintf(f, "\"spawn\":%d,", spawn);
    // cmd is the command (+0x21), action the action (+0x22), busy +0x20; ipx/ipy the internal pixel.
    // The harness's frame and anim (set as the unit is drawn) aren't modeled here.
    std::fprintf(f, "\"type\":\"%s\",\"owner\":%d,\"px\":%d,\"py\":%d,\"sx\":%.6f,\"sy\":%.6f,"
                    "\"fix\":[%lld,%lld],\"dir\":%d,\"hp\":%d,\"cmd\":\"%s\",\"action\":\"%s\",\"busy\":%d,"
                    "\"ipx\":%d,\"ipy\":%d",
                 codeName(r.type), r.owner & 0x0F, px, py, fx / 65536.0, fy / 65536.0, fx, fy,
                 r.rotation, maxHP(r) - r.damage, commandName(r.action), actionName(r.field22), r.field20,
                 px + xOffset() * 32, py);
    if (veh) {
        std::fprintf(f, ",\"lights\":%d", static_cast<int>(r.flags & UNIT_LIGHTS));
        if (r.hasOrder) {
            // The path context: the target tile (the current waypoint; a route without one,
            // pathGoal), the step's target pixel, its 16-bit fraction (+0x44), the high words of
            // its velocity (+0x5A/+0x5E), the turn per tick, the waypoint and route cursors and
            // the route's steps still to take (one direction digit each, 0 = E, clockwise).
            int dx = r.pathGoalX, dy = r.pathGoalY;
            if (!r.order.waypoints.empty()) {
                const int i = std::min(r.order.curWaypoint, static_cast<int>(r.order.waypoints.size()) - 1);
                const uint32_t wp = i < 0 ? r.order.detourSlot : r.order.waypoints[static_cast<size_t>(i)];
                dx = unpackLocX(wp);
                dy = unpackLocY(wp);
            }
            const int n = static_cast<int>(r.routeDirs.size());
            std::fprintf(f, ",\"dest\":[%d,%d],\"path\":{\"start\":[%d,%d],\"moved\":[%d,%d],\"toMove\":[%d,%d],"
                            "\"rotSpeed\":%d,\"wp\":%d,\"numWp\":%d,\"pt\":%d,\"numPts\":%d,\"steps\":\"",
                         dx, dy, r.stepDestX + 16, r.stepDestY + 16, r.fracX, r.fracY,
                         static_cast<int>(static_cast<uint32_t>(r.velX) >> 16),
                         static_cast<int>(static_cast<uint32_t>(r.velY) >> 16),
                         static_cast<int>(static_cast<uint8_t>(r.turnStep)), r.order.curWaypoint, r.order.count, r.routeIdx, n);
            for (int i = r.routeIdx; i >= 0 && i < n && i < 64; ++i)
                std::fputc('0' + (r.routeDirs[static_cast<size_t>(i)] & 7), f);
            std::fputs("\"}", f);
        }
    }
    // Armed vehicles and Guard Posts: the weapon, turret direction, reload and target
    // (high word 0x7FFF = none, 0xFFFF = a unit in the low word, else a tile).
    const int w = r.cargo;
    if (isWeaponFire(w) && (veh || (r.flags & UNIT_TURRET_BLD))) {
        std::fprintf(f, ",\"weapon\":\"%s\",\"tur\":%d,\"reload\":%d,\"tgt\":", codeName(w), r.turretDir, r.reload);
        const uint16_t hi = static_cast<uint16_t>(r.field4E), lo = static_cast<uint16_t>(r.target4C);
        if (hi == 0x7FFF) std::fputs("null", f);
        else if (hi == 0xFFFF) std::fprintf(f, "%d", static_cast<int>(lo));
        else std::fprintf(f, "[%d,%d]", static_cast<int>(static_cast<int16_t>(lo)), static_cast<int>(static_cast<int16_t>(hi)));
    }
    // Weapon effects, only while one is on: [EMP, stickyfoam, ESG] timers; buildings only EMP.
    const bool emp = (r.flags & UNIT_EMP) != 0, foam = veh && (r.flags & 0x40000u), esg = veh && (r.flags & UNIT_HIT_FLASH);
    if (emp || foam || esg)
        std::fprintf(f, ",\"status\":[%d,%d,%d]", emp ? static_cast<int>(static_cast<uint16_t>(r.empTicks)) : 0,
                     foam ? static_cast<int>(static_cast<uint16_t>(r.foamTicks)) : 0,
                     esg ? static_cast<int>(static_cast<uint16_t>(r.esgTicks)) : 0);
    // The tiles within 3 of the unit's that hold its id: a moving vehicle's reserved tile, a building's footprint.
    std::fputs(",\"tiles\":[", f);
    bool firstTile = true;
    const int W = g_gameMap.width(), H = g_gameMap.height();
    for (int ddy = -3; ddy <= 3; ++ddy)
        for (int ddx = -3; ddx <= 3; ++ddx) {
            int x = (px >> 5) + ddx;
            const int y = (py >> 5) + ddy;
            if (xOffset() == 0 && W > 0) x = (x + W) % W;   // 512-wide maps wrap
            if (x < 0 || x >= W || y < 0 || y >= H || g_gameMap.cellOccupant(x, y) != id) continue;
            std::fprintf(f, "%s[%d,%d]", firstTile ? "" : ",", x, y);
            firstTile = false;
        }
    std::fputs("]}", f);
    first = false;
}

// fx: live weapon fire whose shooter is a spawn (an armed ESG, code name SPAM, also its mines); ents: every other live object (explosions,
// death animations, other fire, units the script didn't spawn), [id, type, action, timer, px, py].
void writeTraceFx(FILE* f) {
    std::fputs(",\"fx\":[", f);
    bool first = true;
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        if (!alive(i) || !isWeaponFire(g_units[i].type)) continue;
        const UnitRecord& r = g_units[i];
        const int spawn = spawnOf(r.sourceId);
        if (spawn < 0) continue;
        std::fprintf(f, "%s{\"id\":%d,\"type\":\"%s\",\"parent\":%d,\"action\":\"%s\",\"dir\":%d,\"px\":%d,\"py\":%d,\"timer\":%d",
                     first ? "" : ",", i, codeName(r.type), spawn, actionName(r.field22), r.rotation, unitOX(r), unitOY(r),
                     r.field3C);
        if (r.type == unitTypeFromCodeName("SPAM") && (r.flags & 0x8000u))
            std::fprintf(f, ",\"mines\":%d", static_cast<int>(r.esgMask));
        std::fputc('}', f);
        first = false;
    }
    std::fputs("],\"ents\":[", f);
    first = true;
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        if (!alive(i) || spawnOf(i) >= 0) continue;
        const UnitRecord& r = g_units[i];
        if (isWeaponFire(r.type) && spawnOf(r.sourceId) >= 0) continue;
        std::fprintf(f, "%s[%d,\"%s\",\"%s\",%d,%d,%d]", first ? "" : ",", i, codeName(r.type), actionName(r.field22),
                     r.field3C, unitOX(r), unitOY(r));
        first = false;
    }
    std::fputc(']', f);
}

void writeTrace(int tick) {
    FILE* f = g_trace;
    if (!f || tick == g_lastTrace) return;
    g_lastTrace = tick;
    std::fprintf(f, "{\"tick\":%d,\"units\":[", tick);
    bool first = true;
    if (g_spawnSlot.size() < g_spawned.size()) g_spawnSlot.resize(g_spawned.size(), 0);
    for (size_t i = 0; i < g_spawned.size(); ++i) {
        const int id = g_spawned[i];
        if (!id) { g_spawnSlot[i] = 3; continue; }   // failed: never had an object
        if (!g_spawnGone[i] && (!alive(id) || g_units[id].type != g_spawnType[i])) g_spawnGone[i] = true;
        if (g_spawnGone[i]) {
            if (g_spawnSlot[i] < 2) {
                const UnitRecord& r = g_units[id];
                if (r.id == -1) g_spawnSlot[i] = 1;
                else if (g_spawnSlot[i] == 1 || r.type != g_spawnType[i]) g_spawnSlot[i] = 2;
            }
            continue;
        }
        writeUnit(f, first, id, static_cast<int>(i));
    }
    if (g_spawned.empty())   // no spawns: every vehicle
        for (int p = 0; p < TethysGame::noPlayers(); ++p)
            for (int i = 1; i < static_cast<int>(g_units.size()); ++i)
                if (alive(i) && (g_units[i].flags & UNIT_VEHICLE) && (g_units[i].owner & 0x0F) == p) writeUnit(f, first, i, -1);
    std::fputc(']', f);
    bool anyReused = false;
    for (size_t i = 0; i < g_spawnSlot.size(); ++i)
        if (g_spawnSlot[i] == 2) {
            std::fprintf(f, "%s%u", anyReused ? "," : ",\"reused\":[", static_cast<unsigned>(i));
            anyReused = true;
        }
    if (anyReused) std::fputc(']', f);
    writeTraceFx(f);
    // The object allocator: the highest id handed out and the ring of freed slots, oldest first, as
    // [id, tick freed, references]. The harness adds the ring's [head, tail]; the port's ring has none.
    std::fprintf(f, ",\"alloc\":[%d,[", static_cast<int>(g_units.size()) - 1);
    bool firstSlot = true;
    for (int i : unitFreeRing()) {
        std::fprintf(f, "%s[%d,%d,%d]", firstSlot ? "" : ",", i, g_units[i].freeTick, g_units[i].refCount);
        firstSlot = false;
    }
    std::fputs("]]", f);
    std::fprintf(f, ",\"rng\":[%u,%u]}\n", TethysGame::rng().s0(), TethysGame::rng().s1());
    std::fflush(f);
}

// ---- Mouse and keyboard ----------------------------------------------------------------
// The harness's input verbs, as real input to the game: SDL events through GameState's
// handleEvent, the way the window's own events arrive. A <pos> is `<x> <y>` (the
// harness's client pixels: they start inside the frame the port draws, at (12,22), and
// --size 1056x800 gives the original's 1032x766 client at its default 1024x768),
// `tile <x> <y>` (that tile's centre in the view), `unit <id>` (the unit's centre),
// `mini <x> <y>` (the minimap pixel showing that tile) or `ui <name>` ("tabN" a report
// tab, "minimapN" a minimap button, else a case-insensitive part of a button's tooltip
// or label, '_' for spaces). Mouse verbs take `left|right|middle`, `shift`/`ctrl`/`alt`
// held while the button goes down and `hold N` (release N ticks later); any verb takes
// `+shot` (a screenshot one tick later). With input in the script, the game draws a
// frame every tick, as the original does between its ticks, and the telemetry gets
// `selected` and `view`. Not modeled: edge scrolling and the arrow keys' scrolling (the
// port reads the real cursor and keyboard there) and `hotkeys` (the original's table of
// button hotkeys); `uilist` logs the port's buttons.

Renderer* g_renderer = nullptr;
GameState* g_gs = nullptr;
bool g_inputUsed = false;
int g_inX = 0, g_inY = 0;                // the cursor, in window pixels
Uint32 g_inButtons = 0;                  // SDL_BUTTON() mask of the buttons held
SDL_Keymod g_inMods = KMOD_NONE;         // modifiers held by keydown
struct DeferredUp { int tick, x, y, button; };
std::vector<DeferredUp> g_deferredUp;    // releases scheduled by `hold N`
std::vector<int> g_pendingShots;         // ticks of `+shot` screenshots

bool isInputVerb(const std::string& v) {
    static const char* const verbs[] = { "mouse", "click", "rclick", "mclick", "dblclick", "down", "up", "drag", "key",
                                         "keydown", "keyup", "type", "select", "uilist", "hotkeys" };
    return std::any_of(std::begin(verbs), std::end(verbs), [&](const char* n) { return v == n; });
}

void inSend(GameState& gs, SDL_Event& e) {
    SDL_SetModState(g_inMods);
    gs.debugMouseButtons(static_cast<int>(g_inButtons));
    gs.handleEvent(*g_renderer, e);
}

void inMove(GameState& gs, int x, int y) {
    g_inX = x; g_inY = y;
    int sx = 0, sy = 0;
    g_renderer->nativeToScreen(x, y, sx, sy);
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = sx; e.motion.y = sy;
    e.motion.state = g_inButtons;
    inSend(gs, e);
}

void inButton(GameState& gs, int button, bool down) {
    static const Uint8 sdl[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
    int sx = 0, sy = 0;
    g_renderer->nativeToScreen(g_inX, g_inY, sx, sy);
    if (down) g_inButtons |= SDL_BUTTON(sdl[button]); else g_inButtons &= ~SDL_BUTTON(sdl[button]);
    SDL_Event e{};
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.button = sdl[button];
    // No CS_DBLCLKS on the original's windows: a double click is two single clicks there too.
    e.button.clicks = 1;
    e.button.x = sx; e.button.y = sy;
    inSend(gs, e);
}

struct KeySpec { SDL_Keycode sym = SDLK_UNKNOWN; bool shift = false, ctrl = false, alt = false; char ch = 0; };

// "a", "7", "enter", "f5", "ctrl+1", "alt+c", "shift+tab", "vk:0x41" (letters, digits and the keys named here), as the
// harness's ParseKey reads them on a US layout.
bool parseKey(std::string s, KeySpec& k) {
    for (;;) {
        const size_t plus = s.find('+', 1);
        if (plus == std::string::npos) break;
        std::string mod = s.substr(0, plus);
        for (char& c : mod) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (mod == "shift") k.shift = true;
        else if (mod == "ctrl") k.ctrl = true;
        else if (mod == "alt") k.alt = true;
        else return false;
        s = s.substr(plus + 1);
    }
    std::string low = s;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::pair<const char*, SDL_Keycode> names[] = {
        {"enter", SDLK_RETURN}, {"return", SDLK_RETURN}, {"esc", SDLK_ESCAPE}, {"escape", SDLK_ESCAPE},
        {"space", SDLK_SPACE}, {"tab", SDLK_TAB}, {"backspace", SDLK_BACKSPACE}, {"delete", SDLK_DELETE},
        {"insert", SDLK_INSERT}, {"home", SDLK_HOME}, {"end", SDLK_END}, {"pageup", SDLK_PAGEUP},
        {"pagedown", SDLK_PAGEDOWN}, {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"up", SDLK_UP}, {"down", SDLK_DOWN},
        {"pause", SDLK_PAUSE}, {"shift", SDLK_LSHIFT}, {"ctrl", SDLK_LCTRL}, {"alt", SDLK_LALT},
        {"numplus", SDLK_KP_PLUS}, {"numminus", SDLK_KP_MINUS}, {"plus", SDLK_EQUALS}, {"minus", SDLK_MINUS} };
    for (const auto& [n, sym] : names)
        if (low == n) { k.sym = sym; if (sym == SDLK_SPACE) k.ch = ' '; return true; }
    if (low.size() > 1 && low[0] == 'f' && std::isdigit(static_cast<unsigned char>(low[1]))) {
        const int n = std::atoi(low.c_str() + 1);
        if (n >= 1 && n <= 12) { k.sym = SDLK_F1 + n - 1; return true; }
    }
    if (low.rfind("vk:", 0) == 0) {
        const int vk = static_cast<int>(std::strtol(low.c_str() + 3, nullptr, 0)) & 0xFF;
        if (vk >= 'A' && vk <= 'Z') { k.sym = vk - 'A' + 'a'; return true; }
        if (vk >= '0' && vk <= '9') { k.sym = vk; return true; }
        return false;
    }
    if (s.size() == 1) {
        const char c = s[0];
        static const char* const shifted = "~!@#$%^&*()_+{}|:\"<>?";
        static const char* const plain   = "`1234567890-=[]\\;',./";
        k.ch = c;
        if (c >= 'A' && c <= 'Z') { k.sym = c - 'A' + 'a'; k.shift = true; return true; }
        if (const char* p = std::strchr(shifted, c)) { k.sym = plain[p - shifted]; k.shift = true; return true; }
        if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F) { k.sym = c; return true; }
    }
    return false;
}

SDL_Keymod modOf(SDL_Keycode sym) {
    return sym == SDLK_LSHIFT ? KMOD_LSHIFT : sym == SDLK_LCTRL ? KMOD_LCTRL : sym == SDLK_LALT ? KMOD_LALT : KMOD_NONE;
}

void inKey(GameState& gs, SDL_Keycode sym, bool down, char ch) {
    if (const SDL_Keymod m = modOf(sym)) g_inMods = static_cast<SDL_Keymod>(down ? (g_inMods | m) : (g_inMods & ~m));
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = sym;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
    e.key.keysym.mod = static_cast<Uint16>(g_inMods);
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    inSend(gs, e);
    // What TranslateMessage posts after a key: the character (the chat line reads it), not with Ctrl or Alt held.
    if (down && ch && !(g_inMods & (KMOD_CTRL | KMOD_ALT))) {
        SDL_Event t{};
        t.type = SDL_TEXTINPUT;
        t.text.text[0] = ch;
        inSend(gs, t);
    }
}

void inPressKey(GameState& gs, const KeySpec& k) {
    const SDL_Keycode mods[3] = { SDLK_LCTRL, SDLK_LALT, SDLK_LSHIFT };
    const bool want[3] = { k.ctrl, k.alt, k.shift };
    bool pressed[3] = {};
    for (int i = 0; i < 3; ++i)
        if (want[i] && !(g_inMods & modOf(mods[i]))) { inKey(gs, mods[i], true, 0); pressed[i] = true; }
    inKey(gs, k.sym, true, k.ch);
    inKey(gs, k.sym, false, 0);
    for (int i = 2; i >= 0; --i) if (pressed[i]) inKey(gs, mods[i], false, 0);
}

// A map pixel (port coordinates) -> window pixel through the view (it may fall outside it, as in the original).
void mapToWindow(GameState& gs, int px, int py, int& x, int& y) {
    int cx = 0, cy = 0;
    gs.debugCamera(cx, cy);
    const int W = g_gameMap.width() * 32;
    int dx = px - cx;
    if (xOffset() == 0 && W > 0) dx = ((dx % W) + W + W / 2) % W - W / 2;   // world maps wrap: the nearest copy
    x = gs.debugViewX() + dx;
    y = gs.debugViewY() + py - cy;
}

void logInputAt(GameState& gs, int tick, const std::string& verb, int x, int y) {
    int vx = 0, vy = 0, vw = 0, vh = 0, cx = 0, cy = 0;
    gs.debugViewRect(vx, vy, vw, vh);
    gs.debugCamera(cx, cy);
    const int ux = x - GameState::kClientX, uy = y - GameState::kClientY;   // client pixels, as the harness logs them
    if (x >= vx && y >= vy && x < vx + vw && y < vy + vh)
        logf("input: tick %d %s at (%d,%d): detail pane map tile (%d,%d) file", tick, verb.c_str(), ux, uy,
             (cx + x - vx) >> 5, (cy + y - vy) >> 5);
    else
        logf("input: tick %d %s at (%d,%d)", tick, verb.c_str(), ux, uy);
}

void runInput(GameState& gs, const Line& l, int tick) {
    std::vector<std::string> a;
    bool shot = false;
    for (const std::string& s : l.args) { if (s == "+shot") shot = true; else a.push_back(s); }
    if (shot) g_pendingShots.push_back(tick + 1);
    size_t i = 0;
    auto num = [&](size_t k) { return k < a.size() ? std::atoi(a[k].c_str()) : 0; };
    auto pos = [&](int& x, int& y) -> bool {
        if (i >= a.size()) return false;
        const std::string kind = a[i];
        if (kind == "tile" || kind == "mini") {
            const int tx = tileX(l, num(i + 1)), ty = num(i + 2);
            i += 3;
            if (kind == "tile") { mapToWindow(gs, tx * 32 + 16, ty * 32 + 16, x, y); return true; }
            // The minimap pixel whose map point is nearest the tile's centre, as the harness searches it.
            int vx = 0, vy = 0, vw = 0, vh = 0;
            gs.debugViewRect(vx, vy, vw, vh);
            long best = -1;
            for (int my = 0; my < GameState::kMiniMapH; ++my)
                for (int mx = 0; mx < GameState::kColW; ++mx) {
                    int px = 0, py = 0;
                    if (!gs.harnessMiniToWorld(gs.kColX + mx, GameState::kMiniMapY + my, px, py)) continue;
                    const long d = std::labs(px + 16 - (tx * 32 + 16)) + std::labs(py + 16 - (ty * 32 + 16));
                    if (best < 0 || d < best) { best = d; x = gs.kColX + mx; y = GameState::kMiniMapY + my; }
                }
            return best >= 0;
        }
        if (kind == "unit") {
            const int id = unitArg(i + 1 < a.size() ? a[i + 1] : "");
            i += 2;
            if (!alive(id)) return false;
            mapToWindow(gs, unitOX(g_units[id]), unitOY(g_units[id]), x, y);
            return true;
        }
        if (kind == "ui") { i += 2; return gs.harnessUiCenter(i - 1 < a.size() ? a[i - 1] : "", x, y); }
        if (!std::isdigit(static_cast<unsigned char>(kind[0])) && kind[0] != '-') return false;
        // The harness's window pixels are its client area's, which starts inside the frame the port draws.
        x = num(i) + GameState::kClientX; y = num(i + 1) + GameState::kClientY;
        i += 2;
        return true;
    };
    int button = 0, hold = 0;
    bool mod[3] = {};   // shift, ctrl, alt
    auto words = [&] {
        for (size_t k = i; k < a.size(); ++k) {
            if (a[k] == "hold" && k + 1 < a.size()) hold = std::max(0, std::atoi(a[++k].c_str()));
            else if (a[k] == "left") button = 0;
            else if (a[k] == "right") button = 1;
            else if (a[k] == "middle") button = 2;
            else if (a[k] == "shift") mod[0] = true;
            else if (a[k] == "ctrl") mod[1] = true;
            else if (a[k] == "alt") mod[2] = true;
        }
    };
    const SDL_Keycode modKey[3] = { SDLK_LSHIFT, SDLK_LCTRL, SDLK_LALT };
    bool pressedMod[3] = {};
    auto holdMods = [&] {
        for (int k = 0; k < 3; ++k)
            if (mod[k] && !(g_inMods & modOf(modKey[k]))) { inKey(gs, modKey[k], true, 0); pressedMod[k] = true; }
    };
    auto releaseMods = [&] { for (int k = 2; k >= 0; --k) if (pressedMod[k]) inKey(gs, modKey[k], false, 0); };
    auto release = [&](int x, int y) {
        if (hold > 0) { g_deferredUp.push_back({ tick + hold, x, y, button }); return; }
        inButton(gs, button, false);
    };

    int x = g_inX, y = g_inY, x2 = 0, y2 = 0;
    const std::string& v = l.verb;
    if (v == "mouse" || v == "click" || v == "rclick" || v == "mclick" || v == "dblclick" || v == "down" || v == "up") {
        if (!pos(x, y)) { logf("input: tick %d %s: bad position", tick, v.c_str()); return; }
        words();
        if (v == "rclick") button = 1;
        if (v == "mclick") button = 2;
        if (v == "dblclick" || v == "down" || v == "up" || v == "mouse") hold = 0;
        logInputAt(gs, tick, v, x, y);
        holdMods();
        inMove(gs, x, y);
        const bool press = v == "down" || v == "click" || v == "rclick" || v == "mclick" || v == "dblclick";
        if (press) inButton(gs, button, true);
        if (v == "up" || (press && v != "down")) release(x, y);
        if (v == "dblclick") { inButton(gs, button, true); inButton(gs, button, false); }
        releaseMods();
    } else if (v == "drag") {
        if (!pos(x, y) || !pos(x2, y2)) { logf("input: tick %d drag: bad position", tick); return; }
        words();
        logInputAt(gs, tick, v, x, y);
        logInputAt(gs, tick, v, x2, y2);
        holdMods();
        inMove(gs, x, y);
        inButton(gs, button, true);
        for (int s = 1; s <= 4; ++s) inMove(gs, x + (x2 - x) * s / 4, y + (y2 - y) * s / 4);
        release(x2, y2);
        releaseMods();
    } else if (v == "select") {
        // A left click on the unit, centring the view on it first if it is off the view.
        const int id = unitArg(a.empty() ? "" : a[0]);
        if (!alive(id)) { logf("input: tick %d select: no unit %s", tick, a.empty() ? "" : a[0].c_str()); return; }
        mapToWindow(gs, unitOX(g_units[id]), unitOY(g_units[id]), x, y);
        int vx = 0, vy = 0, vw = 0, vh = 0;
        gs.debugViewRect(vx, vy, vw, vh);
        if (x < vx || y < vy || x >= vx + vw || y >= vy + vh) {
            requestCenterView(unitOX(g_units[id]), unitOY(g_units[id]));
            gs.onIdle(*g_renderer, 0.0f);   // the view moves when drawn
            mapToWindow(gs, unitOX(g_units[id]), unitOY(g_units[id]), x, y);
        }
        logInputAt(gs, tick, v, x, y);
        inMove(gs, x, y);
        inButton(gs, 0, true);
        inButton(gs, 0, false);
    } else if (v == "key" || v == "keydown" || v == "keyup") {
        for (const std::string& s : a) {
            KeySpec k;
            if (!parseKey(s, k)) { logf("input: tick %d %s: unknown key %s", tick, v.c_str(), s.c_str()); continue; }
            logf("input: tick %d %s %s (key 0x%x%s%s%s)", tick, v.c_str(), s.c_str(), static_cast<unsigned>(k.sym),
                 k.shift ? " shift" : "", k.ctrl ? " ctrl" : "", k.alt ? " alt" : "");
            if (v == "key") inPressKey(gs, k);
            else inKey(gs, k.sym, v == "keydown", v == "keydown" ? k.ch : 0);
        }
    } else if (v == "type") {
        std::string text;
        for (const std::string& s : a) text += (text.empty() ? "" : " ") + s;
        logf("input: tick %d type \"%s\"", tick, text.c_str());
        for (char ch : text) {
            KeySpec k;
            if (parseKey(std::string(1, ch), k)) inPressKey(gs, k);
        }
    } else if (v == "uilist") {
        for (const std::string& line : gs.harnessUiList()) logf("ui: %s", line.c_str());
    } else if (v == "hotkeys") {
        logf("input: tick %d hotkeys: the original's button hotkey table isn't modeled here", tick);
    }
}

// Releases due this tick (`hold N`), each at its own point.
void runDeferredInput(GameState& gs, int tick) {
    for (size_t k = 0; k < g_deferredUp.size();) {
        if (g_deferredUp[k].tick > tick) { ++k; continue; }
        const DeferredUp d = g_deferredUp[k];
        g_deferredUp.erase(g_deferredUp.begin() + static_cast<long>(k));
        inMove(gs, d.x, d.y);
        inButton(gs, d.button, false);
    }
}

// Telemetry extension for input runs, as the harness's InputStateJson: the selection and the view's top-left corner.
void writeInputState(FILE* f, GameState& gs) {
    std::fputs(",\"selected\":[", f);
    const std::vector<int>& sel = gs.debugSelection();
    for (size_t k = 0; k < sel.size() && k < 32; ++k) std::fprintf(f, "%s%d", k ? "," : "", sel[k]);
    int cx = 0, cy = 0;
    gs.debugCamera(cx, cy);
    std::fprintf(f, "],\"view\":[%d,%d]", cx, cy);
}

// ---- Audio log -------------------------------------------------------------------------
// audio.jsonl with --audio, as the harness writes it (OP2H_AUDIO): every sound the game
// asks for, whether or not sound is on. `sfx` an effect (x = -1 without a position),
// `savant` a voice line for the queue, `play` the line the queue starts, `msg` a status
// message with the throttle's verdict (`pass` 1 shown, 0 dropped, -1 not a throttled id),
// `music` the playlist when it changes, and a `tables` line with each sound's length.
// The queue and the throttle run on the harness's virtual clock (tick * 1000 / 36 ms from a
// base of 100000000), and the queue takes its turn on the ticks the harness gives it
// (tick & 3 == 2, after the tick's script). Positions are file pixels. Left out: `at` (the
// return address in Outpost2.exe of the call) and the tables' Savant priorities (the
// port's queue has none: first in, first out).

FILE* g_audio = nullptr;
std::vector<int> g_musicList;
int g_musicRepeat = -99, g_musicPos = -99;

uint32_t audioClock() { return 100000000u + static_cast<uint32_t>(static_cast<int64_t>(TethysGame::tick()) * 1000 / 36); }

void audioEvent(const char* ev, int id, int x, int y, int pass) {
    if (!g_audio) return;
    std::fprintf(g_audio, "{\"tick\":%d,\"ev\":\"%s\",\"id\":%d,\"name\":\"%s\",\"x\":%d,\"y\":%d", TethysGame::tick(), ev, id,
                 soundName(id), x, y);
    if (pass != -2) std::fprintf(g_audio, ",\"pass\":%d", pass);
    std::fputs("}\n", g_audio);
}

void logMusic(const char* when) {
    if (!g_audio) return;
    std::vector<int> list;
    int repeat = 0, pos = 0;
    musicDebugState(list, repeat, pos);
    if (list == g_musicList && repeat == g_musicRepeat && pos == g_musicPos) return;
    g_musicList = list; g_musicRepeat = repeat; g_musicPos = pos;
    std::fprintf(g_audio, "{\"tick\":%d,\"ev\":\"music\",\"when\":\"%s\",\"songs\":[", TethysGame::tick(), when);
    for (size_t i = 0; i < list.size(); ++i) std::fprintf(g_audio, "%s%d", i ? "," : "", list[i]);
    std::fprintf(g_audio, "],\"repeat\":%d,\"pos\":%d}\n", repeat, pos);
}

void writeAudioTables() {
    if (!g_audio) return;
    std::fprintf(g_audio, "{\"tick\":%d,\"ev\":\"tables\",\"ms\":[", TethysGame::tick());
    for (int id = 0; id < 0xFA; ++id) std::fprintf(g_audio, "%s%d", id ? "," : "", soundLengthMs(id));
    std::fputs("]}\n", g_audio);
}

// After the tick's script: the queue's turn, then the music.
void audioTick(int tick) {
    if (!g_audio) return;
    if ((tick & 3) == 2) soundHarnessPump();
    logMusic("tick");
    std::fflush(g_audio);
}

// ---- Game data -------------------------------------------------------------------------
// gamedata.json, as the harness's WriteGameData writes it at the first cycle: the map's
// geometry, the cell types' speed table, every unit type's stats as loaded (the 17 words of
// player 0's block, plus a building's footprint, dock, rubble and flags and a weapon's
// radius, shortest shot and range mode), with --eco the morale, mine and tech tables and
// each type's resource priority and research topic, and the cell type of every tile.
// Left out where the port doesn't keep them: the cell types' names, `vehicleFlags` of
// the types that aren't buildings, walls or weapons (the original writes the low word
// of +0x248 for every type), and the morale `maxima`.

void writeGameData(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    const int W = g_gameMap.width(), H = g_gameMap.height(), IW = g_gameMap.internalWidth();
    const MAP_RECT& clip = g_gameMap.clipRect();
    std::fprintf(f, "{\"map\":{\"width\":%d,\"height\":%d,\"paddedWidth\":%d,\"paddingTiles\":%d,\"tileXMask\":%u,"
                    "\"pixelWidth\":%d,\"clipRect\":[%d,%d,%d,%d]},\n",
                 W, H, IW, xOffset(), static_cast<unsigned>(IW - 1), IW * 32, clip.x1, clip.y1, clip.x2, clip.y2);
    // The terrain table's columns are the five tracks' speeds, then blocks-line-of-sight and blight speed.
    std::fputs("\"trackTypes\":[\"Legged\",\"Wheeled\",\"Miner\",\"Tracked\",\"Hover\"],\n\"cellTypes\":[", f);
    for (int c = 0; c < kNumCellTypes; ++c) {
        std::fprintf(f, "%s\n {\"id\":%d,\"trackSpeed\":[", c ? "," : "", c);
        for (int t = 0; t < 5; ++t) std::fprintf(f, "%s%d", t ? "," : "", terrainMoveCost(c, t));
        std::fprintf(f, "],\"blocksLineOfSight\":%d,\"blightSpeed\":%d}", terrainMoveCost(c, 5), terrainMoveCost(c, 6));
    }
    std::fputs("],\n\"unitTypes\":[", f);
    bool first = true;
    for (int i = 1; i <= 0x72; ++i) {
        const char* code = unitCodeName(i);
        if (!code) continue;
        int s[17] = {};
        if (const uint8_t* b = unitStatBlock(i, 0)) std::memcpy(s, b, sizeof(s));
        const char* name = unitDisplayName(i);
        std::fprintf(f, "%s\n {\"mapId\":%d,\"code\":\"%s\",\"name\":\"%s\",\"track\":%d,\"hp\":%d,\"armor\":%d,\"sight\":%d,"
                        "\"moveSpeed\":%d,\"turnRate\":%d,\"reload\":%d",
                     first ? "" : ",", i, code, name ? name : "", mapIdIsVehicle(i) ? vehicleTrackClass(i) : 0, s[0], s[2], s[6],
                     s[9], s[10], s[12]);
        const StructSheetDesc* bd = mapIdIsBuilding(i) ? structSheet(i) : (i >= 0x11 && i <= 0x14) ? wallSheet(i) : nullptr;
        const WeaponSheetDesc* wd = isWeaponFire(i) ? weaponSheet(i) : nullptr;
        if (bd) std::fprintf(f, ",\"vehicleFlags\":%d", bd->sizeX & 0xFFFF);
        else if (wd) std::fprintf(f, ",\"vehicleFlags\":%d", wd->radius & 0xFFFF);
        std::fputs(",\"stats\":[", f);
        for (int k = 0; k < 17; ++k) std::fprintf(f, "%s%d", k ? "," : "", s[k]);
        std::fputc(']', f);
        if (mapIdIsBuilding(i) && bd)
            std::fprintf(f, ",\"size\":[%d,%d],\"dock\":[%d,%d],\"rubble\":[%d,%d],\"buildingFlags\":%u", bd->sizeX, bd->sizeY,
                         bd->dockLocE, bd->dockLocP, bd->commonRubble, bd->rareRubble, bd->flags24C);
        if (wd) {
            std::fprintf(f, ",\"pixelsSkipped\":%d,\"radius\":%d", wd->skipped, wd->radius);
            const int mode = weaponRangeMode(i);
            if (mode >= 0) std::fprintf(f, ",\"indirect\":%d", mode);
        }
        std::fputc('}', f);
        first = false;
    }
    std::fputc(']', f);
    if (g_eco) {
        static const char* const kLevels[5] = {"UTOPIA", "GROOVY", "OKAYFINE", "UPSET", "WAYBAD"};
        std::fputs(",\n\"morale\":{\"levels\":[", f);
        for (int k = 0; k < 5; ++k) {
            const MoraleRow& m = moraleRow(k);
            std::fprintf(f, "%s{\"name\":\"%s\",\"v\":[%d,%d,%d,%d,%d,%d,%d,%d]}", k ? "," : "", kLevels[k], m.points, m.powBonus,
                         m.rschBonus, m.factBonus, m.foodBonus, m.defectRate, m.fertRate, m.dieRate);
        }
        std::fputs("],\"modifiers\":[", f);
        for (int k = 0; k < moraleEntryCount(); ++k)
            std::fprintf(f, "%s[\"%s\",%d,%d]", k ? "," : "", moraleEntryName(k), moraleEntryValue(k), moraleEntryPct(k) ? 1 : 0);
        std::fputs("]},\n\"mines\":[", f);
        for (int k = 0; k < 9; ++k) {
            int initial, peakTruck, peak, minTruck, min;
            mineSheetRow(k / 3, k % 3, initial, peakTruck, peak, minTruck, min);
            std::fprintf(f, "%s{\"variant\":%d,\"bar\":%d,\"trucks\":[0,%d,%d],\"yield\":[%d,%d,%d]}", k ? "," : "", k / 3, k % 3,
                         peakTruck, minTruck, initial, peak, min);
        }
        // Tech tree as loaded, in techNum order: [techID, plymouthCost, edenCost, maxScientists, lab, category].
        std::fputs("],\n\"techTree\":[", f);
        for (int k = 0; k < techCount(); ++k) {
            const Tech* t = techAt(k);
            std::fprintf(f, "%s[%d,%d,%d,%d,%d,%d]", k ? "," : "", t ? t->id : 0, t ? t->plymouthCost : 0, t ? t->edenCost : 0,
                         t ? t->maxScientists : 0, t ? t->lab : 0, t ? t->category : 0);
        }
        // Per type: [mapId, resource priority (+0x2A0), research topic (+0x1E4, the tech's index once loaded)].
        std::fputs("],\n\"typeExtra\":[", f);
        first = true;
        for (int i = 1; i <= 0x72; ++i) {
            if (!unitCodeName(i)) continue;
            const StructSheetDesc* bd = mapIdIsBuilding(i) ? structSheet(i) : (i >= 0x11 && i <= 0x14) ? wallSheet(i) : nullptr;
            const int topic = unitResearchTopic(i);
            std::fprintf(f, "%s[%d,%d,%d]", first ? "" : ",", i, bd ? bd->resourcePriority : 0, topic > 0 ? techIndex(topic) : 0);
            first = false;
        }
        std::fputc(']', f);
    }
    // Cell type of every tile in file coordinates (row-major).
    std::fputs(",\n\"cells\":[", f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) std::fprintf(f, "%s%d", (x | y) ? "," : "", g_gameMap.getCellType(LOCATION{x, y}));
    std::fputs("]}\n", f);
    std::fclose(f);
}

// ---- Telemetry -------------------------------------------------------------------------
// telemetry.jsonl, as the harness's WriteTelemetry writes it: one line per sample with
// each player's colony and every building and vehicle of each player. x/y are the
// original's internal tiles (the port's tile + the x offset). With --eco, the colony's
// allocation, food, population and morale fields as the harness names them, the unit
// internals and the mining beacons. Fields the port doesn't model are left out (never
// written as 0): eco's maxFood, morale08, morale24 and rcc, the raw `pi` dwords and the
// units' `raw` bytes and `hive`.


void writeTelemUnit(FILE* f, bool& first, int id) {
    if (!alive(id)) return;
    const UnitRecord& r = g_units[id];
    const int hp = maxHP(r);
    std::fprintf(f, "%s{\"id\":%d,\"type\":\"%s\",\"owner\":%d,\"x\":%d,\"y\":%d,\"hp\":%d,\"maxHp\":%d,\"cmd\":\"%s\"",
                 first ? "" : ",", id, codeName(r.type), r.owner & 0x0F, (unitOX(r) >> 5) + xOffset(), unitOY(r) >> 5,
                 hp - r.damage, hp, commandName(r.action));
    // Unit::GetWeapon is the +0x24 word of any vehicle: the weapon, a ConVec's kit, a truck's packed cargo.
    if ((r.flags & UNIT_VEHICLE) && r.cargo > 0) std::fprintf(f, ",\"cargo\":\"%s\"", codeName(r.cargo));
    if (g_eco) {
        std::fprintf(f, ",\"flags\":%u,\"act\":\"%s\",\"timer\":%d,\"busy\":%d,\"w24\":%u", r.flags, actionName(r.field22),
                     r.field3C, r.field20, static_cast<unsigned>(r.cargo));
        if (r.type == mapCargoTruck) std::fprintf(f, ",\"truck\":[%d,%d]", r.cargo & 0xF, (r.cargo >> 4) & 0xFFF);
        int dx, dy;
        if ((r.flags & UNIT_BUILDING) && dockCell(id, dx, dy)) std::fprintf(f, ",\"dock\":[%d,%d]", dx + xOffset(), dy);
    }
    std::fputc('}', f);
    first = false;
}

void writePlayerEco(FILE* f, int p) {
    const ColonyState& c = g_colony[p];
    std::fprintf(f, ",\"eco\":{\"maxCommon\":%d,\"maxRare\":%d,\"availWorkers\":%d,\"availScientists\":%d,"
                    "\"workersReq\":%d,\"scientistsReq\":%d,\"sciAsWorkers\":%d,\"sciResearching\":%d,\"powerAvail\":%d,"
                    "\"powerInactive\":%d,\"foodProd\":%d,\"foodCons\":%d,\"foodLacking\":%d,\"active\":%d,\"unpowered\":%d,"
                    "\"recalc\":%d,\"starveCase\":%d,\"growth\":[%d,%d,0],\"death\":[%d,%d,%d],\"caps\":[%d,%d,%d,%d],"
                    "\"activeCC\":%d,\"nurseries\":%d,\"universities\":%d,\"moraleEvent\":%d,\"morale10\":%d,"
                    "\"resDemand\":%d,\"foodSupply\":%d,\"disabledRatio\":%d,\"recForumDemand\":%d,"
                    "\"medDemand\":%d,\"unoccupied\":%d,\"sciAsWorkersPct\":%d}",
                 c.oreCapacity, c.rareCapacity, c.workersLeft, c.scientistsLeft, c.workersRequired, c.scientistsRequired,
                 c.scientistsAsWorkers, c.researchAssigned, c.powerLeft, c.powerIdle, c.foodProduced, c.foodEaten,
                 c.foodDeficit, c.activeCount, c.unpoweredCount, c.dirty, c.starveRotor,
                 c.accBirth, c.accGrowUp,                                   // kids +0x84, workers +0x80 (scientists +0x7C: none)
                 c.accKidDeath, c.accWorkerDeath, c.accSciDeath,            // +0x88, +0x90, +0x8C
                 c.stat(ST_RES_CAP), c.stat(ST_MED_CAP), c.stat(ST_REC_CAP), c.stat(ST_FORUM_CAP),
                 c.stat(ST_CC_OP), c.stat(ST_NURSERY), c.stat(ST_UNIVERSITY), c.moraleEvents, c.moraleTrend, c.crowding,
                 c.foodState, c.disabledPct, c.recreationLoad, c.medicalLoad, c.idlePct, c.phdPct);
}

int g_lastTelem = -1;

void writeTelemetry(int tick) {
    FILE* f = g_telem;
    if (!f || tick == g_lastTelem) return;
    g_lastTelem = tick;
    std::fprintf(f, "{\"tick\":%d,\"mark\":%d,\"end\":%d,\"players\":[", tick, tick / 100, gameEndCode());
    const int players = TethysGame::noPlayers();
    for (int i = 0; i < players; ++i) {
        const PlayerData& d = g_playerData[i];
        const ColonyState& c = g_colony[i];
        // SatelliteCounts (+0x08): RLVs in bits 0-3, solar satellites 4-7, the space bits from 8.
        const unsigned sats = (d.rlvCount & 0xF) | (d.solarSat & 0xF) << 4 | d.spaceBits << 8;
        std::fprintf(f, "%s{\"id\":%d,\"human\":%d,\"eden\":%d,\"food\":%d,\"commonOre\":%d,\"rareOre\":%d,\"workers\":%d,"
                        "\"scientists\":%d,\"kids\":%d,\"morale\":%d,\"moraleLevel\":%d,\"event\":%d,\"satellites\":%u,"
                        "\"powerGen\":%d,\"powerUsed\":%d,\"foodNet\":%d,\"buildings\":%d,\"idleBuildings\":%d,\"techs\":[",
                     i ? "," : "", i, d.isHuman != 0, d.isEden != 0, d.foodStored, d.ore, d.rareOre, d.workers,
                     d.scientists, d.kids, c.moraleValue, d.morale, c.moraleEvents, sats, c.powerGenerated,
                     c.powerRequired, c.foodNet, c.structCount, c.inactiveCount);
        bool first = true;
        for (int t = 0; t < techCount(); ++t)
            if (researchHas(i, t)) { std::fprintf(f, "%s%d", first ? "" : ",", techAt(t)->id); first = false; }
        std::fputc(']', f);
        if (g_eco) writePlayerEco(f, i);
        std::fputc('}', f);
    }
    std::fputs("],\"units\":[", f);
    bool first = true;
    for (int p = 0; p < players; ++p) {         // each player's buildings, then its vehicles, as the harness lists them
        for (int id : colonyStructures(p)) writeTelemUnit(f, first, id);
        for (int id : colonyVehicles(p)) writeTelemUnit(f, first, id);
    }
    std::fputc(']', f);
    if (g_eco) {
        std::fputs(",\"beacons\":[", f);
        first = true;
        for (size_t id = 1; id < g_units.size(); ++id) {
            const UnitRecord& r = g_units[id];
            if (r.type != 0x51 || !alive(static_cast<int>(id))) continue;   // BEACON
            std::fprintf(f, "%s{\"x\":%d,\"y\":%d,\"ore\":%d,\"bar\":%d,\"variant\":%d}", first ? "" : ",",
                         (unitOX(r) >> 5) + xOffset(), unitOY(r) >> 5, r.beaconRare != 0, r.beaconYield, r.beaconVariant);
            first = false;
        }
        std::fputc(']', f);
    }
    if (g_inputUsed && g_gs) writeInputState(f, *g_gs);
    std::fputs("}\n", f);
    std::fflush(f);
}

// ---- Screenshots -----------------------------------------------------------------------

bool writeBmp(const std::string& path, const uint32_t* px, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t stride = static_cast<uint32_t>(w) * 3 + 3 & ~3u, image = stride * h;
    uint8_t hdr[54] = { 'B', 'M' };
    auto put32 = [&](int at, uint32_t v) { std::memcpy(hdr + at, &v, 4); };
    put32(2, 54 + image); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
    hdr[26] = 1; hdr[28] = 24; put32(34, image);
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(stride, 0);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t c = px[y * w + x];
            row[x * 3] = c & 0xFF; row[x * 3 + 1] = (c >> 8) & 0xFF; row[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        std::fwrite(row.data(), 1, stride, f);
    }
    std::fclose(f);
    return true;
}

void takeShot(GameState& gs, Renderer& renderer, const std::string& out, int tick) {
    gs.onIdle(renderer, 0.0f);   // no time passes: draws the frame (and applies a pending view)
    char name[64];
    std::snprintf(name, sizeof(name), "/shot_%07d.bmp", tick);
    writeBmp(out + name, renderer.getFramebuffer(), renderer.getWidth(), renderer.getHeight());
}

// The `shot` verb: a screenshot on the spot, of the frame drawn now.
std::string g_outDir = ".";
void shotNow(int tick) {
    if (g_gs && g_renderer) takeShot(*g_gs, *g_renderer, g_outDir, tick);
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--harness") continue;
        else if (a == "--script") o.script = next();
        else if (a == "--out") o.out = next();
        else if (a == "--map") o.map = next();
        else if (a == "--techtree") o.techtree = next();
        else if (a == "--ticks") o.ticks = std::atoi(next());
        else if (a == "--seed") o.seed = std::atoi(next());
        else if (a == "--trace-every") o.traceEvery = std::atoi(next());
        else if (a == "--shot-every") o.shotEvery = std::atoi(next());
        else if (a == "--telemetry-every") o.telemEvery = std::atoi(next());
        else if (a == "--eco") o.eco = true;
        else if (a == "--audio") o.audio = true;
        else if (a == "--show") o.show = true;
        else if (a == "--size") {
            if (std::sscanf(next(), "%dx%d", &o.width, &o.height) != 2) return false;
        } else {
            std::fprintf(stderr, "--harness: unknown option %s\n", a.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int runHarness(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) return 2;
    g_log = std::fopen((o.out + "/harness.log").c_str(), "w");
    if (!g_log) { std::fprintf(stderr, "--harness: can't write %s/harness.log\n", o.out.c_str()); return 2; }
    logf("harness: port, map=%s ticks=%d seed=%d trace=%d shot=%d telemetry=%d eco=%d audio=%d", o.map.c_str(), o.ticks,
         o.seed, o.traceEvery, o.shotEvery, o.telemEvery, o.eco ? 1 : 0, o.audio ? 1 : 0);
    std::vector<Line> script;
    if (!o.script.empty() && !loadScript(o.script, script)) { logf("FATAL: can't read script %s", o.script.c_str()); return 2; }
    logf("script: %u commands from %s", static_cast<unsigned>(script.size()), o.script.c_str());

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { logf("FATAL: SDL_Init: %s", SDL_GetError()); return 1; }
    g_ResManager.init(".");
    g_art.load();
    Renderer renderer;
    if (!renderer.init("OP2 harness", o.width, o.height, false)) { logf("FATAL: no window"); SDL_Quit(); return 1; }
    if (!o.show)
        if (SDL_Window* w = SDL_GetWindowFromID(1)) SDL_HideWindow(w);

    auto mission = std::make_unique<MissionScript>();
    mission->dllName = "harness scenario";
    mission->mapName = o.map;
    mission->levelDesc = g_desc.levelDesc;
    mission->techtreeName = o.techtree;
    g_desc.mapName = mission->mapName.c_str();
    g_desc.techtreeName = mission->techtreeName.c_str();
    mission->desc = &g_desc;
    mission->initProc = &scenarioInitProc;
    mission->aiProc = &scenarioAIProc;
    // The voice queue and the message throttle run on the harness's virtual clock, so a run doesn't depend on how
    // fast it plays; with --audio every sound request also goes to audio.jsonl.
    g_audio = o.audio ? std::fopen((o.out + "/audio.jsonl").c_str(), "w") : nullptr;
    soundSetHarness(&audioClock, g_audio ? &audioEvent : nullptr);
    g_outDir = o.out;
    GameState gs;
    gs.setSeed(static_cast<uint32_t>(o.seed));
    if (!gs.startMission(std::move(mission))) {
        logf("FATAL: the scenario didn't start (map %s)", o.map.c_str());
        renderer.shutdown(); SDL_Quit();
        return 2;
    }
    logf("start: map %dx%d, x offset %d, players %d, rng [%u,%u]", g_gameMap.width(), g_gameMap.height(),
         g_gameMap.internalXOffset(), TethysGame::noPlayers(), TethysGame::rng().s0(), TethysGame::rng().s1());
    logMusic("start");

    g_trace = o.traceEvery > 0 ? std::fopen((o.out + "/trace.jsonl").c_str(), "w") : nullptr;
    g_telem = o.telemEvery > 0 ? std::fopen((o.out + "/telemetry.jsonl").c_str(), "w") : nullptr;
    g_eco = o.eco;
    size_t pos = 0;
    int tick = 0, nextShot = 5, nextTelem = 0, nextTrace = 0;
    bool wroteGameData = false;
    g_renderer = &renderer;
    g_gs = &gs;
    g_inputUsed = std::any_of(script.begin(), script.end(), [](const Line& l) { return isInputVerb(l.verb); });
    // The cursor starts where the harness parks it: over the side column, clear of the view and the edges.
    if (g_inputUsed) inMove(gs, renderer.getWidth() - 90, renderer.getHeight() / 2);
    const char* why = "tick limit";
    const Uint32 t0 = SDL_GetTicks();
    for (;;) {
        const bool ended = gs.simTick();
        tick = TethysGame::tick();
        if (!wroteGameData) { writeGameData(o.out + "/gamedata.json"); writeAudioTables(); wroteGameData = true; }
        // With input, a frame before the tick's input (the original draws between its ticks): the layout and the
        // buttons are the ones on screen, and a view the script asked for is applied.
        if (g_inputUsed) { gs.onIdle(renderer, 0.0f); runDeferredInput(gs, tick); }
        while (pos < script.size() && script[pos].tick <= tick) runLine(gs, script[pos++], tick);
        audioTick(tick);
        if (std::find(g_pendingShots.begin(), g_pendingShots.end(), tick) != g_pendingShots.end())
            takeShot(gs, renderer, o.out, tick);
        // Telemetry at the first tick, then at each multiple of the interval (the harness's g_nextTelem).
        if (g_telem && tick >= nextTelem) { writeTelemetry(tick); nextTelem = (tick / o.telemEvery + 1) * o.telemEvery; }
        // Trace at the first tick, then at each multiple of the interval (the harness's g_nextTrace).
        if (g_trace && tick >= nextTrace) { writeTrace(tick); nextTrace = (tick / o.traceEvery + 1) * o.traceEvery; }
        if (o.shotEvery > 0 && tick >= nextShot) {
            takeShot(gs, renderer, o.out, tick);
            nextShot = (tick / o.shotEvery + 1) * o.shotEvery;
        }
        if (ended) { why = "mission ended"; break; }
        if (o.ticks > 0 && tick >= o.ticks) break;
    }
    const double secs = (SDL_GetTicks() - t0) / 1000.0;
    if (o.shotEvery > 0 && tick % o.shotEvery) takeShot(gs, renderer, o.out, tick);
    writeTelemetry(tick);                  // and one at the end
    writeTrace(tick);
    if (g_telem) std::fclose(g_telem);
    logf("quit at tick %d: %s (%.2fs, %.0f ticks/s)", tick, why, secs, tick / (secs > 0 ? secs : 1));
    if (g_trace) std::fclose(g_trace);
    if (g_audio) std::fclose(g_audio);
    std::fclose(g_log);
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

}  // namespace op2
