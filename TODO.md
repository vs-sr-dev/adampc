# TODO

Contesto: [docs/RECON.md](docs/RECON.md) (ricognizione dei firmware),
[docs/hardware_research.md](docs/hardware_research.md) (documentazione pubblica), [README.md](README.md).

**Stato (fine sessione 2, 2026-11-19 sera)**: tutto l'hardware dell'ADAM è emulato a basso livello
(Z80, VDP, PSG, master, tastiera, Digital Data Pack, due floppy con tutte le ROM di controller note,
stampante con pagina PNG e suono, espansione di memoria). Gira tutto il software provato (elenco in
docs/RECON.md, "Software provato"). Immagini di test in `media/` (usare sempre copie: l'emulatore
riscrive i supporti). Prima di ogni modifica ai tempi: `tests/regress.sh`.

## Sessione del 2026-11-19: dal nulla a SmartWriter

- Scelta: **LLE**. Tutti i 6801 di AdamNet girano con la loro ROM; il protocollo emerge da solo.
- Core scritti da zero: Z80 (`src/z80.c`, zexdoc + zexall 67/67, totale T-state identico a
  Casiolator), 6801 (`src/m6801.c`: porte con strobe P3, timer, SCI con WU), TMS9928A, SN76489A.
- Bus AdamNet a livello di frame (`src/adamnet.c`), master con DMA via OS3, tastiera a matrice.
- Risultato: boot in SmartWriter, digitazione completa attraverso tastiera 6801 -> AdamNet ->
  master 6801 -> DMA -> EOS -> SmartWriter. Audio, finestra SDL2, `--headless`, `--trace-net`.

## Sessione del 2026-11-19 (seguito): Digital Data Pack

- Cablaggio e formato del nastro ricavati dal firmware del 6801 (docs/RECON.md): era
  "UNKNOWN" nella documentazione. Celle di 70 µs, sincronismo 0x16, header di fabbrica
  "HE"/"GW" con indice fisico, dati con checksum a 16 bit, passo 17,92".
- `src/ddp.c`: nastro come celle di flusso bi-phase su due tracce, meccanica (avanti/indietro,
  20/80 ips, freno, fine corsa, MSENSE), scrittura catturando i fronti di P2.0 e
  decodificandoli; l'immagine `.ddp` si aggiorna e viene salvata all'uscita.
- Bug trovato strada facendo: gli spazi tra i record vanno registrati a zeri, altrimenti
  il firmware perde tutti gli header (vedi RECON).
- Test: `tools/mktesttape.py` -> boot da nastro ("ADAM TAPE BOOT OK") e scrittura/rilettura
  di un blocco via EOS ("TAPE WRITE OK", immagine verificata byte per byte).
- `src/z80dis.c` + `dz80` (disassemblatore Z80), `--trace-ddp`/`--trace-ddp2`, turbo nastro e
  drag & drop nel frontend.

- **Primo software vero**: SmartLOGO (`media/`, `.ddp`) si carica da nastro e parte (tartaruga
  e "TUTORIALS? YES OR NO"). 47 blocchi in ~400 s emulati: ogni blocco costa ~8 s perché tra la
  fine dei dati e l'header seguente (320 ms a 20 ips) il firmware non fa in tempo (2 trasferimenti
  da 1K per la doppia lettura EOS + 200 ms di avvio) e riposiziona. Turbo nastro a tempo nel frontend.
- MIDIBasic (`.img`, 18 blocchi) è un volume dati EOS: il suo blocco 0 torna a SmartWriter
  (GOTO_WP), comportamento corretto. Le immagini corte vengono salvate alla loro dimensione.
- WordStar (`.dsk`) è un disco dati CP/M (serve il disco di sistema CP/M), Log Maker è un `.lbr` CP/M.

## Sessione del 2026-11-19 (seguito): floppy

- Mappa del controller floppy ricavata dalla ROM (decodifica A10/A11/A12, DMA via indirizzi,
  continuazioni via NMI: docs/RECON.md). `src/wd2793.c`: WD2793 a eventi con rotazione,
  tempi MFM, passi, write track; due drive (nodi 4/5), il drive 1 sempre collegato.
- Provati: SmartBASIC 1.2 (boot, programmi, SAVE/LOAD), CP/M 2.2 Coleco (DIR) e WordStar dal B:.
- Typer: `~XX` codice tasto, `|` pausa; `--shots N`; `tools/eosdir.py`.

## Sessione del 2026-11-19 (seguito): stampante

- Meccanica ricavata dal firmware (docs/RECON.md): due passo-passo su P1 (margherita 96 petali,
  carrello), martelletto/nastro/rullo su P4, sensori home e camma. `src/printer.c` la emula e
  "stampa" il petalo davanti al foglio quando cade il martelletto. Primo tentativo: tutto
  sfalsato di 5 petali (homing della margherita letto male), poi testo perfetto.
- **Scoperta grossa**: la seconda metà del chip EOS (u22) sono gli ultimi 8K di SmartWriter,
  scelti da EOS ENABLE = 0. Mancavano: la stampa dal word processor finiva subito e la
  schermata della macchina da scrivere era senza righello/rullo. Ora tutto a posto.
- Strumenti di debug: `--trace-eos`, `--trace-z80 FROM TO`, `--watch ADDR`, `--dump-ram`.

## Sessione del 2026-11-19 (sera, sessione 2): software, supporti nuovi, ROM del floppy

- `git init` + commit per ogni blocco di lavoro (`roms/`, `media/`, `build/` esclusi).
- Software nuovo (dati dall'utente in `media/`): **Adam Links Golf** e **Market Monitor** (dischi
  autoavvianti) funzionano; **Towers of Hanoi** e **Bricks** digitati in SmartBASIC da file
  (`--type-file`, `--type-after`); **CPM-Smartwriter Filter Utility** su due drive
  (`tools/eosput.py` per metterci il file d'ingresso); MIDIBasic su nastro: `CATALOG D1`,
  `RUN MidiColor,D1` (poi aspetta l'interfaccia MIDI).
- **SmartWriter STORE/GET su nastro** OK. Per arrivarci: un data pack "nuovo" a zeri faceva
  saltare il boot nel vuoto. Il boot EOS esegue il blocco 0 anche se illeggibile (il firmware del
  nastro manda il buffer comunque), quindi i pack vergini erano già inizializzati EOS:
  `--ddp-new`/`--disk-new` ora creano un volume vuoto come `INIT_TAPE_DIR` (modello di directory
  letto dalla ROM EOS) con un blocco 0 che torna a SmartWriter.
- **Stampante**: pagina PNG accanto al testo (font 5x7 della ROM di SmartWriter, sovrapposizioni e
  mezze righe vere) e rumore della meccanica nell'audio (`--no-printer-sound`).
- **Floppy, ROM alternative** (analisi delegata a un agente, poi verificata nell'emulatore):
  faccia da P1.4, 320K/720K/1.44M, formattazione via blocco FACE, scheda 1.44M con 6801 a 2 MHz.
  Write Track: 3 tempi di byte per il primo byte (prima zero). Dettagli in docs/RECON.md.
- **Bug grosso trovato strada facendo**: SmartBASIC smetteva di leggere la tastiera a seconda dei
  tempi del disco. Causa: lo stallo DMA inventato nella sessione 1 (3 cicli Z80 per byte, il vero è
  ~65) sfasava Z80 e master in una corsa reale dell'EOS (riscansione dei dispositivi con una lettura
  tastiera pendente). Ora lo stallo è la durata vera del BUSRQ.
- `tests/regress.sh`: 13 prove end-to-end con controlli automatici (78 s in parallelo).
- Espansione di memoria `--xram` (banchi da 64K via porta 0x42), autotest Z80 da SmartBASIC OK.
- **Oregon Trail 1975** (Applesoft di Chris Torrence, disco DOS 3.3) portato su SmartBASIC:
  `tools/a2dos.py` + `tools/oregon2adam.py`, TYPETIME riscritto in Z80. Risultato in
  `media/Oregon Trail 1975 (SmartBASIC port)/` (RUN OREGON; LOCK per le maiuscole). Scoperte su
  memoria e tastiera di SmartBASIC in docs/RECON.md; le routine di test ora stanno a 0x7C00
  (prima finivano dentro l'interprete e funzionavano per caso).
- Il "tasto mangiato" della sessione 1 era tempismo dei miei test (SmartBASIC pronto verso il
  frame 3000-3400), non un difetto.

## Domande aperte

- **GAME sul disco SmartBASIC**: "?NEXT without FOR in 100"; il blocco 42 contiene un altro
  programma più residui di directory: probabilmente dati vecchi nell'immagine originale.
- **Passo dei blocchi sul nastro**: il firmware conta 17,92" per blocco nel salto veloce, ma
  300 piedi / 128 blocchi darebbero ~27"; con 27" la lettura sequenziale non riposizionerebbe.
  Servono misure o testimonianze sui tempi reali di caricamento (es. SmartLOGO, SmartBASIC).
- **Scheda 1.44M a 2 MHz**: ipotesi coerente (clock SCI esterno, HD impossibile a 1 MHz) ma
  non documentata. Idem per come P1.3 arriva al selettore di velocità.
- **Corsa EOS/master** sulla riscansione: con i tempi giusti non capita, ma resta una finestra
  reale. Se ricompare, guardare prima i tempi (DMA, wait state) che la logica.
- Contenuto dei data pack vergini Coleco: ipotizzato "inizializzati EOS" (vedi sopra), da
  confermare con un'immagine di un pack nuovo.

## Prossimi passi

1. **Log Maker** (`.lbr` CP/M): i membri sono compressi LZH (crunch, firma `76 FD`): servono un
   decompressore (strumento Python) e uno strumento per scrivere file su un disco CP/M ADAM
   (parametri del DPB nel sorgente CP/M di `media/`).
2. Cartucce: provare giochi ColecoVision (serve materiale di test), header 55AA/AA55, ROM di
   espansione 66 99.
3. Precisione: wait state (M1, PSG), timing VDP intra-linea, collisioni sul bus AdamNet,
   stallo DMA applicato in tempo reale invece che a posteriori.
4. Spinner (INT dello Z80), secondo controller, mappatura tasti configurabile.
5. Software su nastro (giochi) e software che usi l'espansione di memoria, se se ne trovano.
6. Tokenizzatore SmartBASIC: listato di testo -> file di tipo A dentro un .dsk/.ddp, così i
   listati (Apple o ADAM) si caricano senza digitarli (formato della riga visto a D16C: da studiare).
7. Strumenti: `eosdir.py`/`eosput.py` conoscono solo la mappa standard da 160K; aggiungere le
   mappe delle altre ROM (le formule stanno in `tests/tools/expect_map.py`).

Risorse: `media/ADAM CP-M 2.2 Source (2025-10-01) (Nino Brunori)/` = sorgente ricostruito del
CP/M ADAM (utile per il terminale a 80 colonne virtuali e i driver); `docs/dis/` = disassemblati
dei 6801; `build/dz80` per lo Z80.

## In fondo, a hardware completato

- **Super Game Module** (AY-3-8910, RAM extra; la sua porta di mappatura RAM coincide con la
  0x7F dell'ADAM, da verificare sulla documentazione) + **profilazione VDP**: violazioni dei
  tempi di accesso alla VRAM, sprite persi per linea, tempo per frame.
- Banco di prova: il porting di Final Fantasy (NES) su ColecoVision SGM in
  `D:\Homebrew5\ColecoFF`, molte slice compilate; la più recente dovrebbe essere un collage
  delle altre, quindi la più impegnativa. Farla girare in modalità ColecoVision+SGM ed
  eventualmente in modalità ADAM (memoria via 0x7F, audio di ripiego sull'SN76489).
  È anche l'oracolo del futuro kit game-agnostic NES -> Coleco SGM, che potrebbe avere
  l'ADAM come target quasi gratis (stessi VDP, PSG, Z80; più RAM; manca solo l'AY).
- **Dopo SGM/FF1: Wizardry I (Apple II) sull'ADAM, approccio BYOA.** Il repo "Wizardry Proving
  Grounds v3.2" (sorgente ricostruito) non ha licenza ed è derivato da un'opera altrui: non usarlo.
  Strada: Wizardry Apple gira sul p-System UCSD (Apple Pascal 1.1), `WIZARDRY.CODE` è p-code.
  1) lettore del file system Apple Pascal per i dischi dell'utente; 2) interprete p-code in Z80
  scritto dai manuali pubblici della p-machine; 3) riscrittura Z80 delle parti in assembly 6502
  (labirinto, ritratti, suono, disco) e delle unità Apple Pascal usate; 4) far girare il
  `WIZARDRY.CODE` originale dell'utente. Grafica 280x192 monocroma -> 256x192 Graphics II;
  memoria: 64K + `--xram`. Precedenti Z80: versioni MSX e PC-88 (solo come indizio di fattibilità).
  Serve: immagini dei dischi originali (master + scenario).
- Contesto kit GB -> ADAM: lo scroll a passi di 8 pixel è la scelta già validata con FF1;
  restano i vincoli su sprite (4 per linea, 1 colore) e colore dello sfondo (2 per segmento 8x1).
