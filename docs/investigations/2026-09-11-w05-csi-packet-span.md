# CSI corresponding-packet span: pinned SDK boundary

Scope: firmware W-05 on ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643`.
The native receipt bridge and public packet path are implemented as Candidate;
production syntax/archive checks are separate from the pending runtime/RF gate.
The SDK and its archives were read without modification; no device was used.

## Confirmed from the current archives

`wdev_csi_rx_process` in each target's `libpp.a:wdev.o` constructs the public
`wifi_csi_info_t`, invokes the registered callback synchronously, and then frees
that info structure. Its header pointer comes from the fourth native argument;
the published payload pointer is always that pointer plus **24 bytes**. The
fifth argument is copied directly into `payload_len`. It does not parse QoS,
Address4 or HT-control extensions before setting the payload pointer.

| Target | `hdr` / `payload` / `payload_len` offsets in constructed info | Current `libpp.a` SHA-256 |
| --- | --- | --- |
| C3 | 72 / 76 / 80 | `756aa3ba9fcd8bef1ee5a8ec36c82406b5ed3a4d43f94ef6b15e05b2cd7a593c` |
| S3 | 72 / 76 / 80 | `ae7004d00c2c5e1cf106548b22e2ed5bb3a059d65393f4e15791019658191cdb` |
| C5 | 88 / 92 / 96 | `a06374436b70eb6b8a2b8d653976218db6e08e7dbe90c8414b07ad9e923d9f99` |

Disassembly is retained under `build/w05-csi-packet-review/` as
`<target>-wdev_csi_rx_process.txt` and `<target>-wdev-all.txt`.
The offsets above describe the reviewed constructor, not a runtime struct-layout
API or permission to read arbitrary pointers.

In the C3 `wDev_SnifferRxData` caller, the fifth argument is read from the
low 12 bits at offset 44 of the copied RX prefix; `hdr` is that copy plus 48.
Allocation/copying separately accounts for descriptor-chain lengths and the
CSI prefix. Other callers include control-frame, normal RX and AMPDU paths,
plus the driver function table. The constructor alone therefore does not prove
that `payload_len` bytes are readable beginning at the published `payload`.

The [public SDK declaration](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_wifi/include/local/esp_wifi_types_native.h)
has no independent header length. Public field comments are insufficient to
resolve the constructor/caller distinction above.

## Implemented capture contract

The four reviewed direct origins (sniffer, control frame, ordinary RX and AMPDU)
redirect their first-prefix/continuation memcpy calls and CSI constructor call.
Each memcpy completes before an address-only receipt is recorded. C3/S3 use a
48-byte RX metadata region and a 44-byte first-prefix copy; C5 uses 64/56 bytes.
Metadata-only tails do not expand the packet span. Gaps, overlap or integer overflow
invalidate the receipt. The constructor wrapper requires the exact RX base/header
identities and carries a stack scope through the original synchronous constructor.
The public callback consumes that scope once, checking sample pointer/length,
header pointer and the raw reported length. Four bounded task lanes isolate pending
copies; ISR, unwrapped and unreceipted origins report packet unavailable.

The adapter reads only min(completed copied packet extent, rx_ctrl.sig_len).
It parses the variable MAC header within that range and copies a prefix of the
same observation into the CSI slot's optional packet storage. It does not use
payload_len as a body/read bound and does not concatenate hdr/payload pointers.
FCS and protected-payload representation remain unknown. Full completeness is
relative to the driver packet report, not independently measured RF bytes.

Open reserves the optional packet records and storage before Radio acquisition.
None allocates no packet records/bytes, header reserves 36 bytes per slot, full
reserves snapLength (default 1600, maximum 16384). Packet and samples share the
slot's exact lease and generation through Frame/Batch/View/Source and pool retirement.
Public packet methods and optional packet wire sections are registered in sole v1.

Only the per-build imported archive is patched; SDK sources/archives and immutable
Build Context files are unchanged. The patcher verifies whole-archive/member hashes
and each relocation's original symbol, offset and addend. RISC-V call instruction
bytes and Xtensa literal/relaxation pairs are checked before rewriting. Final linked
ELF placement/IRAM and RF coverage remain part of the Wi-Fi phase gate.

Production checks: C3/S3/C5 affected source syntax, four new ROM method entries,
feature-disabled omission and all three archive transformations. The Host resource
size check exposed a promoted unsigned-comparison warning; checked size_t addition
replaces it. Exact post-fix checks are recorded in `build/w05-csi-packet-evidence.json`.
Deferred fixtures use actual receipt/span/parser/resource/store/wire implementations,
plus real VM Frame/Batch/View/Source conversion, Nth allocation failure and moving GC.
They are only AST-parsed in this implementation wave. No test fixture was imported,
compiled or executed; no new firmware image was linked or flashed.

Runtime, RF, malformed-native-buffer, native-task scheduling and final linked archive
verification remain `not-run`. This batch does not qualify every RX origin on hardware
or complete the remaining CSI PHY/time/filter and common wireless budget work.
