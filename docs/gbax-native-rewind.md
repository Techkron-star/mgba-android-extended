# Native GBAX rewind

Opt-in, 16/32/64/96 MiB retained native history (not total emulator RSS), max 60 snapshots at 60 emulated frames each. Immutable 64 KiB pages shared when unchanged; zero pages omitted. No fixed-size Libretro envelope or Java allocation for history. If a baseline exceeds budget, history is discarded. Rewind never rolls back SAVX or standard SRAM: any SAVX command (including failures) or changed battery CRC invalidates the history before recording or stepping. Same-epoch restores preserve the live SAVX session, restore CPU and volatile RAM, and allocate missing pages before mutation. Ordinary named states still invalidate the session. Clear on reset, external state load, cheat changes and unload. Frontend must disable/clear on fast-forward and lifecycle changes. A step runs one ordinary frame afterward to refresh the screen. No guarantee of real-time performance or durability on Android until device tests. Existing disk formats unchanged.

## Read-only world caches

The 64 KiB pages now carry host-only dirty bits. CPU byte/halfword/word writes,
DMA (through the same stores), raw/debug writes, SAVX staging replacement, reset,
named-state restore and rewind restore invalidate the relevant bits. Successful
capture clears them. An unchanged page shares its previous immutable snapshot
without memcmp; a known zero page also avoids rescanning. Modified pages retain
the original exact byte comparison. No ABI, on-disk format or memory window
changes. Capture failure retains conservative invalidation.

`tools/gbax-rewind-cache-test.c` runs the actual core with a 32 MiB read-only cache,
compares clean snapshots with forced compare-all snapshots, then checks writes,
multiple rewinds and capture after restore. The native benchmark is not an Android
performance guarantee. The Libretro regression uses the always-updated master
cycle counter: globalCycles only increments in debugger-enabled builds.
