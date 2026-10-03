# damage

A factor on all damage. `--mod damage=N`: N tenths of the normal damage (5 = half, 10 = the original, 30 = triple).

The game scales every point of damage by `speed_adjustment` (`LIFEBAR.ASM`, 16.16 fixed point, set from the
"game timer speed" adjustment when a match starts). The mod multiplies that value from the moment the game sets it, so
no game code changes. Life that is added (a pickup) goes through the same scaling. The combo damage rules (15 minus the
hit number) are applied first, so a combo changes in the same proportion.

Tried: the variable becomes half or double with 5 and 20. Unverified: the balance, and that nothing else reads
`speed_adjustment`.
