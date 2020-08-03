# Super Mario 64 Nintendo 3DS Port

This repo does **not** include all assets necessary for compiling the game.
A prior copy of the game is required to extract the assets.

## Changes vs. vanilla 3DS port

 - Based off [Refresh 11](https://github.com/sm64-port/sm64-port/commit/9214dddabcce4723d9b6cda2ebccbac209f6447d)
 - Configurable controls via `sm64config.txt`
 - GFX_POOL_SIZE [fix](https://github.com/aboood40091/sm64-port/commit/6ae4f4687ed234291ac1e572b75d65191ca9f364) (support 60 FPS on 32bit platforms)
 - Mini-menu (tap touch-screen to trigger)
     - Enable/disable AA
     - Enable/disable 800px mode
     - Exit the game
 - Experimental Stereo 3D support; add build flag `ENABLE_N3DS_3D_MODE=1` to try it out

## Building

After building, either install the `.cia` if you made one, or copy over the `sm64.us.f3dex2e.3dsx` into the `/3ds` directory on your SD card and load via The Homebrew Launcher.

### Docker

**Clone Repository:**

```sh
git clone https://github.com/mkst/sm64-port.git
```

**Nagivate into freshly checked out repo:**

```sh
cd sm64-port
```

**Copy in baserom.XX.z64:**

```sh
cp /path/to/your/baserom.XX.z64 .
```

**Checkout this branch:**

```sh
git checkout 3ds-port
```

**Build with prebaked image:**

Change `VERSION=us` if applicable.
```sh
docker run --rm --mount type=bind,source="$(pwd)",destination=/sm64  \
  markstreet/sm64:3ds \
  make TARGET_N3DS=1 VERSION=us --jobs 4
```

**Create .cia from .3dsx (Optional):**

```sh
docker run --rm --mount type=bind,source="$(pwd)",destination=/data \
  markstreet/3dstools:0.1 \
  sh -c "cxitool build/us_3ds/sm64.us.f3dex2e.3dsx sm64.cxi && \
  makerom -f cia -o sm64.cia -target t -i sm64.cxi:0:0 -ignoresign"
```

### Linux / WSL (Ubuntu)

```sh
sudo su -

apt-get update && \
    apt-get install -y \
        binutils-mips-linux-gnu \
        bsdmainutils \
        build-essential \
        libaudiofile-dev \
        pkg-config \
        python3 \
        wget \
        zlib1g-dev

wget https://github.com/devkitPro/pacman/releases/download/v1.0.2/devkitpro-pacman.amd64.deb \
  -O devkitpro.deb && \
  echo ebc9f199da9a685e5264c87578efe29309d5d90f44f99f3dad9dcd96323fece3 devkitpro.deb | sha256sum --check && \
  apt install -y ./devkitpro.deb && \
  rm devkitpro.deb

dkp-pacman -Syu 3ds-dev --noconfirm
# if this ^^ fails with error about archive format, use a VPN to get yourself out of the USA and then try again.

exit

cd

git clone https://github.com/mkst/sm64-port.git

cd sm64-port

# go and copy the baserom to c:\temp (create that directory in windows explorer)
cp /mnt/c/temp/baserom.us.z64 .

sudo chmod 644 ./baserom.us.z64

export PATH="/opt/devkitpro/tools/bin/:~/sm64-port/tools:${PATH}"
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export DEVKITPPC=/opt/devkitpro/devkitPPC

make -j4
```

### Other

TBD; feel free to submit a PR.

## Project Structure

```
sm64
├── actors: object behaviors, geo layout, and display lists
├── asm: handwritten assembly code, rom header
│   └── non_matchings: asm for non-matching sections
├── assets: animation and demo data
│   ├── anims: animation data
│   └── demos: demo data
├── bin: C files for ordering display lists and textures
├── build: output directory
├── data: behavior scripts, misc. data
├── doxygen: documentation infrastructure
├── enhancements: example source modifications
├── include: header files
├── levels: level scripts, geo layout, and display lists
├── lib: SDK library code
├── rsp: audio and Fast3D RSP assembly code
├── sound: sequences, sound samples, and sound banks
├── src: C source code for game
│   ├── audio: audio code
│   ├── buffers: stacks, heaps, and task buffers
│   ├── engine: script processing engines and utils
│   ├── game: behaviors and rest of game source
│   ├── goddard: Mario intro screen
│   ├── menu: title screen and file, act, and debug level selection menus
│   └── pc: port code, audio and video renderer
├── text: dialog, level names, act names
├── textures: skybox and generic texture data
└── tools: build tools
```

## Credits

 - Credits go to [Gericom](https://github.com/Gericom) for the [sm64_3ds](https://github.com/sm64-port/sm64_3ds) port that this flavour is based off.

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
