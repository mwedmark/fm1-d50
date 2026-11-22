/* Checks app/d50/d50_math.c against the WASI libm: the largest relative error of each function over its range. */
#define D50_MATH_PREFIX(name) d50_##name
#include "../../d50/d50_math.c"
#include <stdio.h>
static double worst(const char *name, float (*a)(float), float (*b)(float), float lo, float hi, int abs_err)
{
    double w = 0; float at = 0;
    for (int i = 0; i <= 200000; ++i) {
        float x = lo + (hi - lo) * (float)i / 200000.0f;
        double r = b(x), v = a(x), e = abs_err ? fabs(v - r) : (r != 0 ? fabs((v - r) / r) : fabs(v));
        if (e > w) { w = e; at = x; }
    }
    printf("%-8s [%g, %g] %s error %.3g at %g\n", name, lo, hi, abs_err ? "abs" : "rel", w, at);
    return w;
}
static float p_a(float x) { return d50_powf(1.7f, x); }
static float p_b(float x) { return powf(1.7f, x); }
static float p2_a(float x) { return d50_powf(x, 0.37f); }
static float p2_b(float x) { return powf(x, 0.37f); }
int main(void)
{
    int bad = 0;
    bad |= worst("exp2f", d50_exp2f, exp2f, -60, 60, 0) > 1e-6;
    bad |= worst("log10f", d50_log10f, log10f, 1e-6f, 1e6f, 0) > 1e-6;
    bad |= worst("log10f", d50_log10f, log10f, 0.5f, 2.0f, 1) > 1e-6;
    bad |= worst("powf", p_a, p_b, -40, 40, 0) > 3e-6;   /* the log2 error times an exponent of 30 */
    bad |= worst("powf", p2_a, p2_b, 1e-4f, 1e4f, 0) > 2e-6;
    bad |= worst("sinf", d50_sinf, sinf, -100, 100, 1) > 2e-6;
    bad |= worst("cosf", d50_cosf, cosf, -100, 100, 1) > 2e-6;
    bad |= worst("tanf", d50_tanf, tanf, -1.5f, 1.5f, 0) > 5e-6;
    printf("pow(-2,3)=%g pow(0,2)=%g pow(2,0)=%g\n", d50_powf(-2, 3), d50_powf(0, 2), d50_powf(2, 0));
    printf(bad ? "FAIL\n" : "pass\n");
    return bad;
}
