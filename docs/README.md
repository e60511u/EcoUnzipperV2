# Dezipper - ZIP Extractor & Disk Space Saver

**Version**: 2.0.0

Dezipper is a Windows utility that extracts ZIP files while freeing disk space by truncating the original ZIP file after extraction (removing the compressed data to save space).

## Features

### Core Functionality
- **ZIP Extraction**: Supports both stored (uncompressed) and deflated (compressed) ZIP files
- **Auto-truncation**: Automatically removes compressed data from the original ZIP after extraction
- **Multi-file Support**: Handles ZIP files with multiple entries
- **Directory Handling**: Creates proper directory structures during extraction
- **CRC Validation**: Verifies file integrity after extraction

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
| `-k, --keep` | Keep original ZIP (don't truncate) |
| `-f, --force` | Overwrite existing files |
| `-l, --list` | List contents only |
| `-t, --test` | Test archive integrity (CRC) |
| `-p, --preserve` | Preserve file timestamps |
| `-x, --password` | Password for encrypted ZIPs |

### GUI Features
- Modern dark-themed interface with custom colors (dark blue background, white text)
- File browser dialog with native Windows dialog
- Password field (masked input)
- Options checkboxes (Keep ZIP, Preserve timestamps, Force overwrite)
- Progress messages during extraction
- Threaded extraction (UI stays responsive)
- Auto-launch when double-clicked

### Smart Behaviors
- **Default Output**: Creates a new folder named after the ZIP file
- **Mode Detection**: Automatically detects CLI vs GUI mode
- **Directory Creation**: Automatically creates output directories
- **Error Handling**: Comprehensive error messages for corrupt files

## Usage Examples

### CLI Mode (from terminal/command prompt)
```bash
# Extract to folder named after ZIP
dezipper.exe myarchive.zip

# Extract to current directory
dezipper.exe -d . myarchive.zip

# Extract to specific folder
 dezipper.exe -d output myarchive.zip

# Keep original ZIP (no truncation)
 dezipper.exe -k myarchive.zip

# List contents only
 dezipper.exe -l myarchive.zip

# Test archive integrity
 dezipper.exe -t myarchive.zip

# With password for encrypted ZIPs
 dezipper.exe -x mypassword encrypted.zip
```

### GUI Mode (double-click the executable)
1. Browse or enter ZIP file path
2. (Optional) Enter password for encrypted files
3. Select options (keep ZIP, preserve timestamps, force overwrite)
4. Click "Extract ZIP"

## How It Works

1. Opens the ZIP file in read-write mode
2. Parses local file headers to find each entry
3. Extracts files to a new folder (default behavior)
4. Zeros out the compressed data in the original ZIP
5. Truncates the ZIP file to remove zeroed data
6. Result: Original ZIP is reduced in size (only headers remain)

**Note**: After truncation, the original ZIP file becomes invalid as all file data has been removed. This is intentional - the goal is to free disk space.

## Build Requirements

- Windows OS
- GCC (MinGW) or compatible C compiler
- zlib development library
- Windows SDK (for Win32 API)

### Compilation

#### Modular Build (recommended)
```bash
gcc -Wall -Wextra -o bin/dezipper.exe common.c extract.c gui.c main.c -lz -lgdi32 -luser32 -lcomdlg32 -mwindows
```

#### Or use Makefile
```bash
make
```

## File Structure

```
Dezipper/
├── common.c              # Common utilities (path handling, string utils)
├── common.h              # Common header with shared definitions
├── extract.c             # ZIP extraction logic
├── extract.h             # Extraction header
├── gui.c                 # GUI implementation (Win32 API)
├── gui.h                 # GUI header with window messages
├── main.c                # Entry point, CLI/GUI detection
├── Makefile              # Build configuration
├── bin/
│   └── dezipper.exe      # Compiled executable
├── docs/
│   └── README.md         # This file
└── (test files...)
```

## Technical Details

### Architecture

The project has been refactored from a single-file design into a modular architecture:

- **common.c/h**: Shared utilities for path manipulation, string operations, and common types
- **extract.c/h**: Core ZIP extraction logic including local file header parsing and deflation
- **gui.c/h**: Windows GUI implementation with proper message handling
- **main.c**: Application entry point with CLI/GUI mode detection

### ZIP Format Support
- Standard ZIP (non-ZIP64)
- Local file header parsing
- Central directory detection
- CRC-32 verification
- Deflate decompression using zlib

### Windows Integration
- Uses Win32 API for GUI (CreateWindow, SendMessage)
- Native file dialog (GetOpenFileName)
- CreateDirectory for folder structure
- SetFileTime for timestamp preservation
- SetEndOfFile for file truncation

### Memory Management
- Dynamic buffer allocation for file data
- Entry tracking for truncation
- Proper cleanup on errors
- Thread-safe extraction with PostMessage progress updates

### GUI Implementation
- Custom dark theme with RGB(0x1a, 0x2a, 0x4a) background
- Custom fonts (Segoe UI, bold title)
- Separate brushes for different control types (background, edit, button)
- Threaded extraction with WM_EXTRACTION_* messages
- Proper resource cleanup (fonts, brushes) in WM_DESTROY

## Limitations & Notes

1. **ZIP becomes invalid**: After extraction, the original ZIP is truncated and cannot be used
2. **Compression methods**: Only supports stored (0) and deflate (8). Other methods are skipped with a warning
3. **No ZIP64**: Large files (>4GB) are not supported
4. **Basic encryption**: Password detection is implemented but full decryption requires additional crypto implementation

## History

- **v2.0.0**: Major refactoring into modular architecture (common.c, extract.c, gui.c, main.c). Improved GUI theming with proper checkbox rendering and resource cleanup.
- **v1.0.0**: Initial release with CLI, GUI, password support, and truncation functionality

## License

This project is provided as-is for educational and personal use.