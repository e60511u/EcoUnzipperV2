#ifndef COMMON_H
#define COMMON_H

#define VERSION "2.2.0"
#define MAX_PATH_LEN 512
#define MAX_ENTRIES 1000000

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <winioctl.h>

// Manually define if missing in some environments
#ifndef FSCTL_SET_SPARSE
#define FSCTL_SET_SPARSE CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 49, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#endif

#ifndef FSCTL_SET_ZERO_DATA
#define FSCTL_SET_ZERO_DATA CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 50, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif

#ifndef FILE_ZERO_DATA_INFORMATION_TYPE
#define FILE_ZERO_DATA_INFORMATION_TYPE
typedef struct _FILE_ZERO_DATA_INFORMATION {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER BeyondFinalZero;
} FILE_ZERO_DATA_INFORMATION, *PFILE_ZERO_DATA_INFORMATION;
#endif

// Robust little-endian reading
#define READ_LE16(p) ((unsigned short)((p)[0] | ((p)[1] << 8)))
#define READ_LE32(p) ((unsigned int)((p)[0] | ((p)[1] << 8) | ((p)[2] << 16) | ((p)[3] << 24)))
#define READ_LE64(p) ((unsigned long long)((p)[0]) | \
                     ((unsigned long long)((p)[1]) << 8) | \
                     ((unsigned long long)((p)[2]) << 16) | \
                     ((unsigned long long)((p)[3]) << 24) | \
                     ((unsigned long long)((p)[4]) << 32) | \
                     ((unsigned long long)((p)[5]) << 40) | \
                     ((unsigned long long)((p)[6]) << 48) | \
                     ((unsigned long long)((p)[7]) << 56))

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
    int show_progress;
} Options;

int create_directory(const char *path);
void normalize_path(char *path);
int file_exists(const char *path);
void get_zip_basename(const char *zip_path, char *output, size_t output_size);
unsigned int calculate_crc32(const unsigned char *data, unsigned int size);
int set_file_times(const char *path, unsigned int dostime, unsigned int dosdate);
void TrimSpaces(char *str);
void print_usage(const char *prog_name);
int parse_arguments(int argc, char *argv[], Options *opts, const char **zip_path);
int list_zip_contents(const char *zip_path, Options *opts);

int is_console_attached(void);

#endif