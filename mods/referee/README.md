# referee

Turns on the referee that the original game shipped art and code for but
never enabled (`orig/REF.ASM`, `REFSEQ1.ASM`, `REFIMG.ASM`, `IMG/REF.LOD`).
He is wrestler number 9, added through `gen.txt` (see docs/MODS.md); the
game is regenerated with him, and `--mod referee` sets `ref_enabled`.

He is only half finished in the original, so `gen.txt` also fixes or works
around (in memory; `orig/` is never touched):
- his turn animations use torso frames `J1TT5A`/`J1TR5Z` above 4, which do
  not exist in the LOD; they are clamped to frame 4;
- his position logic never penalised standing too far from the wrestlers;
  a penalty (x8) is added so he stays close;
- he is taken out of `process_ptrs` after setup, so the AI does not pick him
  as an opponent (the tables indexed by the opponent's number have no entry
  for him); `set_images` draws him from `ref_process` instead;
- the wrestler loop ran the drone brain for him (`PLYR_TYPE` is not 0), and
  its tables have no entry for him: it is skipped;
- he cannot be hurt: `adjust_health` and `wrestler_hit_special` ignore him.
  The original has no fall, knock down or get up animations for him, only
  stands, walks, turns and two light hits (`ref_hit` in REF.ASM), so a
  knock down showed another wrestler's frames in his palette.

Unverified: a full match with two active players and the match end with him
on; the light hits from the players' attacks (`ref_hit`).
