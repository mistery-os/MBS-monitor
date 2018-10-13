#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/kthread.h>

static struct task_struct *thread_st;
static int thread_fn(void *unused)
{
	while(1)
	{
		printk(KERN_INFO "Thread Running\n");
		sleep(5);
	}
	printk(KERN_INFO "Thread Stopping\n");
	do_exit(0);
	return 0;
}

static int __init mntr_init(void)
{
	printk(KERN_INFO "Creating Thread\n");
	thread_st = kthread_create(thread_fn, NULL, "MBSmonitor");
	if (thread_st)
		printk("Thread created successfully\n");
	else
		printk(KERN_INFO "Thread creation failed\n");
	return 0;
}

static void __exit mntr_exit(void)
{
	printk("Cleaning Up\n");
}

module_init(mntr_init);
module_exit(mntr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yongseob LEE");
MODULE_DESCRIPTION("MBS Monitor");
