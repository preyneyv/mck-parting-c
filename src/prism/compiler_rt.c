/* Compiler helpers from the toolchain's prebuilt libgcc are not necessarily
 * position-independent even when the cartridge itself is compiled as PIC.
 * Keep local call sites PC-relative, then cross the cartridge boundary through
 * the normal GOT import mechanism. */
extern float prism_import_aeabi_fdiv(float numerator, float denominator);

float __aeabi_fdiv(float numerator, float denominator)
{
  return prism_import_aeabi_fdiv(numerator, denominator);
}
