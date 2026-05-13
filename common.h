#ifndef COMMON_H
#define COMMON_H

#define VERSION "1.0.0"
#define MAX_PATH_LEN 512
#define MAX_ENTRIES 1000

#include <windows.h>

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

typedef struct {
    long data_offset;
    unsigned int compressed_size;
    unsigned int uncompressed_size;
    unsigned int crc32;
    int is_extracted;
} EntryInfo;

int is_console_attached(void);

#endif