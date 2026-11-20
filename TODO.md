# TODO

Contesto: [docs/RECON.md](docs/RECON.md) (ricognizione dei firmware),
[docs/hardware_research.md](docs/hardware_research.md) (documentazione pubblica), [README.md](README.md).

**Stato (fine sessione 3, 2026-11-20)**: tutto l'hardware dell'ADAM è emulato a basso livello
(Z80, VDP, PSG, master, tastiera, Digital Data Pack, due floppy con tutte le ROM di controller note,
stampante con pagina PNG e suono, espansione di memoria). In più: modalità ColecoVision, Super Game
Module, MegaCart e un profilatore VDP/tempo di frame; FF1 (coleco-ff slice78) gira fino
all'overworld sia su ColecoVision+SGM sia su ADAM. Gira tutto il software provato (elenco in
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

## Sessione del 2026-11-20 (sessione 3): Super Game Module, MegaCart, profilatore VDP

- **Modalità ColecoVision** (`--coleco`): stesso Z80/VDP/PSG, 1K di RAM a specchio a 6000-7FFF,
  niente AdamNet. Il BIOS ColecoVision e l'OS-7 dell'ADAM sono lo stesso file (stesso MD5), quindi
  basta `roms/adam/os7.u2`.
- **Super Game Module** (`--sgm`): AY-3-8910 scritto da zero dalle formule del datasheet GI
  (`src/ay8910.c`: toni, rumore a 17 bit, inviluppo con CONT/ATT/ALT/HOLD), clock 1,789772 MHz;
  porte 0x50/0x51/0x52. Su ColecoVision anche la RAM: porta 0x53 bit 0 (24K a 2000-7FFF) e 0x7F
  bit 1 (8K al posto del BIOS). Sull'ADAM solo l'AY: la RAM c'è già e la 0x7F è la mappa dell'ADAM.
  Verifica senza software altrui: `tests/tools/aytest.py` (cartuccia assemblata a mano) misura
  440,0 Hz per TP=254 (atteso 440,4) e 13,5 Hz per il triangolo dell'inviluppo con EP=256 (13,65).
- **MegaCart**: banchi da 16K, l'ultimo fisso a 8000, la lettura di FFC0+N mappa il banco N a C000,
  all'accensione la finestra mostra l'ultimo banco (come scritto nel crt0 di coleco-ff).
- **Profilatore** (`--prof`, `--prof-csv`, `--prof-log`, `--prof-from`, `--prof-pc` +
  `tools/profsym.py`), numeri dal Data Manual TI del 1982 (Tabella 2-2): tra due trasferimenti
  VRAM servono fino a 8 µs in Graphics I/II con il display attivo, 3,1 in Text, 3,5 in Multicolor,
  2 µs con lo schermo spento o nei 4300 µs dopo l'interrupt. Sotto 2 µs è "hard" (con uno Z80 a
  3,58 MHz non succede: l'OUT più veloce costa 11 cicli = 3,07 µs), sotto il limite del modo è
  "risky". Poi sprite oltre il quarto per riga, tempo di frame fino al ciclo d'attesa (trovato da
  solo: salto corto all'indietro ripetuto senza cambiare la memoria, o HALT), durata dell'NMI,
  corse NMI/VDP (registro indirizzo a metà, indirizzo spostato sotto un trasferimento), letture
  dello status fuori dall'NMI che si prendono il flag F. Verifica: `tests/tools/vdptest.py`.
- Correzione al VDP dal datasheet (2.3.3): il flag 5S si accende solo se F = 0.
- `--joy FILE` (script del joystick), `--dump-vram`, `--trace-ay`. Tre prove nuove in
  `tests/regress.sh` (sgm_ay, vdp_prof, ff_sgm), 16/16 passano.
- **FF1 coleco-ff slice78 (MegaCart 512K)**: leggenda, scelta delle classi, menu, overworld,
  camminata, su ColecoVision+SGM e su ADAM (schermate identiche). Lo script del joystick
  (`tests/media/ff_overworld.joy`) rifà gli input di `mame_drive.lua`. Cosa dice il profilatore:
  - **sprite**: in leggenda e overworld gli sprite non usati sono parcheggiati in parte a Y=0xC7
    (fuori schermo, giusto) e in parte a **Y=0x00, che sono le righe 1-16**: 13 sprite trasparenti
    lassù, 5S acceso a ogni frame. Oggi non si vede nulla, ma uno sprite vero nelle prime 16 righe
    oltre il quarto sparirebbe. Rimedio: 0xD0 nella Y del primo sprite libero (terminatore).
    Per il kit NES -> Coleco: lo sprite "nascosto" del NES (Y >= 0xEF) va tradotto in 0xD0 o in
    0xC0-0xCF, mai in 0.
  - **VRAM**: un solo punto "risky", `__tms9918_setup_mode2_pattern` di z88dk (PC BBB7): ciclo
    OUT/INC/JR da 27 cicli = 7,54 µs con lo schermo acceso, sotto gli 8 µs del caso peggiore.
    Succede al cambio di modo, ~1000 byte. Nessuna corsa NMI/VDP.
  - **tempo**: leggenda 2,7% del frame; l'NMI (audio) costa fino a ~1,1 ms; un passo in overworld
    tiene la CPU piena per ~28 frame. Dove va il tempo durante la camminata (`--prof-pc` +
    profsym): 35% attesa del vblank, 22% `redraw_ow_view_from_banks`, 20% `ow_read_macro`, ~10%
    negli helper di sccz80 per lo stack (`l_gint*`), 2,4% `mc_select_bank`.
  - **tile fuori posto in overworld** (viste dall'utente, "pezzi di altre parti della mappa"):
    nuovo controllo del profilatore, *NMI che torna con un altro banco MegaCart*. Nella camminata
    di prova: 168 volte in 2860 frame, sempre "interrotto il banco 6 (quadrante), tornato col banco
    0". `audio_nmi_tick` rimette `main_bank`, non il banco che ha trovato; `ow_read_macro` e
    `redraw_ow_view_from_banks` selezionano il quadrante e leggono qualche istruzione dopo, quindi
    se l'NMI cade lì in mezzo la lettura prende il banco 0 allo stesso offset. Rimedio nel porting:
    una copia in RAM del banco corrente (scritta *prima* della lettura di FFC0+N) e l'NMI che
    rimette quella, non `main_bank`.
  - **transizioni**: aprendo/chiudendo il menu lo schermo resta acceso e per qualche frame la
    mappa si vede con le tile del font (la pattern table cambia prima della name table).
- Insidie di questa sessione: le funzioni `static` di z88dk sono simboli *locali* nella .map
  (all'inizio profsym attribuiva tutto a `svc_wait_vblank`: 77% invece di 35%); la .map dice però
  a quale funzione C appartiene ogni simbolo (`file.c::funzione::`), ed è quello che si usa ora.

## Sessione del 2026-11-20 (seguito): timing del VDP dentro la riga

- Fonte: l'articolo di openMSX "VDP VRAM timing, part 2" (W. Vermaelen, 2014) con il suo
  diagramma, le cui righe TMS9918 sono ricostruite dal "9918 Master Timing" disegnato a mano da
  TI nel 1978 (archivio di Karl Guttag) e dal manuale applicativo. Documentazione, nessun sorgente.
- `tools/vdpslots.py` legge gli slot dal diagramma (colori dei riquadri, un riquadro = un accesso
  di 2 cicli VDP) e scrive `src/vdpslots.h`: Graphics I/II 19 slot CPU per riga, al massimo 32
  cicli VDP fra uno e l'altro (il "once every 16 memory cycles" del manuale); Text uno ogni 6;
  bordo verticale uno ogni 4 (gli altri sono refresh). Multicolor: le letture della color table
  date alla CPU (ipotesi dell'articolo, coerente col manuale).
- Il profilatore ora calcola lo slot in cui il VDP esegue ogni trasferimento (ritardo 8/11/13
  cicli VDP, il "2 µs" del manuale) e segnala una richiesta che arriva prima: persa / probabilmente
  persa / al limite. Verifica di coerenza: caso peggiore Graphics 11 + 32 = 43 cicli VDP = 28,7
  cicli Z80 (la regola dei 29), Text 11 + 6 = 17 = 11,3 (i 12 di Grauw).
- Allineamento: il flag F cade al ciclo 319 della riga 191 (fine del display attivo), dove adamPC
  chiude la sua riga 191. L'istante dell'I/O dentro l'istruzione ora è esatto (OUTI/OTIR al
  ciclo 16, INI/INIR al 13, IN/OUT alla fine).
- `tests/tools/vdptest.py`, quattro cartucce: OTIR nel bordo (niente perso), ciclo da 30 cicli
  nel display (niente), ciclo da 27 di z88dk (~14 probabilmente persi su 256), OTIR nel display
  (~56 persi su 256).
- FF1: i ~1000 byte di `__tms9918_setup_mode2_pattern` (z88dk) scritti a schermo acceso danno
  54 byte probabilmente persi e 41 al limite; il resto del gioco non ha richieste in anticipo.
  Aggiunto a `D:\Homebrew5\ColecoFF\ADAMPC_FINDINGS.md`.

## Sessione del 2026-11-20 (seguito): Log Maker, dischi CP/M, CrLZH

- `tools/cpmdisk.py` (dir/get/put/era/new): DPB del BIOS CP/M ADAM letto nel sorgente ricostruito
  (OFF 13, blocchi da 1K, 64 voci), verificato sulle directory dei dischi CP/M e WordStar.
- `tools/lbr.py`: librerie LBR + decompressore CrLZH v2. La documentazione del formato
  (`LZHREL.DOC`) dice solo "LZHUF con la struttura di CRUNCH"; i parametri della v2 sono stati
  ricavati sperimentando sui file e confermati dal checksum (dettagli in docs/RECON.md).
  Funziona su tutti i membri di Log Maker e del pacchetto CRLZH20 dell'autore.
- Prima idea scartata: far girare `UCRLZH20.COM` (scaricato) sotto CP/M nell'emulatore; il
  classificatore dei permessi l'ha bloccato (binario scaricato da internet), quindi strada
  empirica con verifica del checksum. Nessun binario scaricato è stato eseguito.
- **Log Maker gira**: `LOGMAKER.COM` (BASIC compilato) sotto CP/M 2.2 dal drive B:, scrive lo
  script di chat per QTERM sul disco, l'host lo rilegge. Prova `logmaker` in `tests/regress.sh`.
- Insidie: `DIR` di CP/M si interrompe a un tasto premuto; un tasto battuto mentre un `.COM` grande
  si carica va perso (tempi del typer, non dell'emulatore).

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
- **SGM**: le pagine dei forum (AtariAge) rifiutano la lettura automatica, i dettagli delle porte
  vengono da riassunti di ricerca + il crt0 di coleco-ff. Da confermare su un documento Opcode:
  stato della 0x7F all'accensione, comportamento esatto sull'ADAM, se la RAM SGM copre anche
  6000-7FFF (qui sì).
- **Tabella dei volumi dell'AY**: passo di ~3 dB (16 livelli), non misurata; idem il rapporto
  di livello AY/SN76489 sul modulo vero.
- **Slot del VDP**: il diagramma è ricostruito dal disegno TI del 1978, non misurato; il ritardo
  "2 µs" è fra 8 e 13 cicli VDP; non si sa cosa faccia il TMS9918 con un byte scavalcato (perso?
  scritto all'indirizzo sbagliato?). Per questo adamPC segnala e non altera la VRAM. Una misura
  su hardware vero (es. il VATT degli MSXisti, o un test su ColecoVision) chiuderebbe la questione.

## Prossimi passi

**Fatto nella sessione 3: Super Game Module + profilazione VDP + slot del VDP** (sopra).
Ordine deciso il 2026-11-20: punti 1 (Log Maker) e 6 (tokenizzatore SmartBASIC), poi il repo
pronto per la pubblicazione; il kit Apple II -> ADAM in una pipeline separata, con un handoff.
Seguiti possibili del profilatore:
- profilatore nella finestra: grafico del tempo di frame sovrapposto, righe con sprite persi
  evidenziate;
- altre scene di FF1 (battaglia, città, ponte) con script `--joy` presi dagli altri
  `mame_drive_*.lua`;
- l'ADAM come target del kit NES -> Coleco: con `--sgm` FF1 gira già identico.

Poi le rifiniture, nessuna bloccante:

1. ~~Log Maker~~: fatto (`tools/lbr.py`, `tools/cpmdisk.py`). Restano: CrLZH v1, squeeze e
   crunch (`76 FF`, `76 FE`) se se ne trovano; `cpmdisk.py` per le altre ROM del floppy.
2. Cartucce: provare giochi ColecoVision (serve materiale di test), header 55AA/AA55, ROM di
   espansione 66 99.
3. Precisione: wait state (M1, PSG), collisioni sul bus AdamNet,
   stallo DMA applicato in tempo reale invece che a posteriori.
4. Spinner (INT dello Z80), secondo controller, mappatura tasti configurabile.
5. Software su nastro (giochi) e software che usi l'espansione di memoria, se se ne trovano.
6. Tokenizzatore SmartBASIC: listato di testo -> file di tipo A dentro un .dsk/.ddp, così i
   listati (Apple o ADAM) si caricano senza digitarli (formato della riga visto a D16C: da studiare).
7. Strumenti: `eosdir.py`/`eosput.py` conoscono solo la mappa standard da 160K; aggiungere le
   mappe delle altre ROM (le formule stanno in `tests/tools/expect_map.py`).

Risorse: `media/ADAM CP-M 2.2 Source (2025-10-01) (Nino Brunori)/` = sorgente ricostruito del
CP/M ADAM (utile per il terminale a 80 colonne virtuali e i driver); `docs/dis/` = disassemblati
dei 6801 (`tools/gendis.sh`); `build/dz80` per lo Z80.

## SGM e FF1 (fatto nella sessione 3), poi il kit Apple II

- SGM, MegaCart, modalità ColecoVision e profilatore: fatti (vedi la sessione 3). FF1 slice78
  (`media/ColecoFF slice78/`, copia della build in `D:\Homebrew5\ColecoFF\build`) è il banco
  di prova e l'oracolo del futuro kit game-agnostic NES -> Coleco SGM; l'ADAM come target è
  quasi gratis (stessi VDP, PSG, Z80; più RAM; l'AY con l'SGM o l'ADAM Sound Enhancer).
- **Dopo SGM/FF1: kit game-agnostic Apple II -> ADAM, banco di prova Wizardry I.** BYOA: il repo
  "Wizardry Proving Grounds v3.2" (sorgente ricostruito, senza licenza, derivato da un'opera
  altrui) non si usa. Stessa forma di JagFF4: traduttore + ponte hardware + profilo facoltativo.
  - **Traduttore 6502 -> Z80**: lo stesso pezzo serve al kit NES -> Coleco SGM, scriverlo una volta.
    Codice automodificante: ricaduta su un interprete per quelle zone.
  - **Tre ingressi ciechi**: (1) p-System/Apple Pascal (Wizardry: `WIZARDRY.CODE` è p-code) ->
    p-machine Z80 dai manuali pubblici + unità Apple Pascal, gli EXTERNAL 6502 via traduttore;
    (2) binari 6502 -> traduttore + ponte; (3) Applesoft -> tradurre l'interprete Applesoft dalla
    ROM Apple dell'utente (programmi intatti, 40 colonne); la via SmartBASIC resta quella leggera.
  - **Ponte**: testo 40x24 -> Text mode TMS 1:1; lo-res 40x48 -> Multicolor 64x48; hi-res
    280x192 -> Graphics II 256x192 con fusione OR 35:32 (le linee da 1 pixel non spariscono;
    profilo: OR / ritaglio / allineato a sinistra), scritture in $2000-$3FFF intercettate, copia in
    RAM, invio al VDP dei byte cambiati a ogni frame, page flip = doppio buffer; language card ->
    mappa 0x7F / espansione 0x42; tastiera via EOS; altoparlante $C030 -> SN76489 a 1 bit;
    paddle -> controller; disco RWTS/BIOS Pascal -> blocchi EOS su immagine convertita
    (140K Apple in un disco ADAM da 160K, 2 blocchi Pascal = 1 blocco ADAM).
  - Limiti: codice cronometrato (musica sull'altoparlante), protezioni dalla copia (serve
    un'immagine non protetta), colore hi-res da artefatti NTSC solo approssimato.
  - Serve: immagini dei dischi Wizardry I (master + scenario), ROM Apple II dell'utente per Applesoft.
- Contesto kit GB -> ADAM: lo scroll a passi di 8 pixel è la scelta già validata con FF1;
  restano i vincoli su sprite (4 per linea, 1 colore) e colore dello sfondo (2 per segmento 8x1).
