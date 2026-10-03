# easymoves

Every special move as **Toward, Toward + a button**. `--mod easymoves`.

The slots, in the order they are handed out to a wrestler's moves:

| | super kick | kick | punch | super punch |
|---|---|---|---|---|
| Toward, Toward | 1st | 2nd | 3rd | the grab (neck grab), left alone |
| Down, Down | 4th | 5th | 6th | 7th |

A move that already has one of these keeps it. Moves that start the same way in the original (for
instance the hip toss and the throw of a jumping opponent, both Away, Away + punch) share one new
combination, since the game tells them apart by the state of the match. The original ways in still
work. The full list of who gets what is in [MOVES.md](MOVES.md), written by `mkgen.py`.

Three kinds of move:
- **Standing moves** (the secret move table): face rake, jump kick, hip toss, fling and so on.
- **Head hold moves**: the high risk move when you have the hold and the *reversal* when you are held
  (the processes named `*_hdhold_*`). They are a group of their own, since only the head hold is on then.
- **Other special processes** (the throw of a jumping opponent, the spirits, the salt): they share the
  standing group.

**Repeats.** A move that repeats on its button (Shawn's spin kick, Doink's clap, ...) looks at how many times the
button of the *original* way in was pressed since it began. Started the easy way with another button, it would not
repeat on that button. So when an easy move starts, presses of its button also count as presses of the original
button for the next four seconds (`easy_alias`, called by the extra record or process; `easy_alias_mask`, called from
`count_button_presses`). Tried: Doink's clap started Down, Down + super kick: `easy_alias` ran with the two
buttons, and each super kick press after it counted as a punch. Not seen: the repeats of every move.

Doink's hammer and Lex's morning star repeat on the kick button in the original, although
they are started with the super kick. With the mod the repeat button is the super kick: the count of
super kicks is copied over the kick count just before the check (`easy_sync_kick`).

How it works: `gen.txt` (written by `mkgen.py`, not by hand) puts an extra record in front of each standing
move in the secret move table, and makes a copy of each special move process with the new sequence
(`edit ... clone`, tools/gsp/extras.py). Both only do the move when `easy_enabled` is set, which the mod does;
without it the record matches and does nothing (the input goes on to the normal punch or kick) and the
process copies just wait.

Unverified: only Doink's ear slap (Down, Down + super kick) was tried and shown to do a move with the mod and
not without it. The head hold copies were built and the match ran for 5000 frames with a lot of input, but
none was seen in a real head hold. The Toward, Toward + kick moves also start a normal kick attack.
