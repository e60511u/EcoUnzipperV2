# Dezipper — Improvement Points

## 1. 🚨 Data Loss Risk (Critical)

This is the most important issue in the current design.

When extraction starts and "Keep ZIP" is not selected, the tool immediately begins **punching holes** in the original archive. If the process is interrupted at any point — power failure, crash, full disk, or a simple Ctrl+C — the source ZIP is left in a **partially destroyed, unrecoverable state**.

The user loses both the source archive and the incomplete extraction.

**Suggested mitigations:**

- Add a `--safe` mode (or make it the default) that only punches holes after a successful CRC validation of all extracted files.
- Before modifying the ZIP, save the Central Directory offset and size to a small `.dezipper_state` sidecar file so recovery is at least theoretically possible.
- Display a prominent warning in the README and in the CLI/GUI before any destructive operation begins.
- Consider a dry-run phase: extract everything first, verify all CRCs, then punch holes only on full success.

---

## 2. 🔐 Password Passed as a CLI Argument

The `-x, --password` option accepts the password directly on the command line. This exposes it in several places:

- The shell's command history (`.bash_history`, PowerShell history, etc.)
- The OS process list, visible to other users via tools like Task Manager or `tasklist`
- Log files that capture full command lines

**Suggested fix:** Remove the ability to pass a password inline. Instead:

- Prompt interactively when a password is required (hidden input, no echo).
- Or accept the password from `stdin` via a pipe: `echo "mypassword" | dezipper -x - archive.zip`
- Document this clearly in the CLI help output.

---

## 3. 📦 Limited Compression Method Support

The tool currently only supports:

- Method 0 — Stored (no compression)
- Method 8 — Deflate

Many real-world ZIP archives use other compression methods that will silently fail or produce an error with no clear explanation:

| Method | Algorithm | Common usage |
|--------|-----------|--------------|
| 9 | Deflate64 | Older WinZip archives |
| 12 | BZIP2 | Some Linux-generated ZIPs |
| 14 | LZMA | 7-Zip and modern tools |
| 98 | PPMd | Rarely, WinZip |
| 99 | AES encryption (WinZip) | Very common today |

**Suggested improvements:**

- Add explicit, user-friendly error messages when an unsupported method is encountered (e.g., *"Unsupported compression method: LZMA (14). Only Deflate and Stored are currently supported."*).
- Add BZIP2 support via `libbz2` — it is simple to integrate and widely used.
- Consider LZMA support via `liblzma` (part of XZ Utils) for broader compatibility.
- Document supported and unsupported methods clearly in the README.

---

## 4. ⚠️ Overconfident Reliability Claims

The README states:

> "Uses standard Central Directory parsing for 100% reliability."

The ZIP format has many known edge cases that can silently break even well-written parsers:

- **Encoding ambiguity**: File names can be stored in CP437, local system codepage, or UTF-8 (flag `0x800`). Mishandling this causes corrupted paths on extraction.
- **Overlapping entries**: Crafted or corrupted ZIPs can have entries that reference the same byte ranges.
- **Self-extracting archives (SFX)**: Have a non-zero offset between the start of the file and the start of the ZIP data; finding the Central Directory requires scanning from the end.
- **Multi-disk archives**: The format supports spanning across multiple volumes; the current tool likely does not handle this.
- **Malformed trailing comments**: Variable-length End-of-Central-Directory comments can confuse parsers that scan naively.

**Suggested fix:** Replace "100% reliability" with an honest, specific statement such as: *"Reliable for standard single-disk ZIP archives using Deflate or Stored compression."*

---

## 5. 🖥️ Windows / NTFS Only — No Graceful Fallback

The space-saving feature requires NTFS sparse file support, which is a Windows-only API. This is stated in the build requirements, but the tool does not handle non-NTFS scenarios gracefully:

- Running on a FAT32 or exFAT drive (e.g., a USB stick) will silently fall back to standard behavior — or fail — without a clear message.
- The tool could detect the filesystem at startup and warn the user when sparse files are unavailable.

**Suggested improvement:** Add a runtime check for NTFS and display a clear notice: *"Sparse file support not available on this filesystem. Extraction will proceed normally; the original ZIP will be deleted upon completion."*

---

## 6. 🗂️ Filename Encoding Handling

ZIP archives created on different operating systems store filenames in different encodings:

- Windows ZIPs typically use CP437 or the local ANSI codepage.
- Modern tools set the UTF-8 flag (`bit 11` of the general purpose bit flag).
- Without proper handling, accented characters, CJK characters, and special symbols will result in corrupted filenames.

**Suggested improvement:** Check the UTF-8 flag on each entry and decode accordingly. Fall back to CP437 for entries without the flag. Consider exposing a `--encoding` CLI option for edge cases.

---

## 7. 📋 Pause/Resume State Persistence

The README mentions pause and resume support, and that "already-extracted files are automatically skipped." However, this relies on checking whether a file already exists on disk, which has limitations:

- A file may exist but be **incomplete** if extraction was interrupted mid-file.
- A file may have been **corrupted** on disk independently.
- The tool has no way to distinguish a correctly extracted file from a partial one without storing state externally.

**Suggested improvement:** Write a lightweight state file (e.g., `archive.zip.dezipper_progress`) that stores the list of successfully extracted and CRC-verified files. On resume, validate against this list rather than relying on filesystem presence alone. Delete the state file when extraction completes successfully.

---

## 8. 📄 License Clarity

The current license section states:

> "This project is provided as-is for educational and personal use."

This is not a recognized open-source license and is legally ambiguous in several ways:

- It does not explicitly grant or deny distribution rights.
- "Educational use" is not a defined legal category.
- Contributors and forks have no clear terms to operate under.

**Suggested improvement:** Choose and apply a standard license (MIT, Apache 2.0, or GPL-2.0) so that users, contributors, and downstream consumers have clear legal terms. Tools like [choosealicense.com](https://choosealicense.com) can help with the decision.

---

## Summary Table

| # | Issue | Severity |
|---|-------|----------|
| 1 | Partial extraction leaves ZIP unrecoverable | 🔴 Critical |
| 2 | Password exposed in process list and shell history | 🔴 Critical |
| 3 | Only 2 compression methods supported | 🟠 High |
| 4 | "100% reliability" claim is misleading | 🟡 Medium |
| 5 | No graceful fallback on non-NTFS filesystems | 🟡 Medium |
| 6 | Filename encoding not handled | 🟡 Medium |
| 7 | Resume relies on file existence, not verified state | 🟡 Medium |
| 8 | Ambiguous license terms | 🟢 Low |
