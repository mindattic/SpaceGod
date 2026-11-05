# Ultimate Spaceships Creator on Fab — second check (US-T108)

Independent second check of the open questions raised about Ultimate Spaceships Creator (USC),
its Expansion and Space Stations Creator, alongside Ryan's own check under US-T107. All of Fab's
listing pages return a Cloudflare bot challenge to a plain fetch, so every Fab page below was read
through an actual Chromium browser (Playwright/CDP, non-headless — headless was still challenged).
Dates checked: 2026-11-05.

## Listings identified

| Product | Seller | Fab listing URL |
|---|---|---|
| Ultimate Spaceships Creator (base) | Ebal Studios | https://www.fab.com/listings/e2e41618-a3e0-440b-be4b-78b01a1cd42f |
| Ultimate Spaceships Creator Expansion | Ebal Studios | https://www.fab.com/listings/5d35cb40-e273-4937-9764-f4d74662f246 |
| Space Station Creator Remastered (current "Space Stations Creator") | Ebal Studios | https://www.fab.com/listings/45dc8c53-bc7d-449e-88d6-14de01c22605 |
| "Space Creator Bundle" (the URL given as a starting point) | **Galactica Studio** (unrelated seller) | https://www.fab.com/listings/5a014582-91ae-4541-92a8-826e278896a4 |

The base listing's title ("Ultimate Spaceships Creator") and the Expansion's URL match what the
original request named. The "Space Stations Creator" product is live under the name **"Space
Station Creator Remastered"** — the original (unremastered) listing (`db926ef9-...`) says in its
own text that it has been replaced by this one; Remastered is the one still for sale.

## Open questions, answered

### 1. Does it support Unreal Engine 5.8?

**Yes, confirmed for all three Ebal Studios listings.** Each one's "Included formats → Unreal
Engine" tab states, verbatim: *"Unreal Engine versions 4.20 – 4.27 and 5.0 – 5.8"*.
- Base USC: confirmed on its listing page.
- Expansion: confirmed on its listing page (identical version string).
- Space Station Creator Remastered: confirmed on its listing page (identical version string).

Source: each listing's own "Included formats" panel, read 2026-11-05.

### 2. Does it have Nanite and LODs?

**Nanite: not supported / not advertised — confirmed by absence.** The word "Nanite" does not
appear anywhere on the base USC, Expansion, or Space Station Creator Remastered listing pages
(descriptions, feature lists, changelogs, or format/compatibility sections were all read in full).
Fab listings that do support Nanite state so explicitly in their compatibility section, so the
consistent silence across all three of Ebal Studios' own listings is a real negative signal, not
just an omission — these are traditional (non-Nanite) static meshes.

**LODs: not shipped, by design, and not present in the Fab description either.** None of the three
Fab listings mention LODs. Ebal Studios' own documentation (gitbook, Unity-focused, but describing
the same underlying meshes) is explicit about this by choice: *"LODs effect on performance for the
Ultimate Spaceships Creator is negligible. Thus I have not created any LODs for the meshes."*
(https://ebal-studios.gitbook.io/usc/tips/lods.md, read 2026-11-05 via an agent fetch — this page
was not blocked). There's a standing caution on every Fab listing too: *"Textures resolution and
detail level are not suitable for extreme closeup shots."*

So: no Nanite, no LODs, by the publisher's own design choice — not a gap in the listing, a
documented decision.

### 3. Which license does it use?

**Fab Standard License on all three (and on the miscategorized "bundle" listing too) — not an
older Unreal Marketplace EULA.** Each listing's "Details" panel links "Standard License" to
`https://www.fab.com/eula`, last updated October 1st, 2024. Read in full: it explicitly permits
commercial and private use, modifying the assets into a project, and "commercially distribute your
Projects with the Fab assets incorporated into it" — it covers commercial games. It replaced the
separate old Unreal Marketplace EULA and Unity Asset Store EULA when Epic unified both stores into
Fab; none of these three listings carry the older marketplace-specific terms.

Source: https://www.fab.com/eula, read 2026-11-05.

### 4. Are the Expansion and "Space Stations Creator" sold separately, or as a bundle?

**Sold separately. There is no Ebal Studios bundle combining Ultimate Spaceships Creator, its
Expansion, and Space Station Creator Remastered.** Each is its own standalone Fab listing with its
own price (see table below). The Expansion's own description states this outright: *"This pack is
NOT included in or part of the Ultimate Spaceships Creator, it's a standalone product that can be
used on its own without necessarily purchasing the Ultimate Spaceships Creator."* The "More from
Ebal Studios" carousel on all three listings (23 products total) lists the base USC, the Expansion,
Space Station Creator Remastered, and Hi-Rez Spaceships Creator as separate items — no combined SKU
appears anywhere, and Ebal Studios' own seller page lists no such bundle either.

**Important correction: the "Space Creator Bundle" URL given as a starting point is not an Ebal
Studios product at all.** `fab.com/listings/5a014582-91ae-4541-92a8-826e278896a4` is "Space Creator
Bundle" by a different, unrelated seller, **Galactica Studio** — a bundle of Alien Planet Creator,
Solar System Planets, Blackhole Creator, Galaxy System, and Space/Nebula Skyboxes (UE 5.3–5.4 and
5.8, $44.99–$55.99). It does not include Ultimate Spaceships Creator, the Expansion, or Space
Stations Creator in any form, and nothing on its page cross-references Ebal Studios. Mark the
premise "there's a bundle at this URL" as **false**, not "unconfirmed" — it was directly checked
and disproven.

## Current prices (checked 2026-11-05)

| Listing | Price | Published | Last updated |
|---|---|---|---|
| Ultimate Spaceships Creator (base, 510 spaceships) | **$99.99** | Feb 22, 2019 | Aug 9, 2026 |
| Ultimate Spaceships Creator Expansion (80 ships) | **$34.99** | Jul 10, 2021 | Aug 6, 2026 |
| Space Station Creator Remastered (110 stations) | **$74.99** | Apr 3, 2024 | Oct 26, 2026 |
| "Space Creator Bundle" (Galactica Studio — unrelated) | $44.99–$55.99 | Feb 7, 2026 | Nov 2, 2026 |

All three Ebal Studios prices are list prices shown before tax, under the Fab Standard License.

## Bottom line

- **Buy all three Ebal Studios listings individually** — there is no bundle discount available for
  ships + Expansion + stations; the cheapest way to get all three is the sum of their list prices:
  $99.99 + $34.99 + $74.99 = **$209.97**. Watch for a Fab sale instead of looking for a bundle SKU,
  since none exists.
- All three support Unreal Engine 5.8, use the current Fab Standard License (commercial-game-safe,
  not the old Marketplace EULA), and ship as traditional static meshes with **no Nanite and no
  LODs** — a deliberate choice by the publisher, not a missing spec. Plan mounting-point and LOD
  strategy for US-T89/US-T91/US-T102 accordingly: any Nanite/LOD behavior for hardpoints,
  break-apart sections, or nanite-repair/shield-array will have to be built on top of these meshes,
  not inherited from them.
- Ignore the "Space Creator Bundle" link from the original request — it is an unrelated product
  from a different seller and has no bearing on this purchase.

If these answers disagree with US-T107's findings once that ticket's note lands, the disagreement
to flag is specifically the bundle question: confirm whether US-T107 also found that the given
"Space Creator Bundle" URL is unrelated to Ebal Studios, since that is the one point most likely to
be mis-copied as "a real bundle exists" if not checked directly against the live page.
