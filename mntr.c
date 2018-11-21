#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/swap.h>
#include <linux/cpu.h>
#include <linux/mutex.h> /*mbs_mntr*/
//https://github.com/diederikdehaas/rtl8812AU/issues/75
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif
//static struct task_struct *mntr_task;
DEFINE_MUTEX(mbs_mntr_mutex);
//struct mutex mbs_mntr_mutex;
//mutex_init(&mbs_mntr_mutex);
extern int mbs_mntrd(void *p);
/*
 * The background MBS monitoring daemon, started as a kernel thread
 * from module_init.
 *
 * This basically check out counters so that we can allocate _some_
 * free MBSs available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active MBS-allocators
 * (most normal use), this basically shouldn't matter.
 */
#if 0
static int mbs_mntrd_cpu_online(unsigned int cpu)
{
	int nid;

	for_each_node_state(nid, N_MEMORY) {
		pg_data_t *pgdat = NODE_DATA(nid);
		const struct cpumask *mask;

		mask = cpumask_of_node(pgdat->node_id);

		if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids)
			/* One of our CPUs online: restore mask */
			set_cpus_allowed_ptr(pgdat->mbs_mntrd, mask);
	}
	return 0;
}
#endif
void mbs_mntrd_stop(int nid)
{
	struct task_struct *mbs_mntrd = NODE_DATA(nid)->mbs_mntrd;
	int err;

	if (mbs_mntrd) {
		err = kthread_stop(mbs_mntrd);
		NODE_DATA(nid)->mbs_mntrd = NULL;
		if (err!=-EINTR)
			printk(KERN_INFO "mbs monitor stopped of node : %d\n",nid);
	}
}

static int mbs_mntrd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int ret = 0;

	if (pgdat->mbs_mntrd)
		return 0;

	pgdat->mbs_mntrd = kthread_run(mbs_mntrd, pgdat, "mbs_mntrd%d", nid);
	if (IS_ERR(pgdat->mbs_mntrd)) {
		//BUG_ON(system_state < SYSTEM_RUNNING);
		pr_err("Failed to start mbs_mntr on node %d\n", nid);
		ret = PTR_ERR(pgdat->mbs_mntrd);
		pgdat->mbs_mntrd = NULL;
	}
	return ret;
}

static int __init mbs_mntrd_init(void)
{
	int nid, ret;
	printk(KERN_INFO "Launching MBS Monitor\n");

	for_each_node_state(nid, N_MEMORY)
		mbs_mntrd_run(nid);
//	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
//			"mm/vmscan:online", mbs_mntrd_cpu_online,//"mm/vmscan1:online", mbs_mntrd_cpu_online,
//			NULL);
//	WARN_ON(ret < 0);
	return 0;
}

static void __exit mbs_mntrd_exit(void)
{
	int nid;
	
	for_each_node_state(nid, N_MEMORY)
		mbs_mntrd_stop(nid);
	printk("Cleaning Up\n");
}

module_init(mbs_mntrd_init);
module_exit(mbs_mntrd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yongseob LEE<Yongseob.Rhee@gmail.com>");
MODULE_DESCRIPTION("MBS Monitor");
