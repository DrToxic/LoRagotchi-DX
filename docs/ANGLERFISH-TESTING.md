# Anglerfish Water extension: verification and limits

Owner repository: DrToxic/LoRagotchi-DX. Base: main
4a7b08cd5a9ffedf81f12d5073560d9faffdd040 (2026-11-05).
Development uses an isolated feature/water-anglerfish-20261105 worktree; the
original dirty checkout, other worktrees, external projects and reference
upstream are not changed. Publication is a review PR, not a merge/release/flash.

## Scope

- Append Anglerfish=14; old Water 0..13 and Air 256..273 stay permanent.
- Water catalogue/chooser/Mystery: 15. Air: 18. DX: original fish only.
- Two 32x24 semantic masks add 384 bytes ROM, 5,376 bytes total Water assets.
  No renderer heap, extra framebuffer, serializer, protocol or timer changes.
- All original 4,992 mask bytes have a fixed SHA-256 regression guard derived
  independently from owner main, not regenerated from the candidate.
- Production rendering uses the established fry/young/adult/elder dispatch,
  palette genes and juvenile tints, mirrored home/visitor/travel paths.
  Egg/memorial timing and art are unchanged; elder shares the adult mask as
  established for Water species.

## Local gates

Native: 335/335 cases in native/native_water/native_air/native_kiss_modem.
Production Python suites cover serializers on ESP32 and nRF52 mocks, all 15
species and unknown uint16 IDs, old/truncated/corrupt settings, inheritance,
Mystery persistence, explicit selection/Start pet, menu bounds, adoption and
all existing lifecycle/happiness/custody checks. A preserved 14-choice registry
fixture separately encodes old SPC1 records: resolved choices do not reroll on
boot; unresolved/reselected choices use 15; formerly opaque 14 is recognized;
15/65535 and mystery-colour mapping remain unchanged (both storage branches).

Water pixels: 34,656 counted scenarios per style, three styles (103,968), plus
explicit lure/stalk checks on both mono themes, two phases and four stages.
Air pixels: 44,368 per style, three styles, including Water14 visitors.
The ASan/UBSan C++11 resource probe checks all 65,536 registry IDs, clipped
frames, unchanged 18-byte SPC1 and bounded 127-byte extended COMMIT.
Existing independent juvenile golden signatures are not regenerated.

Wire coverage includes all 15 species, full-key binding, malformed/overflow and
duplicate S1 rejection, immutable guest identity, loss of first Anglerfish
COMMIT/frozen retries, exact S1/A1 negotiation, preserved old S1 Water unknown14
retention/fish fallback, and the actual old RX nine-field/PC SQLite path.
The last cross-repo check and busy-mesh check use read-only sibling checkouts;
standalone CI explicitly prints them as NOT RUN, not silently passed. The strict
legacy RX fixture is revision 808d41aa4b0a3541a77cf2901e00df8bd4f5ced3; its
Aquarium decoder/SQLite fixture is af0fa374423f9eeea690c409082ac7cb6b6edac1.
These are intentionally older-parser tests, not current downstream art support.

## Hosted device matrix

[Dedicated workflow](../.github/workflows/anglerfish-builds.yml): 14 unique Water
configurations (10 standard, 4 experimental) plus six DX/Air controls (V3,
V4 OLED, T096), **20 images**. Water covers V3/V4, T096/T114, M2, Cardputer,
T-Deck BLE/USB, L1/L1-1W, experimental RAK4631, T-Beam 1W, Station G2/G3 ESP32.
RAK4630 is the identical RAK4631 configuration alias and is not compiled twice;
the existing alias guard checks all 15 Water and 15 Air names.

GitHub-hosted only; no full local MCU compilation. Exact pushed revision,
read-only permissions, Python 3.12/PlatformIO 6.1.19, three parallel jobs with
one compile worker each, fail-fast false, host gates before builds, bounded
timeouts, branch-scoped push/manual triggers and superseded-run concurrency.
Existing workflows/triggers are untouched. Artifacts include exact source SHA,
resolved config, dependencies, board/linker/partition evidence and SHA256SUMS;
retention is 14 days. Diagnostics remain available for failed/oversized profiles.
A declared target is not a successful build: verify the exact head SHA and
terminal job conclusions before using an artifact. Validate ESP checksum/hash,
partition MD5/app fit and UF2 blocks/family/linker bounds after downloading.
No changed partitions/features are permitted merely to turn a failure green.

## Reproduce artwork

    python3 test/ui/check_water_rendering.py --preview-dir /tmp/water-frames
    uv run --with pillow==12.1.0 python tools/water/preview_anglerfish.py /tmp/water-frames/plain docs/images

The committed PNG/GIF are nearest-neighbour labelled production framebuffer
captures. The generator is editable at tools/water/generate_sprites.py; pinned
Pillow 12.1.0 reproduces LoRaFishWaterArt.h. CI needs no Pillow.

## Compatibility / physical acceptance

See [Water extension contract](../WATER-EDITION.md#anglerfish-extension-and-mixed-registries).
The catalogue expansion changes unresolved/reselected Water Mystery mapping;
already selected species/eggs are persisted unchanged. Formerly opaque ID14
now presents as Anglerfish; its saved numeric identity is not changed. H1 colour mapping and
Air Mystery mapping do not change. No reset or save migration is needed.

S1 is not a registry inventory or global registry namespace. External RX,
Aquarium, HA and Lodestar applications are not upgraded by this PR. Old peers
can display fish or another local interpretation if they independently assigned
14; no unsupported global interoperability claim is made. Legacy LEG1 eggs
remain fish-only. Legacy RX snapshots do not preserve unsupported species.

Physical panel appearance/timing, radio visits, save power cuts, current draw,
and all experimental carrier/display/input variants remain **unverified**.
T096 retains the existing 128x64 logical display path. Test mono light/dark,
colour genes and all ages, reset/chooser/reboot, succession/adoption, mixed-peer
visits/retries/return, wake/sleep and sustained LoRa/BLE before hardware acceptance.
