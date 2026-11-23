# Maid Frog 0.3.0 (Lismok), formerly Glass Frog

Noise reduction for voice. It removes the noise floor of the microphone and of the room without darkening the top of the voice.
Formats: VST3, AU (macOS), CLAP, Standalone. Mono or stereo. Built with JUCE 8.0.4 / CMake.

## How it works (classical DSP, no AI model)
- STFT (75 % overlap, sqrt-Hann, ~40 ms frame) with a decision-directed Wiener gain and a speech-presence probability per bin.
- Noise floor: it follows the minimum of the spectrum by itself, or you press **Learn Noise** on a pause (room tone) and it keeps that profile (saved with the project).
- **Air** lowers the reduction above ~2.5 kHz only while the voice is present, so the top of the voice and the sibilants stay open (this is what makes other plugins sound dark).
- **Room** estimates the late reverb tail and takes it out of the gaps and the endings of words.
- **Listen to Removed** plays only what is being taken away: if you hear the voice in it, lower Reduction or Sensitivity.
- Latency is fixed (reported to the host). Stereo is linked, so the image does not move.

## Controls
Low Cut (Hz, default 55: removes rumble below it, steep, no phase shift), Reduction (max dB, default 18), Sensitivity (how much counts as noise), Air (keeps the highs), Body (keeps the low harmonics of a deep voice), Room (off by default; delicate), Smooth (fewer artefacts vs faster), Adaptive, Power, Learn Noise, Clear Profile.

## Tests
`g++ -std=c++17 -O2 Tests/gf_tests.cpp -o gf_t && ./gf_t [out_dir]` runs offline checks on a synthetic voice with room noise, hum and hiss and writes demo wavs.

## Not verified
Nothing here has been run inside a DAW; only syntax checks against JUCE and the offline DSP tests.

**Adaptive**: on = the noise floor is followed continuously (good for noise that changes). Off = it uses only the Learn Noise profile and never changes it (cleaner and more stable if the noise is constant and you learned a good room tone).

## What changed vs 0.2.0
- **Low Cut** (new): rumble below 55 Hz is removed on its own, so the plugin no longer has to decide between rumble and voice down there.
- **Body** now protects from ~48 Hz (full at ~76 Hz) up to ~600 Hz: covers the fundamental of a very deep voice.
- **Air** now starts at 700 Hz at about half strength (full from 2.5 kHz): protects the mid and the consonants.
- It installs next to the 0.2.0 plugin: it has its own name (Maid Frog), its own plugin code and bundle ID, so both can sit in the same project to compare.
