# Building

The decomp toolchain was created for Linux & MacOS. While it isn't a hard requirement, if you're running Windows its
advised that you [setup WSL](https://learn.microsoft.com/en-us/windows/wsl/install) with an Ubuntu-like distro for the
easiest setup.
The setup should take less than 10 minutes if you follow the instructions below, except for dumping the game's
executable, which may take longer depending on your familiarity with homebrew and dumping games.

## For Windows users

If you're using Windows, it's highly recommended to use WSL (Windows Subsystem for Linux) with an Ubuntu-like
distribution for the best compatibility.
Follow the [official Microsoft guide](https://learn.microsoft.com/en-us/windows/wsl/install) to set up WSL on your
system, or just run `wsl --install` in an elevated PowerShell terminal if you're on Windows 11.
Once WSL is installed, you can follow the Linux instructions below within your WSL environment.
Now you can go on with the steps below.

## 1. Set up dependencies

* Python 3.6 or newer with [pip](https://pip.pypa.io/en/stable/installation/)
* Ninja
* CMake 3.13+
    * If you are on Ubuntu 18.04, you must
      first [update CMake by using the official CMake APT repository](https://apt.kitware.com/).
* ccache (to speed up builds)
* llvm-objdump

Ubuntu users can install those dependencies by running:

```shell
sudo apt install python3 ninja-build cmake ccache libssl-dev libncurses5 llvm
```

If you are running Ubuntu 23.10 or later, the `libncurses5` package won't be available anymore. You can install it from
the archive using:

```shell
wget http://archive.ubuntu.com/ubuntu/pool/universe/n/ncurses/libtinfo5_6.3-2_amd64.deb && sudo dpkg -i libtinfo5_6.3-2_amd64.deb && rm -f libtinfo5_6.3-2_amd64.deb
```

For other systems, please check for the corresponding `libncurses5` backport, for
example [ncurses5-compat-libs](https://aur.archlinux.org/packages/ncurses5-compat-libs) for Arch-based distributions.

Additionally, you'll also need:

* The following Python modules that can be installed with the following command:
  `pip install capstone colorama cxxfilt pyelftools ansiwrap watchdog python-Levenshtein toml pyyaml`

## 2. Set up the project

1. Clone this repository. If you are using WSL, please clone the repo *inside* WSL, *not* on the Windows side (for performance reasons).
   Run `git clone --recurse-submodules https://github.com/Nintendocustom/Lego-City-Undercover-Decompilation.git`

2. Acquire the **original v1.0.3 EU `main` NSO executable** using
   the [Dumping guide](https://zeldamods.org/wiki/Help:Dumping_games#Switch).
3. Run `python3 tools/setup.py [path to the NSO]`
    * This will:
        * install tools/check to check for differences in decompiled code
        * convert the executable if necessary
      * set up [Clang 3.9.1](https://releases.llvm.org/download.html#3.9.1)
        * create a build directory in `build/`
    * If something goes wrong, follow the instructions given to you by the script.
    * If you wish to use a CMake generator that isn't Ninja, use `--cmake_backend` to specify it.

## 3. Build

To start the build, just run

```shell
python3 tools/build.py
```

By default, a multithreaded build is performed.

To check whether everything built correctly, just run `tools/check` after the build completes.

You can check the current progress with `python3 tools/common/progress.py`.

## Host and browser builds

The matching Switch build is unchanged unless you set `LCU_PLATFORM`.

Native host (software renderer, no game dump required):

```shell
cmake -S . -B build-host -DLCU_PLATFORM=host
cmake --build build-host
./build-host/lcuswitch --test
./build-host/lcuswitch --frames 180 --shot /tmp/lcu.ppm
./build-host/lcuswitch --mount /path/to/your/legally-dumped/assets --frames 240 --shot /tmp/lcu.ppm
```

Browser build (Emscripten). Point it at a directory of your own `.obj`, `.ghg`, or `.gsc` files. Nothing in this repo is a game asset.

```shell
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DLCU_PLATFORM=wasm
cmake --build build-wasm
python3 web/serve.py --directory build-wasm 8080
```

Open `http://127.0.0.1:8080/`. WASD moves, Space jumps, the mouse looks, Enter pauses. Drop a folder to stream meshes one file at a time. Saves go to `localStorage`.

To publish the same build on jsDelivr, pack it into 16KB chunks and push `web/dist`:

```shell
python3 tools/pack_jsdelivr.py --js build-wasm/lcu.js --wasm build-wasm/lcu.wasm --out web/dist
```

`web/play.html` then loads `manifest.json`, the runtime chunks, and the wasm chunks from whatever host served the page (jsDelivr included). Add `?cdn=1` to force `https://cdn.jsdelivr.net/gh/bitball41/Lego-City-Undercover-Decomp-WEBPORTilation@cursor/web-port-runtime-c6b2/web/dist/`. `?ref=` and `?repo=` override that pin. `?dist=` is a full base URL.

## 4. Decompilers

For instructions on setting up a decompiler, see [DECOMPILERS.md](DECOMPILERS.md).
