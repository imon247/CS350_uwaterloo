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
	//kprintf("Hi %d a new process with parent %d!\n", new_proc->pid, new_proc->parent->pid);;
	//struct addrspace *new_as;
	spinlock_acquire(&new_proc->p_lock);
	int error = as_copy(curproc_getas(), &(new_proc->p_addrspace));
	spinlock_release(&new_proc->p_lock);
	kprintf("Hi %d got the new addrspace!\n", new_proc->pid);
	/*
	if(error){
		proc_destroy(new_proc);
		return ENOMEM;
	}*/

	/* Create thread for child process */	
	
	struct trapframe *ntf = kmalloc(sizeof(struct trapframe));
	memcpy(ntf, tf, sizeof(struct trapframe));
	//kprintf("%d copy the parent %d tf\n", new_proc->pid, new_proc->parent->pid);
	//cpytf(ntf, tf);
	/*
	if(ntf==NULL){
		DEBUG(DB_SYSCALL, "sys_fork_error: Could not create new trap frame\n");
		proc_destroy(new_proc);
		return ENOMEM;
	}*/
	
	error = thread_fork(curthread->t_name, new_proc, &enter_forked_process, ntf, 1);
	/*
	if(error){
		proc_destroy(new_proc);
		kfree(ntf);
		ntf = NULL;
		DEBUG(DB_SYSCALL, "sys_fork: Can not create new thread");
		return ENOTSUP;
	}*/
	
	*retval = new_proc->pid;
	//kprintf("A new process with pid %d is finally forked\n", new_proc->pid);
	return (0);
}

int sys_getpid(pid_t *retval){
	*retval = curproc->pid;
	//kprintf("Return value is: %d\n", *retval);
	return (0);
}
/*
int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval){
  int exitstatus;
  int result;
  kprintf("Process %d is waiting for process %d\n", curproc->pid, pid);
  if (options != 0) {
    return(EINVAL);
  }
  lock_acquire(proc_lock);
  
  bool a = proclist[pid]!=NULL;
  bool b = proclist[pid]->exit>=0;
  bool c = proclist[pid]->parent->pid==curproc->pid;
  bool permissive = a && b && c;
  if(permissive){
  	*retval = pid;
	exitstatus = proclist[pid]->exit;
	result = copyout((void *)&exitstatus,status,sizeof(int));
  }
  else if(a==false){
  	return ESRCH;
  }
  else if(a==true && (b==false || c==false)){
    return ECHILD;
  }
  else{
	  while(permissive==false){
	  	cv_wait(pass, proc_lock);
	  	permissive = proclist[pid]!=NULL && proclist[pid]->exit>=0 && proclist[pid]->parent->pid==curproc->pid;
	  }
	  *retval = pid;
	  exitstatus = _MKWAIT_EXIT(proclist[pid]->exit);
	  result = copyout((void *)&exitstatus,status,sizeof(int));
  }
  
  lock_release(proc_lock);
  
  
  // for now, just pretend the exitstatus is 0 
  return(0);
}
*/
/*
void sys__exit(int exitcode){
	struct proc *p = curproc;
	lock_acquire(proc_lock);
	p->exit = _MKWAIT_EXIT(exitcode);
	
	for(int i=0;i<MAXPROC;i++){
		// the proc to be deleted is the parent of some processes that have not exited 
		if(p==proclist[i]->parent && proclist[i]->exit==-1){
			proclist[i]->parent = NULL;
		}
		// the proc to be deleted is the parent of some processes that have exited 
		if(p==proclist[i]->parent && proclist[i]->exit!=-1){
			proc_destroy(proclist[i]);
			proclist[i] = NULL;
		}
	}
	// the proces to be deleted has no parent 
	if(p->parent==NULL){
		proclist[p->pid] = NULL;
		proc_destroy(p);
	}
	// the process to be deleted has a parent 
	else{
		cv_broadcast(pass, proc_lock);
	}
	lock_release(proc_lock);
	
	
	
	
	
	
	struct addrspace *as;
	//struct proc *p = curproc;
	
	DEBUG(DB_SYSCALL, "Syscall: _exit(%d)\n", exitcode);
	
	KASSERT(curproc->p_addrspace != NULL);
	as_deactivate();
	
	as = curproc_setas(NULL);
	as_destroy(as);
	
	proc_remthread(curthread);
	
	proc_destroy(p);
	thread_exit();
	
	panic("return from thread_exit in sys__exit\n");
}
*/
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
