# CPPIP - CP/M File Copy Utility
## User Manual - v1.10 (44)  [Document revision 1.10]

*A C reimplementation of PPIP v1.8 by D. Jewett III (1985-1988).*
*C port, NABU IA extension, and FreHD SD extension by Intangybles, 2026.*

---

## Contents

1. [Overview](#overview)
2. [Command Syntax](#command-syntax)
3. [Drive and User Area Prefix](#drive-and-user-area-prefix-du)
4. [Options](#options)
5. [Wildcards](#wildcards)
6. [Examples](#examples)
7. [CON: Editor](#con-editor)
8. [IA: Internet Adapter (NABU)](#ia-internet-adapter-nabu)
9. [SD: FreHD Card](#sd-frehd-card)
10. [File Attributes](#file-attributes)
11. [CRC Verify](#crc-verify)
12. [Cancelling](#cancelling)
13. [Messages](#messages)
14. [Notes](#notes)

---

## Overview

CPPIP is a file copy utility for CP/M 2.2, designed for single-file or
wildcard copies without loading a full directory browser.
CRC verification is built in. Two extensions add access to external file stores
from the CP/M command line: the NABU RetroNET Internet Adapter (`IA:`) and the
FreHD SD card emulator (`SD:`).

Key features:

- Copy single files or groups of files using wildcards
- Cross-drive and cross-user-area copies in one command
- CRC-16 verification after copy with automatic retry
- Move mode: copy then delete the source
- Type text directly from the console into a file
- **NABU IA extension:** copy files to and from the RetroNET Internet Adapter
  file store using the `IA:` prefix
- **FreHD SD extension:** copy files to and from an SD card via the FreHD
  hard disk emulator using the `SD:` prefix

Three builds are distributed:

| Binary | Description |
|--------|-------------|
| `CPPIP.COM` | Standard build. IA: available via `/N` or on CloudCP/M. SD: compiled in (requires FreHD hardware). |
| `NPPIP.COM` | NABU edition. IA: always active, no switch needed. No SD: code. |
| `FPPIP.COM` | FreHD edition. SD: always active. No IA: code. |

All three are functionally identical for standard CP/M file operations.

---

## Command Syntax

CPPIP accepts two command styles, detected automatically.

### MS-DOS style (default)

```
CPPIP [DU:]source[.ext] [[DU:]dest[.ext]] [/options]
```

Source comes first, then destination. If no destination is given, the file is
copied to the current drive and user area with the same name.

### CP/M style

```
CPPIP [[DU:]dest[.ext]=][DU:]source[.ext] [/options]
```

Destination comes first, separated from source by `=`. CPPIP detects this
style automatically when it finds `=` in the command line.

### Help

```
CPPIP /H
```

Prints a brief usage summary and option list.

---

## Drive and User Area Prefix (DU:)

Any filename can be prefixed with a drive letter, user area number, or both.
The format is `DU:` where `D` is the drive letter (A-P) and `U` is the user
area number (0-31). You can specify either or both:

```
B:      drive B, current user area
3:      user area 3, current drive
B3:     drive B, user area 3
```

Omitting either part uses the current value.

Examples:

```
CPPIP FILE.DAT B:       copy to drive B, same user area
CPPIP FILE.DAT 3:       copy to user area 3, same drive
CPPIP FILE.DAT B3:      copy to drive B, user area 3
```

---

## Options

Options are introduced by `/` and can appear anywhere in the command line.
They can be combined: `/VC` is the same as `/V /C`.

| Option | Name | Description |
|--------|------|-------------|
| `/V` | Verify | CRC-16 verify after copy. On CP/M copies, retries up to 3 times on mismatch. |
| `/C` | CRC | Print the CRC value in hex after a verified copy (requires `/V`). |
| `/E` | Existing | Overwrite existing read/write destination without asking. |
| `/W` | Wipe | Overwrite any destination (read/write or read-only) without asking. |
| `/M` | Move | Copy then delete source. Forces `/V` verify automatically. |
| `/N` | NABU IA | Enable IA: on a non-CloudCP/M NABU system. (CPPIP only.) |
| `/H` | Help | Show help and exit. |

Note: `/E` only silences the prompt for writable files. Read-only files still
ask unless `/W` is also used.

---

## Wildcards

Wildcards can appear in both the source and destination filenames.

`*` in the source matches any sequence of characters in that position.
`?` in the destination is replaced by the corresponding character from the
matched source filename.

Examples:

```
CPPIP *.COM B:              copy all .COM files to drive B:
CPPIP *.COM *.BAK           copy all .COM files, renaming extension to .BAK
CPPIP A*.* B3:              copy all files starting with A to B: user area 3
CPPIP *.COM B:???????1      copy all .COM files, inserting 1 at end of name
```

CPPIP handles up to 512 matched filenames per command. When wildcards would
produce duplicate destination names, CPPIP detects the duplicate and skips the
second file with a `Duplicate!` message.

---

## Examples

```
CPPIP FILE.DAT                    copy FILE.DAT to same name, current drive/user
CPPIP FILE.DAT B:                 copy to drive B:, same user area
CPPIP FILE.DAT B3:                copy to drive B: user area 3
CPPIP FILE.DAT B3:BACKUP.DAT      copy and rename
CPPIP BACKUP.DAT=FILE.DAT         CP/M style: dest=source
CPPIP *.COM B:                    copy all .COM files to B:
CPPIP C8:*.COM A0:*.OBJ           copy .COM files on C: user 8 to A: user 0 as .OBJ
CPPIP FILE.DAT B: /V              copy + CRC verify
CPPIP FILE.DAT B: /VC             copy + verify + print CRC
CPPIP FILE.DAT B: /M              move (copy then delete source)
CPPIP FILE.DAT B: /W              copy, overwriting read-only dest without asking
```

---

## CON: Editor

Using `CON:` as the source lets you type text directly into a destination file:

```
CPPIP CON: NOTES.TXT
```

Type your text and press `Ctrl-Z` on a new line to finish and save the file.

Editing features:

- `Backspace` or `Rubout` - delete the last character on the current line
- `Ctrl-Z` - end input and save the file
- `Enter` - produces a CR+LF line ending in the file

Wildcards are not allowed in the destination when using `CON:`.

---

## IA: Internet Adapter (NABU)

The `IA:` prefix accesses the NABU RetroNET Internet Adapter file store over
the HCCA port, giving you direct access to a network file server from the
CP/M command line.

CPPIP and NPPIP use identical `IA:` syntax. The only difference is how IA:
is activated:

| Binary | How to activate IA: |
|--------|---------------------|
| `NPPIP` | IA: is always active. No switch needed. Just use `IA:` anywhere. |
| `CPPIP` on CloudCP/M | IA: is active automatically. No switch needed. |
| `CPPIP` on any other NABU CP/M | Add `/N` to the command to enable IA:. |

Once active, every `IA:` command works the same regardless of which binary
you are running.

### IA path formats

```
IA:FILE.DAT             flat file at root of store
IA:SUBDIR/FILE.DAT      file in a subfolder (folder must already exist)
IA:/D/1/FILE.DAT        CloudCP/M drive D user area 1 (folder auto-created)
IA:D:/1/FILE.DAT        same as above - both formats are equivalent
```

The `/X/` and `X:/` formats are strongly recommended for all subfolder work.
They automatically create missing directory trees on write, and find files
regardless of whether they were stored with upper or lowercase names.

### Long filenames on IA:

The IA file store runs on a PC server with no filename length restriction.
Files with names longer than 8 characters (e.g. `NIALLCONV.COM`) are copied
to CP/M with the name truncated to 8 characters (`NIALLCON.COM`). The extension
is always preserved. A `[truncated]` note appears on the copy line so you can
see that the name changed. If the truncated name collides with an existing file,
the usual `Exists! Delete?` prompt appears.

### IA examples

Using NPPIP (IA: always active):

```
NPPIP FILE.DAT IA:/Z/BACKUP/FILE.DAT    copy to IA store
NPPIP IA:/D/1/FILE.DAT B:               copy from IA store to drive B:
NPPIP IA:/D/1/*.COM B:                  copy all .COM files from IA to B:
NPPIP *.DAT IA:/Z/BACKUP/               copy all .DAT files to IA folder
NPPIP IA:FILE.DAT B: /M                 move from IA to B: (deletes IA copy)
```

Using CPPIP on CloudCP/M (IA: active automatically):

```
CPPIP FILE.DAT IA:/Z/BACKUP/FILE.DAT    copy to IA store
CPPIP IA:/D/1/*.COM B:                  copy all .COM files from IA to B:
```

Using CPPIP on a non-Cloud NABU (add `/N` to enable IA:):

```
CPPIP FILE.DAT IA:/Z/BACKUP/FILE.DAT /N    copy to IA store
CPPIP IA:/D/1/*.COM B: /N                  copy all .COM files from IA to B:
```

See `IA-GUIDE.TXT` for the full IA path reference and troubleshooting guide.

---

## SD: FreHD Card

The `SD:` prefix accesses files directly on an SD card via the FreHD
hard disk emulator connected to the Z80 I/O bus.

`SD:` is available in CPPIP.COM and FPPIP.COM. It requires FreHD hardware to
be present. On systems without FreHD, `SD:` commands will fail cleanly.

See `FREHD-GUIDE.TXT` for full setup and usage details.

### Which binary to use

| Binary | Use when |
|--------|----------|
| `FPPIP.COM` | You have FreHD and want the smallest, dedicated SD: binary. |
| `CPPIP.COM` | You want both SD: and IA: support in one binary. |

### SD path formats

```
SD:FILE.COM             file at the root of the SD card
SD:SUBDIR/FILE.COM      file in a subdirectory (directory must already exist)
SD:SUBDIR/*.COM         wildcard - copy all .COM files from SUBDIR
```

Subdirectories on the SD card must already exist. FreHD does not create
directories automatically.

### Long filenames on SD:

The SD card uses FAT format and can hold files with names longer than 8
characters. CP/M can only store 8-character names. If a wildcard copy
encounters a file whose name is longer than 8 characters (e.g. `NIALLCONV.COM`),
it is skipped with a visible warning. You can still copy it individually by
specifying a short destination name:

```
FPPIP SD:NIALLCONV.COM A:NIALL.COM
```

### SD examples

```
FPPIP SD:FILE.COM A:                    copy from SD to current drive
FPPIP A:FILE.COM SD:                    copy from CP/M to SD root
FPPIP SD:*.DAT A1: /V                   copy all .DAT files from SD to A: user 1 with verify
FPPIP A:*.COM SD:BACKUP/ /V             copy all .COM files from A: to SD BACKUP folder
FPPIP SD:SUBDIR/*.COM A: /M             move all .COM from SD subfolder to A: (deletes SD copies)
FPPIP SD:PROG.COM A: /VC               copy with verify and print CRC value
```

---

## File Attributes

CPPIP copies the read-only attribute from the source file to the destination.

If the destination file already exists, CPPIP will ask before overwriting:

- **Read/write file:** `Exists! Delete?` - press Y to overwrite, N to skip.
- **Read-only file:** `R/O! Delete?` - press Y to overwrite, N to skip.
- **Press Ctrl-C** at either prompt to cancel and return to the CP/M command prompt.

`/E` skips the prompt for read/write files.
`/W` skips the prompt for all files including read-only.

---

## CRC Verify

When `/V` is active, CPPIP computes a CRC-16 (CCITT - the same algorithm used
by XMODEM and KERMIT) checksum of the source file as it is read, then re-reads
the destination and computes the same checksum.

**CP/M to CP/M copies:**

- **Match:** `OK` is shown.
- **Mismatch:** the destination is deleted and the copy is retried. Up to
  3 attempts are made before CPPIP gives up and reports a CRC failure.

**Copies involving IA: or SD: (either source or destination):**

- **Match:** `OK` is shown.
- **Mismatch:** `CRC failed!` is shown. The copy is not retried.
  For SD: copies, check the FreHD connection. For IA:, check the server.

`/C` prints the CRC value in hexadecimal after each verified copy. Values
can be cross-checked with XMODEM, KERMIT, and other standard CP/M transfer
tools that use the same CRC-16 algorithm.

Move mode (`/M`) forces `/V` automatically.

---

## Cancelling

**During a batch copy:** Press `Ctrl-C` between files to stop. The current file
is completed first; remaining files in the batch are skipped. `^C` is printed
and CPPIP returns to CP/M.

**At a prompt:** Press `Ctrl-C` at any `Delete?` prompt to cancel the entire
operation and return to the CP/M command prompt immediately.

---

## Messages

| Message | Meaning |
|---------|---------|
| `Exists! Delete?` | Destination exists. Press Y to overwrite, N to skip. |
| `R/O! Delete?` | Destination is read-only. Press Y to overwrite, N to skip. |
| `same` | Source and destination resolve to the same file - skipped. |
| `Duplicate!` | Wildcard expansion would write the same destination twice - skipped. |
| `No file(s) found` | No source files matched the pattern. |
| `- Verifying` | CRC verify pass in progress. |
| `OK` | Copy complete (and verified if `/V` was used). |
| `CRC: XXXX` | CRC value in hex (shown with `/C`). |
| `FAILED - retrying...` | CRC mismatch on CP/M copy - retrying. |
| `CRC failed!` | All retries exhausted (CP/M), or IA:/SD: verify failed. |
| `ERROR: Disk full. Copy deleted.` | Destination disk has no space. Partial file removed. |
| `^C` | Ctrl-C pressed - batch stopped, remaining files skipped. |
| `[truncated]` | IA: source filename was longer than 8 chars - name shortened to fit CP/M. |
| `ERROR: IA: directory not found` | IA subfolder does not exist. Use `/X/` format or check spelling. |
| `ERROR: IA file not found` | IA file does not exist at the given path. |
| `IA: unavailable - use /N` | CPPIP on non-CloudCP/M - add `/N` or use NPPIP. |
| `ERROR: SD: directory not found` | SD: target subdirectory does not exist. Create it on the SD card first. |
| `ERROR: SD: file not found` | SD: source file does not exist. |
| `SD name too long for CP/M, skipped` | SD: source file has a name > 8 chars - cannot be stored on CP/M. Copy manually with a short destination name. |

---

## Notes

- CPPIP targets CP/M 2.2. Tested on: NABU CloudCP/M, RomWBW CP/M 2.2,
  TRS-80 Model 4P with Montezuma Micro CP/M 2.2, and Kaypro CP/M 2.2.
- ZRDOS compatible: the post-write disk reset is skipped when running under ZRDOS.
- The original PPIP v1.8 by D. Jewett III is in the public domain.
  CPPIP is copyright (c) Intangybles 2026.

---

*CPPIP/NPPIP/FPPIP v1.10 (44) - Intangybles 2026*
