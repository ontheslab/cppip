# CPPIP / NPPIP - CP/M File Copy Utility

A C reimplementation of **PPIP v1.8** (D. Jewett III, 1985-1988), a classic CP/M file copy utility. Built with [z88dk](https://z88dk.org) targeting CP/M 2.2 and NABU CloudCP/M.

Two binaries are produced from the same source:

| Binary | Description |
|--------|-------------|
| `CPPIP.COM` | Standard build. IA: file store available via `/N` or on CloudCP/M. |
| `NPPIP.COM` | NABU edition. IA: always active - no switch required. |

The original Z80 assembly source (all nine modules) is preserved in the `PPIP Master/` folder.

---

> **Development Status - Beta / Tester Release**
>
> CPPIP/NPPIP is in active development and should be considered beta quality.
> Standard CP/M file operations are stable. The NABU IA: RetroNET extension
> has been tested on real hardware and is working well, but edge cases may
> remain. **Do not rely on this tool as your only means of copying important
> files.** Always verify critical copies and keep backups.

---

## What it does

CPPIP copies files on CP/M systems - between drives, between user areas, or any combination. It handles wildcards, CRC verification, move operations, and read-only file protection. On NABU systems, the `IA:` prefix adds direct access to the RetroNET Internet Adapter file store - copy files between CP/M and the IA server from the command line.

---

## Usage

```
CPPIP [DU:]source[.ext] [[DU:][dest[.ext]]] [/options]
```

or CP/M style (auto-detected when `=` is present):

```
CPPIP [[DU:]dest[.ext]=][DU:]source[.ext] [/options]
```

*(Replace `CPPIP` with `NPPIP` when using the NABU edition.)*

**DU: prefix** - Drive letter A-P, user area 0-31. Examples: `B3:FILE.TXT`, `A0:*.COM`, `B:`, `15:`

**IA: prefix** (NABU only) - RetroNET Internet Adapter file store. See `manual/ia-guide.txt` for the full path reference.

**CON: as source** - Type text directly to a file from the console. `^Z` ends input.

### Options

All options default to OFF:

| Switch | Description |
|--------|-------------|
| `/V`   | CRC-16 verify after copy - re-reads destination and compares |
| `/C`   | Print CRC value after each copy |
| `/E`   | Delete existing read/write destination without asking |
| `/W`   | Delete existing read/write and read-only destination without asking |
| `/M`   | Move: copy then delete source (automatically enables `/V`) |
| `/N`   | Enable IA: on non-CloudCP/M NABU systems (CPPIP only) |
| `/H`   | Show help |

---

## Building

Requires [z88dk](https://z88dk.org).

```batch
build.bat
```

Produces both `CPPIP.COM` and `NPPIP.COM` in one pass.

```batch
build.bat debug
```

Debug build of `CPPIP.COM`.

---

## Development Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Core single-file copy - BDOS I/O, DU: prefix, FCB building | ✅ Complete |
| 2 | Wildcard expansion - `*.COM`, `?` matching, duplicate detection | ✅ Complete |
| 3 | Options - CRC verify `/V`, report `/C`, move `/M`, overwrite `/E` `/W` | ✅ Complete |
| 4 | Console copy mode - `CON:` as source, mini line editor | ✅ Complete |
| 5 | NABU IA extension - `IA:` prefix, subdirectory support, wildcard on IA side | ✅ Complete |
| 6 | Dual binary - `CPPIP.COM` / `NPPIP.COM`, version format 1.00 (NN) | ✅ Complete |
| 7 | Size and memory optimisation - reduce binary footprint, dynamic I/O buffer | 🔲 In progress |

---

## Background

PPIP was originally written in Z80 assembly by D. Jewett III between 1985 and 1988. It was a staple utility on CP/M systems of the era, offering cross-drive and cross-user-area file operations that the standard `PIP` utility handled less conveniently.

This C reimplementation preserves the original functionality and adds the NABU-specific IA: extension for the [NABU Personal Computer](https://nabu.ca) running with a RetroNET Internet Adapter.

---

## License

Original assembly source © D. Jewett III, 1985-1988.
C reimplementation © Intangybles 2026.
