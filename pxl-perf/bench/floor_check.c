#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static float pxl__floor(float value) {
  if (!(fabsf(value) < 8388608.0f)) {
    return value;
  }
  float whole = (float)(int32_t)value;
  return whole > value ? whole - 1.0f : whole;
}
int main(void) {
  uint64_t bad = 0, signed_zero = 0;
  for (uint64_t i = 0; i <= 0xffffffffu; ++i) {
    uint32_t bits = (uint32_t)i;
    float x; memcpy(&x, &bits, 4);
    float a = pxl__floor(x), b = floorf(x);
    if (isnan(a) && isnan(b)) continue;
    if (a != b) { if (++bad < 5) printf("x=%a got %a want %a\n", x, a, b); }
    else if (signbit(a) != signbit(b)) ++signed_zero;
  }
  printf("mismatches: %llu, sign-of-zero differences: %llu\n", (unsigned long long)bad, (unsigned long long)signed_zero);
  return bad != 0;
}
