# Rezalica

**Rezalica** je VST3 sampler za Windows (radi i kao samostalna aplikacija) u kojem režeš samplove na 16 padova i sviraš ih MIDI kontrolerom ili tipkovnicom računala. Ima i jednostavan sekvencer u zasebnom prozoru. Funkcije su inspirirane klasičnim hardverskim phrase samplerima.

## Mogućnosti

- **16 padova** (4×4). Svaki pad ima svoj sample ili isječak (slice) zajedničkog samplea.
- **Rezanje (chop)**: učitaj zvuk i automatski ga izreži na **4, 8 ili 16 jednakih dijelova** ili **po tranzijentima** (udarcima).
- **Valni oblik**: vidiš sample, sve isječke i markere početka/kraja. Markere povlačiš mišem (Shift+klik pomiče najbliži marker), a klik na isječak odabire njegov pad.
- **Postavke po padu**: Gain, Pan, Pitch (±24 polutona), Attack, Release, Start, End.
- **Modovi pada**: `LOOP`, `REVERSE`, `GATE` (svira samo dok držiš), `FX` (šalje pad kroz efekte), `MUTE`.
  - Loop bez Gatea radi kao prekidač: prvi udarac pokreće, drugi zaustavlja.
- **Efekti** (svi se mogu automatizirati u DAW-u):
  - **Filter + Drive**: niskopropusni filtar s rezonancijom i saturacijom
  - **Lo-Fi / Vinyl**: bit-crusher, smanjenje sample ratea i pucketanje ploče
  - **Delay**: slobodno vrijeme u ms ili sinkronizirano s tempom (1/4, 1/8, 1/8 s točkom, 1/16, triole)
- **Sekvencer** (gumb `SEQUENCER` otvara zaseban prozor):
  - 8 patterna, duljina 4–32 koraka (šesnaestinke), swing
  - `SYNC HOST`: prati transport i tempo DAW-a, a bez toga ima vlastiti BPM i `PLAY`
  - `REC`: snima ono što sviraš na padovima izravno u pattern
  - Shift+klik dodaje naglašeni korak, desni klik ili povlačenje briše
- Sve se sprema s projektom u DAW-u: padovi, postavke i patterni. Za samplove se pamti putanja do datoteke.

## Sviranje

| Pad | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MIDI nota | C1 (36) | C#1 | D1 | D#1 | E1 | F1 | F#1 | G1 | G#1 | A1 | A#1 | B1 | C2 | C#2 | D2 | D#2 (51) |
| Tipka | 1 | 2 | 3 | 4 | Q | W | E | R | A | S | D | F | Z | X | C | V |

- `Space` pokreće i zaustavlja interni sekvencer, a `Esc` zaustavlja sve.
- Desni klik na pad otvara izbornik: učitaj sample, obriši pad, izreži sample tog pada.
- Audio datoteku možeš povući i ispustiti na pad (učitava se na taj pad) ili na valni oblik (reže se preko svih padova).
- **Ableton Live:** da bi tipkovnica računala svirala padove, klikni u prozor plugina. Ableton tipke šalje pluginu samo dok je njegov prozor u fokusu.

## Instalacija (Windows)

1. Na GitHubu otvori **Actions**, pa zadnji uspješni **Build**.
2. Pod **Artifacts** preuzmi `Rezalica-VST3-Windows`.
3. Raspakiraj i kopiraj mapu `Rezalica.vst3` u `C:\Program Files\Common Files\VST3\`.
4. U DAW-u ponovno skeniraj pluginove. Rezalica se pojavljuje kao instrument.

## Samostalna izrada (build)

Potrebni su CMake 3.22+ i Visual Studio 2022 s komponentom „Desktop development with C++”. JUCE se preuzima automatski.

```bash
cmake -B build
```

```bash
cmake --build build --config Release
```

Gotov plugin nalazi se u `build/Rezalica_artefacts/Release/VST3/Rezalica.vst3`.

Testovi (bez grafičkog sučelja):

```bash
cmake -B build -DREZALICA_BUILD_TESTS=ON
```

```bash
cmake --build build --config Release --target RezalicaTests
```

## Struktura koda

| Datoteka | Opis |
|---|---|
| `Source/SamplerEngine.*` | Glasovi, reprodukcija padova, loop, reverse, gate, envelope |
| `Source/Sequencer.h` | Step sekvencer (sinkronizacija s DAW-om, swing, snimanje) |
| `Source/FxChain.h` | Filter + Drive, Lo-Fi, Delay |
| `Source/PluginProcessor.*` | Glavni procesor: MIDI, rezanje, detekcija tranzijenata, spremanje stanja |
| `Source/PluginEditor.*` | Glavni prozor |
| `Source/WaveformView.*`, `PadButton.h` | Valni oblik i padovi |
| `Source/SequencerWindow.*` | Prozor sekvencera |

## Licenca

Rezalica koristi [JUCE](https://juce.com). JUCE 8 je dostupan pod licencom AGPLv3 ili pod komercijalnom JUCE licencom.
