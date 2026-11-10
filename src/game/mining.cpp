#include "../net/net_game.h"
#include "mining.h"
#include "salvage.h"
#include "../audio/sound.h"

#include <cstdlib>
#include <string>

#include "building_sheet.h"
#include "colony.h"
#include "command.h"
#include "construction.h"
#include "docking.h"
#include "game_map.h"
#include "player.h"
#include "tethys_game.h"
#include "unit.h"
#include "vehicle_anim.h"
#include "weapon.h"
#include "../res/res_manager.h"

namespace op2 {

static bool valid(int id) { return id > 0 && id < static_cast<int>(g_units.size()) && g_units[id].id != -1; }
static int ownerOf(const UnitRecord& r) { return r.owner & 0x0F; }
static int statsOf(const UnitRecord& r) { return (r.owner >> 4) & 0x0F; }
static bool isEden(const UnitRecord& r) {               // [+0x44] & 0x1000
    const int o = ownerOf(r);
    return o < kMaxPlayers && g_playerData[o].isEden != 0;
}
// Byte "Improved" do bloco do jogador (desc + idx*0x44 + 0x25 = bloco+0x1D).
static bool improved(const UnitRecord& r) {
    const uint8_t* s = unitStatBlock(r.type, statsOf(r));
    return s && s[0x1D] != 0;
}
static int cargoKind(const UnitRecord& u) { return u.cargo & 0x0F; }
static int cargoAmount(const UnitRecord& u) { return (u.cargo >> 4) & 0xFFF; }

// ---- MINES.TXT (sub_44AF00 → unk_564560) --------------------------------------
struct MineRow { int initial = 0, peakTruck = 1, peak = 0, minTruck = 2, min = 0; };
static MineRow s_mines[3][3];          // [variante][rendimento]
static bool s_minesLoaded = false;

bool loadMinesSheet() {
    if (s_minesLoaded) return true;
    auto st = g_ResManager.openStream("mines.txt");
    if (!st) return false;
    const std::string text(reinterpret_cast<const char*>(st->data()), st->size());
    std::vector<std::vector<int>> rows;
    size_t p = text.find('\n');                          // pula o cabeçalho
    while (p != std::string::npos && rows.size() < 9) {
        ++p;
        const size_t q = text.find('\n', p);
        const std::string line = text.substr(p, q == std::string::npos ? std::string::npos : q - p);
        std::vector<int> v;
        size_t a = 0;
        while (a <= line.size()) {
            const size_t b = line.find('\t', a);
            v.push_back(std::atoi(line.substr(a, b == std::string::npos ? std::string::npos : b - a).c_str()));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (v.size() >= 7) rows.push_back(v);
        p = q;
    }
    if (rows.size() < 9) return false;
    // Linhas: rendimento 0..2 (High/Med/Low), cada um com as variantes 1..3.
    for (int y = 0; y < 3; ++y)
        for (int var = 0; var < 3; ++var) {
            const std::vector<int>& v = rows[y * 3 + var];
            MineRow& m = s_mines[var][y];
            m.initial = v[2] * 1000 / 100;
            m.peakTruck = v[3];
            m.peak = v[4] * 1000 / 100;
            m.minTruck = v[5];
            m.min = v[6] * 1000 / 100;
        }
    s_minesLoaded = true;
    return true;
}

static int mineCurve(int yield, int variant, int loads) {      // sub_44B0B0
    if (yield < 0 || yield > 2 || variant < 0 || variant > 2) return 0;
    const MineRow& m = s_mines[variant][yield];
    if (loads > m.minTruck) return m.min;
    if (loads < m.peakTruck) return m.peakTruck ? (m.peak - m.initial) * loads / m.peakTruck + m.initial : m.initial;
    const int span = m.minTruck - m.peakTruck;
    return span ? (m.min - m.peak) * (loads - m.peakTruck) / span + m.peak : m.peak;
}

int mineLoadAmount(int mineId) {                                // vt[0x104]
    const UnitRecord& m = g_units[mineId];
    if (m.type == 0x2A) {                                       // sub_44B140
        const StructSheetDesc* d = structSheet(0x2A);
        return d ? d->player[statsOf(m)].productionCapacity : 0;
    }
    int amount = mineCurve(m.mineYield, m.mineVariant, m.mineLink);   // sub_44AE00
    if ((m.type == 0x15 || m.type == 0x16) && improved(m)) amount = amount * 120 / 100;
    if (m.type == 0x16) amount >>= 1;
    return amount;
}

int mineTakeLoad(int mineId) {                                  // vt[0x100]
    const int amount = mineLoadAmount(mineId);
    if (g_units[mineId].type != 0x2A) ++g_units[mineId].mineLink;   // [+0x68]++
    return amount;
}

// ---- Smelter -------------------------------------------------------------------
static int& stockOf(int p, bool rare) { return rare ? g_playerData[p].rareOre : g_playerData[p].ore; }
static int capOf(int p, bool rare) { return rare ? g_colony[p].rareCapacity : g_colony[p].oreCapacity; }

bool smelterCanAccept(int smelterId, int amount) {             // sub_47DBC0 / sub_47DCB0
    const UnitRecord& s = g_units[smelterId];
    if (improved(s)) amount = amount * 120 / 100;
    const int p = ownerOf(s);
    const bool rare = s.type == 0x38;
    return stockOf(p, rare) + amount <= capOf(p, rare);
}

bool smelterReceive(int smelterId, int amount) {               // sub_47DB40 / sub_47DC30
    const UnitRecord& s = g_units[smelterId];
    if (improved(s)) amount = amount * 120 / 100;
    const int p = ownerOf(s);
    const bool rare = s.type == 0x38;
    int& stock = stockOf(p, rare);
    const int cap = capOf(p, rare);
    if (stock + amount > cap) { stock = cap; return false; }
    stock += amount;
    return true;
}

// ---- Prédio sob o caminhão (sub_437EB0 com classe 4 e o dono) ----------------
int buildingAtTile(int x, int y, int owner) {
    const int mask = g_gameMap.width() - 1;
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        const UnitRecord& b = g_units[i];
        if (b.id == -1 || !(b.flags & UNIT_BUILDING) || (b.flags & UNIT_DEAD)) continue;
        if (owner >= 0 && ownerOf(b) != (owner & 0x0F)) continue;   // (−1 = qualquer dono)
        int w, h; buildingFootprint(b.type, w, h);
        const int l = ((b.pixelX >> 5) - (w >> 1)) & mask, t = (b.pixelY >> 5) - (h >> 1);
        if (y < t || y >= t + h) continue;
        if (((x - l) & mask) < w) return i;
    }
    return 0;
}
static int buildingUnder(const UnitRecord& u) {
    return buildingAtTile((unitOX(u) >> 5) & (g_gameMap.width() - 1), unitOY(u) >> 5, ownerOf(u));
}

// ---- Caminhão: doca ------------------------------------------------------------
// vt[0x80] em sub-estado 0x0D: tabelas por tipo de carga (1ª fase unk_4DEAD0 /
// unk_4DEB00, 2ª fase unk_4DEAE8 / unk_4DEB18; Eden / Plymouth). Carregando na
// mina ([+0x6D]): 0x230/0x22F (Eden) e 0x241/0x24C (Plymouth) p/ comum/raro.
static const int16_t kDockE1[11] = {597, 599, 598, 589, 592, 590, 594, 593, 595, 591, 596};
static const int16_t kDockE2[11] = {587, 578, 584, 586, 580, 579, 0, 0, 581, 0, 582};
static const int16_t kDockP1[11] = {609, 600, 601, 602, 604, 603, 606, 605, 607, 610, 608};
static const int16_t kDockP2[11] = {611, 616, 583, 585, 613, 612, 0, 0, 614, 0, 615};
// Com carga (tipo 1..10), 16 direções: Eden / Plymouth; ação 0x0E (despejar) usa
// as tabelas "d". Tipo 0 = tabela normal do veículo (sub_44C4F0).
static const int16_t kLoadE[11][16] = {
    {0},
    {617, 632, 631, 630, 629, 628, 627, 626, 625, 624, 623, 622, 621, 620, 619, 618},
    {649, 664, 663, 662, 661, 660, 659, 658, 657, 656, 655, 654, 653, 652, 651, 650},
    {679, 694, 693, 692, 691, 690, 689, 688, 687, 686, 685, 684, 683, 682, 681, 680},
    {885, 900, 899, 898, 897, 896, 895, 894, 893, 892, 891, 890, 889, 888, 887, 886},
    {869, 884, 883, 882, 881, 880, 879, 878, 877, 876, 875, 874, 873, 872, 871, 870},
    {925, 940, 939, 938, 937, 936, 935, 934, 933, 932, 931, 930, 929, 928, 927, 926},
    {909, 924, 923, 922, 921, 920, 919, 918, 917, 916, 915, 914, 913, 912, 911, 910},
    {973, 988, 987, 986, 985, 984, 983, 982, 981, 980, 979, 978, 977, 976, 975, 974},
    {633, 648, 647, 646, 645, 644, 643, 642, 641, 640, 639, 638, 637, 636, 635, 634},
    {1005, 1020, 1019, 1018, 1017, 1016, 1015, 1014, 1013, 1012, 1011, 1010, 1009, 1008, 1007, 1006},
};
static const int16_t kLoadP[11][16] = {
    {0},
    {773, 788, 787, 786, 785, 784, 783, 782, 781, 780, 779, 778, 777, 776, 775, 774},
    {805, 820, 819, 818, 817, 816, 815, 814, 813, 812, 811, 810, 809, 808, 807, 806},
    {821, 836, 835, 834, 833, 832, 831, 830, 829, 828, 827, 826, 825, 824, 823, 822},
    {853, 868, 867, 866, 865, 864, 863, 862, 861, 860, 859, 858, 857, 856, 855, 854},
    {837, 852, 851, 850, 849, 848, 847, 846, 845, 844, 843, 842, 841, 840, 839, 838},
    {957, 972, 971, 970, 969, 968, 967, 966, 965, 964, 963, 962, 961, 960, 959, 958},
    {941, 956, 955, 954, 953, 952, 951, 950, 949, 948, 947, 946, 945, 944, 943, 942},
    {989, 1004, 1003, 1002, 1001, 1000, 999, 998, 997, 996, 995, 994, 993, 992, 991, 990},
    {789, 804, 803, 802, 801, 800, 799, 798, 797, 796, 795, 794, 793, 792, 791, 790},
    {1021, 1036, 1035, 1034, 1033, 1032, 1031, 1030, 1029, 1028, 1027, 1026, 1025, 1024, 1023, 1022},
};
static const int16_t kDumpE[11][16] = {
    {0},
    {1974, 1974, 1981, 1981, 1980, 1980, 1979, 1979, 1978, 1978, 1977, 1977, 1976, 1976, 1975, 1975},
    {2006, 2006, 2013, 2013, 2012, 2012, 2011, 2011, 2010, 2010, 2009, 2009, 2008, 2008, 2007, 2007},
    {2022, 2022, 2029, 2029, 2028, 2028, 2027, 2027, 2026, 2026, 2025, 2025, 2024, 2024, 2023, 2023},
    {2054, 2054, 2061, 2061, 2060, 2060, 2059, 2059, 2058, 2058, 2057, 2057, 2056, 2056, 2055, 2055},
    {2038, 2038, 2045, 2045, 2044, 2044, 2043, 2043, 2042, 2042, 2041, 2041, 2040, 2040, 2039, 2039},
    {2086, 2086, 2093, 2093, 2092, 2092, 2091, 2091, 2090, 2090, 2089, 2089, 2088, 2088, 2087, 2087},
    {2070, 2070, 2077, 2077, 2076, 2076, 2075, 2075, 2074, 2074, 2073, 2073, 2072, 2072, 2071, 2071},
    {2102, 2102, 2109, 2109, 2108, 2108, 2107, 2107, 2106, 2106, 2105, 2105, 2104, 2104, 2103, 2103},
    {1990, 1990, 1997, 1997, 1996, 1996, 1995, 1995, 1994, 1994, 1993, 1993, 1992, 1992, 1991, 1991},
    {2118, 2118, 2125, 2125, 2124, 2124, 2123, 2123, 2122, 2122, 2121, 2121, 2120, 2120, 2119, 2119},
};
static const int16_t kDumpP[11][16] = {
    {0},
    {1966, 1966, 1973, 1973, 1972, 1972, 1971, 1971, 1970, 1970, 1969, 1969, 1968, 1968, 1967, 1967},
    {1998, 1998, 2005, 2005, 2004, 2004, 2003, 2003, 2002, 2002, 2001, 2001, 2000, 2000, 1999, 1999},
    {2014, 2014, 2021, 2021, 2020, 2020, 2019, 2019, 2018, 2018, 2017, 2017, 2016, 2016, 2015, 2015},
    {2046, 2046, 2053, 2053, 2052, 2052, 2051, 2051, 2050, 2050, 2049, 2049, 2048, 2048, 2047, 2047},
    {2030, 2030, 2037, 2037, 2036, 2036, 2035, 2035, 2034, 2034, 2033, 2033, 2032, 2032, 2031, 2031},
    {2078, 2078, 2085, 2085, 2084, 2084, 2083, 2083, 2082, 2082, 2081, 2081, 2080, 2080, 2079, 2079},
    {2062, 2062, 2069, 2069, 2068, 2068, 2067, 2067, 2066, 2066, 2065, 2065, 2064, 2064, 2063, 2063},
    {2094, 2094, 2101, 2101, 2100, 2100, 2099, 2099, 2098, 2098, 2097, 2097, 2096, 2096, 2095, 2095},
    {1982, 1982, 1989, 1989, 1988, 1988, 1987, 1987, 1986, 1986, 1985, 1985, 1984, 1984, 1983, 1983},
    {2110, 2110, 2117, 2117, 2116, 2116, 2115, 2115, 2114, 2114, 2113, 2113, 2112, 2112, 2111, 2111},
};

int truckAnimRaw(const UnitRecord& u, int dir) {             // sub_4062B0
    const bool eden = isEden(u);
    const int kind = cargoKind(u);
    if (u.field22 == 0x0D) {
        if (u.dockLoad) {                                     // carregando na mina
            const bool common = static_cast<unsigned>(kind - 2) < 1u;
            return eden ? (common ? 0x230 : 0x22F) : (common ? 0x241 : 0x24C);
        }
        if (kind > 10) return 0;
        const bool second = (u.flags & 0x1000000u) != 0;
        return eden ? (second ? kDockE2[kind] : kDockE1[kind]) : (second ? kDockP2[kind] : kDockP1[kind]);
    }
    if (kind == 0 || kind > 10) {                             // sub_44C4F0
        const int a = vehicleAnimIndex(u.type, (dir & 15) << 4, eden);
        return a >= 0 ? a + 0x65 : 0;
    }
    const bool dump = u.action == 0x0E;
    return eden ? (dump ? kDumpE[kind][dir & 15] : kLoadE[kind][dir & 15])
                : (dump ? kDumpP[kind][dir & 15] : kLoadP[kind][dir & 15]);
}

int truckFrame(const UnitRecord& u, int id) {                // sub_44C7B0
    const int dir = ((u.rotation + 8) & 0xF0) >> 4;
    const int raw = truckAnimRaw(u, dir);
    if (!raw) return 0;
    const int last = artLastFrameRaw(raw);
    if (u.field22 == 0x0D || u.action == 0x0E) {
        const int f = (TethysGame::tick() - u.dockTick) & 0xFFFF;
        if (last > f) return f;
        if (u.action == 0x0E && u.field22 == 1) return 0;
        return last - 1;
    }
    if (!u.action) return 0;
    const int t = (TethysGame::tick() >> ((u.flags & 0x10000u) ? 1 : 0)) + id;
    return t % (last + 1);
}

int truckDockPhaseTicks(const UnitRecord& u) {               // sub_404C70(vt[0x80](0))
    if (TethysGame::cheatFlag(4)) return 4;                    // dword_56DE68
    const int raw = truckAnimRaw(u, 0);
    const int n = raw ? artLastFrameRaw(raw) : 1;
    return n > 0 ? n : 1;
}

// vt[0x94] (sub_406530): pedido [+0x5C] = (quantidade << 4) | tipo e sub-estado 0x0D.
static void truckDockBegin(UnitRecord& u, int kind, int amount, int loadFlag) {
    u.cargoRequest = static_cast<uint16_t>((amount << 4) | (kind & 0x0F));
    u.flags &= ~0x1000000u;
    u.dockTick = static_cast<int16_t>(TethysGame::tick());
    u.dockLoad = static_cast<uint8_t>(loadFlag);
    u.field22 = 0x0D;
    u.field3C = (kind == 2 || kind == 3) ? 1 : truckDockPhaseTicks(u);
}

void truckSetCargo(int truckId, int kind, int amount, int loadFlag) {   // sub_406530
    UnitRecord& u = g_units[truckId];
    if (kind == 6 || kind == 7 || kind == 9) {
        u.cargoRequest = static_cast<uint16_t>((amount << 4) | (kind & 0x0F));
        if (kind == 9 && amount > 0x1F40) amount -= 0x1F40;
        u.cargo = static_cast<uint16_t>((amount << 4) | (kind & 0x0F));
        return;
    }
    truckDockBegin(u, kind, amount, loadFlag);
}

void truckDockStart(int truckId) {                           // sub_406070
    UnitRecord& u = g_units[truckId];
    const int b = buildingUnder(u);
    if (!b) return;
    const UnitRecord& bl = g_units[b];
    const int kind = cargoKind(u);
    switch (bl.type) {
        case 0x15:                                            // mina comum
            if (kind == 0 && structureOperational(bl)) truckDockBegin(u, 2, 0, 1);
            break;
        case 0x16: case 0x2A:                                 // mina rara / Magma Well
            if (kind == 0 && structureOperational(bl)) truckDockBegin(u, 3, 0, 1);
            break;
        case 0x36:                                            // smelter comum
            if (kind == 2 && structureOperational(bl)) truckDockBegin(u, 0, 0, 0);
            break;
        case 0x38:                                            // smelter raro
            if (kind == 3 && structureOperational(bl)) truckDockBegin(u, 0, 0, 0);
            break;
        case 0x39:                                            // GORF: sub_44DA80 decide
            if (vehicleCanDock(truckId, b, true)) truckDockBegin(u, 0, 0, 0);
            break;
        default:
            break;
    }
}

void truckDockExchange(int truckId) {                        // sub_406610
    UnitRecord& u = g_units[truckId];
    u.dockLoad = 0;
    const int b = buildingUnder(u);
    if (!b) return;
    UnitRecord& bl = g_units[b];
    const int kind = cargoKind(u);
    const int p = ownerOf(u);
    PlayerData& pd = g_playerData[p];
    auto loadMetal = [&](bool rare) {                         // tipo 0: tira do estoque
        // CARGA PARCIAL: o original zerava a carga quando o estoque tinha
        // caido abaixo do pedido entre a ordem e a atracacao. Agora leva o que
        // houver.
        int& stock = rare ? pd.rareOre : pd.ore;
        u.cargo = u.cargoRequest;
        int want = cargoAmount(u);
        if (want > stock) {
            want = stock;
            u.cargo = static_cast<uint16_t>((want << 4) | (u.cargoRequest & 0x0F));
        }
        stock -= want;
    };
    auto unloadMetal = [&](bool rare) {                       // tipo 4/5: põe no estoque
        int& stock = rare ? pd.rareOre : pd.ore;
        int room = capOf(p, rare) - stock;
        const int amt = cargoAmount(u);
        if (room >= amt) room = amt;
        u.cargo = 0;
        stock += room;
    };
    switch (bl.type) {
        case 0x15:                                            // mina comum
            if (kind) return;
            u.dockLoad = 1;
            u.cargo = static_cast<uint16_t>((mineTakeLoad(b) << 4) | 2);
            break;
        case 0x16: case 0x2A:                                 // mina rara / Magma Well
            if (kind) return;
            u.dockLoad = 1;
            u.cargo = static_cast<uint16_t>((mineTakeLoad(b) << 4) | 3);
            break;
        case 0x19:                                            // Common Storage
            if (kind == 0) loadMetal(false);
            else if (kind == 4) unloadMetal(false);
            break;
        case 0x1A:                                            // Rare Storage
            if (kind == 0) loadMetal(true);
            else if (kind == 5) unloadMetal(true);
            break;
        case 0x27:                                            // Agridome: comida
            if (kind == 1) {
                pd.foodStored += cargoAmount(u);
                u.cargo = 0;
            } else {                                          // (qualquer outro tipo)
                const VehPlayerStats* vs = vehicleSheet(u.type, statsOf(u));
                int cap = vs ? vs->cargo : 0;
                if (cap > pd.foodStored) cap = pd.foodStored;   // CARGA PARCIAL
                if (cap <= 0) { u.cargo = 0; break; }
                pd.foodStored -= cap;
                u.cargo = static_cast<uint16_t>((cap << 4) | 1);
            }
            break;
        case 0x36:                                            // smelter comum
            if (kind == 2) {
                if (!structureOperational(bl)) return;
                smelterReceive(b, cargoAmount(u));
                u.cargo = 0;
            } else if (kind == 0) loadMetal(false);
            else if (kind == 4) unloadMetal(false);
            break;
        case 0x37:                                            // Spaceport
            u.cargo = u.cargoRequest;
            break;
        case 0x38:                                            // smelter raro
            if (kind == 3) {
                if (!structureOperational(bl)) return;
                smelterReceive(b, cargoAmount(u));
                u.cargo = 0;
            } else if (kind == 0) loadMetal(true);
            else if (kind == 5) unloadMetal(true);
            break;
        case 0x39:                                            // GORF: entulho (sub_46E4E0 / sub_46E550)
            if (kind == 6 || kind == 7) gorfReceiveRubble(b, kind, cargoAmount(u));
            u.cargo = 0;
            break;
        default:
            break;
    }
    colonyMarkDirty(p);                                       // dword_56E37C = 1
}

// sub_405F10: 0 = sem prédio sob o caminhão; 1 = pode atracar; 2 = esperar.
static int truckDockStatus(const UnitRecord& u) {
    const int b = buildingUnder(u);
    if (!b) return 0;
    const UnitRecord& bl = g_units[b];
    switch (bl.type) {
        case 0x15: case 0x16:
            return structureOperational(bl) ? 1 : 2;
        case 0x36: case 0x38:
            return structureOperational(bl) && smelterCanAccept(b, 1) ? 1 : 2;
        default:
            return 1;
    }
}

// ---- Docas: quem atraca e STORAGE de recursos --------------------------------
bool vehicleCanDock(int vehicleId, int buildingId, bool ownerCheck) {   // sub_44DA80
    if (!valid(vehicleId) || !valid(buildingId)) return false;
    const UnitRecord& v = g_units[vehicleId];
    const UnitRecord& b = g_units[buildingId];
    if (ownerCheck && ownerOf(v) != ownerOf(b)) return false;
    if (v.type == mapSpider || v.type == mapScorpion) return false;
    const StructSheetDesc* d = structSheet(b.type);
    const uint32_t f = d ? d->flags24C : 0;
    if (f & 0x04) return true;                                 // Docking All
    if (v.type == mapConVec) return (f & 0x10) != 0;           // Docking CV
    if (v.type != mapCargoTruck || !(f & 0x08)) return false;  // Docking Truck
    const int kind = cargoKind(v), p = ownerOf(v);
    const PlayerData& pd = g_playerData[p];
    switch (b.type) {
        case 0x15: case 0x16: case 0x2A: return kind == 0;
        case 0x19: return kind == 0 || (kind == 4 && pd.ore < capOf(p, false));
        case 0x1A: return kind == 0 || (kind == 5 && capOf(p, true) > pd.rareOre);
        case 0x27: return kind == 0 || kind == 1;
        case 0x36: return kind == 0 || ((kind == 2 || kind == 4) && smelterCanAccept(buildingId, 1));
        case 0x37: return kind == 0 || kind == 8 || kind == 9;
        case 0x38: return kind == 0 || ((kind == 3 || kind == 5) && smelterCanAccept(buildingId, 1));
        case 0x39:
            if (kind == 6) return pd.ore < capOf(p, false);
            if (kind == 7) return capOf(p, true) > pd.rareOre;
            return false;
        default: return false;
    }
}

const char* truckCargoName(const UnitRecord& u) {             // sub_406210
    static const char* const kNames[8] = {"(empty)", "Food", "Common Ore", "Rare Ore", "Common Metals",
                                           "Rare Metals", "Common Rubble", "Rare Rubble"};
    const int k = cargoKind(u);
    if (k < 8) return kNames[k];
    if (k == 8) return unitDisplayName(cargoAmount(u));       // módulo/carga espacial
    return "";
}

int storageStock(int buildingId) {                             // [+0x6A4]
    const UnitRecord& b = g_units[buildingId];
    const PlayerData& pd = g_playerData[ownerOf(b)];
    switch (b.type) {
        case 0x19: case 0x36: return pd.ore;
        case 0x1A: case 0x38: return pd.rareOre;
        case 0x27: return pd.foodStored;
        default: return 0;
    }
}

static bool vehCommandable(const UnitRecord& r) {
    return r.id != -1 && !((r.flags | (r.flags << 2)) & 0x80000u) && r.field22 != 0x0A;
}
static int truckCap(const UnitRecord& v) {                     // bloco+0x38 (Cargo Capacity)
    const VehPlayerStats* vs = vehicleSheet(v.type, statsOf(v));
    return vs ? vs->cargo : 0;
}

bool storageLoadAvailable(int b) {                             // sub_456EC0
    const int vid = dockedVehicle(b);
    if (!vid) return false;
    const UnitRecord& v = g_units[vid];
    if (!vehCommandable(v) || (v.flags & 0x40000u) || v.field22 == 0x0D || v.cargo != 0) return false;
    if (!vehicleCanDock(vid, b, true)) return false;
    const UnitRecord& bl = g_units[b];
    const PlayerData& pd = g_playerData[TethysGame::localPlayer()];
    // CARGA PARCIAL: o original so acende o L&OAD com estoque para ENCHER o
    // caminhao (`truckCap(v) <= estoque`), entao os ultimos 900 de minerio
    // ficavam presos no deposito sem jeito de tirar. Agora basta ter algo.
    switch (bl.type) {
        case 0x27: return pd.foodStored > 0;
        case 0x36: return pd.ore > 0;
        case 0x38: return pd.rareOre > 0;
        default: return structureOperational(bl);
    }
}

bool storageUnloadAvailable(int b) {                           // sub_4570D0
    const int vid = dockedVehicle(b);
    if (!vid) return false;
    const UnitRecord& v = g_units[vid];
    if (v.field22 == 0x0D || v.cargo == 0) return false;
    if (!vehicleCanDock(vid, b, true)) return false;
    if (!vehCommandable(v) || (v.flags & 0x40000u)) return false;
    const UnitRecord& bl = g_units[b];
    switch (bl.type) {
        case 0x27: return true;
        case 0x36: return cargoKind(v) == 2 ? structureOperational(bl) : true;
        case 0x38: return cargoKind(v) == 3 ? structureOperational(bl) : true;
        default: return structureOperational(bl);
    }
}

// Pacotes 0x0B / 0x0C (sub_40E210 casos 11/12): prédio vivo, veículo atracado
// comandável e sem 0x40000. (Anel anti-duplicata [jogador+0xBD8]: rede.)
static int dockedForPacket(int b) {
    if (!valid(b) || (g_units[b].flags & UNIT_DEAD)) return 0;
    const int vid = dockedVehicle(b);
    if (!vid) return 0;
    const UnitRecord& v = g_units[vid];
    if (!vehCommandable(v) || (v.flags & 0x40000u)) return 0;
    return vid;
}

// Os dois botoes do painel STORAGE mandam o pacote; quem carrega e descarrega
// de verdade e o applyLoadCargo/applyUnloadCargo, ja com o turno.
void storageLoad(int b) {                                      // sub_457070: pacote 0x0B
    CommandPacket pkt;
    pkt.type = CMD_LOAD_CARGO;
    pkt.unitIds.push_back(b);
    commandSubmit(TethysGame::localPlayer(), pkt);
}
void storageUnload(int b) {                                    // sub_4571E0: pacote 0x0C
    CommandPacket pkt;
    pkt.type = CMD_UNLOAD_CARGO;
    pkt.unitIds.push_back(b);
    commandSubmit(TethysGame::localPlayer(), pkt);
}

void applyLoadCargo(int owner, int b) {                        // sub_423F70
    if (!valid(b) || ownerOf(g_units[b]) != (owner & 0x0F)) return;
    const int vid = dockedForPacket(b);
    if (!vid || g_units[vid].type != mapCargoTruck) return;
    UnitRecord& v = g_units[vid];
    const PlayerData& pd = g_playerData[ownerOf(g_units[b])];
    const int cap = truckCap(v);
    // CARGA PARCIAL: pede o que couber -- `min(capacidade, estoque)` -- em vez
    // de so aceitar carga cheia.
    auto pede = [&](int kind, int estoque) {
        const int amt = cap < estoque ? cap : estoque;
        if (amt > 0) truckDockBegin(v, kind, amt, 0);
    };
    switch (g_units[b].type) {
        case 0x19: case 0x36: pede(4, pd.ore); break;
        case 0x1A: case 0x38: pede(5, pd.rareOre); break;
        case 0x27: pede(1, pd.foodStored); break;
        default: break;
    }
}

void applyUnloadCargo(int owner, int b) {                       // sub_424120 → vt[0x94](0,0,0)
    if (!valid(b) || ownerOf(g_units[b]) != (owner & 0x0F)) return;
    const int vid = dockedForPacket(b);
    if (!vid || g_units[vid].type != mapCargoTruck) return;
    truckDockBegin(g_units[vid], 0, 0, 0);
}

// ---- Despejar carga -----------------------------------------------------------
void sendDumpCargo(int id) {                                   // sub_4557B0: pacote 0x0E
    const int me = TethysGame::localPlayer();
    if (!valid(id) || ownerOf(g_units[id]) != me) return;
    CommandPacket pkt;
    pkt.type = CMD_DUMP_CARGO;
    pkt.unitIds.push_back(id);
    commandSubmit(me, pkt);
}
void dumpCargoOrder(int id) {
    if (!valid(id)) return;
    UnitRecord& u = g_units[id];
    if (!vehCommandable(u)) return;
    u.action = 0x0E; u.field20 = 1; u.field3C = 0;
}

void dumpCargoStart(int id) {                                  // jpt_43AD56 case 14
    UnitRecord& u = g_units[id];
    u.field3C = artLastFrameRaw(0x7BC);
    if (u.field3C <= 0) u.field3C = 1;
    u.dockTick = static_cast<int16_t>(TethysGame::tick());     // [+0x5E]
    soundPlayAt(unitOX(u), unitOY(u), 0x45);
    u.action = 0x0E; u.field22 = SUB_WAIT; u.field20 = 0;
}

void dumpCargoTick(int id) {                                   // jpt_43B312 case 14
    UnitRecord& u = g_units[id];
    u.cargo = 0;
    u.action = 0;
}

// ---- Rota ---------------------------------------------------------------------
int oreRouteTarget(int stage, int mineId, int truckOwner, int tileX, int tileY) {   // sub_454290
    const int b = buildingAtTile(tileX, tileY, truckOwner);
    if (!b) return 0;
    int dx, dy;
    if (!dockCell(b, dx, dy) || dx != tileX || dy != tileY) return 0;   // tem de ser a doca
    const int t = g_units[b].type;
    if (stage == 0) return (t == 0x15 || t == 0x16 || t == 0x2A) ? b : 0;
    if (stage == 1) {
        if (!valid(mineId)) return 0;
        const int mt = g_units[mineId].type;
        if (t != 0x36 && t != 0x38) return 0;
        if (mt == 0x15 && t != 0x36) return 0;
        if ((mt == 0x16 || mt == 0x2A) && t != 0x38) return 0;
        return b;
    }
    return b == mineId ? b : 0;
}

// ---- Rota automatica (acrescimo deste porte) ---------------------------------
// A fundicao compativel mais proxima: comum (0x36) para a mina comum (0x15),
// de minerio raro (0x38) para a mina rara (0x16) e para o Magma Well (0x2A) --
// o mesmo casamento que o oreRouteTarget exige no segundo clique. A distancia
// e medida entre as DOCAS, que e por onde o caminhao entra, e o eixo X da a
// volta no mapa como tudo o mais.
//
// Entre as compativeis, ganha a mais proxima DISPONIVEL NO MOMENTO -- operando
// e com espaco no estoque. E exatamente a condicao do truckDockStatus para o
// caminhao NAO ficar esperando na doca, entao o atalho escolhe onde ele vai
// conseguir descarregar. So quando nenhuma esta disponivel e que vale a mais
// proxima de todas: a rota ainda e montada, e o caminhao espera nela, como no
// comando de tres cliques. (Minerio BRUTO so descarrega em fundicao: o
// Storage Tanks guarda metal ja fundido -- ver vehicleCanDock.)
int nearestSmelterFor(int mineId) {
    if (!valid(mineId)) return 0;
    const UnitRecord& m = g_units[mineId];
    const int mt = m.type;
    if (mt != 0x15 && mt != 0x16 && mt != 0x2A) return 0;
    const int querido = mt == 0x15 ? 0x36 : 0x38;
    int mx, my;
    if (!dockCell(mineId, mx, my)) return 0;
    const int dono = ownerOf(m);
    const int largura = g_gameMap.ok() ? g_gameMap.width() : 0;
    int melhor = 0, melhorDisp = 0;
    long long melhorD = 0, melhorDispD = 0;
    for (int i = 1; i < static_cast<int>(g_units.size()); ++i) {
        const UnitRecord& b = g_units[i];
        if (b.id == -1 || (b.flags & UNIT_DEAD) || !(b.flags & UNIT_BUILDING)) continue;
        if (b.type != querido || ownerOf(b) != dono) continue;
        int sx, sy;
        if (!dockCell(i, sx, sy)) continue;
        int dx = sx - mx;
        if (largura) {                                  // o menor dos dois lados
            dx = ((dx % largura) + largura) % largura;
            if (dx > largura / 2) dx -= largura;
        }
        const long long dy = sy - my;
        const long long d = static_cast<long long>(dx) * dx + dy * dy;
        if (!melhor || d < melhorD) { melhor = i; melhorD = d; }
        // A condicao do truckDockStatus para descarregar sem esperar.
        const bool disponivel = structureOperational(b) && smelterCanAccept(i, 1);
        if (disponivel && (!melhorDisp || d < melhorDispD)) { melhorDisp = i; melhorDispD = d; }
    }
    return melhorDisp ? melhorDisp : melhor;
}

bool autoOreRoute(const std::vector<int>& sel, int mineId) {
    if (!valid(mineId)) return false;
    const int me = TethysGame::localPlayer();
    if (ownerOf(g_units[mineId]) != me) return false;
    // So caminhoes de carga VAZIOS, como o proprio comando de rota exige
    // (sub_453E80): com carga a bordo o primeiro trecho nao faria sentido.
    std::vector<int> caminhoes;
    for (int id : sel)
        if (valid(id) && g_units[id].type == mapCargoTruck && g_units[id].cargo == 0 &&
            ownerOf(g_units[id]) == me)
            caminhoes.push_back(id);
    if (caminhoes.empty()) return false;
    const int smelter = nearestSmelterFor(mineId);
    if (!smelter) return false;
    int mx, my, sx, sy;
    if (!dockCell(mineId, mx, my) || !dockCell(smelter, sx, sy)) return false;
    // Os mesmos tres pontos dos tres cliques: mina, fundicao, mina.
    const std::vector<uint32_t> pts = {packLocation(mx, my), packLocation(sx, sy),
                                       packLocation(mx, my)};
    sendOreRoute(caminhoes, pts, 0, 1, mineId, smelter);
    return true;
}

void sendOreRoute(const std::vector<int>& sel, const std::vector<uint32_t>& pts,
                  int mineIdx, int smelterIdx, int mineId, int smelterId) {
    CommandPacket pkt;
    pkt.type = 0x27;
    pkt.waypoints = pts;
    pkt.routeMineIdx = mineIdx;
    pkt.routeSmelterIdx = smelterIdx;
    pkt.routeMine = mineId;
    pkt.routeSmelter = smelterId;
    const int me = TethysGame::localPlayer();
    for (int id : sel)
        if (valid(id) && ownerOf(g_units[id]) == me && (g_units[id].flags & UNIT_VEHICLE)) pkt.unitIds.push_back(id);
    if (!pkt.unitIds.empty()) commandSubmit(me, pkt);
}

// Volta a andar (loc_43D6E5: sub_44C2F0, [+0x23] = [+0x22], [+0x22] = 1).
static void resume(UnitRecord& u) {
    u.clearRoute();
    u.field23 = u.field22;
    u.field22 = 1;
    u.field20 = 1;
}

void oreRouteTick(int id) {                                   // jpt_43B312 case 39
    UnitRecord& u = g_units[id];
    if (!u.hasOrder || u.order.waypoints.empty()) { u.action = 0; return; }
    OrderBlock& ob = u.order;
    // sub_44BEA0: chegou ao fim do trecho ([ordem+4] <= [ordem+8])?
    if (!(ob.countMinus1 <= ob.curWaypoint) && u.field23 == 1) {
        u.field22 = SUB_WAIT; u.field3C = 0x28;               // bloqueado: espera 40
        return;
    }
    int esi = -1, ebx;
    if (u.field23 == 0x0D) {                                  // voltou da doca
        if (u.flags & 0x4000u) {                              // no smelter: descarregou?
            if (u.cargo == 0) { resume(u); return; }
        } else {                                              // na mina: carregou?
            if (u.cargo != 0) { resume(u); return; }
        }
        u.action = 0;
        return;
    }
    const int cur = ob.curWaypoint;
    if (cur == u.routeMineIdx) {                              // na mina → trecho até o smelter
        ebx = u.routeSmelterIdx;
        u.flags &= ~0x4000u;
    } else if (cur == u.routeSmelterIdx) {                    // no smelter → volta
        ebx = ob.count - 1;
        u.flags |= 0x4000u;
    } else if (ob.count - 1 > cur) {                          // ponto intermediário
        resume(u);
        return;
    } else {                                                  // último ponto (a mina de novo)
        u.flags &= ~0x4000u;
        ebx = u.routeSmelterIdx;
        esi = u.routeMineIdx + 1;
    }
    switch (truckDockStatus(u)) {
        case 0:                                               // sub_44DFB0: abandona a rota
            u.field23 = u.field22;
            u.action = 0; u.field22 = 0;
            u.hasOrder = false; u.order = OrderBlock{};
            return;
        case 1:
            if (!unitTurnTo(u, 0x80)) return;                 // sub_44C410
            ob.countMinus1 = ebx;                             // [ordem+4]
            if (esi > 0) ob.curWaypoint = esi;                // [ordem+8]
            truckDockStart(id);                               // vt[0x88]
            return;
        default:                                              // espera 50
            u.field22 = SUB_WAIT; u.field3C = 0x32;
            return;
    }
}

} // namespace op2
