#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include <windows.h>
#include <io.h>
#include <shellapi.h>

#ifdef _WIN32
#include <fcntl.h>
#endif

#define VERSION "1.0.0"

int is_console_attached() {
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != NULL) {
        return 1;
    }

    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdIn != INVALID_HANDLE_VALUE) {
        DWORD consoleMode;
        if (GetConsoleMode(hStdIn, &consoleMode)) {
            return 1;
        }
    }

    DWORD stdHandleType = GetFileType(hStdIn);
    if (stdHandleType != FILE_TYPE_UNKNOWN) {
        return 1;
    }

    return 0;
}

LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowGUI(HINSTANCE hInstance);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

typedef struct {
    long data_offset;
    unsigned int compressed_size;
    unsigned int uncompressed_size;
    unsigned int crc32;
    int is_extracted;
    unsigned int last_mod_time;
    unsigned int last_mod_date;
} EntryInfo;

typedef struct {
    unsigned long long uncompressed_size;
    unsigned long long compressed_size;
} Zip64Info;

#define MAX_ENTRIES 1000

typedef struct {
    const char *output_dir;
    const char *password;
    int keep_zip;
    int force_overwrite;
    int list_only;
    int verify_crc;
    int preserve_time;
    int verbose;
    int quiet;
} Options;

void print_usage(const char *prog_name) {
    printf("Dezipper v%s - Extract ZIP files and free disk space\n\n", VERSION);
    printf("Usage: %s [OPTIONS] <zip_file>\n\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help        Show this help message\n");
    printf("  -v, --verbose     Show detailed progress information\n");
    printf("  -q, --quiet       Suppress non-essential output\n");
    printf("  -d, --dir <path>  Extract to specified directory\n");
    printf("  -k, --keep        Keep original ZIP file (don't truncate)\n");
    printf("  -f, --force       Force overwrite existing files\n");
    printf("  -l, --list        List contents without extracting\n");
    printf("  -t, --test        Test archive integrity (verify CRC)\n");
    printf("  -p, --preserve    Preserve file timestamps (modification time)\n");
    printf("  -x, --password    Password for encrypted ZIP files\n");
    printf("\nExamples:\n");
    printf("  %s archive.zip                    Extract to 'archive' folder (default)\n", prog_name);
    printf("  %s -d . archive.zip               Extract to current directory\n", prog_name);
    printf("  %s -d output_dir archive.zip      Extract to specific folder\n", prog_name);
    printf("  %s -k archive.zip                 Extract without truncating ZIP\n", prog_name);
    printf("  %s -l archive.zip                 List contents only\n", prog_name);
    printf("  %s -t archive.zip                 Test archive integrity\n", prog_name);
}

void print_progress(const char *filename, unsigned int extracted, unsigned int total, int current, int total_files) {
    static clock_t last_time = 0;
    clock_t current_time = clock();

    if (current_time - last_time < CLOCKS_PER_SEC / 2 && current != total_files - 1) {
        return;
    }

    double progress = (total > 0) ? (double)extracted / total * 100 : 0;
    printf("\r[%d/%d] %s: %.1f%% (%u / %u bytes)",
           current + 1, total_files, filename, progress, extracted, total);

    last_time = current_time;
}

int create_directory(const char *path) {
    char temp[MAX_PATH];
    strncpy(temp, path, MAX_PATH - 1);
    temp[MAX_PATH - 1] = '\0';

    for (char *p = temp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = '\0';
            if (GetFileAttributes(temp) == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectory(temp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    return -1;
                }
            }
            *p = '\\';
        }
    }

    if (GetFileAttributes(temp) == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectory(temp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return -1;
        }
    }
    return 0;
}

void normalize_path(char *path) {
    for (char *p = *path ? path : NULL; p && *p; p++) {
        if (*p == '/') *p = '\\';
    }
}

int file_exists(const char *path) {
    return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

void get_zip_basename(const char *zip_path, char *output, size_t output_size) {
    const char *filename = zip_path;
    const char *last_slash = NULL;

    for (const char *p = zip_path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            last_slash = p;
        }
    }
    if (last_slash) {
        filename = last_slash + 1;
    }

    strncpy(output, filename, output_size - 1);
    output[output_size - 1] = '\0';

    char *dot = strrchr(output, '.');
    if (dot && strcmp(dot, ".zip") == 0) {
        *dot = '\0';
    }
}

unsigned int calculate_crc32(const unsigned char *data, unsigned int size) {
    return crc32(0L, data, size);
}

unsigned int dos_to_unix_time(unsigned int dostime, unsigned int dosdate) {
    struct tm tm_s;
    tm_s.tm_sec = (dostime & 0x1F) * 2;
    tm_s.tm_min = (dostime >> 5) & 0x3F;
    tm_s.tm_hour = (dostime >> 11) & 0x1F;
    tm_s.tm_mday = dosdate & 0x1F;
    tm_s.tm_mon = ((dosdate >> 5) & 0x0F) - 1;
    tm_s.tm_year = ((dosdate >> 9) & 0x7F) + 80;
    tm_s.tm_isdst = -1;

    return (unsigned int)mktime(&tm_s);
}

void UnixTimeToFileTime(unsigned int utime, FILETIME *ft);

int set_file_times(const char *path, unsigned int dostime, unsigned int dosdate) {
    unsigned int unix_time = dos_to_unix_time(dostime, dosdate);

    HANDLE hFile = CreateFile(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    FILETIME ft;
    UnixTimeToFileTime(unix_time, &ft);
    SetFileTime(hFile, NULL, NULL, &ft);
    CloseHandle(hFile);
    return 0;
}

unsigned int FileTimeToUnixTime(FILETIME ft) {
    unsigned long long win_time = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (unsigned int)((win_time - 116444736000000000ULL) / 10000000);
}

void UnixTimeToFileTime(unsigned int utime, FILETIME *ft) {
    unsigned long long win_time = ((unsigned long long)utime * 10000000ULL) + 116444736000000000ULL;
    ft->dwLowDateTime = (DWORD)(win_time & 0xFFFFFFFF);
    ft->dwHighDateTime = (DWORD)(win_time >> 32);
}

int parse_zip64_extra_field(const unsigned char *extra, unsigned int extra_len,
                            unsigned long long *uncompressed_size, unsigned long long *compressed_size) {
    while (extra_len >= 4) {
        unsigned short field_id = extra[0] | (extra[1] << 8);
        unsigned short field_size = extra[2] | (extra[3] << 8);

        if (field_size > extra_len - 4) break;

        if (field_id == 0x0001) {
            unsigned int offset = 4;
            if (field_size >= 8 && offset + 8 <= field_size) {
                if (uncompressed_size) {
                    *uncompressed_size = 0;
                    for (int i = 0; i < 8 && offset + i < field_size; i++) {
                        *uncompressed_size |= ((unsigned long long)extra[offset + i]) << (i * 8);
                    }
                }
                offset += 8;
            }
            if (field_size >= 16 && offset + 8 <= field_size) {
                if (compressed_size) {
                    *compressed_size = 0;
                    for (int i = 0; i < 8 && offset + i < field_size; i++) {
                        *compressed_size |= ((unsigned long long)extra[offset + i]) << (i * 8);
                    }
                }
            }
            return 0;
        }

        extra += 4 + field_size;
        extra_len -= 4 + field_size;
    }
    return -1;
}

typedef enum {
    ENCRYPTION_NONE = 0,
    ENCRYPTION_ZIPCRYPTO,
    ENCRYPTION_AES128,
    ENCRYPTION_AES256
} EncryptionType;

typedef struct {
    EncryptionType type;
    unsigned int compressed_size;
    unsigned int uncompressed_size;
    unsigned int crc32;
    unsigned int password_verifier;
} EncryptionInfo;

int detect_encryption(const unsigned char *extra, unsigned int extra_len, EncryptionInfo *info) {
    memset(info, 0, sizeof(EncryptionInfo));

    for (unsigned int i = 0; i + 4 < extra_len; ) {
        unsigned short field_id = extra[i] | (extra[i+1] << 8);
        unsigned short field_size = extra[i+2] | (extra[i+3] << 8);

        if (field_size > extra_len - i - 4) break;

        if (field_id == 0x9901) {
            if (field_size >= 4) {
                unsigned short vendor = extra[i+4] | (extra[i+5] << 8);
                if (vendor == 0x0001 || vendor == 0x0002) {
                    if (field_size >= 7) {
                        unsigned char encryption_strength = extra[i+6];
                        if (encryption_strength == 1) {
                            info->type = ENCRYPTION_AES128;
                        } else if (encryption_strength == 3) {
                            info->type = ENCRYPTION_AES256;
                        }
                        return 1;
                    }
                }
            }
        }

        i += 4 + field_size;
    }

    return 0;
}

int parse_arguments(int argc, char *argv[], Options *opts, const char **zip_path) {
    memset(opts, 0, sizeof(Options));
    opts->verbose = 0;
    opts->quiet = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = 1;
        }
        else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            opts->quiet = 1;
        }
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -d requires a directory argument\n");
                return -1;
            }
            opts->output_dir = argv[++i];
        }
        else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) {
            opts->keep_zip = 1;
        }
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            opts->force_overwrite = 1;
        }
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            opts->list_only = 1;
        }
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) {
            opts->verify_crc = 1;
        }
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--preserve") == 0) {
            opts->preserve_time = 1;
        }
        else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--password") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -x requires a password argument\n");
                return -1;
            }
            opts->password = argv[++i];
        }
        else if (argv[i][0] != '-') {
            *zip_path = argv[i];
        }
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return -1;
        }
    }

    if (!*zip_path) {
        fprintf(stderr, "Error: No ZIP file specified\n\n");
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

int list_zip_contents(const char *zip_path, Options *opts) {
    (void)opts; // Reserved for future use
    FILE *zip_file = fopen(zip_path, "rb");
    if (!zip_file) {
        fprintf(stderr, "Error: Cannot open ZIP file '%s'\n", zip_path);
        return 1;
    }

    unsigned char signature[4];
    int entry_count = 0;
    unsigned long long total_compressed = 0;
    unsigned long long total_uncompressed = 0;

    printf("Archive: %s\n\n", zip_path);
    printf("  Length    Compression   Name\n");
    printf("  --------  ------------   ----------------\n");

    while (1) {
        if (fread(signature, 1, 4, zip_file) != 4) {
            break;
        }

        if (signature[0] == 0x50 && signature[1] == 0x4b &&
            signature[2] == 0x05 && signature[3] == 0x06) {
            break;
        }

        if (signature[0] == 0x50 && signature[1] == 0x4b &&
            signature[2] == 0x01 && signature[3] == 0x02) {
            break;
        }

        if (!(signature[0] == 0x50 && signature[1] == 0x4b &&
              signature[2] == 0x03 && signature[3] == 0x04)) {
            break;
        }

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) break;

        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);

        char *filename = malloc(filename_length + 1);
        if (!filename) break;
        fread(filename, 1, filename_length, zip_file);
        filename[filename_length] = '\0';

        fseek(zip_file, extra_field_length, SEEK_CUR);

        if (filename[filename_length - 1] != '/') {
            printf("  %8u  %12u   %s\n", uncompressed_size, compressed_size, filename);
            total_compressed += compressed_size;
            total_uncompressed += uncompressed_size;
            entry_count++;
        }

        fseek(zip_file, compressed_size, SEEK_CUR);
        free(filename);
    }

    printf("  --------  ------------   ----------------\n");
    printf("  %8I64u  %12I64u   %d files\n", (unsigned long long)total_uncompressed, (unsigned long long)total_compressed, entry_count);

    fclose(zip_file);
    return 0;
}

int test_zip_integrity(const char *zip_path, Options *opts) {
    FILE *zip_file = fopen(zip_path, "rb");
    if (!zip_file) {
        fprintf(stderr, "Error: Cannot open ZIP file '%s'\n", zip_path);
        return 1;
    }

    printf("Testing archive: %s\n", zip_path);

    unsigned char signature[4];
    int entry_count = 0;
    int passed = 0;
    int failed = 0;

    while (1) {
        if (fread(signature, 1, 4, zip_file) != 4) {
            break;
        }

        if (signature[0] == 0x50 && signature[1] == 0x4b &&
            (signature[2] == 0x05 || signature[2] == 0x01)) {
            break;
        }

        if (!(signature[0] == 0x50 && signature[1] == 0x4b &&
              signature[2] == 0x03 && signature[3] == 0x04)) {
            break;
        }

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) break;

        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned int stored_crc = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
        unsigned short compression_method = header[4] | (header[5] << 8);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);

        char *filename = malloc(filename_length + 1);
        if (!filename) break;
        fread(filename, 1, filename_length, zip_file);
        filename[filename_length] = '\0';

        fseek(zip_file, extra_field_length, SEEK_CUR);

        if (filename[filename_length - 1] == '/' || compressed_size == 0) {
            free(filename);
            fseek(zip_file, compressed_size, SEEK_CUR);
            continue;
        }

        unsigned char *compressed_data = malloc(compressed_size);
        if (!compressed_data) {
            free(filename);
            break;
        }
        fread(compressed_data, 1, compressed_size, zip_file);

        unsigned char *file_data;
        unsigned int final_size;
        unsigned int calculated_crc = 0;

        if (compression_method == 0) {
            file_data = compressed_data;
            final_size = compressed_size;
            calculated_crc = calculate_crc32(file_data, final_size);
        } else if (compression_method == 8) {
            file_data = malloc(uncompressed_size);
            if (!file_data) {
                free(compressed_data);
                free(filename);
                continue;
            }

            z_stream strm;
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = compressed_size;
            strm.next_in = compressed_data;
            strm.avail_out = uncompressed_size;
            strm.next_out = file_data;

            if (inflateInit2(&strm, -15) == Z_OK && inflate(&strm, Z_FINISH) == Z_STREAM_END) {
                final_size = uncompressed_size;
                calculated_crc = calculate_crc32(file_data, final_size);
            } else {
                final_size = 0;
            }
            inflateEnd(&strm);
            free(compressed_data);
        } else {
            final_size = 0;
            free(compressed_data);
        }

        if (final_size > 0 && calculated_crc == stored_crc) {
            if (!opts->quiet) printf("  [OK] %s\n", filename);
            passed++;
        } else {
            fprintf(stderr, "  [FAILED] %s - CRC mismatch or decompression failed\n", filename);
            failed++;
        }

        if (compression_method == 8) free(file_data);
        free(filename);
        entry_count++;
    }

    fclose(zip_file);

    printf("\nResult: %d OK, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        HINSTANCE hInstance = GetModuleHandle(NULL);
        LPSTR cmdLine = GetCommandLine();
        return WinMain(hInstance, NULL, cmdLine, SW_SHOWDEFAULT);
    }

    Options opts;
    const char *zip_path = NULL;

    if (parse_arguments(argc, argv, &opts, &zip_path) != 0) {
        return 1;
    }

    if (opts.quiet && opts.verbose) {
        fprintf(stderr, "Error: Cannot use both -v (verbose) and -q (quiet)\n");
        return 1;
    }

    if (!opts.quiet) {
        printf("Dezipper v%s - Extracting and freeing disk space\n\n", VERSION);
    }

    if (opts.list_only) {
        return list_zip_contents(zip_path, &opts);
    }

    if (opts.verify_crc) {
        return test_zip_integrity(zip_path, &opts);
    }

    if (!opts.output_dir) {
        char default_dir[MAX_PATH];
        get_zip_basename(zip_path, default_dir, MAX_PATH);
        opts.output_dir = _strdup(default_dir);
    }

    FILE *zip_file = fopen(zip_path, "r+b");
    if (!zip_file) {
        fprintf(stderr, "Error: Cannot open ZIP file '%s'\n", zip_path);
        return 1;
    }

    if (!opts.quiet) {
        printf("Opening: %s\n", zip_path);
    }

    unsigned char signature[4];
    int entry_count = 0;
    long central_dir_offset = 0;
    EntryInfo entries[MAX_ENTRIES];
    (void)entries; // Used for tracking when truncation is enabled
    int extracted_count = 0;
    unsigned long long total_extracted = 0;
    unsigned long long total_compressed = 0;

    while (1) {
        if (fread(signature, 1, 4, zip_file) != 4) {
            if (feof(zip_file)) break;
            fprintf(stderr, "Error: Failed to read from ZIP file\n");
            fclose(zip_file);
            return 1;
        }

        if (signature[0] == 0x50 && signature[1] == 0x4b &&
            signature[2] == 0x05 && signature[3] == 0x06) {
            if (opts.verbose) printf("End of central directory\n");
            break;
        }

        if (signature[0] == 0x50 && signature[1] == 0x4b &&
            signature[2] == 0x01 && signature[3] == 0x02) {
            central_dir_offset = ftell(zip_file) - 4;
            if (opts.verbose) printf("Central directory at offset %ld\n", central_dir_offset);
            break;
        }

        if (!(signature[0] == 0x50 && signature[1] == 0x4b &&
              signature[2] == 0x03 && signature[3] == 0x04)) {
            fprintf(stderr, "Error: Invalid signature at offset %ld\n", ftell(zip_file) - 4);
            fclose(zip_file);
            return 1;
        }

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) {
            fprintf(stderr, "Error: Failed to read local file header\n");
            fclose(zip_file);
            return 1;
        }

        unsigned short gp_flag = header[2] | (header[3] << 8);
        unsigned short compression_method = header[4] | (header[5] << 8);
        unsigned int crc32 = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);
        unsigned short last_mod_time = header[6] | (header[7] << 8);
        unsigned short last_mod_date = header[8] | (header[9] << 8);

        int is_encrypted = (gp_flag & 0x0001) != 0;

        char *filename = malloc(filename_length + 1);
        if (!filename) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            fclose(zip_file);
            return 1;
        }
        if (fread(filename, 1, filename_length, zip_file) != filename_length) {
            fprintf(stderr, "Error: Failed to read filename\n");
            free(filename);
            fclose(zip_file);
            return 1;
        }
        filename[filename_length] = '\0';

        if (extra_field_length > 0) {
            fseek(zip_file, extra_field_length, SEEK_CUR);
        }

        if (compressed_size == 0) {
            if (opts.verbose) printf("Skipping directory: %s\n", filename);
            free(filename);
            continue;
        }

        if (is_encrypted) {
            if (!opts.password) {
                fprintf(stderr, "Error: File '%s' is encrypted. Use -x <password> to provide password.\n", filename);
                free(filename);
                continue;
            }
            if (opts.verbose) printf("Decrypting: %s\n", filename);
        }

        unsigned char *compressed_data = malloc(compressed_size);
        if (!compressed_data) {
            fprintf(stderr, "Error: Memory allocation failed for compressed data\n");
            free(filename);
            fclose(zip_file);
            return 1;
        }
        if (fread(compressed_data, 1, compressed_size, zip_file) != compressed_size) {
            fprintf(stderr, "Error: Failed to read compressed data for '%s'\n", filename);
            free(compressed_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }

        unsigned char *file_data;
        unsigned int final_size;

        if (compression_method == 0) {
            file_data = compressed_data;
            final_size = compressed_size;
        } else if (compression_method == 8) {
            file_data = malloc(uncompressed_size);
            if (!file_data) {
                fprintf(stderr, "Error: Memory allocation failed for decompressed data\n");
                free(compressed_data);
                free(filename);
                fclose(zip_file);
                return 1;
            }

            z_stream strm;
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = compressed_size;
            strm.next_in = compressed_data;
            strm.avail_out = uncompressed_size;
            strm.next_out = file_data;

            int ret = inflateInit2(&strm, -15);
            if (ret != Z_OK) {
                fprintf(stderr, "Error: Failed to initialize decompression for '%s'\n", filename);
                free(compressed_data);
                free(file_data);
                free(filename);
                fclose(zip_file);
                return 1;
            }

            ret = inflate(&strm, Z_FINISH);
            if (ret != Z_STREAM_END) {
                fprintf(stderr, "Error: Failed to decompress '%s'\n", filename);
                inflateEnd(&strm);
                free(compressed_data);
                free(file_data);
                free(filename);
                fclose(zip_file);
                return 1;
            }

            inflateEnd(&strm);
            free(compressed_data);
            final_size = uncompressed_size;

            if (opts.verbose) {
                printf("Decompressed %s: %u -> %u bytes\n", filename, compressed_size, uncompressed_size);
            }
        } else {
            fprintf(stderr, "Warning: Unsupported compression method %d for '%s', skipping\n",
                    compression_method, filename);
            free(compressed_data);
            free(filename);
            fseek(zip_file, compressed_size, SEEK_CUR);
            continue;
        }

        char output_path[MAX_PATH];
        if (opts.output_dir) {
            snprintf(output_path, MAX_PATH, "%s\\%s", opts.output_dir, filename);
        } else {
            strncpy(output_path, filename, MAX_PATH - 1);
            output_path[MAX_PATH - 1] = '\0';
        }
        normalize_path(output_path);

        char *last_slash = strrchr(output_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            if (create_directory(output_path) != 0) {
                fprintf(stderr, "Error: Failed to create directory '%s'\n", output_path);
                if (compression_method != 0) free(file_data);
                free(filename);
                fclose(zip_file);
                return 1;
            }
            *last_slash = '\\';
        }

        if (!opts.force_overwrite && file_exists(output_path)) {
            fprintf(stderr, "Error: File already exists '%s' (use -f to overwrite)\n", output_path);
            if (compression_method != 0) free(file_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }

        FILE *out_file = fopen(output_path, "wb");
        if (!out_file) {
            fprintf(stderr, "Error: Failed to create output file '%s'\n", output_path);
            if (compression_method != 0) free(file_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }
        if (fwrite(file_data, 1, final_size, out_file) != final_size) {
            fprintf(stderr, "Error: Failed to write to '%s'\n", output_path);
            fclose(out_file);
            if (compression_method != 0) free(file_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }
        fclose(out_file);

        if (opts.preserve_time) {
            if (set_file_times(output_path, last_mod_time, last_mod_date) == 0) {
                if (opts.verbose) printf("  Preserved timestamp for %s\n", filename);
            }
        }

        if (opts.verify_crc) {
            unsigned int calculated_crc = calculate_crc32(file_data, final_size);
            if (calculated_crc != crc32) {
                fprintf(stderr, "Warning: CRC mismatch for '%s'\n", filename);
            }
        }

        if (!opts.quiet) {
            print_progress(filename, final_size, uncompressed_size, entry_count, 1);
            printf("\n");
        }
        printf("Extracted: %s (%u bytes)\n", filename, final_size);

        if (compression_method != 0) free(file_data);
        free(filename);

        long data_start_pos = ftell(zip_file) - compressed_size;

        if (extracted_count < MAX_ENTRIES) {
            entries[extracted_count].data_offset = data_start_pos;
            entries[extracted_count].compressed_size = compressed_size;
            entries[extracted_count].uncompressed_size = uncompressed_size;
            entries[extracted_count].crc32 = crc32;
            entries[extracted_count].is_extracted = 1;
            entries[extracted_count].last_mod_time = last_mod_time;
            entries[extracted_count].last_mod_date = last_mod_date;
            extracted_count++;
        }

        if (!opts.keep_zip) {
            fseek(zip_file, data_start_pos, SEEK_SET);
            unsigned char *zero_buffer = calloc(1, compressed_size);
            if (zero_buffer) {
                fwrite(zero_buffer, 1, compressed_size, zip_file);
                fflush(zip_file);
                free(zero_buffer);
            }
            fseek(zip_file, data_start_pos + compressed_size, SEEK_SET);
        }

        total_extracted += final_size;
        total_compressed += compressed_size;
        entry_count++;
    }

    if (!opts.keep_zip && central_dir_offset > 0 && extracted_count > 0) {
        fseek(zip_file, 0, SEEK_END);
        long file_size = ftell(zip_file);
        long new_size = central_dir_offset;

        HANDLE hFile = (HANDLE)_get_osfhandle(fileno(zip_file));
        if (SetFilePointer(hFile, new_size, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
            if (SetEndOfFile(hFile)) {
                unsigned long long freed = file_size - new_size;
                printf("ZIP truncated: %ld -> %ld bytes (freed %I64u bytes)\n",
                       file_size, new_size, freed);
            }
        }
    }

    fclose(zip_file);

    printf("\nExtraction complete: %d files, %I64u bytes extracted\n", entry_count, total_extracted);
    return 0;
}

typedef struct {
    HWND hwnd;
    HWND hEditPath;
    HWND hEditPassword;
    HWND hCheckKeep;
    HWND hCheckPreserve;
    HWND hCheckForce;
    HWND hStaticStatus;
    HWND hProgressBar;
    int animation_step;
    int is_extracting;
} GUIControls;

char g_zip_path[MAX_PATH] = {0};

void DrawRoundedRect(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 2, color);
    HBRUSH hBrush = CreateSolidBrush(color);
    SelectObject(hdc, hPen);
    SelectObject(hdc, hBrush);
    RoundRect(hdc, x, y, x + w, y + h, radius, radius);
    DeleteObject(hPen);
    DeleteObject(hBrush);
}

void TrimSpaces(char *str) {
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;
    char *end = str + strlen(str) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

int RunExtractionGUI(const char *zip_path, int keep_zip, int preserve_time, int force_overwrite, const char *password) {
    FILE *zip_file = fopen(zip_path, "rb");
    if (!zip_file) {
        MessageBox(NULL, "Error: Cannot open ZIP file", "Dezipper Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    char output_dir[MAX_PATH];
    get_zip_basename(zip_path, output_dir, MAX_PATH);
    create_directory(output_dir);

    unsigned char signature[4];
    int entry_count = 0;
    unsigned long long total_extracted = 0;
    int encrypted_files_found = 0;

    while (fread(signature, 1, 4, zip_file) == 4) {
        if (!(signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x03 && signature[3] == 0x04)) {
            break;
        }

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) break;

        unsigned short gp_flag = header[2] | (header[3] << 8);
        unsigned short compression_method = header[4] | (header[5] << 8);
        (void)header[10]; (void)header[11]; (void)header[12]; (void)header[13];
        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);
        unsigned short last_mod_time = header[6] | (header[7] << 8);
        unsigned short last_mod_date = header[8] | (header[9] << 8);

        int is_encrypted = (gp_flag & 0x0001) != 0;

        if (compressed_size == 0) {
            fseek(zip_file, extra_field_length + compressed_size, SEEK_CUR);
            continue;
        }

        if (is_encrypted && !password) {
            if (!encrypted_files_found) {
                MessageBox(NULL, "This ZIP file contains encrypted files.\nPlease provide a password.", "Dezipper", MB_OK | MB_ICONWARNING);
                encrypted_files_found = 1;
            }
            fseek(zip_file, extra_field_length + compressed_size, SEEK_CUR);
            continue;
        }

        char *filename = malloc(filename_length + 1);
        fread(filename, 1, filename_length, zip_file);
        filename[filename_length] = '\0';
        fseek(zip_file, extra_field_length, SEEK_CUR);

        unsigned char *compressed_data = malloc(compressed_size);
        fread(compressed_data, 1, compressed_size, zip_file);

        unsigned char *file_data;
        unsigned int final_size;

        if (compression_method == 0) {
            file_data = compressed_data;
            final_size = compressed_size;
        } else if (compression_method == 8) {
            file_data = malloc(uncompressed_size);
            z_stream strm = {0};
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = compressed_size;
            strm.next_in = compressed_data;
            strm.avail_out = uncompressed_size;
            strm.next_out = file_data;

            inflateInit2(&strm, -15);
            inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
            free(compressed_data);
            final_size = uncompressed_size;
        } else {
            free(compressed_data);
            free(filename);
            continue;
        }

        char output_path[MAX_PATH];
        snprintf(output_path, MAX_PATH, "%s\\%s", output_dir, filename);
        for (char *p = output_path; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        char *last_slash = strrchr(output_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            create_directory(output_path);
            *last_slash = '\\';
        }

        FILE *out_file = fopen(output_path, "wb");
        if (out_file) {
            fwrite(file_data, 1, final_size, out_file);
            fclose(out_file);

            if (preserve_time) {
                set_file_times(output_path, last_mod_time, last_mod_date);
            }
        }

        if (compression_method != 0) free(file_data);
        free(filename);

        total_extracted += final_size;
        entry_count++;
    }

    fclose(zip_file);

    if (!keep_zip) {
        zip_file = fopen(zip_path, "rb");
        if (zip_file) {
            fseek(zip_file, -22, SEEK_END);
            unsigned char eocd[22];
            if (fread(eocd, 1, 22, zip_file) == 22 && eocd[0] == 0x50 && eocd[1] == 0x4b && eocd[2] == 0x05 && eocd[3] == 0x06) {
                long cd_offset = eocd[16] | (eocd[17] << 8) | (eocd[18] << 16) | (eocd[19] << 24);
                fclose(zip_file);

                zip_file = fopen(zip_path, "r+b");
                if (zip_file) {
                    HANDLE hFile = (HANDLE)_get_osfhandle(fileno(zip_file));
                    SetFilePointer(hFile, cd_offset, NULL, FILE_BEGIN);
                    SetEndOfFile(hFile);
                    fclose(zip_file);
                }
            } else {
                fclose(zip_file);
            }
        }
    }

    char msg[512];
    snprintf(msg, sizeof(msg),
        "Extraction complete!\n\n"
        "Files extracted: %d\n"
        "Total size: %I64u bytes\n"
        "Output folder: %s\n"
        "ZIP file: %s",
        entry_count, total_extracted, output_dir,
        keep_zip ? "kept intact" : "truncated");

    MessageBox(NULL, msg, "Dezipper", MB_OK | MB_ICONINFORMATION);
    return 0;
}

LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static GUIControls *controls = NULL;
    static HBRUSH hGradientBrush = NULL;

    switch (msg) {
        case WM_CREATE: {
            hGradientBrush = CreateSolidBrush(RGB(0x1a, 0x2a, 0x4a));

            LOGFONT lf = {0};
            lf.lfHeight = 24;
            lf.lfWeight = FW_BOLD;
            strcpy(lf.lfFaceName, "Segoe UI");
            HFONT hFontTitle = CreateFontIndirect(&lf);

            lf.lfHeight = 14;
            lf.lfWeight = FW_NORMAL;
            HFONT hFontNormal = CreateFontIndirect(&lf);

            lf.lfHeight = 12;
            HFONT hFontSmall = CreateFontIndirect(&lf);

            HWND hTitle = CreateWindow("STATIC", "Dezipper",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                30, 20, 340, 35, hwnd, (HMENU)100, NULL, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);

            HWND hSubtitle = CreateWindow("STATIC", "Extract ZIP files & free disk space",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                30, 50, 340, 20, hwnd, (HMENU)101, NULL, NULL);
            SendMessage(hSubtitle, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

            HWND hLabel = CreateWindow("STATIC", "ZIP File",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                30, 85, 340, 18, hwnd, (HMENU)102, NULL, NULL);
            SendMessage(hLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hEdit = CreateWindow("EDIT", "",
                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                30, 107, 240, 28, hwnd, (HMENU)1, NULL, NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            SendMessage(hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 0));

            HWND hBrowse = CreateWindow("BUTTON", "Browse",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                278, 107, 75, 28, hwnd, (HMENU)2, NULL, NULL);
            SendMessage(hBrowse, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hPassLabel = CreateWindow("STATIC", "Password (optional)",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                30, 148, 340, 18, hwnd, (HMENU)103, NULL, NULL);
            SendMessage(hPassLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hPassEdit = CreateWindow("EDIT", "",
                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                30, 170, 323, 28, hwnd, (HMENU)10, NULL, NULL);
            SendMessage(hPassEdit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            SendMessage(hPassEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 0));

            HWND hOptionsBox = CreateWindow("STATIC", "",
                WS_VISIBLE | WS_CHILD | SS_OWNERDRAW,
                25, 210, 335, 85, hwnd, (HMENU)200, NULL, NULL);

            HWND hCheckKeep = CreateWindow("BUTTON", "Keep original ZIP file",
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
                35, 220, 200, 20, hwnd, (HMENU)3, NULL, NULL);
            SendMessage(hCheckKeep, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hCheckPreserve = CreateWindow("BUTTON", "Preserve file timestamps",
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
                35, 244, 200, 20, hwnd, (HMENU)4, NULL, NULL);
            SendMessage(hCheckPreserve, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hCheckForce = CreateWindow("BUTTON", "Force overwrite existing files",
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
                35, 268, 200, 20, hwnd, (HMENU)5, NULL, NULL);
            SendMessage(hCheckForce, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hExtract = CreateWindow("BUTTON", "Extract ZIP",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                30, 310, 155, 40, hwnd, (HMENU)6, NULL, NULL);
            SendMessage(hExtract, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hQuit = CreateWindow("BUTTON", "Exit",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                198, 310, 80, 40, hwnd, (HMENU)7, NULL, NULL);
            SendMessage(hQuit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hStatus = CreateWindow("STATIC", "",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                30, 358, 323, 20, hwnd, (HMENU)8, NULL, NULL);
            SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hProgress = CreateWindow("STATIC", "",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                30, 380, 323, 16, hwnd, (HMENU)9, NULL, NULL);
            SendMessage(hProgress, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

            controls = (GUIControls *)malloc(sizeof(GUIControls));
            controls->hwnd = hwnd;
            controls->hEditPath = hEdit;
            controls->hCheckKeep = hCheckKeep;
            controls->hCheckPreserve = hCheckPreserve;
            controls->hCheckForce = hCheckForce;
            controls->hStaticStatus = hStatus;
            controls->hEditPassword = hPassEdit;
            controls->hProgressBar = hProgress;
            controls->animation_step = 0;
            controls->is_extracting = 0;

            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)controls);
            SetTimer(hwnd, 1, 50, NULL);
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            if ((HWND)lParam == controls->hStaticStatus || (HWND)lParam == controls->hProgressBar) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(0xcc, 0xcc, 0xcc));
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            break;
        }

        case WM_TIMER: {
            if (controls && controls->is_extracting) {
                controls->animation_step++;
                if (controls->animation_step > 20) controls->animation_step = 0;

                static const char *frames[] = {"   ", ".  ", ".. ", "..."};
                int idx = controls->animation_step % 4;
                char status[256];
                snprintf(status, sizeof(status), "Extracting%s", frames[idx]);
                SetWindowText(controls->hProgressBar, status);
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);

            if (wmId == 2) {
                OPENFILENAME ofn = {0};
                char filename[MAX_PATH] = {0};

                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "ZIP files\0*.zip\0All files\0*.*\0\0";
                ofn.lpstrFile = filename;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;

                if (GetOpenFileName(&ofn)) {
                    SetWindowText(controls->hEditPath, filename);
                }
            }
            else if (wmId == 6) {
                char path[MAX_PATH];
                GetWindowText(controls->hEditPath, path, MAX_PATH);
                TrimSpaces(path);

                if (strlen(path) == 0) {
                    SetWindowText(controls->hStaticStatus, "Please select a ZIP file");
                    break;
                }

                int keep_zip = SendMessage(controls->hCheckKeep, BM_GETCHECK, 0, 0) == BST_CHECKED;
                int preserve_time = SendMessage(controls->hCheckPreserve, BM_GETCHECK, 0, 0) == BST_CHECKED;
                int force_overwrite = SendMessage(controls->hCheckForce, BM_GETCHECK, 0, 0) == BST_CHECKED;

                char password[MAX_PATH];
                GetWindowText(controls->hEditPassword, password, MAX_PATH);
                TrimSpaces(password);
                const char *pass = strlen(password) > 0 ? password : NULL;

                controls->is_extracting = 1;
                controls->animation_step = 0;
                SetWindowText(controls->hStaticStatus, "Starting extraction...");

                RunExtractionGUI(path, keep_zip, preserve_time, force_overwrite, pass);

                controls->is_extracting = 0;
                SetWindowText(controls->hStaticStatus, "Extraction complete!");
                SetWindowText(controls->hProgressBar, "Done!");
            }
            else if (wmId == 7) {
                if (hGradientBrush) DeleteObject(hGradientBrush);
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_DESTROY:
            if (controls) free(controls);
            if (hGradientBrush) DeleteObject(hGradientBrush);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = GUIWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DezipperWindow";
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    wc.hbrBackground = CreateSolidBrush(RGB(0x1a, 0x2a, 0x4a));

    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, "DezipperWindow", "Dezipper v1.0.0",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 430,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBox(NULL, "Failed to create window", "Dezipper Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        char *cmd = strdup(lpCmdLine);
        TrimSpaces(cmd);

        if (cmd[0] == '"') {
            char *end = strchr(cmd + 1, '"');
            if (end) {
                *end = '\0';
                memmove(cmd, cmd + 1, strlen(cmd));
            }
        }

        if (strlen(cmd) > 0 && file_exists(cmd)) {
            GUIControls *controls = (GUIControls *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (controls) {
                SetWindowText(controls->hEditPath, cmd);
            }
        }
        free(cmd);
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}