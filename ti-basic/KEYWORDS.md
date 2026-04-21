# TI Extended BASIC Keyword Status

Status legend:
- **Impl** — implementation in place in the simulator
- **Test** — verified working on device with at least one program
- *(blank)* — not yet implemented / not yet tested

---

## Immediate-mode commands

| Keyword        | Impl | Test | Notes                                          |
|----------------|:----:|:----:|------------------------------------------------|
| NEW            |  ✅  |  ✅  | Clears program and screen                      |
| RUN            |  ✅  |  ✅  |                                                |
| LIST           |  ✅  |  ✅  | Full, single line, range: LIST n / n- / -m / n-m |
| OLD            |  ✅  |  ✅  | Loads from LittleFS as `/NAME.bas`             |
| SAVE           |  ✅  |  ✅  | Saves text form to LittleFS                    |
| MERGE          |  ✅  |  ✅  | Fold file into current program; collisions win |
| BYE            |  ✅  |  ✅  | Restarts the ESP32                             |
| CAT / CATALOG  |  ✅  |  ✅  | `CAT [FLASH\|SDCARD\|DSK1..3]`; `DIR` is an alias |
| SIZE           |  ✅  |  ✅  | Prints free heap + estimated program space     |
| NUMBER / NUM   |  ✅  |  ✅  | Auto line-number input mode                    |
| RESEQUENCE/RES |  ✅  |  ✅  | Renumbers lines + GOTO/GOSUB/THEN/ELSE targets |
| BREAK          |  ✅  |  ✅  | Set breakpoint line list                       |
| UNBREAK        |  ✅  |  ✅  | Clear breakpoints                              |
| TRACE          |  ✅  |  ✅  | Prints `<lineN>` before each line              |
| UNTRACE        |  ✅  |  ✅  |                                                |
| CON/CONTINUE   |  ✅  |  ✅  | Resumes after STOP / breakpoint / BREAK        |
| DELETE (file)  |  ✅  |  ✅  | `DELETE NAME` or `DELETE "NAME"`, removes .bas |

## Program control flow

| Keyword        | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| IF / THEN      |  ✅  |  ✅  |                                            |
| ELSE           |  ✅  |  ✅  |                                            |
| FOR / TO       |  ✅  |  ✅  |                                            |
| STEP           |  ✅  |  ✅  |                                            |
| NEXT           |  ✅  |  ✅  |                                            |
| GOTO / GO TO   |  ✅  |  ✅  |                                            |
| GOSUB          |  ✅  |  ✅  |                                            |
| RETURN         |  ✅  |  ✅  |                                            |
| ON ... GOTO    |  ✅  |  ✅  |                                            |
| ON ... GOSUB   |  ✅  |  ✅  |                                            |
| END            |  ✅  |  ✅  |                                            |
| STOP           |  ✅  |  ✅  |                                            |
| REM            |  ✅  |  ✅  |                                            |
| `!` tail comment |  ✅  |  ✅  | TOK_BANG 0x83; stops statement + line processing |
| ON BREAK       |  ✅  |  ✅  | STOP (default) / NEXT                      |
| ON ERROR       |  ✅  |  ✅  | `<line>` / STOP (default) / NEXT; disarms on entry |
| ON WARNING     |  ✅  |  ✅  | STOP (→error) / PRINT (default) / NEXT         |

## Variables, assignment, I/O

| Keyword        | Impl | Test | Notes                                        |
|----------------|:----:|:----:|----------------------------------------------|
| LET (implicit) |  ✅  |  ✅  | Bare `VAR = expr` supported                  |
| DIM            |  ✅  |  ✅  | 1D/2D/3D arrays, numeric and string          |
| OPTION BASE    |  ✅  |  ✅  | 0 or 1                                       |
| PRINT          |  ✅  |  ✅  | `;` `,` `TAB()` supported                    |
| INPUT          |  ✅  |  ✅  |                                              |
| LINPUT         |  ✅  |  ✅  |                                              |
| DISPLAY AT     |  ✅  |  ✅  |                                              |
| ACCEPT AT      |  ✅  |  ✅  |                                              |
| READ           |  ✅  |  ✅  |                                              |
| DATA           |  ✅  |  ✅  |                                              |
| RESTORE        |  ✅  |  ✅  |                                              |
| RANDOMIZE      |  ✅  |  ✅  |                                              |
| DEF            |  ✅  |  ✅  | `DEF FNx(y)=expr` single-line, numeric + `FNx$` |
| SUB            |  ✅  |  ✅  | Numeric+string params, pass-by-value-result  |
| SUBEND         |  ✅  |  ✅  |                                              |
| SUBEXIT        |  ✅  |  ✅  | Early return from subprogram                 |
| OPEN           |  ✅  |      | `OPEN #n:"FLASH.NAME"` or `"DSK1.NAME"`; DSK1..3 = V9T9 `.dsk` (Phase 1: DIS/VAR INPUT only) |
| CLOSE          |  ✅  |      | `CLOSE #n [,#m ...]`                         |
| PRINT #        |  ✅  |      | One line per statement; `;` and `,` work     |
| INPUT #        |  ✅  |      | Comma-split from one line                    |
| LINPUT #       |  ✅  |      | Whole line into first string var             |
| EOF(n)         |  ✅  |      | -1 at end-of-file, 0 otherwise               |
| RESTORE #      |      |      |                                              |
| DELETE (file)  |      |      |                                              |
| IMAGE          |      |      | Format string for PRINT USING                |
| PRINT USING    |      |      |                                              |
| DISPLAY USING  |      |      |                                              |

## Operators

| Operator       | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| `+ - * / ^`    |  ✅  |  ✅  |                                            |
| `=`, `<>`      |  ✅  |  ✅  |                                            |
| `<`, `<=`      |  ✅  |  ✅  |                                            |
| `>`, `>=`      |  ✅  |  ✅  |                                            |
| `AND`, `OR`    |  ✅  |  ✅  |                                            |
| `NOT`          |  ✅  |  ✅  |                                            |
| `&` concat     |  ✅  |  ✅  |                                            |
| `XOR`          |  ✅  |  ✅  | Extended BASIC only                        |
| Operator precedence |  ✅  |  ✅  | NOT > XOR > AND > OR; Test 22 verified |

## String functions

| Function       | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| ASC            |  ✅  |  ✅  |                                            |
| CHR$           |  ✅  |  ✅  |                                            |
| LEN            |  ✅  |  ✅  |                                            |
| POS            |  ✅  |  ✅  |                                            |
| SEG$           |  ✅  |  ✅  |                                            |
| STR$           |  ✅  |  ✅  |                                            |
| VAL            |  ✅  |  ✅  |                                            |
| RPT$           |  ✅  |  ✅  |                                            |

## Numeric functions

| Function       | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| ABS            |  ✅  |  ✅  |                                            |
| ATN            |  ✅  |  ✅  |                                            |
| COS            |  ✅  |  ✅  |                                            |
| SIN            |  ✅  |  ✅  |                                            |
| TAN            |  ✅  |  ✅  |                                            |
| EXP            |  ✅  |  ✅  |                                            |
| LOG            |  ✅  |  ✅  |                                            |
| INT            |  ✅  |  ✅  |                                            |
| SGN            |  ✅  |  ✅  |                                            |
| SQR            |  ✅  |  ✅  |                                            |
| RND            |  ✅  |  ✅  | Zero-arg form works without parens         |
| PI             |  ✅  |  ✅  | Zero-arg constant                          |
| MAX            |  ✅  |  ✅  | Extended BASIC                             |
| MIN            |  ✅  |  ✅  | Extended BASIC                             |

## CALL subprograms — graphics

| Subprogram     | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| CALL CLEAR     |  ✅  |  ✅  |                                            |
| CALL SCREEN    |  ✅  |  ✅  |                                            |
| CALL COLOR     |  ✅  |  ✅  |                                            |
| CALL CHAR      |  ✅  |  ✅  | 8-byte hex pattern                         |
| CALL HCHAR     |  ✅  |  ✅  |                                            |
| CALL VCHAR     |  ✅  |  ✅  |                                            |
| CALL GCHAR     |  ✅  |  ✅  |                                            |
| CALL CHARSET   |  ✅  |  ✅  | Resets chars 32-127 to ROM defaults        |
| CALL CHARPAT   |  ✅  |  ✅  | `CALL CHARPAT(code, A$)` — 16-char hex     |

## CALL subprograms — sprites (Extended BASIC)

| Subprogram     | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| CALL SPRITE    |      |      | Create sprite                              |
| CALL MOTION    |      |      | Set sprite motion                          |
| CALL POSITION  |      |      | Read sprite position                       |
| CALL LOCATE    |      |      | Relocate sprite                            |
| CALL COINC     |      |      | Sprite coincidence check                   |
| CALL DISTANCE  |      |      | Sprite-to-sprite distance                  |
| CALL DELSPRITE |      |      |                                            |
| CALL MAGNIFY   |      |      |                                            |
| CALL PATTERN   |      |      | Change sprite pattern                      |

## CALL subprograms — I/O & system

| Subprogram     | Impl | Test | Notes                                      |
|----------------|:----:|:----:|--------------------------------------------|
| CALL KEY       |  ✅  |  ✅  | Mode 0; other modes treated same           |
| CALL VERSION   |  ✅  |  ✅  | Returns 110                                |
| CALL JOYST     |      |      |                                            |
| CALL SOUND     |      |      |                                            |
| CALL SAY       |      |      | Speech                                     |
| CALL SPGET     |      |      | Speech                                     |
| CALL ERR       |  ✅  |  ✅  | Stub: returns 0,0,0,lastErrLine (no classification yet) |
| CALL INIT      |      |      | Memory Expansion init                      |
| CALL LINK      |      |      | Assembly linkage                           |
| CALL LOAD      |      |      | Memory poke                                |
| CALL PEEK      |      |      | Memory read                                |

---

## Editor / environment features

| Feature                            | Impl | Test | Notes                                   |
|------------------------------------|:----:|:----:|-----------------------------------------|
| Line editor — DEL / INS / ERASE    |  ✅  |  ✅  | FCTN+1/2/3                              |
| Line editor — arrows (L/R/U/D)     |  ✅  |  ✅  | FCTN+S/D/E/X                            |
| Line editor — BKSP (PC-style)      |  ✅  |  ✅  | 0x7F → cursor-left + delete, BLE & serial |
| REDO recall (FCTN+8)               |  ✅  |  ✅  | Recalls last submitted line             |
| Line-number recall (`<N>` + UP/DN) |  ✅  |  ✅  |                                         |
| UP/DOWN browse in EDIT mode        |  ✅  |  ✅  | Commits current line before navigating  |
| CLEAR breaks running program       |  ✅  |  ✅  | FCTN+4 or Ctrl+C or ESC                 |
| BLE keyboard input                 |  ✅  |  ✅  | F12 or BOOT button = pairing            |
| Serial paste                       |  ✅  |  ✅  | 16 KB decoupled buffer                  |
| Title / menu screen                |  ✅  |  ✅  | Color stripes, 3×3 logo, © char         |
| Error format (blank + msg + BEL)   |  ✅  |  ✅  | TI-style "* MSG IN nn"                  |
| TI-standard token table (0x00-0xFE)|  ✅  |  ✅  | Matches ninerpedia spec; two-token `<=`,`>=`,`<>` |
| `tokenNames[256]` O(1) detokenize  |  ✅  |  ✅  | ASCII bytes + keyword tokens unified    |
| 800×480 new-board port             |  ✅  |  ✅  | ESP32-8048S043C, 32×24 @ 16×16 chars    |
| ~~ OTG version preserved ~~        |  ✅  |  ✅  | Frozen in `ti-basic-otg/`               |
| V9T9 `.dsk` image support          |  ✅  |      | MOUNT / UNMOUNT / NEWDISK / COPY; FLASH or SDCARD; read + write DIS/VAR (Phase 1+2); PROGRAM deferred |
