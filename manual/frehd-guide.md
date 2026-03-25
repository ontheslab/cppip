# FPPIP / CPPIP - FreHD SD Card Guide
## v1.10 (47)  [Document revision 1.01]

*Detailed guide to SD: file operations via the FreHD hard disk emulator.*

---

## Contents

1. [What is FreHD?](#what-is-frehd)
2. [Which Binary to Use](#which-binary-to-use)
3. [Hardware Requirements](#hardware-requirements)
4. [Quick Start](#quick-start)
5. [SD: Path Formats](#sd-path-formats)
6. [Wildcards with SD:](#wildcards-with-sd)
7. [Long Filenames](#long-filenames)
8. [Subdirectories](#subdirectories)
9. [CRC Verify with SD:](#crc-verify-with-sd)
10. [Move Mode with SD:](#move-mode-with-sd)
11. [All Examples](#all-examples)
12. [Messages](#messages)
13. [Troubleshooting](#troubleshooting)

---

## What is FreHD?

FreHD (Free Hard Disk) is an open-source hard disk emulator for TRS-80 computers,
designed by Frederic Vecoven. It connects to the Z80 I/O bus and presents an SD card
as a block storage device. CPPIP uses FreHD's file-level protocol to read and write
individual files on the SD card directly from CP/M.

For FreHD hardware details and firmware:
- GitHub: `https://github.com/fvecoven/FreHDv1`
- Web: `https://www.vecoven.com/trs80/trs80.html`

---

## Which Binary to Use

Three binaries are provided. For FreHD use:

| Binary | SD: support | IA: support | Size | Use when |
|--------|------------|------------|------|----------|
| `FPPIP.COM` | Always active | None | ~20KB | Dedicated FreHD system - smallest binary |
| `CPPIP.COM` | Active if FreHD detected | Via `/N` on non-CloudCP/M | ~34KB | Mixed system needing both SD: and IA: |
| `NPPIP.COM` | None | Always active | ~28KB | NABU CloudCP/M only - no FreHD code |

On startup, CPPIP and FPPIP probe for FreHD hardware. If detected, the banner
shows `[FreHD Detected]`. If not detected, SD: commands will fail cleanly.

---

## Hardware Requirements

- A FreHD unit connected to the TRS-80
- An SD card formatted as FAT32 and inserted in the FreHD
- FreHD firmware that supports the READFILE/WRITEFILE/READDIR protocol
  (all current firmware versions do)
- FPPIP.COM or CPPIP.COM on your CP/M disk - the existing `import2.com`
  can be used to load the new programs from the SD card.

No CP/M configuration is needed. FPPIP communicates with FreHD directly via
Z80 I/O port instructions.

---

## Quick Start

```
A0>FPPIP
FPPIP v1.10 FreHD Edition [FreHD Detected]
```

If you see `[FreHD Detected]` in the banner, you are ready to go.

Copy a file from the SD card to your current CP/M drive and user area:

```
FPPIP SD:FILE.COM A:
```

Copy a file from CP/M to the SD card root:

```
FPPIP FILE.COM SD:
```

Copy with CRC verify:

```
FPPIP SD:FILE.COM A: /V
```

---

## SD: Path Formats

```
SD:FILE.COM             file at the root of the SD card
SD:SUBDIR/FILE.COM      file in a subdirectory
SD:SUBDIR/*.COM         wildcard - all .COM files in SUBDIR
SD:*.DAT                wildcard - all .DAT files at the root
```

- File and directory names are case-insensitive on the SD card.
  `SD:File.com`, `SD:FILE.COM`, and `SD:file.com` all refer to the same file.
- Names are always displayed in uppercase.
- The SD card root is `/` internally. You do not need to type a leading `/`.

---

## Wildcards with SD:

Wildcards work the same way as for CP/M files:

```
FPPIP SD:*.COM A:           copy all .COM files from SD root to A:
FPPIP SD:UTIL/*.COM A:      copy all .COM files from SD UTIL folder to A:
FPPIP A:*.DAT SD:BACKUP/    copy all .DAT files from A: to SD BACKUP folder
```

Standard wildcard characters:

- `*` matches any sequence of characters in the name or extension field
- `?` in the destination is replaced by the corresponding character from
  the source filename

During wildcard expansion, FPPIP reads the SD card directory and filters
entries against the pattern. Directories, volume labels, and hidden entries
are always skipped automatically.

---

## Long Filenames

The SD card uses FAT format and can store filenames longer than 8 characters
(e.g. `NIALLCONV.COM`). CP/M can only store names up to 8 characters.

**Single-file copy:** Specifying a long SD filename explicitly works fine.
You must give a short destination name:

```
FPPIP SD:NIALLCONV.COM A:NIALL.COM      works -- explicit short dest name
FPPIP SD:NIALLCONV.COM A:              will fail -- dest name is also long
```

**Wildcard copy:** If a wildcard pattern matches a file whose name part is
longer than 8 characters, that file is skipped. A warning is printed showing
the filename and a suggested manual copy command:

```
 NIALLCONV.COM: SD name too long for CP/M, skipped.
   To copy: FPPIP SD:NIALLCONV.COM A:NEWNAME.COM
```

The batch continues with the remaining files. Files whose names fit in 8
characters are not affected.

---

## Subdirectories

SD: subdirectories must already exist on the SD card before you copy to them.
FPPIP cannot create directories (FreHD does not provide a make-directory command).

**Correct:**

```
FPPIP A:*.COM SD:BACKUP/        SD BACKUP folder must already exist
```

**What happens if the folder is missing:**

```
A0>FPPIP A:*.COM SD:MISSING/ /V
FPPIP v1.10 FreHD Edition [FreHD Detected]
A0:FILE.COM to SD:MISSING/FILE.COM
ERROR: SD: cannot create: MISSING/FILE.COM - check directory exists
```

The first file fails with a clear message and the batch stops immediately.
No further files are attempted.

**Creating directories:** Use your SD card reader on a PC to create any required
folder structure before bringing the card back to the FreHD system.

---

## CRC Verify with SD:

Add `/V` to any SD: copy command to enable CRC-16 verify. After the copy is
complete, FPPIP re-reads the destination and compares the checksum against the
source.

```
FPPIP SD:FILE.COM A: /V            SD->CP/M with verify
FPPIP A:FILE.COM SD: /V            CP/M->SD with verify
FPPIP SD:*.COM A: /V               wildcard SD->CP/M with verify
```

On a mismatch, the partial destination file is deleted and `CRC failed!` is
shown. Unlike CP/M-to-CP/M copies, SD: copies are not automatically retried
(SD card errors are rare; if one occurs, check the card and retry manually).

Add `/C` as well to print the CRC value in hexadecimal:

```
FPPIP SD:FILE.COM A: /VC
```

---

## Move Mode with SD:

`/M` copies the file then deletes the source, effectively moving it. Verify
(`/V`) is forced on automatically when `/M` is used.

```
FPPIP SD:FILE.COM A: /M            move from SD to A: (deletes SD copy after verify)
FPPIP A:FILE.COM SD: /M            move from A: to SD (deletes CP/M copy after verify)
```

If verify fails, the source is not deleted.

---

## All Examples

Single-file copies:

```
FPPIP SD:PROG.COM A:               copy PROG.COM from SD root to current drive
FPPIP SD:UTIL/PROG.COM A:          copy from SD UTIL subfolder
FPPIP A:FILE.DAT SD:               copy from CP/M to SD root
FPPIP A:FILE.DAT SD:BACKUP/        copy from CP/M to SD BACKUP folder
FPPIP A:FILE.DAT SD:BACKUP/BK.DAT  copy and rename on SD
FPPIP SD:PROG.COM A: /V            copy with CRC verify
FPPIP SD:PROG.COM A: /VC           copy with verify and print CRC
FPPIP SD:PROG.COM A: /M            move (copy then delete SD copy)
```

Wildcard copies:

```
FPPIP SD:*.COM A:                  copy all .COM files from SD root to A:
FPPIP SD:UTIL/*.COM A:             copy all .COM files from SD UTIL folder
FPPIP A:*.DAT SD:BACKUP/           copy all .DAT files from A: to SD BACKUP
FPPIP SD:*.* A: /V                 copy everything from SD root with verify
FPPIP A1:*.COM SD:WORK/ /V         copy all .COM from A: user 1 to SD WORK
FPPIP SD:*.COM A: /M               move all .COM from SD root to A:
```

---

## Messages

| Message | Meaning |
|---------|---------|
| `[FreHD Detected]` | FreHD hardware found on the I/O bus at startup. |
| `SD:filename to A:filename` | Copy in progress (shown at start of each file). |
| `- Verifying` | CRC verify pass running after copy. |
| `OK` | Copy (and verify, if used) completed successfully. |
| `CRC: XXXX` | CRC value in hex (with `/C`). |
| `CRC failed!` | Source and destination checksums do not match. |
| `[truncated]` | Not applicable to SD: - long-name SD files are skipped, not truncated. |
| `SD name too long for CP/M, skipped` | SD filename has a name part > 8 chars. Cannot be stored on CP/M. |
| `ERROR: SD: file not found` | Source file does not exist on the SD card. |
| `ERROR: SD: cannot create: path - check directory exists` | The destination subdirectory does not exist on the SD card. Create it on a PC using a card reader before copying. |
| `ERROR: SD: cannot create: path` | Could not create destination file. SD card may be full or write-protected. |
| `Exists! Delete?` | Destination CP/M file already exists. Press Y to overwrite, N to skip. |
| `R/O! Delete?` | Destination CP/M file is read-only. Press Y to overwrite, N to skip. |

---

## Troubleshooting

**No `[FreHD Detected]` in banner**

- Check that the FreHD is powered and the SD card is inserted.
- Check the I/O bus connection between FreHD and the system.
- The FreHD detection sends a GETVER command and checks SIZE2 == 6. Any
  other response (or no response) is treated as no FreHD present.

**`ERROR: SD: file not found` for a file you can see in the directory**

- Check the spelling. SD: filenames are shown in uppercase but comparisons
  are case-insensitive.
- The file may be in a subdirectory. Use `SD:SUBDIR/FILE.COM` not `SD:FILE.COM`.

**`ERROR: SD: cannot create: ... - check directory exists`**

- The destination subdirectory does not exist on the SD card. Create it using
  a PC card reader before copying. The error fires on the first file create
  attempt and the batch stops immediately - no further files are tried.

**CRC failed! on an SD: copy**

- Remove and re-insert the SD card, then retry.
- Check for a write-protect switch on the SD card.
- Try a different SD card if the problem persists.

**Wildcard copies skip some files silently**

- Check that skipped files do not have names longer than 8 characters.
  FPPIP prints a warning for each skipped long-name file.
- Directories, volume labels, and hidden files on the SD card are always
  skipped - this is normal behaviour.

---

*FPPIP/CPPIP v1.10 (47) - Intangybles 2026*
