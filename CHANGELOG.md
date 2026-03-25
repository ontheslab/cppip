# Changelog

All notable changes to CPPIP / NPPIP / FPPIP are documented here.

---

## [1.10] - 2026 (Build 44)

### Added

- **FPPIP.COM** - new dedicated FreHD edition binary. SD: always active,
  no IA: code, smallest binary (~20KB).
- **SD: prefix** - direct access to files on an SD card via the FreHD hard
  disk emulator. Available in FPPIP.COM always, and CPPIP.COM when FreHD
  hardware is detected at startup.
- **Runtime hardware detection** - startup banner shows `[CloudCP/M]` or
  `[FreHD Detected]` when the respective hardware is found.
- **SD: directory pre-flight check** - before a wildcard CP/M->SD copy, the
  target directory is checked first. A single clear error is shown if it
  does not exist. (FreHD cannot create directories; prepare the card on a
  PC first.)
- **SD: long-filename warning** - wildcard SD: copies skip files with a name
  part longer than 8 characters, with a visible warning and a suggested
  copy command.
- **IA: truncation note** - wildcard IA: copies that shorten a filename to
  fit CP/M show `[truncated]` on the copy line. If the truncated name
  collides with an existing file and the user declines, a rename hint is
  shown.

### Fixed

- **SD: CRC verify on odd-record files (build 36)** - the last block of a
  file whose size is not a multiple of 256 bytes was being read and checked
  incorrectly. FreHD reports the actual byte count for the last block; only
  those bytes are now checked. Fixes verify failures on all odd-sized files.
- **SD->CP/M batch: false "Exists!" prompts (builds 37-39)** - stale state
  between files in a wildcard batch caused some files to appear to already
  exist on the CP/M destination when they did not.
- **Montezuma Micro CP/M 2.2 compatibility (builds 38-39)** - Montezuma
  Micro's BDOS returns a user-0 directory entry as a fallback when a file is
  not found in the current user area. A loop added to handle this caused an
  infinite lockup. Fix: single directory search; if the returned entry is
  from the wrong user area, treat the file as not found.
- **SD: destination deleted on CRC failure (build 37)** - the partial
  destination file is now deleted on a failed SD->CP/M verify. Previously it
  was left on disk.
- **SD wildcard false match (builds 42-43)** - files with a name part longer
  than 8 characters (e.g. NIALLCONV.COM) were matched incorrectly: the
  extension was silently dropped, producing an unreadable file on CP/M. A
  post-conversion check now catches this and issues a skip warning.

### Build notes

NPPIP incorrectly included FreHD code during builds 39-41, adding ~6KB of
unused code. Corrected at build 42; NPPIP is IA-only as intended.

---

## [1.00] - 2026 (Builds 26-32)

### Added

- **Dual-binary build (build 28)** - CPPIP.COM (standard, /N for IA) and
  NPPIP.COM (NABU edition, IA always active, CloudCP/M tag in banner).
- **Version format (build 28)** - changed to `major.minor (build)` e.g.
  `1.00 (28)`.
- **Dynamic TPA buffer (build 31)** - I/O buffer moved from fixed memory to
  free TPA (Transient Program Area) claimed at startup. Binary shrinks from
  ~49.8KB to ~28KB. Buffer grows from 128 to ~175 records at runtime.

### Fixed

- **Stack overflow on startup (build 32)** - the TPA allocator left no room
  for the stack, corrupting memory on the first function call. Fix: reserve
  1024 bytes for the stack before sizing the I/O buffer.
- **Spurious key-press after IA: activity (build 31)** - console status
  check returned a false positive after HCCA communication on CloudCP/M.
  Replaced with a direct input check that is reliable on all CP/M 2.2
  systems.
- **Keyboard type-ahead at prompts (build 31)** - keyboard buffer now
  drained before each "Exists!/R/O! Delete?" prompt.
- **Ctrl-C at Delete? prompt (build 30)** - Ctrl-C now triggers a CP/M
  warm boot instead of being treated as N.
- **Batch IA: copy stopped on first skip (build 29)** - declining an
  overwrite prompt stopped the whole batch. Now skips that file and
  continues with the rest.
- **IA: wildcard batch stopped after first file (build 27)** - the RetroNET
  server has one global file-list state. A directory check during each copy
  was resetting it. Fix: restore the file list at the start of each loop
  iteration.

---

## [1.0.20 to 1.0.26] - 2026

### Fixed

- **NIA server crash on missing subdirectory** - opening a file on a path
  whose directory does not exist crashed the NIA server and hung the NABU
  indefinitely. Fix: each directory component is verified before any file
  open is attempted. Drive-letter paths (X:\DIR\FILE) are exempt as the
  server creates those automatically.
- **Case-sensitive IA: file lookup** - files stored with lowercase names
  were not found when read back with uppercase names. Fix: use a
  case-insensitive file open instead of the case-sensitive size query.
- **Plain path with bare leading /** - a bare leading / was passed to the
  server and mapped to the Windows drive root. It is now stripped first.

---

## [1.0.12 to 1.0.19] - 2026

### Added

- **IA: subdirectory support (v1.0.19)** - source and destination can now
  include any number of directory levels: IA:FRED/FILE.DAT, IA:A/B/C/*.DAT.
- **/X/ and X:/ path prefix (v1.0.19)** - both formats auto-create
  directories on write and avoid the server crash bug.
- **/V verify for IA copies (v1.0.12 / v1.0.13)** - CRC verify now works
  for both IA->CP/M and CP/M->IA copies.
- **NABU hardware detection (v1.0.12)** - IA: blocked on non-NABU systems
  to prevent lockup. /N option allows override on non-CloudCP/M NABU.

### Fixed

- **IA: options set-only (v1.0.18)** - typing /V/V no longer cancels verify.
- **IA: wildcard destination (v1.0.18)** - IA:*.SAV as destination now
  resolves correctly instead of being used as a literal filename.
- **IA: filename case (v1.0.15)** - filenames forced to uppercase to prevent
  case mismatch on the store.
- **CP/M->IA bare IA: destination (v1.0.14)** - bare "IA:" destination now
  derives each filename from the CP/M source. Previously sent a zero-length
  filename causing a comms error.
- **IA->CP/M bare drive destination (v1.0.12)** - IA:FILE.COM D: now copies
  to D:FILE.COM instead of D:.

---

## [1.0.09 to 1.0.11] - 2026

### Added

- **Phase 5: NABU IA RetroNET extension (v1.0.09)** - IA: prefix added.
  Copy in both directions between CP/M and the IA file store. All IA code
  compiled out on non-NABU builds.
- **Phase 4: CON: as source (v1.0.09)** - type text from the console
  directly into a file. Ctrl-Z to finish.

*Note: v1.0.10 / v1.0.11 were live development builds, rolled back and
reworked into v1.0.12.*

---

## [1.0.01 to 1.0.05] - 2026

### Added

- **Phase 3: CRC-16 verify (v1.0.05)** - /V re-reads the destination after
  copy and compares checksums. Up to 3 retries on mismatch; bad copy deleted
  between retries. /E, /W, /M all implemented.
- **Phase 2: wildcard support (v1.0.01)** - file listing and pattern
  matching, up to 512 matches. Duplicate destination detection. Binary
  renamed CPPIP.COM.
- **Phase 1: core single-file copy** - DU: prefix parsing, FCB (File Control
  Block) building, cross-drive and cross-user-area copies. Port of PPIP v1.8
  (D. Jewett III, 1985-1988).

---

*CPPIP/NPPIP/FPPIP - Intangybles 2026*
