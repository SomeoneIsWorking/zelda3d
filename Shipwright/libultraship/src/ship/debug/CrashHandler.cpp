#include <spdlog/spdlog.h>
#include "ship/utils/StringHelper.h"
#include "ship/debug/CrashHandler.h"
#include "ship/Context.h"

#ifdef _WIN32
#include <windows.h>
#include <DbgHelp.h>
#include <errhandlingapi.h>
#include <inttypes.h>
#include <excpt.h>

#pragma comment(lib, "Dbghelp.lib")
#endif

namespace Ship {

#define WRITE_VAR_LINE(handler, varName, varValue) \
    handler->AppendStr(varName);                   \
    handler->AppendLine(varValue);
#define WRITE_VAR(handler, varName, varValue) \
    handler->AppendStr(varName);              \
    handler->AppendStr(varValue);

#define WRITE_VAR_LINE_M(varName, varValue) \
    AppendStr(varName);                     \
    AppendLine(varValue);
#define WRITE_VAR_M(varName, varValue) \
    AppendStr(varName);                \
    AppendStr(varValue);

bool CrashHandler::CheckStrLen(const char* str) {
    if (strlen(str) + mOutBuffersize >= gMaxBufferSize) {
        return false;
    }
    return true;
}

void CrashHandler::AppendStrTrunc(const char* str) {
    while (mOutBuffersize < gMaxBufferSize - 1) {
        mOutBuffer[mOutBuffersize++] = *str++;
    }
    mOutBuffer[mOutBuffersize] = '\0';
}

void CrashHandler::AppendStr(const char* str) {
    if (!CheckStrLen(str)) {
        AppendStrTrunc(str);
        return;
    }

    while (*str != '\0') {
        mOutBuffer[mOutBuffersize++] = *str++;
    }
}

void CrashHandler::AppendLine(const char* str) {
    AppendStr(str);
    mOutBuffer[mOutBuffersize++] = '\n';
}

/**
 * @brief Prints common data relevant to the crash
 *
 * @param buffer
 */
void CrashHandler::PrintCommon() {
    if (mCallback != nullptr) {
        mCallback(mOutBuffer.get(), &mOutBuffersize);
    }

    SPDLOG_CRITICAL(mOutBuffer.get());
}

#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#if defined(__linux__)
void CrashHandler::PrintRegisters(ucontext_t* ctx) {
    char regbuffer[30];
    AppendLine("Registers:");
#if defined(__x86_64__)
    snprintf(regbuffer, std::size(regbuffer), "RAX: 0x%016llX", ctx->uc_mcontext.gregs[REG_RAX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RDI: 0x%016llX", ctx->uc_mcontext.gregs[REG_RDI]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RSI: 0x%016llX", ctx->uc_mcontext.gregs[REG_RSI]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RDX: 0x%016llX", ctx->uc_mcontext.gregs[REG_RDX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RCX: 0x%016llX", ctx->uc_mcontext.gregs[REG_RCX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R8:  0x%016llX", ctx->uc_mcontext.gregs[REG_R8]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R9:  0x%016llX", ctx->uc_mcontext.gregs[REG_R9]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R10: 0x%016llX", ctx->uc_mcontext.gregs[REG_R10]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R11: 0x%016llX", ctx->uc_mcontext.gregs[REG_R11]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RSP: 0x%016llX", ctx->uc_mcontext.gregs[REG_RSP]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RBX: 0x%016llX", ctx->uc_mcontext.gregs[REG_RBX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RBP: 0x%016llX", ctx->uc_mcontext.gregs[REG_RBP]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R12: 0x%016llX", ctx->uc_mcontext.gregs[REG_R12]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R13: 0x%016llX", ctx->uc_mcontext.gregs[REG_R13]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R14: 0x%016llX", ctx->uc_mcontext.gregs[REG_R14]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "R15: 0x%016llX", ctx->uc_mcontext.gregs[REG_R15]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "RIP: 0x%016llX", ctx->uc_mcontext.gregs[REG_RIP]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EFL: 0x%016llX", ctx->uc_mcontext.gregs[REG_EFL]);
    AppendLine(regbuffer);
#elif defined(__i386__)
    snprintf(regbuffer, std::size(regbuffer), "EDI: 0x%08lX", ctx->uc_mcontext.gregs[REG_EDI]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "ESI: 0x%08lX", ctx->uc_mcontext.gregs[REG_ESI]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EBP: 0x%08lX", ctx->uc_mcontext.gregs[REG_EBP]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "ESP: 0x%08lX", ctx->uc_mcontext.gregs[REG_ESP]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EBX: 0x%08lX", ctx->uc_mcontext.gregs[REG_EBX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EDX: 0x%08lX", ctx->uc_mcontext.gregs[REG_EDX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "ECX: 0x%08lX", ctx->uc_mcontext.gregs[REG_ECX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EAX: 0x%08lX", ctx->uc_mcontext.gregs[REG_EAX]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EIP: 0x%08lX", ctx->uc_mcontext.gregs[REG_EIP]);
    AppendLine(regbuffer);
    snprintf(regbuffer, std::size(regbuffer), "EFL: 0x%08lX", ctx->uc_mcontext.gregs[REG_EFL]);
    AppendLine(regbuffer);
#endif
}
#endif // __linux__ (PrintRegisters: glibc register dump; macOS uses backtrace only)

static void ErrorHandler(int sig, siginfo_t* sigInfo, void* data) {
    // Guard against re-entry: this handler itself allocates (backtrace_symbols, demangle, strings),
    // so if the crash corrupted the heap a nested fault would recurse forever / hang. On the second
    // entry, terminate immediately and async-signal-safely.
    static volatile sig_atomic_t sHandling = 0;
    if (sHandling) {
        _exit(128 + sig);
    }
    sHandling = 1;

    // zelda3d (#91): a crash that originates INSIDE malloc/free — e.g. a double-free during
    // window-close teardown — leaves glibc's malloc arena lock HELD. Everything below
    // (backtrace_symbols, demangling, std::string, the crash dialog) then mallocs and DEADLOCKS on
    // that lock, so the process hangs forever instead of dying. That deadlock is the window-close
    // hang. Guards:
    //   1) Dump an async-signal-safe backtrace (backtrace_symbols_fd — no malloc) to stderr NOW, so
    //      the trace is always captured even if the rich handler below wedges. stderr -> run.log.
    //   2) Arm a watchdog so a held malloc lock can never hang the process; and for SIGABRT (glibc's
    //      heap-corruption signal — arena lock is held) bail out immediately after the safe dump.
    {
        void* asBt[256];
        int asN = backtrace(asBt, 256);
        static const char hdr[] = "\n[zelda3d] FATAL signal — async-signal-safe backtrace:\n";
        (void)!write(STDERR_FILENO, hdr, sizeof(hdr) - 1);
        backtrace_symbols_fd(asBt, asN, STDERR_FILENO);
    }
    signal(SIGALRM, [](int s) { _exit(128 + s); });
    alarm(3); // hard cap: the malloc-using handler below can never hang the process more than 3s
    if (sig == SIGABRT) {
        _exit(128 + sig);
    }

    std::shared_ptr<CrashHandler> crashHandler = Context::GetRawInstance()->GetCrashHandler();
    char intToCharBuffer[16];

    std::array<void*, 4096> arr;
    constexpr size_t nMaxFrames = arr.size();
    size_t size = backtrace(arr.data(), nMaxFrames);
    char** symbols = backtrace_symbols(arr.data(), nMaxFrames);

    snprintf(intToCharBuffer, sizeof(intToCharBuffer), "Signal: %i", sig);
    crashHandler->AppendLine(intToCharBuffer);

    switch (sig) {
        case SIGILL:
            crashHandler->AppendLine("ILLEGAL INSTRUCTION");
            break;
        case SIGABRT:
            crashHandler->AppendLine("ABORT");
            break;
        case SIGFPE:
            crashHandler->AppendLine("ERRONEUS ARITHEMETIC OPERATION");
            break;
        case SIGSEGV:
            crashHandler->AppendLine("INVALID ACCESS TO STORAGE");
            break;
    }

    // macOS deprecates ucontext and its signal-handler register state is unreliable, so we skip the
    // register dump there; the symbolized backtrace below is the actionable part. Linux dumps registers.
#if defined(__linux__)
    crashHandler->PrintRegisters(static_cast<ucontext_t*>(data));
#else
    (void)data;
#endif

    crashHandler->AppendLine("Traceback:");
    for (size_t i = 1; i < size; i++) {
        Dl_info info;
        int gotAddress = dladdr(arr[i], &info);
        std::string functionName(symbols[i]);

        if (gotAddress != 0 && info.dli_sname != nullptr) {
            int32_t status;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            const char* nameFound = info.dli_sname;

            if (status == 0) {
                nameFound = demangled;
            }

            functionName = StringHelper::Sprintf("%s (+0x%X)", nameFound, (char*)arr[i] - (char*)info.dli_saddr);
            free(demangled);
        }
        snprintf(intToCharBuffer, sizeof(intToCharBuffer), "%i ", (int)i);
        WRITE_VAR_LINE(crashHandler, intToCharBuffer, functionName.c_str());
    }
    // No GUI crash dialog: it pops a blocking zenity window on the user's desktop (the dev
    // workflow relaunches constantly), and the crash log is already written to stderr/the log
    // file + printed below. Re-enable by setting SOH_CRASH_DIALOG=1 if a popup is ever wanted.
    const char* crashDialog = getenv("SOH_CRASH_DIALOG");
    if (crashDialog != nullptr && crashDialog[0] == '1') {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, (Context::GetRawInstance()->GetName() + " has crashed").c_str(),
                                 (Context::GetRawInstance()->GetName() +
                                  " has crashed. Please upload the logs to the support channel in discord.")
                                     .c_str(),
                                 nullptr);
    }
    free(symbols);
    crashHandler->PrintCommon();

    Context::GetRawInstance()->GetLogger()->flush();
    // Use _exit, NOT exit(): the crash frequently fires DURING normal teardown (~Context) with the
    // heap already corrupted and the malloc lock held (e.g. the window-close Vulkan/Wayland swapchain
    // teardown double-free). exit() would re-run atexit handlers and global destructors against that
    // heap and deadlock — leaving the process running forever after the window is closed. _exit
    // terminates immediately without touching the heap or re-entering teardown. spdlog::shutdown()
    // is skipped for the same reason; the flush above is best-effort and the crash text is already
    // on stderr.
    _exit(1);
}

static void ShutdownHandler(int sig, siginfo_t* sigInfo, void* data) {
    exit(1);
}

#elif _WIN32

#if defined(_WIN32) && !defined(_WIN64)
#define WINDOWS_32_BIT
#endif

void CrashHandler::PrintRegisters(CONTEXT* ctx) {
    AppendLine("Registers:");
    char regBuff[29];
#if defined(_M_AMD64)
    sprintf_s(regBuff, std::size(regBuff), "    RAX: 0x%016llX", ctx->Rax);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RCX: 0x%016llX", ctx->Rcx);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RDX: 0x%016llX", ctx->Rdx);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RBX: 0x%016llX", ctx->Rbx);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RSP: 0x%016llX", ctx->Rsp);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RBP: 0x%016llX", ctx->Rbp);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RSI: 0x%016llX", ctx->Rsi);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RDI: 0x%016llX", ctx->Rdi);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R9:  0x%016llX", ctx->R9);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R10: 0x%016llX", ctx->R10);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R11: 0x%016llX", ctx->R11);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R12: 0x%016llX", ctx->R12);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R13: 0x%016llX", ctx->R13);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R14: 0x%016llX", ctx->R14);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    R15: 0x%016llX", ctx->R15);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    RIP: 0x%016llX", ctx->Rip);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    EFLAGS: 0x%08lX", ctx->EFlags);
    AppendLine(regBuff);
#elif defined(WINDOWS_32_BIT)
    sprintf_s(regBuff, std::size(regBuff), "    EDI: 0x%08lX", ctx->Edi);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    ESI: 0x%08lX", ctx->Esi);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    EBX: 0x%08lX", ctx->Ebx);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    ECX: 0x%08lX", ctx->Ecx);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    EAX: 0x%08lX", ctx->Eax);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    EBP: 0x%08lX", ctx->Ebp);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    ESP: 0x%08lX", ctx->Esp);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    EFLAGS: 0x%08lX", ctx->EFlags);
    AppendLine(regBuff);

    sprintf_s(regBuff, std::size(regBuff), "    EIP: 0x%08lX", ctx->Eip);
    AppendLine(regBuff);
#endif
}

void CrashHandler::PrintStack(CONTEXT* ctx) {
// Some crash hander features are not supported on ARM64
#ifndef _M_ARM64
    BOOL result;
    HANDLE process;
    HANDLE thread;
    HMODULE hModule;
    ULONG frame;
    DWORD64 displacement;
    DWORD disp;

#if defined(_M_AMD64)
    STACKFRAME64 stack;
    memset(&stack, 0, sizeof(STACKFRAME64));
#elif defined(WINDOWS_32_BIT)
    STACKFRAME stack;
    memset(&stack, 0, sizeof(STACKFRAME));
    stack.AddrPC.Offset = (*ctx).Eip;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Offset = (*ctx).Esp;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = (*ctx).Ebp;
    stack.AddrFrame.Mode = AddrModeFlat;
#endif

    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(TCHAR)];
    char module[512];

    PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;

    CONTEXT ctx2;
    memcpy(&ctx2, ctx, sizeof(CONTEXT));

    PrintRegisters(&ctx2);

    process = GetCurrentProcess();
    thread = GetCurrentThread();

    SymSetOptions(SYMOPT_NO_IMAGE_SEARCH | SYMOPT_IGNORE_IMAGEDIR);
    SymInitialize(process, "debug", true);

    constexpr DWORD machineType =
#if defined(_M_AMD64)
        IMAGE_FILE_MACHINE_AMD64;
#elif defined(WINDOWS_32_BIT)
        IMAGE_FILE_MACHINE_I386;
#endif

    AppendLine("Traceback:");

    displacement = 0;
    for (frame = 0;; frame++) {
        result = StackWalk(machineType, process, thread, &stack, &ctx2, nullptr, SymFunctionTableAccess,
                           SymGetModuleBase, nullptr);
        if (!result) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        SymFromAddr(process, (ULONG64)stack.AddrPC.Offset, &displacement, symbol);
#if defined(_M_AMD64)
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
#elif defined(WINDOWS_32_BIT)
        IMAGEHLP_LINE line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
#endif
        if (SymGetLineFromAddr(process, stack.AddrPC.Offset, &disp, &line)) {
            AppendStr("    ");
            AppendStr(symbol->Name);
            AppendStr(" in ");
            AppendStr(line.FileName);
            char lineNumberStr[16];
            sprintf_s(lineNumberStr, sizeof(lineNumberStr), " Line: %d", line.LineNumber);
            AppendLine(lineNumberStr);
        } else {
            WRITE_VAR_M("    ", symbol->Name);
            char addrString[20];
            sprintf_s(addrString, std::size(addrString), "0x%016llX", symbol->Address);
            WRITE_VAR_M("(", addrString);
            AppendStr(")");
            hModule = nullptr;
            GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCTSTR)(stack.AddrPC.Offset), &hModule);

            if (hModule != nullptr) {
                GetModuleFileNameA(hModule, module, sizeof(module));
                WRITE_VAR_LINE_M(" in ", module);
            } else {
                WRITE_VAR_LINE_M(" in ", "???");
            }
        }
    }
    PrintCommon();
    Context::GetRawInstance()->GetLogger()->flush();
    spdlog::shutdown();
#endif
}

extern "C" LONG WINAPI seh_filter(PEXCEPTION_POINTERS ex) {
    char exceptionString[20];
    std::shared_ptr<CrashHandler> crashHandler = Context::GetRawInstance()->GetCrashHandler();

    snprintf(exceptionString, std::size(exceptionString), "0x%x", ex->ExceptionRecord->ExceptionCode);

    WRITE_VAR_LINE(crashHandler, "Exception: ", exceptionString);
    crashHandler->PrintStack(ex->ContextRecord);
    MessageBoxA(nullptr,
                (Context::GetRawInstance()->GetName() +
                 " has crashed. Please upload the logs to the support channel in discord.")
                    .c_str(),
                "Crash", MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

// True when this translation unit was built with AddressSanitizer. GCC defines the macro; clang
// answers through __has_feature. Both are compile-time, so a release build is byte-identical to
// before.
#if defined(__SANITIZE_ADDRESS__)
#define ZELDA3D_BUILT_WITH_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ZELDA3D_BUILT_WITH_ASAN 1
#endif
#endif

CrashHandler::CrashHandler() : mOutBuffer(std::make_unique<char[]>(gMaxBufferSize)) {
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
    struct sigaction action = { 0 };
    struct sigaction shutdownAction = { 0 };

    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = ErrorHandler;

#if defined(ZELDA3D_BUILT_WITH_ASAN)
    // Do NOT take the fault signals on a sanitizer build. AddressSanitizer installs its own SIGSEGV
    // handler and turns a fault into a report that names the address, the allocation it belongs to
    // and the shadow state around it. Ours gets there first, prints a symbol-only backtrace and
    // _exit()s, so the sanitizer report is never produced -- which is how issue 0022 came back with
    // nothing but ten frame addresses on the one build that could have explained it.
    //
    // This is a compile-time choice on a build that exists to be diagnosed, not a runtime toggle:
    // the release build installs exactly what it always did. Said out loud at startup, because a
    // sanitizer run with no crash-handler output would otherwise look like the handler failing.
    fprintf(stderr, "[zelda3d] CrashHandler: sanitizer build -- leaving SIGILL/SIGABRT/SIGFPE/SIGSEGV to "
                    "AddressSanitizer so its report is not pre-empted. Shutdown signals still handled.\n");
#else
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
#endif

    shutdownAction.sa_flags = SA_SIGINFO;
    shutdownAction.sa_sigaction = ShutdownHandler;
    sigaction(SIGINT, &shutdownAction, nullptr);
    sigaction(SIGTERM, &shutdownAction, nullptr);
    sigaction(SIGQUIT, &shutdownAction, nullptr);
    sigaction(SIGKILL, &shutdownAction, nullptr);
#elif defined(_WIN32)
    SetUnhandledExceptionFilter(seh_filter);
#endif
}

CrashHandler::CrashHandler(CrashHandlerCallback callback) : CrashHandler() {
    mCallback = callback;
}

CrashHandler::~CrashHandler() {
    SPDLOG_TRACE("destruct crash handler");
}

void CrashHandler::RegisterCallback(CrashHandlerCallback callback) {
    mCallback = callback;
}
} // namespace Ship
