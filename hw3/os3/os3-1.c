#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "list.h"

#define PAGE_INVALID (0)
#define PAGE_VALID (1)
#define PTE_SIZE 4
#define MAX_REFERENCES (256)
#define MAX_PROCESS (10)

/*-------------------------------구조체----------------------------------------------------*/
typedef struct{
    int PAGESIZE;
    int VAS_PAGES;
    int PAS_FRAMES;
    int PARGETABLE_FRAMES;
} SIZE;

typedef struct{
    unsigned char* data;
} frame;

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

/*-------------------------------전역변수----------------------------------------------------*/
SIZE sys_size;
LIST_HEAD(job_queue);

int process_count = 0;
int ref_indices[MAX_PROCESS] = {};
int faults[MAX_PROCESS] = {};
int next_free_frame = 0;
bool* frame_in_use;

/*-------------------------------함수 선언--------------------------------------------------*/
void input();
int allocated_frame();
void print_page_tables(pte* page_tables[]);
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

    pte* page_tables[MAX_PROCESS];
    struct list_head* pos;
    process_raw* proc;
    int i = 0;

    list_for_each(pos, &job_queue) {
        page_tables[i++] = calloc(sys_size.VAS_PAGES, sizeof(pte));
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
            pte* pt = page_tables[i];

            unsigned char page = proc->references[ref_indices[i]++];
            bool flag = false;

            if (pt[page].vflag == PAGE_VALID) {
                pt[page].ref++;
                flag = true;
            } else {
                int f = allocated_frame();
                if (f == -1) {
                    printf("Out of memory!!\n");
                    ref_indices[i]--;
                    goto jump;
                    i++;
                    continue;
                }
                pt[page].frame_no = f;
                pt[page].vflag = PAGE_VALID;
                pt[page].ref = 1;
                faults[i]++;
            }
            i++;
        }
    }
    jump:
    print_page_tables(page_tables);
    return 0;
}
/*-------------------------------함수 정의--------------------------------------------------*/
void input() {
    fread(&sys_size.PAGESIZE, sizeof(int), 1, stdin);
    fread(&sys_size.PAS_FRAMES, sizeof(int), 1, stdin);
    fread(&sys_size.VAS_PAGES, sizeof(int), 1, stdin);

    sys_size.PARGETABLE_FRAMES = (sys_size.VAS_PAGES * PTE_SIZE)/sys_size.PAGESIZE;
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
    next_free_frame = (sys_size.VAS_PAGES * 4 / sys_size.PAGESIZE) * process_count;
}
void print_page_tables(pte* page_tables[]) {
    struct list_head* pos;
    process_raw* proc;
    int i = 0;

    int total_frame_count = 0;
    int total_page_faults = 0;
    int total_ref = 0;

    list_for_each(pos, &job_queue) {
        proc = list_entry(pos, process_raw, list);

        int frame_count = 0;
        for (int p = 0; p < sys_size.VAS_PAGES; p++) {
            if (page_tables[i][p].vflag) frame_count++;
        }

        printf("** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n",
               proc->pid, frame_count + ((sys_size.VAS_PAGES * 4 / sys_size.PAGESIZE)), faults[i], ref_indices[i]);

        for (int p = 0; p < sys_size.VAS_PAGES; p++) {
            if (page_tables[i][p].vflag) {
                printf("%03d -> %03d REF=%03d\n", p, page_tables[i][p].frame_no, page_tables[i][p].ref);
            }
        }
        total_frame_count += frame_count + (sys_size.VAS_PAGES * 4 / sys_size.PAGESIZE);
        total_page_faults += faults[i];
        total_ref += ref_indices[i];

        i++;
    }
    printf("Total: Allocated Frames=%03d Page Faults/References=%03d/%03d\n", total_frame_count, total_page_faults, total_ref);
}
int compare_by_pid(const void* a, const void* b) {
    process_raw* pa = (process_raw*)a;
    process_raw* pb = (process_raw*)b;
    return pa->pid - pb->pid;
}