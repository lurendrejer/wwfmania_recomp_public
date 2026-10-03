package org.wwf.wrestlemania;

import org.libsdl.app.SDLActivity;

/** SDL's activity, loading libSDL2.so and libmain.so (the game: src/platform/main_game.c). */
public class WWFActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }
}
