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
//시스템 파라미터 정보 저장 구조체
typedef struct{
    int PAGESIZE;               //페이지 크기
    int VAS_PAGES;              //가상 주소 공간 내 페이지 개수
    int PAS_FRAMES;             //물리 메모리의 프레임 개수
    int PARGETABLE_FRAMES;      //각 프로세스 페이지 테이블에 필요한 프레임 수
} SIZE;

typedef struct{
    unsigned char* data;    
} frame;

//페이지 테이블 엔트리 구조체
typedef struct{
    int frame_no;               //할당된 프레임 번호
    bool vflag;                 //valid or invalid flag
    unsigned char ref;          //참조 횟수 카운트
    unsigned char pad;          //패딩
} pte;

//프로세스 정보 구조체
typedef struct{
    int pid;                    //프로세스 번호
    int ref_len;                //페이지 참조 시퀀스 길이
    unsigned char* references;  //참조 시퀀스 (페이지 번호 배열)
    struct list_head list;      //연결 리스트용 노드
} process_raw;


/*-------------------------------전역변수----------------------------------------------------*/

SIZE sys_size;
LIST_HEAD(job_queue);               //프로세스 리스트(링크드리스트 헤드)

int process_count = 0;              //총 프로세스 개수
int ref_indices[MAX_PROCESS] = {};  //각 프로세스별 현재 참조 인덱스
int faults[MAX_PROCESS] = {};       //각 프로세스 별 page fault 카운트
int next_free_frame = 0;            //다음에 할당할 프레임 번호
bool* frame_in_use;                 //프레임 사용 여부 배열


/*-------------------------------함수 선언--------------------------------------------------*/

void input();                               //입력 및 초기화 함수
int allocated_frame();                      //프레임 할당 함수
void print_page_tables(pte* page_tables[]); //페이지 테이블 및 결과 출력 함수


/*-------------------------------메인 함수--------------------------------------------------*/

int main() {
    input();

    //각 프로세스 별로 페이지 테이블 할당(VAS_PAGES만큼)
    pte* page_tables_l1[process_count];
    struct list_head* pos;
    process_raw* proc;
    int i = 0;

    //프로세스 로드 시, L1 PT를 위해 프레임 하나를 할당하고 초기화
    list_for_each(pos, &job_queue) {
        page_tables_l1[i++] = malloc(sys_size.PAGESIZE / 4, sizeof(pte));
    }


    // Demand Paging 시뮬레이터: 모든 포레스가 번갈아 한 번씩 접근
    bool done = false;
    while (!done) {
        done = true;
        i = 0;
        list_for_each(pos, &job_queue) 
        {
            proc = list_entry(pos, process_raw, list);
            if (proc->ref_len <= ref_indices[i])    {   i++;    continue;   }
            //이 프로세스의 참조 시퀀스가 모두 끝났다면 패스

            unsigned char page = proc->references[ref_indices[i]++];    
            pte* pt = page_tables_l1[i];                                
            //현재 프로세스의 참조 페이지 및 pte의 주소 저장

            done = false;
            if (pt[page].vflag == PAGE_VALID)       {   pt[page].ref++; }
            //이미 페이지가 할당 되어 있으면 참조 횟수만 증가

            else
            //할당되어 있지 않으면 새로 프레임 할당
            {
                int f = allocated_frame();
                if (f == -1)
                {
                    printf("Out of memory!!\n");
                    ref_indices[i]--;
                    goto jump;
                }

                //페이지 폴트 발생
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
/*------------------------------- 함수 정의 --------------------------------------------------*/

/*------------------------------- 입력 및 초기화 함수 ------------------------------------------*/
void input()
{
    //시스템 파라미터(페이지 크기, 프레임 수, VAS 페이지 수) 입력
    fread(&sys_size.PAGESIZE, sizeof(int), 1, stdin);
    fread(&sys_size.PAS_FRAMES, sizeof(int), 1, stdin);
    fread(&sys_size.VAS_PAGES, sizeof(int), 1, stdin);

    sys_size.PARGETABLE_FRAMES = (sys_size.VAS_PAGES * PTE_SIZE)/sys_size.PAGESIZE;
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

        INIT_LIST_HEAD(&proc->list);
        list_add_tail(&proc->list, &job_queue);

        process_count++;
    }

    //페이지 테이블 프레임 예약
    next_free_frame = (sys_size.VAS_PAGES * 4 / sys_size.PAGESIZE) * process_count;
}

/*-------------------------------페이지 테이블 및 결과 출력 함수------------------------------*/
void print_page_tables(pte* page_tables[])
{
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

        printf("** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n", proc->pid, frame_count + ((sys_size.VAS_PAGES * 4 / sys_size.PAGESIZE)), faults[i], ref_indices[i]);

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

/*-------------------------------프레임 할당 함수------------------------------*/
int allocated_frame() {
    while(next_free_frame < sys_size.PAS_FRAMES && frame_in_use[next_free_frame]) {
        next_free_frame++;
    }
    if (next_free_frame >= sys_size.PAS_FRAMES) return -1;
    frame_in_use[next_free_frame] = true;
    return next_free_frame;
}
