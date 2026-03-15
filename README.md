# CPPIP — CP/M File Copy Utility

A C reimplementation of **PPIP v1.8** (D. Jewett III, 1985–1988), a classic CP/M file copy utility. Built with [z88dk](https://z88dk.org) targeting CP/M 2.2 and NABU CloudCP/M.

The original Z80 assembly source (all nine modules) is preserved in the `PPIP Master/` folder.

---

> **Development Status — Experimental**
>
> CPPIP is currently in active development and should be considered experimental.
> While standard CP/M copy operations are working well, the NABU IA: RetroNET
> extension in particular is still being tested and refined. **Do not rely on
> this tool as your only means of copying important files.** Always verify
> critical copies independently and keep backups.

---

## What it does

CPPIP copies files on CP/M systems — between drives, between user areas, or any combination. It handles wildcards, CRC verification, move operations, read-only file protection, and on NABU systems, copying to and from the RetroNET Internet Adapter file store.

---

## Usage

```
CPPIP [DU:]source[.ext] [[DU:][dest[.ext]]] [/options]
```

or CP/M style (auto-detected when `=` is present):

```
CPPIP [[DU:]dest[.ext]=][DU:]source[.ext] [/options]
```

**DU: prefix** — Drive letter A–P, user area 0–31. Examples: `B3:FILE.TXT`, `A0:*.COM`, `B:`, `15:`

**IA: prefix** (NABU only) — NABU RetroNET Internet Adapter file store. Example: `IA:FILE.TXT`

**CON: as source** — Type text directly to a file from the console. `^Z` ends input.

### Options

All options default to OFF and toggle with `/`:

| Switch | Description |
|--------|-------------|
| `/V`   | CRC-16 verify after copy — re-reads destination and compares |
| `/C`   | Print CRC value after each copy |
| `/E`   | Delete existing read/write destination without asking |
| `/W`   | Delete existing read/write and read-only destination without asking |
| `/M`   | Move: copy then delete source (automatically enables /V) |
| `/N`   | Enable IA: on non-CloudCP/M NABU systems |
| `/H`   | Show this help |

---

## Building

Requires [z88dk](https://z88dk.org).

**Standard CP/M build:**
```batch
build.bat
```

**NABU CloudCP/M build (includes IA: support):**
```batch
build.bat nabu
```

Output: `CPPIP.COM`

---

## Implementation Phases

The project was developed in five phases:

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Core single-file copy — BDOS I/O, DU: prefix, FCB building | Complete |
| 2 | Wildcard expansion — `*.COM`, `?` matching, duplicate detection | Complete |
| 3 | Options — CRC verify `/V`, CRC report `/C`, move `/M`, overwrite `/E` `/W` | Complete |
| 4 | Console copy mode — `CON:` as source, mini line editor with `^Z` | Complete |
| 5 | NABU IA extension — `IA:` prefix for RetroNET Internet Adapter file store | Complete |

---

## Background

PPIP was originally written in Z80 assembly by D. Jewett III between 1985 and 1988. It was a staple utility on CP/M systems of the era, offering cross-drive and cross-user-area file operations that the standard CP/M `PIP` utility handled less conveniently.

This C reimplementation preserves all the original functionality and adds the NABU-specific IA: extension for the [NABU Personal Computer](https://nabu.ca) running with any Internet Adapter (I Hope).

---

## License

Original assembly source © D. Jewett III, 1985–1988.
C reimplementation © Intangybles.
