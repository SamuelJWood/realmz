# Realmz

Realmz is a classic, turn-based RPG, originally developed for early Macintosh computers. It was originally released as shareware, with additional scenarios available for purchase. Tim Phillips has graciously agreed to a release of the original code under a non-commercial license (see "License" section below).

# This Fork

Here are the main changes from the upstream version of Realmz (realmz-castle/realmz):

-Graphics
    -Fullscreen mode has been added.
    -The window can be resized arbitrarily.
    -Maps shown in game are now centered.
    -The overhead dungeon view is centered.
    -The overhead dungeon view no longer causes flickering.
    -Animations showing damage to party members when outside of combat are now visible over the characters' portraits as in the original Realmz.
    -Closing various screens/dialogs no longer causes buttons and selected player character indicator to flicker.

-Music
    -All original music files play in game (the Outdoor Music file was converted from a Mac only tracker format to MOD)
    -MP3 can also be used
    -The game will randomly select tracks to play if there are multiple similarly named files (e.g., "Outdoor Music", "Outdoor Music 2.mp3" "Outdoor Music files.mod").

-Scenario Bugfixes
    -All scenarios have had spelling mistakes fixed (hundreds of spelling mistakes in total).
    -Prelude to Pestilence's bugs with missing items in the potion shop, errors of allegiance, etc. have been fixed.

-User interface
    -A new menu system has been created that shows scenario icons, descriptions of Caste and Race, etc.
    -The "Esc" key can be used instead of "A" to Abort targeting.
    -Most screens can be closed by pressing "Esc" - Items screen, Encounter, Trade, etc.
    -The anomalous sound bug when moving the cursor over the right side of the screen in the after battle rewards screen has been fixed.
    -Right clicking on characters in combat will show their information.
    -Pressing "T" to target in combat will attempt to automatically switch to a ranged weapon first.

# License

<p xmlns:cc="http://creativecommons.org/ns#">Realmz, copyright © 1994 by Tim Phillips. Realmz and its associated software, in both source code and binary formats, its game assets, and its documentation (the Licensed Material), are distributed under the terms of the <a href="https://creativecommons.org/licenses/by-nc-sa/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/nc.svg?ref=chooser-v1" alt=""><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/sa.svg?ref=chooser-v1" alt=""></a>. The Licensed Material is provided on an as-is basis, with no warranties of any kind.</p>

# Building for Windows from WSL2

These steps produce a Windows Release build (`.zip`) from WSL2 (Ubuntu 22.04 or later).

## 1. Install prerequisites

```bash
sudo apt-get update
sudo apt-get install -y cmake make git wget
```

## 2. Install llvm-mingw

Download the Ubuntu x86_64 release from [llvm-mingw releases](https://github.com/mstorsjo/llvm-mingw/releases) and install to `/opt/llvm-mingw`:

```bash
wget https://github.com/mstorsjo/llvm-mingw/releases/download/20260505/llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64.tar.xz
tar xf llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64.tar.xz
sudo mv llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64 /opt/llvm-mingw
export PATH=/opt/llvm-mingw/bin:$PATH
```

Add the `export PATH` line to your `~/.bashrc` or `~/.zshrc` to make it permanent.

## 3. Cross-compile and install zlib

```bash
git clone https://github.com/madler/zlib.git && cd zlib
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/realmz/cmake/TC-mingw.cmake \
  -DCMAKE_INSTALL_PREFIX=~/mingw-install \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build && cmake --install build
cd ..
```

## 4. Cross-compile and install phosg and resource_dasm

Use the same commits as the Mac build. Pass the toolchain file and install prefix to each:

```bash
# phosg (commit b2e0c12edb7e274a5e20c460f44eee44f49f57ef)
git clone https://github.com/fuzziqersoftware/phosg.git && cd phosg
git checkout b2e0c12edb7e274a5e20c460f44eee44f49f57ef
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/realmz/cmake/TC-mingw.cmake \
  -DCMAKE_INSTALL_PREFIX=~/mingw-install \
  -DCMAKE_BUILD_TYPE=Release \
  -DZLIB_LIBRARY=~/mingw-install/lib/libz.dll.a \
  -DZLIB_INCLUDE_DIR=~/mingw-install/include
cmake --build build && cmake --install build
cd ..

# resource_dasm (commit 27f64c89a5fed855e68c2a5e97b6c6c389d8eb19)
git clone https://github.com/fuzziqersoftware/resource_dasm.git && cd resource_dasm
git checkout 27f64c89a5fed855e68c2a5e97b6c6c389d8eb19
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/realmz/cmake/TC-mingw.cmake \
  -DCMAKE_INSTALL_PREFIX=~/mingw-install \
  -DCMAKE_BUILD_TYPE=Release \
  -Dphosg_DIR=~/mingw-install/lib/cmake/phosg \
  -DZLIB_LIBRARY=~/mingw-install/lib/libz.dll.a \
  -DZLIB_INCLUDE_DIR=~/mingw-install/include
cmake --build build && cmake --install build
cd ..
```

## 5. Clone the repo and fetch submodules

```bash
git clone https://github.com/SamuelJWood/realmz.git && cd realmz
git submodule update --init
vendored/SDL_ttf/external/download.sh
```

## 6. Configure and build

```bash
cmake --preset Windows
cmake --build --preset Windows --parallel $(nproc)
```

## 7. Package as a ZIP

```bash
cd build_win && cpack -G ZIP
```

The resulting `.zip` file in `build_win/` contains everything needed to run Realmz on Windows — no installer required.
