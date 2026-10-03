# cpuskill

CPU difficulty. `--mod cpuskill=N`, -5 (easier) to 5 (harder).

`drone_calcskill` in `DRONE.ASM` ends by adding the game's own difficulty setting (`+8 default`) to a skill number
0-29 that the drones use; `gen.txt` adds `cpu_skill_add` there. Each step of the mod is two skill points, which is one
step of the game's own difficulty setting. The result is still limited to 0-29 by the game.

Tried: the value reaches the variable. Unverified: how the difference feels; the skill number is only what the
drone code makes of it (`DRN_SKILL`), and not measured against the arcade's difficulty setting.
