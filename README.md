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
  - **LAZY CHOP**: kao na hardverskim samplerima – nakon klika čeka u tišini. Prvi udarac pada, tipke ili MIDI note pokreće odabrani sample od početka i tamo stavlja prvi marker; svaki sljedeći udarac postavlja rez na mjestu playheada (crvena linija), a isječci redom pune A1, A2, A3… Završava sam kad sample dođe do kraja ili kad ponovno klikneš LAZY CHOP.
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
  - **BOUNCE** (consolidate, prije RENDER): kad završiš s Time Stretchom i Warpom, BOUNCE zapisuje obrađeni sample kao novi WAV u `Dokumenti/Rezalica/Bounced` i automatski ga stavlja umjesto starog na sve padove koji ga koriste. Rezovi ostaju na istom mjestu.
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
- **Step sekvencer** (gumb `STEP SEQ` otvara zaseban prozor), uglavnom za perkusivne samplove:
  - 8 patterna, duljina 4–128 koraka (šesnaestinke), **swing posebno za svaki pattern** s rešetkom 1/16 ili 1/8
  - **CLICK** (metronom, desni klik: glasnoća) i **COUNT-IN** (1 ili 2 takta klikova prije snimanja kad je REC uključen) u step sekvenceru, piano rollu i glavnom prozoru
  - kod patterna duljih od 32 koraka prikaz ide po stranicama (1-32, 33-64, …), a `FOLLOW` prati playhead
  - gumbi A–D prikazuju redove banke koju uređuješ, a pattern svira svih 64 padova
  - `SYNC HOST`: prati transport i tempo DAW-a, a bez toga ima vlastiti BPM i `PLAY`
  - `REC`: snima ono što sviraš na padovima izravno u pattern
  - **Klikanje po ćeliji mijenja jačinu (velocity)**: 1. klik = puno (127), 2. = tiše (96), 3. = još tiše (64), 4. = najtiše (32), 5. = briše korak. Bijeli stupić u ćeliji pokazuje jačinu.
  - **Ctrl + klik** na ćeliju otvara kliznik koji pomiče udarac **ispred grida (ranije) ili iza grida (kasnije)**, do pola koraka (prikazuje i ms) te kliznik za točnu jačinu. Pomaknuti korak ima žutu oznaku.
  - Shift+klik = puna jačina, desni klik ili povlačenje briše
- Sve se sprema s projektom u DAW-u: padovi, postavke i patterni. Za samplove se pamti putanja do datoteke.

- **DRAG MIDI / DRAG WAV** (u step sekvenceru i u piano rollu): povuci gumb mišem u Ableton (ili drugi DAW / Explorer).
  - **DRAG MIDI** daje MIDI clip patterna: padovi su note C1 i dalje (kao što ih Rezalica svira), a melodijske note su na svojoj visini. Sadrži i tempo.
  - **DRAG WAV** renderira pattern u audio (točno jedan loop, s repovima efekata iz prethodnog prolaza, pa se savršeno ponavlja).
  - Step sekvencer izvozi korake, a piano roll svoje note. Običan klik pokazuje datoteku u mapi `Dokumenti/Rezalica/Export`.
- **Piano Roll** (gumb `PIANO ROLL` otvara zaseban prozor), za melodijske samplove i za bubnjeve:
  - Usklađen je sa step sekvencerom: ista reprodukcija, tempo, pattern i duljina. Oba sviraju zajedno.
  - **DRUMS**: redovi su 64 pada. **MELODIC**: redovi su tonovi, a odabrani pad svira kromatski (C3 = originalna visina, s akordima).
    - Obje vrste nota sviraju zajedno, a gumbi samo biraju koju uređuješ.
  - `PLAY WITH MIDI KEYS`: MIDI klavijatura svira odabrani sample po tonovima.
  - `REC` snima note s njihovom duljinom: padove u DRUMS, MIDI tipke u MELODIC.
  - **Olovka (DRAW)**: lijevi klik stavlja notu, a povlačenjem odmah određuješ duljinu. Povlačenjem ruba nota se rasteže.
  - **SELECT**: okvir za odabir više nota i pomicanje. Ctrl+povlačenje kopira.
  - **Desni klik** briše notu, a desni klik s povlačenjem briše sve note preko kojih prijeđeš.
  - Ostalo:
    - Velocity traka ispod nota (povuci gore-dolje)
    - Snap 1/4 … 1/32, triole, Off
    - QUANTIZE (Q), UNDO / REDO (Ctrl+Z / Ctrl+Y)
    - Ctrl+C / Ctrl+V / Ctrl+D (kopiraj, zalijepi, dupliciraj), Ctrl+A, Delete
    - Strelice: transpozicija (Shift = oktava) i pomicanje lijevo-desno
    - Kotačić: pomicanje, Ctrl + kotačić: zoom, Alt + kotačić: visina redova
    - Klik na klavijaturu preslušava ton, a `Space` pokreće i zaustavlja.
- **Warp markeri**, kao u Abletonu:
  - **Warp traka** je tamna traka na vrhu valnog oblika sa žutim dijamantima. Kad je miš u njoj, **obično povlačenje** (bez Ctrl) rasteže audio: uhvatiš dijamant ili povučeš s mjesta transient markera, pa se stvori novi warp marker. Dvoklik u traci dodaje warp marker, a dvoklik na dijamant ga briše. Ispod trake miš radi kao i prije.
  - Ctrl + povlačenje radi bilo gdje na valnom obliku.
  - Uhvati marker (transient ili ručni) u warp traci ili s Ctrl i povuci ga lijevo ili desno po gridu. Sample se između markera rasteže i skuplja tako da udarac padne na grid.
  - Grid je visoke rezolucije: s jačim zoomom ide sve do 1/64 doba. Alt isključuje snap.
  - Ako WARP nije uključen, Ctrl+klik ga sam uključi s duljinom koja najbolje odgovara sampleu.
  - Isječci (padovi) prate zvuk. Ctrl + dvoklik ili desni klik uklanja warp marker.

- **Banke A–D**:
  - **Ctrl + klik** na banku otvara vertikalni kliznik za glasnoću cijele banke (+6 dB do potpuno tiho). Žuta linija pokazuje trenutnu glasnoću, a dvoklik vraća 0 dB. Žuta crtica na gumbu banke također pokazuje glasnoću.
  - **Desni klik** na banku otvara izbornik: Mute bank, Unmute bank, Copy bank, Paste bank, Reset volume. Utišana banka je crvena i označena s „M”.
  - Paste bank kopira svih 16 padova (samplove kao samostalne kopije, postavke, efekte i glasnoću banke).
- **UNDO / REDO** (gumbi u gornjoj traci, Ctrl+Z / Ctrl+Y ili Ctrl+Shift+Z):
  - poništava i vraća zadnje radnje: učitavanje, rezanje, markere, Trim, Warp, warp markere, Render, Clear, Paste pada, kotačiće i gumbe pada te efekte
  - pamti zadnjih 40 koraka, a uputa na gumbu pokazuje što će se poništiti
- **Kopije su samostalne**: pad kopiran s Copy/Paste ili isti sample učitan na drugi pad dobiva vlastitu kopiju. Warp, Time Stretch i Render na jednom padu ne diraju njegove kopije. Isječci iz istog reza (Chop) i dalje se obrađuju zajedno.

## Pokretanje

Standalone sampler se uvijek pokreće prazan (bez samplova, koraka i nota). Rad se čuva u projektima (FILE → Save / Open Recent), a nakon rušenja nudi se vraćanje automatski spremljenog projekta.

Veliki prozor sampla prati i padove koje okidaju step sekvencer i piano roll: prikazuje chop koji upravo svira, ali odabrani pad i banka se ne mijenjaju (možeš raditi u banci B dok svira banka A). Kad zaustaviš, vraća se na odabrani pad.

**STEP SEQ**, **PIANO ROLL** i **SONG** otvaraju se unutar glavnog prozora (ispod prozora sampla i padova, preko cijele širine); isti gumb ih zatvara. Dok su otvoreni, knobovi pada i efekti su skriveni.

## Jedan tempo za cijeli sampler

Tempo (crveni LED BPM zaslon) je zajednički za glavni prozor, STEP SEQ, PIANO ROLL i SONG; svaki prozor ima svoj mali LED zaslon koji pokazuje i mijenja isti tempo. Warpani (time-stretch) samplovi s uključenim „FOLLOW SAMPLER TEMPO“ automatski se prilagode kad promijeniš tempo. U standalone verziji nema „SYNC HOST“ (nema DAW-a).

## Piano roll: odabir i velocity

- **Olovka** (gumb s ikonom): kad uđeš u Piano Roll, nijedan alat nije odabran. Uključi olovku za crtanje nota (tipka B);
  **Shift + povlačenje** označava note okvirom. **HUMANIZE** + klizač *Amount*: označene note (ili sve) malo pomakne s grida
  (najviše 1/4 koraka) i promijeni im velocity (najviše ±24), kao da svira čovjek. **COPY TO NEXT PATTERN** kopira pattern u sljedeći.

- **DRAW** alat ima pokazivač u obliku olovke.
- Klik na tipku klavijature lijevo (u MELODIC i DRUMS prikazu) odabire sve note tog reda; povlačenje po klavijaturi odabire sve note u redovima između (Shift / Ctrl: dodaje postojećem odabiru).
- Kad su note odabrane, traka **VELOCITY** mijenja jačinu svih odabranih nota odjednom.
- Novi patterni u step sekvenceru i piano rollu imaju po defaultu **32 koraka** (2 takta).

## Računalna tipkovnica u prozorima STEP SEQ i PIANO ROLL

- **Padovi**: 1 2 3 4 / Q W E R / A S D F / Z X C V (i u step sekvenceru i u piano rollu u DRUMS prikazu).
- **Note** (piano roll, MELODIC prikaz), kao u Abletonu na hrvatskoj tipkovnici: A W S E D F T G Z H U J K O L P = C … D#, **Y** = oktava dolje, **X** = oktava gore. Žuta traka na klavijaturi piano rolla pokazuje koje note tipke trenutno sviraju.
- Na hrvatskoj (QWERTZ) tipkovnici donji red za padove je Y X C V (Y i Z oba sviraju pad 13). Note sviraju odabrani pad kromatski (C3 = izvorna visina).
- Snimanje u piano roll: uključi **REC** (crveni krug), pritisni **PLAY** i sviraj tipkama, padovima ili MIDI klavijaturom. Udarac na pad koji piano roll svira (npr. A1) snima se kao nota C3.
- **PLAY WITH MIDI KEYS**: vanjska MIDI klavijatura svira odabrani pad kromatski. Standalone sam uključi sve MIDI ulaze pri pokretanju (ako ni jedan nije uključen u Audio Settings).

## Izgled

Veliki prozor za waveform izgleda kao ekran starog televizora (kremasti okvir, zaobljeno staklo s odsjajem i linijama). Sve tipke zaglavlja su u jednom redu s transportom. Prozori REZALICE (SAMPLING, efekti …) stoje iznad drugih samo dok je REZALICA aktivna. Sučelje izgleda kao hardverski sampler: brušena srebrna ploča, crni paneli s crvenim tiskom, sive gumene tipke s LED lampicom kad su uključene, crni knobovi s kremastim pokazivačem i crvenom skalom, kremasti gumeni padovi i crveni LED zaslon za tempo (BPM: povuci gore/dolje, Shift = fino, kotačić, dupli klik = upis). Isto je u standalone verziji i u VST pluginu.

## Skale i akordi (PIANO ROLL, MELODIC)

U MELODIC dijelu piano rolla, u gornjem redu: **tonalitet** (C … B) i **skala**, desno **TO SCALE**; u drugom redu **akord**.

- **Skale:** dur i molovi (prirodni, harmonijski, melodijski), modusi (dorski, frigijski, lidijski, miksolidijski, lokrijski,
  lidijski dominantni, alterirani), pentatonike i blues, balkanske / orijentalne (hijaz – frigijski dominantni,
  dvostruki harmonijski, mađarski / ciganski mol, ukrajinski dorski, perzijska, napuljske, španjolska 8-tonska),
  japanske (hirajoshi, in-sen, iwato, kumoi), jazz i simetrične (bebop, cjelotonska, umanjene, Prometheus, enigmatska).
- Tonovi skale su **svjetliji**, ostali **tamni**; na klavijaturi su zeleno označeni, korijen crveno.
- **Lijepljenje:** nacrtane i pomaknute note, tipke računala (A W S E …) i **MIDI KEYS** sviraju samo tonove skale.
  Strelice gore / dolje pomiču označene note za jedan ton skale (Shift: oktavu).
- **Akord** (Triad, 7th, 9th, Sus2, Sus4, Power): klik nacrta cijeli akord izgrađen iz skale (bez skale: durski akordi).
- **TO SCALE:** označene note (ili sve) prebaci na najbliži ton skale.
- Tonalitet, skala i akord spremaju se s projektom.

## Efekti po padu (EFFECTS)

Desno od gumba pada (GAIN … END) je **EFFECTS** s **6 slotova** za odabrani pad – svaki pad ima svojih 6 efekata,
uz efekte busova u mikseru. Povuci plugin iz **BROWSER > EFFECTS** u slot; klik otvara plugin, desni klik: bypass /
ukloni / odaberi. Odabereš drugi pad – EFFECTS pokazuje njegove efekte. Padovi s efektima imaju oznaku **FX**.
Pad svira kroz svoje efekte, zatim kroz svoj bus i MASTER; kašnjenje pluginova (latency) se izjednačava, pa svi padovi
i metronom ostaju u ritmu. Efekti se spremaju s projektom.

## VST instrumenti na padovima

- Instrumenti su u **BROWSER > INSTRUMENTS** (efekti u **BROWSER > EFFECTS**; npr. Vital, Serum, Kontakt). **Povuci** instrument na pad
  ili desni klik na pad > **Load VST instrument**. Otvori se prozor instrumenta; na padu piše ime i ljubičasta oznaka VSTi.
- Pad tada svira instrument umjesto samplea: udarac na pad / step sequencer = nota **C3**, a u **PIANO ROLL** (MELODIC, taj pad)
  i s **MIDI KEYS** svaka nota ide u instrument (s velocityjem, akordima, skalama, HUMANIZE).
- Zvuk ide kroz **GAIN, PAN, MUTE** pada, njegove **EFFECTS** (6 slotova) i bus, kao sample. STOP dvaput gasi sve note.
- Klik na ekran valnog oblika (ili desni klik na pad > Open instrument window) otvara instrument; desni klik na pad > Replace / Remove.
- Instrument i njegove postavke spremaju se u projekt. Najviše 16 padova s instrumentom.
- **BOUNCE TO PAD** (Piano Roll, gore desno): note piano rolla ovog patterna (s instrumentom / samplom i efektima) renderiraju se u
  WAV petlju i učitaju na pad po izboru – prvi prazni, odabrani ili bilo koji pad u bankama A–D – pa se sample dalje obrađuje
  (chop, warp, efekti …). Datoteke su u *Dokumenti/Rezalica/Bounced*.
- **DRAG WAV** i izvoz pjesme koriste instrument koji već radi (s učitanim zvukovima), pa su sve note u datoteci.
- Kad je **odabran pad s instrumentom**, MIDI klavijatura automatski svira taj instrument (kao "armed" kanal u DAW-u).
  Ako Piano Roll (MELODIC) svira prazan pad, prebaci se na pad s instrumentom.

## Choke grupe za više padova

Odaberi više padova (Shift + klik, npr. A1, A2, A3), desni klik na bilo koji od njih → **Choke group (3 selected pads)** →
*Choke 1 … 8* ili *No choke*: svi odabrani padovi idu u tu grupu odjednom (jedan UNDO sve vraća). Bez višestrukog odabira
isti izbornik mijenja samo taj pad. Padovi u choke grupi imaju oznaku **CH 1**, **CH 2** … dolje desno.

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

## Samostalna aplikacija (Standalone)

- **FILE**: New Project (Ctrl+N), Open Project… (Ctrl+O), **Open Recent** (zadnjih 10 projekata), Save Project (Ctrl+S), Save Project As… (Ctrl+Shift+S), Export Song as WAV / MIDI…, Audio Settings…
  - Projekti se spremaju kao `.rezalica` datoteke (zadano u `Dokumenti/Rezalica/Projects`). Samplovi se pamte po putanji.
  - **Spremanje promjena**: pri zatvaranju programa, New Project i Open, ako ima nespremljenih promjena, pita „Save changes?“ — **YES** (spremi), **NO** (ne spremaj), **CANCEL** (ostani).
- **Autosave**: svake minute (ako se nešto promijenilo) projekt se automatski sprema. Ako se Rezalica sruši ili nestane struje, pri sljedećem pokretanju nudi vraćanje automatski spremljenog projekta.
- **Verzije rada**: svakih **5 minuta** (i prije svakog učitavanja plugina / instrumenta i otvaranja projekta) sprema se i zasebna verzija
  s datumom, vremenom i imenom pjesme. Čuva se zadnjih 50 (i ništa starije od 7 dana, ali uvijek barem 10). **FILE > Restore Version…**
  otvara popis; trenutni rad se prije vraćanja i sam spremi kao verzija.
- **Zaštita od pluginova koji ruše program**: Rezalica bilježi što radi s pluginom (učitavanje, otvaranje prozora, postavke). Ako se
  sruši, pri sljedećem pokretanju kaže koji plugin je bio kriv i nudi **Restore** ili **Without plug-ins**: pjesma se vrati, a efekti i
  instrumenti ostanu u projektu kao „(not loaded)“ – klik na slot / pad ih učita jednog po jednog. Plugin koji nije instaliran na
  računalu isto ostaje zapamćen (ne nestaje iz projekta).
- **Save** prethodnu spremljenu verziju projekta čuva kao `ime.rezalica.bak`.
- **SAMPLING**: snimanje na odabrani pad, kao na hardverskom sampleru (MPC 2000XL / Roland SP):
  - **Start**: odmah ili **na zvuk iznad thresholda** (klizač, bijela crta na metru); **Length**: do STOP-a ili fiksno (1–8 s, 1 / 2 / 4 takta po tempu) – samo stane.
  - **Channels**: stereo, mono lijevi ili mono desni ulaz; **CLIP** svijetli kad je ulaz preglasan.
  - **Monitor**: ulaz se čuje dok snimaš; **Effects**: snimanje ulaza **kroz efekte** BUS-a A–D ili MASTER-a.
  - Kao na MPC-u: **REC** odmah naoruža sampler („WAITING…“) i čeka ulaz iznad thresholda; čim zvuk krene, snimanje počne samo od sebe.
  - **Choose the pad after REC**: dok čeka (ili snima) prazni padovi trepću – udari pad koji treba dobiti snimku (taj udarac se ne čuje).
  - Nakon snimke **TAKE**: PLAY (preslušaj), **KEEP** (ime, „Trim the silence“, odmah chop na 2/4/8/16 ili po tranzijentima), **RETRY**, **DISCARD**.
  - **COMPUTER** (Windows): snima zvuk koji računalo reproducira (YouTube, Spotify, drugi programi …), bez kabela i virtualnih uređaja (WASAPI loopback zadanog izlaza). Pauze bez zvuka ostaju u snimci.
  - **INPUT**: snima ulaz zvučne kartice (mikrofon, line in, gramofon). „Start on sound“ čeka i kreće točno na prvom zvuku. Ulazni uređaj biraš u Audio Settings.
  - **RESAMPLE**: snima ono što Rezalica svira (padovi, efekti, sekvencer, master). „One loop of the pattern“ pokreće odabrani pattern od početka i snimi točno jedan njegov krug.
  - Snimka se sprema kao WAV u `Dokumenti/Rezalica/Recordings` i odmah je spremna za rezanje (chop mod ili LAZY CHOP). UNDO vraća stari sample.
- **BROWSER**: stoji lijevo u glavnom prozoru i ostaje otvoren između pokretanja. Omiljene mape (FAVORITES) i nedavni projekti pamte se u zasebnoj datoteci `RezalicaSampler.settings`. Klik preslušava, dupli klik (ili LOAD TO PAD) učitava na odabrani pad, a LOAD + NEXT PAD učita pa prijeđe na sljedeći pad. Datoteku možeš i **povući** na bilo koji pad ili u prozor za samplove (te van, u DAW ili mapu).
  - **FAVORITES**: desni klik na bilo koju mapu u browseru → „Add to FAVORITES“ (ponovni desni klik je uklanja). Gumb FAVORITES prikazuje omiljene mape i skače u njih, a nalaze se i na vrhu padajućeg izbornika mjesta (označene zvjezdicom). Pamte se između pokretanja.
- **MIXER** (dolje, umjesto ugrađenih efekata): **BUS A, B, C, D** i **MASTER**, svaki s **4 utora za VST3 efekte** (FabFilter, Valhalla, iZotope …), faderom, mjeračem razine i M (mute) / S (solo).
  - **BUS A–D su SEND busovi:** pad poslan na bus uvijek svira **suh, punom glasnoćom** na MASTER; njegova kopija ide kroz efekte busa, a **fader busa je send količina** (koliko efekta). Bus bez efekata ne vraća ništa (nema udvostručavanja). M / S utišavaju / soliraju povrat efekta. Za insert efekte na jednom padu koristi **EFFECTS** pada (6 slotova).
  - Svi padovi i banke su po defaultu spojeni na **MASTER**.
  - Desni klik na pad → **Send to** → BUS A–D (ili „Same as bank“). Desni klik na gumb banke → **Send bank … to** → cijela banka na BUS. Pad na busu ima plavu oznaku „BUS A“ …
  - U EFFECTS i INSTRUMENTS popisima efekti su složeni u **mape po proizvođačima** (FabFilter, Valhalla DSP, iZotope …); klik otvara mapu, „‹ ALL MAKERS“ vraća natrag.
  - **Skeniranje je sigurno**: svaki plugin skenira zasebna kopija Rezalice, pa plugin koji se sruši ne ruši program. Takvi pluginovi se preskaču i vide se pod **SKIPPED (broj)** (ime i mapa, „Try them again“ za ponovni pokušaj). Napredak se sprema nakon svakog plugina.
  - Efekti: **BROWSER → EFFECTS → SCAN** (pretraži VST3 mape, „VST3 FOLDERS…“ za dodatne mape), zatim **povuci** efekt u utor. Klik na utor otvara efekt, desni klik: Bypass / Remove / Load effect.
  - Efekti i njihove postavke spremaju se s projektom i ulaze u export / resample.
- **Višestruki odabir padova**: klik na prvi pad, pa **Shift+klik** na zadnji označi sve između (npr. A1–A16), a **Ctrl+klik** dodaje ili miče pojedini pad. Označeni padovi imaju žuti okvir. Desni klik na bilo koji pad → **Clear all selected pads** isprazni sve odjednom (jedan UNDO ih vraća). ESC ili običan klik poništava odabir.
- **Chopovi se međusobno prekidaju**: padovi dobiveni rezanjem istog sampla automatski dobiju istu choke grupu, pa svaki novi chop prekine onaj koji svira (kao na MPC-u / Koali). Vrijedi i za starije projekte. Ako želiš da se chopovi preklapaju, na padu odaberi „No choke“.
- **CHOKE** (pored MUTE na padu): padovi iste choke grupe (1–8) prekidaju jedan drugog, npr. otvoreni i zatvoreni hi-hat.
- **Transportne tipke** (gore desno, pored LED BPM zaslona): **REC** (snimanje odsviranog u odabrani pattern, s count-inom; trepće dok čeka), **PLAY**, **PAUSE** (zaustavi, PLAY nastavlja s istog mjesta), **STOP** (natrag na početak) i **metronom** (ikona metronoma, prati tempo projekta; desni klik = glasnoća). Isti transportni blok imaju i STEP SEQ, PIANO ROLL (REC snima u piano roll) i SONG prozor (bez REC-a).
- Prozor se može povećati i **maksimizirati** (tipka u naslovnoj traci ili dupli klik na nju); valni oblik i padovi rastu s prozorom.
- **PLAY / STOP** (i **razmaknica**): sampler svira odabrani pattern (step sekvencer + piano roll) bez obzira na scene i pjesmu. Ako je pattern prazan (nema koraka ni nota), PLAY svira sample odabranog pada: s uključenim LOOP-om ga vrti do STOP-a, bez LOOP-a ga odsvira jednom.
- **Tempo** (polje „BPM“ pored CLICK-a): tempo projekta. Prate ga sekvencer, pjesma, metronom (CLICK) i warpani samplovi.
- U standalone verziji nema gumba LOAD SAMPLE ni STOP ALL: samplovi se učitavaju iz BROWSER-a ili povlačenjem datoteke, a ESC zaustavlja sve. Razmaknica radi u glavnom prozoru, step sekvenceru i piano rollu, a u prozoru SCENE pokreće pjesmu.
- **SONG** (desno, pored STEP SEQ): slaganje pjesme.
  - **SCENE** ima **128 mjesta** (stranice 1–32 / 33–64 / 65–96 / 97–128). Klik na mjesto bira pattern, a desni klik prazni mjesto. Patterni sviraju jedan za drugim.
  - **Povlačenje** patterna mišem ga premješta (zamjena s mjestom na koje ga spustiš), a **Ctrl + povlačenje** ga kopira.
  - **Dupli klik** na P1, P2 … otvara **RENAME**: ime patterna (npr. „Intro“, „Refren“) i njegova **boja**. Boja se bira i iz izbornika mjesta. Ime i boja spremaju se s projektom. Prazno ime vraća P1, P2…
  - **Trake (1–4 tracks)**: svaka traka je svoj red patterna, a sve trake sviraju istovremeno (npr. T1 bubnjevi, T2 bas/melodija). Scena traje koliko njena najduža traka.
  - **SONG** ima po jedan red od 64 koraka za svaku traku (stranice 1–32 / 33–64). Klik na korak uključi tu traku na tom koraku pjesme, a ponovni klik je isključi. Korak na kojem su sve trake isključene je takt tišine. Povlačenjem mišem puniš više koraka.
  - **START marker**: klik na ravnalo iznad SONG redova postavlja odakle PLAY SONG kreće, a desni klik ga vraća na početak.
  - PLAY SONG (ili razmaknica u tom prozoru) svira pjesmu od start markera, STOP je zaustavlja, a LOOP je ponavlja (od početka). Export uvijek uzima cijelu pjesmu. Step sekvencer i piano roll prate pattern koji upravo svira. Glavni PLAY uvijek svira samo pattern.
  - **Timer** pokazuje proteklo vrijeme pjesme (min:s.desetinke), poziciju (takt.doba.šesnaestinka) i ukupnu duljinu.
  - FILE → **Export Song (WAV / MP3)…** otvara prozor s opcijama: format (WAV ili MP3), sample rate (44,1 / 48 / 88,2 / 96 kHz; MP3 44,1 / 48), bit depth (16 / 24 / 32-bit float, dither za 16-bit), MP3 bitrate (128–320 kbps), stereo / mono, normalizacija na −0,3 dB, rep na kraju (0–5 s), naslov i izvođač (metapodaci). Prikazuje duljinu i procjenu veličine datoteke. MP3 koristi Windowsov ugrađeni enkoder.
  - FILE → **Export Song as MIDI…**
- **Audio Settings…** (u FILE izborniku): postavke zvuka:
  - vrsta drivera: **ASIO** (ASIO4ALL, FlexASIO i ASIO driveri zvučnih kartica), Windows Audio (i Exclusive / Low Latency), DirectSound
  - izlazni i ulazni uređaj, aktivni kanali, sample rate, buffer size (latencija), probni ton i MIDI ulazi (kontroleri)
- ASIO podrška ugrađuje se pri automatskoj izradi pomoću službenog Steinberg ASIO SDK-a (ASIO je zaštitni znak tvrtke Steinberg Media Technologies GmbH).

## NORMALIZE (peak)

Gumb **NORMALIZE** (u redu s LOOP / REVERSE / GATE / MUTE) radi klasičnu peak normalizaciju, kao Audacity,
FL Studio, Ableton ili MPC: pronađe najglasniji vrh samplea između markera i pojača (ili stiša) cijeli
sample za isti iznos, tako da taj vrh dođe na odabranu razinu. Dinamika se ne mijenja.

- Razine: 0 dBFS, -0.1 dBFS, **-1 dBFS (preporučeno)**, -2, -3 dBFS (mjesto za miks), -6 dBFS.
- Nakon normalizacije se automatski radi **bounce** (kao u Abletonu): dio koji pad svira (između markera) postaje
  novi WAV u `Documents/Rezalica/Bounced` i odmah se učitava natrag na pad i u prozor s valnim oblikom za daljnju
  obradu (chop, warp, trim ...). Drugi padovi iz istog samplea ostaju kakvi jesu. UNDO ga vraća.
- Gumb **BOUNCE** (prije RENDER) i dalje sprema warpani (time-stretch) sample kao novi WAV, kao i dosad.

## MIDI kontroleri

Gumb **MIDI** (pored BROWSER) treperi zeleno kad stiže MIDI i otvara izbornik:

- **Kontroleri:** svaki kontroler koji spojiš (i dok program radi) sam se uključi; u *Controllers* ih možeš uključiti / isključiti.
- **Pad notes** – koje note sviraju padove:
  - *Rezalica / Akai MPD*: C1 = A1 … D#2 = A16, pa banke B, C, D (64 pada)
  - *MPC 2000XL* i *General MIDI drums*: 16 nota svira 16 padova **trenutne banke**, raspoređeno kao na hardveru
  - *Custom*: desni klik na pad → **Learn MIDI note** → udari pad / tipku na kontroleru (ili *Forget MIDI note*)
- **MIDI Learn:** uključi ga, klikni gumb, fader ili tipku u programu (plavo), pa pomakni / pritisni kontrolu na kontroleru.
  Radi za gumbe pada (GAIN … END, LOOP, REVERSE, GATE, MUTE), transport (REC, PLAY, PAUSE, STOP, metronom), banke A–D,
  tempo, fadere i M / S miksera te DECK (START, 33/45, SYNC, LOOP, REVERSE, REC, PITCH, VOLUME; ploča = **jog wheel**
  za scratch ili **cut** fader). Desni klik briše vezu, ESC ili MIDI LEARN završava.
- **Quick set-up of a controller:** program redom traži: udari 16 padova (A1 … A16), okreni knobove (GAIN … END),
  pomakni fadere (BUS A–D, MASTER) ili pritisni transport tipke. Klik bilo gdje preskače jednu, ESC prekida.
  U MIDI Learn i padovi uče notu: klikni pad pa ga udari na kontroleru.
- **Transport tipke** kontrolera koje šalju **MMC** (tvornički Akai MPD / MPK, M-Audio i većina drugih) rade odmah:
  PLAY, STOP, REC, PAUSE.
- **Presets:** postavke (pad note + MIDI Learn) spremaju se za svaki kontroler posebno (*Save for <kontroler>*) i same se
  učitaju kad taj kontroler spojiš; *Save as…* sprema pod bilo kojim imenom; *Export / Import* u datoteku `.rzmidi`
  (za dijeljenje ili drugo računalo).
- **Clock in:** Rezalica prati tempo i START / CONTINUE / STOP drugog uređaja.
- **Clock out:** šalje MIDI clock (24 po udarcu) i START / STOP na odabrani MIDI izlaz.

## Audio datoteke iz sesije (kao u Abletonu)

Rezalica pamti svaki audio koji sama napravi u sesiji: BOUNCE, NORMALIZE, TRIM, warp, snimke sa SAMPLING-a i scratcheve s DECK-a.
Kad pri izlasku (ili novom / drugom projektu) na pitanje o spremanju kažeš **NO**, program pokaže te datoteke koje nijedan
spremljeni projekt ne koristi i pita:

- **DELETE** – premjesti ih u koš za smeće (Recycle Bin), pa se mogu vratiti.
- **KEEP** – ostavi ih u `Documents/Rezalica`.

Datoteke koje koristi spremljeni projekt nikad se ne nude za brisanje, a tvoji originalni sampleovi se nikad ne diraju.

## Skeniranje pluginova i mapa sa samplovima

- **Pri svakom pokretanju** Rezalica nakon par sekundi sama brzo pogleda VST3 mape i učita samo **nove ili promijenjene**
  pluginove (poznati se ne učitavaju ponovno), pa se novo instalirani efekti pojave sami. SCAN u PLUGINS radi isto, ručno.
- Svaki plugin se skenira u zasebnom procesu. Ako ne uspije, pokuša se **još jednom, sam**, na kraju skena; tek ako
  ni tada ne uspije, ide u SKIPPED. Plugin koji je već bio ispravan ne baca se u SKIPPED zbog jednog neuspjelog ponovnog skena.
- **Mape sa samplovima** se ne skeniraju unaprijed (ni cijeli disk): browser čita mapu kad je otvoriš. Novi zvukovi u
  otvorenoj mapi pojave se sami za par sekundi, a gumb **REFRESH** odmah ponovno pročita mapu.

## DECK (gramofon za scratch)

Gumb **DECK** (uz SONG) otvara gramofon u donjem dijelu prozora.

- **Učitavanje:** povuci sample iz BROWSER-a (ili datoteku) na ploču. Ploča se odmah počne vrtjeti.
- **REVERSE:** motor vrti ploču unatrag (sample svira obrnuto).
- **Scratch:** uhvati ploču mišem i pomiči je naprijed-nazad. Kad je držiš mirno, zvuk staje; kad je pustiš, motor je vraća na brzinu.
- **Cut (fader):** drži **SHIFT** dok scratchaš mišem.
- **START / STOP:** motor se zaleti i koči kao direct-drive gramofon. **33 / 45** okretaja.
- **PITCH:** okrugli gumb, raspon 8 % ili 16 % (desno = brže, dvoklik = 0). **VOLUME** je glasnoća decka.
- **SYNC:** ploča se vrti u tempu sekvencera. Tempo samplea se čita iz imena datoteke (npr. „85-bpm"),
  inače se pogađa iz duljine. Sa SYNC-om START, dok sekvencer svira, kreće točno na sljedećem udarcu.
- **EFFECTS:** 4 slota za efekte, isto kao u mikseru (povuci plugin iz BROWSER > EFFECTS; klik = otvori, desni klik = bypass / ukloni).
  Glasnoću decka podešava VOLUME na decku.
- Klik u gornju traku cijelog samplea (ispod ploče) pomiče iglu.
- **REC:** snima ono što deck svira (nakon fadera i DECK efekata); kad ga zaustaviš, scratch ide na odabrani pad (tišina se odreže).
- Deck svira kroz svoje efekte pa kroz MASTER; postavke i efekti spremaju se s projektom.
- Pri ponovnom pokretanju programa deck je **prazan**, osim ako je sample na ploči spremljen s projektom
  (SAVE / SAVE AS) – tada ostaje na ploči. Otvaranje projekta vraća i njegov sample na deck, novi projekt počinje s praznim deckom.

## Instalacija (Windows)

1. Na GitHubu otvori **Actions**, pa zadnji uspješni **Build**.
2. Pod **Artifacts** preuzmi `Rezalica-Setup-Windows` i raspakiraj ga.
3. Pokreni `Rezalica-Setup-0.2.8.exe`. Instalacija stavlja program u `Program Files\Rezalica`,
   dodaje ga u Start izbornik i (ako je kvačica uključena) ikonu na radnu površinu.
4. Deinstalacija: *Postavke > Aplikacije* ili *Upravljačka ploča > Programi*.

Program ne treba Visual C++ redistributable. Instalaciju gradi CI iz `installer/Rezalica.iss` (Inno Setup 6).

## Samostalna izrada (build)

Potrebni su CMake 3.22+ i Visual Studio 2022 s komponentom „Desktop development with C++”. JUCE se preuzima automatski. Za ASIO u samostalnoj aplikaciji preuzmi Steinberg ASIO SDK i dodaj `-DREZALICA_ASIO_SDK=<mapa SDK-a>` (mapa koja sadrži `common/iasiodrv.h`).

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
| `Source/SceneWindow.*` | SCENE / SONG prozor (trake, drag & drop, boje, start marker) |
| `Source/ToolWindows.*` | Prozori SAMPLING, BROWSER i MASTER FX |
| `Source/MasterFx.h`, `Source/Recorder.h` | Master EQ / kompresor / limiter; snimač ulaza i resamplea |
| `Source/AppSettings.h` | Nedavni projekti, mapa browsera, autosave |

## Licenca

Rezalica koristi [JUCE](https://juce.com). JUCE 8 je dostupan pod licencom AGPLv3 ili pod komercijalnom JUCE licencom.
