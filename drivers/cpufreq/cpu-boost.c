/*
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "cpu-boost: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/time.h>

#define IB_FREQ_MIN 1113600
#define IB_FREQ_MAX 1113600
#define IB_DURATION 150
#define MDSS_TIMEOUT 5000

struct cpu_sync {
	int cpu;
	unsigned int input_boost_min;
};

extern bool st_cpu_bias;

static DEFINE_PER_CPU(struct cpu_sync, sync_info);
static struct workqueue_struct *cpu_boost_wq;
static struct work_struct input_boost_work;
static struct delayed_work input_boost_rem;

static bool max_boost_active = false;
unsigned long last_input_time;

/*
 * The CPUFREQ_ADJUST notifier is used to override the current policy min to
 * make sure policy min >= boost_min. The cpufreq framework then does the job
 * of enforcing the new policy.
 *
 * The sync kthread needs to run on the CPU in question to avoid deadlocks in
 * the wake up code. Achieve this by binding the thread to the respective
 * CPU. But a CPU going offline unbinds threads from that CPU. So, set it up
 * again each time the CPU comes back up. We can use CPUFREQ_START to figure
 * out a CPU is coming online instead of registering for hotplug notifiers.
 */
static int boost_adjust_notify(struct notifier_block *nb, unsigned long val,
				void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned int cpu = policy->cpu;
	struct cpu_sync *s = &per_cpu(sync_info, cpu);
	unsigned int ib_min = s->input_boost_min;

	switch (val) {
	case CPUFREQ_ADJUST:
		if (!ib_min)
			break;
		ib_min = min((s->input_boost_min == UINT_MAX ?
				policy->max : s->input_boost_min), policy->max);

		pr_debug("CPU%u policy min before boost: %u kHz\n",
			 cpu, policy->min);
		pr_debug("CPU%u boost min: %u kHz\n", cpu, ib_min);

		cpufreq_verify_within_limits(policy, ib_min, UINT_MAX);

		pr_debug("CPU%u policy min after boost: %u kHz\n",
			 cpu, policy->min);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block boost_adjust_nb = {
	.notifier_call = boost_adjust_notify,
};

static void update_policy_online(void)
{
	unsigned int i;

	/* Re-evaluate policy to trigger adjust notifier for online CPUs */
	get_online_cpus();
	for_each_online_cpu(i) {
		pr_debug("Updating policy for CPU%d\n", i);
		cpufreq_update_policy(i);
	}
	put_online_cpus();
}

static void do_input_boost_rem(struct work_struct *work)
{
	unsigned int i;
	struct cpu_sync *i_sync_info;

	if (st_cpu_bias)
		st_cpu_bias = false;

	/* Reset the input_boost_min for all CPUs in the system */
	pr_debug("Resetting input boost min for all CPUs\n");
	for_each_possible_cpu(i) {
		i_sync_info = &per_cpu(sync_info, i);
		i_sync_info->input_boost_min = 0;
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	if (max_boost_active) {
		max_boost_active = false;
	}
}

static void do_input_boost(struct work_struct *work)
{
	unsigned int i;
	struct cpu_sync *i_sync_info;

	if (max_boost_active) {
		return;
	}

	cancel_delayed_work_sync(&input_boost_rem);

	/* Set the input_boost_min for all CPUs in the system */
	pr_debug("Setting input boost min for all CPUs\n");
	for_each_possible_cpu(i) {
		i_sync_info = &per_cpu(sync_info, i);

		if (i < 4)
			i_sync_info->input_boost_min = IB_FREQ_MIN;
		else
			i_sync_info->input_boost_min = IB_FREQ_MAX;
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	queue_delayed_work(system_power_efficient_wq, &input_boost_rem,
					msecs_to_jiffies(IB_DURATION));
}

void mdss_boost_kick(void)
{
	if (work_pending(&input_boost_work)
				|| time_after(jiffies, last_input_time + msecs_to_jiffies(MDSS_TIMEOUT))) {
		return;
	}

	if (!st_cpu_bias)
		st_cpu_bias = true;

	queue_work(cpu_boost_wq, &input_boost_work);
}

static void do_input_boost_max(unsigned int duration_ms)
{
	unsigned int i;
	struct cpu_sync *i_sync_info;
	cancel_delayed_work_sync(&input_boost_rem);

	for_each_possible_cpu(i) {
		i_sync_info = &per_cpu(sync_info, i);
		i_sync_info->input_boost_min = UINT_MAX;
	}

	update_policy_online();

	queue_delayed_work(system_power_efficient_wq,
		&input_boost_rem, msecs_to_jiffies(duration_ms));

	max_boost_active = true;
}

void input_boost_max_kick(unsigned int duration_ms) 
{
	if (!st_cpu_bias)
		st_cpu_bias = true;

	do_input_boost_max(duration_ms);
}

static void cpuboost_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	last_input_time = jiffies;

	if (work_pending(&input_boost_work))
		return;

	if (!st_cpu_bias)
		st_cpu_bias = true;

	queue_work(cpu_boost_wq, &input_boost_work);
}

static int cpuboost_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void cpuboost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpuboost_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler cpuboost_input_handler = {
	.event          = cpuboost_input_event,
	.connect        = cpuboost_input_connect,
	.disconnect     = cpuboost_input_disconnect,
	.name           = "cpu-boost",
	.id_table       = cpuboost_ids,
};

static int cpu_boost_init(void)
{
	int cpu, ret;
	struct cpu_sync *s;

	cpu_boost_wq = alloc_workqueue("cpuboost_wq", WQ_HIGHPRI, 0);
	if (!cpu_boost_wq)
		return -EFAULT;

	INIT_WORK(&input_boost_work, do_input_boost);
	INIT_DELAYED_WORK(&input_boost_rem, do_input_boost_rem);

	for_each_possible_cpu(cpu) {
		s = &per_cpu(sync_info, cpu);
		s->cpu = cpu;
	}
	cpufreq_register_notifier(&boost_adjust_nb, CPUFREQ_POLICY_NOTIFIER);
	ret = input_register_handler(&cpuboost_input_handler);

	return ret;
}
late_initcall(cpu_boost_init);
