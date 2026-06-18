// Minimal crash backtrace handler.
//
// The legacy combat code contains latent out-of-bounds / null-deref bugs that
// manifest as hard segfaults with no message. To diagnose them without a
// debugger, this installs a last-chance handler that prints the faulting
// address and a raw stack backtrace (as module-relative offsets) to stderr,
// which is flushed immediately (main() sets stderr unbuffered). The offsets can
// be symbolized offline against the built binary with llvm-symbolizer /
// addr2line, e.g.:
//
//   llvm-symbolizer --obj=Realmz.exe <offset> <offset> ...
//
// so the build needs debug info (-g) for readable results.

#include <cstdio>

#ifdef _WIN32
#include <windows.h>

static LONG WINAPI realmz_crash_filter(EXCEPTION_POINTERS* info) {
  void* base = reinterpret_cast<void*>(GetModuleHandleW(nullptr));
  fprintf(stderr, "\n=== REALMZ CRASH ===\n");
  if (info && info->ExceptionRecord) {
    EXCEPTION_RECORD* er = info->ExceptionRecord;
    fprintf(stderr, "exception code=0x%08lx faulting_addr=%p imagebase=%p (off=0x%llx)\n",
        er->ExceptionCode,
        reinterpret_cast<void*>(er->ExceptionAddress),
        base,
        static_cast<unsigned long long>(
            reinterpret_cast<char*>(er->ExceptionAddress) -
            reinterpret_cast<char*>(base)));
    // For an access violation, ExceptionInformation[0] is 0=read/1=write/8=DEP
    // and [1] is the data address that was being accessed. This is what actually
    // identifies the bad pointer/index.
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
      ULONG_PTR op = er->ExceptionInformation[0];
      const char* kind = (op == 1) ? "WRITE" : (op == 8) ? "EXEC" : "READ";
      fprintf(stderr, "access=%s data_addr=0x%llx\n", kind,
          static_cast<unsigned long long>(er->ExceptionInformation[1]));
    }
  }
  void* frames[64];
  USHORT n = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
  for (USHORT i = 0; i < n; i++) {
    fprintf(stderr, "BT %2u %p off=0x%llx\n", i, frames[i],
        static_cast<unsigned long long>(
            reinterpret_cast<char*>(frames[i]) - reinterpret_cast<char*>(base)));
  }
  fprintf(stderr, "=== END CRASH ===\n");
  fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER; // let the process terminate
}

extern "C" void InstallCrashHandler(void) {
  SetUnhandledExceptionFilter(realmz_crash_filter);
}

#else // non-Windows builds rely on AddressSanitizer instead

extern "C" void InstallCrashHandler(void) {}

#endif
