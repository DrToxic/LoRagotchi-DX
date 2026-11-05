# PC Aquarium Water integration — copyable starter prompt

These are inspected **local source** facts, not a claim about current GitHub heads
or an installed package. Reinspect/preserve owner changes before implementation.
This prompt does not authorise publication, a release, flashing or upstream sync.

---

Implement Water species in the existing Aquarium plus its RX adapter where needed.
Use owner forks only, isolated feature worktrees and newest relevant owner-local
history. Read project notes/source first. Never clone/fetch the reference original
or overwrite unrelated changes. Reuse Panda3D, SQLite and existing MeshCore/link
adapters; retain independently functional desktop and HA apps, no paid service.

## Verified starting versions (recheck before edits)

- Water firmware: projects/loragotchi-dx-water, feature/water-edition, **candidate 1**;
  underlying MeshCore **v1.18.0**, protocol **LFG1**, not LFG2. Read WATER-EDITION.md,
  LoRaFishSpecies.h, LoRaFishCommit.h, MyMesh.cpp and tests at the completed commit.
- Standalone projects/loragotchi-aquarium-living: **0.10.0**,
  a01d9c87aa153dd391ff4c253fda0c3a2196082d.
  Older projects/loragotchi-community-tank is **0.2.1** at af0fa37, not latest local.
- HA projects/loragotchi-aquarium-ha-living: **0.3.0.dev12**,
  4db9f317fedad08a8b2ea50c5997fc36baf2f513.
- RX projects/loragotchi-rx-firmware: 808d41aa4b0a3541a77cf2901e00df8bd4f5ced3;
  separate busy-route worktree projects/loragotchi-rx-busy-mesh:
  04271a5e0d7bee833397aa6ae548373e588ff2a2. Inspect ancestry/accepted changes before
  selecting an RX base; a branch label does not prove a change merged elsewhere.

## Existing contracts: do not invent received metadata

Permanent uint16 Water registry v1 IDs:
0 tropical fish, 1 blowfish, 2 shark, 3 dolphin, 4 whale, 5 seal, 6 shrimp,
7 squid, 8 lobster, 9 crab, 10 octopus, 11 jellyfish, 12 shell-dweller, 13 sea star, 14 Anglerfish (owner fork extension).
Species is not stage, fish ID, hue or emoji/name. Unknown IDs round-trip exactly
and render a neutral fish fallback. Sizes are stylised, with no species visit ban.

Radio visits are text **LFG1**, full-public-key/transaction/fish-bound:

~~~text
LFG1|REQ|tx|fish|stage|name
LFG1|ACK|tx|fish|S1
LFG1|COMMIT|tx|fish|C|palette_body|palette_scales|palette_eyes|tint|H1|body_gene|scales_gene|eyes_gene|P1|panel|S1|species
~~~

H1 appears only if fine genes require it; P1/S1 follow it. Stage: 0 fry, 1 young,
2 adult, 3 elder. H1 valid genes: 0..6 or 8..247 (240 hue steps), 6=Mono. 7 and 255
are rendering-only sentinels, never persisted genes. Base fields are nearest legacy
palette; tint is uint16 RGB565. P1: 0 Blue, 1 White, 2 Yellow/Blue (fish lower region
transfers cyan). Existing ARRIVED/RECALL/RETURN/CANCEL1/AQUARIUM1 remain unchanged.

Only an exact expected-peer/transaction ACK with the **exact S1 or S1|A1 suffix** enables optional fields.
A plain ACK produces exactly the old nine fields, no H1/P1/S1 suffix. Old RX requires
n==9; do not advertise S1 until a new receiver actually preserves the metadata.
Cache/retry the same complete negotiated COMMIT; test loss of the first COMMIT.
Contact markers 🐟🐠🐡🦈🐬🐳🐋🦭🦐🦑🦞🦀🐙🪼🐚💦 are opt-in conventions, not species
identity or capability proof. Generic water symbols are not additional markers.
Raw names remain unchanged; outgoing pet labels are bounded, clean UTF-8.

**RX→PC is LRX/1, not text LFG1.** Exact companion signature [0x70,'R','X',1],
existing lease/identity/offset reads/event ACK. USB/TCP uses MeshCore length framing;
BLE uses one GATT value per companion frame. Existing snapshot **kind 1 is exactly
77 bytes**: event ID at 1, owner[32] at 5, fish ID at 37, tx at 41, stage45, mode46,
body/scales/eyes47/48/49 (0..6), tint LE16 at50, appearance flag52, 24-byte name53.
Kind 2 terminal return status is 11 bytes. Current decoder rejects other lengths/
invalid palette values. It contains **no species/H1/P1**. Do not silently append to
kind 1, repurpose padding, or claim updating the PC alone can recover missing data.

Old RX/PC can receive a safely returned legacy fish but cannot preserve species or
fine hues. Design/document an explicit negotiated new event kind/version for the
new metadata while retaining existing 77-byte events for old consumers. Prove old
PCs do not ACK/discard undecodable events and return jobs remain independent of
PC online state. Specify the exact chosen schema before implementing both ends;
that successor format is not defined or deployed by this Water firmware change.

**Adoption is separate LEG1/H1**, existing HELLO/READY/EGG/SAVED grammar carries
generation plus three genes, no species. Water explicitly treats those eggs as
species0. Acceptance bypasses local Mystery/setup and retains accepted generation/
genes including durable intent/completion replay. Species-aware adoption needs
its own compatible capability path; do not append fields old strict parsers reject
or infer an adopted species from a model/name/parent. No accepted egg rerolls.

## Implement and prove

1. One stable registry through decode/domain model/SQLite migrations, export/import,
   demo/replay/operator/reshare flows. Legacy missing values default fish; unknown
   IDs survive. Partial/old reshares must not erase earlier known species/genes.
2. Distinct native animated creatures for all 15, adapted to Aquarium's art style,
   not enlarged tiny firmware masks. Shared stage/genetic colour roles, neutral
   juvenile eyes, clear silhouettes/swim/crawl/pulse/tentacle movement, bounded
   frame time and low-quality fallback. Do not block species visits.
3. Extend RX parsing/durable journals/PC events under the version plan. Immutable
   captured identity/genes on retries, full-key binding, PC durable-save-before-ACK,
   replay/dedup after commit-before-ACK crash. Return original independently of PC
   capture/render state. Preserve bounded slots/return retries and AQUARIUM1's
   peer-claim (not attestation) trust boundary.
4. Keep fresh-game creature and colours independent Mystery options, resolved once
   and persisted; no navigation/reboot reroll. Preserve existing pets and durable
   adopted identity/genes. Do not break Water's existing legacy egg flow.
5. Test all species/native renders, old/new schemas, unknown IDs, malformed/overflow
   input, first-COMMIT loss, short/full duplicate/stale/wrong-full-peer packets,
   partial stream frames, save/ACK crash replay, outbox independence and old RX/PC
   interoperability. Fresh-package tests plus native multi-window/deadzone/context
   cleanup and HA device lifecycle regression. Label simulations vs physical tests.
6. Return exact commits/version/package metadata, production screenshots and limits.
   Use GitHub Actions for V3/V4 OLED/T096 firmware builds to conserve local resources;
   ask owner before source push needed to run CI. Do not claim binaries, Windows/
   Pi/GPU/radio acceptance, publication or delivery that did not occur.

Do not delete user stores/reset tanks to repair migrations. Keep source readiness,
firmware builds, PC schema, artwork, delivered artifacts and publication separate.
Complete scoped local work unless a concrete missing safety decision blocks it.
