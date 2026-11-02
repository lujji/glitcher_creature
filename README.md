# glitcher_creature
rp2350 glitcher

## dependencies
Install [pico-sdk](https://github.com/raspberrypi/pico-sdk). Add it to your PATH or pass using `-DPICO_SDK_PATH=` during build.

## usage
TBD

## building
### configuration
* **config.h** - pin configuration
* **ucb_cdc.h** - uart bridge config

### build and flash
```
mkdir build && cd build
cmake ..
make -j
make program
```
