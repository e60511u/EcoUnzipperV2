# Dezipper - ZIP Extractor & Disk Space Saver

**Version**: 2.3.0

Dezipper is a high-performance Windows utility that extracts ZIP files while actively freeing disk space. Unlike traditional extractors, it uses advanced NTFS features to reclaim space from the original ZIP file during the extraction process.

## Features

### Core Functionality
- **Robust ZIP Extraction**: Reliable for standard single-disk ZIP archives using Deflate or Stored compression.
- **ZIP64 Support**: Handles "huge" archives and files exceeding 4GB.
- **Real-time Space Recovery**: Utilizes **NTFS Sparse Files** to zero out and reclaim disk blocks *while* extracting.
- **Pause & Resume**: Supports pausing extraction and restarting later; uses a `.dezipper_progress` manifest file to automatically skip already-extracted files.
- **Auto-Cleanup**: Automatically deletes the ZIP archive once extraction and space reclamation are complete (unless `-k` is used).
- **Secure Password Handling**: Password is provided via interactive prompt instead of CLI arguments, protecting sensitive data.
- **Multi-file & Directory Support**: Recreates complex folder structures accurately.
- **CRC-32 Validation**: Verifies file integrity during extraction.

### Compression Support
- Method 0 (Stored/No compression)
- Method 8 (Deflate)

### CLI Options
| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-v, --verbose` | Show detailed progress |
| `-q, --quiet` | Suppress output |
| `-P, --progress` | Show text-based progress bar |
| `-d, --dir <path>` | Extract to specified directory |
| `-k, --keep` | Keep original ZIP (don't truncate/delete) |
| `-f, --force` | Overwrite existing files |
| `-l, --list` | List contents only |
| `-t, --test` | Test archive integrity (CRC) |
| `-p, --preserve` | Preserve file timestamps |
| `-x, --password` | Password for encrypted ZIPs |

### GUI Features
- **Optimized Layout**: Wide-screen interface (550x480) for long file paths.
- **Progress Tracking**: Real-time progress bar shows extraction percentage relative to total uncompressed size.
- **Pause Capability**: Stop extraction mid-way and resume later.
- **Modern Dark Theme**: Custom dark blue background with high-contrast text.
- **Native Integration**: Windows file browser and threaded processing (UI stays responsive).
- **Functional Options**: Fully working checkboxes for Keep ZIP, Preserve timestamps, and Force overwrite.

## How It Works (Space Saving)

1. **Sparse Initialization**: If "Keep ZIP" is not selected, the tool marks the original ZIP as an NTFS Sparse File.
2. **Central Directory Parsing**: Maps all file entries reliably.
3. **Extraction & Punching**:
   - Extracts the compressed data to the destination.
   - Immediately "punches" a hole in the original ZIP by zeroing the used data blocks.
   - The OS reclaims physical disk space instantly as the extraction progresses.
4. **Final Truncation & Deletion**: Once complete, the file is truncated and then deleted from the disk, freeing 100% of the archive's footprint.

## Build Requirements

- Windows OS (NTFS filesystem required for real-time space recovery)
- GCC (MinGW)
- zlib development library

### Compilation
```bash
gcc -Wall -Wextra -o bin/dezipper.exe common.c extract.c gui.c main.c -lz -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -mwindows
```

## History

- **v2.3.0**:
  - Added CLI text-based progress bar (`-P`).
  - Added GUI visual progress bar.
  - Implemented Pause/Resume functionality with manifest tracking.
  - Added automatic ZIP file deletion after extraction.
  - Added secure interactive password prompting.
  - Added `LICENSE` (MIT).
  - Improved reliability claims and filesystem awareness.
- **v2.1.0**: 
  - Switched to Central Directory parsing for robust extraction.
  - Fixed "missing files" bug in linear scanning.
  - Unified CLI and GUI extraction engines.
- **v2.0.0**: 
  - Major refactoring into modular architecture.
  - Fixed GUI checkbox behavior and password retrieval.
  - Initial modular dark theme implementation.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for more details.