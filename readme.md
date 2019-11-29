# Archive: Concurrent Computing OS Coursework

*Note: Unfortunately the original notes document submitted with this coursework was lost when SAFE was
decommissioned in October. The below list is not a full technical specification but does a good job of
outlining the functionality of the OS.*

This OS provides:

* A weighted round-robin scheduler based on timer interrupts
* An alternative priority-aging scheduler
* `fork()`, `exec()`, `kill()`, `yield()` and `nice()` system calls for process handling
    * New `execx()` system call to start a program with an argument
* Semaphores to lock system resources, supported by two new system calls
* Per-process file descriptors supporting redirection
* Pipes
* An inode-based file system supporting
    * Directories and hierarchy
    * Relative paths
    * Large file support
    * Multi-block caching
* A new built-in shell, `xsh`, to make use of all the above features.

Repo contents:
* `device/`: Hardware libaries
* `kernel/`: Kernel mode programs, including the kernel and file system
* `user/`: User mode programs such as `xsh`, `philosophers`, and `cat`

