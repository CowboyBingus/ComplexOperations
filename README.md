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
| Player managers | Seven per-player component managers hold eight players instead of four; a build for eight gives the avatar manager sixteen slots (see below). |

The mod needs the bridge with the eight-pod bay: level data that places the four launchers (with the names
hellpod5 to hellpod8), four briefing cameras and the bay's lamps. Without that data it leaves a normal ship with four
pods and says so in its log.

Every player in the session needs the mod.

## Player managers

The game keeps one record for each player in seven managers: player, score, customization, kill streak, ship
inventory, ship customization and ship modules. Each has four slots. A build for eight builds all seven in the mod's
memory with eight slots and changes the 1,115 code sites that address them, the score manager's loot rows among them.
6 caves do the loot rows' code that the game wrote out for four rows (see Mission).

A build for eight also moves the avatar manager from eight slots to sixteen. Players' bodies hold a slot too, so
eight players and their bodies need more than eight. Its 2,700 code sites follow the same rules: 2,675 array offsets,
20 distances from the component block, 4 capacity checks and 1 constant that folds a slot's start with an array.

A build for four keeps all eight managers where the game has them. It joins four players, and HellForge and other mods
read these managers at the game's offsets. A HellForge that finds the moved offsets through the game's code works with
both builds.

This alone lets no fifth player join: the session's own lists still hold four. It is the base the later parts need.

## Stack arrays

Some of the game's functions copy every avatar, or every player, into an array on the stack with room for four or
eight. Four players already make five or more avatars for a moment: a body stays in the manager while its player
respawns. The mod's code goes in with the counts (see Session lists) and works for any number of avatars.

| Function | Fix | Builds |
|---|---|---|
| Cover and hellpod culls: the candidate farthest from all avatars goes | Code of the mod's sums the distances from the live positions | Both |
| Reinforcement anchor: the best-placed avatar | A running best instead of a score per avatar | Both |
| Encounter stage: the enemy calls spread over the avatars | Code of the mod's shuffles and calls as the game does | Both |
| HUD sample counter: containers carried per player slot | Room for sixteen slots; replaces the guard for four | Both |
| Lobby live upload: positions of up to four players | Players past four are left out | Both |
| Voice lines by the nearest Helldiver | Only avatars 1 to 8 speak | Eight only |
| Spawn exclusion around avatars (three lists of eight) | Avatars past eight are left out | Eight only |
| Director and position lists from the frame allocator | Room for sixteen | Eight only |
| Dormancy pass: far entities sleep by the nearest player (four positions, the stack cookie behind) | The positions of eight players in the mod's memory | Eight only |

The fixes for rooms of eight go in a build for eight only, the one whose avatar manager holds more than eight. So does
the dormancy pass's: it counts players, and only a build for eight has a fifth. 19 code sites change in a build for
four, 35 in a build for eight. For four avatars or fewer the culls and the encounter give
the game's own result. The anchor starts the fourth score at 0, which the game leaves unset.

Limits that remain:

- The director keeps per-player pacing for eight avatars; avatars 9 to 16 get none.
- The encounter shuffle and the anchor are checked against the game's code in the unit tests only.

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
station's rows. 77 guards cover the accesses to them: an index past the table's end skips the access, and for 0 to 3
nothing changes. 66 guards are code beside the game's; 11 are a compare changed in place. The guards go in and out
together with the counts. A build for eight keeps 29 of them. The loadout screen's 19 and the mission summary's 4 make
way for their eight cards and rows (see Loadout screen and Mission); the galactic map's 24 and the name tags' 1 are
its own (see Galactic map, Squad HUD and name tags).

Some message handlers copy a list from the wire into a buffer of fixed size and trust the count. Nine guards keep
such lists inside their buffers: the handler ignores a message whose list is too long, and two senders stop at what
the receiver holds. A build for four has all nine. A build for eight has seven: the hostability scores' two make way
for the scores of eight peers.

A build for eight sends the hostability scores of all eight peers. The host sends them every 10 seconds, and a new
host takes them over after a migration. The host leaves its own score out, so a message holds seven at most. The count
on the wire already carries seven, so the network config stays as it is. The handler takes eight. 3 sites and 3 caves
do this, with the counts. A build for four keeps the game's four scores.

A build for eight players also lets a client's player index 4 to 7 count as valid, opens the join gate to eight, and
makes the SOS beacon count to eight. Its lobby takes eight too (see Lobby). When the player managers could not get
eight slots, its join gate stays at four.

Limits that remain:

- A peer without the mod must not be in a session of seven or more players. The hostability scores would be more than
  its handler holds.
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

- Where HellForge's Steam broker runs, the host needs a HellForge whose broker lets a lobby have eight members: the nightly
  of 2026-11-11 or later (0.4.0-beta allows four). With an older one the host has no Steam lobby and nobody can
  join; `steam-broker.log` then says `refused ISteamMatchmaking::CreateLobby: cMaxMembers out of range`.
- A lobby's entry in a message to other players lists four members at most.
- A friend's full lobby without the mod shows four of eight places; Steam refuses the join.
- Joining another lobby with the squad takes only the players in host slots 0 to 3 along; slots 4 to 7 stay behind.
- A public lobby of a build for eight shows in HellForge's lobby browser, on the map and in Quick Play only with a
  HellForge that counts a lobby's squad from its places (pull request 351, in dev). With an older one, players join
  by a friend's lobby or an invite. Seen in a game with two test installs: a stranger's map scan finds the lobby.

## Loadout screen

The loadout screen keeps four arrays by player or card: the players' records, a map from player to card, its reverse
and the cards. A build for eight allocates the screen bigger and moves the four arrays to arrays of eight at its end.
430 code sites address the new places or count to eight; 56 more go through code beside the game's. Among those are
15 guards rebuilt for eight: an index of eight or more, or none, skips the access as before.

The eight cards stand in one row at half the game's size, 232 reference units apart, so they do not overlap.

Each of the 12 places that store a card's height (780 reference units) reads it from the mod's memory instead, through
code beside the game's: the card's constructor, the screen's and the card's resizes, and the expand and collapse
animations. The mod keeps the game's 780 there. A callback runs in front of each update of the screen; it does not
move the cards yet.

Each card's 3D figure has a record in the game's placement system, which holds eight. The weapon pictures on the screen
use records 4 to 7, so cards 5 to 8 take records 8 to 11. The build for eight moves the eight records to a block of
twelve in the mod's memory and gives 228 code fields the distance to it. The loops over the records, the player
lookup and the figure's place in the shared image count to twelve, through 13 changed sites and 10 caves. Two of
the caves find the owner of records 8 to 11 for the figure's weathering and wound decals. The figures
draw as tiles of a 4 x 2 image at half width. The figure's update sizes the image again whenever the screen is not
1920 x 1080 pixels; two of the sites keep that size at half width too.

The figures move only when no record is in use. When they cannot move, cards 5 to 8 show no figure, as before.

The engine keeps one more loadout table, outside the screen: one entry for each peer, four of them. The build for
eight moves it to the mod's memory with room for eight, when it writes its count sites. The four entries go along,
and the game's pointer and its three registry entries lead to the new place. Then 11 sites let the table's searches
and its clear count to eight. When the table is not where the game keeps it, the counts stay at four.

Each card also adds itself to a list of the game's type registry, which has room for four; two other lists lie behind
it. The build for eight moves that list to the mod's memory with room for eight, before the cards' sites go in: 7 code
sites read it there. It moves only while it is empty and goes back with the counts while it is empty. When it cannot
move, the counts stay at four.

A build for four leaves the screen and the table as the game has them and keeps the screen's 19 guards.

Limits that remain:

- The text on the cards is half size too: small but legible at 1280 x 720.
- The figures of players 5 to 8 are each seen in a game, on host and joiner, in sessions of two players. More than
  two cards at once are not yet seen. Those runs were at 1280 x 720, before the fix of the figure's run-time width:
  the figures there were drawn twice as wide as they should be.
- The figures of players 6 to 8 showed the frost their Helldiver wore after arriving. Wound decals on the figures of
  players 5 to 8 are not yet seen in a game.

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
- The mod keeps the other players 4 to 7 the record waits for, in both of the game's record functions. 6 sites in the
  game's three answer functions take an answering or leaving player off that list too, and keep the record waiting
  while one is left. The time-out and the drop work as for four; the 3 drops free the mod's lists.
- The further participants and answers need the post's hook. Without it, or with one of their 11 sites changed, they
  stay out: the record keeps four, and the rest of the counts and guards still goes in.

A build for eight gives the mission summary screen eight rows and cards. The game packs two row blocks of four rows,
four cards and four navigation entries into one allocation of 0x8A540 bytes. The build allocates 0x120C70 bytes and
puts copies with eight entries in the new part; the old places stay unused. 49 code sites address the new places or
count to eight, and 10 more go through code beside the game's. The 4 guards on the rows by player index stop at eight
instead of four. The screen exists only at a mission's end, long after the counts are written at startup. A build for
four keeps the screen and its 4 guards.

Limits that remain:

- The mission result's loot rows hold score slots 1 to 4 only.
- A record of the game's second record function (0x12E6690) lists the four participants its caller hands it. No caller
  of that function was found in the game's code.
- The summary's cards 5 to 8 use the game's layout for four (a layout mode, not a count): where they appear on screen
  is not known.
- The summary's list of best entries keeps four.

## Difficulty

A build for eight carries the HiveMind's difficulty multipliers on past four players. The game keeps each multiplier
for one to four players and reads it by player count with no bound, so a fifth player reads past them.

- 26 caves replace those reads: spawners, patrols, encounters, guard forces and travelers.
- One to four players get the game's own value.
- Five to eight continue the step from three to four players: v4 + (n - 4) x (v4 - v3), at least 0.1.
- Spawn caps are not changed here.

## Complex operations

One level-11 operation in four is a complex operation. HellForge owns difficulty 11; the mod only adds to it.

| Part | A complex operation |
|---|---|
| Map | 16 km across (a world of 16384 m), where HellForge's level 11 map is 2 km. |
| Missions | Three missions on the one map. |
| Objectives | Up to 30: every mission's primaries, then major and optional ones in the room left. |
| Arcs | The objectives are dealt in arcs: each mission's primaries stand together in their own part of the map. |
| Tactical map | 16 objective markers, as in the game. A marker goes to the next objective when one is done. |

- Which operations are complex comes from the operation's seed and planet, so every player sees the same ones.
- Every player needs the mod. The host launches a complex mission only when every player's PC can make one;
  otherwise that mission is a normal level-11 mission.
- A PC that cannot make one plays normal missions. An example is a PC with less than 28 GB of memory free to commit
  when the game starts. Such a player is denied only when hot-joining a running complex mission.
- The mod's log says why the part is off: a line that starts with `complex operations are off:`.
- A normal level-11 mission with the mod loaded is the mission it is without the mod at the same generation seed.
  The mod clears the lowest bit of every normal level-11 mission's seed, so an odd seed generates as the even one
  below it.
- Two engine numbers differ from a game without the mod, with no effect on play:
  - Entity ids made after the ship are 4 higher, one per extra hellpod of the eight-pod bay.
  - Reused unit handles come in another order. The bay is the likely cause; nothing looks these up except by equality.

The game sizes some engine room once, as it starts, before any mission is known. So when the part is on, every level
gets the room a 16 km world needs:

| Room | Game or HellForge | With the mod |
|---|---|---|
| Physics world (broadphase) | 3072 m | 17408 m: the map and 512 m around it |
| Havok world's bodies | 0x40000 | 0x100000 |
| Static actors | 0x40000 | 0x100000 |
| Connectors | 0x40000 | 0x400000 |
| Navigation grid | 8 x 8 tiles, extents 1024 | 32 x 32 tiles over four times the bounds, extents 4096 (see below) |

Measured on one city mission at difficulty 11, 8 runs without the mod and 4 with it:

- Memory committed at the start of play: about 0.6 GB more (9.33 to 9.40 GB against 8.72 to 8.79 GB).
- Time to the start of play: the same, about 45 s.
- Time until the navigation grid has no tiles left to build: about 1 s longer on average (99.0 s against 97.9 s;
  98.1 to 99.7 s against 97.3 to 98.9 s).
- The navigation build scheduler: 5 to 6 µs a call on average, at most 43 µs, against 0.5 µs.

### On the war table

A banner beside the native operation panel, and beside the briefing's mission panel, says what a level-11 operation
or mission starts as. It is drawn the way Know Your Constellation draws: the engine's Gui through the game's Lua, in
the game's body font and its panel inks.

| Banner | When |
|---|---|
| `COMPLEX OPERATION`, `3 MISSIONS ON ONE 16 KM MAP` | The launch makes a complex mission of it: yellow edge and title. |
| `STANDARD OPERATION`, `EACH MISSION ON ITS OWN MAP` | The part is on and the roll says normal. |
| `COMPLEX OPERATIONS OFF` | The part is off: `OFF ON THE HOST`, `OFF ON THIS PC` or `NOT THIS GAME BUILD`. |
| `WAITING FOR HOST` | A client that has no answer from the host yet. |
| `STARTS AS STANDARD` | The briefing, as host: a squad member's PC is not known to be able, so the launch drops the mark. |

- The state comes from the functions the launch decides with, on the same inputs. A client whose answer from the host
  is on takes the operation's roll from the board, so a table filled before that answer still shows the right state.
- A client's briefing cannot tell whether the host's launch will drop the mark: it shows the operation's roll, and the
  host's briefing shows `STARTS AS STANDARD`.
- Until this PC's check of the part settles, no banner shows.
- Other levels and other screens get no banner.
- The banner goes right of the panel, above its top line, or one column further right when that does not fit.
  Know Your Constellation's box never reaches either place.

### On every level

These differ from a game without the mod on every level, the ship and the war-table previews. Each adds room, or
acts only where the game would read or write out of bounds. What turns each group on differs.

In every build, with the part on or off:

- Session lists and managers move to blocks with eight entries (see Session lists); guards keep indexes 4 and up off
  the game's four-entry lists.
- The hellpod manager and the ship's pod table move; their capacity goes from 4 to 8.
- HellForge's generation block grows from 21.7 to 40.4 MB: 65024 locations, a minimap 8 times larger, important and
  linked lists of 1024.
- Five readers of the peer count in the exe read 4 bits, not 3.
- Every level-11 war-table entry's seed has bit 0 cleared for a normal operation and set for a complex one.
- Address space: 1 MB near the exe and pages near both modules.

On a PC that passes the early check (the room below, at least 28 GB of memory free to commit at the start):

- The engine room in the table above, and the 32 x 32 navigation grid with the trim's 0x4000-byte block near the exe.
- Entity records have 16384 slots.
- Stamp index cells take 0x5000 bytes, not 0x548; the built index is the same.

With the part on:

- Each structured map's Voronoi step takes and clears 0x168CC68 bytes of weld pool, not 0x174C68.
- The status registry has a list of 24576 and a hash of 65536. Past 4096 units the mod skips where the game crashes.
- The render world is 2 times larger and render pools 0 and 1 are 4 times larger. Sample groups take 0x81008 bytes,
  route-cell lists are sized for cells plus locations, and the upload allocator links at the tail.
- The layer heightfield copy, the object walk's 0 handle, the pose-sync drop, the owner-NULL stubs and about 16 more
  guards act only where the game would fault. So do two more: the Hive Lord graph's labelling equals the game's up to
  its 255 regions, and a normal level's first batch keeps the scatter multiplier only for a record map that is not
  empty, where the game's probe still finds every key.

Each of these equals the game's up to the game's own limit, and logs a line when a normal level passes that limit:

- The route line width at game+0xF53116 is the game's on a normal level. It is scaled only on a complex level.
- The AI task guard leaves a guard point's task out past 1792 of 2048 tasks: `AI guard points left out`.
- The stamp instance buffer is 8 times larger: `op 0x2B job past the game's 3072 instances`.
- Hive Lord labelling: `Hive Lord graph past 255 regions`.
- Scatter owner bitsets are 512 wide: `owner cell sets outside the game's bitset box`.

### After a complex level

On the ship, the network timeouts drop from 60 to 10 s, and a 4096 x 4096 one-mip texture stays allocated.

What a complex level changes is put back before the ship or the next level:

- Kill heights, water and the sea plane are restored by the first level whose table holds their type. Until then
  they stay on a list of 1024 a kind. A full list lowers nothing more and logs `to put back is full`.
- Early packages are released at the next level's first main-thread step.
- Every level init and the ship's set-up end the near tiles' state: the one-mip slot, the replay flag and the crater
  hold, and on the ship the tiles. The one-mip spawn checks its level.
- Town filters are widened on complex levels only and put back at the next level's first step, while the type still
  holds the same rule and stamp lists. A full list of 8192 widens nothing more.
- Obstacles in the navigation grid are filed under the ship's grid (Navigation grid below).

Assumed, not proven:

- The game's own kill heights and water levels are never -480 m and -501 m, the mod's marks. All 257 types read
  hold -100 and -18.5.
- A town rule rebuilt at the same address with the same lists would get its old filter back.
- The generation state is process-wide: a normal generation overlapping a complex preview's level init is not proven
  impossible.

### On a complex mission

The living world runs every complex mission. Its director patrols are off; instead the host keeps squads as records
that walk the map's routes, and brings a squad online as the game's own patrol group near the players, its losses
kept. Each group of players near each other gets 1.5 times the game's patrol budget, 72 solo and 96 in co-op, and the
map holds at most 190 such enemies. The game's list of 256 navmesh boundary requests a frame limits the host to about
232 walking enemies in all.

On a complex mission the mod's density guards apply:

- The game's own enemies keep their room: the 70-enemy room, the breach release and the despawner count them
  without the living world's squads.
- A full squad-slot table and more than 64 kinds of enemy in the spawn budget are refused instead of crashing, and so
  is a nearly full ray queue. The AI job gate for new spawns rises from 448 to 1024.
- Mission locations' pickups, markers and samples spawn when a player comes within 600 m.

The mod also keeps a complex level's destruction records, render version tables, tree extents and navmesh boundary
requests within what the game holds. On a normal mission and on the ship none of these density and complex-level
guards acts, and the difficulty rows stay the game's.

### Navigation grid

The navigation grid of every world has 32 x 32 tiles, room for a complex level's 16 km. A normal level-11 mission
and the ship build stock's 8 x 8 tiles of it:

- Two stubs in the exe act only on an armed world. 0x7ACDD0, the point-to-tile step, runs the engine's own
  instructions on stock's bounds and 8 x 8 tiles, then adds 12 on each axis. 0x7B744C, behind the build-priority
  fill, gives tiles 12 to 19 stock's 64 priorities and every other tile a priority that never gets built.
- The world's bounds stay stock's, so the closest-navmesh clamp and every tile box are stock's.
- Each priority stays at its tile's place in the array, because the scheduler and the raisers index the array by tile.
  So tiles of equal priority may build in another order than stock's. Their content is the same.
- A complex level disarms its world and builds all 1024 tiles.
- The living world's check of the engine's code reads the trim's jump at 0x7ACDD0 as the engine's bytes, but only while
  the jump leads into the trim's own block and that block holds the reviewed code. Any other form still turns off its
  navmesh boosts and rebuilds.
- If the engine's bytes differ from the reviewed ones, neither stub goes in. Each normal level then builds all 1024
  tiles and logs that its world is not armed, which fails the stock-tiles check. A nav world of id 4 or more does the
  same: the stubs hold four worlds.
- The stubs, the armed worlds and the priority table take one 0x4000-byte block near the exe: address space only.

Open items:

- The 960 outer tiles keep the flag the engine sets for every tile at the build. So the scheduler finds work each
  frame and sorts 1024 entries a frame for the whole level. Clearing that flag is left out: no stock code path holds
  a built tile without its navmesh, and not every reader of the tile record was traced.
- An obstacle filed while the world was disarmed keeps the tiles it was filed in. This can happen to obstacles that
  live on from a complex level into the next level. The ship's own obstacles are not affected: the ship's grid is set
  at the start of its set-up, before they are filed, and HellForge's ship step runs there too.
- When the log has `early ship retile skipped:` or `early ship retile off:`, the grid is set at the end of the
  ship's set-up instead, and the ship's own obstacles keep the grid they were filed under.
- While the eight-pod bay is loaded, it changes the ship's navmesh in its four centre tiles, so their face counts
  differ from stock's. Without the bay's level data they equal stock's.
- That also corrects HellForge's ship navigation after a 256 m mission, where HellForge alone files the ship's
  obstacles under the 256 m grid. HellForge's own fix is pull request 378.

### Cancelled preview

With the part on, two guards act on every level, only where the game would go wrong:

- **Cancelled preview.** A mission that starts while any level generation is still running waits for it to end: a
  complex or a normal war-table preview, or a refresh run. A complex preview stops at its next tile and leaves its plan
  out of the mission. The wait has no limit, so a mission start can wait out the rest of a refresh run. It logs a line
  at 2 s and every 10 s after.
- **Runners.** Stamp or terrain runners that do not return within 30 s are dropped for the session, with the line
  `did not return within 30 s`; the dispatcher's thread builds the rest.

### Requirements

The part needs a HellForge with pull requests 362, 364 and 366: dev after 2026-11-11, or the first release after it.
With an older HellForge the part is off and the rest of the mod works.

## Extraction

A build for eight gives the Pelican eight passenger seats where the game has six. 15 caves add two seats, 8 and 9 in
the game's seat table. They attach to two nodes of the Pelican's unit, `attach_passenger_left_4` and
`attach_passenger_right_4`. Players reach them as they reach the other passenger seats and sit as in the third seat on
each side.

The game's Pelican has no such nodes; the mod's recipe adds them to the Pelican's unit (`shuttle_gunship`) in each of
the five archives that hold it. Each new node stands to the second seat as the third seat's node stands to the first:
0.28 m forward of it, at the front edge of the second seat. Further along the rows there is no room: the bulkhead is
ahead of the first seats, and the way in from the ramp is behind the second.

The seats open only when the mod finds both nodes on a Pelican's unit. The first live gunship decides, until the next
session context; other units with the Pelican's seat layout are skipped. Without the nodes the Pelican keeps six
passenger seats and the log says why.

Each attach to seat 8 or 9 checks that vehicle's own unit. A unit without the node, or a game with the seats off,
seats that passenger at the third seat's node of that side.

A build for eight also loads the boosters of all eight players. The game's booster update collects four players'
boosters; 1 cave runs it over eight. With four players or fewer it does what the game does.

Limits that remain:

- The HUD's booster strip shows four boosters.
- A peer without the mod finds no node for a player in seat 8 or 9 and attaches them to node -1; every player needs
  the mod.

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

- The galactic map, squad HUD, name tags, mission, mission summary, Activity record, clan, avatar manager, stack
  array and loadout cards' registry list changes are checked against the game's code and in the unit tests only.
- Tested with one player: all eight pods are entered on foot, launch, and are there again after the return; missions
  and returns work with the lists counting to eight. A mission from a new pod after a completed mission's summary
  works; the mission was ended through the game's own handler, not by flying an extraction.
- Tested with two players (two game instances on one PC, build for four): the join, both players on the ship, a
  seat in a new pod seen by the other player.
- Not tested: a build for eight players in a game, a player with index 4 to 7, a launch with two or more players, a
  lobby with more than four members.
- Complex operations are checked against the game's code and in the unit tests only. None was run in a game with
  this mod.
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
the mod. Complex operations need a newer one still (see Complex operations).

Put these four files in one folder inside HellForge's mods folder:

- `hellforge-mod.json`
- `complex_operations.dll`
- `hellforge-recipe.json`
- `hellforge-recipe.literals`

HellForge builds the bay's level data and the Pelican's unit from the recipe and the player's own game files before a
session, into the folder's `data/`. The recipe copies every unchanged value from the player's game. It ships only the values the mod changed (a moved
vertex's new coordinate, for example) and the mod's own new data. Without the level data the mod leaves four pods.

The recipe is written by the bay's build tools (`bay8_recipe.py hellforge`, then `gunship_seats.py recipe` for the
Pelican), which are not in this folder.

## Source

| File | Holds |
|---|---|
| `src/co_mod.c` | The entry point: the build check and the game-loaded event. |
| `src/co_pods.c` | The eight pods: memory, code sites, hooks. |
| `src/co_pod_sites.h` | The code sites that address the manager and the pod table. Generated; do not edit. |
| `src/co_managers.c` | The seven player managers and, in a build for eight, the avatar manager: memory, code sites and the loot rows' caves. |
| `src/co_manager_sites.h` | Their code sites and caves. Generated; do not edit. |
| `scripts/sites/` | The reviewed site lists of the seven managers and the avatar manager. |
| `scripts/gen_manager_sites.py` | Writes `co_manager_sites.h` from those lists and a dump of the game's code; assembles the loot rows' caves with GNU `as`. |
| `hellforge-recipe.json`, `.literals` | The bay's level, geometry and collision data and the Pelican's seat nodes as a HellForge recipe. |
| `scripts/pod_sites.tsv` | The reviewed site list of the pod manager: one row for each instruction. |
| `scripts/gen_pod_sites.py` | Writes `co_pod_sites.h` from the site list and a dump of the game's code. |
| `tests/pods_test.c` | The refusals: another build, changed code, a failed write, a failed hook. |
| `src/co_engine.c` | The engine's peer-count readers and the lobby search's member limit. |
| `src/co_session.c` | The session lists and the lobby's member lists: the extension block, code sites and stubs. |
| `src/co_session_sites.h` | Their code sites. Generated; do not edit. |
| `scripts/gen_session_sites.py` | Writes `co_session_sites.h` from the site list and a dump of the game's code. Holds the lobby's member lists' sites; assembles their stubs with GNU `as`. |
| `tests/managers_test.c` | The same refusals for the player managers, the loot rows' sites and caves among them. Runs the caves. |
| `tests/avatars_test.c` | The avatar manager in each build: its sites with a state and a second one, refusal on changed code and rollback of a failed write. |
| `src/co_avatars.c` | The stack fixes' code: the culls' distance sums and the encounter's shuffle and calls. |
| `scripts/sites/stack_sites.py` | The stack fixes' sites and caves for each build, and the game's code their test runs. |
| `tests/stack_test.c` | The stack fixes in each build, refusal and rollback. Runs the game's code beside the patched code on the same avatars and compares the results. |
| `tests/co_stack_code.h` | The game's code for that test. Generated; do not edit. |
| `tests/session_test.c` | The same for the session lists, and that sites and stubs lead to the new place. Runs the lobby copies' stubs. |
| `src/co_counts.c` | The session lists' counts: sites, code caves, the hooks for entries 4 to 7. The guards, the loot rows' guards, the Activity record's participants and the hostability scores too. |
| `src/co_count_sites.h` | Their code sites and the caves' code. Generated; do not edit. |
| `scripts/sites/count_sites.tsv` | The reviewed count sites: one row for each instruction. |
| `scripts/sites/loadout_sites.py` | The loadout screen's sites and caves for eight, and the per-peer loadout table's move and bounds. |
| `scripts/sites/figure_sites.py` | The figure records' move: its 228 fields, sites and caves, checked against the dump. |
| `scripts/sites/summary_sites.py` | The mission summary's sites and caves for eight, and its guards for four. |
| `scripts/gen_count_sites.py` | Writes `co_count_sites.h` and `tests/co_stack_code.h`; assembles the caves with GNU `as`. Holds the guards, the member lists' caps, the lobby's size and the loot rows' guards. |
| `scripts/sites/difficulty_sites.py` | The HiveMind's difficulty sites and their caves for eight. |
| `scripts/sites/extract_sites.py` | The Pelican's seat caves and seat block, and the booster cave, for eight. |
| `src/co_extract.c` | The check of the Pelican's seat nodes, and the booster update over eight. |
| `tests/counts_test.c` | The counts and guards: written behind the move, taken back on a failed write, entries 4 to 7. The loadout screen in each build, and the per-peer table's move and its undo. The loot rows' guards and the mission result's slots in both managers' states, the Activity record's participants, answers and post, and the join limit without the managers. The mission summary in each build. The card height's 12 caves, run with the slot at two heights. |
| `tests/figures_test.c` | The figure records' move and way back, the game's code on the moved records, the caves for records 8 to 11, the refusals. |
| `tests/cards_test.c` | The loadout cards' layout: eight cards in two rows at nine screen sizes against the predicted pixels, the HUD scale's limit and the game's four. |
| `tests/extract_test.c` | The difficulty caves for one to eight players, the seat block, the node checks, the attach node caves and the boosters. Refusal by part and rollback of a failed write. |
| `src/co_ui.c` | The tables a build for eight moves at run time: the galactic map's cursors, the squad HUD's panel, the name tags' rows. Guards for four, the move, its undo, the hook of the HUD's init. |
| `src/co_ui_sites.h` | Their code sites and caves. Generated; do not edit. |
| `scripts/sites/map_sites.py` | The galactic map's sites and caves for eight, and its guards for four. |
| `scripts/sites/hud_sites.py`, `tag_sites.py` | The squad HUD's and the name tags' sites and caves for eight; the tags' guard for four. |
| `scripts/gen_ui_sites.py` | Writes `co_ui_sites.h`; checks that no site shares a byte with the other steps' in a build for eight. |
| `scripts/sites/cave_asm.py` | Assembles the caves with GNU `as` for both generators. |
| `src/co_registry.c` | The type registry's list of loadout cards: its move to room for eight and back. |
| `src/co_registry_sites.h` | Its code sites. Generated; do not edit. |
| `scripts/sites/registry_sites.py` | The reviewed loads that reach that list. |
| `scripts/gen_registry_sites.py` | Writes `co_registry_sites.h`; checks each load against a dump and the other steps' sites. |
| `tests/registry_test.c` | The list's move in each build, run through the game's loads; refusal on changed code or a card in the list, rollback of a failed write. |
| `tests/ui_test.c` | The guards for four, the map's move with the state, the HUD's and tags' move at the HUD's init, refusal on changed code and rollback of a failed write. |
| `tests/engine_test.c` | The engine's sites: written, refused on changed code, taken back on a failed write. |
| `src/co_roll.c` | Whether an operation is a complex operation: an integer hash of its seed and planet. |
| `tests/roll_test.c` | The roll's fixed answers and its share of operations at 5, 25 and 50 %. |
| `src/co_tag.c` | The war-table banner: its state, the war table's readers, its place and its plan. |
| `src/co_lua.c`, `co_gui.c` | The game's Lua and the engine's Gui from C, as Know Your Constellation's; a hook on `lua_pcall`. |
| `tests/tag_test.c` | The banner's states, readers and launch prediction; its drawing through a real `lua51.dll` (`CO_LUA51_PATH`). |
| `src/xl/` | The extra large world of a complex operation. `co_complex_start` (`src/co_capacity.c`) installs it once the part is on. |
| `src/xl/co_spawners.c`, `co_clusters.c`, `co_city.c`, `co_pocket.c`, `co_status.c`, `co_locate.c` | A complex level's lists and searches, each beside HellForge's own step: the game's or HellForge's value on any other level. |
| `src/xl/co_objectives.c`, `co_markers.c`, `co_text.c` | The objective list within 30 places, the check of the map's 16 markers, the grouped HUD rows' texts. |
| `src/xl/proto/` | Its files as copied from a HellForge loader; `README.md` there says from where. Parts of `co_xl.c`. |
| `src/xl/co_shim.h`, `co_shim.c` | The loader's helpers those files call, over the SDK and the mod's own parts. |
| `src/xl/co_xl_standin.h` | The names of the loader's level 11 install those files read, over the mod's own parts. |
| `src/xl/co_nav.c`, `co_navtrim.c` | The navigation grid of 32 x 32 tiles, and the two stubs that give a normal level or the ship stock's 8 x 8 tiles of it. |
| `tests/navtrim_test.c` | The stubs against the engine's own bytes: the point-to-tile step over 25643 points per tile size, registers kept, the priority table, arming, refusals and rollback. |
| `tests/shim_test.c` | The player list read at the game's offsets, and at the moved ones in a build for eight. |
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
- The difficulty, seat and booster caves are checked part by part: a changed byte turns off that part alone.
- A failed step is logged in plain words, for example "four pods as before".

## Log

The mod's lines are in the session's `game/mod-cowboybingus.complex-operations.log` and start with `hellpods:`,
`players:`, `session:`, `engine:`, `extraction:`, `navigation:` or `war-table tag:`.

| Line | Meaning |
|---|---|
| `the pod manager and the ship's pod table have 8 slots` | The code is in place. |
| `7 component managers have 8 slots` | A build for eight players: the player managers are in place. |
| `a build for four keeps the game's player managers` | A build for four: the managers stay where the game has them. |
| `the avatar manager has 16 slots for players and their bodies` | A build for eight players: the avatar manager moved with them. |
| `... code sites keep the game's stack arrays for avatars and players inside their room` | The stack fixes are in place. |
| `10 arrays of the session lists and the lobby's member lists moved` | Those arrays are in the mod's memory. |
| `the session lists count to eight` | The lists' code counts to eight; the line says how many players get in. |
| `... guards keep the lists in messages inside the game's buffers` | The message guards are in place. |
| `a message with a list of ... entries was ignored` | A peer sent a longer list than the handler holds; once a handler. |
| `the hostability scores of eight peers go in their message` | A build for eight players: the scores' sites and caves are in place. |
| `player indexes 4 to 7 are valid` | A build for eight players: the lobby takes eight too. |
| `the lobby search lists lobbies with up to 7 members` | A build for eight players: the search's site is in place. |
| `the loadout screen has eight records and cards` | A build for eight players: the loadout screen's arrays moved. |
| `every loadout card has a 3D figure` | A build for eight players: the figure records moved to room for twelve. |
| `... the loadout screen shows four figures` | The figures stay out; the reason stands before it. Cards 5 to 8 show no figure. |
| `the per-peer loadout table moved to room for eight peers` | A build for eight players: that table is in the mod's memory. |
| `the type registry's list of loadout cards has room for eight` | A build for eight players: the cards' registry list is in the mod's memory. |
| `the type registry's list of loadout cards is not empty` | A card was there before the move: the counts stay at four. |
| `the mission summary has eight rows and cards` | A build for eight players: the summary screen's rows and cards moved. |
| `the per-peer loadout table is not where the game keeps it` | Something else moved or changed it: the counts stay at four. |
| `the score manager's loot rows are cleared and added for eight rows` | The loot rows' code moved with the managers. |
| `the score manager's loot rows by index are bounded where the managers keep them` | The loot rows' guards are in place. |
| `an Activity record lists every participant and waits for each one's answer` | The Activity record's further participants and answers are in place. |
| `Activity records list and wait for four players` | The post's hook or one of those sites is missing; the reason stands before it. The rest is in. |
| `the session is full at four players: the player managers have four slots` | A build for eight whose managers have four slots: four players. |
| `the session is full at four players: tables of four entries are not guarded` | A build for eight without the map's or the tags' guards: four players. |
| `the galactic map has eight cursors` | A build for eight players: the map's cursors are in the mod's memory. |
| `the galactic map keeps four cursors` | The move did not happen; the reason stands before it. |
| `the squad HUD has seven teammate panels`, `eight players have name tags` | A build for eight players: the HUD's tables are in the mod's memory. |
| `the squad HUD keeps three teammate panels`, `four players have name tags` | Those moves did not happen; the reason stands before it. |
| `the HiveMind's difficulty multipliers go on past four players` | A build for eight players: the difficulty caves are in place. |
| `the Pelican's seats 8 and 9 are in place` | A build for eight players: the seat caves are in; the seats open once the Pelican has their nodes. |
| `the Pelican's unit has the nodes attach_passenger_left_4 and right_4` | Seats 8 and 9 are open until the next session context. |
| `the Pelican's unit has no attach_passenger_left_4 and right_4 nodes` | The level data is missing: six passenger seats. |
| `a unit other than shuttle_gunship has seat type 40` | That unit is skipped; the next gunship decides. |
| `seat 8 has no node of its own here` (or seat 9) | That passenger sits at the third seat's node of that side. Logged once a session context. |
| `the engine's unit functions are not the reviewed ones` | The node check cannot run: six passenger seats. |
| `the boosters of all eight players are loaded` | A build for eight players: the booster cave is in place. |
| `the HiveMind's multipliers stop at four players`, `the Pelican keeps six passenger seats`, `boosters of players 5 to 8 are not loaded` | That part is off and the rest goes in; the reason stands before it. |
| `navigation: nav world N is not armed for stock's 8 x 8 tiles (...)` | That normal level builds all 32 x 32 tiles; the reason stands in brackets. |
| `navigation: ...; normal levels build the whole 32 x 32 grid` | The engine's bytes are not the reviewed ones: the two tile stubs are off. |
| `difficulty: extra large maps: destroy of a live unit held until the world's destroy list had room (N so far)` | On a complex mission the game's destroy list was full; the destroy waits a frame. Logged at counts 1, 2, 4 and so on. |
| `war-table tag: map, operation N: complex, banner x ...` | The banner for that state; also `briefing, operation N`. |
| `war-table tag: map, operation N: not level 11, no banner` | That operation is another level. |
| `war-table tag: no banner until this PC's check of the complex part has settled` | Once, while the part's check runs. |
| `war-table tag: removed (the screen changed; N unbalanced frames)` | The banner's GUI is gone. N above 0 means the Lua stack was not left as found. |
| `war-table tag: off: ...` | The banner is off: the game's Lua or the war table's code is not the reviewed one. |
| `TEST BUILD, not for play` | Built with `--test-first-slot`. |
| `the session lists count to four as before` | The count change is off; the reason stands before it. |
| `the bridge is not the one with the eight-pod bay` | The level data is missing: four pods. |
| `... is not the reviewed one` | The game's code differs from the reviewed build: that part is off. |
