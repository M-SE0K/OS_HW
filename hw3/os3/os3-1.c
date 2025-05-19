// PAGESIZE, VAS_PAGES, PAS_FRAMES
// #define PAS_SIZE (PAGESIZE*PAS_FRAMES)
// #define VAS_SIZE(PAGESIZE*VAS_PAGES)

// #define PARGETABLE_FRAMES (VAS_PAGES*PTE_SIZE/PAGESIZE)

#define PAGE_INVALID (0)
#define PAGE_VALID (1)
#define PTE_SIZE 4

#define MAX_REFERENCES (256)
#define MAX_PROCESS (10)
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "list.h"

//#include <algorithm>

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

    //pte* page_tables;
    
    struct list_head list;
} process_raw;


typedef struct{
    frame* f;//allocated frame
    bool vflag;//valid-invalid bit;
    unsigned char ref;  //reference bit
    unsigned char pad;  //padding
} pte;

/*-------------------------------전역변수----------------------------------------------------*/


SIZE sys_size;
LIST_HEAD(job_queue);

int process_count = 0;
int ref_indices[MAX_PROCESS] = {};  // 현재 프로세스에서 참조하고 있는 페이지 번호 기록
int faults[MAX_PROCESS] = {};


int next_free_frame = 0;            //현재 frame의 개수
bool* frame_in_use;

/*-------------------------------함수----------------------------------------------------*/

void input();
int compare_by_pid(const void* a, const void* b);
int allocated_frame()
//프레임 할당하는 로직
{
    while(next_free_frame < sys_size.PAS_FRAMES && frame_in_use[next_free_frame])             {   next_free_frame++;  }
    if (next_free_frame >= sys_size.PAS_FRAMES)                                               {   return -1;  }
    //"0부터 순차적으로 프레임을 할당할 것" : 최대 프레임 개수를 벗어나지 않으며 && 현재 프레임이 사용중인 경우 -> 범위 내 비어있는 프레임 탐색

    frame_in_use[next_free_frame] = true;
    return next_free_frame;
}
struct list_head* list_quick_sort(struct list_head* head, int (*cmp)(process_raw*, process_raw*));

int compare_pid(process_raw* a, process_raw* b) {
    return a->pid - b->pid;
}

static inline void list_splice_tail(struct list_head* list, struct list_head* head) {
    if (!list_empty(list)) {
        struct list_head* first = list->next;
        struct list_head* last = list->prev;
        struct list_head* at = head;

        first->prev = at->prev;
        at->prev->next = first;

        last->next = at;
        at->prev = last;

        INIT_LIST_HEAD(list); // clear original
    }
}

struct list_head* list_quick_sort(struct list_head* head, int (*cmp)(process_raw*, process_raw*)) {
    if (list_empty(head) || head->next->next == head) {
        return head; // 0 or 1 element
    }

    LIST_HEAD(less);
    LIST_HEAD(equal);
    LIST_HEAD(greater);

    struct list_head *pos, *n;
    process_raw *pivot = list_entry(head->next, process_raw, list);
    list_for_each_safe(pos, n, head) {
        process_raw* current = list_entry(pos, process_raw, list);
        list_del_init(pos);

        int result = cmp(current, pivot);
        if (result < 0) list_add_tail(&current->list, &less);
        else if (result == 0) list_add_tail(&current->list, &equal);
        else list_add_tail(&current->list, &greater);
    }

    struct list_head* sorted_less = list_quick_sort(&less, cmp);
    struct list_head* sorted_greater = list_quick_sort(&greater, cmp);

    // concatenate: sorted_less + equal + sorted_greater → into head
    INIT_LIST_HEAD(head);
    if (!list_empty(sorted_less)) list_splice_tail(sorted_less, head);
    if (!list_empty(&equal)) list_splice_tail(&equal, head);
    if (!list_empty(sorted_greater)) list_splice_tail(sorted_greater, head);

    return head;
}



int main()
{
    input();
    //"모든 프로세스는 이진 파일로부터 로드하고"

    process_raw* process;
    //"Page reference sequence를 가진 여러 프로세스(최대 10개)"

    pte* page_tables[sys_size.PARGETABLE_FRAMES];
    //"모든 프로세스에 1-Level 페이지 테이블을 할당하고 초기화"
    list_quick_sort(&job_queue, compare_pid);
    list_for_each_entry(process, &job_queue, list){
        printf("PID: %d, ref_len: %d\n", process->pid, process->ref_len);
        // for (int i = 0; i < process->ref_len; i++)
        //     printf("ref: %d\n", process->references[i]);
    }
    //"각 프로세스가 PID 순서대로 돌아가며"

    for (int i = 0; i < process_count; i++) {   page_tables[i] = calloc(sys_size.VAS_PAGES, sizeof(pte));  }
    //각 프로세스에서 페이지를 관리하는 페이지 테이블을 0으로 초기화

    bool done = false;
    while(!done)
    //"각 프로세스가 PID 순서대로 돌아가며 한 번에 하나씩 페이지를 접근"
    {
        done = true;
        //int fault = 0;

        for (int i = 0; i < process_count; i++)
        //"각 프로세스가 한 번에 하나씩 페이지를 접근"
        {
            if (process[i].ref_len <= ref_indices[i])   {   continue;   }
            //현재 프로세스에 대한 페이지 참조가 종료된 경우

            done = false;
            unsigned char page = process[i].references[ref_indices[i]++];
            pte* pt = page_tables[i];
            //현재 참조해야될 페이지 시퀀스를 저장함

            if (pt[page].vflag == PAGE_VALID)   { pt[page].ref++;  }
            //해당 페이지에 이미 프레임이 할당되어있는 경우
            else
            //해당 페이지에 이미 프레임이 할당되어 있지 않은 경우 -> 프레임 할당!
            {
                int f = allocated_frame();
                if (f == -1)
                //OOM 출력 및 종료 시점 확인
                {
                    printf( "Out of memory!!\n");
                    continue;
                }

                // pt[page].f->b = NULL;
                pt[page].vflag = PAGE_VALID;
                pt[page].ref = 1;
                faults[i]++;
                //현재 프로세스의 각 페이지에 대한 기록
            }

        }
    }
}




/*-------------------------------함수----------------------------------------------------*/

void input()
{
    fread(&sys_size.PAGESIZE, sizeof(int), 1, stdin);
    fread(&sys_size.PAS_FRAMES, sizeof(int), 1, stdin);
    fread(&sys_size.VAS_PAGES, sizeof(int), 1, stdin);

    printf("[DEBUG INPUT FUNCTION1] PAGESIZE: %d PAS_FRAMES: %d VAS_PAGES: %d\n", sys_size.PAGESIZE, sys_size.PAS_FRAMES, sys_size.VAS_PAGES);

    sys_size.PARGETABLE_FRAMES = (sys_size.VAS_PAGES * PTE_SIZE) / sys_size.PAGESIZE;
    frame_in_use = calloc(sys_size.PAS_FRAMES, sizeof(bool));

    while(1)
    {
        int pid, len;
        if (fread(&pid, sizeof(int), 1, stdin) != 1)                {    break;  }
        if (fread(&len, sizeof(int), 1, stdin) != 1)                {    break;  }

        unsigned char* refs = malloc(len);
        if (fread(refs, sizeof(unsigned char), len, stdin) != len) {   free(refs); break;  }

        process_raw* proc = malloc(sizeof(process_raw));
        proc->pid = pid;
        proc->ref_len = len;
        proc->references = refs;
        

        INIT_LIST_HEAD(&proc->list);
        list_add_tail(&proc->list, &job_queue);
        
        
        //[DEBUG]
        printf("[DEBUG INPUT FUNCTION2] PID: %d ref_len: %d\n", proc->pid, proc->ref_len);
        //for (int i = 0; i < len; i++)   printf("[DEBUG INPUT FUNCTION3] ref: %d\n", proc->references[i]);
        
    
    }
    sys_size.PARGETABLE_FRAMES = (sys_size.VAS_PAGES * PTE_SIZE)/sys_size.PAGESIZE;
}
int compare_by_pid(const void* a, const void* b)
{
    process_raw* pa = (process_raw*)a;
    process_raw* pb = (process_raw*)b;

    return pa->pid - pb->pid;
}