#include "extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <io.h>

int extract_entry(FILE *zip_file, char *filename, unsigned int filename_length,
                  unsigned short compression_method, unsigned int compressed_size,
                  unsigned int uncompressed_size, unsigned short last_mod_time,
                  unsigned short last_mod_date, const char *output_dir, int preserve_time) {
    
    int is_directory = (filename_length > 0 && 
                        (filename[filename_length - 1] == '/' || 
                         filename[filename_length - 1] == '\\'));

    if (is_directory) {
        char dir_path[MAX_PATH_LEN];
        if (output_dir) {
            snprintf(dir_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
        } else {
            strncpy(dir_path, filename, MAX_PATH_LEN - 1);
            dir_path[MAX_PATH_LEN - 1] = '\0';
        }
        normalize_path(dir_path);
        size_t dir_len = strlen(dir_path);
        if (dir_len > 0 && (dir_path[dir_len-1] == '\\' || dir_path[dir_len-1] == '/')) {
            dir_path[dir_len-1] = '\0';
        }
        create_directory(dir_path);
        return 1;
    }

    if (compressed_size == 0) {
        char output_path[MAX_PATH_LEN];
        if (output_dir) {
            snprintf(output_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
        } else {
            strncpy(output_path, filename, MAX_PATH_LEN - 1);
            output_path[MAX_PATH_LEN - 1] = '\0';
        }
        normalize_path(output_path);
        
        char *last_slash = strrchr(output_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            create_directory(output_path);
            *last_slash = '\\';
        }
        
        FILE *out_file = fopen(output_path, "wb");
        if (out_file) fclose(out_file);
        return 1;
    }

    unsigned char *compressed_data = (unsigned char *)malloc(compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Error: Memory allocation failed for compressed data\n");
        return 0;
    }
    
    size_t bytes_read = fread(compressed_data, 1, compressed_size, zip_file);
    if (bytes_read != compressed_size) {
        fprintf(stderr, "Error: Failed to read compressed data for '%s' (read %zu of %u)\n", 
                filename, bytes_read, compressed_size);
        free(compressed_data);
        return 0;
    }

    unsigned char *file_data = NULL;
    unsigned int final_size = 0;

    if (compression_method == 0) {
        file_data = compressed_data;
        final_size = compressed_size;
    } else if (compression_method == 8) {
        file_data = (unsigned char *)malloc(uncompressed_size);
        if (!file_data) {
            fprintf(stderr, "Error: Memory allocation failed for decompressed data\n");
            free(compressed_data);
            return 0;
        }
        
        z_stream strm = {0};
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
            return 0;
        }
        
        ret = inflate(&strm, Z_FINISH);
        if (ret != Z_STREAM_END) {
            fprintf(stderr, "Error: Failed to decompress '%s'\n", filename);
            inflateEnd(&strm);
            free(compressed_data);
            free(file_data);
            return 0;
        }
        
        inflateEnd(&strm);
        free(compressed_data);
        final_size = uncompressed_size;
    } else {
        fprintf(stderr, "Warning: Unsupported compression method %d for '%s'\n", 
                compression_method, filename);
        free(compressed_data);
        return 0;
    }

    char output_path[MAX_PATH_LEN];
    if (output_dir) {
        snprintf(output_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
    } else {
        strncpy(output_path, filename, MAX_PATH_LEN - 1);
        output_path[MAX_PATH_LEN - 1] = '\0';
    }
    normalize_path(output_path);

    char *last_slash = strrchr(output_path, '\\');
    if (last_slash) {
        *last_slash = '\0';
        create_directory(output_path);
        *last_slash = '\\';
    }

    FILE *out_file = fopen(output_path, "wb");
    if (out_file) {
        size_t written = fwrite(file_data, 1, final_size, out_file);
        fclose(out_file);
        if (written != final_size) {
            fprintf(stderr, "Warning: Incomplete write for '%s'\n", filename);
        }
        if (preserve_time) {
            set_file_times(output_path, last_mod_time, last_mod_date);
        }
    } else {
        fprintf(stderr, "Error: Failed to create output file '%s'\n", output_path);
    }

    if (compression_method != 0) free(file_data);
    return 1;
}

int extract_zip(const char *zip_path, Options *opts) {
    FILE *zip_file = fopen(zip_path, "rb");
    if (!zip_file) {
        fprintf(stderr, "Error: Cannot open ZIP file '%s'\n", zip_path);
        return 0;
    }

    char output_dir[MAX_PATH_LEN];
    if (opts->output_dir) {
        strncpy(output_dir, opts->output_dir, MAX_PATH_LEN - 1);
        output_dir[MAX_PATH_LEN - 1] = '\0';
    } else {
        get_zip_basename(zip_path, output_dir, MAX_PATH_LEN);
    }
    create_directory(output_dir);

    unsigned char signature[4];
    int entry_count = 0;
    long central_dir_offset = 0;

    while (1) {
        if (fread(signature, 1, 4, zip_file) != 4) {
            if (feof(zip_file)) break;
            fprintf(stderr, "Error: Failed to read from ZIP file\n");
            fclose(zip_file);
            return 0;
        }

        if (signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x05 && signature[3] == 0x06) {
            break;
        }
        if (signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x01 && signature[3] == 0x02) {
            central_dir_offset = ftell(zip_file) - 4;
            break;
        }
        if (!(signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x03 && signature[3] == 0x04)) {
            fprintf(stderr, "Error: Invalid signature at offset %ld\n", ftell(zip_file) - 4);
            fclose(zip_file);
            return 0;
        }

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) break;

        unsigned short compression_method = header[4] | (header[5] << 8);
        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);
        unsigned short last_mod_time = header[6] | (header[7] << 8);
        unsigned short last_mod_date = header[8] | (header[9] << 8);

        char *filename = (char *)malloc(filename_length + 1);
        if (!filename) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            fclose(zip_file);
            return 0;
        }
        fread(filename, 1, filename_length, zip_file);
        filename[filename_length] = '\0';

        fseek(zip_file, extra_field_length, SEEK_CUR);

        int success = extract_entry(zip_file, filename, filename_length, compression_method,
                                  compressed_size, uncompressed_size, last_mod_time, last_mod_date,
                                  output_dir, opts->preserve_time);
        
        if (success) {
            entry_count++;
        }
        
        free(filename);
    }

    fclose(zip_file);
    return entry_count;
}