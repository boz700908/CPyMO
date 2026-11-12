/*
 * CPyMO Nintendo Switch 原生 TTS —— 厂商 ReadSpeaker(Nuance Vocalizer) 引擎的裸机包装
 *
 * 背景：/root/rhtts-src 是 macOS 上用 Hypervisor.framework 跑这套引擎的模拟器。
 * NS 的 guest ISA 与 host 相同（都是 ARM64），所以这里把「模拟」整层剥掉，
 * 只保留它真正提供的东西：
 *
 *   1) 三个 NSO 镜像（rtld / main / sdk）解压后按相对顺序铺进一块 RWX 区域
 *   2) 7 个补丁字：把 5 个内存类 svc、RNG、以及 nnMain 的入口改道到自己的 shim
 *   3) 两个入口：main+0x519440 = 引擎 init，main+0x519aa0 = 分块合成
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
#define TT_MAX_CHUNK     12000     /* 引擎单块输出上限（样本数） */
#define TT_SAMPLE_RATE   32000
#define TT_IMAGE_MAX     3
#define TT_RING_SAMPLES  98304     /* 3.07s @32000Hz，2 的幂 */
#define TT_RING_BYTES    (TT_RING_SAMPLES * 2)
#define TT_RING_MASK     (TT_RING_BYTES - 1)

#define TT_IMG_RTLD      0
#define TT_IMG_MAIN      1
#define TT_IMG_SDK       2

/* 引擎关键偏移（相对各镜像基址，定稿见 /memories/cpymo-switch-a11y.md） */
#define OFF_MAIN_BOOT        0x6080    /* nnMain 序言，整函数换成 b tramp_boot */
#define OFF_MAIN_INIT        0x519440  /* 引擎 init(language, ?, vtdb, len, 0, 0) */
#define OFF_MAIN_SYNTH       0x519aa0  /* 分块合成 */
#define OFF_MAIN_HEAP_GLOBAL 0x10ba098 /* 源：全局堆指针 */
#define OFF_MAIN_HEAP_SLOT   0x110c638 /* 目标：引擎期望的堆指针落点 */
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
    "    sub  sp, sp, #0xA0\n"
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
    "    add  x9, sp, #0xA0\n"
    "    str  x9,       [sp, #0x98]\n"
    "    mov  x0, sp\n"
    "    ldr  x16, 1f\n"
    "    blr  x16\n"
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
    "    add  sp, sp, #0xA0\n"
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
    "    stp  x19, x20, [sp, #-32]!\n"
    "    stp  x21, x22, [sp, #16]\n"
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
    "    ldp  x21, x22, [sp, #16]\n"
    "    ldp  x19, x20, [sp], #32\n"
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
    void *p = NULL;
    if (size == 0) return NULL;
    if (posix_memalign(&p, 0x1000, (size + 0xFFF) & ~(size_t)0xFFF) != 0) return NULL;
    return p;
}

static bool tt_fail(const char *fmt, const char *arg)
{
    snprintf(g_error, sizeof(g_error), fmt, arg ? arg : "");
    g_state = CPYMO_SWITCH_TTS_STATE_FAILED;
    printf("[tts] FAIL: %s\n", g_error);
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
    if (o > dst_len) return false;
    if (o < dst_len) memset(dst + o, 0, dst_len - o);
    return true;
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
            if (clen > avail) clen = avail;
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
    printf("[tts] NSO %s: size=0x%x text=0x%x+0x%x bss=0x%x\n",
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
    uint64_t want = r->x[0];

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
            printf("[tts] SetHeapSize 请求 0x%llx -> 给 0x%zx @0x%llx\n",
                   (unsigned long long)want, g_heap_size,
                   (unsigned long long)g_heap_va);
        }
    } else if (want > (uint64_t)g_heap_size) {
        printf("[tts] 警告：再次请求 0x%llx > 已有 0x%zx\n",
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
    struct {
        uintptr_t base;
        size_t size;
        uint32_t type;
        uint32_t perm;
    } tbl[8];
    int n = 0, i;

    memset(m, 0, sizeof(*m));

    if (g_rx && a >= (uintptr_t)g_rx && a < (uintptr_t)g_rx + g_window_size) {
        tbl[n].base = (uintptr_t)g_rx; tbl[n].size = g_window_size;
        tbl[n].type = TT_MT_CODE_STATIC; tbl[n].perm = TT_PM_R | TT_PM_X; n++;
    }
    if (g_heap_mem && a >= (uintptr_t)g_heap_mem && a < (uintptr_t)g_heap_mem + g_heap_size) {
        tbl[n].base = (uintptr_t)g_heap_mem; tbl[n].size = g_heap_size;
        tbl[n].type = TT_MT_HEAP; tbl[n].perm = TT_PM_R | TT_PM_W; n++;
    }
    if (g_vstack && a >= (uintptr_t)g_vstack && a < (uintptr_t)g_vstack + g_vstack_size) {
        tbl[n].base = (uintptr_t)g_vstack; tbl[n].size = g_vstack_size;
        tbl[n].type = TT_MT_NORMAL; tbl[n].perm = TT_PM_R | TT_PM_W; n++;
    }
    if (g_cstack && a >= (uintptr_t)g_cstack && a < (uintptr_t)g_cstack + g_cstack_size) {
        tbl[n].base = (uintptr_t)g_cstack; tbl[n].size = g_cstack_size;
        tbl[n].type = TT_MT_NORMAL; tbl[n].perm = TT_PM_R | TT_PM_W; n++;
    }
    if (g_inbuf && a >= (uintptr_t)g_inbuf && a < (uintptr_t)g_inbuf + g_inbuf_size) {
        tbl[n].base = (uintptr_t)g_inbuf; tbl[n].size = g_inbuf_size;
        tbl[n].type = TT_MT_NORMAL; tbl[n].perm = TT_PM_R | TT_PM_W; n++;
    }
    if (g_outbuf && a >= (uintptr_t)g_outbuf && a < (uintptr_t)g_outbuf + g_outbuf_size) {
        tbl[n].base = (uintptr_t)g_outbuf; tbl[n].size = g_outbuf_size;
        tbl[n].type = TT_MT_NORMAL; tbl[n].perm = TT_PM_R | TT_PM_W; n++;
    }
    if (g_vtdb && a >= (uintptr_t)g_vtdb && a < (uintptr_t)g_vtdb + g_vtdb_len) {
        tbl[n].base = (uintptr_t)g_vtdb; tbl[n].size = g_vtdb_len;
        tbl[n].type = TT_MT_NORMAL; tbl[n].perm = TT_PM_R | TT_PM_W; n++;
    }

    for (i = 0; i < n; i++) {
        if (a >= tbl[i].base && a < tbl[i].base + tbl[i].size) {
            uintptr_t pg = a & ~(uintptr_t)0xFFF;
            m->base = pg;
            m->size = (tbl[i].base + tbl[i].size) - pg;
            m->type = tbl[i].type;
            m->perm = tbl[i].perm;
            return true;
        }
    }

    m->base = a & ~(uintptr_t)0xFFF;
    m->size = 0x1000;
    m->type = 0;
    m->perm = TT_PM_R;
    return false;
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
    /* svcGetInfo(u64 *out, InfoType id, Handle handle, u64 subtype) */
    uint64_t *out = (uint64_t *)(uintptr_t)r->x[0];
    uint64_t id = r->x[1];
    uint64_t v = 0;

    switch (id) {
        case 21: v = (uint64_t)g_heap_va + (uint64_t)g_heap_size; break;
        case 22: v = (uint64_t)g_heap_va; break;
        default: v = 0; break;
    }
    if (out) *out = v;
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

#define TT_TRAMP_STRIDE 0x80   /* 模板 120 字节，留余量 */

static uint32_t tt_add_tramp(void (*fn)(tt_regs *))
{
    size_t len = (size_t)(cpymo_sw_tts_tramp_end - cpymo_sw_tts_tramp_start);
    uint8_t *slot = g_rw + g_tramp_off + (size_t)g_tramp_slot * TT_TRAMP_STRIDE;
    uint64_t addr = (uint64_t)(uintptr_t)fn;
    uint32_t off = g_tramp_off + g_tramp_slot * TT_TRAMP_STRIDE;

    if (len > TT_TRAMP_STRIDE) return 0;        /* 理论上不会发生 */
    memcpy(slot, cpymo_sw_tts_tramp_start, len);
    memcpy(slot + len - 8, &addr, 8);
    g_tramp_slot++;
    printf("[tts] tramp #%u for %p\n", g_tramp_slot - 1, (void *)fn);
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
        printf("[tts] %s (img=%d off=0x%x)\n", buf, img, off);
        return tt_fail0(buf);
    }
    if ((delta & 3) != 0) return tt_fail("分支目标未对齐：%s", what);
    delta >>= 2;
    if (delta < -(1 << 25) || delta >= (1 << 25))
        return tt_fail("分支超出 ±128MB：%s", what);

    insn = (link ? 0x94000000u : 0x14000000u) | ((uint32_t)delta & 0x03FFFFFFu);
    *(uint32_t *)(void *)(g_rw + g_img_off[img] + off) = insn;
    printf("[tts] patch %s: img=%d off=0x%x -> 0x%x (%s)\n",
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
        printf("[tts] %s (img=%d off=0x%x)\n", buf, img, off);
        return tt_fail0(buf);
    }
    *(uint32_t *)(void *)(g_rw + g_img_off[img] + off) = new_word;
    printf("[tts] patch %s: img=%d off=0x%x = 0x%08x\n", what, img, off, new_word);
    return true;
}

/* ------------------------------------------------------------------ */
/* PCM 环形缓冲（引擎线程写，SDL 音频回调读，全程无锁）                  */
/* ------------------------------------------------------------------ */

static void tt_audio_cb(void *ud, Uint8 *stream, int len)
{
    uint32_t flush, r, w;
    int n = 0;

    (void)ud;

    flush = __atomic_load_n(&g_flush_gen, __ATOMIC_ACQUIRE);
    if (flush != g_flush_seen) {
        g_flush_seen = flush;
        w = __atomic_load_n(&g_ring_w, __ATOMIC_ACQUIRE);
        __atomic_store_n(&g_ring_r, w, __ATOMIC_RELEASE);
    }

    r = g_ring_r;
    w = __atomic_load_n(&g_ring_w, __ATOMIC_ACQUIRE);
    while (n < len && r != w) {
        stream[n++] = g_ring[r & TT_RING_MASK];
        r++;
    }
    __atomic_store_n(&g_ring_r, r, __ATOMIC_RELEASE);
    if (n < len) SDL_memset(stream + n, 0, (size_t)(len - n));
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

static size_t tt_build_input(const char *text)
{
    static const char close_tag[] = "</vtml_emotion>";
    size_t cap = g_inbuf_size, n = 0, i, cl;

    if (g_params.emotion_level > 0) {
        int w = snprintf((char *)g_inbuf, cap,
                         "<vtml_emotion category=\"%s\" level=\"%d\">",
                         g_params.emotion_category, g_params.emotion_level);
        if (w < 0 || (size_t)w >= cap) return 0;
        n = (size_t)w;
    }
    for (i = 0; text[i]; i++) {
        if (n + 1 >= cap) break;
        g_inbuf[n++] = (text[i] == '<') ? '>' : (uint8_t)text[i];
    }
    if (g_params.emotion_level > 0) {
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

    if (!g_inbuf || !g_outbuf || !g_cstack) return false;
    in_len = tt_build_input(text);
    if (in_len == 0) return false;

    for (;;) {
        tt_call c;
        int64_t rc;
        uint32_t got;

        if (__atomic_load_n(&g_abort_gen, __ATOMIC_ACQUIRE) != abort_gen) return true;
        if (__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) return true;

        memset(frame, 0, 0x70);
        *(uint64_t *)(void *)(frame + 0x08) = (uint64_t)(uintptr_t)(frame + 0x58);
        *(uint64_t *)(void *)(frame + 0x10) = step;
        *(uint64_t *)(void *)(frame + 0x20) = 0xffffffffull;
        *(uint64_t *)(void *)(frame + 0x28) = (uint64_t)g_params.pitch;
        *(uint64_t *)(void *)(frame + 0x30) = (uint64_t)g_params.rate;
        *(uint64_t *)(void *)(frame + 0x38) = (uint64_t)g_params.volume;
        *(uint64_t *)(void *)(frame + 0x40) = 400;
        *(uint64_t *)(void *)(frame + 0x50) = 0xffffffffull;

        memset(&c, 0, sizeof(c));
        c.fn = (uint64_t)g_img_va[TT_IMG_MAIN] + OFF_MAIN_SYNTH;
        c.sp = (uint64_t)sp;
        c.x[0] = (uint64_t)g_params.voice;
        c.x[1] = 0;
        c.x[2] = (uint64_t)(uintptr_t)g_inbuf;
        c.x[3] = (uint64_t)in_len;
        c.x[4] = (uint64_t)(uintptr_t)g_outbuf;
        c.x[5] = (uint64_t)(uintptr_t)(frame + 0x60);
        c.x[6] = 0;
        c.x[7] = (uint64_t)(uintptr_t)(frame + 0x58);
        cpymo_sw_tts_call_guest(&c);

        rc = (int64_t)c.result;
        if (rc < 0) {
            printf("[tts] synth 返回 %lld\n", (long long)rc);
            return false;
        }

        got = *(uint32_t *)(void *)(frame + 0x60);
        if (got > TT_MAX_CHUNK) got = TT_MAX_CHUNK;
        if (got) {
            if (!tt_wait_ring_space((size_t)got * 2, 20000000000ull)) return false;
            tt_ring_write(g_outbuf, (size_t)got * 2);
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
 * nnMain 入口钩子：main+0x6080 被换成 b 到这里。
 * 原来那句函数体（全局堆指针拷贝）由 C 复刻，然后调引擎 init，
 * 之后这条 guest 线程就变成 TTS 引擎线程，永不返回。
 */
static void tt_boot_hook(tt_regs *r)
{
    uint8_t *m = (uint8_t *)g_img_va[TT_IMG_MAIN];
    tt_call c;

    (void)r;
    printf("[tts] nnMain 入口钩子命中\n");

    {
        uint64_t v = *(uint64_t *)(void *)(m + OFF_MAIN_HEAP_GLOBAL);
        *(uint64_t *)(void *)(m + OFF_MAIN_HEAP_SLOT) = v;
        printf("[tts] 全局堆指针 = 0x%llx\n", (unsigned long long)v);
    }

    memset(&c, 0, sizeof(c));
    c.fn = (uint64_t)g_img_va[TT_IMG_MAIN] + OFF_MAIN_INIT;
    c.sp = ((uint64_t)(uintptr_t)g_cstack + g_cstack_size - 0x100) & ~15ull;
    c.x[0] = (uint64_t)g_params.voice;
    c.x[1] = 0xffffffffull;
    c.x[2] = (uint64_t)(uintptr_t)g_vtdb;
    c.x[3] = (uint64_t)g_vtdb_len;
    c.x[4] = 0;
    c.x[5] = 0;
    cpymo_sw_tts_call_guest(&c);

    g_boot_result = (int64_t)(int32_t)c.result;
    g_engine_ok = (g_boot_result >= 0);
    g_boot_done = true;
    if (!g_engine_ok) g_state = CPYMO_SWITCH_TTS_STATE_FAILED;
    printf("[tts] 引擎 init -> %lld\n", (long long)g_boot_result);
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
    printf("[tts] guest 从 rtld 返回了（异常）\n");
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
    printf("[tts] 音库 %s, %zu 字节\n", path, g_vtdb_len);
    return true;
}

static bool tt_map_memory(void)
{
    Handle own = envGetOwnProcessHandle();
    Result rc;
    int i;

    if (own == INVALID_HANDLE) return tt_fail0("拿不到自身进程句柄");

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
        printf("[tts] img %d text Rx: +0x%x 0x%x\n", i, g_text_off[i], g_text_size[i]);
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

    g_vstack = (uint8_t *)tt_alloc(g_vstack_size);
    g_cstack = (uint8_t *)tt_alloc(g_cstack_size);
    g_inbuf = (uint8_t *)tt_alloc(g_inbuf_size);
    g_outbuf = (uint8_t *)tt_alloc(g_outbuf_size);
    if (!g_vstack || !g_cstack || !g_inbuf || !g_outbuf)
        return tt_fail0("guest 内存分配失败");

    memset(g_vstack, 0, g_vstack_size);
    memset(g_cstack, 0, g_cstack_size);
    return true;
}

static bool tt_apply_patches(void)
{
    uint32_t t_heap, t_perm, t_attr, t_query, t_info, t_rng, t_boot;

    t_heap = tt_add_tramp(shim_set_heap_size);
    t_perm = tt_add_tramp(shim_set_mem_perm);
    t_attr = tt_add_tramp(shim_set_mem_attr);
    t_query = tt_add_tramp(shim_query_memory);
    t_info = tt_add_tramp(shim_get_info);
    t_rng = tt_add_tramp(shim_rng);
    t_boot = tt_add_tramp(tt_boot_hook);
    if (!t_heap || !t_perm || !t_attr || !t_query || !t_info || !t_rng || !t_boot)
        return tt_fail0("trampoline 池分配失败");

    /* sdk 的 nn::svc::aarch64::* 包装表：只改会伤到我们自己的那几个 */
    if (!tt_patch_branch("svc#1 SetHeapSize", TT_IMG_SDK, 0x18220c, 0xD4000021u, t_heap, true)) return false;
    if (!tt_patch_branch("svc#2 SetMemoryPermission", TT_IMG_SDK, 0x182220, 0xD4000041u, t_perm, true)) return false;
    if (!tt_patch_branch("svc#3 SetMemoryAttribute", TT_IMG_SDK, 0x182228, 0xD4000061u, t_attr, true)) return false;
    if (!tt_patch_branch("svc#6 QueryMemory", TT_IMG_SDK, 0x182244, 0xD40000C1u, t_query, true)) return false;
    if (!tt_patch_branch("svc#41 GetInfo", TT_IMG_SDK, 0x1823d4, 0xD4000521u, t_info, true)) return false;

    /* rtld 自己也查一次内存上限 */
    if (!tt_patch_branch("rtld svc#2", TT_IMG_RTLD, 0x1d18, 0xD4000041u, t_perm, true)) return false;
    if (!tt_patch_branch("rtld svc#3", TT_IMG_RTLD, 0x1d20, 0xD4000061u, t_attr, true)) return false;
    if (!tt_patch_branch("rtld svc#6", TT_IMG_RTLD, 0x1d2c, 0xD40000C1u, t_query, true)) return false;

    /* 随机数：填零即可（只用于密钥材料，TTS 不走网络） */
    if (!tt_patch_branch("RNG", TT_IMG_SDK, 0xa7b50, 0xD10183FFu, t_rng, true)) return false;

    /* nninitInitializeSdkModule → 直接 ret */
    if (!tt_patch_word("nninitInitializeSdkModule", TT_IMG_SDK, 0x11c010,
                       0xA9BF7BFDu, 0xD65F03C0u)) return false;

    /* nnMain 入口：整个序言一起跳过，跳到我们的钩子 */
    if (!tt_patch_branch("nnMain 入口", TT_IMG_MAIN, 0x6080, 0xD10083FFu, t_boot, false)) return false;

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
    g_window_size = (size_t)(off + 0x1000 + 0xFFF) & ~(size_t)0xFFF;
    printf("[tts] 窗口: rtld=0x%x main=0x%x sdk=0x%x tramp=0x%x total=0x%zx\n",
           g_img_off[0], g_img_off[1], g_img_off[2], g_tramp_off, g_window_size);

    virtmemLock();
    resv = virtmemFindCodeMemory(g_window_size, 0x1000);
    if (resv) g_code_rv = virtmemAddReservation(resv, g_window_size);
    virtmemUnlock();
    if (!resv || !g_code_rv) return tt_fail0("没有可用的代码内存");
    g_code_resv = resv;
    g_rx = (uint8_t *)resv;
    printf("[tts] 代码区 rx=0x%llx\n", (unsigned long long)(uintptr_t)g_rx);

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
        printf("[tts] %s 铺到 0x%llx\n", names[i], (unsigned long long)g_img_va[i]);
    }

    if (!tt_alloc_regions()) return false;
    if (!tt_load_vtdb(dir)) return false;

    /* 顺序要求：rtld → main → sdk → vtdb（vtdb 在 heap，不参与相对寻址） */
    for (n = 0, i = 0; i < TT_IMAGE_MAX; i++) n += (int)g_img_size[i];
    printf("[tts] 镜像合计 0x%x 字节\n", n);

    if (!tt_apply_patches()) return false;
    if (!tt_map_memory()) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* SDL 音频输出                                                        */
/* ------------------------------------------------------------------ */

static bool tt_audio_init(void)
{
    SDL_AudioSpec want, have;

    SDL_memset(&want, 0, sizeof(want));
    want.freq = TT_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = tt_audio_cb;
    want.userdata = NULL;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return tt_fail("SDL 音频子系统启动失败：%s", SDL_GetError());

    g_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_adev) return tt_fail("打开 TTS 音频设备失败：%s", SDL_GetError());
    if (have.freq != TT_SAMPLE_RATE)
        printf("[tts] 注意：实际采样率 %d（期望 %d）\n", have.freq, TT_SAMPLE_RATE);
    g_audio_open = true;
    SDL_PauseAudioDevice(g_adev, 0);
    printf("[tts] 音频设备已开：%d Hz %d ch\n", have.freq, have.channels);
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
    params->voice = 10;
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
    if (g_params.voice == 0) cpymo_switch_tts_params_default(&g_params);

    eventCreate(&g_cmd_event);
    eventCreate(&g_boot_event);

    if (!tt_prepare(assets_dir)) return false;
    if (!tt_audio_init()) return false;

    g_thread_stack_size = 0x40000;
    g_thread_stack = (uint8_t *)tt_alloc(g_thread_stack_size);
    if (!g_thread_stack) return tt_fail0("线程栈分配失败");
    if (R_FAILED(threadCreate(&g_thread, tt_guest_thread_main, NULL,
                              g_thread_stack, g_thread_stack_size, 0x30)))
        return tt_fail0("threadCreate 失败");
    if (R_FAILED(threadStart(&g_thread))) return tt_fail0("threadStart 失败");
    g_thread_started = true;

    if (!eventWait(&g_boot_event, 30000000000ull))
        return tt_fail0("引擎初始化超时（30s）");
    if (!g_boot_done || !g_engine_ok) {
        char buf[96];
        snprintf(buf, sizeof(buf), "引擎 init 返回 0x%llx",
                 (unsigned long long)(uint32_t)g_boot_result);
        return tt_fail0(buf);
    }

    g_state = CPYMO_SWITCH_TTS_STATE_READY;
    printf("[tts] 引擎就绪\n");
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

    if (g_audio_open) {
        SDL_CloseAudioDevice(g_adev);
        g_audio_open = false;
        g_adev = 0;
    }
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
    g_params = *params;
    if (g_params.pitch < 1) g_params.pitch = 1;
    if (g_params.rate < 1) g_params.rate = 1;
    if (g_params.volume < 0) g_params.volume = 0;
    if (g_params.volume > 1000) g_params.volume = 1000;
    if (g_params.emotion_level < 0) g_params.emotion_level = 0;
    if (g_params.emotion_level > 10) g_params.emotion_level = 10;
    g_params.emotion_category[sizeof(g_params.emotion_category) - 1] = 0;
}

void cpymo_switch_tts_get_params(cpymo_switch_tts_params *out)
{
    if (out) *out = g_params;
}

/* 一切都在引擎线程里推进，宿主这边没有每帧要做的事 */
void cpymo_switch_tts_pump(void)
{
}

#endif /* __SWITCH__ && ENABLE_TEXT_EXTRACT_SWITCH_ACCESSIBILITY */
