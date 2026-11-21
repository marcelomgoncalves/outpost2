// --harness: plays a scenario headless, tick by tick, and writes what happened in the
// file formats of the op2-rs test harness (gitlab.com/bufke/op2-rs, harness/), so the
// port can be compared field by field with the original game playing the same recipe.
//
//   outpost2_modern.exe --harness --script S --out DIR --map cm01.map --ticks N
//                       [--seed N] [--techtree multitek.txt] [--trace-every N]
//                       [--shot-every N] [--size WxH] [--show]
//
// The script is the harness's: one `<tick> <verb> <args...>` per line, '#' starts a
// comment; lines at tick T run after the cycle of tick T, as the harness runs them
// (the first cycle is tick 1, so tick 0 lines run there). Coordinates are "file"
// tiles after `coords file` (the port's own tiles) or the original's internal tiles
// otherwise. A unit is `@N`, the Nth spawn, or a unit id.
//
// Outputs in DIR:
//   trace.jsonl   per traced tick: each spawn still alive, with the harness's field
//                 names (px/py/fix/dir/hp/weapon/tur/reload/tgt/lights...) and the RNG
//   harness.log   what ran, spawns, unsupported verbs, the end of the run
//   shot_<tick>.bmp  the game view every --shot-every ticks
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
#include "game/missile.h"
#include "game/research.h"
#include "game/trigger.h"
#include "game/walls.h"
#include "game/command.h"
#include "game/game_map.h"
#include "game/player.h"
#include "game/scenario.h"
#include "game/tethys_game.h"
#include "game/unit.h"
#include "game/unit_names.h"
#include "gfx/artfile.h"
#include "gfx/renderer.h"
#include "mission/mission.h"
#include "res/res_manager.h"

namespace op2 {
namespace {

struct Options {
    std::string script, out = ".", map = "cm01.map", techtree = "multitek.txt";
    int ticks = 3000, seed = 1, traceEvery = 1, shotEvery = 0, width = 1024, height = 768;
    bool show = false;
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
    return true;
}

std::vector<int> g_spawned;   // spawn index -> unit id (0 = failed)
std::vector<int> g_spawnType;
std::vector<bool> g_spawnGone;

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
    if (l.verb == "shot" || l.verb == "trace" || l.verb == "telemetry") return;   // captures run on their own
    if (l.verb == "group") { runGroup(l); return; }
    if (runEco(l, tick)) return;

    // Orders: the first argument is the unit.
    static const char* const orders[] = {
        "move", "dock", "dockeg", "movewp", "standground", "patrol", "attack", "guard", "attackground", "lights", "stop",
        "selfdestruct" };
    if (std::none_of(std::begin(orders), std::end(orders), [&](const char* n) { return l.verb == n; })) {
        // Verbs the original harness has that need what this mode doesn't do yet: real mouse and keyboard input to the
        // game's window, recorded packets, saves. Any other verb is unknown to the original harness too, which logs
        // it and goes on.
        static const char* const later[] = {
            "mouse", "click", "rclick", "mclick", "dblclick", "down", "up", "drag", "key", "keydown", "keyup", "type",
            "select", "uilist", "hotkeys", "packet", "savegame", "loadgame", "gamespeed", "name", "quit" };
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

void writeUnit(FILE* f, bool& first, int id, int spawn) {
    const UnitRecord& r = g_units[id];
    const int px = unitOX(r), py = unitOY(r);
    // 16.16 position: the step's 16-bit fraction is biased so 0x8000 is "on px"; it reads 0 before the first step.
    // It lives in the order block (+0x38, the original's path context), so it only counts while there is one.
    long long fx = static_cast<long long>(px) << 16, fy = static_cast<long long>(py) << 16;
    if ((r.flags & UNIT_VEHICLE) && r.hasOrder && (r.fracX | r.fracY)) {
        fx += static_cast<int>(r.fracX) - 0x8000;
        fy += static_cast<int>(r.fracY) - 0x8000;
    }
    std::fprintf(f, "%s{\"id\":%d,\"spawn\":%d,\"type\":\"%s\",\"owner\":%d,\"px\":%d,\"py\":%d,\"sx\":%.6f,\"sy\":%.6f,"
                    "\"fix\":[%lld,%lld],\"dir\":%d,\"hp\":%d,\"actionId\":%d",
                 first ? "" : ",", id, spawn, codeName(r.type), r.owner & 0x0F, px, py, fx / 65536.0, fy / 65536.0, fx, fy,
                 r.rotation, maxHP(r) - r.damage, r.action);
    if (r.flags & UNIT_VEHICLE) std::fprintf(f, ",\"lights\":%d", static_cast<int>(r.flags & UNIT_LIGHTS));
    // Armed vehicles and Guard Posts: the weapon, turret direction, reload and target
    // (high word 0x7FFF = none, 0xFFFF = a unit in the low word, else a tile).
    const int w = r.cargo;
    if (w >= 0x3B && w <= 0x4B && ((r.flags & UNIT_VEHICLE) || (r.flags & UNIT_TURRET_BLD))) {
        std::fprintf(f, ",\"weapon\":\"%s\",\"tur\":%d,\"reload\":%d,\"tgt\":", codeName(w), r.turretDir, r.reload);
        const uint16_t hi = static_cast<uint16_t>(r.field4E), lo = static_cast<uint16_t>(r.target4C);
        if (hi == 0x7FFF) std::fputs("null", f);
        else if (hi == 0xFFFF) std::fprintf(f, "%d", static_cast<int>(lo));
        else std::fprintf(f, "[%d,%d]", static_cast<int>(static_cast<int16_t>(lo)), static_cast<int>(static_cast<int16_t>(hi)));
    }
    std::fputc('}', f);
    first = false;
}

void writeTrace(FILE* f, int tick) {
    std::fprintf(f, "{\"tick\":%d,\"units\":[", tick);
    bool first = true;
    for (size_t i = 0; i < g_spawned.size(); ++i) {
        if (g_spawnGone[i]) continue;
        const int id = g_spawned[i];
        if (!alive(id) || g_units[id].type != g_spawnType[i]) { g_spawnGone[i] = true; continue; }
        writeUnit(f, first, id, static_cast<int>(i));
    }
    std::fprintf(f, "],\"rng\":[%u,%u]}\n", TethysGame::rng().s0(), TethysGame::rng().s1());
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
    logf("harness: port, map=%s ticks=%d seed=%d trace=%d shot=%d", o.map.c_str(), o.ticks, o.seed, o.traceEvery,
         o.shotEvery);
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
    GameState gs;
    gs.setSeed(static_cast<uint32_t>(o.seed));
    if (!gs.startMission(std::move(mission))) {
        logf("FATAL: the scenario didn't start (map %s)", o.map.c_str());
        renderer.shutdown(); SDL_Quit();
        return 2;
    }
    logf("start: map %dx%d, x offset %d, players %d, rng [%u,%u]", g_gameMap.width(), g_gameMap.height(),
         g_gameMap.internalXOffset(), TethysGame::noPlayers(), TethysGame::rng().s0(), TethysGame::rng().s1());

    FILE* trace = o.traceEvery > 0 ? std::fopen((o.out + "/trace.jsonl").c_str(), "w") : nullptr;
    size_t pos = 0;
    int tick = 0, nextShot = 5;
    const char* why = "tick limit";
    const Uint32 t0 = SDL_GetTicks();
    for (;;) {
        const bool ended = gs.simTick();
        tick = TethysGame::tick();
        while (pos < script.size() && script[pos].tick <= tick) runLine(gs, script[pos++], tick);
        if (trace && tick % o.traceEvery == 0) writeTrace(trace, tick);
        if (o.shotEvery > 0 && tick >= nextShot) {
            takeShot(gs, renderer, o.out, tick);
            nextShot = (tick / o.shotEvery + 1) * o.shotEvery;
        }
        if (ended) { why = "mission ended"; break; }
        if (o.ticks > 0 && tick >= o.ticks) break;
    }
    const double secs = (SDL_GetTicks() - t0) / 1000.0;
    if (o.shotEvery > 0 && tick % o.shotEvery) takeShot(gs, renderer, o.out, tick);
    logf("quit at tick %d: %s (%.2fs, %.0f ticks/s)", tick, why, secs, tick / (secs > 0 ? secs : 1));
    if (trace) std::fclose(trace);
    std::fclose(g_log);
    renderer.shutdown();
    SDL_Quit();
    return 0;
}

}  // namespace op2
