#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <mips/trapframe.h>
#include "opt-A2.h"
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */


#if OPT_A2



/* below are all exception handlers */
int sys_fork(struct trapframe *tf, pid_t *retval){
	struct proc *new_proc = proc_create_runprogram("New Proc");
	/*
	if(new_proc==NULL){
		DEBUG(DB_SYSCALL, "sys_fork does not create a new process\n");
		return ENPROC;
	}*/
	//struct addrspace *new_as;
	spinlock_acquire(&new_proc->p_lock);
	//kprintf("before calling as_copy, curproc: %d, child pid: %d\n", curproc->pid, new_proc->pid);
	int error = as_copy(curproc_getas(), &(new_proc->p_addrspace));
	
	//if(new_proc->p_addrspace==NULL){
	//	kprintf("fail copying %d's addrspace!!!\n", curproc->pid);
	//}
	spinlock_release(&new_proc->p_lock);
	
	
	if(error){
		proclist[new_proc->pid] = NULL;
		proc_destroy(new_proc);
		return ENOMEM;
	}

	/* Create thread for child process */	
	
	struct trapframe *ntf = kmalloc(sizeof(struct trapframe));
	memcpy(ntf, tf, sizeof(struct trapframe));
	
	if(ntf==NULL){
		DEBUG(DB_SYSCALL, "sys_fork_error: Could not create new trap frame\n");
		proclist[new_proc->pid] = NULL;
		proc_destroy(new_proc);
		return ENOMEM;
	}
	
	error = thread_fork(curthread->t_name, new_proc, &enter_forked_process, ntf, 1);
	
	if(error){
		proclist[new_proc->pid] = NULL;
		proc_destroy(new_proc);
		kfree(ntf);
		ntf = NULL;
		DEBUG(DB_SYSCALL, "sys_fork: Can not create new thread");
		return ENOTSUP;
	}
	
	*retval = new_proc->pid;
	//kprintf("A new process with pid %d is finally forked\n", new_proc->pid);
	//lock_acquire(new_proc->exit_lock);
	return (0);
}

int sys_getpid(pid_t *retval){
	*retval = curproc->pid;
	//kprintf("Return value is: %d\n", *retval);
	return (0);
}

struct proc * find_proc(int pid){
	struct proc *result;
	P(proc_count_mutex);
	result = proclist[pid];
	V(proc_count_mutex);
	return result;
}

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval){
  int exitstatus;
  int result;
  struct proc *cp = curproc;
  //kprintf("Process %d is waiting for process %d\n", curproc->pid, pid);
  if (options != 0) {
    return(EINVAL);
  }
  
  struct proc *target = find_proc(pid);
  if(target==NULL){
  	 return ESRCH;
  }
  if(target->parent != cp){
  	 return ECHILD;
  }
  
  lock_acquire(target->exit_lock);
  //kprintf("proc %d acquires the proc %d's lock!\n", cp->pid, target->pid);
  while(target->is_exit==false){
  	 cv_wait(target->exit_cv, target->exit_lock);
  }
  *retval = target->pid;
  exitstatus = _MKWAIT_EXIT(target->exitcode);
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if(result != 0){
     lock_release(target->exit_lock);
  	 return result;
  }
  //kprintf("proc %d releases the proc %d's lock!\n", cp->pid, target->pid);
  lock_release(target->exit_lock);
  
  return(0);
}


void sys__exit(int exitcode)
{
	struct proc *cp = curproc;
	cp->exitcode = exitcode;
	//kprintf("proc %d wants to exit!\n", cp->pid);
	P(proc_count_mutex);
	for(int i=0;i<MAXPROC;i++){
		if(proclist[i]!=NULL && proclist[i]->parent==cp){
			proclist[i]->parent = NULL;
		}
	}
	V(proc_count_mutex);
	
	lock_acquire(cp->exit_lock);
	//kprintf("proc %d acquires the lock of its own!\n", cp->pid);
	cp->is_exit = true;
	cv_broadcast(cp->exit_cv, cp->exit_lock);
	lock_release(cp->exit_lock);
	//kprintf("proc %d releases the lock of its own!\n", cp->pid);
	
	struct addrspace *as;
	
	DEBUG(DB_SYSCALL, "Syscall: _exit(%d)\n", exitcode);
	
	KASSERT(curproc->p_addrspace != NULL);
	as_deactivate();
	
	as = curproc_setas(NULL);
	as_destroy(as);
	
	proc_remthread(curthread);
	
	P(proc_count_mutex);
	proc_count--;
	V(proc_count_mutex);
	//kprintf("current no of proc: %d\n", proc_count);
	if (proc_count == 0) {
		proc_count++;
		proc_destroy(cp);
		
	}
	
	//kprintf("proc %d deleted!!!!!!\n", pid);
	thread_exit();
	
	panic("return from thread_exit in sys__exit\n");
}


/*
void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  // for now, just include this to keep the compiler from complaining about
     an unused variable 

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  
 //  * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   
  as = curproc_setas(NULL);
  as_destroy(as);

  // detach this thread from its process 
  // note: curproc cannot be used after this call 
  proc_remthread(curthread);

  // if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread 
  proc_destroy(p);
  
  thread_exit();
  // thread_exit() does not return, so we should never get here 
  panic("return from thread_exit in sys_exit\n");
}
*/

/*
int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;
	
   //this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  

  if (options != 0) {
    return(EINVAL);
  }
  // for now, just pretend the exitstatus is 0 
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
*/
#else
void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
#endif
