/**
 * @file tlsf.c
 * @brief Two level Segregated Fit allocater implementation.
 * @author mopp
 * @version 0.1
 * @date 2014-09-29
 */

#include "minunit.h"
#include "elist.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>



typedef struct {
    Elist list;
    void* addr;
    size_t size;
} Frame;


/*
 * NOTE: First level
 *          2^n < size ≤ 2^(n+1) の n
 *       Second level
 *       L2 を 2^4 = 16分割
 *
 *       Power of 2
 *       Area
 *       00 - 01 = 0000 - 0002
 *       01 - 02 = 0002 - 0004
 *       02 - 03 = 0004 - 0008
 *       03 - 04 = 0008 - 0016
 *       04 - 05 = 0016 - 0032
 *                  (1 byte * 16) で分割
 *       05 - 06 = 0032 - 0064
 *                  (2 byte * 16) で分割
 *       06 - 07 = 0064 - 0128
 *                  (4 byte * 16) で分割
 *       07 - 08 = 0128 - 0256
 *                  (8 byte * 16) で分割
 *       08 - 09 = 0512 - 1024
 *                  (32 byte * 16) で分割

 *      ここまでをまとめる
 *       00 - 09 = 0000 - 1024
 *                  (64 byte * 16)
 *       09 - 10 = 1024 - 2048
 *                  (64 byte * 16) で分割
 *       11 - 12 = 2048 - 4096
 *                  (128 byte * 16) で分割
 *       12 - 13 = 4096 - 8192
 *                  (256 byte * 16) で分割
 *
 *      1024 byte 以下はひとまとめのflリストとする.
 */

#define P2(x) (1u << (x))


struct block {
    struct block* prev_block; /* Liner previous block */
    Elist list;               /* Logical previous and next block. */
    union {
        struct {
            uint8_t is_free : 1;
            uint8_t is_free_prev : 1;
            size_t dummy : (sizeof(size_t) * 8 - 2);
        };
        size_t size;
    };
};
typedef struct block Block;


enum {
    ALIGNMENT_LOG2         = 2,
    ALIGNMENT_SIZE         = (1 << ALIGNMENT_LOG2),
    ALIGNMENT_MASK         = 0x3,

    SL_MAX_INDEX_LOG2      = 4,
    FL_BASE_INDEX          = 10 - 1,
    FL_MAX_INDEX           = (32 - FL_BASE_INDEX),
    SL_MAX_INDEX           = (1 << SL_MAX_INDEX_LOG2),

    FL_BLOCK_MIN_SIZE      = (1 << (FL_BASE_INDEX + 1)),
    SL_BLOCK_MIN_SIZE_LOG2 = (FL_BASE_INDEX + 1 - SL_MAX_INDEX_LOG2),
    SL_BLOCK_MIN_SIZE      = (1 << SL_BLOCK_MIN_SIZE_LOG2),
    BLOCK_MEMORY_OFFSET    = sizeof(Block),
};
/* static_assert((ALLOCATE_MIN_SIZE & ALIGNMENT_MASK) == 0, "ALLOCATE_MIN_SIZE is align error"); */

static size_t block_flag_bit_free = 0x01;
static size_t block_flag_bit_prev_free = 0x02;
static size_t block_flag_mask = 0x03;


struct tlsf_manager {
    Elist blocks[FL_MAX_INDEX * SL_MAX_INDEX];
    Elist frames;
    size_t total_memory_size;
    size_t free_memory_size;
    uint32_t fl_bitmap;
    uint16_t sl_bitmaps[FL_MAX_INDEX];
};
typedef struct tlsf_manager Tlsf_manager;

static bool is_debug = false;

static inline void print_block(Block* b, size_t tab);
static inline void print_tlsf(Tlsf_manager * tman);
static inline void mprintf(char const * fmt, ...){
    if (is_debug == false) {
       return;
    }
    va_list arg;
    va_start(arg, fmt);
    vprintf(fmt, arg);
    va_end(arg);
}

#if 0
/* FIXME: */
#define BIT_NR(type) (sizeof(type) * 8u)
static inline size_t find_set_bit_idx_first(size_t n) {
    /* return __builtin_ffs(n) - 1u; */

    size_t mask = 1u;
    size_t idx = 0;
    while (((mask & n) == 0) && (mask <<= 1) != ~0u) {
        ++idx;
    }

    return idx;
}


static inline size_t find_set_bit_idx_last(size_t n) {
    /* return (n ? 32u - __builtin_clz(n) : 0u) - 1u; */

    size_t mask = ((size_t)1u << (BIT_NR(size_t) - 1u));
    size_t idx = BIT_NR(size_t);
    do {
        --idx;
    } while (((mask & n) == 0) && (mask >>= 1) != 0);

    return idx;
}
#endif

__asm__ (
        "find_set_bit_idx_last: \n\t"
        "movq %rdi, %rax        \n\t"
        "bsrq %rax, %rax        \n\t"
        "ret                    \n\t"
    );
size_t find_set_bit_idx_last(size_t);

__asm__ (
        "find_set_bit_idx_first: \n\t"
        "movq   %rdi, %rax        \n\t"
        "tzcntq %rax, %rax        \n\t"
        "ret                    \n\t"
    );
size_t find_set_bit_idx_first(size_t);

static size_t sl_index_mask = 1u << SL_MAX_INDEX_LOG2;
static inline void set_idxs(size_t size, size_t* fl, size_t* sl) {
    if (size < FL_BLOCK_MIN_SIZE) {
        *fl = 0;
        *sl = size >> (SL_BLOCK_MIN_SIZE_LOG2);
    } else {
        /* Calculate First level index. */
        *fl = find_set_bit_idx_last(size);

        /* Calculate Second level index. */
        *sl = (size >> (*fl - SL_MAX_INDEX_LOG2)) ^ (sl_index_mask);

        /* Shift index. */
        *fl -= FL_BASE_INDEX;
    }
}


static inline size_t align_up(size_t x) {
    return (x + (ALIGNMENT_SIZE - 1u)) & ~(ALIGNMENT_SIZE - 1u);
}


// static inline size_t align_down(size_t x) {
//     return x - (x & (ALIGNMENT_SIZE - 1u));
// }


static inline size_t adjust_size(size_t size) {
    return align_up(size);
}


static inline size_t get_size(Block const* b) {
    return b->size & (~block_flag_mask);
}


static inline bool is_sentinel(Block const* const b) {
    return (get_size(b) == 0) ? (true) : false;
}


static inline Block* get_phys_next_block(Block const* const b) {
    return (Block*)((uintptr_t)b + (uintptr_t)BLOCK_MEMORY_OFFSET + (uintptr_t)get_size(b));
}


static inline Block* get_phys_prev_block(Block const* const b) {
    return b->prev_block;
}


static inline void set_prev_free(Block* b) {
    b->size |= block_flag_bit_prev_free;
}


static inline void clear_prev_free(Block* b) {
    b->size &= ~block_flag_bit_prev_free;
}


static inline void set_free(Block* b) {
    b->size |= block_flag_bit_free;
    set_prev_free(get_phys_next_block(b));
}


static inline void claer_free(Block* b) {
    b->size &= ~block_flag_bit_free;
    clear_prev_free(get_phys_next_block(b));
}


static inline void set_size(Block* b, size_t s) {
    b->size = ((b->size & block_flag_mask) | s);
}


static inline Block* generate_block(void* mem, size_t size) {
    assert((size & ALIGNMENT_MASK) == 0);

    Block* b = mem;
    b->size = size - BLOCK_MEMORY_OFFSET;
    b->prev_block = NULL;
    elist_init(&b->list);

    assert(ALIGNMENT_SIZE <= b->size);

    return b;
}


static inline void* convert_mem_ptr(Block const* b) {
    assert(b != NULL);
    return (void*)((uintptr_t)b + (uintptr_t)BLOCK_MEMORY_OFFSET);
}


static inline Block* convert_block(void const * p) {
    assert(p != NULL);
    return (Block*)((uintptr_t)p - (uintptr_t)BLOCK_MEMORY_OFFSET);
}


static inline Elist* get_block_list_head(Tlsf_manager* const tman, size_t fl, size_t sl) {
    return &tman->blocks[fl * sizeof(Elist) + sl];
}

static inline bool is_fl_list_available(Tlsf_manager const* const tman, size_t fl) {
    return ((tman->fl_bitmap & P2(fl)) != 0) ? true : false;
}


static inline bool is_sl_list_available(Tlsf_manager const* const tman, size_t fl, size_t sl) {
    return ((tman->sl_bitmaps[fl] & P2(sl)) != 0) ? true : false;
}


static inline void insert_block(Tlsf_manager* const tman, Block* b) {
    mprintf("insert_block\n");

    assert(is_sentinel(b) == false);

    size_t fl, sl;
    size_t s = get_size(b);
    assert(ALIGNMENT_SIZE <= s);

    set_idxs(s, &fl, &sl);

    mprintf("  Block size = 0x%zx (%zu), fl = %zu, sl = %zu, ptr = %p\n", get_size(b), get_size(b), fl, sl, b);
    mprintf("  bitmap fl = 0x%08x, sl = 0x%08x\n", tman->fl_bitmap, tman->sl_bitmaps[fl]);

    tman->fl_bitmap      |= P2(fl);
    tman->sl_bitmaps[fl] |= P2(sl);

    mprintf("  bitmap fl = 0x%08x, sl = 0x%08x\n", tman->fl_bitmap, tman->sl_bitmaps[fl]);
    print_block(b, 2);
    elist_insert_next(get_block_list_head(tman, fl, sl), &b->list);
}


static inline void sync_bitmap(Tlsf_manager* tman, size_t fl, size_t sl) {
    Elist* head = get_block_list_head(tman, fl, sl);

    if (elist_is_empty(head) == true) {
        uint16_t* sb = &tman->sl_bitmaps[fl];
        *sb &= ~P2(sl);
        if (*sb == 0) {
            tman->fl_bitmap &= ~P2(fl);
        }
    }
}


static inline void sync_bitmap_by_block(Tlsf_manager* tman, Block* b) {
    if (b->is_free == 0) {
        return;
    }

    size_t fl, sl;
    set_idxs(get_size(b), &fl, &sl);
    sync_bitmap(tman, fl, sl);
}


static inline Block* remove_block(Tlsf_manager* tman, size_t fl, size_t sl) {
    mprintf("remove_block\n");
    Elist* head = get_block_list_head(tman, fl, sl);
    assert(elist_is_empty(head) == false);

    Block* b = elist_derive(Block, list, elist_remove(head->next));
    assert(b != NULL);
    print_block(b, 2);

    sync_bitmap(tman, fl, sl);

    return b;
}


static inline Block* remove_good_block(Tlsf_manager* tman, size_t size) {
    size += BLOCK_MEMORY_OFFSET;

    /*
     * ここで、要求サイズ以上の内で、最も大きい範囲に繰り上げを行うことによって
     * 内部フラグメントは生じるが、外部フラグメント、構造フラグメントを抑えることが出来る.
     */
    if (FL_BLOCK_MIN_SIZE <= size) {
        size += P2(find_set_bit_idx_last(size) - SL_MAX_INDEX_LOG2) - 1;
    } else {
        size += SL_BLOCK_MIN_SIZE;
    }

    size_t fl, sl;
    set_idxs(size, &fl, &sl);

    /* 現在のsl以上のフラグのみ取得 */
    size_t sl_map = tman->sl_bitmaps[fl] & (~0u << sl);
    if (sl_map == 0) {
        /* 現在のflにはメモリが無いので、一つ上のindexのフラグを取得 */
        size_t fl_map = tman->fl_bitmap & (~0u << (fl + 1));
        if (fl_map == 0) {
            return NULL;
        }

        /* 大きい空きエリアを探す. */
        fl = find_set_bit_idx_first(fl_map);

        sl_map = tman->sl_bitmaps[fl];
    }
    /* 使えるsl内のメモリを取得. */
    sl = find_set_bit_idx_first(sl_map);

    return remove_block(tman, fl, sl);
}


/*
 * 引数で与えられたブロックの持つメモリからsize分の新しいブロックを取り出して返す.
 */
static inline Block* divide_block(Block* b, size_t size) {
    assert(b != NULL);
    assert(is_sentinel(b) == false);
    assert(size != 0);

    size_t nblock_all_size = size + BLOCK_MEMORY_OFFSET;
    if (get_size(b) <= nblock_all_size) {
        return NULL;
    }

    Block* old_next = get_phys_next_block(b);

    mprintf("divide_block\n");
    print_block(b, 2);

    set_size(b, get_size(b) - nblock_all_size);
    Block* new_next = get_phys_next_block(b);

    old_next->prev_block = new_next;
    new_next->prev_block = b;

    elist_init(&new_next->list);
    set_size(new_next, size);
    set_free(new_next);
    set_prev_free(new_next);

    mprintf("  new next\n");
    print_block(new_next, 4);
    mprintf("  old next\n");
    print_block(old_next, 4);

    return new_next;
}


/*
 * b1とb2を統合する.
 * b2がb1に吸収される形.
 * 物理メモリ上ではb1のアドレスのほうがb2より低い.
 *  -> b1 < b2
 */
static inline void merge_phys_block(Tlsf_manager* tman, Block* b1, Block* b2) {
    mprintf("merge\n");
    mprintf("  b1\n");
    print_block(b1, 2);
    mprintf("  b2\n");
    print_block(b2, 2);

    assert(b1 < b2);
    assert(((uintptr_t)b1 + BLOCK_MEMORY_OFFSET + (uintptr_t)get_size(b1)) == (uintptr_t)b2);

    elist_remove(&b1->list);
    sync_bitmap_by_block(tman, b1);

    elist_remove(&b2->list);
    sync_bitmap_by_block(tman, b2);

    Block* old_next = get_phys_next_block(b1);
    old_next->prev_block = b1;

    set_size(b1, get_size(b1) + BLOCK_MEMORY_OFFSET + get_size(b2));
    set_prev_free(get_phys_next_block(b1));

    insert_block(tman, b1);
}


static inline Block* merge_phys_next_block(Tlsf_manager* tman, Block* b) {
    Block* next = get_phys_next_block(b);
    if ((is_sentinel(next) == true) || (next->is_free == 0)) {
        return b;
    }

    merge_phys_block(tman, b, next);

    return b;
}


static inline Block* merge_phys_prev_block(Tlsf_manager* tman, Block* b) {
    Block* prev = get_phys_prev_block(b);
    if (prev == NULL || prev->is_free == 0) {
        return b;
    }

    merge_phys_block(tman, prev, b);

    return prev;
}


static inline Block* merge_phys_neighbor_blocks(Tlsf_manager* tman, Block* b) {
    return merge_phys_prev_block(tman, merge_phys_next_block(tman, b));
}


Tlsf_manager* tlsf_init(Tlsf_manager* tman) {
    memset(tman, 0, sizeof(Tlsf_manager));
    elist_init(&tman->frames);
    for (size_t i = 0; i < (FL_MAX_INDEX * SL_MAX_INDEX); i++) {
        elist_init(tman->blocks + i);
    }

    return tman;
}


void tlsf_destruct(Tlsf_manager* tman) {
    if (elist_is_empty(&tman->frames) == true) {
        return;
    }

    elist_foreach(itr, &tman->frames, Frame, list) {
        free(itr->addr);
    }

    Elist* l = tman->frames.next;
    do {
        Elist* next = l->next;
        free(l);
        l = next;
    } while (&tman->frames != l);

    memset(tman, 0, sizeof(Tlsf_manager));
}


void tlsf_supply_memory(Tlsf_manager* tman, size_t size) {
    assert((2 * BLOCK_MEMORY_OFFSET ) <= size);
    if (size < (2 * BLOCK_MEMORY_OFFSET)) {
        return;
    }

    mprintf("supply_memory - 0x%zx\n", size);

    /* FIXME: */
    Frame* f = malloc(sizeof(Frame));
    f->addr  = malloc(size);
    if (f->addr == NULL) {
        printf("ERROR!\n");
        return;
    }
    f->size  = size;
    elist_insert_next(&tman->frames, &f->list);
    mprintf("      struct addr : %p\n", f);
    mprintf("             addr : %p ~ 0x%zx\n", f->addr, (uintptr_t)f->addr + (uintptr_t)f->size);

    size_t ns        = (f->size - BLOCK_MEMORY_OFFSET);
    mprintf("             size : 0x%zx (%zu)\n", f->size, f->size);
    mprintf("  align down size : 0x%zx (%zu)\n", ns, ns);
    Block* new_block = generate_block(f->addr, ns);
    set_free(new_block);
    elist_init(&new_block->list);

    Block* sentinel = (Block*)((uintptr_t)f->addr + (uintptr_t)ns);
    sentinel->prev_block = new_block;

    assert(get_phys_next_block(new_block) == sentinel);
    assert(is_sentinel(sentinel) == true);

    /* センチネルは物理メモリ上のものなので論理的なリストへは追加しない. */
    insert_block(tman, new_block);

    ns = get_size(new_block);
    tman->free_memory_size  += ns;
    tman->total_memory_size += ns;
}


void* tlsf_malloc(Tlsf_manager* tman, size_t size) {
    mprintf("malloc\n");
    if (size == 0 || tman == NULL) {
        return NULL;
    }

    /* 一つ上のサイズにする. */
    size_t a_size = adjust_size(size);

    mprintf("  size   : 0x%zx (%zu)\n", size, size);
    mprintf("  a_size : 0x%zx (%zu)\n", a_size, a_size);

    Block* good_block  = remove_good_block(tman, a_size);
    if (good_block == NULL) {
        return NULL;
    }

    Block* alloc_block = divide_block(good_block, a_size);
    Block* select;
    if (alloc_block == NULL) {
        /* 分割出来なかったのでそのまま使用 */
        select = good_block;
    } else {
        select = alloc_block;
        insert_block(tman, good_block);
        tman->free_memory_size -= BLOCK_MEMORY_OFFSET;
    }

    tman->free_memory_size -= get_size(select);

    mprintf("Free memory size  : 0x%zx\n", tman->free_memory_size);

    claer_free(select);
    return convert_mem_ptr(select);
}


void tlsf_free(Tlsf_manager* tman, void* p) {
    mprintf("free\n");
    mprintf("  ptr   : %p\n", p);

    if (tman == NULL || p == NULL) {
        return;
    }

    Block* b = convert_block(p);
    print_block(b, 2);
    assert(b->is_free == 0);

    set_free(b);

    tman->free_memory_size += (get_size(b) + BLOCK_MEMORY_OFFSET);

    b = merge_phys_neighbor_blocks(tman, b);

    mprintf("Free memory size  : 0x%zx\n", tman->free_memory_size);
}


static inline void print_separator(void) {
    if (is_debug == true) {
        puts("============================================================");
    }
}


static inline void echon(char c, size_t times) {
    if (is_debug == true) {
        for (int i = 0; i < times; i++) {
            putchar(c);
        }
    }
}


static inline void print_block(Block* b, size_t tab) {
    if (is_debug == false) {
        return;
    }

    echon(' ', tab);
    printf("Block size      : 0x%08zx (%zd)\n", get_size(b), get_size(b));
    echon(' ', tab);
    printf("      prev ptr  : %p\n", b->prev_block);
    echon(' ', tab);
    printf("      this ptr  : %p *\n", b);
    echon(' ', tab);
    printf("      next ptr  : %p\n", get_phys_next_block(b));
    echon(' ', tab);
    printf("      free      : %d\n", b->is_free);
    echon(' ', tab);
    printf("      prev free : %d\n", b->is_free_prev);
}


static inline void print_tlsf(Tlsf_manager* tman) {
    if (is_debug == false) {
        return;
    }

    print_separator();
    printf("print_tlsf\n");

    for (size_t i = 0; i < FL_MAX_INDEX; i++) {
        bool f = is_fl_list_available(tman, i);
        size_t fs = (i == 0) ? 0 : P2(i + FL_BASE_INDEX);
        printf("First Lv: %02zu - %s", i, (f ? ("Enable ") : ("Disable")));
        printf(" - (0x%08zx <= size < 0x%08zx)\n", fs, fs << 1);

        if (f == false) {
            continue;
        }

        for (size_t j = 0; j < SL_MAX_INDEX; j++) {
            if (is_sl_list_available(tman, i, j) == false) {
                continue;
            }
            printf("  Second Lv: %02zu", j);
            size_t ss = fs + (i == 0 ? (j * SL_BLOCK_MIN_SIZE) : (j * (fs / SL_MAX_INDEX)));
            printf(" - (0x%08zx <= size < 0x%08zx)\n", ss, ss + (i == 0 ? SL_BLOCK_MIN_SIZE : (fs / SL_MAX_INDEX)));

            Elist* l = get_block_list_head(tman, i, j);
            elist_foreach(itr, l, Block, list) {
                print_block(itr, 4);
            }
        }
    }

    print_separator();
    printf("\n");
}


static inline void print_tag_list(Frame* f) {
    if (is_debug == false) {
        return;
    }

    printf("tag\n");
    Block* b = f->addr;

    size_t cnt = 0;
    while (is_sentinel(b) == false) {
        printf("  %zd\n", cnt++);
        print_block(b, 2);
        b = get_phys_next_block(b);
    }
}


static char const* test_indexes(void) {
    size_t fl, sl;

    size_t sizes[]  = {140, 32, 11, 1024, 16 << 20, (4 << 20) * 1024 - 1u, };
    /* size_t sizes[]  = {140, 32, 11, 1024, 16 << 20, 4u * 1024u * 1024u, }; */
    size_t ans_fl[] = {0, 0, 0, 1, 15, 22};
    size_t ans_sl[] = {2, 0, 0, 0, 0, 15};
    printf("%zu\n", sizeof(size_t));

    for (int i = 0; i < 6; i++) {
        set_idxs(sizes[i], &fl, &sl);
        printf("size = 0x%08zx, fl = %02zu, sl = %02zu\n", sizes[i], fl, sl);
        MIN_UNIT_ASSERT("set_idxs is wrong.", fl == ans_fl[i] && sl == ans_sl[i]);
    }

    return NULL;
}


static char const* test_find_bit(void) {
    for (size_t i = 0; i < 32; i++) {
        size_t s = 1u << i;
        MIN_UNIT_ASSERT("find_set_bit_idx_first is wrong.", find_set_bit_idx_first(s) == i);
        MIN_UNIT_ASSERT("find_set_bit_idx_last is wrong.", find_set_bit_idx_last(s) == i);
    }
    MIN_UNIT_ASSERT("find_set_bit_idx_first is wrong.", find_set_bit_idx_first(0x80008000) == 15);
    MIN_UNIT_ASSERT("find_set_bit_idx_last is wrong.", find_set_bit_idx_last(0x7FFFFFFF) == 30);

    return NULL;
}


static char const* all_tests(void) {
    MIN_UNIT_RUN(test_indexes);
    MIN_UNIT_RUN(test_find_bit);
    return NULL;
}


static inline int do_all_tests(void) {
    MIN_UNIT_RUN_ALL(all_tests);
}


static double gettimeofday_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}


int main(void) {
    do_all_tests();

    putchar('\n');
    print_separator();

    Tlsf_manager tman;
    Tlsf_manager* p = &tman;

    tlsf_init(p);

    size_t cnt                     = 0;
    size_t limit                   = 1000000;
    size_t alloc_size              = (1 * 1024 * 1024 * 1024) + (BLOCK_MEMORY_OFFSET * 3);
    static size_t const array_size = 30;
    void* allocs[array_size];
    srand((unsigned int)time(NULL));

    tlsf_supply_memory(p, alloc_size);

    is_debug = true;
    printf("Total memory size : 0x%zx\n", p->total_memory_size);
    printf("Free memory size  : 0x%zx\n", p->free_memory_size);
    /* print_tlsf(p); */
    /* print_tag_list((Frame*)p->frames.next); */

    printf("\nStart Loop\n");
    is_debug = false;

    double begin = gettimeofday_sec();
    for (size_t times = 0; times < limit; times++) {
        size_t r_size = (((size_t)rand() / alloc_size) + 1u);
        allocs[cnt] = tlsf_malloc(p, r_size);

        ++cnt;
        if (array_size <= cnt || allocs[cnt - 1u] == NULL || times == limit - 1) {
            for (int i = 0; i <= (cnt - 1u); i++) {
                tlsf_free(p, allocs[i]);
            }
            cnt = 0;
        }
    }
    double end = gettimeofday_sec();

    is_debug = true;
    printf("Finish Loop\n\n");
    printf("Time is %f\n", end - begin);

    printf("Total memory size : 0x%zx\n", p->total_memory_size);
    printf("Free memory size  : 0x%zx\n", p->free_memory_size);
    /* print_tlsf(p); */
    /* print_tag_list((Frame*)p->frames.next); */

    tlsf_destruct(p);

    return 0;
}
