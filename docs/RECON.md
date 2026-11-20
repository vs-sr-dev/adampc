# Ricognizione: cosa dicono i firmware

Fonti: i dump in `roms/` (retrobios), disassemblati con `tools/dis6801.py` in `docs/dis/` (non nel repository: si rigenerano dalle
proprie ROM con `tools/gendis.sh`, identici a quelli usati qui).
Per i fatti presi dai documenti vedi [hardware_research.md](hardware_research.md).

## ROM

| File | Contenuto |
|---|---|
| `os7.u2` (8K) | OS-7, il BIOS ColecoVision (identico al `BIOS.col` standalone) |
| `alf @1/@2/@3 rev 57` (3x8K) | SmartWriter rev 57, 0x0000-0x5FFF |
| `eos 6 rev 57` (16K) | chip u22 a 0x6000-0x7FFF, la metà la sceglie **EOS ENABLE** (porta 0x3F bit 1, fa da A13 del chip): 1 = prima metà = **EOS**, 0 = seconda metà = **ultimi 8K di SmartWriter** (routine di stampa, grafica della macchina da scrivere: chiamate da 4746 a 67C5/680C). Scoperto perché la stampa dal word processor non partiva |
| `wp_r80.rom` (32K, ~28K usati) | SmartWriter R80, revisione successiva in un solo chip; non usato per ora |
| `master rev a` (2K) | 6801 master AdamNet, F800-FFFF |
| `keyboard.u2`, `printer.u2`, `tape rev a` (2K) | 6801 delle periferiche, modo single-chip |
| `adam_fdc/*.u10` (4K) | ROM esterna del 6801 del floppy (F000-FFFF), più revisioni/cloni |

## Master 6801: come accede alla RAM dello Z80

Nessun interrupt (tutti i vettori puntano a un RTI). Lavora in polling.
Routine `F800` (leggi byte a X) e `F833` (scrivi A a X):

- **P1** = A15-A8, **P4** = A7-A0 (DDR a FF solo durante l'accesso)
- **P2 bit 0** = richiesta di bus (0 = richiesta; DDR a 1 solo durante l'accesso)
- **P2 bit 2** = R/W (1 lettura: P2=0x1E; 0 scrittura: P2=0x1A)
- **P3** = dati. Lettura: P3CSR=0x18 (latch + OSS), una *scrittura* su P3 genera OS3 che avvia il
  ciclo, il dato torna nel latch con IS3, poi `LDAA P3`. Scrittura: P3CSR=0, dato in P3 con DDR=FF,
  una *lettura* di P3 genera OS3.

Nell'emulatore: allo strobe OS3, con P2.0 basso, si legge/scrive la RAM interna da 64K
(mai la mappa corrente dello Z80).

**Quanto tiene il bus**: BUSRQ si alza con `STAA P2DDR` (F81D / F851) e cade con l'ultimo
`STX P1DDR` (F829 / F85D, che scrive anche P2DDR = 10): 16-20 cicli E per byte, cioè ~60-70 cicli
Z80 in cui lo Z80 è fermo. Nella sessione 1 avevo messo 3 cicli inventati: sbagliato di un ordine
di grandezza, e non è un dettaglio (vedi sotto). Ora lo stallo è la durata vera di P2.0 basso.

**Corsa EOS/master sulla riscansione** (trovata il 2026-11-19 sera): la riscansione dei dispositivi
dell'EOS (F9CB: LDIR che azzera i DCB, poi STATUS a ogni nodo) parte mentre nel DCB 0 c'è ancora una
lettura tastiera pendente (comando 4, il master la ritenta ogni ~6 ms ricevendo NACK). Il master
rilegge comando e nodo a ogni giro: se legge il comando 4 appena prima dell'LDIR e il nodo dopo,
manda `40` (nodo 0), va in timeout e scrive 9B **sopra** il comando STATUS appena messo dallo Z80:
la tastiera sparisce dalla lista e SmartBASIC (menu HELLO) non legge più tasti. Con lo stallo da
3 cicli la fase Z80/master era sbagliata e cadeva nella finestra nel 40% delle temporizzazioni del
disco provate; con lo stallo vero, 0 su 12.

## Protocollo PCB/DCB (dal codice del master)

- PCB in 0xFEC0 (puntatore in RAM interna 0xB7). Byte 0: comando dallo Z80, il master risponde
  con comando|0x80. Comandi: 1/2 sync (handshake all'avvio: aspetta 1, risponde 0x81, aspetta 2,
  risponde 0x82), 3 sposta il PCB (nuovo indirizzo in PCB+1/+2, little-endian), 4 reset dei nodi
  1-15, 5 attesa.
- PCB+3 = numero di DCB; DCB da 21 byte da PCB+4. Campi usati dal master: +0 comando/stato,
  +1/2 buffer, +3/4 lunghezza, +5.. numero di blocco (5 byte inviati), +0x0E/0F contatore retry,
  +0x10 nodo, +0x11/12 lunghezza massima, +0x13 bit 0 = dispositivo a blocchi.
- Comandi DCB: 1 status, 2 reset, 3 scrittura, 4 lettura. Esito in +0 (>= 0x80). Errori visti
  nel codice: 0x9B timeout, 0x9C/0x9D/0x9E errori SCI, 0x8C NACK tastiera, 0xE0/0xE1 lunghezze.

## AdamNet dal vivo (traccia `--trace-net`, boot + tasto "a")

    master -> 11                 STATUS al nodo 1 (tastiera)
    kbd    -> 81 01 00 00 00 01  STATUS: max len 0x0001 (lo,hi), tipo 0 = a caratteri, stato 0, checksum
    master -> 21                 ACK
    master -> 12 ... 1F          STATUS agli altri nodi: nessuna risposta (timeout ~4 ms)
    master -> 41                 RECEIVE dal nodo 1
    kbd    -> C1                 NACK: nessun tasto (si ripete ogni ~4 ms)
    ...
    kbd    -> B1 00 01 61 61     SEND: lunghezza 0x0001 (hi,lo!), dato 'a', checksum (XOR)
    master -> 21                 ACK

Nota: nello STATUS la lunghezza è lo/hi, nel pacchetto dati hi/lo.
Ogni byte dura 160 µs (10 bit a 62.5 kbit/s); ogni nodo riceve anche l'eco dei propri byte.

## Tastiera

- Colonne: P3 bit 0-7 (col 0-7), P4 bit 0-4 (col 8-12): una colonna alla volta in uscita a 0
  tramite il DDR (tabella a FBEC). Righe: P1, tasto premuto = 0. Indice tasto = col*8 + riga.
- Scansione a interrupt OCF ogni 0x3B2 cicli (~0.95 ms); SCI a interrupt; usa il bit WU
  (wake-up) per ignorare i messaggi diretti ad altri nodi.
- Tabelle di traduzione a FAF8 (normale), FB40 (shift), FB88 (control), 72 voci ciascuna.
  Modificatori fuori tabella: 0x48 CONTROL, 0x50 SHIFT, 0x58 LOCK, 0x60 HOME.
- Codici speciali: 0x81-0x86 SmartKey I-VI, 0x90 WILD CARD, 0x91 UNDO, 0x92 MOVE/COPY,
  0x93 STORE/GET, 0x94 INSERT, 0x95 PRINT, 0x96 CLEAR, 0x97 DELETE, 0xA0-0xA3 frecce.

## Digital Data Pack (6801 sulla scheda Memory & I/O, nodo 8)

Modo **espanso multiplexato** (P3 = bus dati/indirizzi bassi, P4 bit 0-2 = A8-A10), RAM esterna
1K (2114 x2) a 0x0400-0x07FF = buffer del blocco. Nessun accesso a P3 come porta.

| Porta | Bit | Funzione |
|---|---|---|
| P1 (uscite) | 0 | SPEED: 1 = 80 ips, 0 = 20 ips |
| | 1 / 2 | STOP drive 0 / drive 1 (1 = fermo e uscita dati forzata a 1: fa anche da selezione) |
| | 3 / 4 | GO FWD / GO REV (attivi bassi) |
| | 5 | BRAKE |
| | 6 / 7 | WRITE ENABLE drive 0 / drive 1 (attivi bassi) |
| P2 | 0 out | WRITE DATA (il firmware lo commuta a mano) |
| | 1 in | cassetta presente nel drive 1 (1 = presente) |
| | 2 out | selezione traccia (0 = blocchi 0-127, 1 = 128-255) |
| P4 (ingressi) | 3 / 4 | MSENSE drive 0 / 1 (nastro in moto regolare) |
| | 5 | cassetta presente nel drive 0 (1 = presente) |
| | 7 | READ DATA (i due drive in wired-AND, quello fermo è forzato a 1) |

Comandi visti su P1: `DE` riposo, `D4`/`D2` avanti lento (lettura), `D5`/`D3` avanti veloce,
`CD`/`CB` indietro veloce, `F4`/`F2` frenata da avanti, `EC`/`EA` frenata da indietro
(prima cifra del drive 0 / drive 1). Dopo la frenata aspetta che MSENSE cada.

**Codifica**: bi-phase mark, cella di **70 cicli E** (contati nel loop di scrittura a FF9D: 70 per
lo 0 e per l'1, transizione centrale a ~35). Lettura (FE6A): dopo ogni transizione di bordo
aspetta ~42 cicli, campiona P4 e confronta: se il livello è cambiato c'era la transizione
centrale = bit 1. MSB first, sincronismo sul byte **0x16** (registro a scorrimento in A).

**Formato di un blocco** (gli header sono di fabbrica, il firmware riscrive solo i dati):

    header: 00 00 00 00 16 | 'H''E' o 'G''W' | idx (16 bit) | ~idx | 00 80 | somma | 00
            (la somma rende 0xFF il totale dei 9 byte; idx = posizione fisica 0-127 sulla traccia;
             00 80 = blocchi per traccia)
    dati:   00 00 00 00 16 | 1024 byte | 00 00 16 | checksum16 (somma dei byte, big-endian) | 00

- Tra i record il nastro deve essere registrato a zeri: la ricerca del sincronismo dell'header
  può agganciare il `00 00 16` della checksum dati e leggere 9 byte "di header" oltre la fine
  del record. Se lì c'è nastro vergine, il loop aspetta un fronte e si mangia il preambolo
  dell'header successivo (verificato nell'emulatore: saltava tutti gli header).
- **Passo dei blocchi**: il salto veloce conta 224 ms per blocco a 80 ips = 17,92 pollici
  (0,9 s a 20 ips, coerente con "circa 1 s per blocco" del TRM). Avvio motore: 200 ms di attesa.
- **Mappa logico/fisico** (FD5E/FD99): blocchi per traccia = 128 (dall'header). Traccia 1 =
  blocchi 128-255 in ordine. Traccia 0: formato GW in ordine; formato HE fisico =
  (logico + 64) mod 128, quindi il blocco 0 sta a metà nastro (il boot da nastro HE richiede ~20 s).
- Seek (FB87): se il bersaglio è avanti di almeno 5 blocchi, veloce avanti (distanza - 4), poi legge
  gli header a 20 ips; se è indietro, veloce indietro (distanza + 5). Retry: 3 letture dati.
- Reset (comando AdamNet 0x08): riavvolge entrambi i drive fino a fine corsa (MSENSE cade).
- Stato AdamNet: nibble basso drive 0, alto drive 1; 0 = OK, 1 = errore checksum, 2 = blocco
  non trovato, 3 = niente cassetta.

- **Errore di lettura** (FB0D-FB6B): 3 tentativi, poi stato 1 (checksum) in $95/$96, ma alla
  RECEIVE successiva il firmware **manda comunque il buffer da 1K** (F8B4: con stato < 3 i dati
  partono, con stato >= 3 parte un byte di stato) e poi invalida la cache ($89 = FF). L'errore
  si vede solo chiedendo lo STATUS.

### EOS: boot e supporti vuoti

- **Boot** (F86A-F8F3, in RAM): prova disco nodo 4, disco nodo 5, nastro 8 (drive 0, poi 0x18
  = drive 1), usando il nibble di stato del DCB (< 3 = c'è un supporto); legge il blocco 0 in
  C800 e ci salta (B = dispositivo). Con un errore: dal disco riprova all'infinito, dal nastro
  riprova solo sul timeout (0x9B), altrimenti GOTO_WP (FCE7). Siccome il nastro manda i dati
  anche con checksum sbagliata e il boot non chiede lo stato, **un blocco 0 illeggibile viene
  eseguito**: i data pack vergini dovevano quindi uscire già inizializzati EOS.
- **INIT_TAPE_DIR** (FCBD -> F323): A = dispositivo, C = blocchi di directory, DE = blocchi
  del volume, HL = nome terminato da 0x03. Copia un modello di 0x68 byte dal chip EOS
  (offset 0x13DA: voce volume con firma `55 AA 00 FF`, BOOT, DIRECTORY, BLOCKS LEFT), ci mette
  nome, dimensione directory | 0x80, dimensione volume, DIRECTORY alloc/usati = C, BLOCKS LEFT
  da C+1 con dimensione-(C+1); scrive il blocco 1 e azzera 2..C. Il blocco 0 lo scrive chi
  inizializza (MIDIBasic: `LD BC,45EDh / LD (0066h),BC / JP FCE7h` = NMI disarmato, poi
  SmartWriter). `--ddp-new`/`--disk-new` fanno lo stesso (src/eosvol.c).
- Voce di directory (26 byte): nome + lettera di tipo + 0x03 (12), attributi, blocco iniziale
  (4), allocati (2), usati (2), byte nell'ultimo blocco (2), data (3). Tipi visti: A programma
  o dati SmartBASIC, H documento SmartWriter.

### EOS: chiamate a blocchi (jump table a FC30)
READ_1_BLOCK `FCF3`, WRITE_1_BLOCK `FCF6`: **A** = dispositivo (nibble basso nodo, alto ID
secondario -> DCB+9), **BCDE** = blocco a 32 bit, **HL** = buffer; Z = successo.
READ_1_BLOCK esegue davvero due richieste di lettura (la seconda dopo aver controllato lo stato):
è la "doppia lettura" notata dalla comunità.

## Controller floppy (6801 + WD2793, nodi 4/5)

Modo **espanso multiplexato senza ROM interna** (mode 2): ROM esterna 2732 da 4K a F000-FFFF
(vettori compresi), RAM interna 0x80-0xFF. Nessun indirizzo esteso fisso nel codice: tutto
passa per X. Decodifica ricavata dagli accessi:

| Bit di indirizzo | Funzione |
|---|---|
| A11 | seleziona il WD2793; A14:A13 = registro (00 stato/comando, 01 traccia, 10 settore, 11 dati) |
| A10 | seleziona la RAM da 1K (2114 x2), A0-A9 |
| A12 | **direzione**: 1 = il 2793 viene scritto e la RAM guida il bus; 0 = il 2793 guida il bus e la RAM memorizza |

Quindi: stato `LDAA 0800`, comando `STAA 1800`, traccia `3800`, settore `5800`, dati `7800`;
**`LDAA 6C00+i` sposta un byte 2793 -> RAM** e **`LDAA 7C00+i` RAM -> 2793** (un "DMA" fatto con
la sola decodifica); il buffer per AdamNet si legge a `1400+i` e si scrive a `0400+i`.

- P1: b0 in = disco assente/drive non pronto, b2 in = **DRQ**, b7 in = jumper unità (nodo 4/5),
  b5 out = motore (0x70 acceso, `& 9F` spento), b1 out = precompensazione (tracce >= 18), b3 = DDEN.
- **INTRQ -> NMI**. L'handler (FE0C) legge lo stato, poi **sostituisce l'indirizzo di ritorno con una
  "continuazione" salvata in $A0** (FCC3 dopo restore, FCF4 dopo seek, FD49/FD9E dopo
  lettura/scrittura...): il codice avvia un comando, gira in un loop d'attesa e l'NMI lo "fa saltare"
  al seguito. Coroutine a mano.
- Comandi usati: `06` restore, `16` seek (verify, 20 ms/passo), `84` read sector (E: attesa 30 ms),
  `A4` write sector, `F0` write track (formattazione: ID scritti in ordine 1..8), `D0` force interrupt.
- **Blocchi**: blocco b -> traccia b/4, settori logici (b&3)*2+1 e +2, ID fisico = tabella a FE04
  `1,6,3,8,5,2,7,4` (interleave 5). L'immagine `.dsk` è in ordine di ID: offset = (traccia*8 + ID-1)*512.
- Stato AdamNet: nibble alto 4, basso 0 OK / 3 senza disco -> `0x40` (come nelle tracce documentate).
- Motore: acceso con 480 ms di attesa, spento dopo ~4 s di inattività (OCF 16 ms x 250).
- **Formattazione**: una scrittura del blocco "magico" **FACE** (`CPX #$CEFA`, numero di blocco
  little-endian in $80/$81) formatta tutto il disco con Write Track (`F0`), cilindri dal più
  alto a 0, settori fisici 1..N in ordine (l'interleave sta solo nella tabella di
  lettura/scrittura), gap 3 = 0x64, riempimento 00. Il 2793 concede 3 tempi di byte per il primo
  byte dopo il comando: la ROM standard ci sta, la 320ta no se il tempo è zero.

### ROM alternative del controller (analisi del 2026-11-19)

Tutte: STATUS identico (`80 00 04 01 00`, nessuna dimensione del supporto), P1DDR = 7A,
nessun comando read address / read track / multi-settore, flag di confronto faccia mai usati
(lettura `84`, scrittura `A4`). La **faccia** la sceglie **P1 bit 4, attivo basso** (0 = faccia
1): il 2793 non ha il pin SSO. La ROM standard lo tiene a 1.

| ROM | Geometria | Blocchi | Ordine | Tabella settori | Passo | Note |
|---|---|---|---|---|---|---|
| standard AD31 rev A | 40x1x8 | 160 | - | 1,6,3,8,5,2,7,4 | r=10 | |
| 320ta (= doug) | 40x2x8 | 320 | faccia 0 intera, poi faccia 1 | uguale | r=01 | doug: 3 bit a 0 rispetto a 320ta, dump rovinato |
| dbl2-4 | 40x2x8 | 320 | come 320ta | uguale | r=00 | formatta 9 settori, ID faccia = testina vera |
| pmhdfdc | come dbl2-4 | 320 | | | | byte 0 del blocco FACE = 1: formatta solo la faccia 0 |
| fastpack 720 | 80x2x9 | 720 | a cilindri, facce alterne | 1,5,9,4,8,3,7,2,6 | r=01 | |
| a720dipi (MMSG 1988) | 80x2x9 | 720 | come fastpack | uguale | r=00 | **P1.0 invertito**: 1 = pronto; precompensazione da traccia 36 |
| 1440k Micro Innovations | 80x2x18 / 9 | 1440 / 720 | a cilindri | 18 voci / 9 voci | r=01 | vedi sotto |

**1440k**: codice riscritto (reset a F000). **P1 bit 3 = velocità** (1 = HD 500 kbit/s, 18
settori), memorizzata nella porta; sui retry la commuta da sola (auto-rilevamento). STATUS nibble
alto E (DD) / F (HD). Blocco FACE = formatta HD, **FACF** = DD. RMCR = $0C: **clock SCI esterno**
su P2.2. Nell'emulatore a 1 MHz la formattazione HD fallisce per *lost data* (un byte ogni 16 µs
è troppo per il loop del 6801): con **E = 2 MHz** funziona, e il clock SCI esterno spiega come
AdamNet resti a 62,5 kbit/s. Ipotesi coerente, non documentata.

Verificato nell'emulatore per ogni ROM: formattazione, INIT_TAPE_DIR, SAVE/CATALOG da SmartBASIC,
blocchi marcatore scritti e riletti sul cilindro/faccia/settore previsto dalla mappa.

## Stampante SmartWriter (6801 single-chip, nodo 2)

| Porta | Bit | Funzione |
|---|---|---|
| P1 (uscite) | 7-4 | motore passo-passo a 4 fasi della **margherita** (96 petali): ruotare il nibble a sinistra = +1 petalo |
| | 3-0 | passo-passo del **carrello**: rotazione a sinistra = verso destra; 2 passi per colonna |
| P4 out | 0 | **martelletto** (0 = colpo; durata = energia del carattere, tabella a FD77: 5-8 tick) |
| | 1 | solenoide del **nastro** (avanza ogni due caratteri) |
| | 2 / 3 | motore DC del **rullo**: b2 basso = avanti, b3 basso = freno (PWM 6/8) |
| P4 in | 4 | sensore home carrello (1 = nella zona a sinistra) |
| | 5 | camma del rullo: un ciclo 1 -> 0 -> 1 = mezza riga (LF = 2 cicli, 0x0B = 1) |
| | 6 | sensore home margherita |
| | 7 | 1 = normale, 0 = **auto-test** (stampa la tabella a FCA1) |
| P3 in | 7-4 / 3-0 | trimmer di velocità margherita / carrello (si sommano ai ritardi) |

- Tutto il movimento sta nell'interrupt OCF ogni 417 µs: profili di accelerazione (tabelle FD06,
  FD0E), verso più breve per la margherita (±48 petali), sequenza martelletto -> nastro.
- Homing all'accensione. Carrello: avanti finché il sensore cade, poi indietro finché torna = colonna 1.
  **Margherita: due volte nello stesso verso** (esce dalla zona home e fa un giro finché ci rientra)
  e chiama quel petalo 5: il sensore copre i petali a partire dal 5. Posizione di ogni carattere:
  tabella a FCF7 indicizzata dal codice ASCII.
- Se la camma del rullo non risponde entro 2 s il firmware si ferma per sempre (SCI spento).
- STATUS fisso `82 10 00 00 00 10` (a FF1C): niente sensore di carta.
- Nell'emulatore il carattere stampato è **il petalo davanti al foglio quando cade il martelletto**:
  se margherita, carrello e rullo sono emulati bene, sulla carta esce il testo giusto.
- Immagine della pagina: ogni colpo con passo del carrello e mezza riga del rullo, disegnato con
  il **font 5x7 della ROM di SmartWriter** (u8 da 0x404, ASCII 0x20-0x7E; uguale nell'OS-7 a
  0x15A3). Apici e pedici cadono sulla loro mezza riga.

## Software provato

- **SmartLOGO** (nastro): si carica e parte.
- **SmartBASIC 1.2** (disco): boot, menu HELLO, programmi digitati, `SAVE`/`LOAD` su disco
  (directory EOS aggiornata correttamente, `tools/eosdir.py` per leggerla). Listati digitati da
  file (`--type-file`): Towers of Hanoi, Bricks; CPM-Smartwriter Filter Utility con due drive.
  `CATALOG D1` e `RUN MidiColor,D1` da MIDIBasic su nastro (poi aspetta l'interfaccia MIDI).
- **Adam Links Golf**, **Market Monitor** (dischi autoavvianti): menu, grafica, partita.
- **SmartWriter STORE/GET** su data pack nuovo: il file torna dopo un riavvio.
- Il tasto "mangiato" della sessione 1 era tempismo: SmartBASIC è pronto solo verso il frame 3000,
  e Golf scarta i tasti battuti durante i caricamenti. Nessun problema della tastiera emulata.
- **CP/M 2.2 Coleco** (disco): `A>`, `DIR`; **WordStar** dal drive B: parte (lo schermo CP/M dell'ADAM
  è una finestra di ~36 colonne su un buffer di 80, quindi WordStar si vede "a fette").
- **Log Maker** (CP/M, `.lbr` con membri CrLZH): estratto e decompresso con `tools/lbr.py`, copiato
  su un disco dati CP/M con `tools/cpmdisk.py`, `LOGMAKER.COM` (BASIC compilato) gira da B: e scrive
  il suo script per QTERM sul disco, letto poi dall'host.
- Un tasto premuto mentre un programma lavora può essere consumato dal programma stesso
  (CP/M: un tasto interrompe `DIR`; SmartBASIC perde il primo tasto dopo `NEW`): sul bus la tastiera
  lo spedisce correttamente, quindi non è la tastiera emulata. Da verificare se SmartBASIC lo fa
  anche sull'hardware vero.

## CP/M 2.2 Coleco: il disco

DPB del drive da 160K nel BIOS (`DISK_INIT_TABLE` nel sorgente ricostruito di N. Brunori, `media/`):
SPT 8 (una "traccia" CP/M = un blocco ADAM da 1K), BSH 3 / BLM 7 (blocchi da 1K), EXM 0,
DSM 146, DRM 63 (64 voci, AL0 = C0: blocchi 0-1), CKS 16, OFF 13. Quindi il blocco CP/M n è il
blocco ADAM 13 + n (i primi 13 sono il sistema), e il blocco ADAM passa per l'interleave del
controller come per EOS. Una seconda tabella (`UNK_DDC5`: DSM 0x37, DRM 0x1F, AL0 = 80, OFF 0) è
probabilmente quella del data pack. Verificato leggendo le directory dei dischi CP/M e WordStar.

## CrLZH (formato dei `.?Y?` di Log Maker)

Roger Warren, 1989-1991, `LZHREL.DOC` dentro `CRLZH20.LBR`: struttura dei file come CRUNCH di
S. Greenberg (`76 FD`, nome, `[commento]`, `00`, revisione, revisione significativa, tipo di
controllo, `05`), codifica LZHUF di Okumura/Yoshizaki (1988). I dettagli della versione 2 non sono
scritti da nessuna parte: trovati qui decodificando file veri e confermati dal checksum finale
(somma a 16 bit dei dati, little-endian, sul confine di byte dopo il codice di fine):
finestra di 2048 byte (LZHUF: 4096) piena di spazi, scrittura da 2048 - 60; Huffman adattivo su
315 simboli: 0-255 byte, **256 fine file**, 257-314 copie da 3 a 60; posizione su 11 bit, i 6 alti
col codice fisso di LZHUF (3-8 bit), i **5 bassi** in chiaro; la copia parte da r - posizione - 1.
Percorso: con 314 simboli i letterali venivano sbagliati, con 315 giusti; la prima posizione
occupava 8 bit e non 9 (quindi 5 bit bassi); la lunghezza minima 3 e il codice 256 libero sono
venuti dal punto in cui finisce il riempimento `^Z` del file. Verificato su tutti i membri di Log
Maker e di `CRLZH20.LBR` (compressi dall'autore). Non esercitata: la ricostruzione dell'albero dopo
32768 simboli (un errore lì si vedrebbe come checksum sbagliato).

## SmartBASIC 1.2 (disco "enhanced routines 1987 U.K. ADAM Subscribers")

- **Vocabolario Applesoft**: la tabella delle parole chiave (nell'immagine del disco, attorno a
  0x5E00) ha HOME, VTAB, HTAB, INVERSE, FLASH, SPEED, ONERR/RESUME, GR/HGR/HGR2, HPLOT, DRAW,
  XDRAW, SHLOAD, ROT, SCALE, più CLRERR, BLOAD/BRUN, INIT. Anche l'idioma DOS 3.3
  `PRINT CHR$(4);"comando"` funziona. Gli operatori vanno scritti attaccati: `> =` non vale `>=`.
- **Memoria**: l'interprete occupa RAM fino a 0x7B6B; **0x7C00-0x7FFF è libera** (posto sicuro per
  codice Z80 chiamato con CALL, alzando LOMEM). Il programma sta **in alto** (sotto ~D17F) e cresce
  verso il basso; con un programma da 15K il LOAD dura ~48 s perché ogni riga inserita sposta le
  altre (1-3,5 s fra un blocco e l'altro, il disco lavora 0,3 s). Nessun contatore di frame in RAM.
- **Tastiera dall'EOS**: FCA8 (F4D0) avvia la lettura di 1 tasto in FD75; FC4B (F4E0) la controlla:
  carry 0 = in corso, carry 1 e Z = tasto in A, NACK (8C) fa ripartire da solo la richiesta.
  I tasti battuti mentre il programma stampa o carica restano nel 6801 della tastiera e vengono
  consumati appena l'interprete li chiede (anche da un PRINT): vanno battuti dopo il prompt.
- **Porting da Apple II** (`tools/a2dos.py` legge i dischi DOS 3.3 e decodifica Applesoft,
  `tools/oregon2adam.py`): Oregon Trail 1975 (versione Applesoft di Chris Torrence) gira dopo aver
  sostituito la routine 6502 TYPETIME con un equivalente Z80 a 0x7C00 (contatore a 24 bit +
  FC4B, ~6670 conteggi/s) e ridisegnato due tabelle da 40 a 31 colonne.

## Scelte di modello (ipotesi da verificare)

- **Reset di rete (porta 0x3F bit 0)**: resetta i 6801 delle periferiche, non il master. Motivo:
  dopo il reset il master fa un ritardo di ~400 ms prima del sync, mentre le tracce documentate
  mostrano il primo STATUS ~160 ms dopo il reset di rete.
- Bus AdamNet modellato a livello di frame (non di bit): collisioni non modellate.
- DMA del master: lo Z80 sta fermo per tutto il tempo in cui P2.0 è basso (dal codice, non da
  documentazione sul cablaggio di BUSRQ/BUSAK). Lo stallo è applicato a posteriori (al rilascio).
- Floppy: posizione dei settori sulla traccia calcolata dal formato delle ROM (gap 4a+1 = 146 byte,
  674 byte per settore da 512 con gap 3 = 100).
- Scheda 1.44M: 6801 a E = 2 MHz (ipotesi, vedi sopra).
- Espansione di memoria: `--xram KB`, banchi da 64K scelti dalla porta 0x42 (modulo il numero di
  banchi, quindi un'espansione da 64K la ignora). Nessun software provato la usa ancora.
- Nastro: meccanica istantanea (niente accelerazione), leader di 20000 celle per lato,
  posizione dei record dentro il passo scelta da me (header a 0, dati a +150 celle).
