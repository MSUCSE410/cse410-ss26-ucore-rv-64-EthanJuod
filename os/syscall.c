#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include <stddef.h>

#define MAX_SYSCALL_NUM (500)

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	if (val == NULL) {
		return -1;
	}
	
	// Virtual address to Physical address
	struct proc *p = curr_proc();
	uint64 pa = useraddr(p->pagetable, (uint64)val);
	if (pa == 0) {
		return -1;  
	}
	
	TimeVal *kval = (TimeVal *)pa;
	
	uint64 cycle = get_cycle();
	kval->sec = cycle / CPU_FREQ;
	kval->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/
int sys_task_info(TaskInfo *ti) {
    struct proc *p = curr_proc();

	if (ti == NULL){
		return -1;
	}

	uint64 pa = useraddr(p->pagetable, (uint64)ti);
	if (pa == 0){
		return -1;
	}

	TaskInfo *kti = (TaskInfo *) pa;

    if (p->state == RUNNING) {
        kti->status = Running;
    }
    else if (p->state == RUNNABLE){
        kti->status = Ready;
    }
    else if (p->state == UNUSED){
        kti->status = UnInit;
    }
    else {
        kti->status = Exited;
    }


    for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        kti->syscall_times[i] = p->syscall_times[i];
    }


    kti->time = (int)(get_time() - p->start_time);


    return 0;
}

int sys_mmap(void *start, uint64 len, int port, int flag, int fd)
{
    struct proc *p = curr_proc();
    uint64 addr = (uint64)start;
    
    if (addr % PGSIZE != 0) {
        return -1;
    }

    if (len == 0) {
        return 0;
    }
    
    // len less that 1 GiB
    if (len > 1024 * 1024 * 1024) {
        return -1;
    }
    
    if ((port & ~0x7) != 0) {
        return -1;
    }
    
    if ((port & 0x7) == 0) {
        return -1;
    }
    
    uint64 npages = (len + PGSIZE - 1) / PGSIZE;
    
    // Check if any page in [addr, addr + len) is already mapped
    for (uint64 i = 0; i < npages; i++) {
        uint64 va = addr + i * PGSIZE;
        if (walkaddr(p->pagetable, va) != 0) {
            return -1;
        }
    }
    
    int perm = PTE_U | PTE_V; 
    if (port & 0x1) perm |= PTE_R;
    if (port & 0x2) perm |= PTE_W;
    if (port & 0x4) perm |= PTE_X;
    
    // Allocate and map pages
    for (uint64 i = 0; i < npages; i++) {
        uint64 va = addr + i * PGSIZE;
        
        void *pa = kalloc();
        if (pa == 0) {
            return -1;
        }
        
        // Clear the pages
        memset(pa, 0, PGSIZE);
        
        // Map the pages
        if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) != 0) {
            kfree(pa);
            return -1;
        }
    }
    
    return 0;
}

int sys_munmap(void *start, uint64 len){
	struct proc *p = curr_proc();
    uint64 addr = (uint64)start;


    if (addr % PGSIZE != 0){
        return -1;
    }

    if (len ==0){
        return 0;
    }

    uint64 npages = (len + PGSIZE -1)/ PGSIZE;

    for (uint i = 0; i < npages; i++){
        uint64 va = addr + i * PGSIZE;
        if (walkaddr(p->pagetable, va) == 0){
            return -1;
        }
    }

    uvmunmap(p->pagetable, addr, npages, 1);

    return 0;
}
extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	if (id < MAX_SYSCALL_NUM) {
        curr_proc()->syscall_times[id]++;
    }

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
        ret = sys_task_info((TaskInfo *)args[0]);
        break;
	//adding mmap and munmap
	case SYS_mmap:
        ret = sys_mmap((void *)args[0], args[1], args[2], args[3], args[4]);
        break;
    case SYS_munmap:
        ret = sys_munmap((void *)args[0], args[1]);
        break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}

