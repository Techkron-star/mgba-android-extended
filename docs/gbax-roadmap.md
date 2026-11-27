# GBAX / NebulaGBA — engineering baseline

This fork retains the upstream GBA/GB cores and Android frontend. Phase 0 builds an unmodified emulator from the fork on an isolated branch. The baseline workflow publishes a signed debug APK as a short-lived Actions artifact, not a public release, and does not use signing secrets or custom online servers. The upstream package ID and co-op features are unchanged. Do not replace an existing install if its saves have not been backed up; different debug signing certificates may prevent updates. Test offline with a legally obtained ROM.

## Target architecture (proposed, not implemented)

- Preserve original GBA address ranges and mirroring for ordinary ROMs.
- Add a separate opt-in virtual RAM window at 0x40000000..0x7FFFFFFF (original design maximum 1 GiB addressable). Revised target: fixed 64 MiB window 0x40000000..0x43FFFFFF. No change to native EWRAM size.
- Validate range/overflow for every 8/16/32-bit access. Match ARM7TDMI alignment and rotation semantics, CPU timing, DMA, debugger/core bus access, instruction fetch policy, reset and ROM lifecycle. Define whether execution from the extended region is supported; initially data only.
- Extended memory must be off by default and must never alias normal GBA addresses. Negotiate through explicit ROM marker/version/capability plus user opt-in. Fail closed on unsupported ROMs. Do not expose uninitialized host memory.
- Use lazy allocation/commit after measuring host memory. Do not assume the phone can allocate 1 GiB physically. Configure maximum addressable and resident budget separately.
- Extend save states with a versioned, bounded sparse memory section and validate before restoring. Until implemented, explicitly reject extended-mode save states; never silently truncate memory. Ordinary battery saves remain separate.
- Adapt Unholy Emerald linker/allocator only after CPU access tests. Keep IRQ, DMA-visible graphics, startup, and normal RAM compatible unless separately validated. ROM built for GBAX must not be described as standard-emulator compatible.
- Baseline QA: launch, load GBA ROM, controls, sound, normal battery save/load, state save/load. No game ROM distributed with APK.

## Phase plan

0. Baseline Android APK and offline smoke test.
1. Core extension plus host regression tests, data-only 64 MiB.
2. Capability handshake and GBAX test ROM (original test program).
3. Save state format and debugger/DMA compatibility.
4. Android controls, configurable memory cap and performance.
5. Unholy integration on separate branch. Keep standard ROM builds.

## Phase 1 implementation

Experimental data-only 64 MiB, 64 KiB zero-initialized pages allocated on nonzero writes. Explicit test ROM marker: 12-byte title `GBAX RAM64  `, game code `GBAX`, fixed header byte 0x96. This is ROM opt-in, not a separate UI toggle yet. Scalar CPU accesses, LDM/STM and raw view/patch supported; no instruction execution or DMA extension. Reset/unload frees all backing pages. Save states are rejected in GBAX mode; native battery saves remain unchanged. No changes to existing ROMs or SaveBlocks. Full integration/Android QA pending. Future persistent storage: 16 MiB versioned separate backing store, atomic commits, integrity checks, and explicit guest protocol before any Unholy SaveBlock migrations; do not silently resize flash or change existing saves.

## Phase 2: guest CPU validation

Original diagnostic ROM (tools/gbax-guest-test.s) writes and reads the first and last word of each of the 1,024 pages, checks ARM and Thumb byte/halfword/word and multiple transfers, unaligned ARM word rotation and a cross-page LDM/STM. Green screen = pass; red = fail; status word at 0x02000000. Designed for HLE/skip BIOS, no Nintendo logo or commercial ROM. Native regression executes it with a bounded instruction budget, checks raw bus access, rejects GBAX states, resets memory, then loads an unmarked ROM and verifies native RAM and legacy savestate behavior. The Android build depends on this guest test. The test does not validate DMA, external BIOS, CPU overclock or persistent storage. No production Unholy ROM changes.
