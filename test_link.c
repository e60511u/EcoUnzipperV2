#include <stdio.h>
int main() {
    fseeko64(NULL, 0, 0);
    ftello64(NULL);
    return 0;
}