# CPPIP / NPPIP / FPPIP - IA: Internet Adapter Quick Guide
## Quick Guide - v1.10 (47)  [Document revision 1.10]

CPPIP and NPPIP are CP/M file copy utilities for the NABU computer. This is a
quick guide to using the `IA:` prefix, which gives CPPIP direct access to the
NABU RetroNET Internet Adapter file store - letting you copy files between
your CP/M drives and the IA server without leaving the command line.

- **NPPIP** - IA: is always active. No switch needed.
- **CPPIP** - IA: requires CloudCP/M, or add `/N` on a non-Cloud NABU with an IA.
- **FPPIP** - FreHD edition only. No IA: code. Use CPPIP or NPPIP for IA: access.

---

## Path formats

Four formats are accepted for the IA side of a copy:

| Format | Example | Notes |
|--------|---------|-------|
| Flat filename | `IA:FILE.DAT` | Root of store |
| Plain subpath | `IA:NIALL/FILE.DAT` | Folder must already exist |
| Unix-style | `IA:/D/1/FILE.DAT` | Auto-creates folders; preferred for NABU keyboard |
| Drive-letter | `IA:D:/1/FILE.DAT` | Same as Unix-style - both formats equivalent |

**Use `/X/` or `X:/` for any subfolder work.** Both formats trigger
automatic directory creation on write and case-insensitive file matching
on read. The `/X/` form is easiest to type on the NABU keyboard.

The CloudCP/M file store mirrors the drive/user layout:
`/D/0/` = drive D user 0, `/D/1/` = drive D user 1, and so on.

---

## Examples

### Copy CP/M file TO the IA store

```
CPPIP RETRO.DAT IA:RETRO.DAT            copy to root of store
CPPIP RETRO.DAT IA:NIALL/RETRO.DAT      copy to existing NIALL folder
CPPIP RETRO.DAT IA:/Z/MYDIR/RET.DAT     auto-create Z/MYDIR/ if needed
CPPIP RETRO.DAT IA:/D/1/RETRO.DAT       copy into CloudCP/M D: user 1
CPPIP *.DAT IA:/Z/BACKUP/               copy all .DAT files into Z/BACKUP/
```

### Copy file FROM the IA store to CP/M

```
CPPIP IA:RETRO.DAT RETRO.DAT            from root, current drive/user
CPPIP IA:/D/1/ARCOPY.ZIP B:             from CloudCP/M D: user 1 to B:
CPPIP IA:/D/1/*.COM B:                  copy all .COM files from D: user 1
CPPIP IA:/Z/MYDIR/FILE.DAT D6:          from Z store subfolder to D: user 6
```

### With options

```
CPPIP RETRO.DAT IA:/Z/BK/RET.DAT /V    copy + CRC verify
CPPIP RETRO.DAT IA:/Z/BK/RET.DAT /VC   copy + verify + print CRC value
CPPIP IA:FILE.DAT B: /M                 move from IA to B: (deletes IA copy)
CPPIP IA:FILE.DAT B: /E                 overwrite CP/M destination without asking
```

---

## Path rules

- Folder names and filenames are always uppercased internally.
- `/X/PATH` and `X:/PATH` are equivalent - both reach the same store location.
- Plain subpaths (`NIALL/FILE.DAT`) require the folder to already exist. A
  missing folder gives a clean `ERROR: IA: directory not found` message.
- Drive-letter and `/X/` paths auto-create missing folders on write.
- A typo in a plain subfolder gives a clean error, not a lockup.

## Long filenames in wildcard copies

CP/M can only store filenames with a name part up to 8 characters. If a
wildcard IA: copy matches a file whose name is longer than 8 characters, the
name is automatically truncated to 8 characters (the extension is preserved).
A `[truncated]` note is appended to the copy line:

```
IA:LONGFILENAME.COM to A0:LONGFILE.COM [truncated]
```

If the truncated name collides with an existing file and you decline to
overwrite it, a rename hint is shown:

```
   To copy: NPPIP IA:LONGFILENAME.COM A:NEWNAME.COM
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `ERROR: IA: directory not found` | Folder missing or misspelled | Use `/X/` format to auto-create, or check spelling |
| `ERROR: IA file not found` | File does not exist at that path | Check path and filename |
| `IA: unavailable - use /N` | CPPIP not on CloudCP/M | Add `/N`, or use NPPIP |
| NABU hangs, no message | Old build before server-crash fixes | Update to v1.10 (47) or later |

---

## Acknowledgements

A big thank you to GTAMP (https://gtamp.com/nabu/) for his ideas, guidance,
and testing - his input and encouragement throughout the development of IA:
were invaluable.

