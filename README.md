gmsynth.lv2 - General MIDI Synth
================================

gmsynth.lv2 is a General MIDI Sample Player Plugin.

Install
-------

Compiling gmsynth requires the LV2 SDK, gnu-make, a c++-compiler.

```bash
git clone https://github.com/x42/gmsynth.lv2.git
cd gmsynth.lv2
make

### Either install system-wide (for all users, requires
### admin permissions, may interfere with distro versions)
sudo make install PREFIX=/usr

### or *alternatively* install in your user's $HOME.
### either by symbolic linking from the source-tree:
# mkdir -p ~/.lv2; ln -s "$(pwd)/build" ~/.lv2/gmsynth.lv2
### or by copying:
make install LV2DIR=~/.lv2
```

Note to packagers: the Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CXXFLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CXXFLAGS`), also
see the first 10 lines of the Makefile.
