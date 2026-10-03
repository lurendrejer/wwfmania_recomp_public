# training

Practice mode. `--mod training` (or `--mod training=N`, or left/right on its line in the F1 menu).

Nothing in the game code changes; the mod reads and writes the game's variables every frame.

- **Clock:** the match clock is held where it is (the seconds digits are written back every frame), so a round never times out.
- **Number (`REFILL`):** 1 = Start 1 refills life and turbo of every wrestler; 2 = human wrestlers never lose
  life or turbo (default); 3 = nobody does.
- **Boxes:** the body box of every wrestler (green), and the attack box while a move can hit (red)
  (`OBJ_COLL*` and `OBJ_ATT*` in `PLYR.EQU`, used the way `COLLIS.ASM` does). Each box is drawn as its
  front face at its middle depth; the depth (Z) overlap that also decides a hit is not shown.

Tried: a scripted match, the body boxes line up with the wrestlers (screen y calibrated by eye with
`FEET_ADJ`). Unverified: the red attack box (drawn from the same fields `COLLIS.ASM` uses, not yet compared with
a hit) and the boxes at other view sizes than the default wide view.
