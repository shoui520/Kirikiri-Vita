# Kirikiri Vita

Kirikiri Vita is a clean PlayStation Vita runtime for legally owned retail
Kirikiri visual novels. Retail compatibility, including `xp3filter.tjs`, is the
primary design constraint.

The engine compatibility source is
[Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri). Its Android
and Cocos frontend is not used. The Vita frontend uses VitaGL, Vita controller
and touch APIs, OpenAL, the native Japanese PVF system font, and a small
direct-boot bubble helper.

## Current development commands

```sh
git submodule update --init --recursive
cmake -S . -B build-host -DKRKRVITA_BUILD_TESTS=ON
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
./build-host/krkrvita-tool scan /path/to/game
```

For Vita:

```sh
VITASDK=/home/shoui/vitasdk ./scripts/build-vita.sh
```

See `docs/ARCHITECTURE.md` and `docs/MILESTONE-1.md` for supported and pending
functionality. The patch library is fetched at runtime and is not redistributed
by this repository.
