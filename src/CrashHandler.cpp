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

// Write the crash report to a single stream (stderr or the log file). The
// offsets printed here (off=0x...) are module-relative and can be symbolized
// offline against the same Realmz.exe with llvm-symbolizer / addr2line.
static void write_crash_report(FILE* out, EXCEPTION_POINTERS* info, void* base) {
  if (!out) {
    return;
  }
  fprintf(out, "\n=== REALMZ CRASH ===\n");
  if (info && info->ExceptionRecord) {
    EXCEPTION_RECORD* er = info->ExceptionRecord;
    fprintf(out, "exception code=0x%08lx faulting_addr=%p imagebase=%p (off=0x%llx)\n",
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
      fprintf(out, "access=%s data_addr=0x%llx\n", kind,
          static_cast<unsigned long long>(er->ExceptionInformation[1]));
    }
  }
  void* frames[64];
  USHORT n = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
  for (USHORT i = 0; i < n; i++) {
    fprintf(out, "BT %2u %p off=0x%llx\n", i, frames[i],
        static_cast<unsigned long long>(
            reinterpret_cast<char*>(frames[i]) - reinterpret_cast<char*>(base)));
  }
  fprintf(out, "=== END CRASH ===\n");
  fflush(out);
}

// Open RealmzCrash.log next to the executable (appending, so repeated crashes
// accumulate). The console window is hidden in release builds, so the log file
// is how a crash backtrace reaches the user. Returns nullptr if the path can't
// be built or the file can't be opened.
static FILE* open_crash_log(void) {
  wchar_t path[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return _wfopen(L"RealmzCrash.log", L"a"); // fall back to the working directory
  }
  // Trim the executable name, keeping the trailing separator.
  DWORD i = len;
  while (i > 0 && path[i - 1] != L'\\' && path[i - 1] != L'/') {
    i--;
  }
  path[i] = L'\0';
  static const wchar_t kName[] = L"RealmzCrash.log";
  if (i + (sizeof(kName) / sizeof(kName[0])) > MAX_PATH) {
    return _wfopen(L"RealmzCrash.log", L"a");
  }
  wcscat(path, kName);
  FILE* f = _wfopen(path, L"a");
  return f ? f : _wfopen(L"RealmzCrash.log", L"a");
}

static LONG WINAPI realmz_crash_filter(EXCEPTION_POINTERS* info) {
  void* base = reinterpret_cast<void*>(GetModuleHandleW(nullptr));
  write_crash_report(stderr, info, base);
  FILE* log = open_crash_log();
  if (log) {
    write_crash_report(log, info, base);
    fclose(log);
  }
  return EXCEPTION_EXECUTE_HANDLER; // let the process terminate
}

extern "C" void InstallCrashHandler(void) {
  SetUnhandledExceptionFilter(realmz_crash_filter);
}

// Hide the console window that the console-subsystem build opens on launch. The console stays
// allocated so stdout/stderr remain valid for logging (the game logs heavily during startup); only
// the window is hidden. Building with -DREALMZ_DEBUG_CONSOLE=ON makes this a no-op so the console
// window is visible for debugging. This lives here rather than in main.c because main.c pulls in
// the classic Mac toolbox headers, whose own ShowWindow(WindowPtr) collides with the Win32 API.
extern "C" void RealmzHideConsoleWindow(void) {
#ifndef REALMZ_DEBUG_CONSOLE
  HWND console = GetConsoleWindow(); // kernel32; safe to call directly
  if (!console) {
    return;
  }
  // ShowWindow lives in user32, whose import library also defines GDI helpers (OffsetRect, etc.)
  // that collide with this project's classic Mac toolbox reimplementations. Resolve it at runtime
  // instead of linking user32 so those symbols never clash.
  HMODULE user32 = LoadLibraryA("user32.dll");
  if (!user32) {
    return;
  }
  typedef BOOL(WINAPI * ShowWindowFn)(HWND, int);
  ShowWindowFn show_window = reinterpret_cast<ShowWindowFn>(GetProcAddress(user32, "ShowWindow"));
  if (show_window) {
    show_window(console, SW_HIDE);
  }
  // user32.dll stays loaded; it is a core DLL that remains resident for the process lifetime.
#endif
}

#else // non-Windows builds rely on AddressSanitizer instead

extern "C" void InstallCrashHandler(void) {}
extern "C" void RealmzHideConsoleWindow(void) {}

#endif
