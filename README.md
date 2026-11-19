# Rezalica

**Rezalica** je VST3 sampler za Windows (radi i kao samostalna aplikacija) u kojem režeš samplove na 64 pada (4 banke × 16) i sviraš ih MIDI kontrolerom ili tipkovnicom računala. Ima i jednostavan sekvencer u zasebnom prozoru. Funkcije su inspirirane klasičnim hardverskim phrase samplerima.

## Mogućnosti

- **4 banke (A–D) × 16 padova = 64 pada.** Svaki pad ima svoj sample ili isječak (slice) zajedničkog samplea.
- **Učitavanje**: `LOAD SAMPLE` ili povlačenje datoteke na valni oblik stavlja cijeli sample na odabrani pad, bez rezanja.
- **Rezanje (chop)**: tek kad odabereš mod (**2, 4, 8 ili 16 jednakih dijelova**, **Transients** ili **Manual**), sample se izreže, a pri svakoj promjeni moda automatski se ponovno izreže. **Off** vraća cijeli sample na A1.
  - U modu **Transients** kliznik **Sens** određuje osjetljivost: što je veća, to je više isječaka (do **64**).
  - Isječci 1–16 idu u banku A, 17–32 u banku B, 33–48 u C i 49–64 u D. Banku biraš gumbima **A B C D** iznad padova.
  - **Manual**: chop markere postavljaš sam. **Shift+klik** (ili dvoklik) na valni oblik dodaje marker, povlačenjem ga pomičeš, a **dvoklik na marker** (ili desni klik) ga briše. Markeri se sami poravnaju na najbliži prolaz kroz nulu, da ne bude klikova. Ako je sample prije bio izrezan u drugom modu (npr. Transients), taj se mod poništava, svi njegovi markeri se uklone i prelazi se u Manual. Isto vrijedi kad iz izbornika odabereš Manual.
  - Brisanje dvoklikom radi i na markerima iz moda Transients: ostali markeri ostaju, a mod prelazi u Manual.
  - **LAZY CHOP**: odabrani sample svira od početka. Svaki udarac pada, tipke ili MIDI note postavlja rez na mjestu playheada (crvena linija), a isječci redom pune A1, A2, A3… Završava sam kad sample dođe do kraja ili kad ponovno klikneš LAZY CHOP.
- **Horizontalni zoom** valnog oblika, sve do pojedinačnih uzoraka (16 uzoraka preko cijelog prozora):
  - kotačić miša zumira oko pokazivača, a Shift + kotačić ili traka ispod valnog oblika pomiče prikaz
  - kliznik ZOOM gore desno; dvoklik na njega prikazuje cijeli sample
  - kod jakog zooma vidi se svaki uzorak, pa markere možeš postaviti točno na uzorak
- Kad sviraš MIDI kontrolerom ili tipkovnicom, valni oblik prati pad koji si zadnji udario.
- **Valni oblik**: vidiš sample, sve isječke i markere početka/kraja. Markere povlačiš mišem (Alt+klik pomiče najbliži start/end marker), a klik na isječak odabire njegov pad.
- **Time Stretch / Warp** (gumbi `WARP` i `STRETCH...` iznad kontrola pada). Nikad se ne uključuje sam, nego samo kad ga ti uključiš.
  - `STRETCH...` otvara zaseban prozor sa svim opcijama:
    - **Mode**: **Beats** za bubnjeve i perkusivne loopove (svaki udarac ostaje oštar i svira originalnom brzinom, mijenja se samo razmak) ili **Complex** za melodijski i miješani materijal (phase vocoder s phase lockingom, glatkiji zvuk).
    - **Length**: 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars ili 16 bars.
    - **Tempo**: prati tempo DAW-a (ako se tempo promijeni, sample se sam ponovno rastegne) ili ručni BPM.
    - **Apply to**: cijeli sample (svi njegovi isječci i padovi ga prate) ili samo odabrani pad.
    - **Beats opcije**: Preserve (Transients, 1/8, 1/16), Transient loop (Off, Loop Forward, Loop Back-and-Forth), Transient envelope.
  - Rastezanje se računa u pozadini, pa plugin ne zastaje (dok traje, gumb pokazuje `WARP...`).
  - Visina tona se ne mijenja. Original ostaje netaknut, a `WARP OFF` ga vraća.
  - Rastegnuti sample sprema se u pozadini kao WAV u `Dokumenti/Rezalica/Warped`, a projekt ga pri otvaranju učita izravno.
- **Postavke po padu**: Gain, Pan, Pitch (±24 polutona), Attack, Release, Start, End.
- **Modovi pada**: `LOOP` (dio između markera vrti se ukrug dok držiš pad, tipku ili MIDI notu, a staje kad pustiš), `REVERSE`, `GATE` (svira samo dok držiš), `MUTE`.
- **TRIM** (truncate): od samplea pada ostaje samo narančasti dio između markera. Taj dio postaje novi sample na tom padu i sprema se kao WAV u `Dokumenti/Rezalica/Trimmed`, a ostali padovi zadržavaju originalni sample.
- **Efekti su po padu.** Svaki pad ima svoj lanac efekata sa svojim postavkama. Donji panel uvijek uređuje **odabrani pad** (piše „EFFECTS FOR PAD A3”), pa npr. Reverb na snareu ne utječe na kick. Padovi s uključenim efektom imaju oznaku **FX**.
  - `COPY FX` / `PASTE FX` kopiraju efekte s jednog pada na drugi, a `FX TO BANK` ih kopira na svih 16 padova trenutne banke.
  - **Filter**: analogni ladder filtar (Moog-style) s tipovima LP 24, LP 12, HP 24, HP 12, BP 24 i BP 12 te kontrolama Cutoff i Resonance
  - **Saturation**: tri blaga tipa analogne saturacije, svaki sa svojim kotačićem (0 = isključeno). Radi s 2× oversamplingom i automatskom kompenzacijom glasnoće.
    - **Tube**: topla, uglavnom parni harmonici
    - **Tape**: meka kompresija i blago zaobljeni visoki tonovi
    - **Solid State**: čvršće tranzistorsko rezanje, neparni harmonici
  - **Delay**: slobodno vrijeme u ms ili sinkronizirano s tempom (1/4, 1/8, 1/8 s točkom, 1/16, triole)
  - **Reverb**: Size, Damp, Width, Mix
- **Sekvencer** (gumb `SEQUENCER` otvara zaseban prozor):
  - 8 patterna, duljina 4–32 koraka (šesnaestinke), swing
  - gumbi A–D prikazuju redove banke koju uređuješ, a pattern svira svih 64 padova
  - `SYNC HOST`: prati transport i tempo DAW-a, a bez toga ima vlastiti BPM i `PLAY`
  - `REC`: snima ono što sviraš na padovima izravno u pattern
  - Shift+klik dodaje naglašeni korak, desni klik ili povlačenje briše
- Sve se sprema s projektom u DAW-u: padovi, postavke i patterni. Za samplove se pamti putanja do datoteke.

## Sviranje

| Banka | MIDI note |
|---|---|
| A (A1–A16) | C1–D#2 (36–51) |
| B (B1–B16) | E2–G3 (52–67) |
| C (C1–C16) | G#3–B4 (68–83) |
| D (D1–D16) | C5–D#6 (84–99) |

Tipkovnica računala svira trenutno odabranu banku:

| Pad | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Tipka | 1 | 2 | 3 | 4 | Q | W | E | R | A | S | D | F | Z | X | C | V |

- `Space` pokreće i zaustavlja interni sekvencer, a `Esc` zaustavlja sve.
- Desni klik na pad otvara izbornik: učitaj sample, obriši pad, **Copy pad / Paste pad** (kopira sample, markere, sve postavke i efekte na drugi pad) i izreži sample tog pada.
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
| `Source/FxChain.h` | Ladder filter, saturacija (tube/tape/solid state), Delay, Reverb |
| `Source/PluginProcessor.*` | Glavni procesor: MIDI, rezanje, detekcija tranzijenata, spremanje stanja |
| `Source/PluginEditor.*` | Glavni prozor |
| `Source/WaveformView.*`, `PadButton.h` | Valni oblik i padovi |
| `Source/SequencerWindow.*` | Prozor sekvencera |
| `Source/TimeStretch.h` | Time stretch algoritmi: Beats i Complex (phase vocoder) |
| `Source/StretchWindow.*` | Prozor Time Stretch / Warp |

## Licenca

Rezalica koristi [JUCE](https://juce.com). JUCE 8 je dostupan pod licencom AGPLv3 ili pod komercijalnom JUCE licencom.
