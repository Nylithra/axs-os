/* WTA testi: 32 ve 64 bit Windows konsol programı (CI'da mingw ile derlenir) */
#include <stdio.h>
#include <windows.h>

int main(void)
{
    OSVERSIONINFOA v = { sizeof v };
    GetVersionExA(&v);
    printf("WTA-%d-BIT-TAMAM (Windows %lu.%lu)\n", (int)(sizeof(void *) * 8), v.dwMajorVersion, v.dwMinorVersion);
    return 0;
}
