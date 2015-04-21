#include <stddef.h>
#include <stdint.h>
//#include <stdio.h>
#include "reg.h"
#include "asm.h"
#include "host.h"
#include "clib.c"

/* Size of our user task stacks in words */
#define STACK_SIZE	256

/* Number of user task */
#define TASK_LIMIT	5

/* USART TXE Flag
 * This flag is cleared when data is written to USARTx_DR and
 * set when that data is transferred to the TDR
 */
#define USART_FLAG_TXE	((uint16_t) 0x0080)

/* 72MHz */
#define CPU_CLOCK_HZ 72000000

/* 100 ms per tick. */
#define TICK_RATE_HZ 10

int handle;
int tickcount = 0;

typedef enum Task_state{
	waiting,
	running,
	ready,
	suspended,
	created
}task_state;

typedef struct list{
	const char *task_name;
	unsigned int priority;
	unsigned int *task_address;
	unsigned int user_stack[STACK_SIZE];
	task_state state;
}xList;

xList user_task[TASK_LIMIT];
static unsigned int scheduler_initial_flag = 1;

void usart_init(void)
{
	*(RCC_APB2ENR) |= (uint32_t) (0x00000001 | 0x00000004);
	*(RCC_APB1ENR) |= (uint32_t) (0x00020000);

	/* USART2 Configuration, Rx->PA3, Tx->PA2 */
	*(GPIOA_CRL) = 0x00004B00;
	*(GPIOA_CRH) = 0x44444444;
	*(GPIOA_ODR) = 0x00000000;
	*(GPIOA_BSRR) = 0x00000000;
	*(GPIOA_BRR) = 0x00000000;

	*(USART2_CR1) = 0x0000000C;
	*(USART2_CR2) = 0x00000000;
	*(USART2_CR3) = 0x00000000;
	*(USART2_CR1) |= 0x2000;
}

void print_str(const char *str)
{
	while (*str) {
		while (!(*(USART2_SR) & USART_FLAG_TXE));
		*(USART2_DR) = (*str & 0xFF);
		str++;
	}
}

void delay(volatile int count)
{
	count *= 50000;
	while (count--);
}


/* Exception return behavior */
#define HANDLER_MSP	0xFFFFFFF1
#define THREAD_MSP	0xFFFFFFF9
#define THREAD_PSP	0xFFFFFFFD

/* Initilize user task stack and execute it one time */
/* XXX: Implementation of task creation is a little bit tricky. In fact,
 * after the second time we called `activate()` which is returning from
 * exception. But the first time we called `activate()` which is not returning
 * from exception. Thus, we have to set different `lr` value.
 * First time, we should set function address to `lr` directly. And after the
 * second time, we should set `THREAD_PSP` to `lr` so that exception return
 * works correctly.
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Babefdjc.html
 */
unsigned int *create_task(unsigned int *stack, void (*start)(void), unsigned int priority, const char *name, size_t task_count)
{
	static int first = 1;

	stack += STACK_SIZE - 34; //- 32; /* End of stack, minus what we are about to push */
	if (first) {
		stack[8] = (unsigned int) start;
		first = 0;
	} else {
		stack[8] = (unsigned int) THREAD_PSP;
		stack[15] = (unsigned int) start;
		stack[16] = (unsigned int) 0x01000000; /* PSR Thumb bit */
	}


	user_task[task_count].state = created;
	user_task[task_count].priority = priority;
	user_task[task_count].task_name = name;

	stack = activate(stack);
	user_task[task_count].state = ready;

	char s[10];
	itoa(user_task[task_count].priority,s);
	print_str("task's priority is ");
	print_str(s);
	print_str("\n");

	return stack;
}


void task1_func(void)
{
	print_str("task1: Created!\n");
	print_str("task1: Now, return to kernel mode\n");
	syscall();
	while (1) {
		print_str(user_task[0].task_name);
		print_str(" : Running...\n");
		delay(1000);
	}
}

void task2_func(void)
{
	print_str("task2: Created!\n");
	print_str("task2: Now, return to kernel mode\n");
	syscall();
	while (1) {
		print_str(user_task[1].task_name);
		print_str(" : Running...\n");
		delay(1000);
	}
}

void task3_func(void)
{
	print_str("task3: Created!\n");
	print_str("task3: Now, return to kernel mode\n");
	syscall();
	while (1) {
		print_str(user_task[2].task_name);
		print_str(" : Running...\n");
		delay(1000);
	}
}

void task4_func(void)
{
	print_str("task4: Created!\n");
	print_str("task4: Now, return to kernel mode\n");
	syscall();
	while (1) {
		print_str(user_task[3].task_name);
		print_str(" : Running...\n");
		delay(1000);
	}
}

void write(char *buf, int len)
{
	host_action(SYS_WRITE, handle, (void *)buf, len);
}

unsigned int get_reload()
{
	return *SYSTICK_LOAD;
}

unsigned int get_current()
{
	return *SYSTICK_VAL;
}

unsigned int get_time()
{
//    static const unsigned int scale = 1000000 / configTICK_RATE_HZ;
                    /* microsecond */
//    static const unsigned int scale = 1000;

	return tickcount*(*SYSTICK_LOAD) + (*SYSTICK_LOAD-*SYSTICK_VAL);
//	return tickcount * scale +
//           ((float)*SYSTICK_LOAD - (float)*SYSTICK_VAL) / ((float)*SYSTICK_LOAD / scale);
}

/* calculate timestamp of task switching and implement it with semihosting. */
void task_switch_time()
{
	char buf[128];
	int len = snprintf(buf, 128, "task switch in : get time is = %d\n", get_time());
	write(buf,len);
}

void task_scheduler(xList tasks[], size_t task_number)
{
	int maximum = 0, max_number, i;
	char buf[128];
	unsigned int priorityTasks[TASK_LIMIT] = {0};
	unsigned int RRtasks[TASK_LIMIT];

	while (1) {

		if(scheduler_initial_flag == 1){
			for(i = 0 ; i < task_number ; i++){
				priorityTasks[i] = tasks[i].priority;
				if(tasks[i].priority >= maximum){
					maximum = tasks[i].priority;
					max_number = i;
				}
			}
			scheduler_initial_flag = 0;
		}

		print_str("OS: Activate next task\n");

		if(tickcount % task_number == 0){
			for(i=0 ; i<task_number ; i++)
				RRtasks[i] = priorityTasks[i];
		}

		for(i=0 ; i<task_number ; i++){
			if(RRtasks[i] >= maximum){
				maximum = RRtasks[i];
				max_number = i;
			}
		}
		if(tasks[max_number].state == ready)
		{
			tasks[max_number].state = running;
			tasks[max_number].task_address = activate(tasks[max_number].task_address);
		}
		if(tasks[max_number].state == running)
			tasks[max_number].state = ready;	

		maximum = 0;
		RRtasks[max_number] = 0;		// set task's priority be the smallest.

		tickcount ++;
		int len = snprintf(buf, 128, "task switch out : get time is = %d\n", get_time());
		write(buf,len);

		print_str("OS: Back to OS\n");
	}

}

int main(void)
{
	size_t task_count = 0;

	usart_init();

	handle = host_action(SYS_OPEN, "output/tasklog.txt", 6);
	print_str("OS: Starting...\n");
	print_str("OS: First create task 1\n");
	user_task[task_count].task_address = create_task(user_task[task_count].user_stack, &task1_func, 3, "taskName 1", task_count);
	task_count += 1;
	print_str("OS: Back to OS, create task 2\n");
	user_task[task_count].task_address = create_task(user_task[task_count].user_stack, &task2_func, 4, "taskName 2", task_count);
	task_count += 1;
	print_str("OS: Back to OS, create task 3\n");
	user_task[task_count].task_address = create_task(user_task[task_count].user_stack, &task3_func, 5, "taskName 3", task_count);
	task_count += 1;
	print_str("OS: Back to OS, create task 4\n");
	user_task[task_count].task_address = create_task(user_task[task_count].user_stack, &task4_func, 1, "taskName 4", task_count);
	task_count += 1;

	print_str("\nOS: Start priority-based scheduling!\n");

	/* SysTick configuration */
//	*SYSTICK_LOAD = (CPU_CLOCK_HZ / TICK_RATE_HZ) - 1UL;

//	*SYSTICK_LOAD = 720000;//7200000;
//	*SYSTICK_LOAD = 7200000;//7200000;

	*SYSTICK_LOAD = 7200000;//7200000;
	*SYSTICK_VAL = 0;
	*SYSTICK_CTRL = 0x07;

	task_scheduler(user_task, task_count);

	return 0;
}
