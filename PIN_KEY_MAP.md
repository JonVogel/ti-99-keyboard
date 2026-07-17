# TI-99/4A Adapter — Pin ↔ Key Debug Map

Every key sits at one **column** (read line) and one **row** (drive line), and
works only if BOTH pins are good. So when a *set* of keys dies together, find
the line they share:

- Keys sharing a **column** → that column's read pin is bad.
- Keys sharing a **row** → that row's drive pin is bad.
- A single dead key → its column *or* row (test a neighbor to tell which).

## Column read lines (ESP32 inputs)

| GPIO | TI pin | Col | Keys on this line |
|:----:|:------:|:---:|-------------------|
| 17 | 8  | col0 | `/  ;  P  0  Z  A  Q  1` |
| 12 | 13 | col1 | `.  L  O  9  X  S  W  2` |
| 13 | 14 | col2 | `,  K  I  8  C  D  E  3` |
| 14 | 15 | col3 | `M  J  U  7  V  F  R  4` |
| 18 | 9  | col4 | `N  H  Y  6  B  G  T  5` |
| 11 | 12 | col5 | `=  Space  Enter  SHIFT  CTRL  FCTN` |

## Row drive lines (ESP32 open-drain outputs)

| GPIO | TI pin | Row | Keys on this line |
|:----:|:------:|:---:|-------------------|
| 15 | 5  | row0 | `/  .  ,  M  N  =` |
| 7  | 4  | row1 | `;  L  K  J  H  Space` |
| 4  | 1  | row2 | `P  O  I  U  Y  Enter` |
| 5  | 2  | row3 | `6  7  8  9  0` |
| 10 | 11 | row4 | `Z  X  C  V  B` |
| 6  | 3  | row5 | `A  S  D  F  G  SHIFT` |
| 9  | 10 | row6 | `Q  W  E  R  T  CTRL` |
| 16 | 7  | row7 | `1  2  3  4  5  FCTN` |

## Notes

- Letters and digits live entirely on **col0–col4**. **col5 (GPIO11 / TI pin 12)**
  carries no letters/digits — only `=`, Space, Enter, and the SHIFT/CTRL/FCTN
  modifiers. A dead col5 → no Enter/Space/modifiers, but normal lowercase typing.
- **BOB grouping** (which daughterboard a TI pin routes through), for knowing
  which channel to reheat: BOB#1 = TI pins 1–4, BOB#2 = 5–9, BOB#3 = 10–13,
  BOB#4 = 14–15.
- Quick full-column sweep on one row: **Z X C V B** (row4) or **6 7 8 9 0**
  (row3) hits col0–col4; add **Enter** for col5.
