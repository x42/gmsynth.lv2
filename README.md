gmsynth.lv2 - General MIDI Synth
================================

gmsynth.lv2 is a General MIDI Sample Player Plugin.

Install
-------

Compiling gmsynth requires the LV2 SDK, gnu-make, a c++-compiler.

```bash
git clone git://github.com/x42/gmsynth.lv2.git
cd gmsynth.lv2
make

### install with either of the following:
# sudo make install PREFIX=/usr
# mkdir -p ~/.lv2; ln -s "$(pwd)/build" ~/.lv2/gmsynth.lv2
mkdir -p ~/.lv2; cp -a build ~/.lv2/gmsynth.lv2
```

Note to packagers: the Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CXXLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CXXFLAGS`), also
see the first 10 lines of the Makefile.
