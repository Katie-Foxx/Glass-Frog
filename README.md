# Glass Frog 0.1.0 (Lismok)

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
Reduction (max dB), Sensitivity (how much counts as noise), Air, Room, Smooth (fewer artefacts vs faster), Adaptive, Power, Learn Noise, Clear Profile.

## Tests
`g++ -std=c++17 -O2 Tests/gf_tests.cpp -o gf_t && ./gf_t [out_dir]` runs offline checks on a synthetic voice with room noise, hum and hiss and writes demo wavs.

## Not verified
Nothing here has been run inside a DAW; only syntax checks against JUCE and the offline DSP tests.
