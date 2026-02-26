//
// Created by lqi on 2025/7/14.
//

#include "rescue.h"

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <pthread.h>
#include <dirent.h>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <sys/syscall.h>
#include <cstdio>
#include <memory>
#include <array>
#include <link.h>

// 获取当前进程的所有线程ID
std::vector<pid_t> GetAllThreadIds() {
    std::vector<pid_t> threadIds;
    pid_t pid = getpid();
    
    // 读取 /proc/self/task/ 目录来获取所有线程
    std::string taskPath = "/proc/" + std::to_string(pid) + "/task";
    DIR* dir = opendir(taskPath.c_str());
    
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_DIR) {
                std::string name = entry->d_name;
                if (name != "." && name != "..") {
                    try {
                        pid_t tid = std::stoi(name);
                        threadIds.push_back(tid);
                    } catch (...) {
                        // 忽略非数字的目录名
                    }
                }
            }
        }
        closedir(dir);
    }
    
    return threadIds;
}

// 解析符号名称，进行C++名称解码
std::string DemangleName(const char* symbol) {
    // 尝试提取函数名部分
    // backtrace_symbols 格式通常是: module(function+offset) [address]
    std::string symbolStr(symbol);
    size_t funcStart = symbolStr.find('(');
    size_t funcEnd = symbolStr.find('+', funcStart);
    
    if (funcStart != std::string::npos && funcEnd != std::string::npos) {
        std::string funcName = symbolStr.substr(funcStart + 1, funcEnd - funcStart - 1);
        
        // 使用 abi::__cxa_demangle 解码C++符号
        int status = 0;
        char* demangled = abi::__cxa_demangle(funcName.c_str(), nullptr, nullptr, &status);
        
        if (status == 0 && demangled) {
            std::string result = demangled;
            free(demangled);
            
            // 重新组合完整的符号信息
            return symbolStr.substr(0, funcStart + 1) + result + symbolStr.substr(funcEnd);
        }
    }
    
    return symbolStr;
}

// 获取模块的加载基址（用于PIE可执行文件）
uintptr_t GetModuleBaseAddress(const char* modulePath) {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return 0;
    }
    
    char line[512];
    uintptr_t baseAddr = 0;
    const char* targetName = strrchr(modulePath, '/');
    if (targetName) {
        targetName++; // 跳过 '/'
    } else {
        targetName = modulePath;
    }
    
    while (fgets(line, sizeof(line), maps)) {
        // 格式: address perms offset dev inode pathname
        // 例如: 55e8b0000000-55e8b0001000 r--p 00000000 08:01 123456 /path/to/exe
        uintptr_t start;
        char perms[16], offset[32], path[256];
        
        if (sscanf(line, "%lx-%*x %s %s %*s %*s %255s", &start, perms, offset, path) >= 4) {
            // 找到匹配的模块，且offset为00000000（第一个段）
            if (strstr(path, targetName) && strcmp(offset, "00000000") == 0) {
                baseAddr = start;
                break;
            }
        }
    }
    
    fclose(maps);
    return baseAddr;
}

// 使用addr2line批量获取源码位置信息（支持PIE）
// 需要编译时带-g选项
std::vector<std::string> GetSourceLocations(const std::vector<void*>& addresses, const char* modulePath) {
    std::vector<std::string> results(addresses.size());
    
    if (!modulePath || addresses.empty()) {
        return results;
    }
    
    // 获取模块基址（用于PIE可执行文件）
    uintptr_t baseAddr = GetModuleBaseAddress(modulePath);
    
    // 构建addr2line命令，一次处理所有地址
    // 使用 -i 显示内联函数，-s 只显示基本文件名
    std::ostringstream cmd;
    cmd << "addr2line -e '" << modulePath << "' -f -C -i -s -p";
    
    for (void* addr : addresses) {
        uintptr_t absAddr = (uintptr_t)addr;
        // 对于PIE可执行文件，需要使用相对地址
        if (baseAddr > 0 && absAddr > baseAddr) {
            cmd << " 0x" << std::hex << (absAddr - baseAddr);
        } else {
            cmd << " 0x" << std::hex << absAddr;
        }
    }
    cmd << " 2>/dev/null";
    
    // 执行命令并读取输出
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return results;
    }
    
    char buffer[1024];
    size_t idx = 0;
    while (fgets(buffer, sizeof(buffer), pipe) && idx < addresses.size()) {
        std::string line = buffer;
        // 移除换行符
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        
        // 如果结果是 "?? ??:0" 说明没有调试信息
        if (line.find("?? ??:0") == std::string::npos && 
            line.find("??:?") == std::string::npos &&
            !line.empty()) {
            results[idx] = line;
        }
        idx++;
    }
    
    pclose(pipe);
    return results;
}

// 单个地址的快速版本（用于实时场景，支持PIE）
std::string GetSourceLocation(void* addr, const char* modulePath) {
    if (!modulePath) {
        return "";
    }
    
    // 获取模块基址（用于PIE可执行文件）
    uintptr_t baseAddr = GetModuleBaseAddress(modulePath);
    uintptr_t absAddr = (uintptr_t)addr;
    uintptr_t relAddr = (baseAddr > 0 && absAddr > baseAddr) ? (absAddr - baseAddr) : absAddr;
    
    // 构建addr2line命令
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -i -s -p 0x%lx 2>&1", 
             modulePath, (unsigned long)relAddr);
    
    // 执行命令并读取输出
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return "";
    }
    
    char buffer[1024];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe)) {
        result = buffer;
        // 移除换行符
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        
        // 如果结果是 "?? ??:0" 说明没有调试信息
        if (result.find("?? ??:0") != std::string::npos || 
            result.find("??:?") != std::string::npos) {
            result.clear();
        }
    }
    
    pclose(pipe);
    return result;
}

// 获取更详细的符号信息（使用dladdr + addr2line）
std::string GetDetailedSymbolInfo(void* addr) {
    Dl_info info;
    if (dladdr(addr, &info)) {
        std::ostringstream oss;
        
        // 函数名
        std::string funcName;
        if (info.dli_sname) {
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            if (status == 0 && demangled) {
                funcName = demangled;
                free(demangled);
            } else {
                funcName = info.dli_sname;
            }
        }
        
        // 尝试使用addr2line获取源码位置
        std::string sourceLocation = GetSourceLocation(addr, info.dli_fname);
        
        if (!sourceLocation.empty()) {
            // 如果有源码位置信息，优先显示
            oss << sourceLocation;
            
            // 如果函数名不在源码位置中，追加函数名
            if (!funcName.empty() && sourceLocation.find(funcName) == std::string::npos) {
                oss << " (" << funcName << ")";
            }
        } else {
            // 降级到dladdr的信息
            // 模块名
            if (info.dli_fname) {
                const char* fname = strrchr(info.dli_fname, '/');
                oss << (fname ? fname + 1 : info.dli_fname);
            }
            
            // 函数名
            if (!funcName.empty()) {
                oss << "(" << funcName;
                
                // 偏移量
                if (info.dli_saddr) {
                    ptrdiff_t offset = (char*)addr - (char*)info.dli_saddr;
                    oss << "+0x" << std::hex << offset << std::dec;
                }
                oss << ")";
            }
        }
        
        // 地址
        oss << " [0x" << std::hex << (uintptr_t)addr << std::dec << "]";
        
        return oss.str();
    }
    
    return "";
}

// 打印线程堆栈（优化版本，批量获取源码位置）
std::string PrintThreadStack(pid_t threadId) {
    std::ostringstream oss;
    
    // Linux下获取其他线程的堆栈比较复杂
    // 这里我们只能获取当前线程的堆栈，或者在信号处理中获取
    pid_t currentTid = syscall(SYS_gettid);
    
    oss << "thread " << threadId;
    if (threadId != currentTid) {
        oss << " (cannot backtrace other threads in normal mode, only current thread: " 
            << currentTid << ")" << std::endl;
        return oss.str();
    }
    
    oss << " trace stack:" << std::endl;
    
    // 获取堆栈
    const int MAX_FRAMES = 128;
    void* buffer[MAX_FRAMES];
    int nptrs = backtrace(buffer, MAX_FRAMES);
    
    if (nptrs <= 0) {
        oss << "  Error: cannot get backtrace" << std::endl;
        return oss.str();
    }
    
    // 获取符号信息
    char** strings = backtrace_symbols(buffer, nptrs);
    if (strings == nullptr) {
        oss << "  Error: cannot get backtrace symbols" << std::endl;
        return oss.str();
    }
    
    // 批量获取主程序的源码位置（性能优化）
    char exePath[512];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
    } else {
        exePath[0] = '\0';
    }
    
    // 按模块分组地址
    struct FrameInfo {
        void* addr;
        const char* modulePath;
        std::string funcName;
    };
    std::vector<FrameInfo> frames(nptrs);
    
    for (int i = 0; i < nptrs; i++) {
        frames[i].addr = buffer[i];
        
        Dl_info info;
        if (dladdr(buffer[i], &info)) {
            frames[i].modulePath = info.dli_fname;
            
            // 解析函数名
            if (info.dli_sname) {
                int status = 0;
                char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    frames[i].funcName = demangled;
                    free(demangled);
                } else {
                    frames[i].funcName = info.dli_sname;
                }
            }
        } else {
            frames[i].modulePath = exePath;
        }
    }
    
    // 批量获取主程序的源码位置
    std::vector<void*> mainAddrs;
    std::vector<int> mainIndices;
    const char* mainExePath = nullptr;
    
    for (int i = 0; i < nptrs; i++) {
        if (frames[i].modulePath) {
            // 检查是否是主程序（使用字符串包含而不是完全相等）
            if (strstr(frames[i].modulePath, "MovaX") != nullptr) {
                mainAddrs.push_back(frames[i].addr);
                mainIndices.push_back(i);
                
                // 使用实际的模块路径（可能是相对路径或绝对路径）
                if (mainExePath == nullptr) {
                    // 如果是相对路径，转换为绝对路径
                    if (frames[i].modulePath[0] == '.') {
                        mainExePath = exePath;  // 使用 /proc/self/exe 的路径
                    } else {
                        mainExePath = frames[i].modulePath;
                    }
                }
            }
        }
    }
    
    std::vector<std::string> sourceLocations;
    if (!mainAddrs.empty() && mainExePath != nullptr) {
        sourceLocations = GetSourceLocations(mainAddrs, mainExePath);
    }
    
    // 打印每一帧
    for (int i = 0; i < nptrs; i++) {
        oss << "  #" << i << " ";
        
        // 查找是否有源码位置信息
        std::string sourceLocation;
        for (size_t j = 0; j < mainIndices.size(); j++) {
            if (mainIndices[j] == i && j < sourceLocations.size()) {
                sourceLocation = sourceLocations[j];
                break;
            }
        }
        
        if (!sourceLocation.empty()) {
            // 显示源码位置
            oss << sourceLocation;
        } else if (!frames[i].funcName.empty()) {
            // 显示函数名和模块
            if (frames[i].modulePath) {
                const char* fname = strrchr(frames[i].modulePath, '/');
                oss << (fname ? fname + 1 : frames[i].modulePath) << "(";
            }
            oss << frames[i].funcName << ")";
        } else {
            // 降级使用backtrace_symbols的结果
            oss << DemangleName(strings[i]);
        }
        
        // 地址
        oss << " [0x" << std::hex << (uintptr_t)buffer[i] << std::dec << "]";
        oss << std::endl;
    }
    
    free(strings);
    oss << std::endl;
    
    return oss.str();
}

// 信号处理函数 - 用于捕获崩溃
void CrashSignalHandler(int sig, siginfo_t* info, void* context) {
    std::ostringstream oss;
    
    // 打印信号信息
    oss << "\n==========================================================\n";
    oss << "Caught signal " << sig << " (";
    
    switch (sig) {
        case SIGSEGV: oss << "SIGSEGV - Segmentation fault"; break;
        case SIGABRT: oss << "SIGABRT - Abort signal"; break;
        case SIGFPE:  oss << "SIGFPE - Floating point exception"; break;
        case SIGILL:  oss << "SIGILL - Illegal instruction"; break;
        case SIGBUS:  oss << "SIGBUS - Bus error"; break;
        default: oss << "Unknown signal"; break;
    }
    
    oss << ")\n";
    oss << "Signal info: si_addr=" << info->si_addr 
        << ", si_code=" << info->si_code << "\n";
    oss << "==========================================================\n\n";
    
    // 打印当前线程的堆栈
    pid_t currentTid = syscall(SYS_gettid);
    oss << PrintThreadStack(currentTid);
    
    // 输出到stderr和日志
    const std::string crashLog = oss.str();
    write(STDERR_FILENO, crashLog.c_str(), crashLog.size());
    qCritical() << crashLog.c_str();
    
    // 恢复默认信号处理并重新触发信号
    signal(sig, SIG_DFL);
    raise(sig);
}

// 设置崩溃处理器
void SetupCrashHandler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = CrashSignalHandler;
    
    // 注册多个信号处理
    sigaction(SIGSEGV, &sa, nullptr);  // 段错误
    sigaction(SIGABRT, &sa, nullptr);  // abort()
    sigaction(SIGFPE, &sa, nullptr);   // 浮点异常
    sigaction(SIGILL, &sa, nullptr);   // 非法指令
    sigaction(SIGBUS, &sa, nullptr);   // 总线错误
}

// 主入口函数
void setupRescue(ve::Data* d)
{
    // 设置崩溃处理器
    SetupCrashHandler();
    
    // export功能：导出数据
    QObject::connect(ve::d(d, "export"), &ve::Data::changed, [] {
        ve::data::Manager::writeToBin("root.bin", ve::data::manager().rootMobj()->exportToBin(false));
    });
    
    // dump功能：打印所有线程的堆栈
    QObject::connect(ve::d(d, "dump"), &ve::Data::changed, [rescue_d = d] {
        std::vector<pid_t> threadIds = GetAllThreadIds();
        std::ostringstream oss;
        
        pid_t currentTid = syscall(SYS_gettid);
        oss << "Current thread: " << currentTid 
            << ", Thread count: " << threadIds.size() << "\n\n";
        
        // 打印当前线程的详细堆栈
        oss << PrintThreadStack(currentTid);
        
        // 列出其他线程
        if (threadIds.size() > 1) {
            oss << "Other threads in process:\n";
            for (pid_t tid : threadIds) {
                if (tid != currentTid) {
                    oss << "  - Thread " << tid << " (cannot backtrace from current context)\n";
                }
            }
            oss << "\nNote: Linux implementation can only backtrace current thread in normal mode.\n";
            oss << "Use signal handler to capture all threads during crash.\n";
        }
        
        std::string result = oss.str();
        qCritical() << result.c_str();
        ve::d(rescue_d, "result")->set(nullptr, QString::fromStdString(result));
    });
}
