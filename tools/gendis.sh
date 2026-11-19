#!/bin/bash
# tools/gendis.sh - disassemble the AdamNet 6801 ROMs into docs/dis/ (not in the repository:
# they would be the firmware itself). Needs the ROMs in roms/, see README.
# Addresses quoted in docs/RECON.md refer to these listings.
cd "$(dirname "$0")/.." || exit 1
mkdir -p docs/dis
D="python -I tools/dis6801.py"
$D "roms/adam/master rev a 174b.u6" F800 > docs/dis/master6801.asm
$D "roms/adam_kb/keyboard.u2" F800 > docs/dis/kb6801.asm
$D "roms/adam_ddp/tape rev a 8865.u24" F800 > docs/dis/ddp6801.asm
$D "roms/adam_prn/printer.u2" F800 > docs/dis/prn6801.asm
# disk controller: code reached only through the NMI "continuations" ($A0) and their branches
$D "roms/adam_fdc/adam disk u10 ad 31 rev a 09-27-84.u10" F000 \
   FCF4 FD07 FD0D FD1C FD49 FD58 FD65 FD71 FD9E FDAD FE37 FE4C FE50 FE6D FE75 FE7F FE81 FE8C \
   FE8E FE90 FE9D FE9F FEAC FEB6 FEC0 FEC9 FED6 FEE0 FEEA FEEC FEF9 FEFB FF08 FF0A FF17 FF20 \
   FF22 FF2D FF2F FF3C FF46 FF48 FF5C FF5E FF68 FF7F FF8B > docs/dis/fdc6801.asm
ls docs/dis
