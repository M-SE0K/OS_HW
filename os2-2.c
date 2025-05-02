#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include "list.h"
#include <stdbool.h>

#define IDLE_PID 100

typedef struct {
    unsigned char op;
    unsigned char length;
} code_tuple;

typedef struct {
    int pid;
    int arrival_time;
    int code_bytes;
} process_info;

typedef struct{
    process_info info;
    code_tuple* tuples;

    int pc;
    int remaining_time;

    struct list_head code_list;
} process;

typedef struct{
    struct list_head ready_queue;

    process* running;
    process* prev_proc;
    process* context_proc;

    int idle_clocks;
    bool is_io_wait;

    bool is_context_switching;
    int context_switch_end_time;
} cpu;

LIST_HEAD(job_queue);

cpu cpus[2];
process* IDLE_PROC;
bool exit_flag = false;
bool all_switching = false;
int idle_cnt[2] = {};

void init_process();                                        //cpu 및 idle 초기화 로직
void binaryFile_input();                                    //초기 이진 파일 입력 받는 로직
bool is_context_switching_check(cpu* c, int clock, int i);  //context_switching state 관리 및 출력하는 로직
void load_job_to_ready_queue(int clock);                    //job_queue의 프로세스를 ready_queue로 올리는 메서드
void terminate_current_process(cpu* c, int clock);          //현재 실행 중인 프로세스의 종료를 확인하기 위한 메서드
bool job_exit(int clock);                                   //종료 조건 검사 및 출력
int main()
{
    init_process();
    binaryFile_input();

    int clock = 0;
    cpus[0].running = IDLE_PROC;
    cpus[1].running = IDLE_PROC;

    while(1)
    //1 clock단위로 종료조건까지 무한 루프
    {
        load_job_to_ready_queue(clock);

        for (int i = 0; i < 2; i++)
        //현재 clock에 대한 cpu의 Operation Processing
        {
            cpu* c = &cpus[i];

            if(!is_context_switching_check(c, clock, i))
            {

                continue;   
            }   
            //CS가 끝나지 않은 경우 프로세스 작업을 하지 못함("CS중 프로세스는 IDLE 상태가 됨.")

            if (c->running == IDLE_PROC && !list_empty(&c->ready_queue) && !c->is_context_switching)
            //CPU가 IDLE_PROC를 실행하고 있지만, CPU의 ready_queue에 프로세스가 존재하며, 문맥전환이 아닌 경우
            {
                if (clock == 0) {   (c->running) = list_entry(c->ready_queue.next, process, code_list); }
                //clock이 0일 때는 CS를 하지 않음

                else
                //이외의 모든 상황은 IDLE -> 프로세스(I/O, CPU)간의 전환
                {
                    c->is_context_switching  = true;
                    c->context_switch_end_time = clock + 10;
                    c->prev_proc = (c->running);
                    c->context_proc = list_entry(c->ready_queue.next, process, code_list);
                    c->idle_clocks++;
                }
                
            }
            if (c->running == IDLE_PROC && !c->is_context_switching && !c->is_io_wait)
            //순수 IDLE를 실행하고 있는 경우, CS를 하는 중도 아니고 && I/O 대기를 하고 있지도 않은
            {
                c->idle_clocks++;
                continue;
            }

            code_tuple current = (c->running)->tuples[(c->running)->pc];

            switch (current.op)
            //수행할 명령어의 부호에 따른 처리 0 : CPU_JOB, 1 : I/O_
            {
                case 0:
                {   
                    if ((c->running)->remaining_time == 0)  {   (c->running)->remaining_time = current.length;  }
                    //최초 실행의 경우 명령어의 remaining_time(종료 시간)을 length로 초기화
                    break;
                }
                case 1:
                {
                    if ((c->running)->remaining_time == 0 && !(c->is_io_wait))
                    //최초 실행의 경우 명령어의 remaining_time(종료 시간)을 length로 초기화하며, 실행 중인 명령어가 I/O 작업에 대한 대기 중인 것을 방지하기 위함.
                    //I/O를 실행하고 있지 않고, is_io_wait()으로 표현함. 추후 변경 필요
                    {
                        (c->running)->remaining_time = current.length;
                        (c->is_io_wait) = true;
                        printf("%04d CPU%d: OP_IO START len: %03d ends at: %04d\n", clock, i + 1, current.length, clock + current.length);
                    }
                    c->idle_clocks++;
                    break;
                }
            }
            if ((c->running) != IDLE_PROC)  {   terminate_current_process(c, clock);    }
            //각 CPU가 실행 중인 프로세스의 종료 조건을 검사하기 위함.
        }
        clock++;
        
        

    }
}
void load_job_to_ready_queue(int clock)
{
    process* pos, *n;

    list_for_each_entry_safe(pos, n, &job_queue, code_list)
    {
        struct list_head* p;
        int size1 = 0, size2 = 0;

        if ((pos->info.arrival_time <= clock))
        //도착시간이 됐거나 진작에 도착했어야 하는 경우에 대해 묶어서 처리함. 프로세스의 삽입 순서가 불규칙할 것이라고 생각함.
        {
            list_for_each(p, &cpus[0].ready_queue)  {   size1++;    }
            list_for_each(p, &cpus[1].ready_queue)  {   size2++;    }

            int target = size1 <= size2 ? 1 : 2; 
            //단순순회를 통한 각 ready_queue의 사이즈 비교 및 Load Balance 실시("두 cpu의 ready_queue의 사이즈가 같은 경우 cpu1에 삽입할 것.")

            if (cpus[0].is_context_switching && cpus[1].is_context_switching)   
            {   
                all_switching = true;
                //printf("clock: %d [All_switching] and all_swithcing = %d\n", clock, all_switching);
                
                continue;   
            }
            //두 cpu가 모두 CS중인 경우 프로세스 로드를 미룸.

            else
            {
                list_move_tail(&pos->code_list, &cpus[target - 1].ready_queue);
                printf("%04d CPU%d: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\n", clock, target, pos->info.pid, pos->info.arrival_time, pos->info.code_bytes);
            }
        }
    }
    
    if (clock == 0)
    //최초 실행시 0 arrival_time이 0인 프로세스들에 대한 로드 및 출력을 마친 이후 출력할 것.
    {
        printf("%04d CPU%d: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\n", clock, 1, IDLE_PROC->info.pid, IDLE_PROC->info.arrival_time, IDLE_PROC->info.code_bytes);
        printf("%04d CPU%d: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\n", clock, 2, IDLE_PROC->info.pid, IDLE_PROC->info.arrival_time, IDLE_PROC->info.code_bytes);
    }

}
bool is_context_switching_check(cpu* c, int clock, int i)
{
    if (c->is_context_switching)
    //현재 CS가 진행 or 종료상태가 되는 경우
    {
        if (clock == c->context_switch_end_time)
        //현재 CS가 종료시간이 된 경우
        {   
            //printf("CPU%d->context_switch_end_time: %d\n", (c-cpus) + 1, c->context_switch_end_time);
            if (!list_empty(&c->ready_queue))
            //현재 CPU의 ready_queue에 프로세스가 존재하는 경우 cpu의 running 상태를 초기화해줌.
            {
                printf("%04d CPU%d: Switched\tfrom: %03d\tto: %03d\n", clock, i + 1, (c->prev_proc)->info.pid, (c->context_proc)->info.pid);
                (c->running) = c->context_proc;
            }
            else
            //현재 CPU의 ready_queue에 프로세스가 존재하지 않는 경우
            {
                if ((c->prev_proc) == IDLE_PROC)
                //IDLE_PROC에서 프로세스(실제 작업이 존재하는)로의 작업 전환인 경우
                {
                    
                    printf("%04d CPU%d: Switched\tfrom: %03d\tto: %03d\n", clock, i + 1, (c->prev_proc)->info.pid, (c->context_proc->info).pid);
                    (c->running) = c->context_proc;
                }
                else
                //프로세스에서 IDLE_PROC로 전환하는 경우
                {
                    job_exit(clock);
                    //프로세스의 종료 조건에 해당되는 경우 job_queue가 모두 비워지고, 모든 프로세스가 IDLE_PROC가 되며, ready_queue가 비워지는 경우

                    printf("%04d CPU%d: Switched\tfrom: %03d\tto: %03d\n", clock, i + 1, (c->prev_proc)->info.pid, (IDLE_PROC->info).pid);
                    (c->running) = IDLE_PROC;
                }
            }
            c->is_context_switching = false;
            if (all_switching)
            {
                all_switching = false;
                load_job_to_ready_queue(clock);
            }
            
            //CS가 완료된 경우 각 CPU의 is_context_switching 플래그 초기화
        }
        else
        //현재 문맥전환 상태이며, 문맥전환 종료 시간이 되지 않은 경우
        {
            c->idle_clocks++;
            return false;
        }
    }
    return true;
}
void terminate_current_process(cpu* c, int clock)
{
    (c->running)->remaining_time--;
    
    if ((c->running)->remaining_time == 0 && (c->running) != IDLE_PROC)
    //실행 중인 프로세스의 명령어가 종료됐는 지 검사
    {
        
        (c->is_io_wait) = false;
        (c->running)->pc++;
        //다음에 실행할 명령어 및 종료조건 검사를 위함. "각 프로세스의 수행 진척도(수행 중인 코드 위치 관리)"

        if ((c->running)->pc >= (c->running->info.code_bytes / 2))
        //현재 실행중인 프로세스의 명령어를 모두 실행했는 지에 대한 검사함. code_bytes / 2 == code_tuple이므로
        {
            (c->is_context_switching) = true;
            (c->context_switch_end_time) = clock + 10 + 1;
            (c->prev_proc) = (c->running);
            //현재 실행 중인 프로세스의 명령어를 모두 실행하였으므로, CS 진행을 위한 flag 초기화
            //CS 수행 시간 저장. 현재 clock까지의 연산 종료 이후 CS가 이루어지는 것이므로 +10에 현재 clock에 대해 +1을 함.
            //CS 수행 중 CPU가 IDLE를 실행하기 위함

            if (!list_empty(&c->ready_queue))
            //CS 수행을 위해 실행할 프로세스를 미리 꺼내두기 위해 CPU의 ready_queue가 비어있는 지 검사
            {
                (c->context_proc) = list_entry((c->ready_queue.next)->next, process, code_list);
                list_del_init(c->ready_queue.next);
            }
            else    {   c->context_proc = IDLE_PROC;    }
        }
    }
}



bool job_exit(int clock)
{
    if (((cpus[0].running -> remaining_time <= 0) || cpus[0].running == IDLE_PROC)
     && ((cpus[1].running -> remaining_time <= 0) || cpus[1].running == IDLE_PROC))
    {
        if ((list_empty(&cpus[0].ready_queue) && list_empty(&cpus[1].ready_queue)) && list_empty(&job_queue))
        {   
            if (cpus[0].running == IDLE_PROC)   {   cpus[0].idle_clocks--;  }

            int TOTAL_CLOCKS = clock - 10;
            int CPU1_IDLE_CLOCKS = cpus[0].idle_clocks - 10;
            int CPU2_IDLE_CLOCKS = cpus[1].idle_clocks - 10;

            double CPU1_UTIL = ((double)((double)TOTAL_CLOCKS - (double)CPU1_IDLE_CLOCKS) / (double)(TOTAL_CLOCKS)) * 100;
            double CPU2_UTIL = ((double)((double)TOTAL_CLOCKS - (double)CPU2_IDLE_CLOCKS) / (double)(TOTAL_CLOCKS)) * 100;

            double TOTAL_UTIL = (CPU1_UTIL + CPU2_UTIL) / 2;

            
            printf("*** TOTAL CLOCKS: %04d CPU1 IDLE: %04d CPU2 IDLE: %04d CPU1 UTIL: %2.2f%% CPU2 UTIL: %2.2f%% TOTAL UTIL: %2.2f%%\n",
                 TOTAL_CLOCKS, CPU1_IDLE_CLOCKS, CPU2_IDLE_CLOCKS, CPU1_UTIL, CPU2_UTIL, TOTAL_UTIL);
            exit_flag = true;
            exit(0);
        }
    }
    
    return false;
}
void binaryFile_input()
{
    process_info temp_info;
    code_tuple temp_tuple;

    while(fread(&temp_info, sizeof(process_info), 1, stdin) == 1)
    {
        process* new_proc = (process*)malloc(sizeof(process));
        new_proc->info = temp_info;
        new_proc->tuples = (code_tuple*)malloc(sizeof(code_tuple) * (temp_info.code_bytes / 2));
        
        for (int i = 0; i < (temp_info.code_bytes / 2); i++)
        {
            fread(&temp_tuple, sizeof(code_tuple), 1, stdin);
            new_proc->tuples[i] = temp_tuple;
        }
        
        new_proc->pc = 0;
        INIT_LIST_HEAD(&new_proc->code_list);
        list_add_tail(&new_proc->code_list, &job_queue);
    }
}
void init_process()
{
    IDLE_PROC = (process*)malloc(sizeof(process));
    IDLE_PROC->info.pid = IDLE_PID;
    IDLE_PROC->info.arrival_time = 0;
    IDLE_PROC->info.code_bytes = 2;
    IDLE_PROC->tuples = (code_tuple*)malloc(sizeof(code_tuple));

    IDLE_PROC->tuples[0].op = 0xFF;
    IDLE_PROC->tuples[0].length = 1;
    IDLE_PROC->pc = 0;
    INIT_LIST_HEAD(&IDLE_PROC->code_list);

    for (int i = 0; i < 2; i++)
    {
        INIT_LIST_HEAD(&cpus[i].ready_queue);
        cpus[i].running = NULL;
        cpus[i].idle_clocks = 0;
    }
}

