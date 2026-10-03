# coopladder

Two people play the one-player championship ladder together, as one team against the computer.

`--mod coopladder=N` (or left/right on the mod's line in the F1 MODS page, "LADDER"):

| N | What COOP does on the "SELECT PLAY MODE" screen (both players in) |
|---|------------------------------------------------------------------|
| 0 | the original royal rumble (mod on, nothing changes) |
| 1 | the Intercontinental ladder (7 matches) |
| 2 | the WWF championship ladder (7 matches, the last an 8-on-2 final battle) |
| 3 | (default) the belt question ("SELECT YOUR TITLE", the existing screen) follows, then that ladder |

Head to Head is untouched. With the mod off (or N = 0) the game runs exactly as the original
(checked: same instruction count at frame 2500 with and without `--mod coopladder=0`).

## How it works

`gen.txt` edits the original modules (nothing in `orig/` changes). The game already has a cooperative mode:
with `royal_rumble` set both humans stand on the first team with one shared life bar, one camera and one
getup/name display. The mod keeps `royal_rumble` set during a ladder match and adds `coop_ladder`
(`asm/COOPLADDER.ASM`). The places that are about the *line of eight* (wrestler counter, zombies that
replace dead drones, clock speed, rounds per match, timeout rule, auto pin, damage table, KO of drones) call
`rr_line` instead of reading `royal_rumble`: in a ladder match that is a line only in the WWF final battle.
Match flow: `pregame_show` starts the ladder; after a match the team's win becomes `match_winner = 3` (both
humans keep playing) and goes on to the ladder screen; a loss repeats the match after the buy-in screen
(both may continue; if only one buys in he carries on alone, as one player, with the same ladder).
The ladder screen shows both humans' logos on the left and the opponents on the right.

## Verified (wwfrun, screenshots and label traces; `ctest -R coopladder`)

- question screen -> belt screen (N=3) -> ladder screen -> match 1, two humans on one side against Bret Hart.
- a human win goes to the next ladder screen and match; seven matches, then fireworks, the beaten-game
  initials screen for both and the credits, for both belts (the wins are forced by pokes in the tests).
- WWF final: "OPPONENTS: 7..1" counter, dead drones replaced from the line.
- a loss: "MATCH AWARDED TO", buy-in screen for both; both press start and choose, the same match is
  fought again; only one of them pressing start gives a one-player match against the same opponent.

## Unverified / known gaps

- Only played by script. The balance of two humans against 1 to 3 opponents is untouched.
- The progress screen shows only player 1's wrestler running in (two logos on the logo screen).
- Fights against your own character happen in one-opponent matches, as in the original one-player game
  (only the first multi-opponent matches avoid both humans' wrestlers).
- The final-screen texts are the rumble's: "NEW WWF TAG TEAM CHAMPIONS ... PREPARE TO BATTLE EACH OTHER"
  (also after the Intercontinental ladder), and the end story is player 2's wrestler's.
- Audits, award bars, high-score entries and win-streak messages follow the rumble (skipped or per team);
  the initials screen at the end is the two-player one. Not checked for correct counts.
- Buddy mode is switched off in a cooperative ladder (it would add drones without the ladder).
- **Two players only.** The ladder is for the game's two players on one team. With `fourplayer` also on, a third and fourth
  person (the mod's buddy-mode partners) are not put into the ladder: only the two players are, and the others wait. The
  ladder match is built on the royal rumble's team code, which has no place for partners (the fourplayer mod keeps them out of
  the rumble for the same reason). Not solved.
- A player joining mid-ladder (one-player game, second player buys in) was not tried; the code continues
  the existing ladder.
- The edits use the Nth `move @royal_rumble,a14` of several modules (see gen.txt); another mod inserting such
  lines in the same files would shift them.
- Not tried on a real display (no SDL here); the F1 menu line is generic.
