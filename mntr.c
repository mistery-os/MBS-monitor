#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/swap.h>
//https://github.com/diederikdehaas/rtl8812AU/issues/75
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif
static struct task_struct *mntr_task;
static int mbs_mntrd(void *p)
{
/*
	allow_signal(SIGKILL|SIGSTOP);
	while(!kthread_should_stop())
	{
		printk(KERN_INFO "Thread Running\n");
		ssleep(5);
		if (signal_pending(mntr_task))
			break;
	}
	printk(KERN_INFO "Thread Stopping\n");
	do_exit(0);
	return 0;
*/
	unsigned int alloc_order, reclaim_order;
	unsigned int classzone_idx = ZONE_PRAM;
	pg_data_t *pgdat = (pg_data_t*)p;
	struct task_struct *tsk = current;

	struct mbs_mntr_state mbs_mntr_state = {
		.mbs_mntr_k = 0,
	};

	int nid = pgdat->node_id;
	int i, zid = ZONE_PRAM;
	struct zone *zone = pgdat->node_zones + zid;


	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);
	current->mbs_mntr_state = & mbs_mntr_state;


	tsk->flags |= PF_MBS_MNTRD;

	for ( ; ; ){
		if (kthread_should_stop())
			break;

	}

	tsk->flags &= ~(PF_MBS_MNTRD);
	current->mbs_mntr_state = NULL;

	return 0;

}

static int mbs_mntrd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int ret = 0;

	if (pgdat->mbs_mntrd)
		return 0;

	pgdat->mbs_mntrd = kthread_run(mbs_mntrd, pgdat, "mbs_mntrd%d", nid);
	if (IS_ERR(pgdat->mbs_mntrd)) {
		/* failure at boot is fatal */
		BUG_ON(system_state < SYSTEM_RUNNING);
		pr_err("Failed to start mbs_mntr on node %d\n", nid);
		ret = PTR_ERR(pgdat->mbs_mntrd);
		pgdat->mbs_mntrd = NULL;
	}
	return ret;

}
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

void mbs_mntrd_stop(int nid)
{
	struct task_struct *mbs_mntrd = NODE_DATA(nid)->mbs_mntrd;

	if (mbs_mntrd) {
		err = kthread_stop(mbs_mntrd);
		NODE_DATA(nid)->mbs_mntrd = NULL;
		if (err!=-EINTR)
			printk(KERN_INFO "mbs monitor stopped of node : %d\n",nid);
	}
}

static int __init mbs_mntrd_init(void)
{
	int nid, ret;
	printk(KERN_INFO "Launching MBS Monitor\n");

	for_each_node_state(nid, N_MEMORY)
		mbs_mntrd_run(nid);
	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
			"mm/vmscan1:online", mbs_mntrd_cpu_online,
			NULL);
	WARN_ON(ret < 0);
	return 0;
}

static void __exit mbs_mntrd_exit(void)
{
	for_each_node_state(nid, N_MEMORY)
		mbs_mntrd_stop(nid);
	printk("Cleaning Up\n");
}

module_init(mbs_mntrd_init);
module_exit(mbs_mntrd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yongseob LEE");
MODULE_DESCRIPTION("MBS Monitor");
