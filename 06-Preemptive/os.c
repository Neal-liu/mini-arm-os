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
#define TASK_LIMIT	3

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
int task_number = 0;

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

void print_tick(int number){ 
	int i = 0, j, Tick_NOW = number ;
	char buf[16], str[16] = "tick=0\n"; 

	if (Tick_NOW != 0){
		while (Tick_NOW != 0){ 
			buf[i++] = (char)((Tick_NOW % 10) + '0'); 
			Tick_NOW /= 10;
		}
		for (j = 0; j < i; j++){
			str[j + 5] = buf[i - 1 - j]; 
		}
		str[j + 5] = '\n'; 
		str[j + 6] = 0; 
	} 
	print_str(str);
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
unsigned int *create_task(unsigned int *stack, void (*start)(void), int priority)
{
	static int first = 1;

	stack += STACK_SIZE - 34; //- 32; /* End of stack, minus what we are about to push */
	if (first) {
		stack[8] = (unsigned int) start;
		stack[9] = priority;
		first = 0;
	} else {
		stack[8] = (unsigned int) THREAD_PSP;
		stack[15] = (unsigned int) start;
		stack[16] = (unsigned int) 0x01000000; /* PSR Thumb bit */
		stack[17] = priority;
	}

//	print_tick(*(stack+15));
//	print_tick(*(stack+17));

	char ss1[10];
	char ss2[10];
	int temp1, temp2;
	temp1 = *(stack+9);
	itoa(temp1,ss1);
	temp2 = *(stack+17);
	itoa(temp2,ss2);
	if(temp2 == 0){
		print_str("task1's priority is ");
		print_str(ss1);
		print_str("\n");
	}
	else if (temp1 == 0){
		print_str("task's priority is ");
		print_str(ss2);
		print_str("\n");
	}

	task_number++;
	stack = activate(stack);

	return stack;
}


void task1_func(void)
{
	print_str("task1: Created!\n");
	print_str("task1: Now, return to kernel mode\n");
	syscall();
	while (1) {
		print_str("task1: Running...\n");
		delay(1000);
	}
}

void task2_func(void)
{
	print_str("task2: Created!\n");
	print_str("task2: Now, return to kernel mode\n");
	syscall();
	while (1) {
		print_str("task2: Running...\n");
		delay(1000);
	}
}

void semi_func(void){
//	signed int handle, error;
	
	print_str("Implements semihosting!!\n");
	handle = host_action(SYS_SYSTEM, "mkdir -p output");
	handle = host_action(SYS_SYSTEM, "touch output/tasklog.txt");
	handle = host_action(SYS_OPEN, "output/tasklog.txt", 4);
	if(handle == -1){
		print_str("open file error !!\n");
	}
	syscall();
	while(1){
		print_str("semihosting : Running...\n");
		delay(1000);
	}
}

void write(char *buf, int len){
	host_action(SYS_WRITE, handle, (void *)buf, len);
}

unsigned int get_reload(){
	return *SYSTICK_LOAD;
}

unsigned int get_current(){
	return *SYSTICK_VAL;
}

unsigned int get_time(){
//    static const unsigned int scale = 1000000 / configTICK_RATE_HZ;
                    /* microsecond */
//    static const unsigned int scale = 1000;

	return tickcount*(*SYSTICK_LOAD) + (*SYSTICK_LOAD-*SYSTICK_VAL);
//	return tickcount * scale +
//           ((float)*SYSTICK_LOAD - (float)*SYSTICK_VAL) / ((float)*SYSTICK_LOAD / scale);
}
void task_switch_time(){
	
	char buf[128];
	int len = snprintf(buf, 128, "task switch in  : get time is = %d\n", get_time());
	write(buf,len);
}

int main(void)
{
	unsigned int user_stacks[TASK_LIMIT][STACK_SIZE];
	unsigned int *usertasks[TASK_LIMIT];
	size_t task_count = 0;
//	size_t current_task;
	int temp, biggest = 0, biggestnumber, i;
	char t[10];
	char buf[128];
	unsigned int priorityTasks[TASK_LIMIT] = {0};
	unsigned int RRtasks[TASK_LIMIT];

	usart_init();

	print_str("OS: Starting...\n");
	print_str("OS: First create task 1\n");
	usertasks[0] = create_task(user_stacks[0], &task1_func, 4);
	task_count += 1;
	print_str("OS: Back to OS, create task 2\n");
	usertasks[1] = create_task(user_stacks[1], &task2_func, 2);
	task_count += 1;
	print_str("OS: Back to OS, create task 3\n");
	usertasks[2] = create_task(user_stacks[2], &semi_func, 3);
	task_count += 1;

	print_str("\nOS: Start round-robin scheduler!\n");

	/* SysTick configuration */
//	*SYSTICK_LOAD = (CPU_CLOCK_HZ / TICK_RATE_HZ) - 1UL;

//	*SYSTICK_LOAD = 720000;//7200000;
//	*SYSTICK_LOAD = 7200000;//7200000;

	*SYSTICK_LOAD = 720000;//7200000;
	*SYSTICK_VAL = 0;
	*SYSTICK_CTRL = 0x07;
//	current_task = 0;

	while (1) {
		print_str("OS: Activate next task\n");

		if(tickcount == 0){
			for(i=0 ; i<task_number ; i++){
				temp = *(usertasks[i]+19);
				itoa(temp,t);
				print_str("priority is = ");
				print_str(t);
				print_str("\n");
				priorityTasks[i] = temp;
				if(temp >= biggest){
					biggest = temp;
					biggestnumber = i;
				}
			}
			usertasks[biggestnumber] = activate(usertasks[biggestnumber]);
		}
		else{
			if(tickcount % task_number == 1){
				for(i=0 ; i<task_number ; i++)
					RRtasks[i] = priorityTasks[i];
			}

			for(i=0 ; i<task_number ; i++){
//				char s[10];
//				itoa(RRtasks[i],s);
//				print_str(s);
//				print_str("\n");
				if(RRtasks[i] >= biggest){
					biggest = RRtasks[i];
					biggestnumber = i;
				}
			}
			usertasks[biggestnumber] = activate(usertasks[biggestnumber]);
			biggest = 0;
			RRtasks[biggestnumber] = 0;		// I just want this task's priority be very small.
		}
		
		tickcount ++;
		int len = snprintf(buf, 128, "task switch out : get time is = %d\n", get_time());
		write(buf,len);

//		usertasks[current_task] = activate(usertasks[current_task]);
		print_str("OS: Back to OS\n");

//		current_task = current_task == (task_count - 1) ? 0 : current_task + 1;
	}

	return 0;
}
