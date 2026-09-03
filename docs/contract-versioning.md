# Contract versioning

`contract/can_proxy_contract.h` is the contract. This page says how it may
change and how a consumer pins it.

## Version numbers

The `0x400` status frame carries `contract major` (byte 0) and `contract
minor` (byte 1). They are the values of `CANPROXY_CONTRACT_MAJOR` and
`CANPROXY_CONTRACT_MINOR` in the header the proxy was built with.

| Bump | Allowed changes | Consumer built against an older header |
|---|---|---|
| **minor** (1.0 → 1.1) | new frame IDs; new capability or telltale bits taken from the reserved range; reserved bytes given a meaning; new enum values at the end of an enum | keeps working: it ignores IDs, bits and bytes it does not know, because reserved bits and bytes were required to be zero and unknown IDs were never filtered in |
| **major** (1.x → 2.0) | anything else: moving a field, changing a scale or SNA value, re-assigning a bit, changing a cycle or staleness window, removing a frame | must refuse: `canproxy_status_compatible()` returns false and the consumer shows "no vehicle data" rather than decoding garbage |

Rules that make minor bumps safe, enforced by the tests in `contract/tests`:

1. Reserved bits and bytes transmit as zero. A field added later always
   starts from a state an old reader treats as "nothing".
2. Bits 0-11 of the telltale frame and bits 0-12 of the capability bitmap
   never move. They predate this contract.
3. SNA encodings are fixed per width (`0xFF`, `0x80`, `0xFFFF`, `0x8000`,
   `0xFFFFFFFF`) and encoders never emit them for a valid value (clamping
   stops one short).
4. A new frame gets a new ID inside `0x400`-`0x4FF` and, if optional, a
   capability bit that says whether it is published at all.

## What the consumer does

- Decode `0x400` first. If `contract_major` is not the one in its header,
  treat the proxy as absent.
- Decode any frame whose ID it knows. Never assume a frame's presence from
  the contract version; use the capability bitmap and the staleness windows.
- Mask reserved bits after unpacking if it stores bitmaps; the pack helpers
  already strip them on the proxy side.

## How the cluster pins a copy

The proxy repository is the source of truth. A consumer vendors the header:

```
qt-cluster-demo/src/contract/can_proxy_contract.h     # verbatim copy
qt-cluster-demo/src/contract/CONTRACT_VERSION         # "1.1 <git tag> <sha256>"
```

Updating the copy is a deliberate commit that names the contract tag. A
submodule was rejected so the cluster stays buildable from a plain clone.

Tags in this repository: `contract-v<major>.<minor>` on the commit that
freezes a version. The header on a tagged commit is never edited afterwards
except for comments.

## Change log

### 1.1 (2026-09-04)

Additive over the 1.0 draft that lived in the cluster repository:

- `0x400` bytes 4-7: capability bitmap widened from 16 to 32 bits; bits
  13-17 assigned (state of health, charging state, cabin temperature, power
  state, assist frame present).
- `0x401` vehicle identity frame added (drivetrain, source kind, plugin
  version, vehicle id).
- `0x420` bytes 0-3: telltales widened from 16 to 32 bits; bits 12-19
  assigned (EV ready, charging, limited power, low traction battery, park
  brake, low fuel, fog, HV system fault).
- `0x450` driver assist and eco frame added, optional, gated by capability
  bit 17.
- Draft open points resolved: power state stays in `0x410` and gets
  capability bit 16; charging state stays in `0x430` and gets bit 14;
  capability bitmap may change at runtime but a consumer applies a 2 s
  hold-down before changing layout.

### 1.0 (draft, unreleased)

The draft in `qt-cluster-demo/docs/can-proxy-interface.md` before this
repository existed. Never emitted by any proxy.
