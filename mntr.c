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
static struct task_struct *mntr_task;
DEFINE_MUTEX(mbs_mntr_mutex);
//struct mutex mbs_mntr_mutex;
//mutex_init(&mbs_mntr_mutex);
void wakeup_mbs_mntrd(struct zone *zone, int order, enum zone_type classzone_idx)
{
	pg_data_t *pgdat;

	if (!managed_zone(zone))
		return;

	if (!cpuset_zone_allowed(zone, GFP_KERNEL | __GFP_HARDWALL))
		return;
	pgdat = zone->zone_pgdat;
	pgdat->mbs_mntrd_classzone_idx = mbs_mntrd_classzone_idx(pgdat,
							   classzone_idx);
	pgdat->mbs_mntrd_order = max(pgdat->mbs_mntrd_order, order);
	if (!waitqueue_active(&pgdat->mbs_mntrd_wait))
		return;

	/* Hopeless node, leave it to direct reclaim */
	if (pgdat->mbs_mntrd_failures >= MAX_RECLAIM_RETRIES)
		return;

	if (pram_pgdat_balanced(pgdat, order, classzone_idx))
		return;

	trace_mm_vmscan_wakeup_mbs_mntrd(pgdat->node_id, classzone_idx, order);
	wake_up_interruptible(&pgdat->mbs_mntrd_wait);
}
EXPORT_SYMBOL(wakeup_mbs_mntrd);
int calculate_pressure_mbs_threshold(struct zone *zone)
{
	int threshold;
	int watermark_distance;

	/*
	 * As vmstats are not up to date, there is drift between the estimated
	 * and real values. For high thresholds and a high number of CPUs, it
	 * is possible for the min watermark to be breached while the estimated
	 * value looks fine. The pressure threshold is a reduced value such
	 * that even the maximum amount of drift will not accidentally breach
	 * the min watermark
	 */
	watermark_distance = PRAM_ZONE_FAT(zone) - PRAM_ZONE_FULL(zone);
	threshold = max(1, (int)(watermark_distance / num_online_cpus()));

	/*
	 * Maximum threshold is 125
	 */
	threshold = min(125, threshold);

	return threshold;
}

int calculate_mbs_threshold(struct zone *zone)
{
	int threshold;
	int mem;	/* memory in 128 MB units */

	/*
	 * The threshold scales with the number of processors and the amount
	 * of memory per zone. More memory means that we can defer updates for
	 * longer, more processors could lead to more contention.
 	 * fls() is used to have a cheap way of logarithmic scaling.
	 *
	 * Some sample thresholds:
	 *
	 * Threshold	Processors	(fls)	Zonesize	fls(mem+1)
	 * ------------------------------------------------------------------
	 * 8		1		1	0.9-1 GB	4
	 * 16		2		2	0.9-1 GB	4
	 * 20 		2		2	1-2 GB		5
	 * 24		2		2	2-4 GB		6
	 * 28		2		2	4-8 GB		7
	 * 32		2		2	8-16 GB		8
	 * 4		2		2	<128M		1
	 * 30		4		3	2-4 GB		5
	 * 48		4		3	8-16 GB		8
	 * 32		8		4	1-2 GB		4
	 * 32		8		4	0.9-1GB		4
	 * 10		16		5	<128M		1
	 * 40		16		5	900M		4
	 * 70		64		7	2-4 GB		5
	 * 84		64		7	4-8 GB		6
	 * 108		512		9	4-8 GB		6
	 * 125		1024		10	8-16 GB		8
	 * 125		1024		10	16-32 GB	9
	 */

	mem = zone->managed_pages >> (27 - PAGE_SHIFT);

	threshold = 2 * fls(num_online_cpus()) * (1 + fls(mem));

	/*
	 * Maximum threshold is 125
	 */
	threshold = min(125, threshold);

	return threshold;
}
bool __pram_zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int classzone_idx, unsigned int alloc_flags,
			 long free_pages)
{
	long min = mark;
	int o;
	//const bool alloc_harder = (alloc_flags & (ALLOC_HARDER|ALLOC_OOM));
	const bool alloc_harder = false;//PRAM need not to swap,...

	/* free_pages may go negative - that's OK */
	free_pages -= (1 << order) - 1;
#if 0
	if (alloc_flags & ALLOC_HIGH) //never happen on ZONE_PRAM
		min -= min / 2;
#endif
	/*
	 * If the caller does not have rights to ALLOC_HARDER then subtract
	 * the high-atomic reserves. This will over-estimate the size of the
	 * atomic reserve but it avoids a search.
	 */
	if (likely(!alloc_harder)) {
//		free_pages -= z->nr_reserved_highatomic;
	} else {
#if 0
		/*
		 * OOM victims can try even harder than normal ALLOC_HARDER
		 * users on the grounds that it's definitely going to be in
		 * the exit path shortly and free memory. Any allocation it
		 * makes during the free path will be small and short-lived.
		 */
		if (alloc_flags & ALLOC_OOM)
			min -= min / 2;
		else
			min -= min / 4;
#endif
	}
#if 0
#ifdef CONFIG_CMA
	/* If allocation can't use CMA areas don't use free CMA pages */
	if (!(alloc_flags & ALLOC_CMA))
		free_pages -= zone_page_state(z, NR_FREE_CMA_PAGES);
#endif
#endif
	/*
	 * Check watermarks for an order-0 allocation request. If these
	 * are not met, then a high-order request also cannot go ahead
	 * even if a suitable page happened to be free.
	 */
#if 0
	if (free_pages <= min + z->lowmem_reserve[classzone_idx])
		return false;

	/* If this is an order-0 request then the watermark is fine */
	if (!order)
		return true;
#endif
	/* For a high-order request, check at least one suitable page is free */
	for (o = order; o < MAX_ORDER; o++) {
		struct free_area *area = &z->free_area[o];
		int mt;

		if (!area->nr_free)
			continue;

		for (mt = 0; mt < MIGRATE_PCPTYPES; mt++) {
			if (!list_empty(&area->free_list[mt]))
				return true;
		}
#if 0
#ifdef CONFIG_CMA
		if ((alloc_flags & ALLOC_CMA) &&
		    !list_empty(&area->free_list[MIGRATE_CMA])) {
			return true;
		}
#endif
		if (alloc_harder &&
			!list_empty(&area->free_list[MIGRATE_HIGHATOMIC]))
			return true;
#endif
	}
	return false;
}
bool pram_zone_watermark_ok_safe(struct zone *z, unsigned int order,
			unsigned long mark, int classzone_idx)
{
	long free_pages = zone_page_state(z, NR_FREE_PRAMS);

	if (z->percpu_drift_mark && free_pages < z->percpu_drift_mark)
		free_pages = zone_page_state_snapshot(z, NR_FREE_PRAMS);

	return __pram_zone_watermark_ok(z, order, mark, classzone_idx, 0,
								free_pages);
}
static bool pram_pgdat_balanced(pg_data_t *pgdat, int order, int classzone_idx)
{
	int i;
	unsigned long mark = -1;
	struct zone *zone;
	int nid;

//	for (i = 0; i <= classzone_idx; i++) {
		//zone = pgdat->node_zones + i;
		zone = pgdat->node_zones + ZONE_PRAM;
//		if (!managed_zone(zone))
//			continue;

		nid=zone_to_nid(zone);//pgdat->node_id;
		mark = PRAM_ZONE_FAT(zone);//PRAM_ZONE_SLIM(zone);
		if (pram_zone_watermark_ok_safe(zone, order, mark, classzone_idx))
		{
			add_candidate_nodes(nid);
		        pram_local_policy(nid);
			return true;
		}
//	}

	/*
	 * If a node has no populated zone within classzone_idx, it does not
	 * need balancing by definition. This can happen if a zone-restricted
	 * allocation tries to wake a remote kswapd.
	 */
	if (mark == -1)
		return true;

	return false;
}
static bool prepare_mntrd_sleep(pg_data_t *pgdat, int order, int classzone_idx)
{
	/*
	 * The throttled processes are normally woken up in balance_pgdat() as
	 * soon as allow_direct_reclaim() is true. But there is a potential
	 * race between when mbs_mntrd checks the watermarks and a process gets
	 * throttled. There is also a potential race if processes get
	 * throttled, mbs_mntrd wakes, a large process exits thereby balancing the
	 * zones, which causes mbs_mntrd to exit balance_pgdat() before reaching
	 * the wake up checks. If mbs_mntrd is going to sleep, no process should
	 * be sleeping on pfmemalloc_wait, so wake them now if necessary. If
	 * the wake up is premature, processes will wake mbs_mntrd and get
	 * throttled again. The difference from wake ups in pram_balance_pgdat() is
	 * that here we are under prepare_to_wait().
	 */
//	if (waitqueue_active(&pgdat->pfmemalloc_wait))
//		wake_up_all(&pgdat->pfmemalloc_wait);

	/* Hopeless node, leave it to direct reclaim */
//	if (pgdat->mbs_mntrd_failures >= MAX_RECLAIM_RETRIES)
//		return true;

	if (pram_pgdat_balanced(pgdat, order, classzone_idx)) {
//		clear_pgdat_congested(pgdat);
		return true;
	}

	return false;
}
void set_pgdat_percpu_threshold(pg_data_t *pgdat,
				int (*calculate_pressure)(struct zone *))
{
	struct zone *zone;
	int cpu;
	int threshold;
	int i;

//	for (i = 0; i < pgdat->nr_zones; i++) {
		//zone = &pgdat->node_zones[i];
		zone = &pgdat->node_zones[ZONE_PRAM];
		if (!zone->percpu_drift_mark)
			continue;

		threshold = (*calculate_pressure)(zone);
		for_each_online_cpu(cpu)
			per_cpu_ptr(zone->pageset, cpu)->stat_threshold
							= threshold;
//	}
}
static void mbs_mntrd_try_to_sleep(pg_data_t *pgdat, int alloc_order, int reclaim_order,
				unsigned int classzone_idx)
{
	long remaining = 0;
	DEFINE_WAIT(wait);
	//if (freezing(current) || kthread_should_stop())
	if ( kthread_should_stop())
		return;

	prepare_to_wait(&pgdat->mntrd_wait, &wait, TASK_INTERRUPTIBLE);

	/*
	 * Try to sleep for a short interval. Note that kcompactd will only be
	 * woken if it is possible to sleep for a short interval. This is
	 * deliberate on the assumption that if reclaim cannot keep an
	 * eligible zone balanced that it's also unlikely that compaction will
	 * succeed.
	 */
	if (prepare_mntrd_sleep(pgdat, reclaim_order, classzone_idx)) {
		/*
		 * Compaction records what page blocks it recently failed to
		 * isolate pages from and skips them in the future scanning.
		 * When mbs_mntrd is going to sleep, it is reasonable to assume
		 * that pages and compaction may succeed so reset the cache.
		 */
		//reset_isolation_suitable(pgdat);

		/*
		 * We have freed the memory, now we should compact it to make
		 * allocation of the requested order possible.
		 */
		//wakeup_kcompactd(pgdat, alloc_order, classzone_idx);

		remaining = schedule_timeout(HZ/10);

		/*
		 * If woken prematurely then reset mbs_mntrd_classzone_idx and
		 * order. The values will either be from a wakeup request or
		 * the previous request that slept prematurely.
		 */
		if (remaining) {
			pgdat->mntrd_classzone_idx = ZONE_PRAM; 
			pgdat->mntrd_order = max(pgdat->mntrd_order, reclaim_order);
		}

		finish_wait(&pgdat->mntrd_wait, &wait);
		prepare_to_wait(&pgdat->mntrd_wait, &wait, TASK_INTERRUPTIBLE);
	}

	/*
	 * After a short sleep, check if it was a premature sleep. If not, then
	 * go fully to sleep until explicitly woken up.
	 */
	if (!remaining &&
	    prepare_mntrd_sleep(pgdat, reclaim_order, classzone_idx)) {
//		trace_mm_vmscan_mbs_mntrd_sleep(pgdat->node_id);

		/*
		 * vmstat counters are not perfectly accurate and the estimated
		 * value for counters such as NR_FREE_PAGES can deviate from the
		 * true value by nr_online_cpus * threshold. To avoid the zone
		 * watermarks being breached while under pressure, we reduce the
		 * per-cpu vmstat threshold while mbs_mntrd is awake and restore
		 * them before going back to sleep.
		 */
		set_pgdat_percpu_threshold(pgdat, calculate_mbs_threshold);

		if (!kthread_should_stop())
			schedule();

		set_pgdat_percpu_threshold(pgdat, calculate_pressure_mbs_threshold);
	} else {
		if (remaining)
			//count_vm_event(KSWAPD_LOW_WMARK_HIT_QUICKLY);
		else
			//count_vm_event(KSWAPD_HIGH_WMARK_HIT_QUICKLY);
	}
	finish_wait(&pgdat->mntrd_wait, &wait);
}
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
static int mbs_mntrd(void *p)
{
	allow_signal(SIGKILL|SIGSTOP);
/*
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
#if 0
	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);
	current->mbs_mntr_state = & mbs_mntr_state;
#endif
	tsk->flags |= PF_MBS_MNTRD;
//	set_freezable();

	pgdat->mntrd_order = 0;
	pgdat->mntrd_classzone_idx = MAX_NR_ZONES;

	for ( ; ; ){
		bool ret;

		alloc_order = reclaim_order = pgdat->mntrd_order;
		classzone_idx = ZONE_PRAM;

mbs_mntrd_try_sleep:
		mbs_mntrd_try_to_sleep(pgdat, alloc_order, reclaim_order,
					classzone_idx);

		/* Read the new order and classzone_idx */
		alloc_order = reclaim_order = pgdat->mntrd_order;
		classzone_idx = ZONE_PRAM;
		pgdat->mntrd_order = 0;
		pgdat->mntrd_classzone_idx = ZONE_PRAM;

		//ret = try_to_freeze();
		if (kthread_should_stop())
			break;
		//if (ret)
		//	continue;
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
		//BUG_ON(system_state < SYSTEM_RUNNING);
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
	int err;

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
