#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static struct task_struct *thread_st;
static int thread_fn(void *unused)
{
	allow_signal(SIGKILL);
	while(!kthread_should_stop())
	{
		printk(KERN_INFO "Thread Running\n");
		ssleep(5);
		if (signal_pending(thread_st))
			break;
	}
	printk(KERN_INFO "Thread Stopping\n");
	do_exit(0);
	return 0;
}

static int __init mntr_init(void)
{
	printk(KERN_INFO "Creating Thread\n");
	//thread_st = kthread_create(thread_fn, NULL, "MBSmonitor");
	thread_st = kthread_run(thread_fn, NULL, "MBSmonitor");
	if (thread_st)
	{
		printk("Thread created successfully\n");
		//wake_up_process(thread_st);
	}
	else
		printk(KERN_INFO "Thread creation failed\n");
	return 0;
}

static void __exit mntr_exit(void)
{
	if (threat_st)
	{
		kthread_stop(thread_st);
		printk(KERN_INFO "Thread stopped\n");
	}
	printk("Cleaning Up\n");
}

module_init(mntr_init);
module_exit(mntr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yongseob LEE");
MODULE_DESCRIPTION("MBS Monitor");
