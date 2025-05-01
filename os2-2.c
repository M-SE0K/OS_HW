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
    int total_clocks;  
    
    bool is_io_wait;

    bool is_context_switching;
    int context_switch_end_time;
    
    bool flag;
} cpu;

LIST_HEAD(job_queue);

cpu cpus[2];
process* IDLE_PROC;
bool exit_flag = false;

void binaryFile_input();                                    //초기 이진 파일 입력 받는 메서드
void context_switch(cpu* c, int clock);                     
bool job_exit(int clock);
bool is_context_switching_check(cpu* c, int clock, int i);  
void load_job_to_ready_queue(int clock);                    //job_queue의 프로세스를 ready_queue로 올리는 메서드
void init_process();                                   //idle_process 초기화하는 메서드
void terminate_current_process(cpu* c, int clock);          //현재 실행 중인 프로세스의 종료를 확인하기 위한 메서드

int main()
{
    init_process();
    binaryFile_input();


    int clock = 0;
    cpus[0].running = IDLE_PROC;
    cpus[1].running = IDLE_PROC;

    int count = 60;
    while(1)
    {
        //printf("\n--------[clock: %d]------\n", clock);

        load_job_to_ready_queue(clock);
        for (int i = 0; i < 2; i++)
        {
            cpu* c = &cpus[i];
            //printf("1. CPU%d context_switch_end_time: %d\t is_context_switching: %d\n", i + 1, cpus[i].context_switch_end_time, cpus[i].is_context_switching);
            if(!is_context_switching_check(c, clock, i))
            {
                if (!c->flag)
                    continue;
                // if (job_exit)
                //     break;
                
            }   

            if (c->running == IDLE_PROC && !list_empty(&c->ready_queue) && !c->is_context_switching)
            //순수 IDLE 프로세스인데 수행할 작업이 존재하는 경우
            {
                if (clock == 0)
                {
                    (c->running) = list_entry(c->ready_queue.next, process, code_list);
                    
                }
                else
                {
                    c->is_context_switching  = true;
                    c->context_switch_end_time = clock + 10;
                    c->prev_proc = (c->running);
                    c->context_proc = list_entry(c->ready_queue.next, process, code_list);
                }
                
            }
            if (c->running == IDLE_PROC && !c->is_context_switching && !c->is_io_wait)
            //순수 IDLE 프로세스인데, 문맥전환중이 아니고, I/O도 기다리는 상태가 아닌 경우 그냥 순수 IDLE에 대한 처리
            {
                c->idle_clocks++;
                c->total_clocks++;
                continue;
            }

            code_tuple current = (c->running)->tuples[(c->running)->pc];

            switch (current.op)
            {
                case 0:
                {   
                    if ((c->running)->remaining_time == 0)  
                    {
                        (c->running)->remaining_time = current.length;
                       // printf("---------[DEBUG_IS_CPU_WORK_START] PID: %03d------------\n", (c->running)->info.pid);
                    }
                    // else
                    // {
                    //     //printf("---------\t[clock]: %d\n", clock);
                    //     //printf("---------[DEBUG_IS_CPU_WAIT] PID: %03d----------, remaining_time: %03d\n", (c->running)->info.pid, (c->running)->remaining_time);
                    // }
                    break;
                }
                case 1:
                {
                    if ((c->running)->remaining_time == 0 && !(c->is_io_wait))
                    {
                        (c->running)->remaining_time = current.length;
                        printf("%04d CPU%d: OP_IO START len: %03d ends at: %04d\n", clock, i + 1, current.length, clock + current.length);
                        (c->is_io_wait) = true;
                    }
                    else
                    {
                        //printf("--------[DEBUG_IS_IO_WORK_WAIT] PID: %03d------------\n", (c->running)->info.pid);
                    }
                    c->idle_clocks++;
                    break;
                }
                
                
            }

            if ((c->running) != IDLE_PROC)
                terminate_current_process(c, clock);
        }

        

        clock++;

        if (exit_flag)  break;
    }
}


void terminate_current_process(cpu* c, int clock)
{
    //printf("--------c[TERMINATE_CURRENT_PROCESS_START]------\n");
    (c->running)->remaining_time--;
    c->total_clocks++;

    //printf("[DEBUG]_[clock: %d] [CPU%d -> PID: %04d -> remaining: %d]\n", clock,(c-cpus) + 1, c->running->info.pid, (c->running)->remaining_time);
    if ((c->running)->remaining_time == 0 && (c->running) != IDLE_PROC)
    //현재 실행 중인 프로세스의 명령어가 실행 종료가 될 시간이 된 경우
    {
        
        (c->is_io_wait) = false;
        (c->running)->pc++;
        //printf("[DEBUG_IO_EXIT_NEXT_JOB] CPU: %d and clock: %d\n", (c-cpus) + 1, clock);
        if ((c->running)->pc >= (c->running->info.code_bytes / 2))
        //모든 명령어에 대해 실행이 완료된 경우
        {
            
            //printf("c->running_proc PID: %04d\n", c->running->info.pid);
            (c->is_context_switching) = true;
            //printf("[DEBUG_IS_CONTEXT_SWITCHING -- CPU%d]: %d\n", (c-cpus) + 1, (c->is_context_switching));
            (c->context_switch_end_time) = clock + 11;

            // printf("%d\n", c->context_switch_end_time);
            (c->prev_proc) = (c->running);

            //process* first = list_entry(c->ready_queue.next)

            if (!list_empty(&c->ready_queue))
            {
                (c->context_proc) = list_entry((c->ready_queue.next)->next, process, code_list);
                //printf("prev_proc PID: %03d\n", c->prev_proc->info.pid);
                //printf("context_proc PID: %03d\n", c->context_proc->info.pid);
                list_del_init(c->ready_queue.next);
            }
            else
                c->context_proc = IDLE_PROC;
            //printf("---------[DEBUG_IS_WORK_OUT]----------\n");
        }
    }
   // printf("-------[TERMINATE_CURRENT_PROCESS_EXIT]------\n");
}

bool is_context_switching_check(cpu* c, int clock, int i)
{
    // if (clock == 49 || clock == 45 || clock == 46|| clock == 47|| clock == 48|| clock == 49)
    // {
    //     printf("\nclock: %d\tCPU%d c->is_context_switching = %d\n", clock, (c-cpus) + 1, c->is_context_switching);
    //     printf("clock: %d\tCPU%d c->context_switch_end_time = %d\n", clock, (c-cpus) + 1, c->context_switch_end_time);
    // }

    if (c->is_context_switching)
    //현재 문맥전환 중인 상태인 경우 or 문맥전환을 시작해야 되는 경우
    {

        //printf("[DEBUG_IS_CONTEXT_SWITCHING_END_TIME: %d] and [PID: %03d] and [clock: %d]\n", (c->context_switch_end_time), (c->running)->info.pid, clock);
        if (clock == c->context_switch_end_time)
        //문맥전환이 종료 시간이 된 경우
        {   

            //printf("[clock == c->context_switch_end_time: %d] and [clock: %d]\n", (c->context_switch_end_time), clock);
            if (!list_empty(&c->ready_queue))
            //ready_queue에 전환 이후에도 작업해야할 프로세스가 남아있는 경우
            {
                printf("%04d CPU%d: Switched\tfrom: %03d\tto: %03d\n", clock, i + 1, (c->prev_proc)->info.pid, (c->context_proc)->info.pid);
                (c->running) = c->context_proc;
            }
            else
            //ready_queue가 비어 있는 경우
            {
                if ((c->prev_proc) == IDLE_PROC)
                //IDLE-> process
                {
                    printf("%04d CPU%d: Switched\tfrom: %03d\tto: %03d\n", clock, i + 1, (c->prev_proc)->info.pid, (c->context_proc->info).pid);
                    (c->running) = c->context_proc;
                }
                else
                //process -> IDLE
                {
                    printf("%04d CPU%d: Switched\tfrom: %03d\tto: %03d\n", clock, i + 1, (c->prev_proc)->info.pid, (IDLE_PROC->info).pid);
                    (c->running) = IDLE_PROC;
                }
                    
            }
            c->is_context_switching = false;
        }
        else
        //현재 문맥전환 상태이며, 문맥전환 종료 시간이 되지 않은 경우
        {
            c->idle_clocks++;
            c->total_clocks++;

            return false;   
        }
    }
    return true;
}

void context_switch(cpu* c, int clock)
{
    if(!list_empty(&c->ready_queue))
    {
        list_del_init(c->ready_queue.next);
        (c->running) = list_entry(c->ready_queue.next, process, code_list);
    }
    else
    {
        (c->running) = IDLE_PROC;
    }
    c->is_context_switching = false;
}

bool job_exit(int clock)
{
    if ( (!(cpus[0].is_io_wait) && !(cpus[1].is_io_wait)) && ((cpus[0].running == IDLE_PROC) && (cpus[1].running == IDLE_PROC)))
    {
        if (list_empty(&cpus[0].ready_queue) && list_empty(&cpus[1].ready_queue))
        {
            int TOTAL_CLOCKS = clock;
            int CPU1_IDLE_CLOCKS = cpus[0].idle_clocks;
            int CPU2_IDLE_CLOCKS = cpus[1].idle_clocks;

            double CPU1_UTIL = ((double)((double)TOTAL_CLOCKS - (double)CPU1_IDLE_CLOCKS) / (double)(TOTAL_CLOCKS)) * 100;
            double CPU2_UTIL = ((double)((double)TOTAL_CLOCKS - (double)CPU2_IDLE_CLOCKS) / (double)(TOTAL_CLOCKS)) * 100;

            double TOTAL_UTIL = (CPU1_UTIL + CPU2_UTIL) / 2;

            
            printf("*** TOTAL CLOCKS: %04d CPU1 IDLE: %04d CPU2 IDLE: %04d CPU1 UTIL: %2.2f%% CPU2 UTIL: %2.2f%% TOTAL UTIL: %2.2f%%\n",
                 TOTAL_CLOCKS, CPU1_IDLE_CLOCKS, CPU2_IDLE_CLOCKS, CPU1_UTIL, CPU2_UTIL, TOTAL_UTIL);
            exit_flag = true;
            return true;
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
        cpus[i].total_clocks = 0;
    }
}

void load_job_to_ready_queue(int clock)
{
    //printf("---------[DEBUG_IS_LOAD_JOB_START]--------\n");
    process* pos, *n;

    list_for_each_entry_safe(pos, n, &job_queue, code_list)
    //entry_safe 함수로 인한 무한루프, 반복도중 pos = n 순서가 깨지는 경우. move_tail 써서 구현해버리자.
    {
        struct list_head* p;
        int size1 = 0, size2 = 0;

        if ((pos->info.arrival_time <= clock))
        //도착시간이 됐거나 진작에 도착했어야 하는 경우에 대해 묶어서 처리함. 프로세스의 삽입 순서가 불균형할 것이라고 생각함.
        {
            //1. 로드 밸런싱 과제 조건 : "만약, ready_q가 서로 개수가 같다면 cpu1부터 넣어줌. 두 CPU 모두 CS중이라면, ready_q에 프로세스 로드가 불가능함"
            //2. "CPU1이 clock10~20, CPU2가 clock 13~23에 CS중이라면 15에 프로세스가 도착해도 해당 프로세스는 CPU1의 CS이 끝나는 20 clock에 ready_q에 로드할 것."
            //3. 둘 중 하나라도 CS가 아니라면 로드밸런싱의 1번 조건에 따를 것.
            list_for_each(p, &cpus[0].ready_queue)  {   size1++;    }
            list_for_each(p, &cpus[1].ready_queue)  {   size2++;    }

            int target = size1 <= size2 ? 1 : 2;
            //printf("[clock: %d] target=CPU%d\n", clock, target);
            
            //printf("CPU%d context_switch_end_time: %d\t is_context_switching: %d\n", 1, cpus[0].context_switch_end_time, cpus[0].is_context_switching);
            //printf("CPU%d context_switch_end_time: %d\t is_context_switching: %d\n", 2, cpus[1].context_switch_end_time, cpus[1].is_context_switching);
            if (cpus[0].context_switch_end_time == clock)
            {
                is_context_switching_check(&cpus[0], clock, 0);
                cpus[0].flag = true;
            }
            if (cpus[1].context_switch_end_time == clock)
            {
                is_context_switching_check(&cpus[1], clock, 1);
                cpus[1].flag = true;
            }   


            if (cpus[0].is_context_switching && cpus[1].is_context_switching)
            {
                //printf("거짓말.\n");
                //printf("[clock: %d]\n", clock);
                continue;
            }
            else
            //그냥 정상적인 경우
            {
                //printf("[DEBUG20]\n");
                //printf("이리온~\n");
                list_move_tail(&pos->code_list, &cpus[target - 1].ready_queue);
                printf("%04d CPU%d: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\n", clock, target, pos->info.pid, pos->info.arrival_time, pos->info.code_bytes);
            }
            jump:
            //cpus[target - 1].running = pos;
            
        }
        //printf("---------[DEBUG_IS_LOAD_JOB_EXIT]--------\n");
    }
    
    if (clock == 0)
    {
        printf("%04d CPU%d: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\n", clock, 1, IDLE_PROC->info.pid, IDLE_PROC->info.arrival_time, IDLE_PROC->info.code_bytes);
        printf("%04d CPU%d: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\n", clock, 2, IDLE_PROC->info.pid, IDLE_PROC->info.arrival_time, IDLE_PROC->info.code_bytes);
    }

}