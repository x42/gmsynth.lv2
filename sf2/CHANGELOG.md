# GeneralUser GS Revision History

---

## 1.52

* **000:004 Tine Electric Piano**: Fixed buzzy loops and improved dynamics and tone.
* **000:041 Viola**: Improved note attack and tone across dynamic range. Fixed bad loops on samples 54, 77 and 82.

## 1.511

* Further tweaks to the balance of the drums.
* Further improvements to primary and secondary snares in the standard drum kit.

## 1.51

* All acoustic pianos and harp: improved stretch tuning and overall instrument intonation.
* Improved sample programming on standard snares and brushed snares.
* Tweaked the intonation of many instruments.
* Reduced the entire SoundFont volume by 2 dB to satisfy negative attenuation values that didn't have the dynamic headroom. The following instruments will still have a bit of attenuation when CC7 and velocity are at max:
  - **005:126 Footsteps**: 0.84 dB attenuation
  - **016:025 Mandolin**: 2.00 dB attenuation
* **000:011 Vibraphone**: improved loops for better pitch stability
* **000:041 Viola**: Restored original sample rate (variable, c. 30 KHz) instead of being downsampled to 22.05 KHz.
* **000:081 Saw Lead**: Improved with better attack and filter values, aiming to be more like the Saw Lead in the original Roland GM instruments.
* All **fast strings** presets: Improved note attack.
* All **acoustic guitar** presets: Improved decay envelope.
* Various fixes and tweaks.

## 1.5

* Major code cleanup, including :
  - Removed unnecessary modulators.
  - Moved duplicated data to the global zone.
  - Moved sample tuning info into the samples.
* Minor tweaks here and there.

## 1.472

* Fixed a problem in all presets using the FM electric piano samples whereby notes would sound incorrect at velocity = 105.

## 1.471

* Fixed missing chimes (C6) in Power and Orchestral drum kits.
* Fixed Marimba crashing MuseScore.

## 1.47

* Unified the FluidSynth and MuseScore versions into a single version now that MuseScore bug #72091 (https://musescore.org/node/72091) has been fixed. This version has also been verified (and slightly tweaked) to work with the latest SynthFont2, which now supports SoundFont 2.01 modulators! Requires SynthFont2 2.0.3.2 or later, or VSTSynthFont 1.081 or later.

## 1.46

* Fixed high-pitched squeal in Electric Grand
* Improved kicks and snares in Standard, Standard 2, Room, Power and Electronic drum kits
* Balanced C5 sample in the Bassoon to fit better with the adjacent samples
* Improved the Harpsichord and fixed weird loop buzz
* Improved the Tremolo Strings
* Slight tweak to attack on Accordion
* Improved clicking loops in Fretless Bass
* Fixed bad loops in all strings
* Fixed Recorder release (it was too short)
* Fixed weird attack and velocity->filter response in Chiffer Lead
* Fixed release in upper extreme range of Muted Trumpet
* Fixed flat loop in Acoustic Bass

## 1.45

* Tweaked velocity scaling in FluidSynth version to be less extreme between FF and PP playing.
* Fixed Slap Bass 1 & 2 release to avoid clicks in FluidSynth.

## 1.44

* New license
* Fixed loops in Concert Choir
* Improved Bagpipes
* FluidSynth/MuseScore version: improved programming of harpsichord, trumpet, trombone, muted trumpet, and pan flute.
* MuseScore version: adjusted balance of orchestral instruments
* MuseScore version: modified default drum kit with orchestral bass drum, snare and cymbals.

## 1.43

* Various improvements
* Re-worked strings in Live/Audigy version
* Separate versions for Live/Audigy, FluidSynth and other software synths
* Removed brand and product name references from presets

## 1.4

* Countless samples have been replaced, improved, cleaned, re-looped, etc.
* Just as many instruments have been reworked, rebalanced, etc.
* Improved overall instrument volume and balance
* Previous versions of GeneralUser GS only allowed 20% of the total reverb and chorus effects to be used (this is the default setting in the SoundFont specification).  Setting reverb (CC91) or chorus (CC93) to max value (127) now allows 100% of the effect to be heard.
* Added self-test mode (activated by playing "GUTest.mid")
* There are too many improvements in this bank to list, you will have to hear it for yourself :)

## 1.35

* Made compatible with Sound Blaster Audigy and EMU APS.
* Minor tweaks.

## 1.3

* Combined AWE32/64 and Live! versions into one bank compatible with both sound cards.
* Resampled Marimba.
* Standard snare now sounds much more realistic.
* Cymbals act appropriately when played soft, making decent-sounding cymbal rolls possible.
* Replaced Synth Brass 1, Square Lead and Sawtooth lead.  The old Square Lead and Sawtooth lead can be found in bank 12.
* Countless other modifications to instruments in the bank, including balance fixes.

## 1.2

Both versions:
* Adjusted piano 1 and 2 brightness.
* Adjusted tuning of Marimba -- attack still sounds strange in Live! version, can't figure out why.
* Fixed crackling in Clean Guitar (and variants).
* Increased volume of high Trumpet notes.
* Other small changes.

Live! version:
* Fixed Hi-hat cutoff bug that showed up courtesy of the Creative Labs driver.
* Fixed bad attack on Harpsichord sound.
* Fixed some more synth sounds that weren't right because the Live! driver screwed up the filter settings.

## 1.1

Both versions:
* Reworked the string ensemble using same samples.  Sounds more full now and is in stereo.
* Adjusted balance/envelopes of some woodwinds.
* Made orchestral snare velocity sensitive.
* Other small tweaks here and there.

Live! version:
* Fixed some synth sounds that weren't right because the Live! driver screwed up the filter settings.
