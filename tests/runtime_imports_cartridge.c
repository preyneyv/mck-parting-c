#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <prism/sdk.h>

static const uint8_t runtime_imports_icon[PRISM_CARTRIDGE_ICON_BYTES] = {0};

typedef struct
{
  uint32_t words[8];
} copy_block_t;

extern int32_t __aeabi_idiv(int32_t, int32_t);
extern uint32_t __aeabi_uidiv(uint32_t, uint32_t);
extern uint64_t __aeabi_lmul(uint64_t, uint64_t);
extern int __clzsi2(uint32_t);
extern int __clzdi2(uint64_t);
extern int __ctzsi2(uint32_t);
extern int __ctzdi2(uint64_t);
extern int __popcountsi2(uint32_t);
extern int __popcountdi2(uint64_t);
extern void __aeabi_memcpy(void *, const void *, size_t);
extern void __aeabi_memcpy4(void *, const void *, size_t);
extern void __aeabi_memcpy8(void *, const void *, size_t);
extern void __aeabi_memset(void *, size_t, int);
extern void __aeabi_memset4(void *, size_t, int);
extern void __aeabi_memset8(void *, size_t, int);

extern float __aeabi_fadd(float, float);
extern float __aeabi_fdiv(float, float);
extern float __aeabi_fmul(float, float);
extern float __aeabi_frsub(float, float);
extern float __aeabi_fsub(float, float);
extern void __aeabi_cfcmpeq(float, float);
extern void __aeabi_cfrcmple(float, float);
extern void __aeabi_cfcmple(float, float);
extern int __aeabi_fcmpeq(float, float);
extern int __aeabi_fcmplt(float, float);
extern int __aeabi_fcmple(float, float);
extern int __aeabi_fcmpge(float, float);
extern int __aeabi_fcmpgt(float, float);
extern int __aeabi_fcmpun(float, float);
extern float __aeabi_i2f(int32_t);
extern float __aeabi_ui2f(uint32_t);
extern int32_t __aeabi_f2iz(float);
extern uint32_t __aeabi_f2uiz(float);
extern float __aeabi_l2f(int64_t);
extern float __aeabi_ul2f(uint64_t);
extern int64_t __aeabi_f2lz(float);
extern uint64_t __aeabi_f2ulz(float);
extern double __aeabi_f2d(float);

extern double __aeabi_dadd(double, double);
extern double __aeabi_ddiv(double, double);
extern double __aeabi_dmul(double, double);
extern double __aeabi_drsub(double, double);
extern double __aeabi_dsub(double, double);
extern void __aeabi_cdcmpeq(double, double);
extern void __aeabi_cdrcmple(double, double);
extern void __aeabi_cdcmple(double, double);
extern int __aeabi_dcmpeq(double, double);
extern int __aeabi_dcmplt(double, double);
extern int __aeabi_dcmple(double, double);
extern int __aeabi_dcmpge(double, double);
extern int __aeabi_dcmpgt(double, double);
extern int __aeabi_dcmpun(double, double);
extern double __aeabi_i2d(int32_t);
extern double __aeabi_l2d(int64_t);
extern double __aeabi_ui2d(uint32_t);
extern double __aeabi_ul2d(uint64_t);
extern int32_t __aeabi_d2iz(double);
extern int64_t __aeabi_d2lz(double);
extern uint32_t __aeabi_d2uiz(double);
extern uint64_t __aeabi_d2ulz(double);
extern float __aeabi_d2f(double);

typedef void (*float_flag_compare_fn)(float, float);
typedef void (*double_flag_compare_fn)(double, double);

static uint32_t float_compare_flags(float_flag_compare_fn compare,
                                    float left, float right)
{
  compare(left, right);
  uint32_t flags;
  __asm volatile("mrs %0, apsr" : "=r"(flags) : : "cc");
  return flags;
}

static uint32_t double_compare_flags(double_flag_compare_fn compare,
                                     double left, double right)
{
  compare(left, right);
  uint32_t flags;
  __asm volatile("mrs %0, apsr" : "=r"(flags) : : "cc");
  return flags;
}

static int runtime_vprintf(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  int result = vprintf(format, arguments);
  va_end(arguments);
  return result;
}

static int runtime_vsnprintf(char *destination, size_t capacity,
                             const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(destination, capacity, format, arguments);
  va_end(arguments);
  return result;
}

static bool runtime_imports_pass(void)
{
  volatile int32_t s32a = -100, s32b = 7;
  volatile uint32_t u32a = 100, u32b = 7;
  volatile int64_t s64a = INT64_C(-10000000000), s64b = INT64_C(3000000000);
  volatile uint64_t u64a = UINT64_C(10000000000), u64b = UINT64_C(3000000000);
  if (s32a / s32b != -14 || s32a % s32b != -2 ||
      u32a / u32b != 14 || u32a % u32b != 2 ||
      s64a / s64b != -3 || s64a % s64b != INT64_C(-1000000000) ||
      u64a / u64b != 3 || u64a % u64b != UINT64_C(1000000000))
    return false;
  if (__aeabi_idiv(-100, 7) != -14 || __aeabi_uidiv(100, 7) != 14)
    return false;

  volatile int64_t mul_a = INT64_C(-1234567);
  volatile int64_t mul_b = INT64_C(7654321);
  if (mul_a * mul_b != INT64_C(-9449772114007))
    return false;
  if (__aeabi_lmul(UINT64_C(0x100000001), 3) != UINT64_C(0x300000003))
    return false;

  volatile uint32_t bits32 = 0x00100100u;
  volatile uint64_t bits64 = UINT64_C(0x0000001000000100);
  if (__builtin_clz(bits32) != 11 || __builtin_ctz(bits32) != 8 ||
      __builtin_popcount(bits32) != 2 || __builtin_clzll(bits64) != 27 ||
      __builtin_ctzll(bits64) != 8 || __builtin_popcountll(bits64) != 2)
    return false;
  if (__clzsi2(bits32) != 11 || __ctzsi2(bits32) != 8 ||
      __popcountsi2(bits32) != 2 || __clzdi2(bits64) != 27 ||
      __ctzdi2(bits64) != 8 || __popcountdi2(bits64) != 2)
    return false;

  copy_block_t source = {{1, 2, 3, 4, 5, 6, 7, 8}};
  copy_block_t destination;
  memset(&destination, 0, sizeof(destination));
  memcpy(&destination, &source, sizeof(destination));
  if (memcmp(&destination, &source, sizeof(destination)) != 0)
    return false;
  __aeabi_memset(&destination, sizeof(destination), 0x11);
  __aeabi_memcpy(&destination, &source, sizeof(destination));
  if (memcmp(&destination, &source, sizeof(destination)) != 0)
    return false;
  __aeabi_memset4(&destination, sizeof(destination), 0x22);
  __aeabi_memcpy4(&destination, &source, sizeof(destination));
  if (memcmp(&destination, &source, sizeof(destination)) != 0)
    return false;
  __aeabi_memset8(&destination, sizeof(destination), 0x33);
  __aeabi_memcpy8(&destination, &source, sizeof(destination));
  if (memcmp(&destination, &source, sizeof(destination)) != 0)
    return false;

  volatile float fa = 6.0f, fb = 4.0f;
  float fsum = fa + fb;
  float fdifference = fa - fb;
  float fproduct = fa * fb;
  float fquotient = fa / fb;
  if (fsum != 10.0f || fdifference != 2.0f || fproduct != 24.0f ||
      fquotient != 1.5f || !(fa > fb) || (int32_t)fa != 6 ||
      (uint32_t)fb != 4)
    return false;
  if (__aeabi_fadd(fa, fb) != 10.0f || __aeabi_fsub(fa, fb) != 2.0f ||
      __aeabi_frsub(fa, fb) != -2.0f || __aeabi_fmul(fa, fb) != 24.0f ||
      __aeabi_fdiv(fa, fb) != 1.5f || !__aeabi_fcmpeq(fa, fa) ||
      !__aeabi_fcmplt(fb, fa) || !__aeabi_fcmple(fb, fa) ||
      !__aeabi_fcmpge(fa, fb) || !__aeabi_fcmpgt(fa, fb) ||
      __aeabi_fcmpun(fa, fb) || __aeabi_i2f(-6) != -6.0f ||
      __aeabi_ui2f(6) != 6.0f || __aeabi_f2iz(-6.0f) != -6 ||
      __aeabi_f2uiz(6.0f) != 6 || __aeabi_l2f(INT64_C(-6)) != -6.0f ||
      __aeabi_ul2f(UINT64_C(6)) != 6.0f ||
      __aeabi_f2lz(-6.0f) != INT64_C(-6) ||
      __aeabi_f2ulz(6.0f) != UINT64_C(6) || __aeabi_f2d(1.5f) != 1.5)
    return false;

  const uint32_t n = UINT32_C(1) << 31;
  const uint32_t z = UINT32_C(1) << 30;
  const uint32_t c = UINT32_C(1) << 29;
  if ((float_compare_flags(__aeabi_cfcmpeq, 1.0f, 1.0f) & (n | z | c)) !=
          (z | c) ||
      (float_compare_flags(__aeabi_cfcmple, 1.0f, 2.0f) & (n | z | c)) != n)
    return false;
  (void)float_compare_flags(__aeabi_cfrcmple, 2.0f, 1.0f);

  volatile double da = 6.0, db = 4.0;
  double dsum = da + db;
  double ddifference = da - db;
  double dproduct = da * db;
  double dquotient = da / db;
  if (dsum != 10.0 || ddifference != 2.0 || dproduct != 24.0 ||
      dquotient != 1.5 || !(da > db) || (int32_t)da != 6 ||
      (uint32_t)db != 4 || (float)dquotient != 1.5f)
    return false;
  if (__aeabi_dadd(da, db) != 10.0 || __aeabi_dsub(da, db) != 2.0 ||
      __aeabi_drsub(da, db) != -2.0 || __aeabi_dmul(da, db) != 24.0 ||
      __aeabi_ddiv(da, db) != 1.5 || !__aeabi_dcmpeq(da, da) ||
      !__aeabi_dcmplt(db, da) || !__aeabi_dcmple(db, da) ||
      !__aeabi_dcmpge(da, db) || !__aeabi_dcmpgt(da, db) ||
      __aeabi_dcmpun(da, db) || __aeabi_i2d(-6) != -6.0 ||
      __aeabi_ui2d(6) != 6.0 || __aeabi_l2d(INT64_C(-6)) != -6.0 ||
      __aeabi_ul2d(UINT64_C(6)) != 6.0 || __aeabi_d2iz(-6.0) != -6 ||
      __aeabi_d2uiz(6.0) != 6 || __aeabi_d2lz(-6.0) != INT64_C(-6) ||
      __aeabi_d2ulz(6.0) != UINT64_C(6) || __aeabi_d2f(1.5) != 1.5f)
    return false;
  if ((double_compare_flags(__aeabi_cdcmpeq, 1.0, 1.0) & (n | z | c)) !=
          (z | c) ||
      (double_compare_flags(__aeabi_cdcmple, 1.0, 2.0) & (n | z | c)) != n)
    return false;
  (void)double_compare_flags(__aeabi_cdrcmple, 2.0, 1.0);

  char formatted[64];
  if (sprintf(formatted, "%s:%d:%x", "sprintf", -7, 0x2a) != 13 ||
      strcmp(formatted, "sprintf:-7:2a") != 0 ||
      snprintf(formatted, sizeof(formatted), "%s:%u", "snprintf", 9u) !=
          10 ||
      strcmp(formatted, "snprintf:9") != 0 ||
      runtime_vsnprintf(formatted, sizeof(formatted), "%s:%d", "vsnprintf",
                        -5) != 12 ||
      strcmp(formatted, "vsnprintf:-5") != 0)
    return false;
  if (snprintf(formatted, sizeof(formatted), "%lld:%.1f",
               INT64_C(1234567890123), 1.5) != 17 ||
      strcmp(formatted, "1234567890123:1.5") != 0 ||
      runtime_vsnprintf(formatted, sizeof(formatted), "%llu:%.1f",
                        UINT64_C(9876543210), 2.5) != 14 ||
      strcmp(formatted, "9876543210:2.5") != 0)
    return false;
  if (printf("runtime printf: %d\n", 7) <= 0 ||
      runtime_vprintf("runtime vprintf: %s\n", "ok") <= 0 ||
      puts("runtime puts: ok") < 0 || putchar('>') != '>')
    return false;
  putchar('\n');

  return true;
}

static void enter(prism_t *prism)
{
  if (runtime_imports_pass())
    prism_system_reset(prism);
}

static void frame(prism_t *prism)
{
  u8g2_DrawPixel(prism_display(prism), 0, 0);
}

PRISM_CARTRIDGE(cartridge_runtime_imports,
    .id = "dev.preyneyv.prism.runtime-imports",
    .name = "runtime imports",
    .version = 1,
    .icon = runtime_imports_icon,
    .enter = enter,
    .frame = frame,
);
