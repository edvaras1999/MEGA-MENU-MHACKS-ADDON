# Status - Show Player In Inventory LE 2.4-test10

**Target.** Skyrim LE 1.9.32.0.8, SKSE 1.7.3, MSVC v143 Win32. A native port of Ivy's Show Player In Inventory 2.4 (SE,
GPL-3.0). Engine addresses and object access use the Legendary Edition backend; the screen-shape layout behavior is adapted
from SE and must be validated against LE menus in game.

**This is a development build. The new item display positioning has not been built or accepted in game.** Visual behaviour and compatibility
in an actual load order have not been validated.

Test9 added automatic stock-layout placement for the item card, 3D preview and rotation area, plus manual offsets in KHA-OS/INI. Test10 adds a button for the Compact Item Viewer offset preset and logs whether the Scaleform card was found and whether automatic placement was accepted. It restores temporary game-setting and node transform changes when the item menu closes. Test8's visible-preview default remains in place. Test10 has not yet been rebuilt; in-game interaction with RunicWheelLE and other inventory-preview plugins remains unverified.

## What was checked

From the recorded test-7 package (`PACKAGE-README.txt`, section VALIDATION):

| Check | Result |
|---|---|
| Offline fixture built from the actual plugin sources (a fake game in an owned child process) | 17782 checks, 0 failures |
| Steady-state frames | 2000 simulated frames in each of the direct and retained-parent paths, plus 300 stable camera frames, with interleaved native camera and message work and zero own C++ allocations |
| Late-exit callbacks and supervisor timeout | tested |
| Diagnostic assertions | 78 separately compiled defects were each detected |
| Static `TESV.exe` checks | 18 function signatures and 14 exact hook targets |
| Production DLL loaded and queried in a separate x86 process | 7 checks |

[GitHub Actions](https://github.com/Mhacks79/Show-Player-in-Inventory-LE/actions/workflows/build.yml) repeats the build,
the fixture and the supervisor-timeout proof on every push. The mutation tests (78 compiled defects) are a manual run.
These sources were re-laid out for GitHub (`src/`, `tests/`, `sdk/`, `ThirdParty/` at the top level instead of nested
folders); the first run in that layout is the check that it builds.

## Not verified

- Real rendering, physics, load-order compatibility and runtime performance. The fixture's native functions are stubs, so
  its elapsed time is not evidence of live animation, physics, shader or GPU cost.
- The optional graphical panel has not been exercised on a live Direct3D 9 game device.
- Custom skeletons, camera plugins, SkyUI variants, face mods and ultrawide layouts have not been certified.
- Skyrim Souls is recognised and the two options that conflict with it are skipped; other custom unpause plugins may
  need `bAnimateWhilePaused` and `bNeutralFace` turned off.

## First in-game check (from the package)

Use a disposable save. Open and close Inventory and Magic from first and third person, including Tween transitions; check
framing, face, animation, weapon visibility, input and restored FOV. Test supported containers and barter only after
enabling them. Try dialogue, combat, loading and saving, repeated menu changes, mouse and controller, KHA-OS if
installed, and your normal camera and physics mods. If anything is wrong, disable the feature or remove the DLL and INI,
and provide the log (`Documents\My Games\Skyrim\SKSE\ShowPlayerInInventoryLE.log`) and the conditions. After three guarded
faults the feature stops and the original menu calls stay chained.
