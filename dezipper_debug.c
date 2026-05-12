#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <windows.h>

// Function to create directory if it doesn't exist
int create_directory(const char *path) {
    char temp[MAX_PATH];
    strcpy(temp, path);
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
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <zip_file>\n", argv[0]);
        return 1;
    }

    const char *zip_path = argv[1];
    FILE *zip_file = fopen(zip_path, "rb"); // Open read-only for debugging
    if (!zip_file) {
        perror("Error opening ZIP file");
        return 1;
    }

    fseek(zip_file, 0, SEEK_END);
    long file_size = ftell(zip_file);
    rewind(zip_file);
    printf("ZIP file size: %ld bytes\n", file_size);

    unsigned char signature[4];
    int entry_count = 0;
    while (1) {
        // Read signature
        if (fread(signature, 1, 4, zip_file) != 4) {
            if (feof(zip_file)) break;
            perror("Error reading signature");
            fclose(zip_file);
            return 1;
        }
        printf("\nRead signature: %02x%02x%02x%02x at offset %ld\n", 
               signature[0], signature[1], signature[2], signature[3], ftell(zip_file)-4);

        // Check for end of central directory signature
        if (signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x05 && signature[3] == 0x06) {
            printf("End of central directory reached at offset %ld\n", ftell(zip_file)-4);
            break;
        }

        // Check for local file header signature
        if (!(signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x03 && signature[3] == 0x04)) {
            fprintf(stderr, "Unexpected signature: %02x%02x%02x%02x\n", signature[0], signature[1], signature[2], signature[3]);
            fclose(zip_file);
            return 1;
        }

        // Read local file header (26 bytes)
        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) {
            perror("Error reading local header");
            fclose(zip_file);
            return 1;
        }

        // Extract fields (little-endian)
        unsigned short version_needed = header[0] | (header[1] << 8);
        unsigned short gp_flag = header[2] | (header[3] << 8);
        unsigned short compression_method = header[4] | (header[5] << 8);
        unsigned short last_mod_time = header[6] | (header[7] << 8);
        unsigned short last_mod_date = header[8] | (header[9] << 8);
        unsigned int crc32 = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);

        printf("Local file header at offset %ld:\n", ftell(zip_file)-30);
        printf("  version_needed: %d\n", version_needed);
        printf("  gp_flag: 0x%04x\n", gp_flag);
        printf("  compression_method: %d\n", compression_method);
        printf("  crc32: 0x%08x\n", crc32);
        printf("  compressed_size: %u\n", compressed_size);
        printf("  uncompressed_size: %u\n", uncompressed_size);
        printf("  filename_length: %d\n", filename_length);
        printf("  extra_field_length: %d\n", extra_field_length);

        // Read filename
        char *filename = malloc(filename_length + 1);
        if (!filename) {
            perror("malloc failed");
            fclose(zip_file);
            return 1;
        }
        if (fread(filename, 1, filename_length, zip_file) != filename_length) {
            perror("Error reading filename");
            free(filename);
            fclose(zip_file);
            return 1;
        }
        filename[filename_length] = '\0';
        printf("  filename: %s\n", filename);

        // Read extra field
        if (extra_field_length > 0) {
            printf("  skipping %d bytes of extra field\n", extra_field_length);
            fseek(zip_file, extra_field_length, SEEK_CUR);
        }

        // Handle data descriptor if GP flag bit 3 is set
        if (gp_flag & 0x0008) {
            fprintf(stderr, "Data descriptor not supported\n");
            free(filename);
            fclose(zip_file);
            return 1;
        }

        // Current position should be at start of file data
        long data_start = ftell(zip_file);
        printf("  file data starts at offset %ld, size %u bytes\n", data_start, compressed_size);

        // Read file data
        unsigned char *file_data = malloc(compressed_size);
        if (!file_data) {
            perror("malloc failed");
            free(filename);
            fclose(zip_file);
            return 1;
        }
        if (fread(file_data, 1, compressed_size, zip_file) != compressed_size) {
            perror("Error reading file data");
            free(file_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }

        printf("  first 20 bytes of file data: ");
        for (int i = 0; i < 20 && i < compressed_size; i++) {
            printf("%02x ", file_data[i]);
        }
        printf("\n");

        // Try to interpret as text if it looks like text
        int printable = 1;
        for (int i = 0; i < compressed_size && i < 50; i++) {
            if (file_data[i] < 32 && file_data[i] != '\n' && file_data[i] != '\r' && file_data[i] != '\t') {
                printable = 0;
                break;
            }
        }
        if (printable) {
            printf("  file data as text (first 100 chars): \"%.*s\"\n", 
                   compressed_size < 100 ? compressed_size : 100, file_data);
        } else {
            printf("  file data does not appear to be plain text\n");
        }

        // Only handle stored data (compression method 0) for now
        if (compression_method != 0) {
            fprintf(stderr, "Warning: unsupported compression method %d for file %s, skipping\n", compression_method, filename);
            free(file_data);
            free(filename);
            continue;
        }

        // Create output file path
        char output_path[MAX_PATH];
        strcpy(output_path, filename);
        // Convert forward slashes to backslashes for Windows
        for (char *p = output_path; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        // Create directories if needed
        char *last_slash = strrchr(output_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            if (create_directory(output_path) != 0) {
                fprintf(stderr, "Failed to create directory: %s\n", output_path);
                *last_slash = '\\';
                free(file_data);
                free(filename);
                fclose(zip_file);
                return 1;
            }
            *last_slash = '\\';
        }

        // Write output file
        FILE *out_file = fopen(output_path, "wb");
        if (!out_file) {
            perror("Error creating output file");
            free(file_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }
        if (fwrite(file_data, 1, compressed_size, out_file) != compressed_size) {
            perror("Error writing output file");
            fclose(out_file);
            free(file_data);
            free(filename);
            fclose(zip_file);
            return 1;
        }
        fclose(out_file);
        printf("  Extracted %s to %s\n", filename, output_path);

        // Free file data
        free(file_data);
        free(filename);

        entry_count++;
        // Process only first entry for debugging
        if (entry_count >= 1) {
            printf("Processed first entry, stopping for debugging\n");
            break;
        }
    }

    fclose(zip_file);
    printf("\nDebug extraction complete.\n");
    return 0;
}