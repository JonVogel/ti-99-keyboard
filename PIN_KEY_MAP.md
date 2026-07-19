# TI-99/4A Adapter — Pin ↔ Key Debug Map (v4 / straight ribbon)

> **v4 GPIO map** (straight-through ribbon; J10 pin *p* → TI pin 16−*p*).
> For rev-3 boards (180°-twisted ribbon) see this file at the `v3` git tag.

Every key sits at one **column** (read line) and one **row** (drive line), and
works only if BOTH pins are good. So when a *set* of keys dies together, find
the line they share:

- Keys sharing a **column** → that column's read pin is bad.
- Keys sharing a **row** → that row's drive pin is bad.
- A single dead key → its column *or* row (test a neighbor to tell which).

## Column read lines (ESP32 inputs)

| GPIO | TI pin | J10 pin | Col | Keys on this line |
|:----:|:------:|:-------:|:---:|-------------------|
| 18 | 8  | 8 | col0 | `/  ;  P  0  Z  A  Q  1` |
| 6  | 13 | 3 | col1 | `.  L  O  9  X  S  W  2` |
| 5  | 14 | 2 | col2 | `,  K  I  8  C  D  E  3` |
| 4  | 15 | 1 | col3 | `M  J  U  7  V  F  R  4` |
| 17 | 9  | 7 | col4 | `N  H  Y  6  B  G  T  5` |
| 7  | 12 | 4 | col5 | `=  Space  Enter  SHIFT  CTRL  FCTN` |

## Row drive lines (ESP32 open-drain outputs)

| GPIO | TI pin | J10 pin | Row | Keys on this line |
|:----:|:------:|:-------:|:---:|-------------------|
| 9  | 5  | 11 | row0 | `/  .  ,  M  N  =` |
| 10 | 4  | 12 | row1 | `;  L  K  J  H  Space` |
| 13 | 1  | 15 | row2 | `P  O  I  U  Y  Enter` |
| 12 | 2  | 14 | row3 | `6  7  8  9  0` |
| 15 | 11 | 5  | row4 | `Z  X  C  V  B` |
| 11 | 3  | 13 | row5 | `A  S  D  F  G  SHIFT` |
| 16 | 10 | 6  | row6 | `Q  W  E  R  T  CTRL` |
| 8  | 7  | 9  | row7 | `1  2  3  4  5  FCTN` |

**GPIO3** = Alpha Lock (TI pin 6, J10 pin 10) — wired in v4 but never driven
(software alpha lock); no keys map to it.
**GPIO14** = SPARE repair channel (BOB#4-CH1 → J15 solder pad) — jumper J15 to
any dead line's pad and remap that line's `#define` to 14.

## Notes

- Letters and digits live entirely on **col0–col4**. **col5 (GPIO7 / TI pin 12)**
  carries no letters/digits — only `=`, Space, Enter, and the SHIFT/CTRL/FCTN
  modifiers. A dead col5 → no Enter/Space/modifiers, but normal lowercase typing.
- **BOB grouping** (which daughterboard a line routes through), for knowing
  which channel to reheat — group by **J10 pin**: BOB#1 = J10 1–4,
  BOB#2 = J10 5–8, BOB#3 = J10 9–12, BOB#4 = J10 13–15.
- Quick full-column sweep on one row: **Z X C V B** (row4) or **6 7 8 9 0**
  (row3) hits col0–col4; add **Enter** for col5.
