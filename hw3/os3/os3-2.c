#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "list.h"

#define PAGE_INVALID (0)
#define PAGE_VALID (1)
#define PTE_SIZE 4
#define MAX_REFERENCES (256)
#define MAX_PROCESS (10)

/* ---- 2-level 계층 파라미터 ---- */
#define L1_SIZE 8           // L1 PT 엔트리 개수
#define L2_SIZE 8           // L2 PT 엔트리 개수
#define VAS_PAGES (L1_SIZE * L2_SIZE) // 8x8=64

typedef struct{
    int PAGESIZE;
    int PAS_FRAMES;
} SIZE;

typedef struct{
    int pid;
    int ref_len;
    unsigned char* references;
    struct list_head list;
} process_raw;

typedef struct{
    int frame_no;
    bool vflag;
    unsigned char ref;
    unsigned char pad;
} pte;

// L2 페이지 테이블 (8개 엔트리)
typedef struct {
    pte entries[L2_SIZE];
    int frame_no; // L2 PT가 할당된 프레임 번호
    bool vflag;
} l2_pt;

// L1 페이지 테이블 (8개 엔트리, 각 엔트리는 l2_pt*를 가리킴)
typedef struct {
    l2_pt* entries[L1_SIZE];
    int frame_no; // L1 PT가 할당된 프레임 번호
} l1_pt;

/* ---- 전역변수 ---- */
SIZE sys_size;
LIST_HEAD(job_queue);

int process_count = 0;
int ref_indices[MAX_PROCESS] = {};
int faults[MAX_PROCESS] = {};
int next_free_frame = 0;
bool* frame_in_use;

l1_pt* page_tables[MAX_PROCESS];

/* ---- 함수 선언 ---- */
void input();
int allocated_frame();
void print_page_tables(l1_pt* page_tables[]);

int allocated_frame() {
    while(next_free_frame < sys_size.PAS_FRAMES && frame_in_use[next_free_frame]) {
        next_free_frame++;
    }
    if (next_free_frame >= sys_size.PAS_FRAMES) return -1;
    frame_in_use[next_free_frame] = true;
    return next_free_frame;
}

int main() {
    input();

    struct list_head* pos;
    process_raw* proc;
    int i = 0;

    // 프로세스별 L1 PT 할당
    list_for_each(pos, &job_queue) {
        l1_pt* l1 = calloc(1, sizeof(l1_pt));
        int l1f = allocated_frame();
        if (l1f == -1) {
            printf("Out of memory!!\n");
            exit(1);
        }
        l1->frame_no = l1f;
        for (int j = 0; j < L1_SIZE; j++) l1->entries[j] = NULL;
        page_tables[i++] = l1;
    }

    bool done = false;
    while (!done) {
        done = true;
        i = 0;
        list_for_each(pos, &job_queue) {
            proc = list_entry(pos, process_raw, list);
            if (proc->ref_len <= ref_indices[i]) {
                i++;
                continue;
            }
            done = false;
            l1_pt* l1 = page_tables[i];
            unsigned char page = proc->references[ref_indices[i]++]; // 0~63
            int l1_idx = page / L2_SIZE; // 0~7
            int l2_idx = page % L2_SIZE; // 0~7

            // (1) L1 PTE 할당/valid 확인
            if (l1->entries[l1_idx] == NULL) {
                l2_pt* l2 = calloc(1, sizeof(l2_pt));
                int l2f = allocated_frame();
                if (l2f == -1) {
                    printf("Out of memory!!\n");
                    ref_indices[i]--;
                    goto jump;
                }
                l2->frame_no = l2f;
                l2->vflag = true;
                l1->entries[l1_idx] = l2;
            }
            l2_pt* l2 = l1->entries[l1_idx];

            // (2) L2 PTE 접근/할당/valid
            if (l2->entries[l2_idx].vflag == PAGE_VALID) {
                l2->entries[l2_idx].ref++;
            } else {
                int pf = allocated_frame();
                if (pf == -1) {
                    printf("Out of memory!!\n");
                    ref_indices[i]--;
                    goto jump;
                }
                l2->entries[l2_idx].frame_no = pf;
                l2->entries[l2_idx].vflag = PAGE_VALID;
                l2->entries[l2_idx].ref = 1;
                faults[i]++;
            }
            i++;
        }
    }
jump:
    print_page_tables(page_tables);
    return 0;
}

void input() {
    fread(&sys_size.PAGESIZE, sizeof(int), 1, stdin);
    fread(&sys_size.PAS_FRAMES, sizeof(int), 1, stdin);
    int dummy; // VAS_PAGES는 매크로로 정의, 입력값 읽고 버림
    fread(&dummy, sizeof(int), 1, stdin);

    frame_in_use = calloc(sys_size.PAS_FRAMES, sizeof(bool));

    while(1) {
        int pid, len;
        if (fread(&pid, sizeof(int), 1, stdin) != 1) break;
        if (fread(&len, sizeof(int), 1, stdin) != 1) break;

        unsigned char* refs = malloc(len);
        if (fread(refs, sizeof(unsigned char), len, stdin) != len) {
            free(refs);
            break;
        }

        process_raw* proc = malloc(sizeof(process_raw));
        proc->pid = pid;
        proc->ref_len = len;
        proc->references = refs;
        INIT_LIST_HEAD(&proc->list);
        list_add_tail(&proc->list, &job_queue);
        process_count++;
    }
}

void print_page_tables(l1_pt* page_tables[]) {
    struct list_head* pos;
    process_raw* proc;
    int i = 0;
    int total_frame_count = 0, total_page_faults = 0, total_ref = 0;

    list_for_each(pos, &job_queue) {
        proc = list_entry(pos, process_raw, list);
        l1_pt* l1 = page_tables[i];
        int frame_count = 0;

        // (1) L1 PT 프레임 카운트(=L2PT 개수 + 1)
        int l2_frame_count = 0;
        for (int j = 0; j < L1_SIZE; j++) {
            if (l1->entries[j]) l2_frame_count++;
        }
        frame_count += l2_frame_count + 1; // L1 PT 프레임 포함

        // (2) L2 PT의 실제 데이터 페이지 프레임 수
        int page_frame_count = 0;
        for (int j = 0; j < L1_SIZE; j++) {
            if (l1->entries[j]) {
                l2_pt* l2 = l1->entries[j];
                for (int k = 0; k < L2_SIZE; k++) {
                    if (l2->entries[k].vflag) page_frame_count++;
                }
            }
        }

        printf("** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n",
               proc->pid, page_frame_count, faults[i], ref_indices[i]);
        // (3) L1 PT 출력
        for (int j = 0; j < L1_SIZE; j++) {
            if (l1->entries[j]) {
                printf("(L1PT) %03d -> %03d\n", j*L2_SIZE, l1->entries[j]->frame_no);
            }
        }
        // (4) L2 PT 출력
        for (int j = 0; j < L1_SIZE; j++) {
            if (l1->entries[j]) {
                l2_pt* l2 = l1->entries[j];
                for (int k = 0; k < L2_SIZE; k++) {
                    if (l2->entries[k].vflag) {
                        int page_no = j * L2_SIZE + k;
                        printf("(L2PT) %03d -> %03d REF=%03d\n", page_no, l2->entries[k].frame_no, l2->entries[k].ref);
                    }
                }
            }
        }
        total_frame_count += page_frame_count;
        total_page_faults += faults[i];
        total_ref += ref_indices[i];
        i++;
    }
    printf("Total: Allocated Frames=%03d Page Faults/References=%03d/%03d\n",
           total_frame_count, total_page_faults, total_ref);
}

int compare_by_pid(const void* a, const void* b) {
    process_raw* pa = (process_raw*)a;
    process_raw* pb = (process_raw*)b;
    return pa->pid - pb->pid;
}
