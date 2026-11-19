/* Checks app/d50/d50_inflate.c against zlib: tools/d50_host/test_inflate.js writes deflate streams (stored, fixed, dynamic blocks, the
 * real bank) beside their raw bytes, this unpacks each and compares. */
#include "../../fm1_open/app/d50/d50_inflate.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *load(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 1); fread(b, 1, *n, f); fclose(f); return b;
}
int main(int argc, char **argv)
{
    int bad = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        long nz, nr; unsigned char *z = load(argv[i], &nz), *r = load(argv[i + 1], &nr);
        if (!z || !r) { printf("missing %s\n", argv[i]); return 2; }
        unsigned char *out = malloc(nr + 16);
        int got = d50_inflate(out, (unsigned)nr, z, (unsigned)nz);
        int ok = got == nr && !memcmp(out, r, nr);
        int cut = d50_inflate(out, (unsigned)nr, z, (unsigned)nz / 2) == -1;   /* half the stream must fail, not run off */
        printf("%-40s %ld -> %d %s%s\n", argv[i], nz, got, ok ? "ok" : "DIFFERS", cut ? "" : ", a cut stream did not fail");
        bad |= !ok || !cut;
    }
    printf(bad ? "FAIL\n" : "pass\n");
    return bad;
}
