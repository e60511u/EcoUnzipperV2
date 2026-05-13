#ifndef EXTRACT_H
#define EXTRACT_H

#include "common.h"
#include <stdio.h>

typedef void (*ProgressCallback)(void *userdata, int current, int total, const char *filename);

int extract_entry(FILE *zip_file, char *filename, unsigned int filename_length,
                  unsigned short compression_method, unsigned long long compressed_size,
                  unsigned long long uncompressed_size, unsigned short last_mod_time,
                  unsigned short last_mod_date, const char *output_dir, int preserve_time);

int extract_zip(const char *zip_path, Options *opts);

#endif