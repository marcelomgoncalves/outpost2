**English** · [Português (BR)](README.BR.md)

# Outpost 2: Divided Destiny — Modern Port (C++17 + SDL2)

A reimplementation of *Outpost 2: Divided Destiny* (Sierra On-Line / Dynamix,
1997) in **C++17 + SDL2**, rebuilt through reverse engineering from the
disassembly of the original executable.

**The game runs end to end**: both factions' campaigns, the colony games, the
14 tutorials and networked multiplayer matches for up to 6 players. The 94
original mission DLLs are loaded and **actually executed**, by a custom PE
loader with ABI bridges for the 222 symbols they import from `OUTPOST2.EXE`.

> **Project directive:** every mechanism is extracted from the **original
> code** (disassembly and binaries) — nothing is guessed. Where the original
> has an identified defect, the port **fixes it instead of reproducing it**.

See [`PORT_STATUS.EN.md`](PORT_STATUS.EN.md) for the detailed status: what is
done, what is missing, and the original mechanism behind each part.

---

## ⚠️ Nothing from the 1997 game is here

This repository contains **only the port's code**. The game files
(`OUTPOST2.EXE`, the `*.DLL`s, the `*.VOL`s, `OP2_ART.BMP`, `VIRMASK.RAW`, the `*.ANI`s,
`OUTPOST2.INI`, `OUTPOST2.HLP`…) and the disassembly are protected commercial
material and are **not** versioned or redistributed.

The port **reads** those files from wherever they already are and modifies
none of them. To play, you need a legitimate installed copy of the game.

---

## Requirements

| | | |
|---|---|---|
| System | **Windows** 10 or 11 | the port uses the Windows API (PE loader, Winsock, GDI) |
| Architecture | **Win32 / x86 — mandatory** | not a preference or a default: see why below |
| Compiler | MSVC with C++17 (Visual Studio **2019 or newer**) | the code uses `__fastcall` and the Microsoft ABI; MinGW/Clang have not been tested |
| CMake | **3.15 or newer** | also tested with CMake 4.x |
| SDL2 | development package **2.30.9 for VC** | the executable's only external dependency |
| Original game | **mandatory**, installed | the 1997 assets are read from disk |
| Python 3 | optional | only for the scripts in `tools/` |

There is no vcpkg, Conan or submodules. `ws2_32` and `ole32` (networking) and
`gdi32` (fonts) are resolved by MSVC itself — nothing to install for them.

### Why the x86 build is a requirement

The port loads the original mission DLLs and **runs their 32-bit code inside
its own process** (`src/mission/pe_loader.cpp`), with API bridges using the x86
`__fastcall` convention (`src/mission/op2_api.cpp`). In a 64-bit process this
is impossible: an x64 build **compiles** and only fails when opening the first
mission. To keep that trap from getting that far, `CMakeLists.txt` stops at
configuration time if the architecture is not 32-bit.

---

## Installing the dependencies

### 1. Compiler (MSVC)

Install **Visual Studio Community** (free, 2022 or newer) —
<https://visualstudio.microsoft.com/downloads/> — and, in the installer, tick
the **"Desktop development with C++"** workload. It already includes
everything the port needs: the MSVC compiler, the Windows SDK and **x86
support**, which is the target used here.

If you don't want the IDE, you can install just the command-line tools
(*Build Tools for Visual Studio*, same page). With winget:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

### 2. CMake

Visual Studio's C++ workload includes a CMake. To have the `cmake` command
available in any terminal, install the standalone version:

```powershell
winget install --id Kitware.CMake
```

Or download it from <https://cmake.org/download/> and tick *"Add CMake to the
system PATH"*. Check with `cmake --version` (must be 3.15+).

### 3. SDL2

Download **`SDL2-devel-2.30.9-VC.zip`** — the *development* package for
Visual C++, not the runtime one — from
<https://github.com/libsdl-org/SDL/releases/tag/release-2.30.9>.

Extract it inside the repository so that the tree looks **exactly** like this:

```
third_party/
  SDL2-2.30.9/
    include/          SDL.h, SDL_render.h, ...
    lib/
      x86/            SDL2.lib, SDL2main.lib, SDL2.dll   <- the ones the build uses
      x64/            (comes in the package; not used)
```

That path is the `SDL2_ROOT` in `CMakeLists.txt`. If you prefer SDL2 somewhere
else, change that variable there. The `third_party/` folder is not versioned —
that's why this step is manual.

In PowerShell, from the repository directory:

```powershell
mkdir third_party
Expand-Archive -Path "$HOME/Downloads/SDL2-devel-2.30.9-VC.zip" -DestinationPath third_party
```

### 4. Python, only if you'll use `tools/`

The 20 reverse-engineering scripts in `tools/` need **Python 3**. Most use
only the standard library; nine of them need two packages:

```powershell
pip install pefile capstone
```

`pefile` (reading PE resources and exports) is used by `analyze_shell.py`,
`check_dialog_fmt.py`, `disasm_menu.py`, `dump_dialogs.py`, `extract_bmps.py`,
`extract_menu.py`, `inspect_bmps.py`, `inspect_table.py` and `parse_dlg.py`;
`capstone` (disassembler) by the first two. **None of this is needed to
compile or play.**

---

## Build

With the dependencies in place, from the repository directory:

```powershell
cmake -B build -S . -A Win32
cmake --build build --config Release
```

The `-A Win32` is not optional. If it is omitted on a 64-bit machine,
configuration stops with a message saying what to do.

The post-build step copies `outpost2_modern.exe` and `SDL2.dll` to the
project root. **The executable must run from the game's installation
directory** — that's where it reads the `.VOL`s, the mission DLLs and the
original resources from. The simplest way is to clone the repository **inside
the game folder**: the `.gitignore` is a whitelist, made exactly for this, and
ignores all the original material that lives alongside the sources.

### If something goes wrong

| Symptom | Cause |
|---|---|
| `Outpost2Port precisa ser gerado para Win32 (x86)` | the `-A Win32` was left out |
| `SDL.h` not found, or `SDL2.lib` missing | SDL2 is not in `third_party/SDL2-2.30.9/` (check the folder name) |
| `MSB3073` / *"Permission denied"* when copying the exe | the game is running; close it and rebuild |
| compiles, but every mission fails to open | the build came out x64 — delete `build/` and configure again with `-A Win32` |

> Filtering the build output only for `"error C"` **hides** the post-build
> copy failure. Filter for `error|denied`.

---

## Running

```powershell
outpost2_modern.exe                      # main menu
outpost2_modern.exe --game eden01.map    # opens a map directly
```

The game's `OUTPOST2.INI` file is respected. The port adds fourteen keys the
original doesn't have — AI level, night darkness, volume, demo camera,
fullscreen, the menu skin (`MenuSkin`), horizontal map wrap (`MapWrap`), the
map's bottom row (`ShowBottomRow`), the one-click ore route
(`AutoOreRoute`), refused-command diagnostics (`CommandDebug`), the
production queue (`ProductionQueue`), right-click orders
(`RightClickOrders`) and in-game list scroll bars (`ListScrollBars`) and the O/U dock hotkeys
(`DockHotkeys`); they are documented in `PORT_STATUS.EN.md`.

There are **134 command-line modes** for tests and captures, among them a
regression battery (`--savetest` proves save/reload determinism). The list is
in `PORT_STATUS.EN.md`.

---

### Cheats

The original's cheats are not keys: they are sentences typed into the **chat
line** (Enter). On a match the text turns into `Woof Woof!` and the effect
applies to whoever typed it. **They only work in single player**: in a network
game or a multiplayer scenario the sentence does nothing and goes through as an
ordinary message — the original let them work in any game, which ruins it for
everyone else. **The punctuation is part of the password:**

| Sentence | Effect |
|---|---|
| `Dan's dog could make more ore.` | fills both ore stores to capacity |
| `Dan's car could hold more people.` | workers and scientists +25% |
| `Dan's bike could backfire more often` | weapons reload faster |
| `Dan's gun could die more people.` | weapons with far more penetration |

The first three are from the 1997 game. The fourth is an **addition of this
port**: the effect exists in the original, but its sentence was never
published and the executable only stores the hash — so this sentence was
made to reach it. `PORT_STATUS.BR.md` has the whole recogniser, the hashes,
and what was checked and discarded from the cheat lists that circulate.

---

## Structure

```
src/
  main.cpp        entry point + the 134 test/capture modes
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
```

That's 45,200 lines of C++17. The 1997 formats are all decoded and documented
in `PORT_STATUS.EN.md`: the `.VOL` container (with LZHUF), the `.map`, the
PBMP tilesets, the `op2_art` sprites, the data sheets and the `.ANI` cursors.

---

## Recent changes

### 2026-11-21

- **O/U dock hotkeys (optional).** With the **Dock Hotkeys (O/U)** option on,
  select the structure with a vehicle on its dock and press **O** to load or
  **U** to unload, without opening the items screen. At the Structure
  Factory, O takes the kit from the first occupied bay and U stores it in the
  first empty one. The garage, Agridome, smelters and tanks work too. Off
  (the default), it is the original game.

### 2026-11-20

- **The objectives list no longer vanishes.** In Eden mission 05, with the
  list scrolled down, when an objective was completed (and left the list) or
  a new one came in, the window showed a single line or nothing, and the
  remaining objectives went out of view. The list now follows the changes and
  always shows the objectives left. (The "Communications Satellite: Not
  Operational" screen is the Space Program, the fifth tab button; objectives
  are the sixth, the rightmost.)

- **The briefing screen as in the original.** Each mission's briefing text
  sits in a narrow box on the left, with the mission's picture on the right
  (the 12 Eden and 12 Plymouth pictures, which come from the game itself).
  Objectives and story keep the wide box, as there.
- **The briefing text scrolls with the mouse.** The wheel works, also on
  fine-step wheels and touchpads, and the text box's scroll bar works: arrows,
  track clicks and thumb dragging. Objectives and story too.
- **Scroll bars and the wheel in every list.** In the menus: tutorials, the
  help (topics and text), scores and chapters — which cut off the last ones by
  mission 12. In game, as an option (**List Scroll Bars**, in the preferences
  and the Options tab; off by default): the report, options, objectives,
  research, production and vehicle lists get a draggable bar instead of the
  page arrows, and the wheel scrolls the list under the cursor.
- **Scrolling the map up and down from the screen edge works again** in a
  maximized window (the cursor left the window over the title bar or the
  taskbar and scrolling stopped). And, with the **Wheel/Scroll Bars** option
  (the new name of List Scroll Bars), the mouse wheel scrolls the map.
- **Eden mission 05: the empty Cargo Trucks objective** never completed.
  Fixed; all 22 objectives of the mission were checked one by one.
- **`--harness` runs nearly all OP2TestHarness scripts**: the economy,
  colony, earthwork, disaster and AI group verbs, so op2-rs's scenarios can
  be compared with the original. Only mouse and keyboard input is left.

### 2026-11-19

- **Ore trucks no longer jam at the dock.** With several trucks on the same
  route, the one that had just unloaded got boxed in by the ones waiting and
  the cycle stopped. Now the waiting trucks get out of the way (or swap places
  with it), and no truck drops its route halfway. Tested with up to 25 trucks
  on a single smelter.
- **The attack cursor as in the original.** With units selected, hovering an
  enemy shows the attack cursor (the move cursor over the ground, the dock
  cursor over a dock), and the click does what the cursor shows: over an
  enemy, it **attacks**.
- **The mouse as in the original: the left button does it all.** With units
  selected, the left click gives the order — on the ground it moves, on an
  enemy it attacks, on a dock it docks — and on one of your units it selects
  it; the right button clears the selection (and cancels the chosen command)
  — to drag a new selection box, clear the selection with the right button
  first, as there.
  The modern RTS scheme the port used (left selects, right orders) is still
  there: the **Right-Click Orders** option in the preferences and the Options
  tab.
- **Every port option now defaults to off.** The default is always the
  original game; the port's additions are switched on in `GAME PREFERENCES`.
  The one-click ore route (**Auto Ore Route**) and the touring demo camera
  (**Demo Camera**), which used to default to on, now default to off.
- **The three-click ore route button shows the right cursor.** After pressing
  the button (the two-arrow one), the cursor becomes the **dock** cursor over
  the dock that counts at that step — the mine's, then the smelter's, then the
  mine's again — and the "no dock" cursor elsewhere, as in the original. It
  used to stay the arrow, and the button seemed not to work.
- **Eden mission 04: the data transmission works.** A Scout sent to a lab by
  clicking on top of it stopped next to it and kept trying to arrive forever,
  never transmitting. Now, touching the building, it arrives and transmits,
  and the mission can be completed.
- **Night as in the original: the units' light.** At night, every vehicle with
  its headlights on and every powered building opens a circle of light in the
  dark, as large as its sight range, with a dotted edge — the light moves with
  the vehicle. And buildings and vehicles outside the light now darken, the
  enemy's more than yours, as there. Night darkness (`Night Darkness`) now
  defaults to 100%, the original's.
- **Production queue (optional).** With the **Production Queue** option on in
  the preferences, each click on an item in a factory's list adds +1 to the
  order (the count shows in front of the item; right-click removes 1), items
  can be mixed, and `BUILD IT` sends it all to the queue: the factory produces
  one after another on its own. A click the ore cannot cover does not add.
  `ST&OP` stops the current item and the rest of the queue. Off (the default),
  it is the original game.

### 2026-11-18

- **Research and production no longer fail "at random".** An order at a lab
  or factory sometimes only took after several tries: the protection against
  repeated clicks kept stale entries and dropped the new command. Fixed. And
  the **Command Debug** option (in the preferences, or `CommandDebug=1` in
  `OUTPOST2.INI`) now tells you, in the message log, why a building command was
  refused.
- **The Blight shows on the minimap** and grows there along with the
  infection, as in the original. The minimap also got the original's pure
  player colours, and now marks mining beacons and known wreckage in white.
- **The ConVec info pane shows the kit it carries** ("Cargo" and the kit
  name, or "(empty)"), as in the original; the truck's shows its cargo and
  amount.
- **The Structure Factory dock opens** when a ConVec stores or picks up a kit:
  the door opens, the kit goes down and back up, with the claw and door
  sounds.
- **A mine no longer jams after a dump at its dock.** A truck that dumped its
  cargo while docked at a mine got stuck there, and the mine loaded no other
  truck.
- **Eden mission 2 can be won**: the research asked for in the objectives
  ("Reconstruct … database") now counts when the lab completes it. The research
  trigger fired at the very first moment of the mission and was switched off
  before any research happened — this applies to every mission with a research
  objective.
- **The Blight shows up**: the original's purple checkerboard over infected
  terrain, with the *Display Computer Overlay* option on. It already spread, but
  was never drawn. The edges use the game's `VIRMASK.RAW` (put it in the game
  folder if it is not inside the volumes).
- **Work vehicles show their work animation**, as in the original: the
  Earthworker laying tube or wall, the ConVec building or repairing, the Spider
  and the repair vehicle repairing, the Robo-Dozer bulldozing. They used to
  spin their driving animation while standing still.
- **A tube touching a building's dock does not connect the building**, always,
  as in the original. It used to connect or not depending on the order of the
  buildings — the tube looked done and the building stayed off.

### 2026-11-17

- **Ctrl+click waypoints**, as in the original: after pressing a command
  (move, attack, guard, build, repair, patrol, ore route…), each **Ctrl+click**
  on the map marks a point — a numbered red square — and the unit goes through
  them, in order, before heading to the target. Up to 7 points for most
  commands.
- **Shift in selection**, as in the original: **Shift+click** on a vehicle
  adds it to the selection, or removes it if it was already there; the
  **middle button** does the same. **Shift+box** adds the vehicles to the
  selection (a box without Shift still replaces it).
- **The beep when closing the game**, as in the original: on exit the window
  disappears, a beep plays and the game waits for it to finish. With sound
  off it closes at once (the original waited 3.5 s for nothing).
- **Vehicles in a group no longer keep moving forever.** Sending many vehicles
  to the same place left some of them trying to arrive forever. A pushed
  vehicle now walks to the side tile as in the original (and, if that tile is
  taken, pushes or waits its turn), and a unit that finds no path waits a
  little longer after each try instead of retrying every instant.
- **The port's options can be changed in game**, without editing the INI.
  The main menu's `GAME PREFERENCES` is wider and has a new *"Port extras"*
  column: **Auto Ore Route**, **Wrap All Maps**, **Show Bottom Row**, **Demo
  Camera**, **Original Menu Skin** (the skin switches right away) and the
  **Night Darkness** slider. Unchecked, each box behaves as the original. In
  a mission, the Options tab's *Game Preferences* list gained **Audio Volume**,
  **Night Darkness**, **Wrap All Maps**, **Show Bottom Row** and **Auto Ore
  Route**, which take effect on `&APPLY` without restarting the mission.
- **The map's bottom row is hidden**, as in the original: the view stops at
  the bottom of the map's clip rect (`height - 2` on the stock maps), so the
  last tile row is never on screen. `ShowBottomRow=1` in `OUTPOST2.INI` lets
  the view scroll down to it.
- **The Garage floor is drawn**, as in the original's
  `Garage::DrawGroundLayer`: the Plymouth garage's grey pad with the yellow
  arrows, and the dark floor with lights inside the Eden garage (until now
  the ground showed through). One animation per state (0x214/0x216/0x218 or
  0x85A/0x85C/0x85F, from the same table as the garage body), drawn in the
  ground layer under the units like the other building pads.
- `--missiontest` lists each building's state and animation.

### 2026-11-10

- **Whoever comes head-on gives way.** Two vehicles meeting head-on in a
  narrow passage no longer just wait for each other: the one between two steps
  gets a detour to a side tile, waits there a moment and carries on, as in the
  original. The port used to leave both standing until they gave up and looked
  for another route. Also, a defect of the original fixed: if the detour tile
  gets blocked, the vehicle no longer makes a pointless stop at the next point
  of its path.
- **A vehicle behind a building stays behind it.** The port drew every
  building and then every vehicle, so a vehicle passing north of a tall
  building was drawn over it. The order is now the original's: buildings and
  vehicles together, top to bottom of the map; shots and explosions on top; and
  disasters (lightning, tornado, meteor) last.
- **The truck's load no longer pokes out of the dock.** While docking, the arm
  carrying the ore into the building was drawn sliding down over the terrain.
  The game's sprites carry per-piece clip rectangles, which the port read but
  did not apply when drawing; now it does, as the original — it also covers
  loading at the mine and the Magma Well.
- **A parked vehicle gets out of the way.** A truck (or any vehicle) parked on
  a dock, or in the path, is now pushed to a neighbouring tile when another
  needs to pass, as in the original. The port used to let both drive onto the
  same tile. A busy vehicle (mid-route) or an armed enemy is not pushed: the
  one arriving waits a little and finds another way.
- **One-click ore route** (*an addition of this port; not in the original*):
  with empty cargo trucks selected, **right-clicking a mine's dock** builds the
  whole cycle — it picks the nearest compatible smelter **that is running and
  has room** and off it goes. Works for common ore, rare ore and the Magma
  Well, for both factions. The three-click command is untouched;
  `AutoOreRoute=0` in `OUTPOST2.INI` turns the shortcut off.

### 2026-11-08

- **The map view stops at the edges**, as in the original: only "around the
  world" maps (512 tiles wide) wrap. Units, terrain and the demo camera follow
  the same rule. `MapWrap=1` brings back wrapping on every map, the old
  behaviour.
- **A `Screen Mode` radio pair** — full screen or window — on **both**
  preferences screens: `PREFERENCES` in the main menu, and the `PREFERENCES`
  list in the in-game Options tab. It only takes effect on `ACCEPT`/`&APPLY`,
  and writes `[Game] Fullscreen` to the INI.
- **The in-game `PREFERENCES` list was hiding its own scroll arrows** behind
  the buttons: nothing past the first page could be reached, and the pane was
  even stealing the `L&ESS` button's click.
- **The `L&ESS`/`&MORE` tooltips** are now the original's, one per item
  ("Turn music volume down (L&ess)"), instead of one generic sentence.
- **Meteor Defenses no longer shoot down an ally's missile.** They only spared
  the owner's own missile, so in an allied game a friend's defense shot down
  the missile on its way to the enemy.
- **Walls no longer fail to build** next to a wall under construction, Blight
  or lava. A neighbouring building still blocks, as it should.
- **`Ctrl+P` pauses the game** (and so does the `PAUSE` key, as in the
  original's accelerator table): the clock stops, music and sound go quiet, and
  the *"Game Paused. Press CTRL+P to Resume"* banner comes up. Multiplayer
  games don't pause — the status bar says *"Cannot Pause in multiplayer
  games"*.
- **"MISSION ACCOMPLISHED" and "MISSION FAILED" are the game's banner
  again:** large bold Arial, **red**, with the 2 px black outline — the port
  wrote them in its small debug font, yellow and tiny.
- **ESC asks before abandoning a mission.** It used to drop straight back to
  the menu; now it first cancels whatever is pending (a command target, a
  dialog) and, with nothing pending, opens the game's own end-of-mission screen
  with the question *"Are you sure you want to abort the mission?"*.
- **The attract demo no longer ambushes you.** The one-minute idle clock
  restarts every time the menu comes back — after a while in a mission,
  leaving it dropped you straight into the demo.
- **The game's own cursors are in.** Move, attack, dock, repair, reprogram,
  bulldoze, guard, patrol, salvage and self-destruct now use Outpost 2's
  animated cursors, each with its "can't do that" variant — the port used the
  system arrow for everything.
- **Clicking anywhere on a building selects it.** Only its middle used to
  answer — the click used a 20 px radius from the centre for everything, which
  left most of a 3x2-tile building dead.
- **The view scrolls at the screen edge**, as in any RTS: pushing the pointer
  against the window's border moves the map, with acceleration, and the minimap
  follows. The arrow keys do the same through the same path — and the
  *Scrolling Speed* setting in `PREFERENCES`, which the port read but never
  used, finally drives the speed.
- **Cheats are now single-player only.** In the original the sentence works in
  any game, network ones included — where one player filling their own stores
  ruins it for everyone else.
- **The simulation clock was raised too early.** The game raises its tick
  counter only at the end of the cycle, after the unit loop; the port raised it
  first, so everything gated on `(id ^ tick)` ran a tick out of phase.
- **The debug HUD drew a random number every tick**, shifting the generator's
  sequence — every roll after it (weapon damage, for one) came out different
  from the original. It now reads the generator's state instead of drawing.
- **Dock pads, Magma Vents and the minimap zoom**: the pads of mines, storage
  tanks and the Agridome now come from the original's ground-layer pass (with
  damage states), Magma Vents are drawn at last, and the minimap opens fitted
  to the whole map, as the game does.
- **Cargo Trucks can be partially loaded.** The game only allowed loading
  with enough in storage to fill the truck, so the last 900 units of metal were
  stuck there. Along with it, objectives like "have 5 trucks of common metal"
  now count by **amount**: five trucks adding up to 4900 do not complete it.
- **The Blight never infected a structure from a mission call**: `DoInfect`
  returned without doing anything whenever the game had more than one player —
  that is, in nearly every campaign mission.
- **Ship wreckage placed on rubble was collected as rubble**, and the tile was
  then bulldozed, taking the wreckage with it.
- **Missions' `ForceMorale*` only took effect on the second call**, and the
  first one left the colony at `Great` instead of the level asked for.
- **Invisible "perma-walls"**: an Earthworker that died while building a wall
  left the cell reserved forever, blocking the path with nothing to show.
- **Walls are no longer laid on top of a unit standing on the tile**: the
  operation is cancelled and the metal refunded, as it already was for tubes.
- **Savant alerts that would not stop repeating.** "Power Levels: Optimal"
  and "Storage Needed" fired on every colony recalculation; they now fire on
  state change only. Shortage warnings still insist, as they should.
- **Transfers to/from AI players no longer alert.** Mission scripts transfer
  units all the time, and each one raised a message on screen.
- **Alerts lost in the first two minutes of a game** (a defect of this port,
  not of the original): the anti-repeat filter compared against a clock that
  starts at zero, which silently barred each game's first "Storage Needed" and
  "Structure Disabled".
- **A cap of 2048 live units.** There was no cap at all, and a unit id has to
  fit in the 11 bits of the map cell's occupancy field (2047 max): going past
  it would not report "full", it would silently truncate the occupancy.

### 2026-11-07

- **Right-click move hit the wrong tile and never docked.** The target came
  from window coordinates instead of map-view coordinates, so units went to a
  point 12 px right and 22 px below the click — usually one tile low. The path
  also bypassed the original's move logic (`sub_451280`), so right-clicking a
  building's dock never sent the dock order. It now uses the view coordinates
  and the same path as the MOVE button.
- **Agridome dock pad.** The Agridome has a truck dock (`BUILDING.TXT`:
  Docking Truck = Y) but its art has no pad, so trucks parked on bare ground.
  It now gets the same pad every other docked building carries in its art
  (images 385-388, placed as in all 10 Structure Factory / Smelter / Spaceport
  / GORF groups), for both colonies and only once the building is finished.
- **The game screen follows the window size.** It used to be a fixed 640x480
  image stretched to the window, so a bigger window only made everything
  bigger. The game is now drawn at the real window size: the right column
  (minimap, tabs, panel) keeps its 192 px pinned to the right edge, the map
  view takes the rest, and resizing the window or toggling fullscreen re-lays
  it out. The menus stay at 640x480 scaled, as in the original. The frame
  reaches the real edge, and the save/load dialog and the "Success" box are
  centred.
- **A `MenuSkin` key** to choose the menu style. `0` (default) is the port's
  own look — dark-blue panels, translucent main menu on the right. `1` is the
  **original Outpost 2 dialog skin**, with all art read from the game's own
  `OP2SHRES.DLL`: the navy "blueprint" background (bitmap 225), red border and
  title bar with green bold Arial; buttons from the game's red button art
  (bitmaps 235/236) with a bright outline on focus; black lists with a blue
  selection; the main menu measured from the original (192x348). Without the
  bitmaps every control falls back to the port's style.
- New test modes: `--dockclicktest <DLL>`, which right-clicks an Agridome dock
  with a cargo truck through the real input path and checks that it docks, then
  that a plain move lands exactly on the clicked tile; and an optional camera
  position for `--missiontest`. Screenshots now save at the real frame size.

### 2026-11-06

- **The `END MISSION` items did nothing** — two defects plus three divergences
  from the original. See section 5.7 of `PORT_STATUS.BR.md`.
- **Cheats typed into the chat line**, as in the original, plus one addition of
  this port for the fourth effect. See the cheats section above.
- **English documentation**: `README.EN.md` and `PORT_STATUS.EN.md`. The
  Portuguese originals became `README.BR.md` and `PORT_STATUS.BR.md`, and
  `README.md` is a landing page for both languages.

---

## License

This port's code is under the **MIT** license (see [`LICENSE`](LICENSE)).

The license covers **only the port's code**. It does not extend to anything
from *Outpost 2: Divided Destiny* — not the executable, the DLLs, the `.VOL`s,
the art, the sounds, the maps, or the disassembly used as reference. None of
that is in this repository, and a legitimate copy of the game remains a
requirement to run it.

## Credits

*Outpost 2: Divided Destiny* is by Sierra On-Line / Dynamix. This is an
independent preservation and reimplementation work, unaffiliated with the
rights holders.

The community projects **OPUPatch** and **TethysAPI** were used as
documentation references — to check class names, offsets and the list of the
original's known defects. **No code from them was copied here**, as they are
under a different license.

Reverse Engineering done by Claude Code!
