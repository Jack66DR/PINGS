# PINGS

**manners! DSP** resonator, played from the keys. AU and VST3, macOS universal (Apple Silicon and Intel).

PINGS is Mutable Instruments' **Rings**, by Émilie Gillet, made into a keyboard instrument. The sound is the
module's own DSP code, unmodified, running at its native 48 kHz in its native 24-sample blocks. What PINGS adds is
everything around it: MIDI strums the voices, chords are strummed note by note, a pitch wheel bends it, and
the screen drops a ring in a pond for every strike.

Strike it from the keys and it plays itself with its internal exciter. Or switch EXCITER to INPUT, feed it audio
from a side chain, and the keys just choose the notes while the input does the striking.

## Credit and licence

The Rings DSP and the parts of stmlib it needs are in `ThirdParty/mutable`, copied unmodified from
[pichenettes/eurorack](https://github.com/pichenettes/eurorack) and
[pichenettes/stmlib](https://github.com/pichenettes/stmlib) (commits in `ThirdParty/mutable/SOURCE_COMMITS.txt`).
They are copyright Emilie Gillet and released under the MIT licence; the notice is in
`ThirdParty/mutable/LICENSE` and at the top of each file, and stays with them.

PINGS is an independent project, not made or endorsed by Mutable Instruments. The name is a nod, not a claim.

## Build

GitHub Actions builds it on every push to `main` (Actions tab, "Build PINGS"). It also runs the offline engine test and Apple's `auval` check.
Download the `PINGS-macOS` artifact from the finished run. Inside are `PINGS-AU.zip` and `PINGS-VST3.zip`.

## Install

```sh
sudo unzip -o PINGS-AU.zip -d /Library/Audio/Plug-Ins/Components/
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/PINGS.component
```

Then in Logic: Plug-in Manager, find manners! DSP > PINGS, and Reset & Rescan Selection if it doesn't appear.

## Using it in Logic

PINGS is a MIDI-controlled effect (an `aumf` Audio Unit), so it can take MIDI and audio at once, like SYMPATHY.

1. Make a new **Software Instrument** track.
2. In its **Instrument** slot, choose **AU MIDI-controlled Effects > manners! DSP > PINGS**.
3. Play. With EXCITER on INTERNAL, that's all you need.
4. To excite it with audio: open the **Side Chain** menu in the plug-in window's header, pick a track or bus, and set EXCITER to INPUT.

No keyboard handy? Click the keys on the screen.

## Controls

| Control | What it does |
|---|---|
| **RESONATOR** | Rings' five knobs. |
| FREQUENCY | Transposes everything, two octaves either way, in semitones. |
| STRUCTURE | Depends on the model: the partials' ratios (MODAL), the strings' tuning ratios (SYMPATHETIC), inharmonicity (INHARMONIC), the FM ratio (FM VOICE), or which chord (CHORDS and DISASTROUS PEACE). |
| BRIGHTNESS | Wood and nylon at the bottom, glass and steel at the top. |
| DAMPING | Ring time, from a short tap to about ten seconds. In Disastrous Peace, decay then attack; fully up, it drones. |
| POSITION | Where the string or surface is struck. In Disastrous Peace, the amount of FX. |
| STRUM | The gap between the notes of a chord. Rings needs a few milliseconds between strums (6 ms minimum), so chords always roll a little; turn it up for a lazy hand. |
| **MODEL** | |
| MODAL | Each resonant mode a bandpass filter: plates, bells, tubes, glass. |
| SYMPATHETIC | A bank of strings that ring in sympathy, sitar style. |
| INHARMONIC | Karplus-Strong with delay modulation and allpass dispersion: clangy, detuned, metallic. |
| FM VOICE | Rings' hidden two-operator FM voice (the same FM as Plaits). |
| CHORDS | Rings' hidden "western chords": the sympathetic strings tuned to a chord picked by STRUCTURE. |
| KARPLUSVERB | Rings' hidden string-and-reverb model: the reverb decays with the string. |
| DISASTROUS PEACE | Rings' easter egg, out in the open: an organ and string machine loosely after the Roland RS-09. VOICES sets chord size, PEACE FX picks formant, chorus, reverb, a second formant, ensemble or a second reverb. |
| VOICES | 1, 2, 3 or 4. Each new strike takes the next voice, so earlier notes keep ringing. With one voice, left and right are the module's two pickups; with more, odd voices go left and even right, like its ODD and EVEN outputs. Three voices strike in the module's own pattern. |
| **IN AND OUT** | |
| EXCITER | INTERNAL: PINGS strikes itself on every note. INPUT: the side-chain audio excites it, through the module's noise gate. |
| ONSET STRUM | With EXCITER on INPUT, hits in the audio strum the voices too, the way the module does with only its input patched. |
| IN GAIN | Level into the resonator. |
| DRY | The input, untouched. Off by default. |
| OUT | Everything. |

Note names follow Logic: MIDI 60 is C3. Rings has no note-off: notes ring for as long as DAMPING says, whatever your fingers do.

## Screen

Cyan phosphor on cold black, with magenta for anything struck. Every strike drops a ring in the pond, placed by
pitch, low on the left and high on the right; it flashes magenta and cools to cyan as it spreads. The four lamps
show which voice took the strike. A scope runs along the bottom.

## Local tools

Configure with `-DPNG_BUILD_TESTS=ON` to also build:

- `PINGS_Test`: the offline engine checks (CI runs this): pitch at 44.1, 48 and 96 kHz, every model, chords, the input exciter, onset strums, safety, CPU.
- `PINGS_Tool render out.wav [modal|strings|peace|fed]`: a little performance through the plugin, at 44.1 kHz.
- `PINGS_Tool snapshot out.png [crt|peace|boot]`: renders the editor.

The terminal font is Share Tech Mono, under the SIL Open Font Licence (`Resources/ShareTechMono-OFL.txt`).
