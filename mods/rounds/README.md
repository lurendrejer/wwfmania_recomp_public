# rounds

Wins needed to win a match. `--mod rounds=N`: 1 = one round decides, 2 = best of three (the original), 3 = best of five.

`set_winner` in `LIFEBAR.ASM` is the only place the game's round count grows, but the game tests
`== 2` in about twenty places (the wrestler files, the life bar, the round announcer). So the mod does not change
those: with it on, `set_winner` (see `gen.txt`) keeps the real count of each side in `rn_real` and hands the game 1
until the real count reaches `rounds_needed`, then 2. The "final battle" bonus (which adds 2) counts as a win of the
match. Without the mod (or with `rounds_needed` 0) the code path is the original.

Tried: with the round clock cut to 10 seconds (`matchtime`), a one-round match ends after the first round, and with 3 the
match runs to three rounds. Unverified: the round icons and announcements beyond round 3 (the round number picture for
round 4 and 5 is the game's fallback for "4"), awards that mention "2 round victory", and mixing with `moredrones` or
`fourplayer`.
