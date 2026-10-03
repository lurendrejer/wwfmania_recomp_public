# Sound

WWF WrestleMania's sound comes from a DCS board: an ADSP-2105 DSP with 8K
words of RAM and four 1 MB ROMs (`wwf_music-spch_l1.u2` ... `.u5`) that hold
its program and compressed audio. The game only sends it 16-bit command
words (`orig/DCSSOUND.ASM`). The board decodes and mixes the audio itself.

The port does **not** emulate the board while the game runs. Instead:

1. `dcsrip` (tools/dcs) runs the board's own program once, on an ADSP-2105
   emulation, and records what every command plays. The result is a
   directory of WAV files plus `sounds.txt`, a table that says what each
   command does.
2. The game (`src/sound`) answers the same commands by playing those files.
   Every file can be replaced.

The ROMs are not in the repository. You need your own copy.

## Extracting

    ./build/dcsrip path/to/roms sounds

`path/to/roms` is a directory or a MAME zip (`wwfmania.zip`) with files
ending in `.u2`, `.u3`, `.u4` and `.u5`. dcsrip warns when the CRCs are not
the L1 set (`a9acb250 9442b6c9 cee78fac 5b31fd40`). The run takes a
while: every code from 1 to 0x1FFF is tried, and music plays until it
loops. `wwf` looks for `sounds/` in the current directory (`--sound DIR`).

## What the board does

Commands are 16-bit words, high byte first. The board acknowledges each
byte (bit 11 of the sound status register) and can send bytes back
(bit 10 low = a byte is waiting).

| Command | Meaning |
|---|---|
| `0000` | stop everything |
| `55AA`, `vvXX` | master volume `vv` (0-255); `XX` must be `~vv` |
| `55AB+c`, `vvXX` | volume of channel `c` |
| 990, 991 | stop all channels |
| 993 ... 997 | stop channel 0 ... 4 |
| 999 | revision request: replies `0001` (and stops the music) |
| anything else | a sound (or nothing) |

The board has five channels. Channel 0 is music, 1 to 4 are effects and
speech. A new sound replaces whatever its channel was playing. The game
picks the channel itself: effect codes come in groups of four (`base+0` to
`base+3` play the same sound on channels 1-4), and `DCSSOUND.ASM` tracks
priorities and durations per channel.

After reset the board checks its ROMs, reports `79` and then `01` (no bad
ROM), and plays a tone. A byte written within about 270 ms of the reset
skips all of that, which is what `QSNDRST` does.

The stop codes, the replies and the volume curves above are not hard-coded
in the player: dcsrip measures them and writes them to `sounds.txt`.

## sounds.txt

One entry per line, `#` starts a comment. Times and lengths are in samples
at `rate`.

    rate 31250
    boot boot.wav                      power-up tone
    reply reset 73920:0079 78720:0001  bytes after reset (sample time:value)
    reply 999 0001                     bytes after a revision request
    stop 993 0                         command 993 stops channel 0
    volume master g0 ... g255          gain of each volume value, x 65536
    volume channel g0 ... g255
    sound 0001 0 0001.wav 483 11517 317280
          code ch file     delay loop_start loop_length

`delay` is how long the board took to start the sound. The file starts
at the first audible sample. `loop_start` is -1 for sounds that play once.
Codes that play the same audio share one file, named after the first
code.

## Replacing sounds

Put a file with the same name in a directory and pass it with
`--sound-art DIR`. It can have any sample rate, and it can be mono or
stereo, in 8, 16, 24 or 32-bit PCM or 32-bit float. If it has a WAV
`smpl` loop, that loop is used. Otherwise the original loop points are
scaled to the file's rate, which fits a remaster that keeps the original
timing. The extracted WAVs carry their loop as a `smpl` chunk too, so
audio editors show it.

## How the extraction works

- `adsp2105.c` is an ADSP-2100 family interpreter (ALU, MAC, shifter, both
  address generators with circular buffers and bit reversal, loops and
  stacks, interrupts, timer, the 2105 boot format). It was checked
  instruction by instruction against MAME's `adsp2100` core, and
  `tests/test_adsp.c` covers it with small programs.
- `dcs.c` is the board: the memory map, ROM banking, the latches, and
  SPORT1 autobuffering into the DAC, following MAME's `dcs.cpp`.
- The board's own ROM checksum test passes, which exercises the ALU over
  all 4 MB, and speech decodes with clean pitch harmonics.
- **Loops are found exactly.** After every block of output, dcsrip hashes
  the board's RAM, apart from seven words that tick even when it is idle
  (found at start-up). When a state comes back, everything after it repeats
  forever. If the repeating part is silent, the sound has ended.
  Otherwise it is a loop with an exact start and length.
- The channel of each code is found by muting one channel at a time.

## Open questions

- The volume curves are measured as RMS ratios of a music track, not read
  from the board's tables.
- `SELECT.ASM` fades "channel 5" (`55B0`), which does nothing on the board.
  The player ignores it too. `55B1`-`55B3` raise the level slightly on the
  board and are not modeled. The game does not send them.
- Command 998 (sent by `SELECT.ASM`) has no audible effect.
- The player cuts a channel when a new sound starts. The board does the
  same, but it mixes in fixed point, so clipping of very loud mixes may
  differ.
- The reset replies are delivered at once rather than 2.4 s later. The
  recompiled CPU runs its wait loops faster than the real one, and the
  self test would give up before the real delay.
