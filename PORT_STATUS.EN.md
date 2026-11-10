**English** · [Português (BR)](PORT_STATUS.BR.md)

> **This translation lags the Portuguese original.** It was written from
> `PORT_STATUS.BR.md` as of commit `c80e74d`. Everything up to there is here;
> the work merged afterwards is documented **only in the BR file** so far:
> the chat-line cheats (5.6), item 21, and sections **5.7** (the Options tab
> fixes), **5.8** (the `Screen Mode` radio and the PREFERENCES scroll arrows)
> **5.9** (the per-item PREFERENCES table at `0x4E3118`, and the screen's
> real structure, still open) **5.10** (the OPUPatch defect list checked
> against this port) and **5.11** (the Savant alerts, and a message-filter
> defect of this port's own) **5.12**, **5.13** and **5.14** (the rest of the OPUPatch list, now closed)
> and **5.15** (the tick raised too early, and the debug HUD drawing from the
> RNG every tick), **5.16** (cheats are single-player only now) and **5.17** (edge-of-screen and
> arrow-key view scrolling, ported from `sub_407A10`) **5.18** (the unit
> click area: buildings answer on their whole footprint) and **5.19** (the
> game's own command cursors, read from the `.ANI` files) and **5.20** (ESC
> now asks before abandoning a mission, and the attract-demo idle clock
> restarts whenever the menu comes back) and **5.21** (the end-of-mission
> banner: big red Arial with a black outline, as `sub_4084D0`/`sub_4085C0`
> draw it) and **5.22** (the `Ctrl+P` pause, command 40001, with the same
> banner). The recent changes are summarised in
> [README.EN.md](README.EN.md).

# Outpost 2: Divided Destiny — Modern Port (C++17/SDL2)

> Port status. Last revision: **2026-11-05** (199 commits), done by reading the
> code and running the tests, not the previous documentation — which dated
> from 2026-09-29 and said the port "does not yet reimplement the game itself".

---

## 1. What this is today

A **playable** port of *Outpost 2: Divided Destiny* (Sierra/Dynamix, 1997) to
C++17 + SDL2. The game runs: both factions' campaigns, colony games, the 14
tutorials and networked multiplayer matches.

The mechanics **come from the disassembly** (`Outpost2_src/OUTPOST2.asm`, IDA)
and from the original DLLs' resources — nothing is invented. When the original
has an identified defect, the port **fixes it instead of reproducing it**.

The 1997 files are not modified or redistributed: the port **reads** them
(`OP2SHRES.DLL`, `OUT2RES.DLL`, the `.VOL`s, `OP2_ART.BMP`, the mission DLLs).
The original missions **actually run**, loaded by a custom PE loader with ABI
bridges for the API they import from `OUTPOST2.EXE`.

### Today's numbers

| | |
|---|---|
| Port code | **45,200 lines** of C++17 in `src/` |
| Simulation (`src/game`, 40 .cpp) | 21,400 lines |
| In-game UI (`src/app`, 20 .cpp) | 8,100 lines |
| Shell/menus (`src/shell`) | 3,900 lines |
| Networking (`src/net`) | 1,400 lines |
| Mission DLLs that load and run | **94 of 94** |
| EXE API symbols implemented | **222 of 222** (the ones the 94 DLLs import) |
| Command-line modes (tests and screens) | 93 |

The two mission rows are the honest measure of coverage: every mission DLL on
disk loads, creates its units and runs, and none imports a symbol the port
lacks. To redo the count: sweep with `--missiontest` over each DLL, and API
coverage by comparing the DLLs' import tables against the registry in
`src/mission/op2_api.cpp`.

---

## 2. Structure

```
src/
  main.cpp        entry point + the 106 test/capture modes
  app/            TApp, UIState, GameState (the game), ShellState, HUD,
                  the frame's 6 tabs, minimap, globe, dialogs
  game/           the simulation: units, structures, movement, pathfinder,
                  combat, economy, research, disasters, triggers, AI
  gfx/            window-sized framebuffer (640x480 base), PBMP, sprites, fonts (bitmap and GDI)
  res/            PE reader (.rsrc + exports), .VOL + LZHUF, ResManager
  core/           OUTPOST2.INI (CConfig)
  audio/          SDL mixer faithful to the original's DirectSound, music (.clm)
  mission/        PE loader for the mission DLLs + API bridge
  net/            lobby (discovery/joining) and lockstep match
tools/            Python reverse-engineering scripts (formats, dumps)
Outpost2_src/     reference disassembly (NOT versioned)
```

---

## 3. What is ported

### Simulation
- Deterministic **clock and RNG**, bit-exact with the original.
- **Units**: pool, creation, faithful drawing (`op2_art` sprites with palette
  and player color), shadows, serial number.
- **Movement and pathfinder** ported from the real architecture (routes,
  queues, blocking) — never an invented A*.
- **Structures**: construction, power on/off, damage, destruction, rubble,
  dismantling, capture, repair, ambient animations.
- Complete **colony economy**: tube network, power, workers, scientists,
  **food, population and morale** (including mission-forced morale).
- **Mining**: Robo-Miner/GeoCon, `MINES.TXT`, trucks, docks, smelters.
- **Earthworks**: tubes, walls, Robo-Dozer, region sweep.
- **Combat**: all tank and turret weapons, EMP/foam/ESG/acid/Thor/Scorpion,
  self-destruct, area damage with armor, walls.
- **Research**: the four trees, labs, scientists, propagation.
- **Disasters**: meteor, lightning, tornado, earthquake, eruption/lava and the
  **blight (microbe)**, with the original's three warning phases.
- **Triggers**: the 16 `Create*Trigger`s, victory/defeat, objectives.
- **AI**: groups (Sc/Fight/Build/Mine), the computer's default and Pinwheel.
- **Save/load** (`SGAME%d.OP2`), with a determinism test.

### Interface
- **Shell**: main menu, campaign (briefing/objectives/chapters from
  `STORY.VOL`), colony games with scoreboard, tutorials, preferences, about.
- **In-game**: right-hand panel, unit frame, 3x3 command grid, building
  buttons, help line, the 6 report tabs, minimap with zoom, tactical view and
  globe, day/night cycle.
- Windows-standard **controls**: keyboard focus, drop-down combo boxes,
  scrollbars.

### Audio
SDL mixer faithful to the original's (per-tile volume and pan, voice queue),
`.clm` music, and the port's own **master volume**.

### Manual (help) and attract demo
The HELP button and F1 open **OUTPOST2.HLP** — "Outpost 2 Online Manual", 145
topics — in a screen of the port's own, because the WinHelp the original
called no longer exists in Windows. The file is read where it is
(`src/res/winhelp.*`), not converted or copied; without it, HELP looks for a
PDF manual next to the game and hands it to the system viewer.

A minute idle on the main menu triggers the **attract demo**, as in the
original: `ademo{1,2,3}.dll` in rotation via the INI's `[Game] Demo` counter
(which is a counter, not an on/off switch), with disasters and day/night on.
Any key or click interrupts it. Two additions in this port make the demo
watchable: the **camera tours** the action spots (in the original it stays
still and much of the scene happens off-screen) and the **enemy is visible in
the dark** — only in the demo; in a match, night blindness still applies.

### Multiplayer
**Networked, bit-exact lockstep** matches, with a synchronized lobby
(broadcast discovery, joining, chat, EJECT, options), the `OUT2RES.DLL`
screens and the 51 M* missions.

### Port additions (not in the original)
Keys in the `[Game]` section of `OUTPOST2.INI`, with controls on the
preferences screen; the original ignores keys it doesn't know.

| Key | Range | Default | What it does |
|---|---|---|---|
| `AILevel` | 0..3 | 0 (AUTO) | Forces the AI level in campaigns and colonies. AUTO = the original's behavior. **Does not apply in network play**, where difficulty is per player and comes from the lobby. |
| `NightDarkness` | 0..100 | 75 | How much of the night darkening curve is applied (100 = the original, which takes full night to ~15% brightness and makes units, which never darken, stand out against the black). |
| `AudioVolume` | 0..100 | 50 | Master volume over the final mix (sounds and music). The original only has `MusicVol`/`SoundVol`, per channel and in 6 steps. |
| `DemoCamera` | 0/1 | 1 | In the attract demo the camera tours the action spots (disaster, combat, units). 0 restores the original's static camera. |
| `Fullscreen` | 0/1 | 1 | Opens fullscreen (no video mode change). Alt+Enter toggles at any time, and in game there is the **Screen Mode** radio pair at the end of the `PREFERENCES` list (see 5.8 in the BR status). |
| `MenuSkin` | 0/1 | 0 | Menu style. 0 = the port's own look (dark-blue panels, main menu on the right); 1 = the original OP2 dialog skin (blueprint background, red frames, green text, art from `OP2SHRES.DLL`). |
| `MapWrap` | 0/1 | 0 | Horizontal map wrap. 0 = as in the original: the view stops at the left/right edges of the map, and only "around the world" maps (512 tiles wide) wrap; 1 = every map wraps (the port's old behaviour). |

---

## 4. What is STILL MISSING

Reviewed item by item on 2026-10-28, checking against the code.

### Large

1. ~~Networking with more than 2 players~~ — **done** on 2026-10-30. The
   topology is the same as the lobby's message layer: **a star with the host
   in the middle**. The host talks to each occupied seat and **relays** each
   client's turns to the others; a client only talks to the host, so nobody
   needs to reach everyone (NAT, firewall). The turn machine was already
   N-player — only `sendRaw` was tied to a pair. Measured: 3 and 4 players in
   lockstep, with an identical hash on every machine
   (`--mplan host|join <DLL> <ticks> <N>`).

### Multiplayer, loose pieces

All resolved on **2026-11-03**, and two of them were my misreading:

2. ~~Options tab actions~~ and ~~TRANSFER~~ — **done**. The tab became the
   original's three screens (menu, `PREFERENCES`, `END MISSION`) with the six
   buttons from `sub_457880`. And the command grid's TRANSFER **does** do
   something: `nullsub_84` is vt[3], the MAP click, which it doesn't use.
   vt[1] (`sub_455A80`) opens the *"Transfer Vehicle(s) to:"* player picker
   (`sub_46D230`), and the choice builds packet **0x18** with the selected
   units and the destination (`sub_455950`); case 24 changes each one's owner
   and notifies both sides. The button appears with the `dword_56DE58` cheat
   or with an **operating Trade Center** in a multiplayer match.
3. ~~Trade Center~~ — **done**: there was no "resource trading UI"; what the
   building does is enable TRANSFER. It went back on the production list,
   because `sub_468120` only lets 0x20 through with `dword_56DC78 != 0` — and
   that pointer is the **network layer**, i.e. the building only exists in a
   network match.
4. ~~Communications tab Chat mode~~ — **done**, and it also wasn't what the
   name suggested: mode 1 is the *"Set Up Allies and Chat Partners"* screen,
   with the two player lists and the chat (local mask) and alliance (packet
   **0x32**) checkboxes. The `&NET STATS` button opens the third screen
   (`sub_467760`), with the original's six fields. The conversation itself is
   the **in-game line** (`sub_4115F0`): Enter opens it, Enter sends packet
   **0x30**, Esc cancels.
5. ~~End-of-match statistics~~ — **done**, see the correction in item 9.

### Simulation odds and ends (all marked in the code)

6. ~~IDLE confirmation dialog when the building stores metal~~ — **done**, and
   the original under-warns. `sub_455CB0` (the IDLE button's vt[1]) compares
   **this** building's capacity with the player's **total** capacity (+0x18
   common, +0x1C rare) and only asks when they are **equal** — that is, when
   it is the only storage for that metal, which is the only case where the
   text "All metal in storage will be lost" is true. With a smelter (10000)
   and a storage tank (5000), idling the smelter with 15000 stored loses 10000
   **silently** (`sub_471F30` truncates). Here the calculation is the
   overflow, so the question covers any loss and the number replaces the
   "All". Two other fixes: the stats block index is the **high** nibble of
   `[+0x1D]` (`sar al, 4`), not the owner; and the original's rare-metal
   branch requires `rareOre != 0` while the common one doesn't, so it asks
   with empty stock — the overflow handles both. Test: `--idletest <DLL>`
   (four cases per building; E10/E12/P10 cover partial loss).
7. ~~The STORAGE panel~~ — **not pending**: checked on 2026-10-28, the
   original's rule (`loc_456560`) enables the command for 0x19, 0x1A, 0x27,
   0x29, 0x2F and 0x36–0x38, which are exactly the eight types the panel
   covers. The "not yet ported" message is just an unreachable guard.
8. ~~"Fire at ground" order~~ — **done, and it wasn't an order**: a vehicle's
   0x2000 bit is only set in `loc_44B8F0` of `sub_44B650` (movement), when a
   **computer-owned** unit armed with Laser (0x3D) or Microwave (0x3E) bumps
   into a tube/wall without a target — that's how the AI clears a path. There
   is no player command for it.
9. ~~Per-player casualty statistics~~ — **done on 2026-11-03**, and the
   earlier conclusion ("never read") was **wrong**: `MissionResults` **does**
   copy them. The end of `sub_489A30` (`loc_489CF3`) takes SEVEN dwords from
   `[player+0x1BC]` to `MissionResults+0xC2 + player*28`, and `OP2Shell` reads
   them from there — reasons 7, 8 and 9 of message 0x73a all send `0x727`,
   which opens **Dialog 155 "MULTIPLAYER GAME STATISTICS"**. There are six
   counters (`+0x1BC` buildings built, `+0x1C0` vehicles built, `+0x1C4`
   buildings lost, `+0x1C8` vehicles lost, `+0x1CC` buildings destroyed,
   `+0x1D0` vehicles destroyed); the seventh dword is the ore total, which
   the original overwrites.
10. ~~Map reveal on construction~~ — **does not exist**: checked on
    2026-10-29. The `dword_574C88+0x110` snippet converts the building's
    rectangle to screen space and passes it to `sub_46F550`, which is called
    from 36 places (among them the hover highlight) — it's the view's
    **dirty rectangle** list. Outpost 2 has no fog of war. This port redraws
    the whole screen every frame, so there's nothing to port.
11. ~~Packet 0x1D (take over) for AI groups~~ — **done** on 2026-10-29:
    `orderTakeOver` in `ai_group.cpp` (port of `sub_42A100`), called from
    case 4 of `classHandler`. The computer's Spider takes over enemy vehicles
    **paralyzed by EMP** within 256 px (`sub_427CC0`). Test:
    `--takeovertest <map>`.
12. ~~Pathfinder mode 3~~ — **not pending**: checked on 2026-11-03,
    `sub_438850` is ported in `pfMoveCost` — wall/tube blocks without
    exception and the goal is to end up next to the target (four neighbors).
    It was the comment in `pathfinder.h` that said "pending".
13. ~~Waypoint queue with Shift~~ — **not pending**: checked on 2026-10-29,
    the `unk_5471B0`/`dword_5471AC` queue is dead code in the executable. The
    counter is field +0x3C of the targeting-mode object, which `sub_41B550`
    zeroes on entering the mode and that nobody increments — the three
    functions that read it only read it. The shipped game always sends ONE
    point per command, which is what the port does.
14. ~~Wall damage from self-destructs~~ — **not pending**: checked on
    2026-11-03, `explodeDestruct` calls `damageWall` with
    (radius − d) * (Concussion + Penetration) / radius, which is exactly the
    calculation in `sub_4A4090`. Two comments (in `weapon.cpp` and
    `disaster.h`) were what announced it as pending.

### Found in the 2026-11-03 inventory

A sweep of the code's own markers against the disassembly. Five "pending
items" the comments announced **did not exist** (the garage, the microbe, the
message audio, economy phase B and the network serialization of
`CommandPacket` are all ported — it was the comments that had fallen behind).

**Second pass, same day**, this time checking each item in the code instead of
reading the comment announcing it: **another three were false** (12, 14 and
17 — pathfinder mode 3, wall damage from self-destructs and ambient
animations — were ported, with a stale comment). Of the six real ones, 16,
18, 19 and 20 were closed the same day; **21 remains**.

16. ~~Vehicle stored in a garage isn't counted anywhere~~ — **done**. This was
    the only item that could break a mission objective: the garage was ported
    on 2026-10-14 (`garageSlot[6]`, `garageCount`), but six counting points
    ignored whoever was inside it, and in the original **all of them** handle
    0x29. The six, with each one's rule read from the disassembly:

    | Point | Bays scanned | Rule |
    |---|---|---|
    | `sub_493AF0` (per-player count) | **6, fixed** (`loc_493D0E`) | type + cargo filter from `byte_493FD4` |
    | `sub_494280` (KIT trigger) | **Storage Bays** | ConVec (type 2) carrying the kit |
    | `sub_494E70` (vehicle count) | `vt[63]` | adds the count to the list |
    | `sub_494600` (EVAC trigger) | **6, fixed** | stored 0x0D transports |
    | `?HasOccupiedBay` | `vt[63]` | returns the **count**, not 0/1 |
    | `?hasVehicle` | **6, fixed** | type + cargo (always the whole word) |

    The bay inconsistency (some loops fix 6, others go up to Storage Bays) is
    **the original's**, and was kept. The cargo rule also differs between the
    two halves of `sub_493AF0`: the **single**-player branch accepts types 1,
    2, 5, 6, 7 and 0x0D (table `byte_493FD4`) and scans garages; the **all**
    branch (`loc_493DE2`) only 1, 2 and 0x0D, and does **not** look at any
    garage — also the original's, verified, and marked in the code so nobody
    "unifies" it. Test: `--garagecounttest <DLL>`, ten cases; with the fix
    neutralized, eight fail and the two negative controls still pass.
17. ~~Building ambient animations~~ — **not pending**: checked on 2026-11-03,
    `structureAmbientTick` is all of `sub_4835C0`, with the `byte_4E4138`
    table (11 records) and the per-type variant (`desc+0x2A6/+0x2A7`), the
    100-tick gate offset by y and id, and the INI's `Ambient` option. It was
    the comment in `structure.h` that said "not yet ported".
18. ~~Auto-engagement: "attacked in the last 12 ticks"~~ — **done**. This was
    the only pending item in this inventory that changed what you see while
    playing, and what `sub_44DE10` does is not target selection: it **alerts
    the neighborhood**. A unit hit in the last 12 ticks passes its own
    attacker to every armed vehicle on its side within 96 px (3 tiles) in x
    and y that is idle (`[+0x21]` = 0) and has no attacker yet (`[+0x26]` =
    0), dating the alert 25 ticks back — leaving 125 of the 150 ticks of the
    retaliation window. Result: the squad reacts together instead of only
    the one that was hit. Each alert adds a reference (`inc byte [+0x32]`)
    on the attacker. **Only applies to human owners**: the gate at the top of
    `sub_44DE10` is `dword_56E304[owner]`, which is `isHuman`
    (`[player+0x28]`); the AI has its own path, `ScGroup::HasBeenAttacked`.
    Test: `--alerttest <DLL>`, eleven cases, including the exact 96 px edge
    (the asm compares with strict `<`) and the PC-owner gate; with the fix
    neutralized, five fail and the seven negative controls still pass.

    The search box is cell-based instead of the `[+0x18]`-sorted unit list
    the original walks in both directions — the set is the same (a vehicle
    occupies one cell) and the pixel test is identical.
19. ~~`vt[35]` on vehicle death~~ — **done**, and it wasn't "per-class
    unloading": I extracted the 40 unit vtables from the asm and in all of
    them except `off_4D1788`, entry 35 is the **same** function,
    `sub_483850` — which sets `dword_56E37C[owner] = 1`, the port's
    `colonyMarkDirty`. `loc_43D8B3` only calls it with bit 1 of `[+0x44]`,
    i.e. only for vehicles. One line in `vehicleDeath`: a dead vehicle forces
    the owner's economy to recalculate.
20. ~~AI group past its deadline~~ — **done** (`sub_42B360`). The order of the
    two commands matters and is this: once the deadline passes, **for the
    first 400 ticks** each member gets self-destruct (0x14) and, **from 400
    ticks on**, whoever is still on the list gets leave-the-map (0x2A). In
    practice the second branch only reaches those self-destruct refuses —
    `unitDoSelfDestruct` requires a commandable unit, so a vehicle under EMP
    is left for 0x2A. Non-vehicles (without bit 1 of `[+0x44]`) leave the
    group first, via `vt[11]`, and get the order the same way. The group is
    not deleted there: it falls into the pass where the list reaches zero.
    Test: `--deletetest <DLL>`, eight cases covering the three branches; with
    the fix neutralized, three fail.
21. **Three presentation odds and ends**: the Spaceport platform's 0x80/0x7F
    sounds; the icon of the vehicle with its cargo in the STORAGE panel
    (`sub_458EF0`, today the animation stays still); and the
    `[player+0xBD8]` anti-duplicate ring, which is network protection against
    repeated commands.

### Not applicable on this installation

15. **Movies (AVI).** The original shell plays `I.AVI`/`IE`/`IP` at startup,
    `E4.AVI`, `MEF`/`MPF` and the eight campaign-ending variants
    (`MEBL`/`MEBW`/`MEUL`/`MEUW`/`MPBL`/`MPBW`/`MPUL`/`MPUW`) and the four
    colony-ending ones (`CES`/`CEP`/`CPS`/`CPP`). **No `.AVI` exists on
    disk** — they were on the CD. Porting the player would have nothing to
    play.

### Deliberate decisions (not pending items)

- **Screen 259 "PICK CONNECTION METHOD" doesn't exist here.** Of the six
  methods in the `unk_4DF370` table only TCP/IP survived — SIGS was Sierra's
  service, IPX was removed from Windows, modem and serial were DirectPlay over
  dial-up. MULTIPLAYER opens Dialog 260 directly. That's why `MpNetOptions` is
  the first multiplayer screen, and the `m_state >= MpNetOptions` tests depend
  on the enum's order.

---

## 5. Analysis tools (`tools/`)

Python scripts using **`pefile`** + **`capstone`** (disassembler) for reverse
engineering the resources and the code:
- **`extract_menu.py` / `dump_dialogs.py` / `parse_dlg.py`** — extract and
  interpret `DLGTEMPLATE`s from the DLLs (produce JSON such as
  `menu_dialog.json`).
- **`extract_bmps.py` / `extract_btns.py` / `inspect_bmps.py`** — extract
  bitmaps (backgrounds, buttons) to PNG/BMP for checking.
- **`analyze_shell.py`** — searches the assembly for where resource IDs (e.g.
  bitmap 225/190, string "MAIN MENU") are referenced, to discover the logic.
- **`inspect_vol.py` / `inspect_table.py`** — inspect the `.VOL` containers.

> **Python dependencies** (not versioned): `pip install pefile capstone pillow`.

---

## 5.1. `.VOL` format — DECODED ✅ (2026-09-29)

The game's asset container, conceptually the same as Doom/Quake's `.WAD`: an
entry directory + concatenated data. It uses **tagged chunks** (RIFF/IFF
style). Working parser in [tools/vol.py](tools/vol.py).

**Layout:**
```
"VOL " <u32 len>            directory container (payload = volh+vols+voli)
    "volh" <u32 len=0>      volume header (empty)
    "vols" <u32 len>        STRING TABLE:  <u32 strLen> + '\0'-separated names
    "voli" <u32 len>        INDEX:         (len/14) entries of 14 bytes
"VBLK" <u32 len> data...    one block per file (data; 2-byte aligned)
"VBLK" <u32 len> data...
...
```
- Every chunk `u32 len` has bit `0x80000000` as an "in use" flag; real size =
  `len & 0x7FFFFFFF`.
- **IndexEntry (14 bytes):** `u32 nameOffset` (0xFFFFFFFF = free slot) ·
  `u32 dataBlockOffset` (points to the "VBLK") · `u32 fileSize`
  (**stored/compressed** size) · `u8 compressionType` (+0x0C) · `u8 flag`
  (0x01).

**Compression** (confirmed in the disassembly — `sub_4A0E50` switches on
`[entry+0x0C]`):

| Type | Routine in the EXE | Algorithm |
|:----:|---------------|-----------|
| 0 | (raw block) | no compression |
| 1 | `sub_4AFA60` | probably RLE |
| 2 | `sub_434460` | probably LZ |
| 3 | `sub_433F80` | **LZHUF** (LZSS + adaptive Huffman, Okumura 1989) |

The type 3 constructor (`sub_433F80`) fills `4096-60 = 0xFC4` positions of a
ring buffer with `' '` and starts `r = 0xFC4` → parameters **N=4096, F=60,
THRESHOLD=2** (the classic LZHUF signature).

**LZHUF implemented** ✅ in [tools/lzhuf.py](tools/lzhuf.py). The position
decoding tables `d_code[256]`/`d_len[256]` were **extracted from the binary**
(file offsets `0xE9700` and `0xE9800`) to guarantee byte-exact decoding — not
reconstructed from memory. The 10 `.txt` files in `SHEETS.VOL` decompress to
clean tab-separated text (balance tables: `building.txt`, `vehicles.txt`,
`weapons.txt`, `mines.txt`, `morale.txt`, `space.txt`, `edentek.txt`,
`ply_tek.txt`, `multitek.txt`, `tutortek.txt`).

> Note: the `.VOL` does not store the decompressed size. The engine treats the
> block as a *stream* and decodes on demand until the bits run out; the sheet
> parser stops at the first invalid line. Our decoder reproduces this
> faithfully — the output may have a few bytes of "garbage" at the end
> (padding bits of the last byte), which are ignored when parsing the tables,
> just as in the original game.

**Contents of the 5 volumes (437 files):**

| Volume | Files | Compression | Contents |
|--------|:--------:|------------|----------|
| `MAPS.VOL` | 71 | raw | `.map` (maps) + `.bmp` |
| `SOUND.VOL` | 97 | raw | `.wav` (effects) |
| `STORY.VOL` | 124 | raw | `.rtf` (briefings/narrative) |
| `VOICES.VOL` | 135 | raw | `.wav` (voices, e.g. `savant*.wav`) |
| `SHEETS.VOL` | 10 | **LZHUF** | `.txt` (balance tables) |

**Status:** ✅ **`.VOL` format 100% decoded** — all 437 files in the 5 volumes
extract and validate (raw and LZHUF). Usage: `python tools/vol.py` (scan) or
`python tools/vol.py extract <VOL> <output/>`. Parser:
[tools/vol.py](tools/vol.py); LZHUF decoder: [tools/lzhuf.py](tools/lzhuf.py).

---

## 5.2. `.map` format — DECODED ✅ (2026-09-29)

The game's terrain maps (tag `0x00001011`). Parser in
[tools/op2map.py](tools/op2map.py), validated visually (rendering the grid
shows coherent terrain: mesas, lava, ore).

**Structure** (deduced from the bytes + disassembly, validated on several
maps):
```
Header (5 x u32 = 20 bytes):
  u32 tag        = 0x00001011
  u32 versionTag = 0
  u32 lgWidth    -> width = 1 << lgWidth   (e.g. 6 -> 64, 7 -> 128)
  u32 height     (in tiles)
  u32 numTileMappings  (e.g. 512 — used in the "TILE SET" section, still TODO)

TileData [width*height] (u32 each) — STRIPED LAYOUT (NOT row-major):
  idx_u32 = ((x>>5) * height + y) * 32 + (x & 31)    (vertical strips of 32 cols)
  bits 0-4  : cellType  (passability/terrain; 0-31)
  bits 5-15 : tileIndex (index into the global pool; mask 0xFFE0 >> 5, 11 bits)
  bit  27   : per-cell flag (lava/dirty), used by the paint routines
  x wraps horizontally (widthMask = width-1); height is always a power of 2.

clipRect (4 x i32): x1, y1, x2, y2  (playable area; eden01 = 32,0,95,62)

Tileset table: repeats { u32 nameLen; char name[nameLen]; u32 numTiles }
  while nameLen is plausible. E.g.: well0000..well0012 (13 .bmp sources;
  sum of numTiles = global tile pool, e.g. 2012).

(Then, in "TILE SET": tile mappings + terrain/animation groups — TODO.)
```

**Contents of `MAPS.VOL`:** 54 `.map` maps (64×64, 128×64, etc.) + a few
`.bmp`s (palettes). Dimensions come from the header; width is always a power
of 2.

**Status:** ✅ header, tile grid (cellType + tileIndex), clipRect, tileset
table and render with graphics — complete. **Mechanism confirmed in the
disassembly**: the striped access appears identically in `GameMap::GetTile`
(@191591) and in the paint routines `sub_4322C0`/`sub_432560`
(`((x>>5)<<lgHeight + y)*128 + (x&31)*4` over `dword_54F01C`). The port's
render is **pixel-identical to the wiki's original render** (eden01/eden02).
Usage: `python tools/op2map.py eden01.map` or
`outpost2_modern.exe --rendermap eden01.map out.bmp`.

**"TILE SET" section** (after the tileset table) — *understood, not needed for
rendering*: a **tile mapping table** (`numTileMappings`, e.g. 512, records
`{u16 tileNumber, u16 tilesetIndex}` + index) followed by **named terrain
groups** (`BLUE`, `mudbas1`, `mudcrk1`… = terrain types / tile animation
sequences, e.g. lava). Terrain rendering works without it (`tileIndex`
already points into the global pool); it serves tile animation and terrain
typing (engine runtime polish).

---

## 5.3. `PBMP` tilesets + terrain render — DECODED ✅ (2026-09-29)

The terrain graphics (`well0000.bmp`..`well0012.bmp`) live **inside
`MAPS.VOL`** and use the game's own **`PBMP`** format (chunked, IFF-like).
Loaded at runtime via `ResManager::OpenStream("well####.bmp")` (searches VOLs
and loose files).

**PBMP format** — parser in [tools/pbmp.py](tools/pbmp.py):
```
"PBMP" u32 size
  "head" u32 len(20)  { u32 ver=2; u32 width; u32 height; u32 bpp=8; u32 ? }
  "PPAL" u32 len      (palette)
      "head" u32 len(4)   { u32 numPalettes=1 }
      "data" u32 len(1024){ RGBQUAD[256]: b,g,r,a }
  "data" u32 len      { 8-bit pixels, width*height, top-down }
```
Each tileset is a column of stacked **32×32** tiles (`height = numTiles*32`).
Also in `MAPS.VOL`: `op2_art.prt` (art palette, `CPAL`/`PPAL` chunks) and
`virmask.raw` (mask). `OP2_ART.BMP` (root) is an 8-bit 2732×2731 BMP.

**Terrain render** — [tools/render_map.py](tools/render_map.py): ties together
the full `.VOL → .map → PBMP` pipeline and draws the map. Each cell's
`tileIndex` indexes the **global pool** (the concatenation of the tiles of all
the map's tilesets, in listed order). ✅ **Validated**: `eden01.map` renders as
coherent terrain (blue lava, rock, vegetation, canyons). Usage:
`python tools/render_map.py eden01.map output.png`.

**Status:** ✅ terrain pipeline complete and working (decode + render any of
the 54 maps with original graphics). This covers the essentials for the
engine to draw the world.

---

## 5.4. `.txt` data sheets — DECODED ✅ (2026-09-29)

The 10 `.txt` files in `SHEETS.VOL` (LZHUF) are the game's data/balance
tables. Parser in [tools/sheets.py](tools/sheets.py). Two formats:

**TSV tables** (1–2 header lines + TAB-separated rows):

| File | Records | Contents |
|---------|:---------:|----------|
| `building.txt` | 42 | structures (cost, power, HP, size…) |
| `vehicles.txt` | 17 | vehicles (HP, armor, movement, weapon…) |
| `weapons.txt` | 29 | weapons **and disasters** (EARTHQUAKE, METEOR… deal damage) |
| `mines.txt` | 9 | mine yields (High/Med/Low × variant) |
| `morale.txt` | 72 | morale (states + per-event modifiers; multi-section) |
| `space.txt` | 23 | space race objects |

**Tech trees** (`BEGIN_TECH "Name" <id> … END_TECH` blocks):
`edentek` (135), `ply_tek` (137), `multitek` (137), `tutortek` (95) = **504
techs**. Properties: CATEGORY, REQUIRES, COST, DESCRIPTION, TEASER,
MAX_SCIENTISTS, LAB, UNIT_PROP, EDEN_COST/PLYMOUTH_COST.

**Mechanism confirmed in the disassembly** (directive: always go to the
code): the game reads line by line (512B), stops at `0x1A` (Ctrl-Z) or end of
stream, and accesses records **by name** (not by count) — which is why the
LZHUF padding residue is harmless. Fields are TAB-separated; techs use
keywords with case-insensitive comparison. Usage: `python tools/sheets.py`.

---

## 5.5. `op2_art.prt` + `OP2_ART.BMP` sprites — DECODED ✅ (2026-09-30)

The game's sprite system (structures, units, UI). Parser in
[tools/artfile.py](tools/artfile.py), validated visually (colony structures
render with correct colors). Loaded in code by `sub_404360("op2_art")`.

- **`OP2_ART.BMP`** (root): 8-bit pixel atlas, 2732×2731, uncompressed.
- **`op2_art.prt`** (inside `MAPS.VOL`): metadata. Structure:
```
"CPAL" u32 numPalettes(=9)
numPalettes x "PPAL" { "head"{u32=1}; "data"{ RGB[256]: r,g,b,flag } }
u32 numImages(=5390)
ImageMeta[numImages] (20 bytes each):
   u32 scanLineByteWidth  (width in bytes, multiple of 4)
   u32 pixelDataOffset    (offset into OP2_ART.BMP's pixels)
   u32 height
   u32 width
   u16 type
   u16 paletteIndex
(then: animation definitions/sprite groups — TODO)
```
The images are 8-bit indexed pixels, top-down, **packed sequentially** in the
atlas: `offset[i+1] = offset[i] + scanLineByteWidth[i]*height[i]` (verified).
The palette is R,G,B (confirmed in the loader: `byte0→red` in the RGB565
packing). The **9 palettes are independent** (not player color variations) —
each image declares its own in `paletteIndex`; **always render with the
image's own palette** (`m.pal`). Player color in OP2 is a runtime remap, not
one of the 9.

**Status:** ✅ **5390 images** extractable from the atlas with the correct
palette (structures, units and UI render correctly). Usage:
`python tools/artfile.py`.

**Animations** — parser in [tools/artanim.py](tools/artanim.py), validated by
**exact consumption** (counts match the header) and by render (coherent
frames). Structure (after the ImageMeta array), confirmed in the disassembly
(`sub_404360`):
```
u32 numAnimations(2079); u32 numFrames(24185); u32 numPictures(160922); u32 numFrameExtra(6012)
Animation[numAnimations]:
   u32 header[9]  (header[8]=numFrames of this anim; header[3..6] ~ bounding box)
   Frame[numFrames]:
      u8 b1 -> numLayers = b1 & 0x7F ; flag1 = b1 & 0x80
      u8 b2 -> flag2 = b2 & 0x80
      if flag1: u16   ; if flag2: u16   (shadow/speed offset)
      Picture[numLayers] (one "layer" of the frame):
         u16 imageIndex ; u8 flag ; u8 slot(order) ; i16 x ; i16 y
   u32 extraCount ; ExtraRecord[extraCount] (4 x u32)
```
✅ **2079 animations, 24185 frames, 160922 layers** parsed; each frame is a
stack of atlas images positioned at (x,y), drawn in `slot` order and with
each image's own palette. Usage: `python tools/artanim.py`.

> The final table (~12KB, offset 0x178E56) **is not read by the art loader**:
> the disassembly shows `sub_404360` freeing the stream and returning right
> after the animation loop (`loc_404C49`). It's vestigial data or belongs to
> another subsystem — ~8-byte records referencing the atlas's last images at
> grid positions.

---

## 6. Build and running

**Toolchain:** CMake >= 3.15 + MSVC, C++17, SDL2 2.30.9 (in `third_party/`).
The build is **Win32 (x86)** — the mission PE loader runs the original DLLs'
32-bit code, so x64 won't do.

```powershell
cmake -B build -S . -A Win32
cmake --build build --config Release
```

The post-build step copies `outpost2_modern.exe` and `SDL2.dll` to the game
root, which is where the executable must run from (it reads the `.VOL`s, the
mission DLLs and the original resources from the working directory).

> If the copy fails with "Permission denied", the game is running — close it
> and rebuild. Filtering the build output for `"error C"` **hides** that
> failure; filter for `error|denied`.

### Command-line tests

There are 106 modes. The ones that cover real regressions:

| Mode | What it proves |
|---|---|
| `--savetest <DLL> <ticks> <more>` | determinism: saving, reloading and continuing must give the same hash |
| `--mplan host\|join <DLL> <ticks>` | network lockstep: both machines finish with the same hash |
| `--mptest <DLL>` | local multiplayer mission |
| `--mpui host\|join` | the network screens' flow, through the buttons' real dispatch |
| `--uitest` | combo boxes and scrollbars, via the mouse and keyboard paths |
| `--chattest <DLL> [screen]` | the chat line: sending, cancelling and the recipient mask |
| `--transfertest <DLL>` | TRANSFER: picker, cancelling and the change of owner |
| `--idletest <DLL>` | the warning about losing metal when idling storage/smelter, on both sides of the boundary |
| `--garagecounttest <DLL>` | the six points where a vehicle stored in a garage must count |
| `--stacktest <DLL> [out.bmp]` | two vehicles never hold the same tile: the whole side's vehicles are right-clicked back and forth through each other |
| `--alerttest <DLL>` | an attacked unit alerts armed neighbors on its own side (`sub_44DE10`) |
| `--deletetest <DLL>` | AI group past its deadline: self-destruct and, 400 ticks later, leave the map |
| `--missiontest <DLL> <output> <ticks>` | loads and runs any mission |
| `--aitest <campaign DLL> <multi DLL>` | `AILevel` reaches player 0 and multiplayer stays intact |
| `--helptest [topic]` | the `.HLP` reader and the manual screen's navigation |
| `--demotest` | the `ademo` rotation, the demo's `GameStartInfo` and interruption by key |
| `--salvagetest`, `--oretest`, `--earthtest`, `--combattest`, `--researchtest`, `--garagetest`, `--gptest`, `--pinwheeltest` | each subsystem |
| `--screenshot` + `--menu`/`--prefs`/`--mpsettings`/`--panetest`/... | screen captures for visual checking |

---

## 7. Summary by area

| Area | Status |
|---|---|
| Infra (SDL2, framebuffer, input, scaling) | Complete |
| Reading the originals (PE, `.VOL`/LZHUF, PBMP, sprites, sheets) | Complete |
| Terrain and map | Complete (render pixel-identical to the original) |
| Units, movement, pathfinder | Complete |
| Structures and construction | Complete |
| Economy, food, population, morale | Complete |
| Combat | Complete |
| Research | Complete |
| Disasters, lava and blight | Complete |
| Triggers, objectives, victory/defeat | Complete |
| AI (groups, default computer, Pinwheel) | Complete |
| Campaign, colony, tutorials | Complete |
| Shell and screens | Complete |
| Manual (OUTPOST2.HLP) and attract demo | Complete |
| HUD, tabs, minimap, globe | Complete |
| Audio and music | Complete |
| Save/load | Complete |
| Network multiplayer | Complete: up to 6 players, chat, alliance, transfer and statistics |
| Movies | Not applicable (files don't exist) |

---

## 8. Useful technical notes

- **Original EXE imagebase:** `0x400000`. Compiled with Visual C++, timestamp
  Aug 09 1997. Original PDB: `C:\Tethys\release\Outpost2.pdb` ("Tethys" was
  the game's codename/world).
- **EXE sections:** `.text` 842 KB of code, plus `.rdata/.data/.rsrc/.reloc`.
- **`.i64`** is the IDA Pro database — open it to browse the disassembly with
  names/comments already assigned, instead of reprocessing the raw `.asm`.
- The port **does not modify** any original 1997 file; it only reads them.
