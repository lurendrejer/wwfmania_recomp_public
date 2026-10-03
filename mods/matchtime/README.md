# matchtime

The length of a round in seconds. `--mod matchtime=N`, 10 to 99 (the original is 99).

The clock is `match_time` (tens, ones and a fraction counter, 16 bits each). Every round starts with 99 and 0 in the
fraction. The mod writes its own tens and ones when it sees that start; the clock counts down from there.
The run-out rule, awards for a quick win and the like use the game's own clock, so they follow.

Tried: 30 seconds counted 30, 29, ... (measured every 30 frames). Unverified: the quick win awards, which may have
fixed limits in seconds.
