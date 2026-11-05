# Research: Ultimate Spaceships Creator on Fab (US-T107)

Date checked: 2026-11-05

Context: US-T103 picked Ultimate Spaceships Creator (Ebal Studios) as the ship art for the
hardpoints (US-T89), break-apart sections (US-T91) and nanite-repair/shield-array (US-T102)
tickets. This note answers the four open questions from that pick before purchase.

## Correction to the starting links

One of the two "Starting points" URLs supplied with this ticket is **not** what it was labeled.
`https://www.fab.com/listings/5a014582-91ae-4541-92a8-826e278896a4` was described as the "Space
Creator Bundle" matching USC's style, but it is a **different publisher's unrelated product**:
"Space Creator Bundle" by **Galactica Studio** (Alien Planet Creator, Solar System Planets,
Advance Spaceship Flying System, Blackhole Creator, Galaxy System, and skyboxes — planets and
flight tooling, not modular ship/station kits). It has no connection to Ebal Studios, Ultimate
Spaceships Creator, or Space Stations Creator. It is listed below for completeness but should not
factor into the purchase decision.

## Listings found

| Pack | Publisher | Fab URL | Price |
|---|---|---|---|
| Ultimate Spaceships Creator (base) | Ebal Studios | https://www.fab.com/listings/e2e41618-a3e0-440b-be4b-78b01a1cd42f | $99.99 (seen on sibling-listing sidebar cards; this listing's own price box could not be fetched — see Q3/Q4 notes) |
| Ultimate Spaceships Creator Expansion | Ebal Studios | https://www.fab.com/listings/5d35cb40-e273-4937-9764-f4d74662f246 | $34.99 |
| Space Station Creator Remastered (= "Space Stations Creator") | Ebal Studios | https://www.fab.com/listings/45dc8c53-bc7d-449e-88d6-14de01c22605 | $74.99 |
| "Space Creator Bundle" (unrelated product, see correction above) | Galactica Studio | https://www.fab.com/listings/5a014582-91ae-4541-92a8-826e278896a4 | $44.99 (bundle; "Total Individual Asset Value: $128.93, Save Over 65%") |

Fab blocks plain fetches; all three Ebal Studios pages (and the unrelated Galactica Studio page)
were read through the `r.jina.ai` read-only rendering proxy. The base pack's page repeatedly
truncated before its "Compatibility and file information" / "Details" blocks across multiple
retrieval attempts (direct fetch: 403; Fab API-shaped URL guess: 403; alternate proxies
`allorigins.win` and `corsproxy.io`: 500/401; Wayback Machine snapshot exists dated 2026-07-15 but
this environment's tools refuse to fetch `web.archive.org`). Where the base pack's own page text
could not be confirmed, it is marked UNCONFIRMED below rather than inferred from its sibling
listings.

## The four open questions

### 1. Does it support Unreal Engine 5.8?

- **Expansion** — CONFIRMED. Fab listing, "Compatibility and file information": "Unreal Engine
  versions 4.20 – 4.27 and 5.0 – 5.8." (https://www.fab.com/listings/5d35cb40-e273-4937-9764-f4d74662f246, checked 2026-11-05)
- **Space Station Creator Remastered** — CONFIRMED, identical text: "Unreal Engine versions
  4.20 – 4.27 and 5.0 – 5.8." (https://www.fab.com/listings/45dc8c53-bc7d-449e-88d6-14de01c22605, checked 2026-11-05)
- **Base Ultimate Spaceships Creator pack** — UNCONFIRMED. The page's own "Compatibility and file
  information" section could not be retrieved despite repeated attempts (see method note above).
  Both sibling Ebal Studios listings on the same engine/format set state "4.20 – 4.27 and 5.0 –
  5.8," which makes it likely the base pack matches, but that is an inference, not something read
  on the base pack's own page, so it is not being reported as confirmed.
- The Ebal Studios blog (ebalstudios.com/blog/usc) and gitbook (ebal-studios.gitbook.io/usc) are
  written for the **Unity** release and contain no Unreal Engine version numbers at all — they add
  no confirmation either way for UE5.8.

**Action before buying:** open the base pack's Fab page directly in a browser and confirm its
"Compatibility and file information" block reads 5.0–5.8 (or whatever it currently shows) before
purchase.

### 2. Does it have Nanite and LODs?

UNCONFIRMED on both counts, for every page that could be read (Expansion, Space Station Creator
Remastered; base pack's relevant sections never loaded).

- **Nanite:** the word "Nanite" does not appear anywhere in the Features lists, descriptions, tags,
  or compatibility sections of the Expansion or Space Station Creator Remastered listings, nor in
  any Ebal Studios blog/gitbook page. Circumstantial signal against it: both confirmed listings
  state compatibility back to Unreal Engine 4.20, a version that predates Nanite (UE5-only)
  entirely — suggesting conventional static meshes, but that is an inference from the version
  range, not an explicit statement, so it is not marked confirmed.
- **LODs:** no Fab listing's Features bullets mention LODs. The gitbook has a dedicated LODs page
  (https://ebal-studios.gitbook.io/usc/tips/lods.md) that states, for the Unity release
  specifically: "LODs effect on performance for the Ultimate Spaceships Creator is negligible. Thus
  I have not created any LODs for the meshes... My tests indicate LODs hardly have any positive
  effect on the performance of a project containing the Ultimate Spaceships Creator." That page is
  Unity-specific (it references Unity's own LOD import feature) so it cannot be taken as a literal
  statement about the Unreal/Fab build, but combined with the absence of any LOD feature bullet on
  the Fab pages, there is no positive evidence of authored LODs on the Unreal side, and the
  publisher's stated policy for this asset family is not to build them.

**Action before buying:** ask Ebal Studios directly (or check the gitbook's Unreal-specific pages,
if any exist) whether the Fab/Unreal build includes Nanite meshes or authored LODs, since nothing
found states this either way for Unreal.

### 3. Which licence does it use?

- **Expansion** — CONFIRMED Standard License. Fab "Details" box: "License terms: Standard License"
  (links to https://www.fab.com/eula). (https://www.fab.com/listings/5d35cb40-e273-4937-9764-f4d74662f246, checked 2026-11-05)
- **Base pack and Space Station Creator Remastered** — UNCONFIRMED by direct page text (the
  Details/License-terms block did not load for either page in repeated attempts). However, Fab's
  platform-wide EULA (https://www.fab.com/eula, dated 2024-10-01) states the Standard License is
  the default for all content on the platform that isn't tagged open-source/Alternative License:
  "when you complete a Transaction for a License Tier you are eligible for, the Content Licensor
  grants you a Standard License." Neither Ebal Studios listing is tagged open-source, so Standard
  License almost certainly applies to all three packs, but this is inferred from the platform-wide
  default, not read directly off those two pages, so it is reported as likely rather than
  confirmed.
- No reference to an older, separate "Marketplace License" was found anywhere; Fab's EULA only
  distinguishes "Standard License" vs "Alternative License" (open-source-tagged content) and
  license *tiers* (Personal-Reference-Only / Personal / Professional) — a different axis than
  "Standard vs Marketplace." There is no indication any Ebal Studios listing still carries an
  old Unreal Marketplace-era licence.
- What the Standard License says about commercial games, quoted from the Fab EULA: "You may
  Distribute software applications (such as video games) that include Content to the general
  public, whether directly by you or through a distributor or publisher," subject to: "Projects you
  Distribute must reasonably add value beyond the value of the Content and the Content must be
  merely a component of the Project and not the primary focus of the Project" (i.e. the asset can't
  be resold stand-alone). This covers SpaceGod's use case (ship art inside a game).

### 4. Are the Expansion and Space Stations Creator sold separately or bundled?

CONFIRMED: sold **separately**. No combined discount bundle exists for USC + Expansion + Space
Stations Creator.

- Base pack's own description: "If you want to expand this collection and obtain more spaceships,
  you can also consider the: Ultimate Spaceships Creator Expansion pack **which is not included in
  this collection**."
- Expansion's own description, the reverse: "This pack is NOT included in or part of the Ultimate
  Spaceships Creator, it's a standalone product that can be used on its own without necessarily
  purchasing the Ultimate Spaceships Creator."
- The full Ebal Studios Fab seller catalog (https://www.fab.com/sellers/Ebal%20Studios, 24
  listings, checked 2026-11-05) has no listing that bundles the base pack, the Expansion and Space
  Station Creator Remastered together. Each is priced and sold on its own: Space Station Creator
  Remastered from $74.99, Ultimate Spaceships Creator Expansion from $34.99, plus the base pack's
  own $99.99.
- The only "bundle" found in this research anywhere is the unrelated Galactica Studio "Space
  Creator Bundle" (see Correction above), which bundles that publisher's own planet/skybox/flight
  assets — not Ebal Studios' spaceship/station kits.

Buying all three separately totals **$209.97** ($99.99 + $34.99 + $74.99); there is no cheaper
bundled path.

## Changelog notes checked for engine/Nanite/LOD mentions

Latest-update entries on all three Ebal Studios Fab listings (checked 2026-11-05) only cover
content additions (new ship/station examples) and material/blueprint optimizations — none mention
an engine-version change, Nanite, or LODs. The gitbook's full Unity-side update history (versions
1.5–7.1) is entirely about Unity render-pipeline changes (Built-In → URP → HDRP) and per-set
material remakes; it has no Unreal Engine references and does not discuss LODs (that lives on the
separate LODs page quoted above).

## Bottom line

- **What to buy:** the base **Ultimate Spaceships Creator** ($99.99) plus the **Expansion**
  ($34.99) for ship variety and the hardpoint/break-apart/nanite-repair tickets, plus
  **Space Station Creator Remastered** ($74.99) for matching-style stations — **$209.97 total**,
  bought as three separate Fab purchases (there is no bundle). This is confirmed as the cheapest
  path to all three; no combined SKU exists to shop around for.
- **Before clicking buy**, resolve the two items this research could not confirm from primary
  sources: (a) whether the *base* pack's own Fab page states UE5.8 support (very likely yes, by
  pattern with its two siblings, but unverified directly), and (b) whether any of the three packs
  actually contain Nanite meshes or authored LODs for the Unreal build — nothing found confirms
  this either way, and the publisher's own stated LOD policy (for the Unity release) is that it
  doesn't build them because the performance gain was negligible in their testing. If Nanite/LOD
  support is a hard requirement for SpaceGod's render pipeline, confirm it with Ebal Studios
  directly first.
- The Standard License (confirmed on the Expansion, very likely identical on the other two) covers
  shipping SpaceGod as a commercial game with this art embedded as a component — no licence blocker
  found.
