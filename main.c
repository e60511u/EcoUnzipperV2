#include <stdio.h>
#include "common.h"
#include "extract.h"
#include "gui.h"

int main(int argc, char *argv[]) {
    if (is_console_attached() && argc > 1) {
        Options opts;
        const char *zip_path = NULL;

        if (parse_arguments(argc, argv, &opts, &zip_path) != 0) return 1;
        
        if (opts.password && strcmp(opts.password, "PROMPT") == 0) {
             static char pass_buf[MAX_PATH_LEN];
             printf("Enter password: ");
             // Simple echo-off emulation or use _getch if available (requires conio.h)
             // For now, simple gets_s or similar.
             fgets(pass_buf, MAX_PATH_LEN, stdin);
             TrimSpaces(pass_buf);
             opts.password = pass_buf;
        }

        if (opts.quiet && opts.verbose) { 
            fprintf(stderr, "Error: Cannot use both -v and -q\n"); 
            return 1; 
        }

        if (opts.list_only) return list_zip_contents(zip_path, &opts);

        int count = extract_zip(zip_path, &opts);
        
        printf("\nExtraction complete: %d files extracted\n", count);
        return 0;
    }

    ShowGUI();
    return 0;
}