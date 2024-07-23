#ifndef _ASMARM_CURRENT_H
#define _ASMARM_CURRENT_H

#include <linux/thread_info.h>

static inline struct task_struct *get_current(void) __attribute_const__;

/*
* 获取当前正在运行的进程（也可以是线程）的 task_struct 结构体的指针。
* 即，它返回当前CPU上正在执行的进程的任务结构体指针;
*/
static inline struct task_struct *get_current(void)
{
	return current_thread_info()->task;
}

#define current (get_current())

#endif /* _ASMARM_CURRENT_H */
