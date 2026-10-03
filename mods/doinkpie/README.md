# doinkpie

Doink throws his unused pie. `--mod doinkpie`.

The original has the projectile (`doink_pie` in `SPECIAL.ASM`, with `piesplat_anim` and the pie
images `PIE01`-`08`) and a commented out `ANI_SNOT,doink_pie` in his punch animations
(`DNKSEQ2.ASM`), but nothing throws it. The pie's flight animation there (`pie_anim`) even shows the bat
frames (`BGBAT`) instead of the pie.

**Move:** Down, Down + kick. `gen.txt` adds it to Doink's secret move table, with a throw animation made from his
punch frames without the attack (`dnk_2_pie_anim`, `dnk_4_pie_anim` in `asm/DOINKPIE.ASM`), a flight of the
four pie frames (`dnk_pie_flight`), and its own projectile id (4, so the hit shows the pie splat and does the
reaper's damage). Without the mod nothing changes; `pie_enabled` is what the move checks.

Tried: a scripted match where the pie leaves his hand and flies, and the splat is on the target. Unverified:
damage balance, a block against it, sounds (none are played), and the pie against the referee.
