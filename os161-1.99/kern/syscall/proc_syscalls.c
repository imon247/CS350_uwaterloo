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
#include <vfs.h>
#include <vnode.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include "opt-A2.h"
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */


#if OPT_A2



/* below are all exception handlers */
int sys_fork(struct trapframe *tf, pid_t *retval){
	struct proc *new_proc = proc_create_runprogram("New Proc");
	
	if(new_proc==NULL){
		DEBUG(DB_SYSCALL, "sys_fork does not create a new process\n");
		return ENPROC;
	}
	//struct addrspace *new_as;
	spinlock_acquire(&new_proc->p_lock);
	//kprintf("before calling as_copy, curproc: %d, child pid: %d\n", curproc->pid, new_proc->pid);
	int error = as_copy(curproc_getas(), &(new_proc->p_addrspace));
	
	//if(new_proc->p_addrspace==NULL){
	//	kprintf("fail copying %d's addrspace!!!\n", curproc->pid);
	//}
	spinlock_release(&new_proc->p_lock);
	
	
	if(error){
		proclist[new_proc->pid-1] = NULL;
		proc_destroy(new_proc);
		return ENOMEM;
	}

	/* Create thread for child process */	
	
	struct trapframe *ntf = kmalloc(sizeof(struct trapframe));
	memcpy(ntf, tf, sizeof(struct trapframe));
	
	if(ntf==NULL){
		DEBUG(DB_SYSCALL, "sys_fork_error: Could not create new trap frame\n");
		proclist[new_proc->pid-1] = NULL;
		proc_destroy(new_proc);
		return ENOMEM;
	}
	
	error = thread_fork(curthread->t_name, new_proc, &enter_forked_process, ntf, 1);
	
	if(error){
		proclist[new_proc->pid-1] = NULL;
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
	result = proclist[pid-1];
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
int sys_execv(const char *program, char **args){
	char *progname;
	char **argv;
	int argc=0;
	struct addrspace *old_as, *new_as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	
	size_t prog_name_len = strlen(program)+1;
	progname = kmalloc(sizeof(char)*prog_name_len);
	result = copyinstr((userptr_t)program, progname, prog_name_len, NULL);
	if(result){
		kfree(progname);
		return result;
	}
	
	while(args[argc]!=NULL){
		argc++;
	}
	argv = kmalloc(sizeof(char *)*argc);
	for(int i=0;i<argc;i++){
		size_t arg_len = strlen(args[i])+1;
		if(arg_len>1024) return E2BIG;
		
		argv[i] = kmalloc(sizeof(char)*arg_len);
		result = copyinstr((userptr_t)args[i], argv[i], arg_len, NULL);
		if(result){
			kfree(argv[i]);
			return result;
		}
	}
	argv[argc] = NULL;
	
	
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if(result) return result;
	
	new_as = as_create();
	if(new_as==NULL){
		vfs_close(v);
		return ENOMEM;
	}
	
	old_as = curproc_setas(new_as);
	as_activate();
	
	result = as_define_stack(new_as, &stackptr);
	if(result) return result;
	
	// copy argv to user space 
	vaddr_t top = 4;
	vaddr_t arg_ptr[argc];
	for(int i=0;i<argc;i++){
		result = copyoutstr(argv[0], (userptr_t)top, strlen(argv[0])+1, NULL);
		if(result) return result;
		arg_ptr[i] = top;
		top += strlen(argv[0])+1;
	}
	arg_ptr[argc] = 0;
	
	while(top%4 != 0) top++;
	for(int i=argc;i>=0;i--){
		int len = ROUNDUP(sizeof(vaddr_t), 4);
		result = copyout(arg_ptr+i, (userptr_t)top, sizeof(vaddr_t));
		top += len;
		if(result) return result;
	}
	
	
	result = load_elf(v, &entrypoint);
	if(result){
		vfs_close(v);
		return result;
	}
	vfs_close(v);
	
	
	as_destroy(old_as);
	
	enter_new_process(argc, (userptr_t)top, stackptr, entrypoint);
	
	panic("Enter_new_process returned\n");
	return -1;
}*/







int sys_execv(const char *program, char **args){
	
	//char *path;
	char **argv;
	int argc=0;
	int result;
	
	size_t pname_len = strlen(program)+1;
	char *path = kmalloc(sizeof(char)*pname_len);
	result = copyinstr((const_userptr_t)program, path, pname_len, NULL);
	if(result) {
		//errno = result;
		return result;
	}
	
	int j=0;
	while(*(args+j)!=NULL){
		if(argc>64){
			//errno = E2BIG;
			return E2BIG;
		}
		j++;
		argc++;
	}
	
	argv = kmalloc(sizeof(char*)*argc);
	int i=0;
	
	while(*(args+i) != NULL){
		size_t arg_len = strlen(*args)+1;
		if(arg_len>1024){
			//errno = E2BIG;
			return E2BIG;
		}
		*(argv+i) = kmalloc(sizeof(char)*arg_len);
		result = copyinstr((userptr_t)(*(args+i)), *(argv+i), arg_len, NULL);
		argv[i][arg_len] = '\0';
		i++;
	}
	*(argv+argc) = NULL;
	
	
	
	
	
	
	
	struct addrspace *new_as, *old_as;
	struct vnode *v;						// vnode is an abstract representation of a file
	vaddr_t entrypoint, stackptr;
	
	char *fname_temp;
	fname_temp = kstrdup(path);
	result = vfs_open(fname_temp, O_RDONLY, 0, &v);
	kfree(fname_temp);
	if(result){
		//errno = result;
		return result;
	}
	
	KASSERT(curproc_getas()!=NULL);
	new_as = as_create();
	if(new_as==NULL){
		vfs_close(v);
		//errno = ENOMEM;
		return ENOMEM;
	}
	
	as_deactivate();
	
	old_as = curproc_setas(new_as);
	as_activate();
	
	// load the excutable
	result = load_elf(v, &entrypoint);
	if(result){
		vfs_close(v);
		return result;
	}
	vfs_close(v);
	
	result = as_define_stack(new_as, &stackptr);
	if(result) return result;
	
	
	// copy strings into user stack
	vaddr_t arg_ptr[argc];
	for(int i=argc-1;i>=0;i--){
		stackptr -= strlen(*(argv+i))+1;
		arg_ptr[i] = stackptr;
		result = copyoutstr(*(argv+i), (userptr_t)stackptr, strlen(*(argv+i))+1, NULL);
		if(result) return result;
	}
	
	// copy argument array into user stack
	while(stackptr % 4 !=0) stackptr--;
	
	arg_ptr[argc] = 0;
	for(int i=argc;i>=0;i--){
		stackptr -= ROUNDUP(sizeof(vaddr_t), 4);
		// make a copy of the content that (arg_ptr+i) is pointing to, and assign it to be 
		//   the content that stackptr is pointing to 
		result = copyout(arg_ptr+i, (userptr_t)stackptr, sizeof(vaddr_t));  
		if(result) return result;
	}
	
	
	
	as_destroy(old_as);
	
	enter_new_process(argc, (userptr_t)stackptr, (vaddr_t)stackptr, entrypoint);
	
	panic("Enter_new_process returned\n");
	return -1;
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
