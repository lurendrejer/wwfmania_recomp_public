# disco

Every colour on screen turns around the colour wheel. `--mod disco=N`, N the speed (1-20, default 4).

C only, no game code changed. After each frame (`frame_end`) the mod keeps a copy of the colour RAM
(`video.colram`) and turns the hue of every colour about the grey axis. Before the next frame (`frame_begin`) it
puts the copy back, so the game only ever reads its own colours. Greys, black and white stay as they are. High-res
art is drawn over the turned palette colour, so it turns with it.

Tried: a screenshot during a match shows the colours turned (purple ring apron to pink). Unverified: a colour that the
game writes into the colour RAM between the mod's two hooks without the display page changing would not flicker, but
has not been looked for.
