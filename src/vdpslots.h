/* vdpslots.h - TMS9918 VRAM access slots, one character per memory access (2 VDP cycles):
 * entry j (0..170) starts at VDP cycle 2j + 1. C = CPU slot, r = refresh, s = sprite,
 * n = name, p = pattern, c = colour. Read by tools/vdpslots.py off the openMSX timing
 * diagram (vdp-timing-v2.png, W. Vermaelen 2014), whose TMS9918 rows come from TI's 1978
 * "9918 Master Timing" chart and the TMS9918 application manual. */
#ifndef VDPSLOTS_H
#define VDPSLOTS_H
static const char vdp_slots_border[172] =
    "CCCCCCCCCCCCCCCCCCCCCCCCCCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCr"
    "CrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrC"
    "rCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCrCCCCCCCCCCCCCCCCCC";
static const char vdp_slots_character[172] =
    "CCCCssssssssssCCCCssssssssnCcpnscpnscpnscpnCcpnscpnscpnsc"
    "pnCcpnscpnscpnscpnCcpnscpnscpnscpnCcpnscpnscpnscpnCcpnscp"
    "nscpnscpnCcpnscpnscpnscpnCcpnscpnscpnscpCCssssssssssssssC";
static const char vdp_slots_text[172] =
    "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCnCpnCpnCpnCpnCpnCpnCpnCpnCpn"
    "CpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpn"
    "CpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpnCpCCCCCCCCCCCCCCCCCCCCCC";
#endif
