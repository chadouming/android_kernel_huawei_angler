/*
 * Intelli Hotplug Driver
 *
 * Copyright (c) 2015, Chad Cormier Roussel <chadcormierroussel@gmail.com>
 * Copyright (c) 2013-2014, Paul Reioux <reioux@gmail.com>
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/kobject.h>

#include <linux/cpufreq.h>

#define SIMPLE_HOTPLUG		       "simple_hotplug"
#define SIMPLE_HOTPLUG_MAJOR_VERSION   1
#define SIMPLE_HOTPLUG_MINOR_VERSION   0

#define DEF_SAMPLING_MS                	1000
#define START_DELAY_MS                 	10000

static struct delayed_work simple_hotplug_work;
static struct work_struct up_down_work;
static struct workqueue_struct *simple_hotplug_wq;

/* Driver Controls */
static atomic_t simple_hotplug_active = ATOMIC_INIT(1);

/* Driver Tuning */
static unsigned int def_sampling_ms = DEF_SAMPLING_MS;

/* CPU Map*/
int cpu_map[8] = { 1, 0, 1, 0, 1, 0, 1, 0 };

static void __ref cpu_up_down_work(struct work_struct *work)
{
	int cpu = 0;

		for_each_online_cpu(cpu)
			if(cpu_map[cpu])
				continue;
			else
				cpu_down(cpu);

		for_each_cpu_not(cpu, cpu_online_mask)
			if(cpu_map[cpu])
                                cpu_up(cpu);
                        else
                                continue;
}

static void simple_hotplug_work_fn(struct work_struct *work)
{
	queue_work_on(0, simple_hotplug_wq, &up_down_work);

	if (atomic_read(&simple_hotplug_active) == 1)
		queue_delayed_work_on(0, simple_hotplug_wq, &simple_hotplug_work,
					msecs_to_jiffies(def_sampling_ms));
}

static int __ref simple_hotplug_start(void)
{
	int ret = 0;

	simple_hotplug_wq = alloc_workqueue("simple_hotplug", WQ_HIGHPRI | WQ_FREEZABLE, 0);
	if (!simple_hotplug_wq) {
		pr_err("%s: Failed to allocate hotplug workqueue\n",
		       SIMPLE_HOTPLUG);
		ret = -ENOMEM;
		goto err_out;
	}

	INIT_WORK(&up_down_work, cpu_up_down_work);
	INIT_DELAYED_WORK(&simple_hotplug_work, simple_hotplug_work_fn);

	queue_delayed_work_on(0, simple_hotplug_wq, &simple_hotplug_work,
			      START_DELAY_MS);

	return ret;
err_out:
	atomic_set(&simple_hotplug_active, 0);
	return ret;
}

static void simple_hotplug_stop(void)
{
	flush_workqueue(simple_hotplug_wq);
	cancel_work_sync(&up_down_work);
	cancel_delayed_work_sync(&simple_hotplug_work);
	destroy_workqueue(simple_hotplug_wq);
}

static void simple_hotplug_active_eval_fn(unsigned int status)
{
	int ret = 0;

	if (status == 1) {
		ret = simple_hotplug_start();
		if (ret)
			status = 0;
	} else
		simple_hotplug_stop();

	atomic_set(&simple_hotplug_active, status);
}

#define show_one(file_name, object)				\
static ssize_t show_##file_name					\
(struct kobject *kobj, struct kobj_attribute *attr, char *buf)	\
{								\
	return sprintf(buf, "%u\n", object);			\
}


show_one(def_sampling_ms, def_sampling_ms);

#define store_one(file_name, object)		\
static ssize_t store_##file_name		\
(struct kobject *kobj, 				\
 struct kobj_attribute *attr, 			\
 const char *buf, size_t count)			\
{						\
	unsigned int input;			\
	int ret;				\
	ret = sscanf(buf, "%u", &input);	\
	if (ret != 1 || input > 100)		\
		return -EINVAL;			\
	if (input == object) {			\
		return count;			\
	}					\
	object = input;				\
	return count;				\
}

store_one(def_sampling_ms, def_sampling_ms);

static ssize_t show_simple_hotplug_active(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n",
			atomic_read(&simple_hotplug_active));
}

static ssize_t store_simple_hotplug_active(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 const char *buf, size_t count)
{
	int ret;
	unsigned int input;

	ret = sscanf(buf, "%d", &input);
	if (ret < 0)
		return ret;

	if (input < 0)
		input = 0;
	else if (input > 0)
		input = 1;

	if (input == atomic_read(&simple_hotplug_active))
		return count;

	simple_hotplug_active_eval_fn(input);

	return count;
}

#define KERNEL_ATTR_RW(_name) \
static struct kobj_attribute _name##_attr = \
	__ATTR(_name, 0664, show_##_name, store_##_name)

KERNEL_ATTR_RW(simple_hotplug_active);
KERNEL_ATTR_RW(def_sampling_ms);

static struct attribute *simple_hotplug_attrs[] = {
	&simple_hotplug_active_attr.attr,
	&def_sampling_ms_attr.attr,
	NULL,
};

static struct attribute_group simple_hotplug_attr_group = {
	.attrs = simple_hotplug_attrs,
	.name = "simple_hotplug",
};

static int __init simple_hotplug_init(void)
{
	int rc;

	rc = sysfs_create_group(kernel_kobj, &simple_hotplug_attr_group);

	pr_info("SIMPLE_HOTPLUG: version %d.%d\n",
		 SIMPLE_HOTPLUG_MAJOR_VERSION,
		 SIMPLE_HOTPLUG_MINOR_VERSION);

	if (atomic_read(&simple_hotplug_active) == 1)
		simple_hotplug_start();
	return 0;
}

static void __exit simple_hotplug_exit(void)
{

	if (atomic_read(&simple_hotplug_active) == 1)
		simple_hotplug_stop();
	sysfs_remove_group(kernel_kobj, &simple_hotplug_attr_group);
}

arch_initcall(simple_hotplug_init);
module_exit(simple_hotplug_exit);

MODULE_AUTHOR("Chad Cormier Roussel <chadcormierroussel@gmail.com>");
MODULE_DESCRIPTION("A dumb cpu hotplug driver to keep desired "
		   "cores from a cpu online."
		   "Modified from Faux123 intelli-plug");
MODULE_LICENSE("GPLv2");
