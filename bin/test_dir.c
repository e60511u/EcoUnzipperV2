#include <windows.h>
#include <stdio.h>

int main() {
    BOOL result = CreateDirectory("test_dir", NULL);
    printf("Result: %d, Error: %d\n", result, GetLastError());
    return 0;
}