#include "extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <io.h>

#ifdef _WIN32
#define fseek64 fseeko64
#define ftell64 ftello64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

void enable_sparse(HANDLE hFile) {
    DWORD dwTemp;
    DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dwTemp, NULL);
}

void free_disk_space(HANDLE hFile, unsigned long long offset, unsigned long long size) {
    if (size == 0) return;
    FILE_ZERO_DATA_INFORMATION fzdi;
    fzdi.FileOffset.QuadPart = (LONGLONG)offset;
    fzdi.BeyondFinalZero.QuadPart = (LONGLONG)(offset + size);
    DWORD dwTemp;
    DeviceIoControl(hFile, FSCTL_SET_ZERO_DATA, &fzdi, sizeof(fzdi), NULL, 0, &dwTemp, NULL);
}

void render_cli_progress_bar(unsigned long long current, unsigned long long total) {
    if (total == 0) return;
    int width = 50;
    float progress = (float)current / total;
    int pos = (int)(width * progress);

    printf("\r[");
    for (int i = 0; i < width; ++i) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3d%% (%I64u/%I64u bytes)", (int)(progress * 100), current, total);
    fflush(stdout);
}

int extract_entry(FILE *zip_file, char *filename, unsigned int filename_length,
                  unsigned short compression_method, unsigned long long compressed_size,
                  unsigned long long uncompressed_size, unsigned short last_mod_time,
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

    if (uncompressed_size == 0 && compressed_size == 0) {
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
            fclose(out_file);
            if (preserve_time) set_file_times(output_path, last_mod_time, last_mod_date);
        }
        return 1;
    }

    unsigned char *compressed_data = (unsigned char *)malloc((size_t)compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Error: Memory allocation failed for compressed data (%I64u bytes)\n", compressed_size);
        fseek64(zip_file, (long long)compressed_size, SEEK_CUR);
        return 0;
    }
    
    size_t bytes_read = fread(compressed_data, 1, (size_t)compressed_size, zip_file);
    if (bytes_read != (size_t)compressed_size) {
        fprintf(stderr, "Error: Failed to read compressed data for '%s' (read %u of %I64u)\n", 
                filename, (unsigned int)bytes_read, compressed_size);
        free(compressed_data);
        return 0;
    }

    unsigned char *file_data = NULL;
    unsigned long long final_size = 0;

    if (compression_method == 0) {
        file_data = compressed_data;
        final_size = compressed_size;
    } else if (compression_method == 8) {
        file_data = (unsigned char *)malloc((size_t)uncompressed_size);
        if (!file_data) {
            fprintf(stderr, "Error: Memory allocation failed for decompressed data (%I64u bytes)\n", uncompressed_size);
            free(compressed_data);
            return 0;
        }
        
        z_stream strm = {0};
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = (uInt)compressed_size;
        strm.next_in = compressed_data;
        strm.avail_out = (uInt)uncompressed_size;
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
            fprintf(stderr, "Error: Failed to decompress '%s' (ret %d)\n", filename, ret);
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
        size_t written = fwrite(file_data, 1, (size_t)final_size, out_file);
        fclose(out_file);
        if (written != (size_t)final_size) {
            fprintf(stderr, "Warning: Incomplete write for '%s'\n", filename);
        }
        if (preserve_time) {
            set_file_times(output_path, last_mod_time, last_mod_date);
        }
    } else {
        fprintf(stderr, "Error: Failed to create output file '%s'\n", output_path);
    }

    if (compression_method != 0) free(file_data);
    else free(compressed_data); // For Method 0, file_data IS compressed_data
    return 1;
}

int extract_zip(const char *zip_path, Options *opts) {
    // Open in r+b to allow sparse operations
    FILE *zip_file = fopen(zip_path, "r+b");
    if (!zip_file) {
        fprintf(stderr, "Error: Cannot open ZIP file '%s'\n", zip_path);
        return 0;
    }

    HANDLE hZip = (HANDLE)_get_osfhandle(fileno(zip_file));
    if (!opts->keep_zip) {
        enable_sparse(hZip);
    }

    char output_dir[MAX_PATH_LEN];
    if (opts->output_dir) {
        strncpy(output_dir, opts->output_dir, MAX_PATH_LEN - 1);
        output_dir[MAX_PATH_LEN - 1] = '\0';
    } else {
        get_zip_basename(zip_path, output_dir, MAX_PATH_LEN);
    }
    create_directory(output_dir);

    // Find EOCD
    fseek64(zip_file, 0, SEEK_END);
    unsigned long long file_size = ftell64(zip_file);
    unsigned long long eocd_pos = 0;
    unsigned char buf[4];
    int found = 0;

    for (int i = 0; i <= 65535; i++) {
        unsigned long long pos = (file_size > 22 + (unsigned long long)i) ? file_size - 22 - i : 0;
        fseek64(zip_file, (long long)pos, SEEK_SET);
        if (fread(buf, 1, 4, zip_file) == 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x05 && buf[3] == 0x06) {
            eocd_pos = pos;
            found = 1;
            break;
        }
        if (pos == 0) break;
    }

    if (!found) {
        fprintf(stderr, "Error: Could not find End of Central Directory signature\n");
        fclose(zip_file);
        return 0;
    }

    unsigned char eocd[22];
    fseek64(zip_file, (long long)eocd_pos, SEEK_SET);
    fread(eocd, 1, 22, zip_file);

    unsigned long long total_entries = READ_LE16(eocd + 10);
    unsigned long long cd_offset = READ_LE32(eocd + 16);

    // Check for ZIP64 EOCD Locator
    if (eocd_pos >= 20) {
        fseek64(zip_file, (long long)(eocd_pos - 20), SEEK_SET);
        unsigned char locator[20];
        if (fread(locator, 1, 20, zip_file) == 20 && locator[0] == 'P' && locator[1] == 'K' && locator[2] == 0x06 && locator[3] == 0x07) {
            unsigned long long zip64_eocd_pos = READ_LE64(locator + 8);
            fseek64(zip_file, (long long)zip64_eocd_pos, SEEK_SET);
            unsigned char eocd64[56];
            if (fread(eocd64, 1, 56, zip_file) == 56 && eocd64[0] == 'P' && eocd64[1] == 'K' && eocd64[2] == 0x06 && eocd64[3] == 0x06) {
                total_entries = READ_LE64(eocd64 + 24);
                cd_offset = READ_LE64(eocd64 + 48);
            }
        }
    }

    int entry_count = 0;
    unsigned long long total_extracted_bytes = 0;
    unsigned long long total_uncompressed_size = 0;

    if (opts->show_progress) {
        fseek64(zip_file, (long long)cd_offset, SEEK_SET);
        for (unsigned long long i = 0; i < total_entries; i++) {
            unsigned char cd[46];
            if (fread(cd, 1, 46, zip_file) != 46) break;
            unsigned long long uncomp = READ_LE32(cd + 24);
            unsigned short name_len = READ_LE16(cd + 28);
            unsigned short extra_len = READ_LE16(cd + 30);
            unsigned short comment_len = READ_LE16(cd + 32);

            if (uncomp == 0xFFFFFFFF) {
                fseek64(zip_file, (long long)name_len, SEEK_CUR);
                unsigned char *extra = (unsigned char *)malloc(extra_len);
                fread(extra, 1, extra_len, zip_file);
                for (int e = 0; e < extra_len - 4; ) {
                    unsigned short tag = READ_LE16(extra + e);
                    unsigned short tsize = READ_LE16(extra + e + 2);
                    if (tag == 0x0001) { uncomp = READ_LE64(extra + e + 4); break; }
                    e += 4 + tsize;
                }
                free(extra);
                fseek64(zip_file, (long long)comment_len, SEEK_CUR);
            } else {
                fseek64(zip_file, (long long)(name_len + extra_len + comment_len), SEEK_CUR);
            }
            total_uncompressed_size += uncomp;
        }
    }

    fseek64(zip_file, (long long)cd_offset, SEEK_SET);

    for (unsigned long long i = 0; i < total_entries; i++) {
        unsigned char cd[46];
        if (fread(cd, 1, 46, zip_file) != 46) break;
        if (cd[0] != 'P' || cd[1] != 'K' || cd[2] != 0x01 || cd[3] != 0x02) break;

        unsigned short method = READ_LE16(cd + 10);
        unsigned short mod_time = READ_LE16(cd + 12);
        unsigned short mod_date = READ_LE16(cd + 14);
        unsigned long long comp_size = READ_LE32(cd + 20);
        unsigned long long uncomp_size = READ_LE32(cd + 24);
        unsigned short name_len = READ_LE16(cd + 28);
        unsigned short extra_len = READ_LE16(cd + 30);
        unsigned short comment_len = READ_LE16(cd + 32);
        unsigned long long local_offset = READ_LE32(cd + 42);

        char *filename = (char *)malloc(name_len + 1);
        fread(filename, 1, name_len, zip_file);
        filename[name_len] = '\0';

        // Check for ZIP64 extra fields
        if (comp_size == 0xFFFFFFFF || uncomp_size == 0xFFFFFFFF || local_offset == 0xFFFFFFFF) {
            unsigned char *extra = (unsigned char *)malloc(extra_len);
            fread(extra, 1, extra_len, zip_file);
            for (int e = 0; e < extra_len - 4; ) {
                unsigned short tag = READ_LE16(extra + e);
                unsigned short tsize = READ_LE16(extra + e + 2);
                if (tag == 0x0001) {
                    int p = e + 4;
                    if (uncomp_size == 0xFFFFFFFF && p + 8 <= e + 4 + tsize) {
                        uncomp_size = READ_LE64(extra + p); p += 8;
                    }
                    if (comp_size == 0xFFFFFFFF && p + 8 <= e + 4 + tsize) {
                        comp_size = READ_LE64(extra + p); p += 8;
                    }
                    if (local_offset == 0xFFFFFFFF && p + 8 <= e + 4 + tsize) {
                        local_offset = READ_LE64(extra + p);
                    }
                }
                e += 4 + tsize;
            }
            free(extra);
            fseek64(zip_file, (long long)comment_len, SEEK_CUR);
        } else {
            fseek64(zip_file, (long long)(extra_len + comment_len), SEEK_CUR);
        }

        unsigned long long next_cd = ftell64(zip_file);

        // Jump to Local Header
        fseek64(zip_file, (long long)local_offset, SEEK_SET);
        unsigned char lfh[30];
        fread(lfh, 1, 30, zip_file);
        unsigned short lfh_name_len = READ_LE16(lfh + 26);
        unsigned short lfh_extra_len = READ_LE16(lfh + 28);
        unsigned long long data_offset = local_offset + 30 + lfh_name_len + lfh_extra_len;
        fseek64(zip_file, (long long)(lfh_name_len + lfh_extra_len), SEEK_CUR);

        if (opts->show_progress) {
            render_cli_progress_bar(total_extracted_bytes, total_uncompressed_size);
        } else if (!opts->quiet) {
            printf("  extracting: %s\n", filename);
        }

        char output_path[MAX_PATH_LEN];
        snprintf(output_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
        normalize_path(output_path);

        if (!opts->force_overwrite && file_exists(output_path)) {
            if (opts->verbose) printf("  skipping: %s (already exists)\n", filename);
            total_extracted_bytes += uncomp_size;
        } else {
            if (extract_entry(zip_file, filename, name_len, method, comp_size, uncomp_size, mod_time, mod_date, output_dir, opts->preserve_time)) {
                entry_count++;
                total_extracted_bytes += uncomp_size;
                if (!opts->keep_zip && comp_size > 0) {
                    free_disk_space(hZip, data_offset, comp_size);
                }
            }
        }

        free(filename);
        fseek64(zip_file, (long long)next_cd, SEEK_SET);
    }

    if (opts->show_progress) {
        render_cli_progress_bar(total_uncompressed_size, total_uncompressed_size);
        printf("\n");
    }

    if (!opts->keep_zip) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)cd_offset;
        SetFilePointerEx(hZip, li, NULL, FILE_BEGIN);
        SetEndOfFile(hZip);
    }

    fclose(zip_file);

    if (!opts->keep_zip) {
        DeleteFile(zip_path);
    }

    return entry_count;
}