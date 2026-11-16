# Alien inspection and turn research (16 November 2026)

Read from the installed game's `DATA/GLOBAL/ANIMATION.PAK` and the
`DLC/SalvageMode1/WORLD/COMMANDS.PAK` through the installed OpenCAGE CathodeLib.
This is an asset/native-code finding, not a claim that the F interaction works.

## Authored inspection jobs

`Archetypes/Jobs/SearchFlareHidingSpotHigh` contains three Alien
`CMD_PlayAnimation` entities with `AnimationSet=ALIEN`:

| Animation | Entity ShortGuid | Use |
| --- | --- | --- |
| `job_high_locker_in` | `2B-53-67-FC` | Enter inspection |
| `job_high_locker_loop` | `8F-90-D1-52` | Inspect loop |
| `job_high_locker_out` | `21-82-C9-8B` | Exit inspection |

The input animation explicitly has `NoLayers=true`, `NoIK=true`,
`BlendInTime=.75`, `ConvergenceTime=1`, `PlayerAnimDrivenView=false`,
`GaitSyncStart=false`, `ManualLoading=false`. Its Marker connection supplies
the interaction frame. Reusing these animations requires that frame and proper
completion/cancellation, not just a clip hash.

`Archetypes/Jobs/SearchFlareHidingSpotLow` contains
`job_flare_hiding_spot_low_001` for ALIEN (entity `75-76-3C-89`).
The other two play-animation entities in that composite are HUMAN/ANDROID
`idle_search` and must not be assigned to the Alien.

The animation string databases also contain these authored clip paths under
`ANIMATION/AYZ_PRODUCTION/ALIEN/IDLE/STAND/JOBS/`:

| Clip suffix | Debug string ID |
| --- | --- |
| `LOCKER_SNIFF_001` | `FCC3F467` |
| `LOCKER_SNIFF_002` | `FDC3F5FA` |
| `LOCKER_LOW_SNIFF_001` | `B0DB2D32` |
| `DESK_LOW_SNIFF_001` | `D390663B` |
| `LOCKER_IN` | `D7628132` |
| `LOCKER_LOOP` | `1AF3B81F` |
| `LOCKER_OUT` | `10258F0B` |

Debug string IDs are identifiers in that database, not automatically valid
runtime tree-resource keys. The normal database has aliases
`ALIEN/JOB_HIGH_LOCKER_IN` (`46F392E1`), `...LOOP` (`85FE5584`),
`...OUT` (`DC652B02`), and `ALIEN/JOB_FLARE_HIDING_SPOT_DESK_001` (`08879B55`).

## Native boundaries

All addresses are preferred image VAs (image base `00400000`); rebase before use.

* `006579F0` reconstructs a primary action as `AT_PLAY_ANIMATION` (18), taking
  an 88-byte native specification and another argument. It copies the spec to
  action+`44`; it is not a function accepting just an animation ID.
* `0066C4C0` starts it; `00656BE0` updates it. Spec+`24`/`28` are resource
  keys; the first eight bytes and other pairs are entity/frame handles.
* `008B57E0` retrieves a native play-animation spec from a CATHODE reference,
  using metadata GUID at `0174BB38` and interface virtual slot+`198`.
* `CMD_PlayAnimation` type GUID lives at `01750848`; its registered factory is
  `00569340` -> `0055D900`. The factory is a CATHODE entity factory, not an
  actor animation request function.
* `hiding_pos` parameter GUID is at `01751114`; getter `008E4E10` reads a
  frame, and `00496AE0` copies its translation through `006C4510` into
  globals `016B18A8..B0`. Those globals alone do not identify an inspectable
  locker/table and should not be used as a proximity detector.

F remains reserved for search. Amanda following is menu-only. Still required:
identify actual nearby hiding controllers and their frames, submit the job on
the game thread, complete/cancel safely, then show the proximity F prompt.
Do not implement a generic wall ray as a locker detector or play hiding kills
without a matched victim.

### Live prototype now implemented

At the locker selected by the player in `DLC/SalvageMode1`, the live command
handle `FC67532B:C9A4B08D` resolves to the real entry-animation specification.
Its marker is `3AC46459:C9A4B08D`, ALIEN resource set `EFA665AC`, and animation
alias `6788B840`. Loop and exit entity IDs `52D1908F` and `8BC98221` resolve
under the same instance, with aliases `B8590CA1` and `0F5BCB05`.

`EntityPath.GenerateCompositeInstanceID(true)` matches native `GetEntityHandle`.
It includes the root seed and excludes the leaf ID. A hand-written fold in the
opposite order was wrong. Verified on the live light entity
`0108E680:38927176` before submitting any animation.

`inspection.cpp` copies those native 88-byte specs, submits entry/loop/exit on
the owned locomotion update, and only restores the action it created (pointer,
serial and primary type all match). F selects an actual loaded command within
2.6 metres and 1.2 metres vertically. Unloaded/deleted job templates are skipped;
the offline catalog includes those alternatives as well as the active ones.
Pending user validation: visible prompt, actual animation, completion and walking.

The generated catalog initially covers the current level; the all-level exporter
is `dev/probe/export_all_inspection_points.ps1`. Do not advertise full-level
coverage until that export succeeds and the runtime filters by level name.

## Standing turns

ALIEN `ActNPCLocomotionStandingTurn` uses `requiredTurn`, `remainingTurn`,
`turnComplete`, and the `turnFinished`/`urgentTurnFinished` metadata listeners.
It selects `idle_turn_r_90`, `idle_turn_l_180`, `idle_turn_l_90`,
`idle_turn_r_180`; each has `ConvergeOrientation=true`,
`ConvergeTranslation=false`, `NotifyTimeOffset=.3`. Tree ease-in and selector
ease are both .5 seconds. No urgent-speed parameter was found in this tree.

`0044D4F0` updates one manager entry: ECX points to a MemoryPtr array manager,
stack arg0 is an index, the array begins at `[ecx]`, ends at `[ecx+4]`, with
8-byte entries. Turn transitions are now filtered to the owned Alien's L.
An update of another actor must not advance our camera or rebuild our action.
