#include "CLIHandler.h"
#include "TestController.h"
#include "Logger.h"


//====================================================================================================
// ...existing code...
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <mutex>
#include <exception>

#pragma comment(lib, "dbghelp.lib")

static std::mutex g_log_mutex;
static HANDLE g_process = GetCurrentProcess();

static void write_log(const char* msg)
{
    std::lock_guard<std::mutex> lk(g_log_mutex);
    OutputDebugStringA(msg);
    // 파일에 즉시 기록 (앱이 곧 종료되어도 파일엔 남음)
    FILE* f = nullptr;
    fopen_s(&f, "C:\\temp\\abort_log.txt", "a");
    if (f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

static void print_stack_to_log()
{
    void* frames[62];
    WORD n = CaptureStackBackTrace(0, _countof(frames), frames, nullptr);
    SymInitialize(g_process, nullptr, TRUE);
    SYMBOL_INFO* sym = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    sym->MaxNameLen = 255;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);

    char buf[1024];
    for (WORD i = 0; i < n; ++i) {
        DWORD64 addr = (DWORD64)(frames[i]);
        if (SymFromAddr(g_process, addr, 0, sym)) {
            snprintf(buf, sizeof(buf), "%02u: %s - 0x%llx", i, sym->Name, sym->Address);
        } else {
            snprintf(buf, sizeof(buf), "%02u: (unknown) 0x%llx", i, addr);
        }
        write_log(buf);
    }
    free(sym);
}

static LONG WINAPI my_unhandled_exception_filter(PEXCEPTION_POINTERS ep)
{
    write_log("Unhandled SEH exception filter triggered");
    print_stack_to_log();

    // 가능한 경우 mini-dump 생성 (디스크에 남김)
    HANDLE fh = CreateFileA("C:\\temp\\crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), fh, MiniDumpWithDataSegs, &mei, nullptr, nullptr);
        CloseHandle(fh);
        write_log("MiniDump written to C:\\temp\\crash.dmp");
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI vectored_handler(PEXCEPTION_POINTERS ep)
{
    write_log("Vectored exception handler invoked");
    print_stack_to_log();
    // 다른 핸들러/디버거가 계속 처리하도록 함
    return EXCEPTION_CONTINUE_SEARCH;
}

static void sigabrt_handler(int)
{
    write_log("Caught SIGABRT / abort()");
    print_stack_to_log();
    // 종료하지 말고 디버거에서 조사할 경우를 위해 abort 유지
    std::abort();
}

static void purecall_handler()
{
    write_log("Caught pure virtual call");
    print_stack_to_log();
    std::abort();
}

static void invalid_param_handler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    write_log("Caught invalid parameter");
    print_stack_to_log();
    std::abort();
}

static void debug_terminate_handler()
{
    write_log("std::terminate called");
    print_stack_to_log();
    std::abort();
}
// ...existing code...
void install_debug_handlers()
{
    // 심볼 옵션
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);

    // SEH/벡터 핸들러 등록
    AddVectoredExceptionHandler(1, vectored_handler);
    SetUnhandledExceptionFilter(my_unhandled_exception_filter);

    // C++/CRT 레벨 핸들러
    std::set_terminate(debug_terminate_handler);
    std::signal(SIGABRT, sigabrt_handler);
    _set_purecall_handler(purecall_handler);
    _set_invalid_parameter_handler(invalid_param_handler);

    write_log("Debug handlers installed");
}
// ...existing code...
//====================================================================================================
/**
 * @brief The main entry point for the IPEFTC application.
 *
 * This function initializes the application, parses command-line arguments,
 * starts the appropriate test (client or server), and waits for the test
 * to complete before shutting down.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 * @return 0 on successful execution, non-zero otherwise.
 */
int main(int argc, char* argv[]) {

    install_debug_handlers();

    // Iterate through all command-line arguments.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            CLIHandler::printHelp();
            exit(0);
        }
    }

    // Start the asynchronous logger service.
    // Logger::start();
    Logger::log("Info: IPEFTC (IPerf Test Client/Server) application starting.");

    // Create the main controller for managing tests.
    TestController controller;
    // Create a command-line handler and link it with the controller.
    CLIHandler cli(controller);

    // Run the command-line handler to parse arguments and start the test.
    cli.run(argc, argv);

    // Wait for the test to complete, periodically calling the controller's update method.
    Logger::log("Info: Waiting for the test to complete...");
    auto testFuture = controller.getTestCompletionFuture();
    while (testFuture.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        controller.update();
    }

    Logger::log("Info: IPEFTC application finished.");
    // Stop the logger service, ensuring all messages are flushed.
    Logger::stop();
    
    // std::this_thread::sleep_for(std::chrono::seconds(10)); // ADDED DELAY for debug pipe communication
    std::cout << "=============== END ================\n"<< std::endl;
    return 0;
}
