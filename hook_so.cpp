#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <link.h>
#include <atomic>
#include <cstdlib>
#include <cerrno>
#include <mutex>
#include <vector>
#include <memory>
#include <cstdint>
#include <thread>

// khai bao lua 5.0.1 - 5.0.3
extern "C" {
    #include "lua.h"
    #include "lauxlib.h"   // chứa lua_setfield, lua_setglobal, lua_gettop, v.v.
//    #include "lualib.h"
}

class LuaInterface;

#define HOOK_STUB_SIZE 14
//#define TRAMPOLINE_COPY_SIZE 32
#define PAGE_ALIGN(addr) ((uintptr_t)(addr) & ~(sysconf(_SC_PAGESIZE) - 1))
#define LOG_BUFFER_SIZE 1024
#define MAX_HOOK_RETRY 3
static size_t TRAMPOLINE_COPY_SIZE = 32; // mặc định

//build
//sudo apt update
//sudo apt install build-essential
//g++ -shared -fPIC -O2 -std=c++14 -pthread hook_so.cpp -ldl -o hook_so.so
//g++ -shared -fPIC -O2 -std=c++14 -pthread -fpermissive hook_so.cpp -ldl -o hook_so.so
//g++ -shared -fPIC -O2 -std=c++14 -pthread -fpermissive -I/home/tlbb/Server/Lua hook_so.cpp -L. -lLuaLib -ldl -o hook_so.so

//g++ -shared -fPIC -O2 -std=c++14 \
-I/home/tlbb/Server/Lua \
hook_so.cpp \
-ldl \
-o hook_so.so

//apt update
//apt install g++-multilib gcc-multilib libc6-dev-i386
/* ============================================================
   CẤU HÌNH
============================================================ */
// Cấu hình runtime
static std::atomic<bool> g_enable_log{true}; // tắt log false

static const char* LOG_PATH = "/home/tlbb/Server/Log/";
static pthread_mutex_t g_patch_mutex = PTHREAD_MUTEX_INITIALIZER;
// Typedef cho hàm gốc (nếu bạn hook bằng member pointer)
typedef void (LuaInterface::*RegFn)();
static RegFn orig_FoxRegisterFunction = nullptr;
// Định nghĩa các hàm Lua cần thiết nếu không có header
// ===== Forward declare Lua =====
extern "C" {
    typedef struct lua_State lua_State;
}
typedef int (*lua_CFunction)(lua_State *L);
typedef int (*FoxLuaScript_RegisterFunction_t)(void* this_ptr, const char* func_name, void* func_ptr);
static FoxLuaScript_RegisterFunction_t g_orig_FoxRegisterFunction = nullptr;
static std::atomic<bool> g_lua_injected{false};
static pthread_mutex_t g_lua_mutex = PTHREAD_MUTEX_INITIALIZER;
static void* g_exe_script_ddddddddddd = nullptr;
//skill005_hook
typedef int (*GetExteriorRideMaxSpeed_Type)(void*);
typedef int (*SendImpactToUnit_Type)(void*, void*, unsigned short, unsigned int, int, int);


/* ============================================================
   LOGGER TIÊN TIẾN - THREAD SAFE, KHÔNG BLOCK - CÓ THỜI GIAN
============================================================ */
class AsyncLogger {
private:
    FILE* log_file;
    pthread_mutex_t mutex;
    char buffer[LOG_BUFFER_SIZE];
    
    // Hàm lấy thời gian hiện tại dạng string
    void get_timestamp(char* timestamp, size_t size) {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        
        // Lấy microsecond (hoặc millisecond) để chi tiết hơn
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        
        // Format: YYYY-MM-DD HH:MM:SS.mmm
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);
        
        // SỬA: Tăng kích thước buffer cho ms
        char ms[16];  // Tăng từ 8 lên 16
        snprintf(ms, sizeof(ms), ".%03ld", ts.tv_nsec / 1000000);
        
        // Ghép chuỗi an toàn
        snprintf(timestamp, size, "%s%s", time_str, ms);
    }
    
public:
    AsyncLogger() : log_file(nullptr) {
        pthread_mutex_init(&mutex, nullptr);
    }
    
    ~AsyncLogger() {
        pthread_mutex_destroy(&mutex);
        if (log_file) {
            fflush(log_file);
            fclose(log_file);
        }
    }
    
    bool init() {
        if (!g_enable_log.load(std::memory_order_relaxed))
            return false;
            
        // Tạo thư mục log nếu chưa tồn tại
        struct stat st = {};
        if (stat(LOG_PATH, &st) == -1) {
            mkdir(LOG_PATH, 0755);
        }
        
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        
        char filename[256];
        strftime(filename, sizeof(filename),
                 "hook_GS_%Y%m%d_%H%M%S_%s.log",
                 &tm_info);
        
        // Tạo đường dẫn đầy đủ
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s%s", LOG_PATH, filename);
        
        log_file = fopen(fullpath, "a");
        if (log_file) {
            setvbuf(log_file, nullptr, _IOLBF, 0);
            
            // Ghi dòng đầu tiên với thời gian
            char timestamp[64];
            get_timestamp(timestamp, sizeof(timestamp));
            fprintf(log_file, "\n[%s] ========== LOGGER INITIALIZED ==========\n", timestamp);
            fflush(log_file);
            
            return true;
        }
        return false;
    }
    
    void log(const char* fmt, ...) {
        if (!g_enable_log.load(std::memory_order_relaxed) || !log_file)
            return;
            
        pthread_mutex_lock(&mutex);
        
        // Lấy thời gian hiện tại
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        
        // Ghi timestamp
        fprintf(log_file, "[%s] ", timestamp);
        
        // Ghi message
        va_list ap;
        va_start(ap, fmt);
        vfprintf(log_file, fmt, ap);
        va_end(ap);
        
        fprintf(log_file, "\n");
        fflush(log_file);
        
        pthread_mutex_unlock(&mutex);
    }
};
static std::unique_ptr<AsyncLogger> g_logger;

#define LOG(fmt, ...) do { \
    if (g_logger) { \
        g_logger->log(fmt, ##__VA_ARGS__); \
    } \
} while(0)

/* ============================================================
   BIẾN TOÀN CỤC VÀ HÀM RESOLVE
============================================================ */
struct GlobalPointers {
	//thu cuoi skill005_hook
    std::atomic<int*> Exterior_RideTbl_ptr{nullptr};
    std::atomic<int*> dword_103FD44_ptr{nullptr};
    std::atomic<int*> dword_103FE70_ptr{nullptr};
    std::atomic<void*> ImpactCore_ptr{nullptr};
    std::atomic<void*> GetExteriorRideMaxSpeed{nullptr};
    std::atomic<void*> SendImpactToUnit{nullptr};

};

static GlobalPointers g_globals;

/* ============================================================
   MEMORY PROTECTION UTILITIES - AN TOÀN CHO MULTITHREADING
============================================================ */
class MemoryProtector {
private:
    void* addr;
    size_t size;
    int old_prot;
    
public:
    MemoryProtector(void* target, size_t sz, int new_prot) 
        : addr(target), size(sz) {
        uintptr_t page_start = PAGE_ALIGN(target);
        size_t page_size = sysconf(_SC_PAGESIZE);
        
        // Lưu protection cũ
        FILE* f = fopen("/proc/self/maps", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                uintptr_t start, end;
                char perms[5];
                if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
                    if (start <= page_start && page_start < end) {
                        old_prot = 0;
                        if (perms[0] == 'r') old_prot |= PROT_READ;
                        if (perms[1] == 'w') old_prot |= PROT_WRITE;
                        if (perms[2] == 'x') old_prot |= PROT_EXEC;
                        break;
                    }
                }
            }
            fclose(f);
        }
        
        mprotect((void*)page_start, page_size, new_prot);
    }
    
    ~MemoryProtector() {
        uintptr_t page_start = PAGE_ALIGN(addr);
        size_t page_size = sysconf(_SC_PAGESIZE);
        mprotect((void*)page_start, page_size, old_prot);
    }
};

/* ============================================================
   HOOK ENGINE TIÊN TIẾN - XỬ LÝ LỖI, RETRY
============================================================ */
class HookEngine {
private:
    static pthread_mutex_t hook_mutex;
    static std::vector<void*> trampolines;
    
public:
	
	/* ============================================================
	   PATCH ENGINE - PHIÊN BẢN AN TOÀN TUYỆT ĐỐI
	============================================================ */

	static bool is_executable_memory(void* addr)
	{
		FILE* f = fopen("/proc/self/maps", "r");
		if (!f) return false;
		char line[512];
		uintptr_t search_addr = (uintptr_t)addr;
		bool is_exec = false;
		while (fgets(line, sizeof(line), f))
		{
			uintptr_t start, end;
			char perms[5];
			if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3)
			{
				if (search_addr >= start && search_addr < end)
				{
					if (perms[2] == 'x') // Có quyền execute
					{
						is_exec = true;
					}
					break;
				}
			}
		}
		fclose(f);
		return is_exec;
	}

	static void patch_code_safe(void* target, void* hook)
	{
		if (!target || !hook)
		{
			LOG("patch_code_safe: target hoac hook NULL");
			return;
		}

		// Kiểm tra xem target có phải là executable memory không
		if (!is_executable_memory(target))
		{
			LOG("patch_code_safe: target %p khong phai executable memory", target);
			return;
		}

		pthread_mutex_lock(&g_patch_mutex);

		LOG("Patching at %p -> %p", target, hook);

		// Tạo patch code
		uint8_t patch[12];
		memset(patch, 0x90, sizeof(patch)); // Fill với NOP trước

		// mov rax, hook (64-bit immediate)
		patch[0] = 0x48;        // REX.W prefix
		patch[1] = 0xB8;        // mov rax, imm64
		*(uint64_t*)(patch + 2) = (uint64_t)hook;

		// jmp rax
		patch[10] = 0xFF;       // jmp
		patch[11] = 0xE0;       // rax

		// Lưu code gốc để debug (nếu cần)
		uint8_t original[12];
		memcpy(original, target, sizeof(original));

		// Tính toán page boundaries
		size_t page_size = sysconf(_SC_PAGESIZE);
		uintptr_t page_start = (uintptr_t)target & ~(page_size - 1);
		size_t total_size = page_size; // Chỉ thay đổi 1 page

		// QUAN TRỌNG: Tạm thời cho phép ghi
		if (mprotect((void*)page_start, total_size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1)
		{
			LOG("mprotect RWX failed: %s", strerror(errno));
			pthread_mutex_unlock(&g_patch_mutex);
			return;
		}

		// Ghi patch - ghi từng phần để tránh lỗi
		// Ghi 8 byte đầu
		memcpy(target, patch, 8);
		__sync_synchronize();
		
		// Ghi 4 byte còn lại
		memcpy((char*)target + 8, patch + 8, 4);
		__sync_synchronize();

		// Clear instruction cache
		__builtin___clear_cache((char*)target, (char*)target + sizeof(patch));

		// Verify patch đã được ghi đúng
		if (memcmp(target, patch, sizeof(patch)) != 0)
		{
			LOG("patch_code_safe: verify that bai, phuc hoi code goc");
			memcpy(target, original, sizeof(original));
			__builtin___clear_cache((char*)target, (char*)target + sizeof(original));
		}

		// Khôi phục quyền RX
		if (mprotect((void*)page_start, total_size, PROT_READ | PROT_EXEC) == -1)
		{
			LOG("mprotect RX restore failed: %s", strerror(errno));
		}

		LOG("patch_code_safe DONE at %p", target);
		
		pthread_mutex_unlock(&g_patch_mutex);
	}
	
	static void* create_trampoline(void* target, size_t TRAMPOLINE_COPY_SIZE)
	{
		if (!target) return nullptr;

		std::lock_guard<std::mutex> lock(*(std::mutex*)&hook_mutex);

		void* trampoline = mmap(nullptr, 4096,
								PROT_READ | PROT_WRITE | PROT_EXEC,
								MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (trampoline == MAP_FAILED)
		{
			LOG("ERROR: Khong the tao trampoline: %s", strerror(errno));
			return nullptr;
		}

		// Copy code gốc
		memcpy(trampoline, target, TRAMPOLINE_COPY_SIZE);

		uint8_t* code = (uint8_t*)target;

		// Fix JMP nếu byte đầu là E9
		if (code[0] == 0xE9)
		{
			int32_t rel_offset = *(int32_t*)(code + 1);
			uint64_t dest = (uint64_t)target + 5 + rel_offset;

			uint64_t tramp_start = (uint64_t)trampoline;
			int32_t new_offset = (int32_t)(dest - (tramp_start + 5));

			*(int32_t*)((uint8_t*)trampoline + 1) = new_offset;

			LOG("Fixed JMP offset: old=%d, new=%d", rel_offset, new_offset);
		}

		// Jump về phần còn lại của code gốc
		uint8_t* p = (uint8_t*)trampoline + TRAMPOLINE_COPY_SIZE;

		p[0] = 0x48;               // mov rax, addr
		p[1] = 0xB8;
		*(uint64_t*)(p + 2) = (uint64_t)((uint8_t*)target + TRAMPOLINE_COPY_SIZE);
		p[10] = 0xFF;              // jmp rax
		p[11] = 0xE0;

		trampolines.push_back(trampoline);

		return trampoline;
	}
	
    static void cleanup() {
        for (auto t : trampolines) {
            munmap(t, 4096);
        }
        trampolines.clear();
    }
};

pthread_mutex_t HookEngine::hook_mutex = PTHREAD_MUTEX_INITIALIZER;
std::vector<void*> HookEngine::trampolines;

/* ============================================================
   GET BASE ADDRESS
============================================================ */
static uintptr_t get_module_base(const char* module_name) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    
    char line[512];
    uintptr_t base = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, module_name) && strstr(line, "r-xp")) {
            base = strtoull(line, nullptr, 16);
            break;
        }
    }
    
    fclose(f);
    return base;
}

/* ============================================================
   RESOLVE SYMBOLS AN TOÀN
============================================================ */
static void resolve_symbols() {
    // Thử dlsym trước
    g_globals.Exterior_RideTbl_ptr.store(
        (int*)dlsym(RTLD_DEFAULT, "g_Exterior_RideTbl"),
        std::memory_order_release);

    if (!g_globals.Exterior_RideTbl_ptr.load(std::memory_order_acquire)) {
        // Fallback to offset - CẦN KIỂM TRA LẠI OFFSET NÀY
        uintptr_t base = get_module_base("Server");
        if (base) {
            // OFFSET NÀY CÓ THỂ SAI! Cần kiểm tra lại trong IDA
            g_globals.Exterior_RideTbl_ptr.store((int*)(base + 0x103fd40), std::memory_order_release);
            LOG("Using fallback address: %p", (void*)(base + 0x103fd40));
        } else {
            g_globals.Exterior_RideTbl_ptr.store((int*)0x103fd40, std::memory_order_release);
        }
    }
    
    // Tương tự cho các symbol khác
    g_globals.dword_103FD44_ptr.store(
        (int*)dlsym(RTLD_DEFAULT, "dword_103FD44"),
        std::memory_order_release);
    if (!g_globals.dword_103FD44_ptr.load(std::memory_order_acquire)) {
        g_globals.dword_103FD44_ptr.store((int*)0x103FD44,std::memory_order_release);
    }
    
    g_globals.dword_103FE70_ptr.store(
        (int*)dlsym(RTLD_DEFAULT, "dword_103FE70"),
        std::memory_order_release);
    if (!g_globals.dword_103FE70_ptr.load(std::memory_order_acquire)) {
        g_globals.dword_103FE70_ptr.store((int*)0x103FE70,std::memory_order_release);
    }
	
    g_globals.ImpactCore_ptr.store(
        dlsym(RTLD_DEFAULT, "g_ImpactCore"),
        std::memory_order_release);
    if (!g_globals.ImpactCore_ptr.load(std::memory_order_acquire)) {
        g_globals.ImpactCore_ptr.store((void*)0x1CA341D4,std::memory_order_release);
    }

    g_globals.GetExteriorRideMaxSpeed.store(
        dlsym(RTLD_DEFAULT, "_ZN9Obj_Human23GetExteriorRideMaxSpeedEv"),
        std::memory_order_release);
    
    g_globals.SendImpactToUnit.store(
        dlsym(RTLD_DEFAULT, "_ZNK13Combat_Module13Impact_Module12ImpactCore_T16SendImpactToUnitER13Obj_Charactertiiiii"),
        std::memory_order_release);


}

/* ============================================================
   HÀM HOOK CHÍNH - TỐI ƯU, CHECK NULL, THREAD SAFE
============================================================ */
//lay ra ham lua co tren sv strings ./libLuaLib.so | grep -i '^lua_' | sort | uniq > lua_symbols.txt
//g++ -shared -fPIC -O2 -std=c++14 -pthread \
    -I/usr/include/lua5.1 \
    hook_so.cpp -ldl -o hook_so.so
//strings ./Server | grep -i TriggerLuaEventExtended  > TriggerLuaEventExtended.txt
//strings ./Server | grep -i -C 10 "TriggerLuaEventExtended" > TriggerLuaEventExtended.txt
//strings ./Server | grep -i "LuaFnTbl\|BeginEvent\|AddText\|AddNumber\|CallScriptFunction" > lua_cpp2.txt
/* ============================================================
   FOXLUA SCRIPT REGISTER FUNCTION - THEO PSEUDOCODE
============================================================ */


//khai báo
extern "C" {
    int LuaFnGetAccountName(lua_State *L);
    int LuaFnEquipTransToNew(lua_State *L);
    // ... thêm các hàm khác
}
/*
// Các hàm Lua thường dùng
extern "C" {

    // Forward declare lua_State nếu không include lua.h
    typedef struct lua_State lua_State;

    // =========================
    // Core stack operations
    // =========================
    int lua_gettop(lua_State *L);
    void lua_settop(lua_State *L, int idx);

    // =========================
    // Type checking
    // =========================
    int lua_isnumber(lua_State *L, int idx);
    int lua_isstring(lua_State *L, int idx);
    int lua_isfunction(lua_State *L, int idx);
    int lua_type(lua_State *L, int idx);
    const char* lua_typename(lua_State *L, int tp);

    // =========================
    // Conversion
    // =========================
    double lua_tonumber(lua_State *L, int idx);
    const char* lua_tostring(lua_State *L, int idx);

    // lauxlib (Lua 5.0.1)
    const char* luaL_checkstring(lua_State *L, int idx);
    double luaL_checknumber(lua_State *L, int idx);
    int luaL_error(lua_State *L, const char* fmt, ...);

    // =========================
    // Push values
    // =========================
    void lua_pushnil(lua_State *L);
    void lua_pushnumber(lua_State *L, double n);
    void lua_pushstring(lua_State *L, const char* s);
    void lua_pushlstring(lua_State *L, const char* s, size_t len);
    void lua_pushvalue(lua_State *L, int idx);

    // =========================
    // Global access
    // =========================
    void lua_getglobal(lua_State *L, const char* name);
    void lua_setglobal(lua_State *L, const char* name);

    // =========================
    // Call functions
    // =========================
    void lua_call(lua_State *L, int nargs, int nresults);
    int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc);

    // =========================
    // Table / userdata
    // =========================
    void lua_newtable(lua_State *L);
    void* lua_newuserdata(lua_State *L, size_t nbytes);

}
*/

/* ============================================================
   HÀM CALL SCRIPT
============================================================ */

extern "C" {
    // ... các extern khác

    // Bỏ __fastcall và __int64, dùng long long
    long long _ZN12LuaInterface21ExeScript_DDDDDDDDDDDEiPKciiiiiiiiiii(
        void* this_ptr,
        unsigned int script_id,
        const char *event_name,
        int p1, int p2, int p3, int p4, int p5,
        int p6, int p7, int p8, int p9, int p10, int p11
    );
}


// Resolve hàm (dlsym ưu tiên + offset fallback)
void resolve_exe_script_func() {
    // Cách 1: dlsym (thử tên mangled chính xác)
    void* handle = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);
    if (handle) {
        g_exe_script_ddddddddddd = dlsym(handle, "_ZN12LuaInterface21ExeScript_DDDDDDDDDDDEiPKciiiiiiiiiii");
        if (g_exe_script_ddddddddddd) {
            LOG("Tìm thấy ExeScript_DDDDDDDDDDD qua dlsym: %p", g_exe_script_ddddddddddd);
            dlclose(handle);
            return;
        }
        dlclose(handle);
    }

    // Cách 2: fallback offset cứng
    uintptr_t base = get_module_base("Server");
    if (base) {
        uintptr_t func_offset = 0x96b8c4;
        //g_exe_script_ddddddddddd = (void*)(base + func_offset);
        g_exe_script_ddddddddddd = (void*)func_offset;
        if (g_exe_script_ddddddddddd && HookEngine::is_executable_memory(g_exe_script_ddddddddddd)) {
            LOG("Fallback offset thành công: ExeScript tại %p (base + 0x%lx)", 
                g_exe_script_ddddddddddd, func_offset);
        } else {
            LOG("Offset 0x96b8c4 không hợp lệ hoặc không executable");
            g_exe_script_ddddddddddd = nullptr;
        }
    } else {
        LOG("Không lấy được base address");
    }
}

// Đầu file: khai báo global (nếu chưa có)
std::atomic<LuaInterface*> g_lua_interface{nullptr};
std::atomic<lua_State*>    g_lua_state{nullptr};

// Hàm gọi Lua event với 11 tham số
void TriggerLuaEventExtended_Hook(
    unsigned int script_id,
    const char *event_name,
    int p1, int p2, int p3, int p4, int p5,
    int p6, int p7, int p8, int p9, int p10, int p11
) {
    if (!g_exe_script_ddddddddddd) {
        LOG("Chưa resolve được ExeScript func");
        return;
    }
    if (!g_lua_interface) {
        LOG("Chưa resolve được LuaInterface");
        return;
    }

    // typedef đúng cú pháp Linux x64 (không __fastcall)
    typedef long long (*ExeScriptFuncPtr)(
        void* this_ptr,
        unsigned int id,
        const char *name,
        int a1, int a2, int a3, int a4, int a5,
        int a6, int a7, int a8, int a9, int a10, int a11
    );

    ExeScriptFuncPtr func = (ExeScriptFuncPtr)g_exe_script_ddddddddddd;

    long long result = func(
        g_lua_interface,
        script_id,
        event_name,
        p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11
    );

    LOG("Gọi thành công ExeScript | script=%u | event=%s | result=%lld",
        script_id, event_name, result);
}

#define TRIGGER_LUA_EVENT(script_id, event, scene, self, target) \
    TriggerLuaEventExtended_Hook( \
        (unsigned int)(script_id), \
        (event), \
        (int)(scene), \
        (int)(self), \
        (int)(target), \
        -1, 0, 0, 0, 0, 0, 0, 0 \
    )
// Sử dụng
//TRIGGER_LUA_EVENT(2116, "OnDefaultEvent", scene_id, obj_id, obj_id);

static double safe_get_number(lua_State *L, int idx, double fallback = 0.0) {
    if (lua_isnumber(L, idx)) {
        return lua_tonumber(L, idx);
    }
    LOG("Arg %d không phải number (type=%d - %s), fallback %.2f",
        idx, lua_type(L, idx),
        lua_typename(L, lua_type(L, idx)),  // dùng lua_typename nếu có
        fallback);
    return fallback;
}

static int safe_get_int(lua_State *L, int idx, int fallback = 0) {
    if (lua_isnumber(L, idx)) {
        return (int)lua_tonumber(L, idx);
    }
    LOG("Arg %d không phải number (type=%d - %s), fallback %d",
        idx, lua_type(L, idx),
        lua_typename(L, lua_type(L, idx)),
        fallback);
    return fallback;
}



// Hàm hook cho LuaFnEquipTransToNew
extern "C" int LuaFnEquipTransToNew(lua_State *L) {
    LOG("LuaFnEquipTransToNew called from Lua");

    int n = lua_gettop(L);
    if (n < 5) {
        LOG("Thiếu argument (cần >=5)");
        lua_pushnumber(L, -1.0);
        return 1;
    }

    int scene_id   = safe_get_int(L, 1);
    int obj_id     = safe_get_int(L, 2);
    int uBagIndex  = safe_get_int(L, 3);
    int ItemSerial = safe_get_int(L, 4);
    double v1      = safe_get_number(L, 5, 0.0);

    LOG("Args: scene_id=%d, obj_id=%d, bag=%d, serial=%d, v1=%.2f",
        scene_id, obj_id, uBagIndex, ItemSerial, v1);


    LOG("EquipTransToNew success (placeholder offsets)");
    lua_pushnumber(L, 1.0);
    return 1;
}
/* ==================== HÀM GET ACCOUNT CỦA CHAR ===================*/

extern "C" int LuaFnGetAccountName(lua_State *L) {
    LOG("LuaFnGetAccountName called from Lua");
    
    int n = lua_gettop(L);
    LOG("Number of arguments: %d", n);
    
    if (n >= 1 && lua_isstring(L, 1)) {
        const char* param = lua_tostring(L, 1);
        LOG("Parameter: %s", param);
    }
    
    const char* text = "Test OK LuaFnGetAccountName";
    
    // Push kết quả lên Lua stack
    lua_pushstring(L, text);
    
    LOG("Returning LuaFnGetAccountName: %s", text);
    return 1;
}
/* ============================================================
   HOOK THÊM HÀM VÀO GS
============================================================ */
extern "C"
int FoxRegisterFunction_Hook(void* this_void_ptr, const char* func_name, void* func_ptr) {
    // Cast an toàn sang LuaInterface* (server truyền đúng loại)
    LuaInterface* this_ptr = static_cast<LuaInterface*>(this_void_ptr);

    // Log debug (bật nếu cần, comment khi ổn định)
    // LOG("FoxRegisterFunction_Hook: this_ptr=%p, func_name=%s, func_ptr=%p", this_ptr, func_name ? func_name : "NULL", func_ptr);

    if (!g_orig_FoxRegisterFunction) {
        LOG("ERROR: g_orig_FoxRegisterFunction is NULL!");
        return 0;
    }

    if (!func_name) {
        LOG("WARNING: func_name is NULL, skipping original call");
        return 0;
    }

    // Gọi hàm gốc để đăng ký hàm hiện tại
    int ret = g_orig_FoxRegisterFunction(this_void_ptr, func_name, func_ptr);
    //LOG("Original function registered: %s (ret=%d)", func_name, ret);

    // Lưu LuaInterface* ngay lần đầu (điểm mấu chốt để lấy this_ptr)
    if (g_lua_interface.load() == nullptr) {
        g_lua_interface.store(this_ptr);
        LOG("[FoxRegisterFunction_Hook] Lưu LuaInterface* = %p", this_ptr);
    }

    // Chỉ inject một lần duy nhất sau ScriptGlobal_Format
    if (!g_lua_injected.load(std::memory_order_acquire) && 
        strcmp(func_name, "ScriptGlobal_Format") == 0) {
        
        pthread_mutex_lock(&g_lua_mutex);
        if (!g_lua_injected.load(std::memory_order_acquire)) {
            LOG("=== BATCH INJECTING LUA FUNCTIONS ===");
            
            // Định nghĩa mảng các hàm Lua cần inject
            const struct {
                const char* name;
                lua_CFunction func;
            } luaFunctions[] = {
                // ===== THÊM CÁC HÀM LUA CỦA BẠN VÀO ĐÂY =====
                {"LuaFnGetAccountName", LuaFnGetAccountName},
                {"LuaFnEquipTransToNew", LuaFnEquipTransToNew},
                // =============================================
            };
            
            const int numFunctions = sizeof(luaFunctions) / sizeof(luaFunctions[0]);
            int successCount = 0;
            
            for (int i = 0; i < numFunctions; i++) {
                LOG("Injecting [%d/%d] %s...", i + 1, numFunctions, luaFunctions[i].name);
                
                int injectRet = g_orig_FoxRegisterFunction(this_ptr,luaFunctions[i].name,(void*)luaFunctions[i].func);
                
                if (injectRet == 1) {
                    successCount++;
                    LOG("  ✓ %s injected successfully", luaFunctions[i].name);
                } else {
                    LOG("  ✗ %s injection failed (ret=%d)", luaFunctions[i].name, injectRet);
                }
            }
            
            if (successCount == numFunctions) {
                LOG("=== ALL %d LUA FUNCTIONS INJECTED SUCCESSFULLY ===", numFunctions);
            } else {
                LOG("=== INJECTED %d/%d LUA FUNCTIONS (SOME FAILED) ===", successCount, numFunctions);
            }
            
            g_lua_injected.store(true, std::memory_order_release);
        }
        pthread_mutex_unlock(&g_lua_mutex);
    }

    //LOG("<<< FoxRegisterFunction_Hook EXITED, returning %d", ret);
    return ret;
}


/* ============================================================
   HÀM HOOK CHÍNH - TỐI ƯU, CHECK NULL, THREAD SAFE
============================================================ */


static void DebugLog(const char* fmt, ...)
{
    FILE* f = fopen("/home/tlbb/Server/Log/ScriptGlobal_Format.txt", "a+");
    if (!f) return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fclose(f);
}

/* ============================================================
   HÀM THÚ CƯỠI
============================================================ */

// Thêm hàm debug trong hook_all hoặc đầu skill005_hook
static void debug_ride_table_to_file() {
    int* exterior_ride_tbl = g_globals.Exterior_RideTbl_ptr.load(std::memory_order_acquire);
    int* dword_103FD44 = g_globals.dword_103FD44_ptr.load(std::memory_order_acquire);
    int* dword_103FE70 = g_globals.dword_103FE70_ptr.load(std::memory_order_acquire);
    
    if (!exterior_ride_tbl || !dword_103FD44 || !dword_103FE70) return;
    
    int rideCount = *exterior_ride_tbl;
    
    // Mở file riêng
    char filename[256];
    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(filename, sizeof(filename), "/home/tlbb/Server/Log/ride_table_%Y%m%d.txt", &tm_info);
    
    FILE* f = fopen(filename, "w");
    if (!f) return;
    
    fprintf(f, "=== RIDE TABLE DEBUG (Total rides: %d) ===\n", rideCount);
    fprintf(f, "dword_103FD44 address: %p\n", dword_103FD44);
    fprintf(f, "dword_103FE70 address: %p\n", dword_103FE70);
    
    // In ride IDs
    fprintf(f, "\n--- Ride IDs (each entry 340 bytes) ---\n");
    for (int i = 0; i < rideCount; i++) {
        uintptr_t addr = (uintptr_t)dword_103FD44 + (i * 340);
        int ride_id = *(int*)addr;
        fprintf(f, "dword_103FD44 Ride[%d] (offset 0x%x) = %d\n", i, i * 340, ride_id);
    }
    
	// In impacts
	fprintf(f, "\n--- Impacts (85 per ride) ---\n");
	for (int ride_idx = 0; ride_idx < rideCount; ride_idx++) {
		int base_idx = ride_idx * 85;
		int ride_id = *(int*)((uintptr_t)dword_103FD44 + (ride_idx * 340));  // Đúng: mỗi ride cách 340 bytes
		fprintf(f, "\nRide %d (ID %d):\n", ride_idx, ride_id);
		for (int impact_offset = 0; impact_offset < 85; impact_offset++) {
			// dword_103FE70 là mảng int liên tục, mỗi phần tử 4 byte
			fprintf(f, "  impact[%d] = %d\n", impact_offset, dword_103FE70[base_idx + impact_offset]);
		}
	}

	fprintf(f, "\n--- dword_103FE70 full array (total %d entries) ---\n", rideCount * 85);
	// In toàn bộ dword_103FE70 từ đầu đến cuối, không reset về 0
	for (int i = 0; i < rideCount * 85; i++) {
		int ride_idx = i / 85;
		int impact_offset = i % 85;
		fprintf(f, "dword_103FE70[%d] (ride %d, impact %d) = %d\n", 
				i, ride_idx, impact_offset, dword_103FE70[i]);
	}
	
	fprintf(f, "\n");
    
    fclose(f);
    LOG("Ride table dumped to %s", filename);
}

	/*
	LOG("========== skill005_hook ==========");
	LOG("exterior_ride_tbl: %p", exterior_ride_tbl);
	LOG("dword_103FD44: %p", dword_103FD44);
	LOG("dword_103FE70: %p", dword_103FE70);
	LOG("impact_core: %p", impact_core);
	LOG("get_max_speed: %p", get_max_speed);
	LOG("send_impact: %p", send_impact);
	LOG("===================================");
	*/
extern "C"
int64_t skill005_hook(void* _this, unsigned int* a2, int a3) {
    if (!a2) {
        LOG("a2 NULL");
        return 0;
    }
    
    // Load globals
    int* exterior_ride_tbl = g_globals.Exterior_RideTbl_ptr.load(std::memory_order_acquire);
    int* dword_103FD44 = g_globals.dword_103FD44_ptr.load(std::memory_order_acquire);
    int* dword_103FE70 = g_globals.dword_103FE70_ptr.load(std::memory_order_acquire);
    void* impact_core = g_globals.ImpactCore_ptr.load(std::memory_order_acquire);
    auto get_max_speed = (GetExteriorRideMaxSpeed_Type)g_globals.GetExteriorRideMaxSpeed.load(std::memory_order_acquire);
    auto send_impact = (SendImpactToUnit_Type)g_globals.SendImpactToUnit.load(std::memory_order_acquire);
    
    if (!exterior_ride_tbl || !dword_103FD44 || !dword_103FE70 || 
        !impact_core || !get_max_speed || !send_impact) {
        LOG("Missing globals");
        return 0;
    }
    
    LOG("a2=%p, a2[2]=%u", a2, a2 ? a2[2] : 0);
	
	// gọi hàm debug
	//debug_ride_table_to_file();
	
    uint64_t vtbl = *(uint64_t*)a2;
    if (!vtbl) {
        LOG("vtable NULL");
        return 0;
    }

    typedef unsigned int (*CheckType)(unsigned int*);
    CheckType check = (CheckType)(*(uint64_t*)(vtbl + 16));

    if (!check) {
        LOG("check func NULL");
        return 0;
    }

    unsigned int checkResult = check(a2);
    if (checkResult != 1) {
        LOG("checkResult=%u != 1", checkResult);
        return 0;
    }

    uint64_t base = *((uint64_t*)a2 + 1677);
    if (!base) {
        LOG("base NULL");
        return 0;
    }

    int v2 = *(int16_t*)(base + 22534);
    LOG("v2=%d", v2);

    int rideCount = *exterior_ride_tbl;
    LOG("rideCount=%d", rideCount);

    if (rideCount <= 0 || rideCount > 1000) { // giới hạn an toàn
        LOG("rideCount invalid");
        return 0;
    }

    int v4 = 0;
    if (v2 != *dword_103FD44) {
        uintptr_t v3 = (uintptr_t)dword_103FD44;

        for (v4 = 1; v4 < rideCount; v4++) {
            int ride = *(int*)(v3 + (v4 * 340)); // mỗi entry 340 byte
            if (v2 == ride) {
                LOG("Found ride at index %d", v4);
                break;
            }
        }

        if (v4 >= rideCount) {
            LOG("ride not found");
            return 0;
        }
    }
    LOG("v4=%d", v4);

    int ExteriorRideMaxSpeed = get_max_speed((void*)a2);
    LOG("ExteriorRideMaxSpeed=%d", ExteriorRideMaxSpeed);

    int index = 85 * v4;
    if (index < 0 || index > 85 * rideCount) {
        LOG("index out of range");
        return 0;
    }
    LOG("table index=%d", index);

    int v6 = dword_103FE70[index];

    switch (ExteriorRideMaxSpeed) {
        case 20: v6 = dword_103FE70[index + 1]; break;
        case 40: v6 = dword_103FE70[index + 2]; break;
        case 60: v6 = dword_103FE70[index + 3]; break;
        case 70: v6 = dword_103FE70[index + 4]; break;
        case 75: v6 = dword_103FE70[index + 5]; break;
        case 80: v6 = dword_103FE70[index + 6]; break;
        case 85: v6 = dword_103FE70[index + 7]; break;
        case 90: v6 = dword_103FE70[index + 8]; break;
        case 95: v6 = dword_103FE70[index + 9]; break;
        default: break;
    }

    LOG("nImpact (impact id)=%d", v6);

    if (v6 <= 0) {
        LOG("invalid impact");
        return 0;
    }

	int v8 = 0;
	//fix thu cuoi toc do 85%
    //LOG("81 = %d",  index +81);
    //LOG("82 = %d",  index +82);
    //LOG("83 = %d",  index +83);
	// vị trí tốc độ của thú cưỡi trong Exterior_Ride.txt, fix impact thú cưỡi + 85%
	if (dword_103FE70[index + 82] == 85) {
		v8 = send_impact(impact_core, a2, (unsigned short)(v6 + 1), a2[2], 1, 100);
		LOG("Fix impact Ride 85% = %d",  v6 + 1);
	} else {
		v8 = send_impact(impact_core, a2, (unsigned short)v6, a2[2], 1, 100);
	}


    // Gửi impact, +1 theo nhu cầu
    //int v8 = send_impact(impact_core, a2, (unsigned short)v6, a2[2], 1, 100);
    LOG("SendImpact result=%d", v8);

    if (v8 <= 0) {
        LOG("SendImpact FAILED impact=%d", v6);
        return 0;
    }

    LOG("==== skill005_hook EXIT OK ====");
    return 1;
}


/* ============================================================
   HÀM ScriptGlobal_Format
============================================================ */
static void*                      g_trampoline = nullptr;

typedef int64_t (*ScriptGlobal_Format_t)(
    char* dest,
    int len,
    const char* fmt,
    int count,
    ...
);

static ScriptGlobal_Format_t g_orig_ScriptGlobal_Format = nullptr;
#include <execinfo.h> // cho backtrace
// Thêm biến global (đầu file)
static thread_local bool g_format_already_resolved = false;

#include <map>
#include <fstream>

/*
============================================================
                CẤU HÌNH OFFSET (SỬA TẠI ĐÂY)
============================================================
Lấy offset trong IDA:
ImageBase = 0
LuaFnScriptGlobal_Format = 0x8CF6E0
ScriptGlobal_Format      = 0x6B0F70
*/

static const uintptr_t OFFSET_LUA_FN   = 0x8CF6E0;
static const uintptr_t OFFSET_FORMAT   = 0x6B0F70;

/*
============================================================
                    ĐỊNH NGHĨA TYPE
============================================================
Phải đúng calling convention 64-bit (System V ABI)
*/

using LuaFnScriptGlobal_Format_t = int64_t (*)(lua_State*);
using ScriptGlobal_Format_tttt      = int (*)(char*, int, const char*, int, ...);

/*
============================================================
                    BIẾN TOÀN CỤC
============================================================
*/

static LuaFnScriptGlobal_Format_t g_orig_LuaFn = nullptr;
static ScriptGlobal_Format_tttt      g_ScriptGlobal_Formattt = nullptr;
//static void*                      g_trampoline = nullptr;

/*
============================================================
                    DICTIONARY
============================================================
*/

static std::map<std::string, std::string> g_dict;
static std::once_flag g_dict_once;
static bool g_dict_loaded = false;

static void LoadDictionary()
{
    LOG("\n========== LOADING DICTIONARY ==========\n");
    
    Dl_info info;
    if (dladdr((void*)LoadDictionary, &info) && info.dli_fname)
    {
        std::string path(info.dli_fname);
        //LOG("[DICT] SO path: %s\n", path.c_str());
        
        size_t pos = path.find_last_of("/\\"); // Tìm cả / hoặc \ (dự phòng)
        if (pos != std::string::npos)
        {
            // Cắt bỏ tên file .so, giữ lại đường dẫn thư mục
            std::string dir = path.substr(0, pos + 1);
            
            // Thử các đường dẫn khác nhau
            std::vector<std::string> test_paths;
            
            // 1. Config/StrDictionary.txt (cùng thư mục với .so)
            test_paths.push_back(dir + "Config/StrDictionary.txt");
            
            // 2. ../Config/StrDictionary.txt (lên 1 cấp)
            test_paths.push_back(dir + "../Config/StrDictionary.txt");
            
            // 3. ../../Config/StrDictionary.txt (lên 2 cấp)
            test_paths.push_back(dir + "../../Config/StrDictionary.txt");
            
            // 4. Đường dẫn tuyệt đối (nếu biết)
            // test_paths.push_back("/home/tlbb/Server/Config/StrDictionary.txt");
            
            // 5. Fallback: current directory
            test_paths.push_back("Config/StrDictionary.txt");
            
            bool found = false;
            
            for (const auto& test_path : test_paths)
            {
                LOG("[DICT] Trying path: %s\n", test_path.c_str());
                
                std::ifstream file(test_path);
                if (file.is_open())
                {
                    //LOG("[DICT] FOUND at: %s\n", test_path.c_str());
                    
                    std::string line;
                    int line_num = 0;
                    int loaded = 0;
                    
                    while (std::getline(file, line))
                    {
                        line_num++;
                        if (line.empty()) continue;
                        
                        size_t tab = line.find('\t');
                        if (tab == std::string::npos)
                        {
                        //    LOG("[DICT] Line %d: no tab found, skipping\n", line_num);
                            continue;
                        }
                        
                        std::string key = line.substr(0, tab);
                        std::string value = line.substr(tab + 1);
                        
                        // Xóa ký tự xuống dòng cuối
                        while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
                            value.pop_back();
                        
                        g_dict[key] = value;
                        loaded++;
                        
                        if (loaded <= 5) // Log 5 dòng đầu để kiểm tra
                        {
                            LOG("[DICT] Sample %d: key='%s' value='%s'\n", 
                                   loaded, key.c_str(), value.c_str());
                        }
                    }
                    file.close();
                    
                    LOG("[DICT] Loaded %d entries from StrDictionary.txt\n", loaded);
                    LOG("[DICT] First 5 keys: ");
                    int count = 0;
                    for (auto& pair : g_dict)
                    {
                        if (count++ < 5)
                            LOG("%s ", pair.first.c_str());
                        else
                            break;
                    }
                    LOG("\n");
                    
                    g_dict_loaded = true;
                    found = true;
                    break;
                }
            }
            
            if (!found)
            {
                LOG("[DICT] FAILED: Could not find StrDictionary.txt in any location\n");
                LOG("[DICT] Searched paths:\n");
                for (const auto& test_path : test_paths)
                {
                    LOG("  - %s\n", test_path.c_str());
                }
            }
        }
        else
        {
            LOG("[DICT] Cannot determine directory from path: %s\n", path.c_str());
        }
    }
    else
    {
        LOG("[DICT] dladdr failed\n");
    }
    
    LOG("========== DICTIONARY LOAD COMPLETE ==========\n\n");
    fflush(stdout);
}

static const char* LookupDict(const char* key, char* out_buf, size_t buf_size)
{
    LOG("[DICT-LOOKUP] Looking up key: '%s'\n", key ? key : "NULL");
    
    if (!key)
    {
        LOG("[DICT-LOOKUP] Key is NULL\n");
        return nullptr;
    }
    
    std::call_once(g_dict_once, LoadDictionary);
    
    if (!g_dict_loaded)
    {
        LOG("[DICT-LOOKUP] Dictionary not loaded yet\n");
        return nullptr;
    }
    
    auto it = g_dict.find(key);
    if (it != g_dict.end())
    {
        LOG("[DICT-LOOKUP] FOUND: '%s' -> '%s'\n", key, it->second.c_str());
        strncpy(out_buf, it->second.c_str(), buf_size - 1);
        out_buf[buf_size - 1] = '\0';
        return out_buf;
    }
    
    LOG("[DICT-LOOKUP] NOT FOUND: '%s'\n", key);
    return nullptr;
}

/*
============================================================
            HÀM HOOK LuaFnScriptGlobal_Format
============================================================
*/

/*
============================================================
                    HELPER MACROS
============================================================
*/
#define SAFE_STR(s) ((s) && (s)[0] ? (s) : "")
#define SAFE_LOG_STR(s) SafeLogString(s)

static void SafeLogString(const char* str)
{
    if (!str)
    {
        LOG("(null)");
        return;
    }
    
    size_t len = strlen(str);
    if (len == 0)
    {
        LOG("(empty)");
        return;
    }
    
    // Kiểm tra string có hợp lệ không
    bool valid = true;
    for (size_t i = 0; i < len && i < 500; i++)
    {
        if (str[i] == '\0')
        {
            valid = false;
            break;
        }
    }
    
    if (!valid)
    {
        LOG("(invalid string)");
        return;
    }
    
    if (len > 500)
    {
        LOG("%.500s... (total %zu bytes)", str, len);
    }
    else
    {
        LOG("%s", str);
    }
}

extern "C"
int64_t LuaFnScriptGlobal_Format_Hook(lua_State* L)
{
    LOG("=================================================================================");
    LOG("LuaFnScriptGlobal_Format_Hook CALLED");
    LOG("=================================================================================");

    if (!L)
    {
        LOG("ERROR: lua_State is NULL");
        return 0;
    }

    int top = lua_gettop(L);
    LOG("Lua stack top = %d", top);

    if (top < 1)
    {
        LOG("ERROR: Stack too small (<1)");
        lua_pushstring(L, "");
        return 1;
    }

    int paramCount = top - 1;
    const char* key = lua_tostring(L, 1);

    LOG("Original format key = '%s'", key ? key : "NULL");
    LOG("Original format key length = %zu", key ? strlen(key) : 0);
    LOG("Number of additional params = %d", paramCount);

    // Log tất cả các tham số gốc từ Lua
    LOG("----- Original Lua arguments -----");
    for (int i = 1; i <= top; i++)
    {
        int type = lua_type(L, i);
        const char* typeName = "unknown";
        switch (type)
        {
            case LUA_TNIL: typeName = "nil"; break;
            case LUA_TBOOLEAN: typeName = "boolean"; break;
            case LUA_TNUMBER: typeName = "number"; break;
            case LUA_TSTRING: typeName = "string"; break;
            default: typeName = "other"; break;
        }
        
        if (type == LUA_TNUMBER)
        {
            double num = lua_tonumber(L, i);
            LOG("  Arg[%d]: type=%s, value=%f", i, typeName, num);
        }
        else if (type == LUA_TSTRING)
        {
            const char* val = lua_tostring(L, i);
            LOG("  Arg[%d]: type=%s, value='%s'", i, typeName, val ? val : "NULL");
        }
        else
        {
            LOG("  Arg[%d]: type=%s", i, typeName);
        }
    }

    if (!key)
    {
        LOG("ERROR: Key is NULL, pushing empty string");
        lua_pushstring(L, "");
        return 1;
    }

    if (paramCount < 0)
    {
        LOG("ERROR: paramCount < 0");
        lua_pushstring(L, key);
        return 1;
    }

    if (paramCount > 9)
    {
        LOG("ERROR: Too many params (%d > 9)", paramCount);
        lua_pushstring(L, "LUA: too many parameters!");
        return 1;
    }

    // Buffer lớn cho xử lý
    char resolved_format[32768] = {0};
    char resolved_args[9][32768] = {{0}};
    const char* final_args[9] = {nullptr};

    LOG("----- STEP 1: Resolve format key from dictionary -----");
    
    // Xử lý format key nếu nó là dạng #{key}
    const char* actual_format = key;
    bool is_resolved_format = false;
    
    if (key[0] == '#' && key[1] == '{')
    {
        size_t len = strlen(key);
        if (len > 3 && key[len - 1] == '}')
        {
            char dict_key[256] = {0};
            strncpy(dict_key, key + 2, len - 3);
            dict_key[len - 3] = '\0';
            
            LOG("Format key is #{...} pattern, extracting key: '%s'", dict_key);
            
            char dict_value[32768] = {0};
            const char* resolved = LookupDict(dict_key, dict_value, sizeof(dict_value));
            
            if (resolved)
            {
                LOG("Resolved format key length: %zu bytes", strlen(resolved));
                strncpy(resolved_format, resolved, sizeof(resolved_format) - 1);
                resolved_format[sizeof(resolved_format) - 1] = '\0';
                actual_format = resolved_format;
                is_resolved_format = true;
            }
            else
            {
                LOG("Key not found in dictionary, keeping original format");
                actual_format = key;
            }
        }
    }

    LOG("----- STEP 2: Resolve parameters from dictionary -----");
    
    auto IsSpecialMacro = [](const char* macro_content) -> bool {
        if (!macro_content) return false;
        
        const char* special_prefixes[] = {
            "_INFOAIM", "_ITEM", "_NPC", "_MAP", "_ZONE", 
            "_SKILL", "_BUFF", "_QUEST", "_DIALOG", "_SHOP"
        };
        
        for (const char* prefix : special_prefixes)
        {
            if (strncmp(macro_content, prefix, strlen(prefix)) == 0)
                return true;
        }
        
        return (strchr(macro_content, ',') != nullptr);
    };

    // Xử lý từng tham số
    for (int i = 2; i <= paramCount + 1; ++i)
    {
        int idx = i - 2;
        int type = lua_type(L, i);
        const char* s = nullptr;
        char num_buf[64] = {0};
        
        LOG("Processing param[%d] (Lua index %d, type=%d)", idx, i, type);
        
        if (type == LUA_TNUMBER)
        {
            double num = lua_tonumber(L, i);
            snprintf(num_buf, sizeof(num_buf), "%g", num);
            s = num_buf;
            LOG("  Number value: %f -> '%s'", num, s);
        }
        else if (type == LUA_TSTRING)
        {
            s = lua_tostring(L, i);
            LOG("  String value: '%s'", s ? s : "NULL");
        }
        else if (type == LUA_TBOOLEAN)
        {
            s = lua_toboolean(L, i) ? "true" : "false";
            LOG("  Boolean value: '%s'", s);
        }
        else
        {
            s = "";
            LOG("  Using empty string for type %d", type);
        }
        
        if (!s) s = "";
        
        // Copy vào buffer an toàn
        strncpy(resolved_args[idx], s, sizeof(resolved_args[idx]) - 1);
        resolved_args[idx][sizeof(resolved_args[idx]) - 1] = '\0';
        final_args[idx] = resolved_args[idx];
    }

    LOG("----- STEP 3: Replace %%s placeholders in resolved format -----");

    char final_buffer[65536] = {0};
    char temp_buffer[131072] = {0};

    // Copy format đã resolve vào buffer
    strncpy(final_buffer, actual_format, sizeof(final_buffer) - 1);
    final_buffer[sizeof(final_buffer) - 1] = '\0';
    
    size_t fmt_len = strlen(final_buffer);
    LOG("Format after dictionary resolution: length=%zu", fmt_len);

    // Đếm số lượng placeholder cần xử lý
    int max_placeholder = -1;
    for (int i = 0; i < paramCount; i++)
    {
        char placeholder[16];
        snprintf(placeholder, sizeof(placeholder), "%%s%d", i);
        if (strstr(final_buffer, placeholder))
        {
            max_placeholder = i;
            LOG("Found placeholder %s", placeholder);
        }
    }
    
    LOG("Max placeholder found: %s%d", max_placeholder >= 0 ? "%s" : "none", max_placeholder);
    
    // CẢNH BÁO nếu số lượng placeholder không khớp
    if (max_placeholder + 1 > paramCount)
    {
        LOG("WARNING: Format needs %d params but only %d provided!", max_placeholder + 1, paramCount);
    }

    // Xử lý từng %s placeholder
    for (int i = 0; i < paramCount; i++)
    {
        char placeholder[16];
        snprintf(placeholder, sizeof(placeholder), "%%s%d", i);
        
        // Tìm và thay thế TẤT CẢ occurrences
        char* pos;
        int replaced = 0;
        while ((pos = strstr(final_buffer, placeholder)) != nullptr)
        {
            LOG("Replacing %s with '%s'", placeholder, final_args[i]);
            
            size_t before_len = pos - final_buffer;
            size_t placeholder_len = strlen(placeholder);
            size_t replace_len = strlen(final_args[i]);
            
            // Tạo buffer mới
            char new_buffer[65536] = {0};
            
            // Copy phần trước
            strncpy(new_buffer, final_buffer, before_len);
            new_buffer[before_len] = '\0';
            
            // Thêm text thay thế
            strcat(new_buffer, final_args[i]);
            
            // Thêm phần sau
            strcat(new_buffer, pos + placeholder_len);
            
            // Copy lại
            strncpy(final_buffer, new_buffer, sizeof(final_buffer) - 1);
            final_buffer[sizeof(final_buffer) - 1] = '\0';
            
            replaced++;
        }
        
        if (replaced > 0)
        {
            LOG("Replaced %d occurrence(s) of %s", replaced, placeholder);
        }
    }

    size_t final_len = strlen(final_buffer);
    LOG("----- Final string length: %zu bytes -----", final_len);

    // Kiểm tra xem còn placeholder nào không
    if (strstr(final_buffer, "%s"))
    {
        LOG("WARNING: Still has %%s placeholders after replacement!");
        
        // Log chi tiết các placeholder còn lại
        for (int i = 0; i < paramCount; i++)
        {
            char placeholder[16];
            snprintf(placeholder, sizeof(placeholder), "%%s%d", i);
            if (strstr(final_buffer, placeholder))
            {
                LOG("  Remaining: %s", placeholder);
            }
        }
    }

    // ============================================================
    // GỌI SCRIPTGLOBAL_FORMAT GỐC
    // ============================================================
    LOG("----- Lua Hook: Đã xử lý xong, đánh dấu flag -----");
    g_format_already_resolved = true;

    char dest[320] = {0};
    int ok = 0;

    if (!g_ScriptGlobal_Formattt)
    {
        LOG("ERROR: g_ScriptGlobal_Formattt is NULL!");
        g_format_already_resolved = false;
        lua_pushstring(L, final_buffer); // Fallback
        return 1;
    }

    LOG("----- Calling original ScriptGlobal_Format -----");
    LOG("  final_buffer length=%zu, count=%d", final_len, paramCount);

    if (final_len >= sizeof(dest))
    {
        LOG("  WARNING: Buffer too large (%zu >= %zu), will be truncated!", 
            final_len, sizeof(dest));
    }

    // QUAN TRỌNG: Gọi ScriptGlobal_Format với count=0 vì đã xử lý xong placeholder
    ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 0);
    
    LOG("  ScriptGlobal_Format returned: %d", ok);
    g_format_already_resolved = false;

    if (!ok)
    {
        LOG("ERROR: Format failed, using final_buffer as fallback");
        lua_pushstring(L, final_buffer);
        return 1;
    }

    // Log kết quả để debug
    size_t dest_len = strlen(dest);
    LOG("Result length: %zu bytes", dest_len);
    if (dest_len < 500)
    {
        LOG("Result content: %s", dest);
    }

    lua_pushstring(L, dest);
    LOG("Pushed result to Lua stack");
    LOG("=================================================================================");
    
    return 1;
}


extern "C"
int64_t LuaFnScriptGlobal_Format_Hook_old(lua_State* L)
{

    LOG("=================================================================================");
    LOG("LuaFnScriptGlobal_Format_Hook CALLED");
    LOG("=================================================================================");

    if (!L)
    {
        LOG("ERROR: lua_State is NULL");
        return 0;
    }

    int top = lua_gettop(L);
    LOG("Lua stack top = %d", top);

    if (top < 1)
    {
        LOG("ERROR: Stack too small (<1)");
        lua_pushstring(L, "");
        return 1;
    }

    int paramCount = top - 1;
    const char* key = lua_tostring(L, 1);

    LOG("Original format key = '%s'", key ? key : "NULL");
    LOG("Original format key length = %zu", key ? strlen(key) : 0);
    LOG("Number of additional params = %d", paramCount);

    // Log tất cả các tham số gốc từ Lua
    LOG("----- Original Lua arguments -----");
    for (int i = 1; i <= top; i++)
    {
        int type = lua_type(L, i);
        const char* typeName = "unknown";
        switch (type)
        {
            case LUA_TNIL: typeName = "nil"; break;
            case LUA_TBOOLEAN: typeName = "boolean"; break;
            case LUA_TLIGHTUSERDATA: typeName = "lightuserdata"; break;
            case LUA_TNUMBER: typeName = "number"; break;
            case LUA_TSTRING: typeName = "string"; break;
            case LUA_TTABLE: typeName = "table"; break;
            case LUA_TFUNCTION: typeName = "function"; break;
            case LUA_TUSERDATA: typeName = "userdata"; break;
            case LUA_TTHREAD: typeName = "thread"; break;
        }
        
        if (type == LUA_TSTRING)
        {
            const char* val = lua_tostring(L, i);
            size_t val_len = val ? strlen(val) : 0;
            LOG("  Arg[%d]: type=%s, length=%zu", i, typeName, val_len);
            if (val_len > 0)
            {
                if (val_len > 200)
                {
                    LOG("    First 200 chars: %.200s...", val);
                }
                else
                {
                    LOG("    Value: '%s'", val);
                }
            }
        }
        else
        {
            LOG("  Arg[%d]: type=%s", i, typeName);
        }
    }

    if (!key)
    {
        LOG("ERROR: Key is NULL, pushing empty string");
        lua_pushstring(L, "");
        return 1;
    }

    if (paramCount < 0)
    {
        LOG("ERROR: paramCount < 0");
        lua_pushstring(L, key);
        return 1;
    }

    if (paramCount > 9)
    {
        LOG("ERROR: Too many params (%d > 9)", paramCount);
        lua_pushstring(L, "LUA: too many parameters!");
        return 1;
    }

    // Buffer lớn cho xử lý (32KB)
    char resolved_format[32768] = {0};
    char resolved_args[9][32768] = {{0}};
    const char* final_args[9] = {nullptr};

    LOG("----- STEP 1: Resolve format key from dictionary -----");
    
    // Xử lý format key nếu nó là dạng #{key}
    const char* actual_format = key;
    
    if (key[0] == '#' && key[1] == '{')
    {
        size_t len = strlen(key);
        if (len > 3 && key[len - 1] == '}')
        {
            char dict_key[256] = {0};
            strncpy(dict_key, key + 2, len - 3);
            dict_key[len - 3] = '\0';
            
            LOG("Format key is #{...} pattern, extracting key: '%s'", dict_key);
            
            char dict_value[32768] = {0};
            const char* resolved = LookupDict(dict_key, dict_value, sizeof(dict_value));
            
            if (resolved)
            {
                LOG("Resolved format key: ->");
                LOG("  Length: %zu bytes", strlen(resolved));
                if (strlen(resolved) > 200)
                {
                    LOG("  First 200: %.200s...", resolved);
                }
                else
                {
                    LOG("  Value: '%s'", resolved);
                }
                strncpy(resolved_format, resolved, sizeof(resolved_format) - 1);
                actual_format = resolved_format;
            }
            else
            {
                LOG("Key not found in dictionary, keeping original format");
                actual_format = key;
            }
        }
        else
        {
            LOG("Malformed #{...} pattern, keeping original");
            actual_format = key;
        }
    }
    else
    {
        LOG("Format key is not #{...} pattern, keeping original");
        actual_format = key;
    }

    LOG("----- STEP 2: Resolve parameters from dictionary -----");
    
    auto IsSpecialMacro = [](const char* macro_content) -> bool {
        if (!macro_content) return false;
        
        const char* special_prefixes[] = {
            "_INFOAIM", "_ITEM", "_NPC", "_MAP", "_ZONE", 
            "_SKILL", "_BUFF", "_QUEST", "_DIALOG", "_SHOP",
            "_INFO", "_AIM"
        };
        
        for (const char* prefix : special_prefixes)
        {
            if (strncmp(macro_content, prefix, strlen(prefix)) == 0)
            {
                return true;
            }
        }
        
        if (strchr(macro_content, ','))
        {
            return true;
        }
        
        return false;
    };

    // Xử lý từng tham số
    for (int i = 2; i <= paramCount + 1; ++i)
    {
        int idx = i - 2;
        const char* s = lua_tostring(L, i);
        
        LOG("Processing param[%d] (Lua index %d):", idx, i);
        
        if (!s)
        {
            LOG("  -> param[%d] is NULL, using empty string", idx);
            final_args[idx] = "";
            continue;
        }

        size_t s_len = strlen(s);
        LOG("  Original length: %zu bytes", s_len);
        
        if (s_len > 200)
        {
            LOG("  First 200 chars: %.200s...", s);
        }
        else
        {
            LOG("  Original text: '%s'", s);
        }

        // Copy vào buffer tạm để xử lý
        char temp[32768] = {0};
        strncpy(temp, s, sizeof(temp) - 1);
        
        // XỬ LÝ MACRO TRONG THAM SỐ
        bool has_macro = false;
        char processed[32768] = {0};
        char* current_pos = temp;
        char* out_pos = processed;
        
        LOG("  Scanning for macros and color codes...");
        
        int macro_count = 0;
        int color_count = 0;
        
        while (*current_pos)
        {
            if (current_pos[0] == '#' && current_pos[1] == '{')
            {
                // Tìm kết thúc macro
                char* end_macro = strchr(current_pos + 2, '}');
                if (end_macro)
                {
                    macro_count++;
                    has_macro = true;
                    
                    // Trích nội dung macro
                    char macro_content[4096] = {0};
                    size_t content_len = end_macro - (current_pos + 2);
                    strncpy(macro_content, current_pos + 2, content_len);
                    macro_content[content_len] = '\0';
                    
                    LOG("  Found macro #%d: #{%.*s}", 
                        macro_count, (int)content_len, current_pos + 2);
                    
                    // Kiểm tra macro đặc biệt
                    if (IsSpecialMacro(macro_content))
                    {
                        LOG("    Keeping special macro as is");
                        // Copy nguyên macro
                        size_t macro_len = end_macro - current_pos + 1;
                        strncpy(out_pos, current_pos, macro_len);
                        out_pos += macro_len;
                    }
                    else
                    {
                        // Tra dictionary
                        char dict_buf[32768] = {0};
                        LOG("    Looking up in dictionary: '%s'", macro_content);
                        
                        const char* resolved = LookupDict(macro_content, dict_buf, sizeof(dict_buf));
                        if (resolved)
                        {
                            size_t resolved_len = strlen(resolved);
                            LOG("    RESOLVED: -> text length %zu", resolved_len);
                            if (resolved_len > 200)
                            {
                                LOG("      First 200: %.200s...", resolved);
                            }
                            else
                            {
                                LOG("      Text: '%s'", resolved);
                            }
                            // Copy nội dung đã resolve
                            strcpy(out_pos, resolved);
                            out_pos += resolved_len;
                        }
                        else
                        {
                            LOG("    NOT FOUND in dictionary, keeping original macro");
                            // Copy nguyên macro
                            size_t macro_len = end_macro - current_pos + 1;
                            strncpy(out_pos, current_pos, macro_len);
                            out_pos += macro_len;
                        }
                    }
                    
                    current_pos = end_macro + 1;
                }
                else
                {
                    *out_pos++ = *current_pos++;
                }
            }
            else if (current_pos[0] == '#' && 
                     (current_pos[1] == 'R' || current_pos[1] == 'G' || 
                      current_pos[1] == 'Y' || current_pos[1] == 'W' || 
                      current_pos[1] == 'r' || current_pos[1] == 'b' ||
                      current_pos[1] == 'c' || current_pos[1] == 'm'))
            {
                // Mã màu
                color_count++;
                *out_pos++ = *current_pos++;
                *out_pos++ = *current_pos++;
            }
            else
            {
                *out_pos++ = *current_pos++;
            }
        }
        *out_pos = '\0';
        
        size_t processed_len = strlen(processed);
        LOG("  Processing complete:");
        LOG("    Macros found: %d", macro_count);
        LOG("    Color codes: %d", color_count);
        LOG("    Final length: %zu bytes", processed_len);
        
        if (has_macro)
        {
            if (processed_len > 200)
            {
                LOG("  After processing (first 200): %.200s...", processed);
            }
            else
            {
                LOG("  After processing: '%s'", processed);
            }
            strncpy(resolved_args[idx], processed, sizeof(resolved_args[idx]) - 1);
            final_args[idx] = resolved_args[idx];
        }
        else
        {
            LOG("  No macros found, keeping original");
            final_args[idx] = s;
        }
    }

    LOG("----- STEP 3: Replace %s placeholders in resolved format -----");

    char final_buffer[65536] = {0};
    char temp_buffer[131072] = {0}; // Buffer tạm lớn hơn

    // Copy format đã resolve vào buffer
    strncpy(final_buffer, actual_format, sizeof(final_buffer) - 1);
    LOG("Format after dictionary resolution:");
    LOG("  Length: %zu bytes", strlen(final_buffer));
    if (strlen(final_buffer) > 200)
    {
        LOG("  First 200: %.200s...", final_buffer);
        // Tìm vị trí của %s0 nếu có
        char* pos = strstr(final_buffer, "%s0");
        if (pos)
        {
            LOG("  Found %%s0 at position %ld", pos - final_buffer);
        }
    }
    else
    {
        LOG("  Content: '%s'", final_buffer);
    }

    // Kiểm tra xem có placeholder %s nào không
    bool has_placeholder = (strstr(final_buffer, "%s") != nullptr);
    LOG("Format has %%s placeholders: %s", has_placeholder ? "YES" : "NO");

    if (!has_placeholder)
    {
        LOG("WARNING: Format has no %%s placeholders but we have %d parameters!", paramCount);
    }

// Xử lý từng %s placeholder
for (int i = 0; i < paramCount; i++)
{
    char placeholder[16];
    snprintf(placeholder, sizeof(placeholder), "%%s%d", i);
    
    LOG("Looking for placeholder: %s", placeholder);
    
    // Tìm tất cả occurrences của placeholder này
    int found_count = 0;
    char* search_pos = final_buffer;
    
    while (true)
    {
        char* pos = strstr(search_pos, placeholder);
        if (!pos)
            break;
        
        found_count++;
        LOG("  Found %s at position %ld (occurrence #%d)", 
            placeholder, pos - final_buffer, found_count);
        
        // Lấy giá trị thay thế - AN TOÀN
        const char* replace_text = "";
        
        // Kiểm tra an toàn
        if (i < paramCount && final_args[i] != nullptr)
        {
            replace_text = final_args[i];
            if (strlen(replace_text) == 0)
            {
                LOG("  Arg[%d] is empty string, using empty string", i);
                replace_text = "";
            }
            else
            {
                size_t replace_len = strlen(replace_text);
                LOG("  Replacing %s with text of length %zu", placeholder, replace_len);
            }
        }
        else
        {
            LOG("  Arg[%d] is missing or out of range, using empty string", i);
            replace_text = "";
        }
        
        // Tính toán các độ dài
        size_t before_len = pos - final_buffer;
        size_t placeholder_len = strlen(placeholder);
        size_t replace_len = strlen(replace_text);
        size_t after_len = strlen(pos + placeholder_len);
        
        // Kiểm tra overflow
        if (before_len + replace_len + after_len >= sizeof(temp_buffer))
        {
            LOG("    ERROR: Buffer overflow would occur, skipping replacement");
            LOG("    Required: %zu, Max: %zu", before_len + replace_len + after_len, sizeof(temp_buffer));
            break;
        }
        
        // Copy phần trước placeholder
        strncpy(temp_buffer, final_buffer, before_len);
        temp_buffer[before_len] = '\0';
        
        // Thêm text thay thế
        strncat(temp_buffer, replace_text, sizeof(temp_buffer) - strlen(temp_buffer) - 1);
        
        // Thêm phần sau placeholder
        strncat(temp_buffer, pos + placeholder_len, 
                sizeof(temp_buffer) - strlen(temp_buffer) - 1);
        
        // Copy back vào final_buffer
        strncpy(final_buffer, temp_buffer, sizeof(final_buffer) - 1);
        final_buffer[sizeof(final_buffer) - 1] = '\0';
        
        LOG("    After replacement (buffer now %zu bytes)", strlen(final_buffer));
        
        // Tiếp tục tìm kiếm từ vị trí mới
        search_pos = final_buffer + before_len + replace_len;
    }
    
    if (found_count == 0)
    {
        LOG("  Placeholder %s not found in current buffer", placeholder);
    }
    else
    {
        LOG("  Replaced %d occurrence(s) of %s", found_count, placeholder);
    }
}

    size_t final_len = strlen(final_buffer);
    LOG("----- Final string after all processing -----");
    LOG("  Total length: %zu bytes", final_len);
    
    if (final_len > 0)
    {
        if (final_len > 500)
        {
            LOG("  First 500 chars: %.500s...", final_buffer);
            LOG("  Last 500 chars: ...%s", final_buffer + final_len - 500);
        }
        else
        {
            LOG("  Full text: %s", final_buffer);
        }
    }

    // Kiểm tra xem còn placeholder nào không
    if (strstr(final_buffer, "%s"))
    {
        LOG("WARNING: Still has %%s placeholders after replacement!");
    }

    // ============================================================
    // GỌI SCRIPTGLOBAL_FORMAT GỐC
    // ============================================================
	LOG("----- Lua Hook: Đã xử lý xong, đánh dấu flag -----");
	g_format_already_resolved = true;

	char dest[320] = {0};
	int ok = 0;

	if (!g_ScriptGlobal_Formattt)
	{
		LOG("ERROR: g_ScriptGlobal_Formattt is NULL!");
		g_format_already_resolved = false; // Reset flag trước khi return
		lua_pushstring(L, "Format function NULL!");
		return 1;
	}

	LOG("----- Calling original ScriptGlobal_Format -----");
	LOG("  dest=%p, dest_len=%d", dest, (int)sizeof(dest));
	LOG("  final_buffer length=%zu", final_len);
	LOG("  count=%d", paramCount);
	LOG("  flag is set, ScriptGlobal_Format will copy directly");

    // CẢNH BÁO: Nếu final_buffer quá dài, có thể bị cắt bớt
    if (final_len >= sizeof(dest))
    {
        LOG("  WARNING: final_buffer (%zu) > dest buffer (%zu), will be truncated!", 
            final_len, sizeof(dest));
    }

    // Gọi ScriptGlobal_Format gốc với final_buffer đã xử lý hoàn chỉnh
    // LƯU Ý: Không truyền args vì đã xử lý xong hết placeholder
    switch (paramCount)
    {
        case 0:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 0);
            break;
        case 1:
            // Vẫn truyền args nhưng có thể không cần
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 1, final_args[0]);
            break;
        case 2:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 2,
                                       final_args[0], final_args[1]);
            break;
        case 3:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 3,
                                       final_args[0], final_args[1], final_args[2]);
            break;
        case 4:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 4,
                                       final_args[0], final_args[1], final_args[2], final_args[3]);
            break;
        case 5:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 5,
                                       final_args[0], final_args[1], final_args[2], 
                                       final_args[3], final_args[4]);
            break;
        case 6:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 6,
                                       final_args[0], final_args[1], final_args[2], 
                                       final_args[3], final_args[4], final_args[5]);
            break;
        case 7:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 7,
                                       final_args[0], final_args[1], final_args[2], 
                                       final_args[3], final_args[4], final_args[5],
                                       final_args[6]);
            break;
        case 8:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 8,
                                       final_args[0], final_args[1], final_args[2], 
                                       final_args[3], final_args[4], final_args[5],
                                       final_args[6], final_args[7]);
            break;
        case 9:
            ok = g_ScriptGlobal_Formattt(dest, sizeof(dest), final_buffer, 9,
                                       final_args[0], final_args[1], final_args[2], 
                                       final_args[3], final_args[4], final_args[5],
                                       final_args[6], final_args[7], final_args[8]);
            break;
        default:
            LOG("ERROR: Unsupported paramCount %d", paramCount);
            ok = 0;
            break;
    }
	LOG("  ScriptGlobal_Format returned: %d", ok);

	// Reset flag SAU KHI gọi xong
	g_format_already_resolved = false;


    if (!ok)
    {
        LOG("ERROR: Format failed");
        char err[256];
        snprintf(err, sizeof(err),
                 "Format fail! Key=%s Param=%d",
                 key, paramCount);

        lua_pushstring(L, err);
        LOG("=================================================================================");
        return 1;
    }

    size_t dest_len = strlen(dest);
    LOG("----- Result buffer -----");
    LOG("  Length: %zu bytes", dest_len);
    
    if (dest_len > 0)
    {
        if (dest_len > 500)
        {
            LOG("  First 500 chars: %.500s...", dest);
        }
        else
        {
            LOG("  Content: '%s'", dest);
        }
    }

	
    lua_pushstring(L, dest);
    LOG("Pushed result to Lua stack");
    LOG("=================================================================================");
    
    return 1;
}

int64_t ScriptGlobal_Format_Hook(
        char *dest,
        int a2,
        const char *a3,
        int a4,
        ...)
{
    LOG("==================================================");
    LOG("ScriptGlobal_Format_Hook ENTER");
    LOG("==================================================");
    
    LOG("Parameters:");
    LOG("  dest     = %p", dest);
    LOG("  dest_len = %d", a2);
    LOG("  format   = '%s'", SAFE_STR(a3));
    LOG("  count    = %d", a4);
    LOG("  format_already_resolved = %s", g_format_already_resolved ? "YES" : "NO");

    // VALIDATE INPUT
    if (!dest || a2 <= 0)
    {
        LOG("ERROR: Invalid dest buffer");
        return 0;
    }
    
    if (!a3)
    {
        LOG("ERROR: format is NULL");
        if (dest && a2 > 0)
        {
            memset(dest, 0, a2);
        }
        return 0;
    }

    // Nếu format đã được resolve, copy trực tiếp và BỎ QUA tham số
    if (g_format_already_resolved)
    {
        LOG("✓ Format already resolved, copying directly (ignoring %d parameters)", a4);
        size_t len = strlen(a3);
        if (len < (size_t)a2)
        {
            strcpy(dest, a3);
            LOG("  Direct copy successful: %zu bytes", len);
            LOG("  Content: %s", dest);
            return 1;
        }
        else
        {
            LOG("ERROR: Buffer too small for direct copy");
            LOG("  Need: %zu, Have: %d", len, a2);
            return 0;
        }
    }

    // CHỈ đến đây mới xử lý đóng gói tham số
    LOG("Format needs packaging with %d parameters", a4);
    
    va_list va;
    va_start(va, a4);
    
    // Clear destination
    if (dest && a2 > 0)
    {
        memset(dest, 0, a2);
    }

    size_t fmt_len = strlen(a3);
    size_t v15 = fmt_len + 1;
    int v16 = (int)v15 - 1;

    if (a2 <= (int)v15 - 1)
    {
        LOG("ERROR: Buffer too small for format");
        LOG("  Buffer size: %d, Need: %d", a2, (int)v15 - 1);
        va_end(va);
        return 0;
    }

    // Copy format string
    LOG("Copying format string (%d bytes)", (int)v15 - 2);
    memcpy(dest, a3, (int)v15 - 2);

    if (a4 <= 0)
    {
        LOG("No parameters, adding closing '}'");
        dest[v16 - 1] = 125; // '}'
        va_end(va);
        return 1;
    }

    LOG("Adding parameter marker '*' at position %d", v16 - 1);
    dest[v16 - 1] = 42; // '*'
    int v18 = v16 + 2;

    if (a2 <= v16 + 2)
    {
        LOG("ERROR: Buffer too small after header");
        LOG("  Need: %d, Have: %d", v16 + 2, a2);
        va_end(va);
        return 0;
    }

    int v19 = 0;
    int v20 = 0;

    LOG("----- Processing %d parameters -----", a4);

    // Xử lý từng parameter
    while (v19 < a4)
    {
        const char* v23 = va_arg(va, const char*);
        
        LOG("Parameter[%d]: %s", v19, SAFE_STR(v23));
        
        // XỬ LÝ AN TOÀN: nếu NULL thì dùng chuỗi rỗng
        const char* safe_param = v23 ? v23 : "";
        
        size_t v24 = strlen(safe_param) + 1;
        uint8_t v25 = (uint8_t)(v24 - 1);
        
        LOG("  Data length: %d bytes", v25);
        LOG("  Value: '%s'", safe_param);

        // Kiểm tra overflow
        if (v18 + 2 + v25 >= a2)
        {
            LOG("ERROR: Buffer overflow");
            LOG("  Current position: %d", v18);
            LOG("  Need: %d more bytes", 2 + v25);
            LOG("  Buffer size: %d", a2);
            va_end(va);
            return 0;
        }

        // Ghi header
        dest[v18] = 42;              // '*'
        dest[v18 + 1] = v25;
        
        LOG("  Header: '*' at [%d], length=%d at [%d]", v18, v25, v18 + 1);
        
        // Ghi data
        char* v27 = &dest[v18 + 2];
        unsigned int v28 = v25;

        if (v28 >= 8)
        {
            memcpy(v27, safe_param, v28);
        }
        else if (v28 & 4)
        {
            *(uint32_t*)v27 = *(uint32_t*)safe_param;
            if (v28 > 4)
                *(uint32_t*)&v27[v28 - 4] = *(uint32_t*)&safe_param[v28 - 4];
        }
        else if (v28)
        {
            *v27 = *safe_param;
            if (v28 & 2)
                *(uint16_t*)&v27[v28 - 2] = *(uint16_t*)&safe_param[v28 - 2];
        }

        v18 += 2 + v25;
        v20 += v25 + 2;
        
        LOG("  After parameter[%d]: v18=%d, v20=%d", v19, v18, v20);
        
        v19++;
    }

    LOG("----- Parameter processing complete -----");
    LOG("v20 total = %d", v20);

    // Xử lý padding nếu cần (giữ logic gốc)
    if (v20 == 56)
    {
        LOG("v20 == 56, adding space padding");
        if (v18 >= a2)
        {
            LOG("ERROR: No space for padding");
            va_end(va);
            return 0;
        }
        dest[v18++] = 32; // space padding
        v20++;
        LOG("  Added space at position %d, v20=%d", v18 - 1, v20);
    }

    // Thêm closing '}'
    if (v18 < a2)
    {
        LOG("Adding closing '}' at position %d", v18);
        dest[v18] = 125; // '}'
        
        if (v18 + 1 < a2)
        {
            LOG("Adding null terminator at position %d", v18 + 1);
            dest[v18 + 1] = 0;
            
            LOG("Writing metadata:");
            LOG("  dest[%d] = arg_count = %d", v16, v19);
            LOG("  dest[%d] = total_len = %d", v16 + 1, v20 + 3);
            
            dest[v16] = v19;
            dest[v16 + 1] = v20 + 3;
            
            LOG("Packaging complete: %d bytes written", v18 + 2);
            
            // Log hex dump để debug (tùy chọn)
            LOG("Hex dump of final buffer:");
            char hex_line[256] = {0};
            int hex_pos = 0;
            for (int i = 0; i < v18 + 2; i++)
            {
                hex_pos += sprintf(hex_line + hex_pos, "%02X ", (unsigned char)dest[i]);
                if ((i + 1) % 16 == 0)
                {
                    LOG("  %s", hex_line);
                    hex_pos = 0;
                }
            }
            if (hex_pos > 0)
            {
                LOG("  %s", hex_line);
            }
            
            va_end(va);
            return 1;
        }
        else
        {
            LOG("ERROR: No space for null terminator at %d", v18 + 1);
        }
    }
    else
    {
        LOG("ERROR: v18 (%d) >= a2 (%d), cannot add closing '}'", v18, a2);
    }

    va_end(va);
    return 0;
}




/* ============================================================
   INITIALIZATION - THREAD SAFE, CHỈ 1 LẦN
============================================================ */
class ServerHook {
private:
    static ServerHook* instance;
    static pthread_once_t once_control;
    
    ServerHook() {
        // Khởi tạo logger
        g_logger = std::make_unique<AsyncLogger>();
        g_logger->init();
        
        LOG("=====================================");
        LOG("     MR TIIIIIIIII - 25.02.2026      ");
        LOG("     Server Hook initialized         ");
        LOG("=====================================");
        
        // Resolve symbols
        resolve_symbols();
        
        // Hook các function cần thiết
        hook_all();
    }
    
	/* ============================================================
	  HOOK_ALL
	============================================================ */
	void hook_all() {
		//------------------------------------------FoxLuaScript::RegisterFunction----------------------------------------------//
		// Hook FoxLuaScript::RegisterFunction ngay lập tức
		LOG("Attempting to hook FoxLuaScript::RegisterFunction...");
		void* fox_addr = dlsym(RTLD_DEFAULT, "_ZN12FoxLuaScript16RegisterFunctionEPKcPv");
		LOG("FoxLuaScript::RegisterFunction address: %p", fox_addr);

		if (fox_addr) {
			LOG("Creating trampoline...");
			void* trampoline = HookEngine::create_trampoline(fox_addr, 32);
			if (trampoline) {
				g_orig_FoxRegisterFunction = (FoxLuaScript_RegisterFunction_t)trampoline;
				LOG("Patching...");
				HookEngine::patch_code_safe(fox_addr, (void*)FoxRegisterFunction_Hook);
				LOG("FoxLuaScript::RegisterFunction hooked with trampoline");
			} else {
				LOG("ERROR: Cannot create trampoline");
			}
		} else {
			LOG("ERROR: Cannot find FoxLuaScript::RegisterFunction");
		}
		//----------------------------------------ScriptGlobal_Format------------------------------------------------//
		
		// Tạo thread riêng để hook skill sau x giây (không block thread chính)
		std::thread([this]() {
			//sleep(45); // Hoặc 
		//	std::this_thread::sleep_for(std::chrono::seconds(45));
			// Hook FoxLuaScript::RegisterFunction ngay lập tức
			LOG("=============== Attempting to hook ScriptGlobal_Format... s===============");

			uintptr_t target = 0x6B0F70;   // offset bạn cung cấp __int64 ScriptGlobal_Format(
			LOG("ScriptGlobal_Format runtime addr: %p", (void*)target);
			g_trampoline = HookEngine::create_trampoline((void*)target, 64);
			if (!g_trampoline)
			{
				LOG("create_trampoline failed");
				return;
			}

			g_orig_ScriptGlobal_Format = (ScriptGlobal_Format_t)g_trampoline;

			HookEngine::patch_code_safe(
				(void*)target,
				(void*)ScriptGlobal_Format_Hook
			);

			LOG("=============== ScriptGlobal_Format hooked successfully ===============");

		}).detach(); // detach để thread tự quản lý
		
		//----------------------------------------LuaFnScriptGlobal_Format------------------------------------------------//

	   std::thread([]()
		{
			LOG("Hook thread started, waiting 15 seconds...");
			std::this_thread::sleep_for(std::chrono::seconds(15));

			//LOG("10 seconds elapsed, starting hook installation...");

			//uintptr_t lua_addr    = base + OFFSET_LUA_FN;
			//uintptr_t format_addr = base + OFFSET_FORMAT;

			LOG("LuaFnScriptGlobal_Format runtime address = %p", (void*)OFFSET_LUA_FN);
			LOG("ScriptGlobal_Format runtime address      = %p", (void*)OFFSET_FORMAT);

			// Verify addresses are valid
			LOG("Checking if addresses are accessible...");
			
			// Test read first byte
			volatile unsigned char test = *(volatile unsigned char*)OFFSET_LUA_FN;
			LOG("First byte at LuaFn: 0x%02X", test);
			
			test = *(volatile unsigned char*)OFFSET_FORMAT;
			LOG("First byte at Format: 0x%02X", test);

			g_ScriptGlobal_Formattt = (ScriptGlobal_Format_tttt)(OFFSET_FORMAT);
			LOG("g_ScriptGlobal_Formattt set to %p", (void*)g_ScriptGlobal_Formattt);

			LOG("Creating trampoline at %p...", (void*)OFFSET_LUA_FN);
			g_trampoline = HookEngine::create_trampoline((void*)OFFSET_LUA_FN, 32);

			if (!g_trampoline)
			{
				LOG("ERROR: create_trampoline failed!");
				return;
			}

			LOG("Trampoline created at %p", g_trampoline);
			g_orig_LuaFn = (LuaFnScriptGlobal_Format_t)g_trampoline;

			LOG("Patching code at %p to jump to hook %p...", 
				(void*)OFFSET_LUA_FN, (void*)LuaFnScriptGlobal_Format_Hook);
			
			HookEngine::patch_code_safe(
				(void*)OFFSET_LUA_FN,
				(void*)LuaFnScriptGlobal_Format_Hook
			);

			LOG("==================================================");
			LOG("     HOOK INSTALLED SUCCESSFULLY");
			LOG("==================================================");
			LOG("LuaFnScriptGlobal_Format -> %p", (void*)OFFSET_LUA_FN);
			LOG("Hook function             -> %p", (void*)LuaFnScriptGlobal_Format_Hook);
			LOG("Trampoline (original)     -> %p", g_trampoline);
			LOG("==================================================");

		}).detach();
    		
		//------------------------------------------fix thu cuoi----------------------------------------------//
		// Tạo thread riêng để hook skill sau 45 giây (không block thread chính)
		std::thread([this]() {
			//sleep(30); // Hoặc 
			std::this_thread::sleep_for(std::chrono::seconds(35));
			
			void* skill_addr = dlsym(RTLD_DEFAULT, 
				"_ZNK13Combat_Module12Skill_Module16CommonSkill005_T16EffectOnUnitOnceER13Obj_CharacterS3_i");
			if (skill_addr) {
				LOG("Hooking Skill005 at %p", skill_addr);
				HookEngine::patch_code_safe(skill_addr, (void*)skill005_hook);
			} else {
				LOG("ERROR: Cannot find Skill005 address");
			}
		}).detach(); // detach để thread tự quản lý
		//----------------------------------------------------------------------------------------//


	}	
	
public:
    static ServerHook* getInstance() {
        pthread_once(&once_control, []() {
            instance = new ServerHook();
        });
        return instance;
    }
    
    ~ServerHook() {
        HookEngine::cleanup();
        g_logger.reset();
    }
};

ServerHook* ServerHook::instance = nullptr;
pthread_once_t ServerHook::once_control = PTHREAD_ONCE_INIT;

/* ============================================================
   CONSTRUCTOR - Gọi khi load shared library
============================================================ */
__attribute__((constructor))
void init() {
	//resolve_exe_script_func(); //ExeScript_DDDDDDDDDDD gọi ở hàm cần dùng .lua
    // Chỉ khởi tạo instance, đảm bảo thread-safe
    ServerHook::getInstance();
}

__attribute__((destructor))
void fini() {
    LOG("Server Hook shutting down...");
    delete ServerHook::getInstance();
}
