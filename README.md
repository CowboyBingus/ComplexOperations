# Complex Operations

A standalone HellForge code mod for eight-player sessions: eight hellpods on the ship, eight slots in the game's
per-player managers and session lists. The default build still takes four players; `--players 8` builds one that
takes eight (see Build).

It runs on game build 25480438 only. On any other build it logs the reason and changes nothing.

## What it does

| Part | Effect |
|---|---|
| Pod manager | The game's ship hellpod manager and the ship's pod table hold eight pods instead of four. |
| New pods | Four more launchers of the bridge's level become pods: hellpod5 to hellpod8. |
| Launch | The launch cutscene fires pod n + 4 together with pod n. |
| Briefing camera | Pods 5 to 8 get a briefing camera of their own. |
| Player managers | Seven per-player component managers hold eight players instead of four (see below). |

The mod needs the bridge with the eight-pod bay: level data that places the four launchers (with the names
hellpod5 to hellpod8), four briefing cameras and the bay's lamps. Without that data it leaves a normal ship with four
pods and says so in its log.

Every player in the session needs the mod.

## Player managers

The game keeps one record for each player in seven managers: player, score, customization, kill streak, ship
inventory, ship customization and ship modules. Each has four slots. The mod builds all seven in its own memory with
eight slots and changes the 1,115 code sites that address them, the score manager's loot rows among them. 6 caves do
the loot rows' code that the game wrote out for four rows (see Mission).

This alone lets no fifth player join: the session's own lists still hold four. It is the base the later parts need.

## Session lists

The session keeps its peers in arrays of four, packed in one object. The lists that will hold eight stay in place.
The six arrays behind them move to the mod's memory: index flags, party records, ready peers, a third peer list,
peer values and hostability. 281 code sites get the new distance; 20 of them through a short stub, because the
instruction has no room for it.

With the room made, the lists count to eight: 119 sites that loop, compare or clear for four entries do so for eight.
In 17 places the game's compiler wrote the code out for exactly four entries; there the mod adds the other four, with
code of its own beside the game's (14) or a hook of the function (3).

Other tables of the game stay at four entries and are indexed by a player index with no check, or filled with no
bound. Among them are the galactic map's cursors, the game state's player rows, the loadout screen's cards, the mission
result's and the mission summary's rows, the Activity record's lists (see Mission), the clan records and the clan
station's rows. 79 guards cover the accesses to them: an index past the table's end skips the access, and for 0 to 3
nothing changes. 68 guards are code beside the game's; 11 are a compare changed in place. The guards go in and out
together with the counts. A build for eight keeps 31 of them. The loadout screen's 19 and the mission summary's 4 make
way for their eight cards and rows (see Loadout screen and Mission); the galactic map's 24 and the name tags' 1 are
its own (see Galactic map, Squad HUD and name tags).

Some message handlers copy a list from the wire into a buffer of fixed size and trust the count. Nine guards keep
such lists inside their buffers: the handler ignores a message whose list is too long, and two senders stop at what
the receiver holds. Every build has them.

A build for eight players also lets a client's player index 4 to 7 count as valid, opens the join gate to eight, and
makes the SOS beacon count to eight. Its lobby takes eight too (see Lobby). When the player managers could not get
eight slots, its join gate stays at four.

Limits that remain:

- The hostability scores stay at four entries. Their message carries five at most, so peers 5 to 8 get no score.
- A guarded table has no entry for players 5 to 8: no clan record. In a build for four they have no map cursor and no
  row in the mission summary either.

## Lobby

Each of the game's three lobby objects (the main lobby, the one being joined, the previous one) keeps its members in a
list of four. The host drops a peer that is not in the main list, and a client sends its state only to peers in it.

The session step moves the three lists to the mod's memory, with room for eight. 31 code sites get the new distance.
In 4 places the game copies a whole lobby object; a stub copies the moved list first. Behind the move, 8 sites let the
lists fill to eight. Every build has this.

The record of the lobby a player is in holds four members, with two counts: members and entries. The game writes the
session's count to both. A cave stops the entries at four, the bound of the record's loops; the friends post reads
the members. 1 site and 1 cave, in every build.

A build for four closes the lobby at four players: Steam marks it not joinable, PlayFab makes it private.

A build for eight makes the lobby take eight. The Steam lobby is made for eight and the PlayFab lobby for twelve (the
game adds four). The lobby closes at eight. The lobby search, a friend's lobby in the friends list and the member
count the host posts for friends go to eight too. 7 sites and 1 cave do this, with the counts.

Limits that remain:

- A lobby's entry in a message to other players lists four members at most.
- A friend's full lobby without the mod shows four of eight places; Steam refuses the join.
- Joining another lobby with the squad takes only the players in host slots 0 to 3 along; slots 4 to 7 stay behind.

## Loadout screen

The loadout screen keeps four arrays by player or card: the players' records, a map from player to card, its reverse
and the cards. A build for eight allocates the screen bigger and moves the four arrays to arrays of eight at its end.
430 code sites address the new places or count to eight; 41 more go through code beside the game's. Among those are
15 guards rebuilt for eight: an index of eight or more, or none, skips the access as before.

The engine keeps one more loadout table, outside the screen: one entry for each peer, four of them. The build for
eight moves it to the mod's memory with room for eight, when it writes its count sites. The four entries go along,
and the game's pointer and its three registry entries lead to the new place. Then 11 sites let the table's searches
and its clear count to eight. When the table is not where the game keeps it, the counts stay at four.

A build for four leaves the screen and the table as the game has them and keeps the screen's 19 guards.

Limits that remain:

- Cards 5 to 8 show no 3D figure: the preview's figure slots stay at four.
- The eight cards are spread evenly at the width of four, so they probably overlap.
- The cards' width comes from the game's data, not from code the mod changes.

## Mission

A build for eight players lets the mission result take one to eight players, where the game stops at four. 1 site.

The score manager keeps the loot each player carries in two tables of four rows, one by score slot and one by player
index. The player managers' step moves both behind the score manager with eight rows each: 54 code sites get the new
places and 7 loops count to eight. That loop count is also the mission report's outcome code; 1 site keeps the code
at four.

In 6 places the game clears the rows or adds them up with code written out for four rows. The managers' step puts
code of the mod's beside each that does all eight rows at the new place. It goes in and out with the rows' move, so
the rows and their code always match.

In 3 places the game uses a player index to find a row. Code of the mod's there asks whether the managers moved and
skips an index without a row. These go in and out with the counts, in every build.

The mission result copies the loot rows by score slot into room for four rows. A guard skips the copy for slots 5 to
8; their loot still counts in the result's loot total.

The Activity record pays a pickup to every participant: the game posts the record to the backend once the peers it
waits for have answered. The record has room for four participants and three of those peers, filled from the
session's ready list with no bound:

- 3 guards stop both lists at their room, in every build.
- The local player always gets a place among the four participants.
- The mod keeps participants 5 to 8 for the record. A hook of the post lists them behind the four. The post writes a
  list of any length; HellForge's backend takes up to 16.

A build for eight gives the mission summary screen eight rows and cards. The game packs two row blocks of four rows,
four cards and four navigation entries into one allocation of 0x8A540 bytes. The build allocates 0x120C70 bytes and
puts copies with eight entries in the new part; the old places stay unused. 49 code sites address the new places or
count to eight, and 10 more go through code beside the game's. The 4 guards on the rows by player index stop at eight
instead of four. The screen exists only at a mission's end, long after the counts are written at startup. A build for
four keeps the screen and its 4 guards.

Limits that remain:

- The mission result's loot rows hold score slots 1 to 4 only.
- With five to eight players the Activity record waits for the answers of three other players. The others are paid
  without it.
- The avatar manager's eight places, which dead players' bodies may also hold, are not covered yet.
- The summary's cards 5 to 8 use the game's layout for four (a layout mode, not a count): where they appear on screen
  is not known.
- The summary's list of best entries keeps four.

## Galactic map

The map object keeps the players' cursors (four of 0x298 bytes) and two lists of four for what each one hovers over.
It sits in a system packed between others in the component block, so it cannot grow.

A build for eight puts the map's 24 guards in at startup: 20 caves and 4 compares in place keep an index of four or
more off the four cursors. When the game builds its state, before the map object exists, the build moves the three
arrays to a block of the mod's with eight entries each, within 2 GB of the component block and at the old arrays'
alignment. 140 code sites get the distance to the block, 23 count or bound to eight in place and 23 go through code
beside the game's, the guards for eight among them. A state built somewhere else gets the sites written again. The
block is zeroed with each new state, as the game zeroes its own.

When the move cannot happen (other code, no memory in reach), the map keeps four cursors with the guards. When the
guards cannot go in either, the session takes four players.

A build for four keeps four cursors and has the 24 guards in its counts (see Session lists).

Limits that remain:

- The game has sounds for three other players' cursors: the fourth to seventh other player reuse them (n mod 3).
- Where a cursor's colour comes from for players 5 to 8 is not known.
- The map's tweens share a pool of 256 nodes; eight cursors take more of it.

## Squad HUD and name tags

The in-mission HUD shows three teammate panels and four name tags over players' heads. Both sit in the HUD's second
part, packed between other HUD members: the panel widget with its three panels, and seven arrays of four tag rows.

A build for eight puts the name tags' guard in at startup: a tag index of four or more skips the row. The game builds
the panels and the tags each time a mission's HUD starts, in one init; the HUD's update reaches them only after it.
The mod hooks that init. At the first one it moves the panel widget to a block of the mod's with seven panels, and the
tag rows to a block with eight rows, both within 2 GB of the HUD and at the old address modulo 0x100. 18 code sites
get the distance to the panel's block, 21 count or reach panels in place and 1 goes through code beside the game's.
The tags take 23 sites with the distance, 9 in place and 2 caves, the guard for eight among them. The blocks stay for
the rest of the game: widgets elsewhere point into them.

When the mod loads after the game has started, or the code differs, the HUD keeps three panels and four tags; the
guard stays. A build for four keeps both, with the tags' guard in its counts.

Limits that remain:

- Seven panels make the panel's background 488 units high instead of 216 (68 a panel): whether it then covers other
  HUD parts is not known.
- Tags 5 to 8 are white: the game picks a player colour only for session slots 0 to 3.
- The new tag rows and panel are drawn in address order among their siblings, which may put them in front of other
  HUD parts.

## Engine

The engine sends a session's peers as a count and a list. The sender writes the count as a whole byte; the readers
take three bits of it, so eight peers arrive as none. Five sites in `helldivers2.exe` make the readers take four
bits. With up to seven peers the message is unchanged.

A build for eight players changes one more site there: the lobby search keeps results with up to seven members, where
the game drops four and more. It goes in on its own: when it cannot, the peer list change stays.

## State

- The galactic map, squad HUD, name tags, mission, mission summary, Activity record and clan changes are checked against the game's code and in the unit tests only.
- Tested with one player: all eight pods are entered on foot, launch, and are there again after the return; missions
  and returns work with the lists counting to eight. A mission from a new pod after a completed mission's summary
  works; the mission was ended through the game's own handler, not by flying an extraction.
- Tested with two players (two game instances on one PC, build for four): the join, both players on the ship, a
  seat in a new pod seen by the other player.
- Not tested: a build for eight players in a game, a player with index 4 to 7, a launch with two or more players, a
  lobby with more than four members.
- The launch sound for a lone player in pods 5 to 8 is hooked; the mod logs once when it answers for such a pod.

## Build

```text
scripts/build.sh [--test] [--package] [--players 4|8] [--test-first-slot <1-7>] [--hellforge <checkout>] [--zig <zig>]
```

`--players` sets how many players a session takes (default 4). The DLL and the tests are built for that count.

`--test-first-slot` is a test aid for one PC with two game instances. As host, the mod gives joining peers slots
and player indexes from that number on, so one joiner reaches the code for players 5 to 8. It needs `--players 8`,
`--package` refuses it, and the log says it is a test build.

The script builds `complex_operations.dll` beside `hellforge-mod.json`. It takes the mod SDK from a HellForge checkout
(`HELLFORGE_OPEN`, else `~/HellForge`) and Zig from `HELLFORGE_ZIG`, else `~/.local/share/zig-native/zig`.

`--package` writes `build/package/complex-operations-<version>.zip` with the four files a player installs.

`--test` also builds and runs `tests/*_test.c` as Windows programs under Wine (`WINE`, else Proton Experimental's).
The tests run the mod against a stand-in game image: no game is needed.

## Install

The mod needs a HellForge with mod SDK 17 or newer. Older ones cannot build the level data from the recipe and refuse
the mod.

Put these four files in one folder inside HellForge's mods folder:

- `hellforge-mod.json`
- `complex_operations.dll`
- `hellforge-recipe.json`
- `hellforge-recipe.literals`

HellForge builds the bay's level data from the recipe and the player's own game files before a session, into the
folder's `data/`. The recipe copies every unchanged value from the player's game. It ships only the values the mod changed (a moved
vertex's new coordinate, for example) and the mod's own new data. Without the level data the mod leaves four pods.

The recipe is written by the bay's build tools (`bay8_recipe.py hellforge`), which are not in this folder.

## Source

| File | Holds |
|---|---|
| `src/co_mod.c` | The entry point: the build check and the game-loaded event. |
| `src/co_pods.c` | The eight pods: memory, code sites, hooks. |
| `src/co_pod_sites.h` | The code sites that address the manager and the pod table. Generated; do not edit. |
| `src/co_managers.c` | The seven player managers: memory, code sites and the loot rows' caves. |
| `src/co_manager_sites.h` | Their code sites and caves. Generated; do not edit. |
| `scripts/sites/` | The reviewed site lists of the seven managers. |
| `scripts/gen_manager_sites.py` | Writes `co_manager_sites.h` from those lists and a dump of the game's code; assembles the loot rows' caves with GNU `as`. |
| `hellforge-recipe.json`, `.literals` | The bay's level, geometry and collision data as a HellForge recipe. |
| `scripts/pod_sites.tsv` | The reviewed site list of the pod manager: one row for each instruction. |
| `scripts/gen_pod_sites.py` | Writes `co_pod_sites.h` from the site list and a dump of the game's code. |
| `tests/pods_test.c` | The refusals: another build, changed code, a failed write, a failed hook. |
| `src/co_engine.c` | The engine's peer-count readers and the lobby search's member limit. |
| `src/co_session.c` | The session lists and the lobby's member lists: the extension block, code sites and stubs. |
| `src/co_session_sites.h` | Their code sites. Generated; do not edit. |
| `scripts/gen_session_sites.py` | Writes `co_session_sites.h` from the site list and a dump of the game's code. Holds the lobby's member lists' sites; assembles their stubs with GNU `as`. |
| `tests/managers_test.c` | The same refusals for the player managers, the loot rows' sites and caves among them. Runs the caves. |
| `tests/session_test.c` | The same for the session lists, and that sites and stubs lead to the new place. Runs the lobby copies' stubs. |
| `src/co_counts.c` | The session lists' counts: sites, code caves, the hooks for entries 4 to 7. The guards, the loot rows' guards and the Activity record's participants too. |
| `src/co_count_sites.h` | Their code sites and the caves' code. Generated; do not edit. |
| `scripts/sites/count_sites.tsv` | The reviewed count sites: one row for each instruction. |
| `scripts/sites/loadout_sites.py` | The loadout screen's sites and caves for eight, and the per-peer loadout table's move and bounds. |
| `scripts/sites/summary_sites.py` | The mission summary's sites and caves for eight, and its guards for four. |
| `scripts/gen_count_sites.py` | Writes `co_count_sites.h`; assembles the caves with GNU `as`. Holds the guards, the member lists' caps, the lobby's size and the loot rows' guards. |
| `tests/counts_test.c` | The counts and guards: written behind the move, taken back on a failed write, entries 4 to 7. The loadout screen in each build, and the per-peer table's move and its undo. The loot rows' guards and the mission result's slots in both managers' states, the Activity record's participants and post, and the join limit without the managers. The mission summary in each build. |
| `src/co_ui.c` | The tables a build for eight moves at run time: the galactic map's cursors, the squad HUD's panel, the name tags' rows. Guards for four, the move, its undo, the hook of the HUD's init. |
| `src/co_ui_sites.h` | Their code sites and caves. Generated; do not edit. |
| `scripts/sites/map_sites.py` | The galactic map's sites and caves for eight, and its guards for four. |
| `scripts/sites/hud_sites.py`, `tag_sites.py` | The squad HUD's and the name tags' sites and caves for eight; the tags' guard for four. |
| `scripts/gen_ui_sites.py` | Writes `co_ui_sites.h`; checks that no site shares a byte with the other steps' in a build for eight. |
| `scripts/sites/cave_asm.py` | Assembles the caves with GNU `as` for both generators. |
| `tests/ui_test.c` | The guards for four, the map's move with the state, the HUD's and tags' move at the HUD's init, refusal on changed code and rollback of a failed write. |
| `tests/engine_test.c` | The engine's sites: written, refused on changed code, taken back on a failed write. |
| `tests/harness/` | The stand-in loader and game image the tests run against. |

## How it stays safe

- Every code site is compared with the bytes it was reviewed with before anything is written. One mismatch leaves the
  game as it is.
- Rows for the new pods are added only when the level's four units carry their pod names.
- A write that fails part-way sets every site written before it back to the game's bytes.
- The per-peer loadout table moves only when the game's pointer and registry lead to the game's own place for it. Its
  bounds go back before it does.
- A build for eight takes no fifth player while the player managers have four slots, or while a table with four
  entries lacks its guards. The loot rows' code goes in and out with their move; their guards ask the managers' state
  each time they run.
- A table moves at run time only before the game's code first touches it, and only into memory within reach of every
  instruction that addresses it.
- A failed step is logged in plain words, for example "four pods as before".

## Log

The mod's lines are in the session's `game/mod-cowboybingus.complex-operations.log` and start with `hellpods:`,
`players:`, `session:` or `engine:`.

| Line | Meaning |
|---|---|
| `the pod manager and the ship's pod table have 8 slots` | The code is in place. |
| `7 component managers have 8 slots` | The player managers are in place. |
| `10 arrays of the session lists and the lobby's member lists moved` | Those arrays are in the mod's memory. |
| `the session lists count to eight` | The lists' code counts to eight; the line says how many players get in. |
| `... guards keep the lists in messages inside the game's buffers` | The message guards are in place. |
| `a message with a list of ... entries was ignored` | A peer sent a longer list than the handler holds; once a handler. |
| `player indexes 4 to 7 are valid` | A build for eight players: the lobby takes eight too. |
| `the lobby search lists lobbies with up to 7 members` | A build for eight players: the search's site is in place. |
| `the loadout screen has eight records and cards` | A build for eight players: the loadout screen's arrays moved. |
| `the per-peer loadout table moved to room for eight peers` | A build for eight players: that table is in the mod's memory. |
| `the mission summary has eight rows and cards` | A build for eight players: the summary screen's rows and cards moved. |
| `the per-peer loadout table is not where the game keeps it` | Something else moved or changed it: the counts stay at four. |
| `the score manager's loot rows are cleared and added for eight rows` | The loot rows' code moved with the managers. |
| `the score manager's loot rows by index are bounded where the managers keep them` | The loot rows' guards are in place. |
| `the session is full at four players: the player managers have four slots` | A build for eight whose managers have four slots: four players. |
| `the session is full at four players: tables of four entries are not guarded` | A build for eight without the map's or the tags' guards: four players. |
| `the galactic map has eight cursors` | A build for eight players: the map's cursors are in the mod's memory. |
| `the galactic map keeps four cursors` | The move did not happen; the reason stands before it. |
| `the squad HUD has seven teammate panels`, `eight players have name tags` | A build for eight players: the HUD's tables are in the mod's memory. |
| `the squad HUD keeps three teammate panels`, `four players have name tags` | Those moves did not happen; the reason stands before it. |
| `TEST BUILD, not for play` | Built with `--test-first-slot`. |
| `the session lists count to four as before` | The count change is off; the reason stands before it. |
| `the bridge is not the one with the eight-pod bay` | The level data is missing: four pods. |
| `... is not the reviewed one` | The game's code differs from the reviewed build: that part is off. |
