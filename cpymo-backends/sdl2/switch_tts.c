/*
 * CPyMO Nintendo Switch 原生 TTS —— 厂商 ReadSpeaker(Nuance Vocalizer) 引擎的裸机包装
 *
 * 背景：/root/rhtts-src 是 macOS 上用 Hypervisor.framework 跑这套引擎的模拟器。
 * NS 的 guest ISA 与 host 相同（都是 ARM64），所以这里把「模拟」整层剥掉，
 * 只保留它真正提供的东西：
 *
 *   1) 三个 NSO 镜像（rtld / main / sdk）解压后按相对顺序铺进一块 RWX 区域
 *   2) 7 个补丁字：把 5 个内存类 svc、RNG、以及 nnMain 的入口改道到自己的 shim
 *   3) 两个入口：main+0x519620 = 引擎 init，main+0x519c80 = 分块合成
 *   4) 一个 0x70 字节的调用帧
 *
 * 其余（模拟器内核、stage-2 映射、dynarmic、brk 捕获、mini-rtld、导出表、FLAC 打包）
 * 全部不需要。
 *
 * 线程模型：
 *   宿主线程 ──(无锁请求环 + Event)──▶ 引擎线程（进 guest 后永不回来）
 *   引擎线程 ──(无锁 PCM 环)──────────▶ SDL 音频回调线程
 * 引擎线程不碰任何依赖 TLS 的 libc/SDL 调用（guest 可能改写 TPIDR_EL0）。
 */

#include "switch_tts.h"
#include "cpymo_switch_diag.h"

#if defined(__SWITCH__) && defined(ENABLE_TEXT_EXTRACT_SWITCH_ACCESSIBILITY)

#include <switch.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpymo_import_sdl2.h"

/* ------------------------------------------------------------------ */
/* 大小与常量                                                          */
/* ------------------------------------------------------------------ */

#define TT_TEXT_MAX      4096      /* 单条朗读文本上限（含 vtml 标签） */
#define TT_REQ_SLOTS     8         /* 请求环槽数 */
#define TT_MAX_CHUNK     12000     /* 引擎单块输出上限（字节） */
#define TT_SAMPLE_RATE   32000
#define TT_IMAGE_MAX     3
#define TT_RING_SAMPLES  131072    /* 4.10s @32000Hz，2 的幂 */
#define TT_RING_BYTES    (TT_RING_SAMPLES * 2)
#define TT_RING_MASK     (TT_RING_BYTES - 1)

#define TT_IMG_RTLD      0
#define TT_IMG_MAIN      1
#define TT_IMG_SDK       2

/* 引擎关键偏移（相对各镜像基址，定稿见 /memories/cpymo-switch-a11y.md） */
#define OFF_MAIN_BOOT        0x66e0    /* nnMain 序言，整函数换成 b tramp_boot */
#define OFF_MAIN_INIT        0x519620  /* 引擎 init(language, ?, vtdb, len, 0, 0) */
#define OFF_MAIN_SYNTH       0x519c80  /* 分块合成 */
#define OFF_MAIN_HEAP_OBJECT 0x1106588
#define OFF_MAIN_HEAP_CTOR   0x5144c0
#define OFF_MAIN_HEAP_INIT   0x4dfe90
#define OFF_MAIN_HEAP_SET    0x4dfda0
#define OFF_MAIN_HEAP_NAME   0xaa3f8b
#define TT_ENGINE_HEAP_SIZE  (32u << 20)
#define OFF_SDK_RNG          0xa7b50   /* GenerateCryptographicallyRandomBytes */
#define OFF_SDK_INIT_MODULE  0x11c010  /* nninitInitializeSdkModule → 直接 ret */

/* ------------------------------------------------------------------ */
/* 寄存器快照（asm 与 C 的契约，严格 0xA0 字节）                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t x[18];   /* 0x00..0x88  x0..x17 */
    uint64_t x18;     /* 0x90 */
    uint64_t sp;      /* 0x98 */
} tt_regs;

typedef struct {
    uint64_t fn;      /* 0x00 */
    uint64_t sp;      /* 0x08 */
    uint64_t x[8];    /* 0x10..0x48  x0..x7 */
    uint64_t result;  /* 0x50 */
} tt_call;

extern const char cpymo_sw_tts_tramp_start[];
extern const char cpymo_sw_tts_tramp_end[];
extern const char cpymo_sw_tts_tramp_return[];
extern void cpymo_sw_tts_call_guest(tt_call *spec);
extern void cpymo_sw_tts_enter_guest(uint64_t pc, uint64_t sp,
                                     uint64_t a0, uint64_t a1);

void cpymo_sw_tts_guest_returned(void);

/*
 * trampoline 模板：保存 x0..x18 与 sp，调 C 钩子，reload，ret。
 * 里面的 .quad 是运行期填进去的 64 位 C 函数地址 —— 用 ldr/blr 间接跳转，
 * 因此不受 ±128MB 分支范围限制（补丁点只 bl 到同窗口内的 trampoline）。
 */
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".global cpymo_sw_tts_tramp_start\n"
    "cpymo_sw_tts_tramp_start:\n"
    "    sub  sp, sp, #0x2D0\n"
    "    str  x30, [sp, #0xA0]\n"
    "    stp  x0,  x1,  [sp, #0x00]\n"
    "    stp  x2,  x3,  [sp, #0x10]\n"
    "    stp  x4,  x5,  [sp, #0x20]\n"
    "    stp  x6,  x7,  [sp, #0x30]\n"
    "    stp  x8,  x9,  [sp, #0x40]\n"
    "    stp  x10, x11, [sp, #0x50]\n"
    "    stp  x12, x13, [sp, #0x60]\n"
    "    stp  x14, x15, [sp, #0x70]\n"
    "    stp  x16, x17, [sp, #0x80]\n"
    "    str  x18,      [sp, #0x90]\n"
    "    add  x9, sp, #0x2D0\n"
    "    str  x9,       [sp, #0x98]\n"
    "    stp  q0, q1, [sp, #0xB0]\n"
    "    stp  q2, q3, [sp, #0xD0]\n"
    "    stp  q4, q5, [sp, #0xF0]\n"
    "    stp  q6, q7, [sp, #0x110]\n"
    "    stp  q8, q9, [sp, #0x130]\n"
    "    stp  q10, q11, [sp, #0x150]\n"
    "    stp  q12, q13, [sp, #0x170]\n"
    "    stp  q14, q15, [sp, #0x190]\n"
    "    stp  q16, q17, [sp, #0x1B0]\n"
    "    stp  q18, q19, [sp, #0x1D0]\n"
    "    stp  q20, q21, [sp, #0x1F0]\n"
    "    stp  q22, q23, [sp, #0x210]\n"
    "    stp  q24, q25, [sp, #0x230]\n"
    "    stp  q26, q27, [sp, #0x250]\n"
    "    stp  q28, q29, [sp, #0x270]\n"
    "    stp  q30, q31, [sp, #0x290]\n"
    "    mrs  x9, nzcv\n"
    "    str  x9, [sp, #0x2B0]\n"
    "    mrs  x9, fpcr\n"
    "    str  x9, [sp, #0x2B8]\n"
    "    mrs  x9, fpsr\n"
    "    str  x9, [sp, #0x2C0]\n"
    "    mov  x0, sp\n"
    "    ldr  x16, 1f\n"
    "    blr  x16\n"
    "    ldr  x9, [sp, #0x2B0]\n"
    "    msr  nzcv, x9\n"
    "    ldr  x9, [sp, #0x2B8]\n"
    "    msr  fpcr, x9\n"
    "    ldr  x9, [sp, #0x2C0]\n"
    "    msr  fpsr, x9\n"
    "    ldp  q0, q1, [sp, #0xB0]\n"
    "    ldp  q2, q3, [sp, #0xD0]\n"
    "    ldp  q4, q5, [sp, #0xF0]\n"
    "    ldp  q6, q7, [sp, #0x110]\n"
    "    ldp  q8, q9, [sp, #0x130]\n"
    "    ldp  q10, q11, [sp, #0x150]\n"
    "    ldp  q12, q13, [sp, #0x170]\n"
    "    ldp  q14, q15, [sp, #0x190]\n"
    "    ldp  q16, q17, [sp, #0x1B0]\n"
    "    ldp  q18, q19, [sp, #0x1D0]\n"
    "    ldp  q20, q21, [sp, #0x1F0]\n"
    "    ldp  q22, q23, [sp, #0x210]\n"
    "    ldp  q24, q25, [sp, #0x230]\n"
    "    ldp  q26, q27, [sp, #0x250]\n"
    "    ldp  q28, q29, [sp, #0x270]\n"
    "    ldp  q30, q31, [sp, #0x290]\n"
    "    ldp  x0,  x1,  [sp, #0x00]\n"
    "    ldp  x2,  x3,  [sp, #0x10]\n"
    "    ldp  x4,  x5,  [sp, #0x20]\n"
    "    ldp  x6,  x7,  [sp, #0x30]\n"
    "    ldp  x8,  x9,  [sp, #0x40]\n"
    "    ldp  x10, x11, [sp, #0x50]\n"
    "    ldp  x12, x13, [sp, #0x60]\n"
    "    ldp  x14, x15, [sp, #0x70]\n"
    "    ldp  x16, x17, [sp, #0x80]\n"
    "    ldr  x18,      [sp, #0x90]\n"
    "    ldr  x30, [sp, #0xA0]\n"
    "    add  sp, sp, #0x2D0\n"
    ".global cpymo_sw_tts_tramp_return\n"
    "cpymo_sw_tts_tramp_return:\n"
    "    ret\n"
    "    .balign 8\n"
    "1:  .quad 0\n"
    ".global cpymo_sw_tts_tramp_end\n"
    "cpymo_sw_tts_tramp_end:\n"
);

/* 从宿主线程调用 guest：切到 guest 栈、blr、切回、取 x0 */
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".global cpymo_sw_tts_call_guest\n"
    "cpymo_sw_tts_call_guest:\n"
    "    stp  x19, x20, [sp, #-48]!\n"
    "    stp  x21, x22, [sp, #16]\n"
    "    str  x30, [sp, #32]\n"
    "    mov  x19, x0\n"
    "    ldr  x21, [x19, #0x00]\n"
    "    ldr  x20, [x19, #0x08]\n"
    "    ldp  x0,  x1,  [x19, #0x10]\n"
    "    ldp  x2,  x3,  [x19, #0x20]\n"
    "    ldp  x4,  x5,  [x19, #0x30]\n"
    "    ldp  x6,  x7,  [x19, #0x40]\n"
    "    mov  x22, sp\n"
    "    mov  sp, x20\n"
    "    blr  x21\n"
    "    mov  sp, x22\n"
    "    str  x0, [x19, #0x50]\n"
    "    ldr  x30, [sp, #32]\n"
    "    ldp  x21, x22, [sp, #16]\n"
    "    ldp  x19, x20, [sp], #48\n"
    "    ret\n"
);

/* 一次性进入 guest 并从我们的栈上起跑（x0/x1 是 rtld 的 argc/argv 语义） */
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".global cpymo_sw_tts_enter_guest\n"
    "cpymo_sw_tts_enter_guest:\n"
    "    mov  x19, x0\n"
    "    adr  x30, 1f\n"
    "    mov  sp,  x1\n"
    "    mov  x0,  x2\n"
    "    mov  x1,  x3\n"
    "    br   x19\n"
    "1:  bl   cpymo_sw_tts_guest_returned\n"
    "2:  b    2b\n"
);

/* ------------------------------------------------------------------ */
/* 全局状态                                                            */
/* ------------------------------------------------------------------ */

static cpymo_switch_tts_state g_state = CPYMO_SWITCH_TTS_STATE_IDLE;
static char g_error[192];
static bool g_engine_ok;
static cpymo_switch_tts_params g_params;
static Mutex g_params_mutex;

/* 可执行窗口 */
static uint8_t *g_rw;            /* 可写别名（映射前写，映射后可继续读） */
static uint8_t *g_rx;            /* guest 可见基址 */
static size_t   g_window_size;
static void    *g_code_resv;     /* virtmemFindCodeMemory 的保留 */
static VirtmemReservation *g_code_rv;

/* 各镜像在窗口内的偏移与 guest 基址 */
static uint32_t g_img_off[TT_IMAGE_MAX];
static size_t   g_img_size[TT_IMAGE_MAX];
static uintptr_t g_img_va[TT_IMAGE_MAX];
static uint32_t g_text_off[TT_IMAGE_MAX];
static uint32_t g_text_size[TT_IMAGE_MAX];
static uint32_t g_tramp_off;     /* trampoline 池在窗口内的偏移 */
static uint32_t g_tramp_slot;

/* guest 侧各块内存 */
static uint8_t  *g_engine_heap;
static uint8_t  *g_heap_mem;
static size_t    g_heap_size;
static uintptr_t g_heap_va;
static uint8_t  *g_vstack;       /* rtld/main 起跑用的栈 */
static size_t    g_vstack_size;
static uint8_t  *g_cstack;       /* 宿主回调 guest 用的独立栈 */
static size_t    g_cstack_size;
static uint8_t  *g_inbuf;
static size_t    g_inbuf_size;
static uint8_t  *g_outbuf;
static size_t    g_outbuf_size;
static uint8_t  *g_vtdb;
static size_t    g_vtdb_len;

/* 引擎线程 */
static Thread g_thread;
static uint8_t *g_thread_stack;
static size_t   g_thread_stack_size;
static bool     g_thread_started;
static Event    g_cmd_event;
static Event    g_boot_event;
static bool     g_boot_done;
static int64_t  g_boot_result;
static bool     g_quit;

/* 无锁请求环（主线程 → 引擎线程） */
static char     g_req_text[TT_REQ_SLOTS][TT_TEXT_MAX];
static uint32_t g_req_kind[TT_REQ_SLOTS];   /* 0=append 1=replace 2=interrupt */
static uint32_t g_req_head;
static uint32_t g_req_tail;
static uint32_t g_req_reset_gen;
static uint32_t g_req_reset_seen;

/* 无锁 PCM 环（引擎线程 → 音频回调） */
static uint8_t  g_ring[TT_RING_BYTES];
static uint32_t g_ring_w;
static uint32_t g_ring_r;
static uint32_t g_flush_gen;
static uint32_t g_flush_seen;
static uint32_t g_abort_gen;

static SDL_AudioDeviceID g_adev;
static bool g_audio_open;

static void *tt_alloc(size_t size)
{
    size_t pages;
    uint8_t *base, *aligned;

    if (size == 0) return NULL;
    pages = (size + 0xFFF) & ~(size_t)0xFFF;

    /* newlib（Switch）没有 posix_memalign：多要一页自己对齐。
       这些区域有意不释放（见 cpymo_switch_tts_free），所以不必记住基址；
       多花的一页会留到进程退出，代价可以忽略。 */
    base = (uint8_t *)malloc(pages + 0x1000);
    if (!base) return NULL;
    aligned = (uint8_t *)(((uintptr_t)base + 0xFFF) & ~(uintptr_t)0xFFF);
    memset(aligned, 0, pages);
    return aligned;
}

static bool tt_fail(const char *fmt, const char *arg)
{
    snprintf(g_error, sizeof(g_error), fmt, arg ? arg : "");
    g_state = CPYMO_SWITCH_TTS_STATE_FAILED;
    CPYMO_DIAG("[tts] FAIL: %s\n", g_error);
    return false;
}

static bool tt_fail0(const char *msg)
{
    return tt_fail("%s", msg);
}

/* 读整个文件到 malloc 缓冲 */
static uint8_t *tt_read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *buf;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

/* ------------------------------------------------------------------ */
/* NSO 头与 raw-LZ4 block 解压                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t magic;          /* 0x00 "NSO0" */
    uint32_t version;        /* 0x04 */
    uint32_t reserved;       /* 0x08 */
    uint32_t flags;          /* 0x0C bit i = 段 i 是 raw-LZ4 */
    uint32_t text_file_off;  /* 0x10 */
    uint32_t text_mem_off;   /* 0x14 */
    uint32_t text_size;      /* 0x18 */
    uint32_t mod_name_off;   /* 0x1C */
    uint32_t ro_file_off;    /* 0x20 */
    uint32_t ro_mem_off;     /* 0x24 */
    uint32_t ro_size;        /* 0x28 */
    uint32_t mod_name_size;  /* 0x2C */
    uint32_t data_file_off;  /* 0x30 */
    uint32_t data_mem_off;   /* 0x34 */
    uint32_t data_size;      /* 0x38 */
    uint32_t bss_size;       /* 0x3C */
    uint8_t  build_id[0x20]; /* 0x40..0x5F */
    uint32_t comp[3];        /* 0x60/0x64/0x68 压缩后长度 */
} __attribute__((packed)) tt_nso;

static uint32_t tt_align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) & ~(a - 1);
}

/* raw LZ4 block → 定长输出；成功要求产出恰好 dst_len */
static bool tt_lz4(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len)
{
    size_t s = 0, o = 0;
    while (s < src_len) {
        uint8_t token = src[s++];
        size_t lit = (size_t)(token >> 4), ml = (size_t)(token & 15);
        size_t off, i;
        uint8_t b;

        if (lit == 15) {
            do {
                if (s >= src_len) return false;
                b = src[s++];
                lit += b;
            } while (b == 255);
        }
        if (lit) {
            if (s + lit > src_len || o + lit > dst_len) return false;
            memcpy(dst + o, src + s, lit);
            s += lit;
            o += lit;
        }
        if (s >= src_len) break;            /* 合法结尾：没有 match */
        if (s + 2 > src_len) return false;
        off = (size_t)src[s] | ((size_t)src[s + 1] << 8);
        s += 2;
        if (ml == 15) {
            do {
                if (s >= src_len) return false;
                b = src[s++];
                ml += b;
            } while (b == 255);
        }
        ml += 4;
        if (off == 0 || off > o || o + ml > dst_len) return false;
        for (i = 0; i < ml; i++) dst[o + i] = dst[o + i - off];
        o += ml;
    }
    return o == dst_len;
}

/* 解一个 NSO 到 dst（dst 容量 = 镜像总大小），返回各段信息 */
static bool tt_load_nso(const char *path, uint8_t *dst, uint32_t dst_cap,
                        uint32_t *text_off, uint32_t *text_size,
                        uint32_t *need_size)
{
    size_t file_len = 0;
    uint8_t *file = tt_read_file(path, &file_len);
    tt_nso h;
    struct {
        uint32_t file_off, mem_off, size;
        int idx;
    } seg[3];
    int i;
    uint32_t need;

    if (!file) return tt_fail0("无法读取 NSO：%s");
    if (file_len < sizeof(tt_nso)) { free(file); return tt_fail0("NSO 头不完整"); }
    memcpy(&h, file, sizeof(h));
    if (h.magic != 0x304F534Eu) { free(file); return tt_fail0("不是 NSO0 文件"); }

    seg[0].file_off = h.text_file_off; seg[0].mem_off = h.text_mem_off;
    seg[0].size = h.text_size; seg[0].idx = 0;
    seg[1].file_off = h.ro_file_off;   seg[1].mem_off = h.ro_mem_off;
    seg[1].size = h.ro_size;   seg[1].idx = 1;
    seg[2].file_off = h.data_file_off; seg[2].mem_off = h.data_mem_off;
    seg[2].size = h.data_size; seg[2].idx = 2;

    need = 0;
    for (i = 0; i < 3; i++) {
        if (seg[i].size == 0) continue;
        if ((uint32_t)seg[i].mem_off + seg[i].size > need)
            need = (uint32_t)seg[i].mem_off + seg[i].size;
    }
    need += h.bss_size;
    need = tt_align_up(need, 0x1000);
    if (need > dst_cap) { free(file); return tt_fail0("NSO 超出预留容量"); }

    memset(dst, 0, need);
    for (i = 0; i < 3; i++) {
        size_t avail;
        bool compressed;
        if (seg[i].size == 0) continue;
        compressed = (h.flags >> seg[i].idx) & 1u;
        if (seg[i].file_off > file_len) { free(file); return tt_fail0("NSO 段越界"); }
        avail = file_len - seg[i].file_off;
        if (compressed) {
            size_t clen = h.comp[seg[i].idx];
            if (clen > avail) { free(file); return tt_fail0("NSO 压缩段长度不足"); }
            if (!tt_lz4(file + seg[i].file_off, clen,
                        dst + seg[i].mem_off, seg[i].size)) {
                free(file);
                return tt_fail0("raw-LZ4 解压失败");
            }
        } else {
            size_t n = seg[i].size;
            if (n > avail) { free(file); return tt_fail0("NSO 段长度不足"); }
            memcpy(dst + seg[i].mem_off, file + seg[i].file_off, n);
        }
    }
    free(file);

    *text_off = h.text_mem_off;
    *text_size = h.text_size;
    *need_size = need;
    CPYMO_DIAG("[tts] NSO %s: size=0x%x text=0x%x+0x%x bss=0x%x\n",
           path, need, h.text_mem_off, h.text_size, h.bss_size);
    return true;
}

/* ------------------------------------------------------------------ */
/* svc shim：只伪造 guest 会害到我们自己的那几个                          */
/* ------------------------------------------------------------------ */

/* Horizon MemoryInfo（svcQueryMemory 的输出，0x28 字节） */
typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t type;
    uint32_t attr;
    uint32_t perm;
    uint32_t ipc_refcount;
    uint32_t device_refcount;
    uint32_t pad;
} tt_meminfo;

#define TT_MT_NORMAL        2u
#define TT_MT_CODE_STATIC   8u
#define TT_MT_HEAP          5u
#define TT_PM_R             1u
#define TT_PM_W             2u
#define TT_PM_X             4u

static void shim_set_heap_size(tt_regs *r)
{
    /*
     * svcSetHeapSize(void **out, u64 size)：宿主的返回值放在 x1，x0 是 Result。
     * 引擎要多大就给多大（按 1MB 对齐），拿不到就逐级减半；给不出请求的
     * 大小时只能记日志，引擎越界与否不由我们控制。
     */
    uint64_t want = r->x[1];

    if (!g_heap_mem) {
        size_t min_sz = (size_t)32 << 20;
        size_t sz = (size_t)((want + 0xFFFFFull) & ~(uint64_t)0xFFFFFull);

        if (sz < min_sz) sz = min_sz;
        if (sz > ((size_t)3 << 30)) sz = (size_t)3 << 30;
        while (sz >= min_sz) {
            g_heap_mem = (uint8_t *)tt_alloc(sz);
            if (g_heap_mem) { g_heap_size = sz; break; }
            sz >>= 1;
        }
        if (g_heap_mem) {
            g_heap_va = (uintptr_t)g_heap_mem;
            CPYMO_DIAG("[tts] SetHeapSize 请求 0x%llx -> 给 0x%zx @0x%llx\n",
                   (unsigned long long)want, g_heap_size,
                   (unsigned long long)g_heap_va);
        }
    } else if (want > (uint64_t)g_heap_size) {
        CPYMO_DIAG("[tts] 警告：再次请求 0x%llx > 已有 0x%zx\n",
               (unsigned long long)want, g_heap_size);
    }
    r->x[0] = 0;
    r->x[1] = (uint64_t)g_heap_va;
}

static void shim_set_mem_perm(tt_regs *r)
{
    (void)r;
    r->x[0] = 0;
}

static void shim_set_mem_attr(tt_regs *r)
{
    (void)r;
    r->x[0] = 0;
}

static bool tt_region_of(uintptr_t a, tt_meminfo *m)
{
    uintptr_t main_base = g_img_va[TT_IMG_MAIN];
    uintptr_t sdk_base = g_img_va[TT_IMG_SDK];
    uintptr_t end = sdk_base + g_img_size[TT_IMG_SDK];

    memset(m, 0, sizeof(*m));
    if (a < main_base) {
        m->base = 0;
        m->size = main_base;
        m->type = 16; /* reserved */
    } else if (a < sdk_base) {
        m->base = main_base;
        m->size = sdk_base - main_base;
        m->type = 3; /* code */
        m->perm = TT_PM_R | TT_PM_X;
    } else if (a < end) {
        m->base = sdk_base;
        m->size = end - sdk_base;
        m->type = 3;
        m->perm = TT_PM_R | TT_PM_X;
    } else {
        m->base = end;
        m->size = (uint64_t)0 - (uint64_t)end;
        m->type = 16;
    }
    return m->type == 3;
}

static void shim_query_memory(tt_regs *r)
{
    /* svcQueryMemory(MemoryInfo *out, u32 *page_info, u64 addr) */
    void *out = (void *)(uintptr_t)r->x[0];
    uintptr_t addr = (uintptr_t)r->x[2];
    if (out) tt_region_of(addr, (tt_meminfo *)out);
    r->x[0] = 0;
    r->x[1] = 0;
}

static void shim_get_info(tt_regs *r)
{
    /* Kernel SVC ABI returns the value in x1. */
    uint64_t id = r->x[1];
    uint64_t v = 0;

    switch (id) {
        case 21: v = (uint64_t)g_heap_va + (uint64_t)g_heap_size; break;
        case 22: v = (uint64_t)g_heap_va; break;
        default: v = 0; break;
    }
    r->x[1] = v;
    r->x[0] = 0;
}

static void shim_rng(tt_regs *r)
{
    /* nn::crypto::GenerateCryptographicallyRandomBytes(buf, len) */
    void *buf = (void *)(uintptr_t)r->x[0];
    size_t len = (size_t)r->x[1];
    if (buf && len) memset(buf, 0, len);
    r->x[0] = 0;
}

/* ------------------------------------------------------------------ */
/* trampoline 池与补丁                                                 */
/* ------------------------------------------------------------------ */

#define TT_TRAMP_STRIDE 0x200

static uint32_t tt_add_tramp(void (*fn)(tt_regs *), uint32_t continuation)
{
    size_t len = (size_t)(cpymo_sw_tts_tramp_end - cpymo_sw_tts_tramp_start);
    uint8_t *slot = g_rw + g_tramp_off + (size_t)g_tramp_slot * TT_TRAMP_STRIDE;
    uint64_t addr = (uint64_t)(uintptr_t)fn;
    uint32_t off = g_tramp_off + g_tramp_slot * TT_TRAMP_STRIDE;

    if (len > TT_TRAMP_STRIDE || (size_t)(g_tramp_slot + 1) * TT_TRAMP_STRIDE > 0x2000) return 0;        /* 理论上不会发生 */
    memcpy(slot, cpymo_sw_tts_tramp_start, len);
    memcpy(slot + len - 8, &addr, 8);
    if (continuation) {
        size_t ret_off = (size_t)(cpymo_sw_tts_tramp_return - cpymo_sw_tts_tramp_start);
        int64_t delta = (int64_t)continuation - (int64_t)(off + ret_off);
        uint32_t branch = 0x14000000u | ((uint32_t)(delta >> 2) & 0x03ffffffu);
        if ((delta & 3) || delta < -(1ll << 27) || delta >= (1ll << 27)) return 0;
        memcpy(slot + ret_off, &branch, sizeof(branch));
    }
    g_tramp_slot++;
    CPYMO_DIAG("[tts] tramp #%u for %p\n", g_tramp_slot - 1, (void *)fn);
    return off;
}

static uint32_t tt_img_word(int img, uint32_t off)
{
    return *(const uint32_t *)(const void *)(g_rw + g_img_off[img] + off);
}

/* 把 img+off 处原来的 expect 换成一条跳到 target_off 的 b/bl */
static bool tt_patch_branch(const char *what, int img, uint32_t off,
                           uint32_t expect, uint32_t target_off, bool link)
{
    uint32_t actual = tt_img_word(img, off);
    uint64_t site = (uint64_t)g_img_off[img] + off;
    int64_t delta = (int64_t)target_off - (int64_t)site;
    uint32_t insn;

    if (actual != expect) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s 校验失败：0x%08x != 0x%08x",
                 what, actual, expect);
        CPYMO_DIAG("[tts] %s (img=%d off=0x%x)\n", buf, img, off);
        return tt_fail0(buf);
    }
    if ((delta & 3) != 0) return tt_fail("分支目标未对齐：%s", what);
    delta >>= 2;
    if (delta < -(1 << 25) || delta >= (1 << 25))
        return tt_fail("分支超出 ±128MB：%s", what);

    insn = (link ? 0x94000000u : 0x14000000u) | ((uint32_t)delta & 0x03FFFFFFu);
    *(uint32_t *)(void *)(g_rw + g_img_off[img] + off) = insn;
    CPYMO_DIAG("[tts] patch %s: img=%d off=0x%x -> 0x%x (%s)\n",
           what, img, off, target_off, link ? "bl" : "b");
    return true;
}

static bool tt_patch_word(const char *what, int img, uint32_t off,
                          uint32_t expect, uint32_t new_word)
{
    uint32_t actual = tt_img_word(img, off);
    if (actual != expect) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s 校验失败：0x%08x != 0x%08x",
                 what, actual, expect);
        CPYMO_DIAG("[tts] %s (img=%d off=0x%x)\n", buf, img, off);
        return tt_fail0(buf);
    }
    *(uint32_t *)(void *)(g_rw + g_img_off[img] + off) = new_word;
    CPYMO_DIAG("[tts] patch %s: img=%d off=0x%x = 0x%08x\n", what, img, off, new_word);
    return true;
}

/* ------------------------------------------------------------------ */
/* PCM 环形缓冲（引擎线程写，SDL 音频回调读，全程无锁）                  */
/* ------------------------------------------------------------------ */

void cpymo_switch_tts_mix(Uint8 *stream, int len, SDL_AudioFormat format,
                          int freq, int channels)
{
    static int phase, sample_valid;
    static int16_t sample;
    union { float f[512]; int32_t i32[512]; int16_t i16[512]; } buffer;
    uint32_t flush, r, w;
    int sample_bytes, frame_bytes, remaining;

    if (!__atomic_load_n(&g_audio_open, __ATOMIC_ACQUIRE) || freq <= 0 ||
        channels <= 0 || channels > 8) return;
    sample_bytes = SDL_AUDIO_BITSIZE(format) / 8;
    if ((format != AUDIO_S16SYS && format != AUDIO_S32SYS && format != AUDIO_F32SYS) ||
        sample_bytes <= 0) return;
    frame_bytes = sample_bytes * channels;
    remaining = len / frame_bytes;

    flush = __atomic_load_n(&g_flush_gen, __ATOMIC_ACQUIRE);
    if (flush != g_flush_seen) {
        g_flush_seen = flush;
        w = __atomic_load_n(&g_ring_w, __ATOMIC_ACQUIRE);
        __atomic_store_n(&g_ring_r, w, __ATOMIC_RELEASE);
        sample_valid = 0;
        phase = 0;
    }

    r = g_ring_r;
    w = __atomic_load_n(&g_ring_w, __ATOMIC_ACQUIRE);
    while (remaining > 0) {
        int frames = remaining > 64 ? 64 : remaining;
        int i, ch;
        for (i = 0; i < frames; ++i) {
            while (!sample_valid || phase >= freq) {
                uint8_t raw[2];
                if (w - r < 2) {
                    sample_valid = 0;
                    phase = 0;
                    break;
                }
                raw[0] = g_ring[r & TT_RING_MASK];
                raw[1] = g_ring[(r + 1) & TT_RING_MASK];
                memcpy(&sample, raw, sizeof(sample));
                r += 2;
                if (sample_valid) phase -= freq;
                sample_valid = 1;
            }
            for (ch = 0; ch < channels; ++ch) {
                int idx = i * channels + ch;
                int16_t v = sample_valid ? sample : 0;
                if (format == AUDIO_F32SYS) buffer.f[idx] = (float)v / 32768.0f;
                else if (format == AUDIO_S32SYS) buffer.i32[idx] = (int32_t)v * 65536;
                else buffer.i16[idx] = v;
            }
            if (sample_valid) phase += TT_SAMPLE_RATE;
        }
        SDL_MixAudioFormat(stream, (const Uint8 *)&buffer, format,
                           (Uint32)(frames * frame_bytes), SDL_MIX_MAXVOLUME);
        stream += frames * frame_bytes;
        remaining -= frames;
    }
    __atomic_store_n(&g_ring_r, r, __ATOMIC_RELEASE);
}

static uint32_t tt_ring_pending(void)
{
    uint32_t w = __atomic_load_n(&g_ring_w, __ATOMIC_ACQUIRE);
    uint32_t r = __atomic_load_n(&g_ring_r, __ATOMIC_ACQUIRE);
    return w - r;
}

static void tt_ring_write(const uint8_t *p, size_t n)
{
    uint32_t w = g_ring_w;
    size_t i;
    for (i = 0; i < n; i++) g_ring[(w + i) & TT_RING_MASK] = p[i];
    __atomic_store_n(&g_ring_w, w + (uint32_t)n, __ATOMIC_RELEASE);
}

static bool tt_wait_ring_space(size_t n, uint64_t timeout_ns)
{
    uint64_t waited = 0;
    while (tt_ring_pending() + (uint32_t)n > TT_RING_BYTES) {
        if (__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) return false;
        if (waited >= timeout_ns) return false;
        svcSleepThread(1000000ull);
        waited += 1000000ull;
    }
    return true;
}

/* 等这段音频播完；可被 interrupt 打断 */
static void tt_wait_playback(uint64_t timeout_ns)
{
    uint64_t waited = 0;
    while (tt_ring_pending() > 0) {
        if (__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) return;
        if (waited >= timeout_ns) {
            __atomic_add_fetch(&g_flush_gen, 1, __ATOMIC_RELEASE);
            return;
        }
        svcSleepThread(2000000ull);
        waited += 2000000ull;
    }
}

/* ------------------------------------------------------------------ */
/* 合成：把一条文本灌进引擎，分块把 PCM 抽出来                          */
/* ------------------------------------------------------------------ */

static size_t tt_build_input(const char *text, const cpymo_switch_tts_params *params)
{
    static const char close_tag[] = "</vtml_emotion>";
    size_t cap = g_inbuf_size, n = 0, i, cl;

    if (params->emotion_level > 0) {
        int w = snprintf((char *)g_inbuf, cap,
                         "<vtml_emotion category=\"%s\" level=\"%d\">",
                         params->emotion_category, params->emotion_level);
        if (w < 0 || (size_t)w >= cap) return 0;
        n = (size_t)w;
    }
    for (i = 0; text[i]; i++) {
        if (n + 1 >= cap) break;
        g_inbuf[n++] = (text[i] == '<') ? '>' : (uint8_t)text[i];
    }
    if (params->emotion_level > 0) {
        cl = sizeof(close_tag) - 1;
        if (n + cl + 1 >= cap) return 0;
        memcpy(g_inbuf + n, close_tag, cl);
        n += cl;
    }
    g_inbuf[n] = 0;
    return n;
}

static bool tt_synth(const char *text)
{
    uintptr_t sp = ((uintptr_t)g_cstack + g_cstack_size - 0x100) & ~(uintptr_t)0xF;
    uint8_t *frame = (uint8_t *)sp;
    uint32_t abort_gen = __atomic_load_n(&g_abort_gen, __ATOMIC_ACQUIRE);
    uint32_t step = 0;
    size_t in_len;
    cpymo_switch_tts_params snapshot;
    const cpymo_switch_tts_params *params = &snapshot;

    if (!g_inbuf || !g_outbuf || !g_cstack) return false;
    cpymo_switch_tts_get_params(&snapshot);
    in_len = tt_build_input(text, params);
    if (in_len == 0) return false;

    memset(frame, 0, 0x70);
    for (;;) {
        tt_call c;
        int64_t rc;
        uint32_t got;

        if (__atomic_load_n(&g_abort_gen, __ATOMIC_ACQUIRE) != abort_gen) return true;
        if (__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) return true;

        *(uint64_t *)(void *)(frame + 0x00) = 0;
        *(uint64_t *)(void *)(frame + 0x08) = (uint64_t)(uintptr_t)(frame + 0x58);
        *(uint64_t *)(void *)(frame + 0x10) = step;
        *(uint64_t *)(void *)(frame + 0x18) = 0;
        *(uint64_t *)(void *)(frame + 0x20) = 0xffffffffull;
        *(uint64_t *)(void *)(frame + 0x28) = (uint64_t)params->pitch;
        *(uint64_t *)(void *)(frame + 0x30) = (uint64_t)params->rate;
        *(uint64_t *)(void *)(frame + 0x38) = (uint64_t)params->volume;
        *(uint64_t *)(void *)(frame + 0x40) = 400;
        *(uint64_t *)(void *)(frame + 0x48) = 0;
        *(uint64_t *)(void *)(frame + 0x50) = 0xffffffffull;

        memset(&c, 0, sizeof(c));
        c.fn = (uint64_t)g_img_va[TT_IMG_MAIN] + OFF_MAIN_SYNTH;
        c.sp = (uint64_t)sp;
        c.x[0] = (uint64_t)params->voice;
        c.x[1] = 0;
        c.x[2] = (uint64_t)(uintptr_t)g_inbuf;
        c.x[3] = (uint64_t)in_len;
        c.x[4] = (uint64_t)(uintptr_t)g_outbuf;
        c.x[5] = (uint64_t)(uintptr_t)(frame + 0x60);
        c.x[6] = 0;
        c.x[7] = (uint64_t)(uintptr_t)(frame + 0x58);
        cpymo_sw_tts_call_guest(&c);

        rc = (int32_t)c.result;
        if (rc < 0) {
            CPYMO_DIAG("[tts] synth 返回 %lld\n", (long long)rc);
            return false;
        }

        got = *(uint32_t *)(void *)(frame + 0x60);
        if (got > TT_MAX_CHUNK || (got & 1u)) return false;
        if (got) {
            if (!tt_wait_ring_space(got, 20000000000ull)) return false;
            tt_ring_write(g_outbuf, got);
        }
        step = 1;
        if (rc == 1) break;
    }

    tt_wait_playback(120000000000ull);
    return true;
}

/* ------------------------------------------------------------------ */
/* 引擎线程：进入 guest 后就永久驻留在这里                              */
/* ------------------------------------------------------------------ */

static void tt_engine_loop(void)
{
    char text[TT_TEXT_MAX];

    for (;;) {
        uint32_t head = __atomic_load_n(&g_req_head, __ATOMIC_ACQUIRE);
        uint32_t tail = g_req_tail;
        uint32_t gen = __atomic_load_n(&g_req_reset_gen, __ATOMIC_ACQUIRE);
        uint32_t slot;

        if (gen != g_req_reset_seen) {
            g_req_reset_seen = gen;
            if (head > tail + 1) tail = head - 1;   /* 只保留最新一条 */
        }

        if (tail == head) {
            eventWait(&g_cmd_event, UINT64_MAX);
            if (__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) break;
            continue;
        }

        slot = tail % TT_REQ_SLOTS;
        memcpy(text, g_req_text[slot], TT_TEXT_MAX);
        text[TT_TEXT_MAX - 1] = 0;
        __atomic_store_n(&g_req_tail, tail + 1, __ATOMIC_RELEASE);

        if (__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) break;
        if (text[0]) tt_synth(text);
    }
}

/*
 * nnMain 入口钩子：main+0x66e0 被换成 b 到这里。
 * 按模组顺序构造并注册独立堆，然后调引擎 init，
 * 之后这条 guest 线程就变成 TTS 引擎线程，永不返回。
 */
static void tt_boot_hook(tt_regs *r)
{
    uint8_t *m = (uint8_t *)g_img_va[TT_IMG_MAIN];
    tt_call c;
    cpymo_switch_tts_params params;

    (void)r;
    cpymo_switch_tts_get_params(&params);
    CPYMO_DIAG("[tts] nnMain 入口钩子命中\n");

    memset(&c, 0, sizeof(c));
    c.sp = ((uint64_t)(uintptr_t)g_cstack + g_cstack_size - 0x100) & ~15ull;
    c.fn = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_CTOR);
    c.x[0] = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_OBJECT);
    cpymo_sw_tts_call_guest(&c);
    c.fn = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_INIT);
    c.x[0] = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_OBJECT);
    c.x[1] = (uint64_t)(uintptr_t)g_engine_heap;
    c.x[2] = TT_ENGINE_HEAP_SIZE;
    c.x[3] = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_NAME);
    cpymo_sw_tts_call_guest(&c);
    c.fn = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_SET);
    c.x[0] = (uint64_t)(uintptr_t)(m + OFF_MAIN_HEAP_OBJECT);
    cpymo_sw_tts_call_guest(&c);

    memset(&c, 0, sizeof(c));
    c.fn = (uint64_t)g_img_va[TT_IMG_MAIN] + OFF_MAIN_INIT;
    c.sp = ((uint64_t)(uintptr_t)g_cstack + g_cstack_size - 0x100) & ~15ull;
    c.x[0] = (uint64_t)params.voice;
    c.x[1] = 0xffffffffull;
    c.x[2] = (uint64_t)(uintptr_t)g_vtdb;
    c.x[3] = (uint64_t)g_vtdb_len;
    c.x[4] = 0;
    c.x[5] = 0;
    cpymo_sw_tts_call_guest(&c);

    g_boot_result = (int64_t)(int32_t)c.result;
    g_engine_ok = (g_boot_result == 0);
    g_boot_done = true;
    if (!g_engine_ok) g_state = CPYMO_SWITCH_TTS_STATE_FAILED;
    CPYMO_DIAG("[tts] 引擎 init -> %lld\n", (long long)g_boot_result);
    eventFire(&g_boot_event);

    if (!g_engine_ok) {
        svcExitThread();
        for (;;) svcSleepThread(100000000ull);
    }

    tt_engine_loop();

    /* 到这里说明要关机了：直接结束这条线程，不再回 guest */
    svcExitThread();
    for (;;) svcSleepThread(100000000ull);
}

static void tt_guest_thread_main(void *arg)
{
    (void)arg;
    cpymo_sw_tts_enter_guest((uint64_t)g_img_va[TT_IMG_RTLD],
                             (uint64_t)(uintptr_t)(g_vstack + g_vstack_size),
                             0, 1);
    /* 不该到这里：rtld 正常情况不会返回 */
}

void cpymo_sw_tts_guest_returned(void)
{
    CPYMO_DIAG("[tts] guest 从 rtld 返回了（异常）\n");
    if (g_state != CPYMO_SWITCH_TTS_STATE_FAILED)
        tt_fail0("guest 意外返回");
    if (!g_boot_done) {
        g_boot_done = true;
        g_boot_result = -1;
        eventFire(&g_boot_event);
    }
}

/* ------------------------------------------------------------------ */
/* 初始化：铺内存、打补丁、映射、起引擎                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t text_off;
    uint32_t text_size;
    uint32_t need;
} tt_nso_info;

static bool tt_nso_need(const tt_nso *h, uint32_t *need)
{
    uint32_t n = 0;
    uint32_t off[3], sz[3];
    int i;

    off[0] = h->text_mem_off; sz[0] = h->text_size;
    off[1] = h->ro_mem_off;   sz[1] = h->ro_size;
    off[2] = h->data_mem_off; sz[2] = h->data_size;

    for (i = 0; i < 3; i++) {
        if (sz[i] == 0) continue;
        if (off[i] + sz[i] > n) n = off[i] + sz[i];
    }
    n += h->bss_size;
    if (n == 0) return false;
    *need = tt_align_up(n, 0x1000);
    return true;
}

static bool tt_nso_measure(const char *path, tt_nso_info *info)
{
    FILE *f = fopen(path, "rb");
    tt_nso h;
    uint32_t need;

    if (!f) return tt_fail("找不到引擎镜像：%s", path);
    if (fread(&h, 1, sizeof(h), f) != sizeof(h)) { fclose(f); return tt_fail0("NSO 头不完整"); }
    fclose(f);
    if (h.magic != 0x304F534Eu) return tt_fail("%s 不是 NSO0", path);
    if (!tt_nso_need(&h, &need)) return tt_fail("%s 段信息为空", path);
    info->text_off = h.text_mem_off;
    info->text_size = h.text_size;
    info->need = need;
    return true;
}

static bool tt_load_vtdb(const char *dir)
{
    char path[512];
    bool found = false;
    int i;

    static const char *const names[] = {
        "D32-YAFANG.vtdb2",
        "D32-YAFANG.vtdb",
    };

    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        g_vtdb = tt_read_file(path, &g_vtdb_len);
        if (g_vtdb) { found = true; break; }
    }
    if (!found) return tt_fail("找不到音库（需 D32-YAFANG.vtdb2）：%s", dir);
    CPYMO_DIAG("[tts] 音库 %s, %zu 字节\n", path, g_vtdb_len);
    return true;
}

typedef struct {
    uint32_t off;
    size_t size;
    bool executable;
    Handle handle;
    bool mapped;
} tt_code_piece;

/* CodeMemory gives each text range RX and each remaining range RW. */
static bool tt_map_code_memory(void)
{
    tt_code_piece pieces[TT_IMAGE_MAX * 3 + 1] = {0};
    size_t count = 0, j;
    uint32_t i;
    Result rc = 0;
    char buf[112];
    uint8_t *backing, *temp;
    VirtmemReservation *temp_rv;
    bool temp_mapped = false;

    for (i = 0; i < TT_IMAGE_MAX; i++) {
        uint32_t start = g_text_off[i];
        uint32_t end = tt_align_up(start + g_text_size[i], 0x1000);
        if ((start & 0xfff) || end > g_img_size[i] || end <= start)
            return tt_fail0("CodeMemory text 段范围无效");
        if (start)
            pieces[count++] = (tt_code_piece){g_img_off[i], start, false, INVALID_HANDLE, false};
        pieces[count++] = (tt_code_piece){g_img_off[i] + start, end - start, true, INVALID_HANDLE, false};
        if (end < g_img_size[i])
            pieces[count++] = (tt_code_piece){g_img_off[i] + end, g_img_size[i] - end, false, INVALID_HANDLE, false};
    }
    pieces[count++] = (tt_code_piece){g_tramp_off, g_window_size - g_tramp_off, true, INVALID_HANDLE, false};
    backing = (uint8_t *)tt_alloc(g_window_size);
    if (!backing) return tt_fail0("CodeMemory 后备内存分配失败");
    virtmemLock();
    temp = (uint8_t *)virtmemFindCodeMemory(g_window_size, 0x1000);
    temp_rv = temp ? virtmemAddReservation(temp, g_window_size) : NULL;
    virtmemUnlock();
    if (!temp_rv) return tt_fail0("CodeMemory 临时地址保留失败");

    for (j = 0; j < count; j++) {
        tt_code_piece *p = &pieces[j];
        uint8_t *write_addr = p->executable ? temp : g_rx + p->off;
        rc = svcCreateCodeMemory(&p->handle, backing + p->off, p->size);
        if (R_FAILED(rc)) break;
        rc = svcControlCodeMemory(p->handle, CodeMapOperation_MapOwner,
                                  write_addr, p->size, Perm_Rw);
        if (R_FAILED(rc)) break;
        if (p->executable) temp_mapped = true;
        else p->mapped = true;
        memcpy(write_addr, g_rw + p->off, p->size);
        if (p->executable) {
            armDCacheFlush(write_addr, p->size);
            rc = svcControlCodeMemory(p->handle, CodeMapOperation_UnmapOwner,
                                      write_addr, p->size, Perm_None);
            if (R_FAILED(rc)) break;
            temp_mapped = false;
            rc = svcControlCodeMemory(p->handle, CodeMapOperation_MapSlave,
                                      g_rx + p->off, p->size, Perm_Rx);
            if (R_FAILED(rc)) break;
            p->mapped = true;
        }
    }
    if (R_FAILED(rc)) {
        snprintf(buf, sizeof(buf), "CodeMemory 第 %u 段失败：0x%08x", (unsigned)j, (unsigned)rc);
        if (temp_mapped)
            svcControlCodeMemory(pieces[j].handle, CodeMapOperation_UnmapOwner,
                                 temp, pieces[j].size, Perm_None);
        for (j = count; j > 0; j--) {
            tt_code_piece *p = &pieces[j - 1];
            if (p->handle == INVALID_HANDLE) continue;
            if (p->mapped)
                svcControlCodeMemory(p->handle,
                                     p->executable ? CodeMapOperation_UnmapSlave : CodeMapOperation_UnmapOwner,
                                     g_rx + p->off, p->size, Perm_None);
            svcCloseHandle(p->handle);
        }
        virtmemLock();
        virtmemRemoveReservation(temp_rv);
        virtmemUnlock();
        return tt_fail0(buf);
    }

    virtmemLock();
    virtmemRemoveReservation(temp_rv);
    virtmemUnlock();
    /* Keep mappings and handles alive while the guest thread can execute. */
    armICacheInvalidate(g_rx, g_window_size);
    return true;
}

static bool tt_map_memory(void)
{
    Handle own = envGetOwnProcessHandle();
    Result rc;
    int i;

    if (own == INVALID_HANDLE) return tt_map_code_memory();

    armDCacheFlush(g_rw, g_window_size);
    rc = svcMapProcessCodeMemory(own, (u64)(uintptr_t)g_rx,
                                (u64)(uintptr_t)g_rw, (u64)g_window_size);
    if (R_FAILED(rc)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "svcMapProcessCodeMemory 失败：0x%08x", (unsigned)rc);
        return tt_fail0(buf);
    }

    /* 整段先按可写，再把各镜像的 text 段单独放开执行权限 */
    rc = svcSetProcessMemoryPermission(own, (u64)(uintptr_t)g_rx,
                                      (u64)g_window_size, Perm_Rw);
    if (R_FAILED(rc)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "整段 Perm_Rw 失败：0x%08x", (unsigned)rc);
        return tt_fail0(buf);
    }
    for (i = 0; i < TT_IMAGE_MAX; i++) {
        if (g_img_size[i] == 0) continue;
        if (g_text_size[i] == 0) continue;
        rc = svcSetProcessMemoryPermission(
            own,
            (u64)(uintptr_t)(g_rx + g_img_off[i] + g_text_off[i]),
            (u64)tt_align_up(g_text_size[i], 0x1000),
            Perm_Rx);
        if (R_FAILED(rc)) {
            char buf[96];
            snprintf(buf, sizeof(buf), "镜像 %d text 段 Perm_Rx 失败：0x%08x",
                     i, (unsigned)rc);
            return tt_fail0(buf);
        }
        CPYMO_DIAG("[tts] img %d text Rx: +0x%x 0x%x\n", i, g_text_off[i], g_text_size[i]);
    }
    /* trampoline 池在镜像之后，同样要可执行（含 .quad 常量，读没问题） */
    rc = svcSetProcessMemoryPermission(
        own,
        (u64)(uintptr_t)(g_rx + g_tramp_off),
        (u64)(g_window_size - g_tramp_off),
        Perm_Rx);
    if (R_FAILED(rc)) {
        char buf[96];
        snprintf(buf, sizeof(buf), "trampoline 池 Perm_Rx 失败：0x%08x", (unsigned)rc);
        return tt_fail0(buf);
    }

    armICacheInvalidate(g_rx, g_window_size);
    return true;
}

static bool tt_alloc_regions(void)
{
    g_vstack_size = 1u << 20;              /* rtld / nnMain 起跑栈 */
    g_cstack_size = 256u << 10;            /* 宿主回调 guest 用的栈 */
    g_inbuf_size = 2u << 20;
    g_outbuf_size = 0x10000;

    g_engine_heap = (uint8_t *)tt_alloc(TT_ENGINE_HEAP_SIZE);
    g_vstack = (uint8_t *)tt_alloc(g_vstack_size);
    g_cstack = (uint8_t *)tt_alloc(g_cstack_size);
    g_inbuf = (uint8_t *)tt_alloc(g_inbuf_size);
    g_outbuf = (uint8_t *)tt_alloc(g_outbuf_size);
    if (!g_engine_heap || !g_vstack || !g_cstack || !g_inbuf || !g_outbuf)
        return tt_fail0("guest 内存分配失败");

    memset(g_vstack, 0, g_vstack_size);
    memset(g_cstack, 0, g_cstack_size);
    return true;
}

static bool tt_apply_patches(void)
{
    static const struct {
        const char *name;
        int img;
        uint32_t off, opcode;
        void (*handler)(tt_regs *);
    } patches[] = {
        {"svc#1 SetHeapSize", TT_IMG_SDK, 0x18220c, 0xD4000021u, shim_set_heap_size},
        {"svc#2 SetMemoryPermission", TT_IMG_SDK, 0x182220, 0xD4000041u, shim_set_mem_perm},
        {"svc#3 SetMemoryAttribute", TT_IMG_SDK, 0x182228, 0xD4000061u, shim_set_mem_attr},
        {"svc#6 QueryMemory", TT_IMG_SDK, 0x182244, 0xD40000C1u, shim_query_memory},
        {"svc#41 GetInfo", TT_IMG_SDK, 0x1823d4, 0xD4000521u, shim_get_info},
        {"rtld svc#2", TT_IMG_RTLD, 0x1d18, 0xD4000041u, shim_set_mem_perm},
        {"rtld svc#3", TT_IMG_RTLD, 0x1d20, 0xD4000061u, shim_set_mem_attr},
        {"rtld svc#6", TT_IMG_RTLD, 0x1d2c, 0xD40000C1u, shim_query_memory},
    };
    uint32_t t_rng, t_boot;
    size_t i;
    for (i = 0; i < sizeof(patches) / sizeof(patches[0]); i++) {
        uint32_t next = g_img_off[patches[i].img] + patches[i].off + 4;
        uint32_t tramp = tt_add_tramp(patches[i].handler, next);
        if (!tramp || !tt_patch_branch(patches[i].name, patches[i].img,
                patches[i].off, patches[i].opcode, tramp, false)) return false;
    }
    t_rng = tt_add_tramp(shim_rng, 0);
    t_boot = tt_add_tramp(tt_boot_hook, 0);
    if (!t_rng || !t_boot) return tt_fail0("trampoline 池分配失败");

    /* 随机数：填零即可（只用于密钥材料，TTS 不走网络） */
    if (!tt_patch_branch("RNG", TT_IMG_SDK, 0xa7b50, 0xD10183FFu, t_rng, false)) return false;

    /* nninitInitializeSdkModule → 直接 ret */
    if (!tt_patch_word("nninitInitializeSdkModule", TT_IMG_SDK, 0x11c010,
                       0xA9BF7BFDu, 0xD65F03C0u)) return false;

    /* nnMain 入口：整个序言一起跳过，跳到我们的钩子 */
    if (!tt_patch_branch("nnMain 入口", TT_IMG_MAIN, 0x66e0, 0xD10083FFu, t_boot, false)) return false;

    return true;
}

static bool tt_prepare(const char *dir)
{
    static const char *const names[TT_IMAGE_MAX] = { "rtld.bin", "main.bin", "sdk.bin" };
    tt_nso_info info[TT_IMAGE_MAX];
    char path[512];
    uint32_t off = 0, i;
    void *resv;
    int n;

    for (i = 0; i < TT_IMAGE_MAX; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (!tt_nso_measure(path, &info[i])) return false;
        g_img_off[i] = off;
        g_img_size[i] = info[i].need;
        g_text_off[i] = info[i].text_off;
        g_text_size[i] = info[i].text_size;
        off += info[i].need;
    }
    g_tramp_off = off;
    g_window_size = (size_t)(off + 0x2000 + 0xFFF) & ~(size_t)0xFFF;
    CPYMO_DIAG("[tts] 窗口: rtld=0x%x main=0x%x sdk=0x%x tramp=0x%x total=0x%zx\n",
           g_img_off[0], g_img_off[1], g_img_off[2], g_tramp_off, g_window_size);

    virtmemLock();
    resv = virtmemFindCodeMemory(g_window_size, 0x1000);
    if (resv) g_code_rv = virtmemAddReservation(resv, g_window_size);
    virtmemUnlock();
    if (!resv || !g_code_rv) return tt_fail0("没有可用的代码内存");
    g_code_resv = resv;
    g_rx = (uint8_t *)resv;
    CPYMO_DIAG("[tts] 代码区 rx=0x%llx\n", (unsigned long long)(uintptr_t)g_rx);

    g_rw = (uint8_t *)tt_alloc(g_window_size);
    if (!g_rw) return tt_fail0("窗口分配失败");
    memset(g_rw, 0, g_window_size);

    for (i = 0; i < TT_IMAGE_MAX; i++) {
        uint32_t t_off = 0, t_sz = 0, need = 0;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (!tt_load_nso(path, g_rw + g_img_off[i], (uint32_t)g_img_size[i],
                         &t_off, &t_sz, &need))
            return false;
        g_img_va[i] = (uintptr_t)(g_rx + g_img_off[i]);
        CPYMO_DIAG("[tts] %s 铺到 0x%llx\n", names[i], (unsigned long long)g_img_va[i]);
    }

    if (!tt_alloc_regions()) return false;
    if (!tt_load_vtdb(dir)) return false;

    /* 顺序要求：rtld → main → sdk → vtdb（vtdb 在 heap，不参与相对寻址） */
    for (n = 0, i = 0; i < TT_IMAGE_MAX; i++) n += (int)g_img_size[i];
    CPYMO_DIAG("[tts] 镜像合计 0x%x 字节\n", n);

    if (!tt_apply_patches()) return false;
    if (!tt_map_memory()) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* SDL 音频输出                                                        */
/* ------------------------------------------------------------------ */

static bool tt_audio_init(void)
{
    __atomic_store_n(&g_audio_open, true, __ATOMIC_RELEASE);
    return true;
}

/* ------------------------------------------------------------------ */
/* 请求入队（主线程）                                                  */
/* ------------------------------------------------------------------ */

static void tt_push(const char *utf8, cpymo_switch_tts_mode mode)
{
    uint32_t head = g_req_head;
    uint32_t tail = __atomic_load_n(&g_req_tail, __ATOMIC_ACQUIRE);
    uint32_t slot;
    size_t len = strlen(utf8);

    if (len >= TT_TEXT_MAX) len = TT_TEXT_MAX - 1;

    if (mode != CPYMO_SWITCH_TTS_APPEND) {
        if (head > tail) head = tail;              /* 丢掉积压（可能丢掉正在读的那条，可接受） */
        __atomic_add_fetch(&g_req_reset_gen, 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&g_abort_gen, 1, __ATOMIC_RELEASE);
        if (mode == CPYMO_SWITCH_TTS_INTERRUPT)
            __atomic_add_fetch(&g_flush_gen, 1, __ATOMIC_RELEASE);
    } else if (head - tail >= TT_REQ_SLOTS) {
        return;                                    /* 队列满：宁可不读，也不阻塞主线程 */
    }

    slot = head % TT_REQ_SLOTS;
    memcpy(g_req_text[slot], utf8, len);
    g_req_text[slot][len] = 0;
    __atomic_store_n(&g_req_kind[slot], (uint32_t)mode, __ATOMIC_RELEASE);
    __atomic_store_n(&g_req_head, head + 1, __ATOMIC_RELEASE);
    eventFire(&g_cmd_event);
}

/* ------------------------------------------------------------------ */
/* 对外 API                                                            */
/* ------------------------------------------------------------------ */

void cpymo_switch_tts_params_default(cpymo_switch_tts_params *params)
{
    if (!params) return;
    memset(params, 0, sizeof(*params));
    params->voice = 11; /* D32-YAFANG，繁体中文 */
    params->pitch = 150;
    params->rate = 135;
    params->volume = 90;
    snprintf(params->emotion_category, sizeof(params->emotion_category), "%s", "happiness");
    params->emotion_level = 1;
}

bool cpymo_switch_tts_init(const char *assets_dir)
{
    if (g_state == CPYMO_SWITCH_TTS_STATE_READY) return true;
    if (g_state == CPYMO_SWITCH_TTS_STATE_FAILED) return false;
    if (!assets_dir || !*assets_dir) return tt_fail0("没有给 TTS 资产目录");

    g_state = CPYMO_SWITCH_TTS_STATE_LOADING;
    g_error[0] = 0;
    mutexLock(&g_params_mutex);
    if (g_params.voice == 0) cpymo_switch_tts_params_default(&g_params);
    mutexUnlock(&g_params_mutex);

    /* autoclear：等待即清除，与下面只用 eventWait 的用法配套 */
    eventCreate(&g_cmd_event, true);
    eventCreate(&g_boot_event, true);

    if (!tt_prepare(assets_dir)) return false;
    if (!tt_audio_init()) return false;

    g_thread_stack_size = 0x40000;
    g_thread_stack = (uint8_t *)tt_alloc(g_thread_stack_size);
    if (!g_thread_stack) return tt_fail0("线程栈分配失败");
    if (R_FAILED(threadCreate(&g_thread, tt_guest_thread_main, NULL,
                              g_thread_stack, g_thread_stack_size, 0x30, -2)))
        return tt_fail0("threadCreate 失败");
    if (R_FAILED(threadStart(&g_thread))) return tt_fail0("threadStart 失败");
    g_thread_started = true;

    if (R_FAILED(eventWait(&g_boot_event, 30000000000ull)))
        return tt_fail0("等待引擎初始化事件失败或超时（30s）");
    if (!g_boot_done || !g_engine_ok) {
        char buf[96];
        snprintf(buf, sizeof(buf), "引擎 init 返回 0x%llx",
                 (unsigned long long)(uint32_t)g_boot_result);
        return tt_fail0(buf);
    }

    g_state = CPYMO_SWITCH_TTS_STATE_READY;
    CPYMO_DIAG("[tts] 引擎就绪\n");
    return true;
}

void cpymo_switch_tts_free(void)
{
    if (g_state == CPYMO_SWITCH_TTS_STATE_IDLE) return;

    __atomic_store_n(&g_quit, true, __ATOMIC_RELEASE);
    __atomic_add_fetch(&g_flush_gen, 1, __ATOMIC_RELEASE);
    eventFire(&g_cmd_event);

    /* 引擎线程驻留在 guest 里，只能靠它自己退出；这里给 200ms 让它收尾 */
    if (g_thread_started) svcSleepThread(200000000ull);

    __atomic_store_n(&g_audio_open, false, __ATOMIC_RELEASE);
    /* 代码内存映射与 guest 区域有意不释放：线程可能还停在 guest 里，
       进程随后就退出了，操作系统会回收。 */
    g_state = CPYMO_SWITCH_TTS_STATE_IDLE;
}

bool cpymo_switch_tts_ready(void)
{
    return g_state == CPYMO_SWITCH_TTS_STATE_READY;
}

cpymo_switch_tts_state cpymo_switch_tts_state_get(void)
{
    return g_state;
}

const char *cpymo_switch_tts_last_error(void)
{
    return g_error[0] ? g_error : "";
}

void cpymo_switch_tts_speak(const char *utf8_text, cpymo_switch_tts_mode mode)
{
    if (!utf8_text || !*utf8_text) return;
    if (g_state != CPYMO_SWITCH_TTS_STATE_READY) return;
    tt_push(utf8_text, mode);
}

void cpymo_switch_tts_stop(void)
{
    if (g_state != CPYMO_SWITCH_TTS_STATE_READY) return;
    __atomic_add_fetch(&g_req_reset_gen, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&g_abort_gen, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&g_flush_gen, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&g_req_head, __atomic_load_n(&g_req_tail, __ATOMIC_ACQUIRE),
                     __ATOMIC_RELEASE);
}

void cpymo_switch_tts_set_params(const cpymo_switch_tts_params *params)
{
    if (!params) return;
    mutexLock(&g_params_mutex);
    g_params = *params;
    g_params.voice = 11; /* 目前只加载 D32-YAFANG 音库 */
    if (g_params.pitch < 1) g_params.pitch = 1;
    if (g_params.rate < 1) g_params.rate = 1;
    if (g_params.pitch > 1000) g_params.pitch = 1000;
    if (g_params.volume < 0) g_params.volume = 0;
    if (g_params.rate > 1000) g_params.rate = 1000;
    if (g_params.volume > 1000) g_params.volume = 1000;
    if (g_params.emotion_level < 0) g_params.emotion_level = 0;
    if (g_params.emotion_level > 10) g_params.emotion_level = 10;
    g_params.emotion_category[sizeof(g_params.emotion_category) - 1] = 0;
    mutexUnlock(&g_params_mutex);
}

void cpymo_switch_tts_get_params(cpymo_switch_tts_params *out)
{
    if (!out) return;
    mutexLock(&g_params_mutex);
    *out = g_params;
    mutexUnlock(&g_params_mutex);
}

/* 一切都在引擎线程里推进，宿主这边没有每帧要做的事 */
void cpymo_switch_tts_pump(void)
{
}

#endif /* __SWITCH__ && ENABLE_TEXT_EXTRACT_SWITCH_ACCESSIBILITY */
