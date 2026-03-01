#define _GNU_SOURCE
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

// khai bao lua 5.0.3
extern "C" {
	#include "lua.h"
	#include "lauxlib.h"
	#include "lualib.h"
}

#define HOOK_STUB_SIZE 14
#define TRAMPOLINE_COPY_SIZE 32
#define PAGE_ALIGN(addr) ((uintptr_t)(addr) & ~(sysconf(_SC_PAGESIZE) - 1))
#define LOG_BUFFER_SIZE 1024
#define MAX_HOOK_RETRY 3


//build
//sudo apt update
//sudo apt install build-essential
//g++ -shared -fPIC -O2 -std=c++14 -pthread hook_so.cpp -ldl -o hook_so.so
//g++ -shared -fPIC -O2 -std=c++14 -pthread -fpermissive hook_so.cpp -ldl -o hook_so.so

//g++ -shared -fPIC -O2 -std=c++14 -pthread \
    -I/home/tlbb/Server/Lua \
    hook_so.cpp \
    -L. -lLuaLib \
    -ldl \
    -o hook_so.so
/* ============================================================
   CẤU HÌNH
============================================================ */
// Cấu hình runtime
static std::atomic<bool> g_enable_log{true};
static const char* LOG_PATH = "/home/tlbb/Server/Log/";

static pthread_mutex_t g_patch_mutex = PTHREAD_MUTEX_INITIALIZER;

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
	// Global pointers (thêm vào struct GlobalPointers g_globals)
	std::atomic<void*> g_GetScene_Func{nullptr};
	std::atomic<void*> g_HumanItemLogic_GetItem_Func{nullptr};
	std::atomic<void*> g_ItemTable_GetBlueItemTB_Func{nullptr};
	std::atomic<void*> g_NotifyEquipAttr_Func{nullptr};
	std::atomic<void*> g_RandomAttrRate_Func{nullptr};  // nếu có hàm random riêng
	//

};

static GlobalPointers g_globals;

// Random functions (sub_804xxxx → thay bằng rand() hoặc server random)
typedef int (*RandomFunc_Type)(int min, int max);  // giả định sub_804AE90() % 10 + 1

// Thay vì typedef cụ thể, dùng void* để compile
typedef void* (*GetScene_Type)(int scene_id);
typedef void* (*HumanItemLogic_GetItem_Type)(void* human, int bag_index);
typedef void* (*ItemTable_GetBlueItemTB_Type)(void* table, int serial);
typedef void (*NotifyEquipAttr_Type)(void* human, int bag_index, void* item);

//skill005_hook
typedef int (*GetExteriorRideMaxSpeed_Type)(void*);
typedef int (*SendImpactToUnit_Type)(void*, void*, unsigned short, unsigned int, int, int);

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
	
	static void* create_trampoline(void* target) {
		if (!target) return nullptr;
		
		std::lock_guard<std::mutex> lock(*(std::mutex*)&hook_mutex);
		
		void* trampoline = mmap(nullptr, 4096,
								 PROT_READ | PROT_WRITE | PROT_EXEC,
								 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (trampoline == MAP_FAILED) {
			LOG("ERROR: Khong the tao trampoline: %s", strerror(errno));
			return nullptr;
		}
		
		// Copy toàn bộ code gốc (bao gồm cả lệnh JMP nếu có)
		memcpy(trampoline, target, TRAMPOLINE_COPY_SIZE);
		
		uint8_t* code = (uint8_t*)target;
		// Nếu là lệnh JMP (opcode 0xE9), cần fix offset cho vị trí mới
		if (code[0] == 0xE9) {
			int32_t rel_offset = *(int32_t*)(code + 1);
			uint64_t dest = (uint64_t)target + 5 + rel_offset;
			uint64_t tramp_start = (uint64_t)trampoline;
			int32_t new_offset = (int32_t)(dest - (tramp_start + 5));
			*(int32_t*)((uint8_t*)trampoline + 1) = new_offset;
			LOG("Fixed JMP offset: old=%d, new=%d", rel_offset, new_offset);
		}
		
		// Tạo jump về phần code gốc sau trampoline (phần chưa copy)
		uint8_t* p = (uint8_t*)trampoline + TRAMPOLINE_COPY_SIZE;
		p[0] = 0x48;               // mov rax, ...
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
		
	// chưa dùng đến
	// Resolve các hàm cần cho EquipTransToNew
	g_globals.g_GetScene_Func.store(dlsym(RTLD_DEFAULT, "_ZN12SceneManager12GetSceneInfoEs"));  // mangled name có thể khác
	if (!g_globals.g_GetScene_Func.load()) {
		// Fallback offset nếu cần (từ IDA)
		LOG("GetScene not found - need manual offset");
	}
	g_globals.g_HumanItemLogic_GetItem_Func.store(dlsym(RTLD_DEFAULT, "_ZN13ItemContainer7GetItemEi"));
    g_globals.g_ItemTable_GetBlueItemTB_Func.store((void*)0xBDF800,std::memory_order_release);
	g_globals.g_NotifyEquipAttr_Func.store(dlsym(RTLD_DEFAULT, "xyzServerNotifyEquipAttr"));  // nếu export
	// end

}

/* ============================================================
   HÀM HOOK CHÍNH - TỐI ƯU, CHECK NULL, THREAD SAFE
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
    LOG("==== skill005_hook thú cưỡi Impact START ====");
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

    int nImpact = dword_103FE70[index];
    switch (ExteriorRideMaxSpeed) {
        case 20: nImpact = dword_103FE70[index + 1]; break;
        case 40: nImpact = dword_103FE70[index + 2]; break;
        case 60: nImpact = dword_103FE70[index + 3]; break;
        case 70: nImpact = dword_103FE70[index + 4]; break;
        case 75: nImpact = dword_103FE70[index + 5]; break;
        case 80: nImpact = dword_103FE70[index + 6]; break;
        case 85: nImpact = dword_103FE70[index + 7]; break;
        case 90: nImpact = dword_103FE70[index + 8]; break;
        case 95: nImpact = dword_103FE70[index + 9]; break;
        default: break;
    }
	
    LOG("nImpact (impact id)=%d", nImpact);
	//fix thu cuoi toc do 85%
    //LOG("81 = %d",  index +81);
    //LOG("82 = %d",  index +82);
    //LOG("83 = %d",  index +83);
	// vị trí tốc độ của thú cưỡi trong Exterior_Ride.txt, fix impact thú cưỡi + 85%
	if (dword_103FE70[index + 82] == 85) {
		nImpact = nImpact + 1;
		LOG("Fix impact Ride 85% = %d",  nImpact);
	}

    if (nImpact <= 0) {
        LOG("invalid impact");
        return 0;
    }

    // Gửi impact, +1 theo nhu cầu
    int result = send_impact(impact_core, a2, (unsigned short)nImpact, a2[2], 1, 100);
    LOG("SendImpact result=%d", result);

    if (!result) {
        LOG("SendImpact FAILED impact=%d", nImpact);
        return 0;
    }

    LOG("==== skill005_hook EXIT OK ====");
    return 1;
}

//lay ra ham lua co tren sv strings ./libLuaLib.so | grep -i '^lua_' | sort | uniq > lua_symbols.txt
//g++ -shared -fPIC -O2 -std=c++14 -pthread \
    -I/usr/include/lua5.1 \
    hook_so.cpp -ldl -o hook_so.so
/* ============================================================
   FOXLUA SCRIPT REGISTER FUNCTION - THEO PSEUDOCODE
============================================================ */
// int __cdecl FoxLuaScript::RegisterFunction(FoxLuaScript *const this, char *FuncName, void *Func)
typedef int (*FoxLuaScript_RegisterFunction_t)(void* this_ptr, const char* func_name, void* func_ptr);
static FoxLuaScript_RegisterFunction_t g_orig_FoxRegisterFunction = nullptr;

static std::atomic<bool> g_lua_injected{false};
static pthread_mutex_t g_lua_mutex = PTHREAD_MUTEX_INITIALIZER;
// Định nghĩa các hàm Lua cần thiết nếu không có header
typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *L);
static void* g_lua_interface = nullptr;          // LuaInterface*

//khai báo
extern "C" {
    int LuaFnGetAccountName(lua_State *L);
    int LuaFnEquipTransToNew(lua_State *L);
    // ... thêm các hàm khác
}
/*
// Các hàm Lua thường dùng
extern "C" {
    // Core stack operations (rất chắc chắn có)
    int lua_gettop(lua_State *L);
    void lua_settop(lua_State *L, int idx);

    // Type checking & conversion (cần thiết cho safe_get_number)
    int lua_isnumber(lua_State *L, int idx);
    int lua_isstring(lua_State *L, int idx);
    int lua_type(lua_State *L, int idx);
    const char* lua_typename(lua_State *L, int tp);   // để log type name nếu cần debug

    double lua_tonumber(lua_State *L, int idx);
    const char* lua_tostring(lua_State *L, int idx);   // có trong danh sách, dùng nếu cần string

    // Push values (cần để trả về kết quả)
    void lua_pushnil(lua_State *L);
    void lua_pushnumber(lua_State *L, double n);
    void lua_pushstring(lua_State *L, const char* s);
    void lua_pushvalue(lua_State *L, int idx);

    // Global variable access (nếu cần đọc/ghi global Lua)
    void lua_getglobal(lua_State *L, const char* name);
    void lua_setglobal(lua_State *L, const char* name);

    // Optional: nếu bạn cần gọi Lua function từ C (ít dùng trong hook)
    void lua_call(lua_State *L, int nargs, int nresults);
    void lua_rawcall(lua_State *L, int nargs, int nresults);

    // Optional: nếu cần tạo table hoặc userdata (ít dùng ở đây)
    void lua_newtable(lua_State *L);
    void* lua_newuserdata(lua_State *L, size_t nbytes);
	
    int lua_isfunction(lua_State *L, int idx);
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

void resolve_lua_interface() {
    uintptr_t base = get_module_base("Server");
    if (!base) {
        LOG("Không tìm thấy base address của Server");
        return;
    }

    // THAY OFFSET NÀY BẰNG OFFSET THỰC TỪ GHIDRA
    uintptr_t lua_interface_offset = 0x2B4C80;  // <-- OFFSET BẠN TÌM ĐƯỢC

    //g_lua_interface = *(void**)(base + lua_interface_offset);
    g_lua_interface = *(void**)lua_interface_offset;

    // Kiểm tra đơn giản
    if (g_lua_interface && HookEngine::is_executable_memory((void*)((uintptr_t)g_lua_interface + 0x10))) {
        LOG("LuaInterface resolved thành công: %p (base + 0x%lx)", 
            g_lua_interface, lua_interface_offset);
    } else {
        LOG("LuaInterface không hợp lệ tại offset 0x%lx - kiểm tra lại Ghidra", lua_interface_offset);
        g_lua_interface = nullptr;
    }
}
// Biến toàn cục
static void* g_exe_script_ddddddddddd = nullptr;

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

    // Lấy Scene (void*)
    auto GetScene = (GetScene_Type)g_globals.g_GetScene_Func.load();
    void* pScene = GetScene ? GetScene(scene_id) : nullptr;
    if (!pScene) {
        LOG("Scene not found for ID %d", scene_id);
        lua_pushnumber(L, -1.0);
        return 1;
    }
	
	
	TriggerLuaEventExtended_Hook(
		002116,               // script_id từ tên file x750010_...
		"OnDefaultEvent",     // event wrapper
		scene_id,                 // p1: sceneId
		obj_id,                 // p2: selfId
		obj_id,                 // p3: targetId
		-1,                   // p4
		0, 0, 0, 0, 0, 0, 0   // các param còn lại (thường 0 hoặc -1)
	);
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
int FoxRegisterFunction_Hook(void* this_ptr, const char* func_name, void* func_ptr) {
/*
    LOG(">>> FoxRegisterFunction_Hook ENTERED");
    LOG("    this_ptr: %p", this_ptr);
    LOG("    func_name: %s", func_name ? func_name : "NULL");
    LOG("    func_ptr: %p", func_ptr);
*/

    if (!g_orig_FoxRegisterFunction) {
        LOG("ERROR: g_orig_FoxRegisterFunction is NULL!");
        return 0;
    }

    if (!func_name) {
        LOG("WARNING: func_name is NULL, skipping original call");
        return 0;
    }

    int ret = g_orig_FoxRegisterFunction(this_ptr, func_name, func_ptr);
    //LOG("Original function registered: %s (ret=%d)", func_name, ret);

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
		// Hook FoxLuaScript::RegisterFunction ngay lập tức
		LOG("Attempting to hook FoxLuaScript::RegisterFunction...");
		void* fox_addr = dlsym(RTLD_DEFAULT, "_ZN12FoxLuaScript16RegisterFunctionEPKcPv");
		LOG("FoxLuaScript::RegisterFunction address: %p", fox_addr);

		if (fox_addr) {
			LOG("Creating trampoline...");
			void* trampoline = HookEngine::create_trampoline(fox_addr);
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
	//resolve_exe_script_func(); //ExeScript_DDDDDDDDDDD
	//resolve_lua_interface(); // chưa có offset hoặc dlsym chưa dùng đượcs
    // Chỉ khởi tạo instance, đảm bảo thread-safe
    ServerHook::getInstance();
}

__attribute__((destructor))
void fini() {
    LOG("Server Hook shutting down...");
    delete ServerHook::getInstance();
}
