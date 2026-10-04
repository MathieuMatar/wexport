#include "ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

#include "../Util/Strings.h"
#include "ExporterCommand.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ck {

#ifdef _WIN32

namespace {

struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { Close(); }
    void Close() {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = nullptr;
    }
};

std::wstring BuildEnvironmentBlock(const std::vector<std::pair<std::string, std::string>>& extra) {
    std::wstring block;
    LPWCH env = GetEnvironmentStringsW();
    for (LPWCH p = env; *p; p += wcslen(p) + 1) {
        std::wstring entry = p;
        bool overridden = false;
        for (const auto& [k, v] : extra) {
            std::wstring key = ToWide(k) + L"=";
            if (_wcsnicmp(entry.c_str(), key.c_str(), key.size()) == 0) overridden = true;
        }
        if (!overridden) block += entry + L'\0';
    }
    FreeEnvironmentStringsW(env);
    for (const auto& [k, v] : extra) block += ToWide(k) + L"=" + ToWide(v) + L'\0';
    block += L'\0';
    return block;
}

}  // namespace

ProcessResult RunProcess(const ProcessSpec& spec, const CancelToken& cancel,
                         const std::function<void(std::string_view)>& onOutput) {
    ProcessResult result;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0) || !CreatePipe(&errRead, &errWrite, &sa, 0)) {
        result.startFailed = true;
        result.startError = "CreatePipe failed";
        return result;
    }
    Handle hOutRead(outRead), hOutWrite(outWrite), hErrRead(errRead), hErrWrite(errWrite);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    Handle nul(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));

    // A job object so Cancel kills wtsexporter and anything it spawned.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof limits);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul.h;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;

    std::wstring cmd = ToWide(JoinWindowsCommandLine(PathToUtf8(spec.executable), spec.args));
    std::wstring env = BuildEnvironmentBlock(spec.extraEnvironment);
    std::wstring cwd = spec.workingDirectory.native();
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(spec.executable.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, env.data(),
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    // Overwrite our copy of the command line: it holds the key.
    std::fill(cmd.begin(), cmd.end(), L'\0');
    if (!ok) {
        result.startFailed = true;
        result.startError = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    Handle process(pi.hProcess), thread(pi.hThread);
    AssignProcessToJobObject(job.h, pi.hProcess);
    ResumeThread(pi.hThread);
    hOutWrite.Close();
    hErrWrite.Close();

    std::mutex outputMutex;
    auto reader = [&](HANDLE pipe) {
        char buf[8192];
        DWORD n = 0;
        while (ReadFile(pipe, buf, sizeof buf, &n, nullptr) && n > 0) {
            std::lock_guard lock(outputMutex);
            if (onOutput) onOutput(std::string_view(buf, n));
        }
    };
    std::thread outThread(reader, outRead), errThread(reader, errRead);

    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (cancel.IsCancelled()) {
            TerminateJobObject(job.h, 1);
            result.cancelled = true;
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
    }
    outThread.join();
    errThread.join();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    return result;
}

#else

ProcessResult RunProcess(const ProcessSpec& spec, const CancelToken& cancel,
                         const std::function<void(std::string_view)>& onOutput) {
    ProcessResult result;
    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        result.startFailed = true;
        result.startError = "pipe failed";
        return result;
    }
    std::vector<std::string> argStore;
    argStore.push_back(spec.executable.string());
    for (const auto& a : spec.args) argStore.push_back(a);
    std::vector<char*> argv;
    for (auto& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDONLY);
        dup2(devnull, 0);
        dup2(outPipe[1], 1);
        dup2(errPipe[1], 2);
        close(outPipe[0]);
        close(errPipe[0]);
        if (!spec.workingDirectory.empty() && chdir(spec.workingDirectory.c_str()) != 0) _exit(127);
        for (const auto& [k, v] : spec.extraEnvironment) setenv(k.c_str(), v.c_str(), 1);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    for (auto& a : argStore) SecureClear(a);
    close(outPipe[1]);
    close(errPipe[1]);
    if (pid < 0) {
        close(outPipe[0]);
        close(errPipe[0]);
        result.startFailed = true;
        result.startError = "fork failed";
        return result;
    }
    setpgid(pid, pid);

    std::mutex outputMutex;
    auto reader = [&](int fd) {
        char buf[8192];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) {
            std::lock_guard lock(outputMutex);
            if (onOutput) onOutput(std::string_view(buf, static_cast<size_t>(n)));
        }
        close(fd);
    };
    std::thread outThread(reader, outPipe[0]), errThread(reader, errPipe[0]);

    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (cancel.IsCancelled() && !result.cancelled) {
            result.cancelled = true;
            kill(-pid, SIGKILL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    outThread.join();
    errThread.join();
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        if (result.exitCode == 127 && !result.cancelled) {
            result.startFailed = true;
            result.startError = "Could not start " + spec.executable.string();
        }
    } else {
        result.exitCode = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
    return result;
}

#endif

}  // namespace ck
