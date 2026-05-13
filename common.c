#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

int create_directory(const char *path) {
    char temp[MAX_PATH_LEN];
    strncpy(temp, path, MAX_PATH_LEN - 1);
    temp[MAX_PATH_LEN - 1] = '\0';
    
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
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
}

int file_exists(const char *path) {
    return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

void get_zip_basename(const char *zip_path, char *output, size_t output_size) {
    const char *filename = zip_path;
    for (const char *p = zip_path; *p; p++) {
        if (*p == '\\' || *p == '/') filename = p + 1;
    }
    strncpy(output, filename, output_size - 1);
    output[output_size - 1] = '\0';
    char *dot = strrchr(output, '.');
    if (dot && strcmp(dot, ".zip") == 0) *dot = '\0';
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

void UnixTimeToFileTime(unsigned int utime, FILETIME *ft) {
    unsigned long long win_time = ((unsigned long long)utime * 10000000ULL) + 116444736000000000ULL;
    ft->dwLowDateTime = (DWORD)(win_time & 0xFFFFFFFF);
    ft->dwHighDateTime = (DWORD)(win_time >> 32);
}

int set_file_times(const char *path, unsigned int dostime, unsigned int dosdate) {
    unsigned int unix_time = dos_to_unix_time(dostime, dosdate);
    HANDLE hFile = CreateFile(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return -1;
    FILETIME ft;
    UnixTimeToFileTime(unix_time, &ft);
    SetFileTime(hFile, NULL, NULL, &ft);
    CloseHandle(hFile);
    return 0;
}

void TrimSpaces(char *str) {
    if (!str || !*str) return;
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;
    char *end = str + strlen(str) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    if (start != str) memmove(str, start, strlen(start) + 1);
}

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
    printf("  -p, --preserve    Preserve file timestamps\n");
    printf("  -P, --progress    Show progress bar\n");
    printf("  -x, --password    Password for encrypted ZIP files\n");
}

int parse_arguments(int argc, char *argv[], Options *opts, const char **zip_path) {
    memset(opts, 0, sizeof(Options));
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) opts->verbose = 1;
        else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) opts->quiet = 1;
        else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--progress") == 0) opts->show_progress = 1;
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: -d requires a directory argument\n"); return -1; }
            opts->output_dir = argv[++i];
        }
        else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) opts->keep_zip = 1;
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) opts->force_overwrite = 1;
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) opts->list_only = 1;
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) opts->verify_crc = 1;
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--preserve") == 0) opts->preserve_time = 1;
        else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--password") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: -x requires a password argument\n"); return -1; }
            opts->password = argv[++i];
        }
        else if (argv[i][0] != '-') *zip_path = argv[i];
        else { fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]); return -1; }
    }
    if (!*zip_path) { fprintf(stderr, "Error: No ZIP file specified\n\n"); print_usage(argv[0]); return -1; }
    return 0;
}

int list_zip_contents(const char *zip_path, Options *opts) {
    (void)opts;
    FILE *zip_file = fopen(zip_path, "rb");
    if (!zip_file) { fprintf(stderr, "Error: Cannot open ZIP file '%s'\n", zip_path); return 1; }
    
    unsigned char signature[4];
    int entry_count = 0;
    unsigned long long total_compressed = 0, total_uncompressed = 0;

    printf("Archive: %s\n\n", zip_path);
    printf("  Length    Compression   Name\n");
    printf("  --------  ------------   ----------------\n");

    while (1) {
        if (fread(signature, 1, 4, zip_file) != 4) break;
        if (signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x05 && signature[3] == 0x06) break;
        if (signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x01 && signature[3] == 0x02) break;
        if (!(signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x03 && signature[3] == 0x04)) break;

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) break;

        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);

        char *filename = malloc(filename_length + 1);
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
    printf("  %8I64u  %12I64u   %d files\n", total_uncompressed, total_compressed, entry_count);
    fclose(zip_file);
    return 0;
}

int is_console_attached(void) {
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != NULL) return 1;
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdIn != INVALID_HANDLE_VALUE) {
        DWORD consoleMode;
        if (GetConsoleMode(hStdIn, &consoleMode)) return 1;
    }
    DWORD stdHandleType = GetFileType(hStdIn);
    if (stdHandleType != FILE_TYPE_UNKNOWN) return 1;
    return 0;
}