/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include "gui/gui_process.h"
#include "gui/i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GuiProcess {
    HANDLE process, job, output;
    int status;
    bool exited, eof;
    double retired_cpu;
    GuiCpuBase cpu_base;
};
static wchar_t* wide(const char* text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t* out = n ? malloc((size_t)n * sizeof(*out)) : NULL;
    if (out && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, n)) {
        free(out);
        return NULL;
    }
    return out;
}
/* Quote for the Windows CRT, not cmd.exe. Spaces, quotes and trailing '\\'
   must survive unchanged, including in non-ASCII filenames. */
static wchar_t* command_line(const char* const* argv) {
    size_t capacity = 1, n = 0;
    for (size_t i = 0; argv[i]; ++i) capacity += 2 * strlen(argv[i]) + 4;
    char* text = malloc(capacity);
    if (!text) return NULL;
    for (size_t i = 0; argv[i]; ++i) {
        size_t slashes = 0;
        if (i) text[n++] = ' ';
        text[n++] = '"';
        for (const char* p = argv[i]; *p; ++p) {
            if (*p == '\\') {
                ++slashes;
                continue;
            }
            if (*p == '"') slashes = slashes * 2 + 1;
            while (slashes) {
                text[n++] = '\\';
                --slashes;
            }
            text[n++] = *p;
        }
        slashes *= 2;
        while (slashes) {
            text[n++] = '\\';
            --slashes;
        }
        text[n++] = '"';
    }
    text[n] = 0;
    wchar_t* result = wide(text);
    free(text);
    return result;
}
static double file_seconds(FILETIME time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return (double)value.QuadPart / 1e7;
}
static double job_cpu(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
    if (!job || !QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), NULL))
        return 0;
    return (double)(info.TotalKernelTime.QuadPart + info.TotalUserTime.QuadPart) / 1e7;
}
GuiProcess* gui_process_create(void) {
    GuiProcess* process = calloc(1, sizeof(*process));
    if (process) process->exited = process->eof = true;
    return process;
}
bool gui_process_start(GuiProcess* process, const char* const* argv, char* error, size_t size) {
    int previous_status;
    if (!gui_process_finished(process, &previous_status)) {
        snprintf(error, size, "%s", snx_tr("A driver is already running"));
        return false;
    }
    process->retired_cpu += job_cpu(process->job);
    if (process->job) CloseHandle(process->job);
    if (process->process) CloseHandle(process->process);
    process->job = process->process = NULL;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION child = {0};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    HANDLE reader = NULL, writer = NULL, null = INVALID_HANDLE_VALUE, job = NULL;
    wchar_t *command = command_line(argv), *executable = wide(argv[0]);
    SIZE_T attribute_size = 0;
    bool initialized = false, success = false;
    DWORD failure = ERROR_NOT_ENOUGH_MEMORY;
    if (!command || !executable) goto done;
    if (!CreatePipe(&reader, &writer, &security, 0) || !SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0))
        goto failed;
    null = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
    job = CreateJobObjectW(NULL, NULL);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (null == INVALID_HANDLE_VALUE || !job ||
        !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        goto failed;
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = writer;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    startup.lpAttributeList = malloc(attribute_size);
    if (!startup.lpAttributeList) goto done;
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size)) goto failed;
    initialized = true;
    HANDLE inherited[] = {writer, null};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), NULL, NULL))
        goto failed;
    if (!CreateProcessW(executable, command, NULL, NULL, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL,
                        &startup.StartupInfo, &child))
        goto failed;
    /* Assign before resuming: no grandchild can escape the job during startup.
       Nested jobs also cover the jobs used internally by the native driver. */
    if (!AssignProcessToJobObject(job, child.hProcess) || ResumeThread(child.hThread) == (DWORD)-1) goto failed;
    process->process = child.hProcess;
    child.hProcess = NULL;
    process->job = job;
    job = NULL;
    process->output = reader;
    reader = NULL;
    process->exited = process->eof = false;
    process->status = 0;
    success = true;
    goto done;
failed:
    failure = GetLastError();
done:
    if (child.hProcess) {
        TerminateProcess(child.hProcess, 1);
        WaitForSingleObject(child.hProcess, INFINITE);
        CloseHandle(child.hProcess);
    }
    if (child.hThread) CloseHandle(child.hThread);
    if (reader) CloseHandle(reader);
    if (writer) CloseHandle(writer);
    if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
    if (job) CloseHandle(job);
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    free(command);
    free(executable);
    if (!success) snprintf(error, size, snx_tr("Cannot launch driver (Windows error %lu)"), (unsigned long)failure);
    return success;
}
int gui_process_read(GuiProcess* process, char* buffer, size_t size) {
    if (!process->output) return -1;
    DWORD available = 0, got = 0;
    if (PeekNamedPipe(process->output, NULL, 0, NULL, &available, NULL)) {
        if (!available) return 0;
        DWORD amount = available < size ? available : (DWORD)size;
        if (ReadFile(process->output, buffer, amount, &got, NULL) && got) return (int)got;
    }
    CloseHandle(process->output);
    process->output = NULL;
    process->eof = true;
    return -1;
}
bool gui_process_finished(GuiProcess* process, int* status) {
    if (!process->exited && WaitForSingleObject(process->process, 0) == WAIT_OBJECT_0) {
        DWORD code;
        if (!GetExitCodeProcess(process->process, &code)) code = 1;
        process->status = (int)code;
        process->exited = true;
    }
    if (!process->exited || !process->eof) return false;
    if (process->job) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
        if (!QueryInformationJobObject(process->job, JobObjectBasicAccountingInformation, &info, sizeof(info), NULL) ||
            info.ActiveProcesses)
            return false;
    }
    *status = process->status;
    return true;
}
static uint64_t resident(HANDLE process) {
    PROCESS_MEMORY_COUNTERS info;
    return GetProcessMemoryInfo(process, &info, sizeof(info)) ? (uint64_t)info.WorkingSetSize : 0;
}
GuiUsage gui_process_usage(GuiProcess* process) {
    GuiUsage usage = {.memory_bytes = resident(GetCurrentProcess()), .available = true};
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        usage.available = false;
        return usage;
    }
    double cpu = file_seconds(kernel) + file_seconds(user) + job_cpu(process->job) + process->retired_cpu;
    double now = (double)GetTickCount64() / 1000;
    if (process->job) {
        DWORD capacity = 64;
        for (;;) {
            size_t bytes = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + (size_t)capacity * sizeof(ULONG_PTR);
            JOBOBJECT_BASIC_PROCESS_ID_LIST* ids = malloc(bytes);
            if (!ids) {
                usage.available = false;
                break;
            }
            BOOL ok = QueryInformationJobObject(process->job, JobObjectBasicProcessIdList, ids, (DWORD)bytes, NULL);
            if (ok) {
                for (DWORD i = 0; i < ids->NumberOfProcessIdsInList; ++i) {
                    HANDLE child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                               (DWORD)ids->ProcessIdList[i]);
                    if (child) {
                        usage.memory_bytes += resident(child);
                        CloseHandle(child);
                    }
                }
                free(ids);
                break;
            }
            DWORD failure = GetLastError();
            free(ids);
            if (failure != ERROR_MORE_DATA || capacity >= 65536) {
                usage.available = false;
                break;
            }
            capacity *= 2;
        }
    }
    usage.cpu_percent = gui_cpu_percent(&process->cpu_base, cpu, now);
    return usage;
}
void gui_process_terminate(GuiProcess* process) {
    if (process && process->job) TerminateJobObject(process->job, 1);
}
void gui_process_destroy(GuiProcess* process) {
    if (!process) return;
    gui_process_terminate(process);
    if (process->job) CloseHandle(process->job);
    if (process->process) {
        WaitForSingleObject(process->process, INFINITE);
        CloseHandle(process->process);
    }
    if (process->output) CloseHandle(process->output);
    free(process);
}
