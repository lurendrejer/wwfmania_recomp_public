# Android TV

The app has been built and run on an **NVIDIA Shield TV Pro**: the build scripts, installing, the HD art, the GPU path, the
memory limits and the F1 menu with a controller. For the steps, start with the Android section of the README. This page began
as a plan, written before there was an Android SDK to try it on, and most of it is still the plan: where a heading or a
paragraph says "untested" or "unverified", read that as "written before it was tried"; it is not a statement about the
current state. Other devices have not been tried.

## State

Written, and tested on Linux only where it can be:

- `WWF_REGEN=OFF` and `-DWWF_GEN_DIR=<a host build's gen>` make CMake use a ready-made generated game and no Python
  (checked: a desktop build in a second directory from the first one's `gen`, `wwfrun` runs the same).
- `src/util/dataextract.c` (`data_extract`): copies a bundle (`filelist.txt` + `data.stamp` + files) to a directory once per
  stamp; test `dataextract`.
- On Android (`#ifdef __ANDROID__` in `src/platform/main_game.c`): the paths of the game come from the app's storage, the
  bundled assets are copied there on the first start, "APPLY AND RESTART" saves and asks for a manual restart. Only
  syntax-checked with `-D__ANDROID__` on the desktop, never run.
- Start + Back pressed together open the F1 menu (the Guide button is the system's on Android TV); works on the desktop too.
- `android/`: a Gradle project (AGP 8.5, SDL2 fetched by `android/fetch-sdl.sh`), a manifest for Leanback, `WWFActivity`,
  `jni/CMakeLists.txt` that builds SDL2 and the game as `libmain.so`, placeholder banner and icons, and the Gradle task
  `bundleWwfData` that puts `orig/IMG`, the generated `gen/` (without its C files) and `sounds/` into the APK's assets if they
  exist at build time (that task was run alone with a fake tree: it copies what exists, writes the file list and a stamp).
  Nothing else in `android/` was run: there is no Android SDK/NDK here.

How the build is meant to go (all untested): on the desktop `cmake -S . -B build` (makes `build/gen` with the mods in), then
`sh android/fetch-sdl.sh`, then `cd android && gradle assembleRelease` (with `ANDROID_HOME`/the NDK installed, and `-PwwfGen=`
if the gen directory is elsewhere). The APK is `android/app/build/outputs/apk/release/`.

## Why it looks possible

- Everything that ships is C11 with SDL2 only in `src/platform/`; SDL2 has an Android backend, and the port already opens
  game controllers (`src/platform/gamepad.c`, up to four).
- The recompiled game is 81 generated C files (`build/gen/c/gsp_*.c`, 11 MB source, a 6 MB static library on Linux). They are
  plain C with no platform code, so the NDK can compile them.
- Speed on a desktop core (Xeon 2.8 GHz): 3000 frames in 1.8 s at render scale 1 and 7 s at scale 3, about 90 MB of RAM. That
  is far above the 54.7 fps the game needs, but nothing was measured on ARM or on a TV box.
- The F1 menu is already drivable with a controller (`pad_key` in `src/platform/menu.c`: D-pad, A = Enter, B = Esc,
  Guide = F1).

## What has to be done

1. **Generate the recompiled code on a host, build it with the NDK.** `tools/gsp/*.py` (Python 3, standard library) turns
   `orig/` into `build/gen/` (the C files, `symbols.txt` and the image tables) at CMake configure time. Do that on the
   desktop first; the Android build then only compiles `gen/c/*.c` (`modules.cmake` lists them) plus `src/`. CMake needs a
   variable that says "use this ready-made gen directory and do not run Python" (today `WWF_GEN_DIR` is fixed to
   `build/gen` and the regeneration is part of configure).
2. **SDL's Android project** (Gradle + `SDL2` sources, `SDLActivity`) with the game as its native library:
   `add_subdirectory` the repo's CMake from the app's `CMakeLists.txt`, and build `wwf_platform` and `src/platform/main_game.c`
   as `libmain.so` (SDL's convention). SDL2 is fetched as source from `github.com/libsdl-org/SDL` (reachable from the
   cloud environment, the SDK is not).
3. **Data files.** The game reads, at run time as files: `orig/IMG` (117 MB, the images), `gen/` (15 MB: `symbols.txt`, the
   `.GLO`/`.TBL`/LOD tables), and optionally `sounds/` (extracted with `dcsrip`) and the high-resolution `art/` (`art/hd` is used when it is there, else `art`; it is
   what `--art` takes on the desktop). Two ways to get them onto the device, both
   wanted:
   - **Bundled into the APK when they are on the disk at build time.** The Gradle build (or the CMake step that drives it)
     looks for `orig/IMG`, the generated `gen/` and `sounds/` next to the source; every one that exists is added to the APK's
     `assets/` (with `noCompress` for `.IMG`/`.wav`, so they are stored, not deflated), and the app is then self-contained.
     One that is missing is simply left out. The game code reads files with `fopen`, and Android assets are not files, so on
     the first start (and whenever the bundled data's version stamp differs from the one in app storage) the app copies the
     assets to `SDL_AndroidGetInternalStoragePath()` (SDL: `SDL_RWFromFile` on an asset path, or the Java `AssetManager`), about
     150 MB and a few seconds, and runs from there. A copy in app storage is checked before it is copied again.
   - **Not bundled: filled in afterwards.** The same folder layout in the app's `Android/data/<id>/files/` (USB or adb), or copied
     from a folder the player picks (Storage Access Framework), for an APK built without the data.
   `wwf.cfg` and `wwf.cmos` (relative paths today) go to the internal storage path too. `fs_read_file` (`src/util/fsutil.c`)
   uses plain `fopen`/`stat`, which works for paths in app storage. Size: an APK with all of it is roughly 250 MB, and a single
   APK may be up to 4 GB, but not larger than the 200 MB or so that the older `zipalign`/install paths on some boxes handle
   comfortably: if a device refuses it, the data goes in an expansion/asset pack or the second way above.
4. **Code that will not work as it is:**
   - the restart (`execvp` in `src/platform/main_game.c`, used by "APPLY AND RESTART"): on Android it must finish and
     start the activity again, or the settings applied by tearing down the window and calling the start code again;
   - `SDL_GetDesktopDisplayMode` for the shape of the view is fine, but the window is always full screen: `--res` and the
     WINDOW SIZE menu line do nothing useful;
   - `#include <unistd.h>` is fine (bionic), but the command line (`--mod`, `--free-play`, ...) has no equivalent: the mods and
     options must come from `wwf.cfg`/the F1 menu.
5. **Controls on a TV.** The Guide/Home button belongs to Android, so the F1 menu needs another way in (for example
   Start + Back held, or a long press of Back). Coin, start and test are keys or Back on the pad; a remote without a pad
   cannot play, so a gamepad is a requirement. Four players on four pads is the case the `fourplayer` mod was made for.
6. **Android TV packaging.** `AndroidManifest.xml`: `android.software.leanback` optional, `android.hardware.gamepad`,
   `<category android:name="android.intent.category.LEANBACK_LAUNCHER"/>`, a 320x180 banner, landscape only, full screen,
   keep the screen on.
7. **Audio.** `src/platform/sdl_audio.c` queues mixed 48 kHz frames; SDL's Android audio should take that, but latency
   was not looked at.

## What is not free to distribute

The images (`orig/IMG`) and the sounds are copyrighted by Midway/Acclaim. The repository has neither the sounds nor the
images in a form that belongs in an app, and an APK containing them could not be published on Google Play or attached to a
release. A build that bundles them is for the person who has the files, on their own devices: the build must never commit
or upload the bundled result, and a build without the files must still produce a working, empty app (the second way above).

## Order I would do it in

1. Split the CMake regeneration from the build (a ready-made gen directory), and check it with a Linux cross build using the
   NDK's toolchain file, if an NDK can be had.
2. A minimal SDL Android project that starts the game with the data bundled as assets (extracted on the first start) or on `/sdcard`, no menus.
3. Controller start/menu combination, the restart replacement, storage paths.
4. Packaging for Leanback, a run on a real box, measuring the frame rate.

## GPU drawing (optional, unverified on a device)

At render scale 3 with HD art the CPU rasterizer and the palette conversion are the cost (docs/VIDEO.md, "GPU path"). The
GPU path does both with OpenGL ES 2. It is off by default.

Trying it on the Shield (or any Android TV box), no menu needed: make an empty file called `gpu` next to `wwf.cfg` (the app's
internal storage, the same folder as the `noart` switch), then start the game:

    adb shell run-as <app id> touch files/gpu         # internal storage path as SDL_AndroidGetInternalStoragePath() gives it

(`rm` the file to turn it off; `noart` next to it still turns the HD art off.) The F1 menu has the same switch on the MODS
page (`GPU DRAWING`, needs APPLY AND RESTART), saved as `gpu=1` in `wwf.cfg`. `adb logcat -s SDL/APP` shows what happened:
`WWF: gpu file found, GPU drawing is on`, then either `GPU path on: <GL_VERSION, GL_RENDERER>` or `GPU path not used: <why>`
(the game then runs on the CPU as before). The existing line `frame N, X ms for the last 60 (machine M, present P)` shows the
effect: the machine time should drop (the drawing is recorded, not done) and the present time should be the GPU's.

What the Android side needs: nothing beyond what is there. The manifest already asks for `glEsVersion` 2.0; SDL's Android
backend uses the `opengles2` render driver by default, which is the one the GPU path runs on (no second window or context; the
window the copy progress bar made is the one the renderer is made on, as before). `android/app/jni/CMakeLists.txt` needs no
change: `gpu_video.c` is part of `wwf_platform`, it loads GL through `SDL_GL_GetProcAddress` and links nothing extra, and the GL
ES 2 headers it includes come with SDL.

Unverified (no device, and no Android SDK/NDK, in the session that wrote it): everything on a real GLES driver. In particular
that the Tegra/Mali/Adreno driver gives `highp` floats exact enough for the start-up comparison (if not, the game stays on the
CPU and says so), that `GL_MAX_TEXTURE_SIZE` is at least the bitmap (about 1700 x 3400 at scale 3), memory for the HD textures
next to the CPU copies, and the EGL context loss on pause/resume (`SDL_RENDER_DEVICE_RESET` is handled by starting the GPU path
again from the CPU copy).

## Open questions

- Which ABI: arm64-v8a is enough for current TV boxes; 32-bit only boxes exist.
- Whether the frame rate holds on a slow box at render scale 3 with the wide view and the dynamic zoom at 10 percent
  (5120 x 2880 bitmap: much too big; the limits would have to be lower on a TV).
- How much of the F1 menu is usable with only a pad (the key rebinding lines need a keyboard to test).

## Scripts (macOS and Windows)

`android/` has scripts that do the steps below. They were written without a Mac or a Windows machine to run them on, so
expect to fix a line or two; only `regen_gen.py` was run (its output is identical to what CMake makes in `build/gen`).

| | macOS | Windows |
|---|---|---|
| once: Java 17, the Android SDK/NDK, `local.properties` | `sh android/setup-mac.sh` | `android\setup-windows.bat` |
| build the APK | `sh android/build-mac.sh` | `android\build-windows.bat` |
| build and install on the connected device | `sh android/build-mac.sh --install` | `android\build-windows.bat install` |
| generate the game again first | `--regen` | `regen` |

The build scripts generate the game with Python only (`android/regen_gen.py`, no CMake or C compiler on the desktop), fetch
SDL2 if `android/SDL` is missing, run `./gradlew assembleRelease`, and print where the APK is:
`android/app/build/outputs/apk/release/app-release.apk`. `regen_gen.py` can also be used alone: `python3 android/regen_gen.py [out]`.

## Setting up a Mac (or any desktop) to build the APK

Untested steps, written for someone who has none of it installed:

1. Java 17: `brew install openjdk@17` (and put it on the PATH as brew says; `java -version` must say 17).
2. The Android command line tools: `brew install --cask android-commandlinetools` (or Android Studio, which brings the SDK manager).
   Then install what the build asks for (accept the licences first: `sdkmanager --licenses`):
   `sdkmanager "platforms;android-34" "build-tools;34.0.0" "platform-tools" "ndk;26.1.10909125" "cmake;3.22.1"`.
3. Tell Gradle where the SDK is, either with the variable `ANDROID_HOME` (bash/zsh: `export ANDROID_HOME=...`; fish:
   `set -x ANDROID_HOME ...`), or a file `android/local.properties` with the line `sdk.dir=/path/to/sdk`.
4. In the repository: `cmake -S . -B build` (makes `build/gen`), `sh android/fetch-sdl.sh`, then
   `cd android && ./gradlew assembleRelease`. `./gradlew` is the Gradle wrapper (8.9): it downloads Gradle itself, so no
   `gradle` needs to be installed. The APK is `android/app/build/outputs/apk/release/app-release.apk`.
5. To a TV box or a phone with USB or network debugging on: `adb install -r android/app/build/outputs/apk/release/app-release.apk`.

## Small APK, data pushed with adb (untested)

An APK with everything in it is over 1 GB and is copied a second time into app storage at the first start, so a device
with little room refuses it (`INSTALL_FAILED_INSUFFICIENT_STORAGE`). Build without data and push the folders instead:

    sh android/build-mac.sh --nodata --install      # android\build-windows.bat has no such switch yet: -PwwfNoData=1 to gradlew

Start the app once (it creates its folder), then, with `D=/sdcard/Android/data/org.wwf.wrestlemania/files`:

    adb push orig/IMG $D/orig/IMG
    adb push sounds $D/sounds
    adb push art/hd $D/art/hd
    adb push build/gen $D/gen        # or a copy without gen/c

With nothing bundled the game uses that folder as its data root (`android_paths` in `src/platform/main_game.c`); the
whole set must be there, because the root is chosen as a whole. A drive adopted as device storage works the same way.

## HD art on worker threads (untested on a device)

The HD art is read and decoded on two worker threads (`src/platform/gfx_async.c`): the first time an image is needed the
original low-resolution one is drawn for a few frames instead of the game stopping to read the PNG, and the images of
the wrestlers in the match are loaded ahead of time (docs/ASSET_OVERRIDES.md, "Loading while playing"). The line in the
log every 60 frames now reads `hd art read from disk: N images, X ms (on the workers), M pending`. An empty file `syncart`
(next to `wwf.cfg`, as `gpu` and `noart`) goes back to reading on the game thread, which is the way to compare. Prefetch
stops after 192 MB of decoded art on Android (the rest is loaded as the game needs it). Not measured on a device yet.

## Timing the GPU path (untested on a device)

With the GPU path on, the log line every 60 frames is followed by a second one: CPU time spent recording the blits and
fills (`record`), submitting the list to GL (`play`), the present pass (`final`), texture uploads (count and size) and
read backs. An empty file `gpuprofile` (in the app's internal storage, or in `Android/data/<id>/files`, which
`adb push` reaches; `gpu` and `noart` are looked for in both places too) also calls `glFinish` after the list and the
present pass and logs how long the GPU was still busy (`GPU busy after play / after final`); that serialises the CPU
and the GPU a little, so use it for finding the bottleneck, not for measuring the final speed.

    adb logcat -d | grep "WWF: " | tail -12
