# orig/

This folder is empty on purpose. It holds the original game: the assembly source and the art of Midway's
*WWF WrestleMania: The Arcade Game*, as published in
[historicalsource/wwf-wrestlemania](https://github.com/historicalsource/wwf-wrestlemania). It is not part of this
repository; the port needs it to build and to run.

Get it with

```sh
tools/fetch_orig.sh
```

which downloads that repository into this folder (`orig/WRESTLE.CMD`, `orig/IMG/` and the rest). Nothing in here is
ever changed by the port. The folder's contents are ignored by git (`.gitignore`), so your copy is never committed.
