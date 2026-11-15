# DigiKey cart changes after the 2026-11-09 design review

Quantities are for 4 devices with spares, as in `ORDER_4x_STM32G431kbt6.csv`.

## Remove

| Remove | Was | Why |
|---|---|---|
| RG16N150KW**TR**-ND (Susumu 150k 0.05%) | 25 | Full-reel listing. Replaced by the cut-tape listing below. |
| Bourns MF-NSMF050-2 (F1) | 10 | Up to 0.75 Ω. The start-up surge could drop VBAT below the TPS65131 UVLO. |

## Add

| Add | MPN | DigiKey PN | Qty | Refs | Why |
|---|---|---|---|---|---|
| Susumu 150k 0.05% 0603, **cut tape** | RG1608N-154-W-T1 | RG16N150KW**CT**-ND | 25 | R11–R14, R16 | Same part as before, cut-tape listing. Lead time is ~22 weeks, so check stock. |
| Bourns PTC 1.1 A, 1206 | MF-NSMF110-2 | search the MPN | 10 | F1 | 0.06–0.20 Ω. Same footprint. |
| **Slkor** (Shenzhen Slkormicro) TVS, bidirectional, 18 V, SOD-123FL | SMF18CA | Slkor listing | 10 | D5 | ESD protection at the electrode pad J4 (new). Littelfuse was out of stock; same standard part number and ratings. |
| Lite-On green LED 0603 | LTST-C191KGKT | 160-1446-1-ND | 10 | D6 | Status LED on PA3 (new) |

## Change quantity

| Line | MPN | Old qty | New qty | Refs | Why |
|---|---|---|---|---|---|
| 10 µF 50 V X5R 1206 | UMK316BBJ106ML-T | 25 | **50** | C24–C27, **C34, C35** | 2 new VBAT bulk caps per board, so 24 are needed for 4 boards |
| 390 Ω 1% 0603 | RT0603FRE07390RL | 10 | **25** | R32, **R34** | New LED resistor reuses this part, so 8 are needed for 4 boards |

## Check the exact orderable part (quantity unchanged)

| Line | Make sure the cart item is | Qty |
|---|---|---|
| U9 photoMOS | **TLP175A(TPL,E** (Toshiba, tape) | 6 |
| J2 battery connector | **S2B-PH-SM4-TB(LF)(SN)** (JST) | 6 |

## No cart change

- C13 was relabeled X5R to match the part you already chose (C0402B105K025T).
- R15/R21 gained an MPN field (RG1608P-101-B-T5). The DigiKey line RG16P100BCT-ND is unchanged.

Net change per device: +5 parts (C34, C35, D5, D6, R34), F1 swapped. Per device 97 placed parts to buy; for 4 devices, 388 needed and 792 suggested.
