# Gameplay-scale proportion sheet (US-T179)

`ScaleModel::ShipLength` is now one linear map for all hardware (ships, stations, gates, harvesters, wrecks, debris, pods):
realistic = km/1000 units; gameplay = 7 units/km. The old gameplay map was 12*km^0.5, which squeezed a 43x length ratio into 6.5x.

Only hardware-vs-planet (R^0.45) and orbital distances (AU^0.55, local p=0.44) are compressed. All views listed below call `ShipLength`:
StructureViews, HarvesterView, GraveyardView, DebrisFields, GateView, CargoPodsView, PodBeacons, EmbeddedPods, FighterViews, BioMassView, main ship render.

| Hardware | Length km | Realistic units | Gameplay units | Ratio to carrier |
|---|---|---|---|---|
| Carrier | 3.2 | 0.0032 | 22.40 | 1:1.0 |
| Command cruiser | 1.6 | 0.0016 | 11.20 | 1:2.0 |
| Battleship | 2.4 | 0.0024 | 16.80 | 1:1.3 |
| Gate breaker | 2.0 | 0.0020 | 14.00 | 1:1.6 |
| Cruiser | 1.2 | 0.0012 | 8.40 | 1:2.7 |
| Defence platform | 1.4 | 0.0014 | 9.80 | 1:2.3 |
| Freighter | 0.6 | 0.0006 | 4.20 | 1:5.3 |
| Destroyer | 0.45 | 0.0004 | 3.15 | 1:7.1 |
| Frigate | 0.32 | 0.0003 | 2.24 | 1:10.0 |
| Ore hauler | 0.3 | 0.0003 | 2.10 | 1:10.7 |
| Lane tender | 0.26 | 0.0003 | 1.82 | 1:12.3 |
| Explorer | 0.22 | 0.0002 | 1.54 | 1:14.5 |
| Corvette | 0.22 | 0.0002 | 1.54 | 1:14.5 |
| Ore tug | 0.2 | 0.0002 | 1.40 | 1:16.0 |
| Gunship | 0.14 | 0.0001 | 0.98 | 1:22.9 |
| Bomber | 0.14 | 0.0001 | 0.98 | 1:22.9 |
| Assault shuttle | 0.12 | 0.0001 | 0.84 | 1:26.7 |
| Gate picket | 0.12 | 0.0001 | 0.84 | 1:26.7 |
| Capture tug | 0.12 | 0.0001 | 0.84 | 1:26.7 |
| Fighter | 0.1 | 0.0001 | 0.70 | 1:32.0 |
| Courier | 0.08 | 0.0001 | 0.56 | 1:40.0 |
| Recon | 0.08 | 0.0001 | 0.56 | 1:40.0 |
| Interceptor | 0.07 | 0.0001 | 0.49 | 1:45.7 |
| Breacher round | 0.06 | 0.0001 | 0.42 | 1:53.3 |
| Cargo pod | 0.014 | 0.0000 | 0.10 | 1:228.6 |
| Gate (GateView) | 25 | 0.0250 | 175.0 | 7.8x |
| Station ring (StructureViews) | 10 | 0.0100 | 70.0 | 3.1x |

Both scales share identical ratios between rows (blend interpolates in log space between two linear maps, so every blend does too). Earth for reference: radius 255 units gameplay, 6.371 realistic.
