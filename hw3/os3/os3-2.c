#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "list.h"

#define PAGE_INVALID 0
#define PAGE_VALID 1
#define PTE_SIZE 4
#define MAX_REFERENCES (256)
#define MAX_PROCESS (10)

/*-------------------------------구조체----------------------------------------------------*/
//시스템 파라미터 정보 저장 구조체
typedef struct{
    int PAGESIZE;               //페이지 크기
    int VAS_PAGES;              //가상 주소 공간 내 페이지 개수
    int PAS_FRAMES;             //물리 메모리의 프레임 개수
    int PARGETABLE_FRAMES;      //각 프로세스 페이지 테이블에 필요한 프레임 수
    int FRAMESIZE               //각 프레임의 크기
} SIZE;



//페이지 테이블 엔트리 구조체
typedef struct{
    int page_number;           //현재 페이지의 프레임의 위치
    int frame_number;            //L2 page table이 저장될 
    bool vflag;                 //valid or invalid flag
    unsigned char ref;          //참조 횟수 카운트
    unsigned char pad;          //패딩
    unsigned char page_access;
} pte;


typedef struct{
    int page_number;           //현재 페이지의 프레임의 위치
    int frame_number;            //L2 page table이 저장될 
    bool vflag;                 //valid or invalid flag
    unsigned char ref;          //참조 횟수 카운트
    unsigned char pad;          //패딩

    pte* l2_pt;
} l1_entry;


//프로세스 정보 구조체
typedef struct{
    int pid;                    //프로세스 번호
    int ref_len;                //페이지 참조 시퀀스 길이
    unsigned char* references;  //참조 시퀀스 (페이지 번호 배열)
    struct list_head list;      //연결 리스트용 노드

    int ref_cnt;
    int fault_cnt;
} process_raw;


/*-------------------------------전역변수----------------------------------------------------*/

SIZE sys_size;
LIST_HEAD(job_queue);               //프로세스 리스트(링크드리스트 헤드)

int process_count = 0;              //총 프로세스 개수
int ref_indices_L1[MAX_PROCESS] = {};  //각 프로세스별 현재 참조 인덱스
int faults[MAX_PROCESS] = {};       //각 프로세스 별 page fault 카운트
int next_free_frame = 0;            //다음에 할당할 프레임 번호
bool* frame_in_use;                 //프레임 사용 여부 배열


int cnt = 0;

/*-------------------------------함수 선언--------------------------------------------------*/

void input();                               //입력 및 초기화 함수
int allocated_frame();                      //프레임 할당 함수
void print_page_tables(l1_entry** page_tables_l1); //페이지 테이블 및 결과 출력 함수
bool simulator_L1(process_raw* proc, int i, l1_entry** page_tables_l1)
{
    bool fault_flag = false;

    unsigned char page = proc->references[ref_indices_L1[i]++];

    int l1_idx = page / sys_size.FRAMESIZE;
    int l2_idx = page % sys_size.FRAMESIZE;

    l1_entry* l1_pt = page_tables_l1[proc->pid];                                
    //현재 프로세스의 참조 페이지 및 pte의 주소 저장

    if (!l1_pt[l1_idx].vflag)
    //이미 페이지가 할당 되어 있으면 참조 횟수만 증가
    {
        int f = allocated_frame();
        if (f == -1)
        {
            return false;
        }
        //페이지 폴트 발생
        l1_pt[l1_idx].l2_pt = calloc(sys_size.FRAMESIZE, sizeof(pte));
        l1_pt[l1_idx].page_number = page / sys_size.FRAMESIZE;
        l1_pt[l1_idx].frame_number = f;
        l1_pt[l1_idx].vflag = true;
        l1_pt[l1_idx].ref = 1; 
        proc->fault_cnt++;


        //printf("[PID: %02d REF: %03d] Page access %03d: (L1PT) Frame %03d,", proc->pid, proc->ref_cnt, page, l1_pt[l1_idx].frame_number);
    }
    else
    {
        //printf("[PID: %02d REF: %03d] Page access %03d: (L1PT) PF, Allocated Frame %03d -> %03d, ", proc->pid, proc->ref_cnt, page, l1_pt[l1_idx].page_number, l1_pt[l1_idx].frame_number);
        l1_pt[l1_idx].ref++;
    }

    pte* l2_pt = l1_pt[l1_idx].l2_pt;
    
    if (l2_pt[l2_idx].vflag)
    {
        //printf("(L2PT) Frame %03d", l2_pt[l2_idx].frame_number);
        l2_pt[l2_idx].ref++;
    }
    else
    {
        int f = allocated_frame();
        if (f == -1)
        {
            return false;
        }
        l2_pt[l2_idx].frame_number = f;
        l2_pt[l2_idx].vflag = true;
        l2_pt[l2_idx].ref = 1;
        l2_pt[l2_idx].page_access = page;

        proc->fault_cnt++;
        fault_flag = true;
        //printf(" (L2PT) PF,Allocated Frame %03d",  l2_pt[l2_idx].frame_number);
    }
    // printf(" falut_cnt = %d", proc->fault_cnt);
    // printf(" page sys_size.framesize = %d\n", page % sys_size.FRAMESIZE);
    //printf("\n");

    return true;
}
/*-------------------------------메인 함수--------------------------------------------------*/

int main() {
    input();

    //각 프로세스 별로 페이지 테이블 엔트리 할당(VAS_PAGES만큼)
    l1_entry** page_tables_l1[MAX_PROCESS] = {0,};

    struct list_head* pos;
    process_raw* proc;
    int i = 0;

    //프로세스 로드 시, L1 PT를 위해 프레임 하나를 할당하고 초기화
    list_for_each(pos, &job_queue) {
        page_tables_l1[i++] = calloc(sys_size.PAGESIZE / 4, sizeof(l1_entry));
        frame_in_use[next_free_frame++] = true;

    }

    // Demand Paging 시뮬레이터: 모든 포레스가 번갈아 한 번씩 접근
    bool done = false;
    while (!done) {
        done = true;
        i = 0;
        list_for_each(pos, &job_queue) 
        {
            proc = list_entry(pos, process_raw, list);
            if (proc->ref_len <= ref_indices_L1[i])    {   i++;    continue;   }
            //이 프로세스의 참조 시퀀스가 모두 끝났다면 패스

            done = false;
            if (!simulator_L1(proc, i, page_tables_l1))
            {
                printf("Out of memory!!\n");
                ref_indices_L1[i]--;
                goto jump;
            }
            //simulator_L2(proc, i, page_tables_l1, page_tables_l2);
            proc->ref_cnt++;
            i++;
        }

        // cnt++;
        // if (cnt == 20)   break;
    }
    jump:
    print_page_tables(page_tables_l1);
    return 0;
}
/*------------------------------- 함수 정의 --------------------------------------------------*/

/*------------------------------- 입력 및 초기화 함수 ------------------------------------------*/
void input()
{
    //시스템 파라미터(페이지 크기, 프레임 수, VAS 페이지 수) 입력
    fread(&sys_size.PAGESIZE, sizeof(int), 1, stdin);
    fread(&sys_size.PAS_FRAMES, sizeof(int), 1, stdin);
    fread(&sys_size.VAS_PAGES, sizeof(int), 1, stdin);

    sys_size.PARGETABLE_FRAMES = (sys_size.VAS_PAGES * PTE_SIZE)/sys_size.PAGESIZE;
    sys_size.FRAMESIZE = (sys_size.PAGESIZE / 4);

    frame_in_use = calloc(sys_size.PAS_FRAMES, sizeof(bool));

    //프로세스 정보 입력(pid, 참조길이, 참조 시퀀스)
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
        proc->ref_cnt = 0;

        INIT_LIST_HEAD(&proc->list);
        list_add_tail(&proc->list, &job_queue);

        process_count++;
    }

    //페이지 테이블 프레임 예약
    //next_free_frame = (sys_size.VAS_PAGES * 4 / sys_size.PAGESIZE) * process_count;
}

/*-------------------------------페이지 테이블 및 결과 출력 함수------------------------------*/
void print_page_tables(l1_entry** page_tables_l1)
{
    struct list_head* pos;
    process_raw* proc;
    int i = 0;

    int total_frame_count = 0;
    int total_page_faults = 0;
    int total_ref = 0;

    list_for_each(pos, &job_queue) {
        proc = list_entry(pos, process_raw, list);

        int allocated_frames = 1;
        l1_entry* l1_pt = page_tables_l1[proc->pid];
        for (int j = 0; j < sys_size.PAGESIZE / 4; j++)
        {
            if (l1_pt[j].vflag && l1_pt[j].ref > 0)
            {
                allocated_frames++;
                pte* l2_pt = l1_pt[j].l2_pt;
                for (int k = 0; k < sys_size.FRAMESIZE; k++)
                {
                    if (l2_pt[k].vflag && l2_pt[k].ref > 0)
                        allocated_frames++;
                }
            }
        }

        printf("** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n", proc->pid, allocated_frames, proc->fault_cnt, ref_indices_L1[i]);
        total_frame_count += allocated_frames;



        for (int j = 0; j < sys_size.PAGESIZE / 4; j++)
        {
            if (l1_pt[j].vflag && l1_pt[j].ref > 0)
            {
                printf("(L1PT) %03d -> %03d\n", l1_pt[j].page_number, l1_pt[j].frame_number);
                pte* l2_pt = l1_pt[j].l2_pt;
                for (int k = 0; k < sys_size.FRAMESIZE; k++)
                {
                    if (l2_pt[k].vflag && l2_pt[k].ref > 0)
                        printf("(L2PT) %03d -> %03d REF=%03d\n", l2_pt[k].page_access, l2_pt[k].frame_number, l2_pt[k].ref);
                }
            }
        }


        total_page_faults += proc->fault_cnt;
        total_ref += ref_indices_L1[i];
        i++;
    }
    printf("Total: Allocated Frames=%03d Page Faults/References=%03d/%03d\n", total_frame_count, total_page_faults, total_ref);
}

/*-------------------------------프레임 할당 함수------------------------------*/
int allocated_frame() {
    while(next_free_frame < sys_size.PAS_FRAMES && frame_in_use[next_free_frame])
    {
        next_free_frame++;
    }
    if (next_free_frame >= sys_size.PAS_FRAMES) return -1;
    frame_in_use[next_free_frame] = true;
    return next_free_frame;
}