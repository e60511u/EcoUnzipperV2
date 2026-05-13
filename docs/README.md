# Dezipper - ZIP Extractor & Disk Space Saver

**Version**: 2.2.0

Dezipper is a high-performance Windows utility that extracts ZIP files while actively freeing disk space. Unlike traditional extractors, it uses advanced NTFS features to reclaim space from the original ZIP file during the extraction process.

## Features

### Core Functionality
- **Robust ZIP Extraction**: Uses standard Central Directory parsing for 100% reliability.
- **ZIP64 Support**: Handles "huge" archives and files exceeding 4GB.
- **Real-time Space Recovery**: Utilizes **NTFS Sparse Files** to zero out and reclaim disk blocks *while* extracting.
- **Final Truncation**: Automatically removes all file data from the original ZIP after extraction, leaving only a tiny "shell" file.
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
| `-d, --dir <path>` | Extract to specified directory |
| `-k, --keep` | Keep original ZIP (don't truncate/sparse) |
| `-f, --force` | Overwrite existing files |
| `-l, --list` | List contents only |
| `-t, --test` | Test archive integrity (CRC) |
| `-p, --preserve` | Preserve file timestamps |
| `-x, --password` | Password for encrypted ZIPs |

### GUI Features
- **Optimized Layout**: Wide-screen interface (550x480) for long file paths.
- **Modern Dark Theme**: Custom dark blue background with high-contrast text.
- **Native Integration**: Windows file browser and threaded processing (UI stays responsive).
- **Intelligent Progress**: Non-wrapping progress labels for clean readability during long operations.
- **Functional Options**: Fully working checkboxes for Keep ZIP, Preserve timestamps, and Force overwrite.

## How It Works (Space Saving)

1. **Sparse Initialization**: If "Keep ZIP" is not selected, the tool marks the original ZIP as an NTFS Sparse File.
2. **Central Directory Parsing**: Maps all file entries reliably.
3. **Extraction & Punching**:
   - Extracts the compressed data to the destination.
   - Immediately "punches" a hole in the original ZIP by zeroing the used data blocks.
   - The OS reclaims physical disk space instantly as the extraction progresses.
4. **Final Truncation**: Once complete, the file is truncated at the Central Directory start, leaving a near-zero byte file.

## Build Requirements

- Windows OS (NTFS filesystem required for real-time space recovery)
- GCC (MinGW)
- zlib development library

### Compilation
```bash
gcc -Wall -Wextra -o bin/dezipper.exe common.c extract.c gui.c main.c -lz -lgdi32 -luser32 -lcomdlg32 -mwindows
```

## History

- **v2.2.0**: 
  - Implemented **NTFS Sparse File** support for real-time space reclamation.
  - Added full **ZIP64** support (archives > 4GB).
  - Switched to 64-bit file offsets (`fseeko64`/`ftello64`).
  - Improved GUI layout and fixed progress text clipping.
- **v2.1.0**: 
  - Switched to Central Directory parsing for robust extraction.
  - Fixed "missing files" bug in linear scanning.
  - Unified CLI and GUI extraction engines.
- **v2.0.0**: 
  - Major refactoring into modular architecture.
  - Fixed GUI checkbox behavior and password retrieval.
  - Initial modular dark theme implementation.

## License

This project is provided as-is for educational and personal use.