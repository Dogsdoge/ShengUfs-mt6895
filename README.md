# ShengUfs-mt6895

MT6895 UFS block driver for UniXNU iOS 16, built by composing three proven layers.

> Status: 3-LAYER SKELETON COMPLETE. All three layers have code in this repo:
> L1 controller (HakiOS, device-verified), L2 extent mapping (host-tested),
> L3 IOMedia glue (Mt6895UfsBlock.cpp, design skeleton).
> No kernel build or device flashing has been done yet.

## Architecture (3-layer composition)

```
+-----------------------------------------------------------+
| Layer 3: IOMedia publication (UniXNU RootPublication)      |
|   ADT -> IOBlockStorageDevice -> IOMedia -> APFS mount     |
|   Source: UniXNU/ShengUfs reference/NativeUfs.cpp         |
+-----------------------------------------------------------+
| Layer 2: extent mapping (this repo: UfsExtents.c)          |
|   file block -> userdata raw block (u64 triples from ADT)  |
|   Source: ios-fuxi 0088 patch, host-tested (test_extents.c)|
+-----------------------------------------------------------+
| Layer 1: UFS host controller (this repo: UfsHc.c)          |
|   0x112b0000, UTRL list takeover, READ(10)/WRITE(10)       |
|   Source: HakiOS redwood_ufshc.c (device-verified on MT6895)|
+-----------------------------------------------------------+
```

## Why this composition

- UniXNU's stock ShengUfs is SM8550 (Qualcomm) specific: host 0x1d84000,
  Qualcomm SMMU stream 0x60. Wrong hardware for MT6895.
- HakiOS's redwood_ufshc.c drives the SAME MT6895 controller (0x112b0000,
  `mediatek,mt8183-ufshci`) and is device-verified (native UFS READ(10),
  APFS root mount, SpringBoard on xaga). Its hardware constants need no change.
- ios-fuxi's 0088 patch implements exactly the file-extent -> raw-block mapping
  we need (root image in a pinned file inside Android's userdata).
- UniXNU's publication layer is the required bridge for APFS root matching
  via ADT `root-matching` UUID.

## Files in this repo

| File | Role | Status |
|---|---|---|
| `UfsHc.c` / `UfsHc.h` | Layer 1: UFS controller core | From HakiOS (MT6895 verified), unmodified |
| `UfsExtents.c` / `UfsExtents.h` | Layer 2: extent mapping | New, host-tested via `test_extents.c` |
| `test_extents.c` | Host self-test for extent logic | ALL TESTS PASS |
| `Mt6895UfsBlock.cpp` | Layer 3: IOBlockStorageDevice with extent-mapped read | Design skeleton |
| `reference/RootLayout.h` | UniXNU manifest parser (for layer 3) | Upstream reference |
| `reference/RootWindowRead.h` | UniXNU window reader (for layer 3) | Upstream reference |

## Host test results

```
ux_load OK: 2 extents, ready=1
map(0) OK: part=47990000 run=1792
map(1792) OK: part=48010000 run=512
map(100) OK: part=47990100 run=1692
map(2304) correctly refused
write guard OK
invalid part bound correctly rejected
ALL TESTS PASSED
```
Build: `cc -Wall -I. -o test_extents test_extents.c UfsExtents.c && ./test_extents`

## Hardware facts (live read-only from RMX3562, 2026-11-24)

```
UFS controller: mediatek,mt8183-ufshci @ 0x112b0000, size 0x2300
IRQ: GIC SPI 0x89 (137), level
Clocks (10): phy_tx_sym, phy_rx_sym0/1, unipro_sys, unipro_sap,
             phy_sap, ufs_ck, ufs_fde_ck, ufs_fde, ufs
Resets (3): unipro_rst, crypto_rst, hci_rst
Power domains: <0x04 0x02>; vcc/vccq/vccq2 supplies
```

## Reference sources (upstream, pinned)

- HakiOS MT6895 UFS: https://github.com/ShuiLan114514/SLNeko-XNU (branch Xaga)
  - `xnu/files/bsd/dev/redwood_ufshc.c` / `.h`
- UniXNU ShengUfs: https://github.com/UniXNU/ShengUfs @ fd420df000f43213d9cd019725abff0849c98c72
  - `NativeUfs.cpp` (150KB, too large for this repo; fetch upstream)
- ios-fuxi extent patch: https://github.com/bangprovn/ios-fuxi
  - `xnu/patches/0088-fuxi-extent-root.patch`
- UniXNU UFS porting doc: https://unixnu.com/porting/ufs

## Remaining work

1. Write layer 3 glue: C++ `IOBlockStorageDevice` subclass that calls `ux_map()`
   and `UfsHc` read functions, publish IOMedia with root-matching UUID.
2. Wire ADT extent table parsing (mkadt.py already injects `unixnu-root-extents`).
3. Integrate with board.toml (`component:ShengUfs-mt6895`), defconfig `CONFIG_XNU_NATIVE_UFS=y`.
4. Build + device test (user flashes boot_a; U盘 firmware backup available).

## Safety

All work so far is on PC/GitHub. No device writes. Device is unlocked
(`oplusboot.verifiedbootstate=orange`) with full firmware backup on USB drive.
