# wrongnames

The announcer calls every wrestler by someone else's name. `--mod wrongnames=N` (1-7): each wrestler is named as the
one N places further on in the roster Bret, Razor, Undertaker, Yokozuna, Shawn, Bam Bam, Doink, Lex. With the default
of 1, Bret is "Razor Ramon", Razor is "the Undertaker", and so on, and Lex is "Bret Hart".

Every place `DCSSOUND.ASM` picks a line by wrestler number goes through `wn_name` (`asm/WRNAMES.ASM`) first:

- `MAKE_ANNOUNCEMENT`: the intro before the match ("X versus Y", "X squares off against Y")
- `SET_UP_PERSONAL_CALL`: "give credit to X", "it doesn't look good for X", "giddup X", "a very impressive move by X"
  and the rising chants
- `WRESTLER_SPEECH`: the lines at the finish

`wn_name` looks the number up in `wn_map`, which the mod writes every frame. The spare slot (7) and numbers above 8 stay
as they are.

Tried (headless, one CPU match, sound commands logged): the announcer's lines before and during the match are other
sound codes with the mod on, and different again with N = 3. Unverified: which names are said, by ear.
