#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <limits.h>

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)

struct list_head {
	struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next) {
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head) {
	__list_add(new, head, head->next);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head) {
	__list_add(new, head->prev, head);
}

static inline void __list_del(struct list_head * prev, struct list_head * next) {
	next->prev = prev;
	prev->next = next;
}

static inline void list_del(struct list_head *entry) {
	__list_del(entry->prev, entry->next);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

static inline void list_del_init(struct list_head *entry) {
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

static inline void list_move(struct list_head *list, struct list_head *head) {
        __list_del(list->prev, list->next);
        list_add(list, head);
}

static inline void list_move_tail(struct list_head *list,
				  struct list_head *head) {
        __list_del(list->prev, list->next);
        list_add_tail(list, head);
}

static inline int list_empty(const struct list_head *head) {
	return head->next == head;
}

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_for_each(pos, head) \
  for (pos = (head)->next; pos != (head);	\
       pos = pos->next)

#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev; prefetch(pos->prev), pos != (head); \
        	pos = pos->prev)

#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_entry((head)->prev, typeof(*pos), member);	\
	     &pos->member != (head); 	\
	     pos = list_entry(pos->member.prev, typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
		n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

#define list_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = list_entry((head)->prev, typeof(*pos), member),	\
		n = list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.prev, typeof(*n), member))

#if 0    //DEBUG
#define debug(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define debug(fmt, args...)
#endif

typedef struct{
    unsigned char action;
    unsigned char length;
    int visit;                                  //처음 I/O코드가 실행될 때 출력해주기 위해 방문 표시
} Code_tuples;


typedef struct{
    int pid;
    int arrival_time;
    int code_bytes;
    int pc;                                     //program counter
    int complete;                               //program이 종료됐는지 표시
    Code_tuples* code;
    struct list_head job, ready, wait;
} Process;

///////////////////////////////////////////////////////////////////////////////////////////-----> 전역변수 선언
    Process *process;                                                                    //
    Process *next;                                                                       //
    Process *idle;                                                                       //
    Code_tuples code_tuple;                                                              //
    int clock_=0, idle_time=0, cont_sw_t = 0, cont_sw_cur, cont_sw_flag;                 //
                                                                                         // 
    LIST_HEAD(job_q);                                                                    //
    LIST_HEAD(ready_q);                                                                  //
    LIST_HEAD(wait_q);                                                                   //
///////////////////////////////////////////////////////////////////////////////////////////


void process_copy(Process in, Process* out){
    out->pid = in.pid;
    out->arrival_time = in.arrival_time;
    out->code_bytes = in.code_bytes;
}

//context switching 수행해주고 끝나면 true 반환
int context_switching_done(){
    cont_sw_t++;
    clock_++;
    if(cont_sw_t == 9){
        idle_time += 10;
        cont_sw_t = 0;
        return 1;
    }
    return 0;
}

//IO 동작 처리하는 함수
void I_O_operation(Process *cur_p){
    if(cur_p->code[cur_p->pc].visit == 0){ //처음 도착하면 I/O 동작 출력
        cur_p->code[cur_p->pc].visit = 1;
        printf("%04d CPU: OP_IO START len: %03d ends at: %04d\n", clock_, cur_p->code[cur_p->pc].length, clock_ + cur_p->code[cur_p->pc].length);
    }
    if(cur_p->code[cur_p->pc].length != 0){ //해당 코드 길이를 1씩 줄여가며 수행시간 체크해줌
        cur_p->code[cur_p->pc].length--;
        idle_time++;
    }else{  //코드 길이만큼 수행했으면 pc증가
        cur_p->pc++;
        clock_--;   //pc 증가할 때에는 clock이 증가X
    }
}

//CPU 동작 처리하는 함수
void CPU_operation(Process *cur_p){ 
    if(cur_p->code[cur_p->pc].length != 0){
        cur_p->code[cur_p->pc].length--;
    }else{
        cur_p->pc++;
        clock_--;
    }
}

void simulator(){                   //simulator 기본 구조
    Process *cur_p = idle;          //현재 동작하고 있는 프로세스
    Process *next_p;                //다음 프로세스
    int flag = 0, count = 0;        //첫 반복 때 context switching 수행을 막기 위한 flag

    while(1){

        //job_q에 있는 작업들 ready_q에 로드 및 종료조건 검사
        list_for_each_entry(process, &job_q, job){
            if(process->arrival_time == clock_){
                list_add_tail(&process->ready, &ready_q);
                printf("%04d CPU: Loaded PID: %03d\tArrival: %03d\tCodesize: %03d\tPC: %03d\n", clock_, process->pid, process->arrival_time, process->code_bytes, process->pc);
            }
            else if(process->pid!=100 && process->complete==0){ //idle process를 제외하고 수행해야하는 process를 count
                count++;
            }
        }if(count == 0){
            printf("*** TOTAL CLOCKS: %04d IDLE: %04d UTIL: %2.2f%%\n", clock_-1, idle_time, ((clock_-1)-idle_time)*1.0/(clock_-1)*100);
            break;
        }else{
            count = 0;
        }

        list_move_tail(&idle->ready, &ready_q); //idle process를 ready_q의 맨 뒤로 보내고

        //ready_q 맨 앞에 있는 process 가져옴
        struct list_head *next_node = ready_q.next;
        next_p = list_entry(next_node, Process, ready);

        if(cur_p->pid == 100){ //현재 수행 process가 idle일 때 다음 process를 수행하기 위해 context switch
            if(flag){
                if(context_switching_done()){   //context switch 소요시간이 종료되면
                    cur_p = next_p;             //현재 process를 다음 process로 전환
                }
                
            }else{
                flag = 1;
                cur_p = next_p;
            }
        }

        if(cur_p->pid != 100){  //현재 수행중인 process가 있을 때
            if(cur_p->pc >= cur_p->code_bytes / 2){ //process 종료 조건
                list_del(&cur_p->ready);
                cur_p->complete = 1;                //완료됐다고 표시
                cur_p = idle;                       //context switch 수행을 위해 현재 동작중인 process가 idle이라고 표시
            }
            else{
                if(cur_p->code[cur_p->pc].action == 1){
                    I_O_operation(cur_p);   //IO 동작일 때
                }else{
                    CPU_operation(cur_p);   //cpu 동작일 때
                }
            }
            clock_++;
        }
    }
}

int main(int argc, char* argv[]){

    Process cur;

    while(fread(&cur, sizeof(int) * 3, 1, stdin) == 1){
        process = malloc(sizeof(*process));
        
        process_copy(cur, process);
        
        INIT_LIST_HEAD(&process->job);
        INIT_LIST_HEAD(&process->ready);
        INIT_LIST_HEAD(&process->wait);

        process->code = malloc(process->code_bytes / 2 * sizeof(Code_tuples));
        for(int i = 0; i < process->code_bytes / 2 ; i++){
            fread(&code_tuple, sizeof(unsigned char)*2 , 1, stdin);
            process->code[i] = code_tuple;
        }

        list_add_tail(&process->job, &job_q);
    }

    idle = malloc(sizeof(Process));
    idle->pid = 100;
    idle->code_bytes=2;
    idle->code = malloc(sizeof(Code_tuples));
    idle->code[0].action = 0xff;

    list_add_tail(&idle->job, &job_q);

    simulator();    //simulator 실행

    list_for_each_entry_safe(process, next, &job_q, job){
        list_del(&process->job);
        free(process->code);
        free(process);
    }

    return 0;
}
