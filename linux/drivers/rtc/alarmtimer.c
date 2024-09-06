/* drivers/rtc/alarmtimer.c
 *
 * Alarmtimer interface
 *
 * This interface provides a timer which is similarto hrtimers,
 * but triggers a RTC alarm if the box is suspend.
 *
 * This interface is influenced by the Android RTC Alarm timer
 * interface.
 * Copyright (C) 2007-2009 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/mach/time.h>
#include <linux/alarmtimer.h>
#include <linux/timerqueue.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sysdev.h>

#define ALARM_PRINT_ERROR (1U << 0)
#define ALARM_PRINT_INIT_STATUS (1U << 1)
#define ALARM_PRINT_TSET (1U << 2)
#define ALARM_PRINT_CALL (1U << 3)
#define ALARM_PRINT_SUSPEND (1U << 4)
#define ALARM_PRINT_INT (1U << 5)
#define ALARM_PRINT_FLOW (1U << 6)

static int debug_mask = ALARM_PRINT_INIT_STATUS | ALARM_PRINT_ERROR;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define pr_alarm(debug_level_mask, args...) \
	do { \
		if (debug_mask & ALARM_PRINT_##debug_level_mask) { \
			pr_info(args); \
		} \
	} while (0)

/**
 * struct alarm_base - Alarm timer bases
 * @lock: Lock for syncrhonized access to the base
 * @timerqueue: Timerqueue head managing the list of events
 * @timer: hrtimer used to schedule events while running
 * @gettime: Function to read the time correlating to the base
 * @base_clockid: clockid for the base
 */
static struct alarm_base {
	spinlock_t		lock;
	struct timerqueue_head	timerqueue;
	ktime_t			(*gettime)(void);
	clockid_t		base_clockid;
} alarm_bases[ALARM_NUMTYPE];

static struct rtc_device *alarm_rtc_dev;
static DEFINE_MUTEX(alarm_setrtc_mutex);
static struct platform_device *alarm_platform_dev;
static struct wakeup_source *ws;

/**
 * alarmtimer_enqueue - Adds an alarm timer to an alarm_base timerqueue
 * @base: pointer to the base where the timer is being run
 * @alarm: pointer to alarm being enqueued.
 *
 * Adds alarm to a alarm_base timerqueue
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_enqueue(struct alarm_base *base, struct alarm *alarm)
{
	if (alarm->state & ALARMTIMER_STATE_ENQUEUED)
		timerqueue_del(&base->timerqueue, &alarm->node);

	timerqueue_add(&base->timerqueue, &alarm->node);
	alarm->state |= ALARMTIMER_STATE_ENQUEUED;
}

/**
 * alarmtimer_dequeue - Removes an alarm timer from an alarm_base timerqueue
 * @base: pointer to the base where the timer is running
 * @alarm: pointer to alarm being removed
 *
 * Removes alarm to a alarm_base timerqueue
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_dequeue(struct alarm_base *base, struct alarm *alarm)
{
	if (!(alarm->state & ALARMTIMER_STATE_ENQUEUED))
		return;

	timerqueue_del(&base->timerqueue, &alarm->node);
	alarm->state &= ~ALARMTIMER_STATE_ENQUEUED;
}

/**
 * alarmtimer_fired - Handles alarm hrtimer being fired.
 * @timer: pointer to hrtimer being run
 *
 * When a alarm timer fires, this runs through the timerqueue to
 * see which alarms expired, and runs those. If there are more alarm
 * timers queued for the future, we set the hrtimer to fire when
 * when the next future alarm timer expires.
 */
static enum hrtimer_restart alarmtimer_fired(struct hrtimer *timer)
{
	struct alarm *alarm = container_of(timer, struct alarm, timer);
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret = HRTIMER_NORESTART;
	int restart = ALARMTIMER_NORESTART;

	spin_lock_irqsave(&base->lock, flags);
	alarmtimer_dequeue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);

	if (alarm->function)
		restart = alarm->function(alarm, base->gettime());

	spin_lock_irqsave(&base->lock, flags);
	if (restart != ALARMTIMER_NORESTART) {
		hrtimer_set_expires(&alarm->timer, alarm->node.expires);
		alarmtimer_enqueue(base, alarm);
		ret = HRTIMER_RESTART;
	}
	spin_unlock_irqrestore(&base->lock, flags);
	__pm_wakeup_event(ws, MSEC_PER_SEC);
	return ret;

}

/**
 * alarm_init - Initialize an alarm structure
 * @alarm: ptr to alarm to be initialized
 * @type: the type of the alarm
 * @function: callback that is run when the alarm fires
 */
void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t))
{
	timerqueue_init(&alarm->node);
	hrtimer_init(&alarm->timer, alarm_bases[type].base_clockid,
		     HRTIMER_MODE_ABS);
	alarm->timer.function = alarmtimer_fired;
	alarm->function = function;
	alarm->type = type;
	alarm->state = ALARMTIMER_STATE_INACTIVE;
}


/**
 * alarm_start - Sets an absolute alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time to run the alarm
 */
int alarm_start(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&base->lock, flags);
	alarm->node.expires = start;
	alarmtimer_enqueue(base, alarm);
	ret = hrtimer_start(&alarm->timer, alarm->node.expires,
			    HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&base->lock, flags);
	return ret;
}

/**
 * alarm_start_relative - Sets a relative alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time relative to now to run the alarm
 */
int alarm_start_relative(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	start = ktime_add(start, base->gettime());
	return alarm_start(alarm, start);
}

/**
 * alarm_try_to_cancel - Tries to cancel an alarm timer
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not running,
 * and -1 if the callback was running
 */
int alarm_try_to_cancel(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&base->lock, flags);
	ret = hrtimer_try_to_cancel(&alarm->timer);
	if (ret >= 0)
		alarmtimer_dequeue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);
	return ret;
}

/**
 * alarm_cancel - cancel an alarm and wait for the handler to finish.
 * @alarm:	the alarm to be cancelled
 *
 * Returns:
 *  0 when the alarm was not active
 *  1 when the alarm was active
 */
int alarm_cancel(struct alarm *alarm)
{
	for (;;) {
		int ret = alarm_try_to_cancel(alarm);
		if (ret >= 0)
			return ret;
		cpu_relax();
	}
}

/**
 * alarm_restart - Restart the alarmtimer from the start.
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not active.
 */
void alarm_restart(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	hrtimer_set_expires(&alarm->timer, alarm->node.expires);
	hrtimer_restart(&alarm->timer);
	alarmtimer_enqueue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);
}

static void alarm_triggered_func(void *p)
{
	struct rtc_device *rtc = alarm_rtc_dev;
	if (!(rtc->irq_data & RTC_AF))
		return;
	__pm_wakeup_event(ws, MSEC_PER_SEC);
	pr_alarm(INT, "rtc alarm triggered\n");
}

u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval)
{
	u64 overrun = 1;
	ktime_t delta;

	delta = ktime_sub(now, alarm->node.expires);

	if (delta.tv64 < 0)
		return 0;

	if (unlikely(delta.tv64 >= interval.tv64)) {
		s64 incr = ktime_to_ns(interval);

		overrun = ktime_divns(delta, incr);

		alarm->node.expires = ktime_add_ns(alarm->node.expires,
							incr*overrun);

		if (alarm->node.expires.tv64 > now.tv64)
			return overrun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		overrun++;
	}

	alarm->node.expires = ktime_add(alarm->node.expires, interval);
	return overrun;
}

/**
 * alarm_forward_now - Increment timer expire time by interval value w.r.t
 * current time.
 * @alarm: ptr to alarm to be canceled
 *
 * Returns number of overrun or missed alarms.
 */
u64 alarm_forward_now(struct alarm *alarm, ktime_t interval)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	return alarm_forward(alarm, base->gettime(), interval);
}

/**
 * alarm_expires_remaining - time difference between expire time and current
 * time
 * @alarm: ptr to alarm to be expire remaining time
 *
 * Returns remaining time in ktime_t
 */
ktime_t alarm_expires_remaining(const struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	return ktime_sub(alarm->node.expires, base->gettime());
}

static int alarm_suspend(struct platform_device *pdev, pm_message_t state)
{
	ktime_t min = ktime_set(0, 0);
	unsigned long flags;
	int i;
	struct timeval tv;
	unsigned long now;
	struct rtc_wkalrm alm;
	int status;

	/* Find the soonest timer to expire*/
	for (i = 0; i < ALARM_NUMTYPE; i++) {
		struct alarm_base *base = &alarm_bases[i];
		struct timerqueue_node *next;
		ktime_t delta;

		spin_lock_irqsave(&base->lock, flags);
		next = timerqueue_getnext(&base->timerqueue);
		spin_unlock_irqrestore(&base->lock, flags);
		if (!next)
			continue;
		delta = ktime_sub(next->expires, base->gettime());
		if (!min.tv64 || (delta.tv64 < min.tv64)) {
			min = delta;
		}
	}
	/*
	 * !isMin means no alarm is needs to be triggered after sleep, hence
	 * no need to start rtc timer.
	 */
	if  (min.tv64 == 0) {
		return 0;
	}

	/* Don't go in sleep mode if the alarm wakes up before 2secs. */
	if (ktime_to_ns(min) < 2 * NSEC_PER_SEC) {
		__pm_wakeup_event(ws, 2 * MSEC_PER_SEC);
		return -EBUSY;
	}

	/* Setup an rtc timer to fire that far in the future */
	tv = ktime_to_timeval(min);
	rtc_read_time(alarm_rtc_dev, &alm.time);
	rtc_tm_to_time(&alm.time, &now);
	memset(&alm, 0, sizeof alm);
	rtc_time_to_tm(now + tv.tv_sec, &alm.time);
	alm.enabled = true;
	/* Set alarm, if in the past reject suspend briefly to handle */
	status = rtc_set_alarm(alarm_rtc_dev, &alm);
	if (status < 0)
		__pm_wakeup_event(ws, MSEC_PER_SEC);
	return status;
}

static int alarm_resume(struct platform_device *pdev)
{
	struct rtc_wkalrm alarm;

	pr_alarm(SUSPEND, "alarm_resume(%p)\n", pdev);

	/* Disable rtc alarm */
	memset(&alarm, 0, sizeof(alarm));
	alarm.enabled = 0;
	rtc_set_alarm(alarm_rtc_dev, &alarm);

	return 0;
}

static struct rtc_task alarm_rtc_task = {
	.func = alarm_triggered_func
};

static int rtc_alarm_add_device(struct device *dev,
				struct class_interface *class_intf)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(dev);

	mutex_lock(&alarm_setrtc_mutex);

	if (alarm_rtc_dev) {
		err = -EBUSY;
		goto err1;
	}

	alarm_platform_dev =
		platform_device_register_simple("alarm", -1, NULL, 0);
	if (IS_ERR(alarm_platform_dev)) {
		err = PTR_ERR(alarm_platform_dev);
		goto err1;
	}
	err = rtc_irq_register(rtc, &alarm_rtc_task);
	if (err)
		goto err2;
	alarm_rtc_dev = rtc;
	pr_alarm(INIT_STATUS, "using rtc device, %s, for alarms", rtc->name);
	mutex_unlock(&alarm_setrtc_mutex);

	return 0;

err2:
	platform_device_unregister(alarm_platform_dev);
err1:
	mutex_unlock(&alarm_setrtc_mutex);
	return err;
}

static void rtc_alarm_remove_device(struct device *dev,
				    struct class_interface *class_intf)
{
	if (dev == &alarm_rtc_dev->dev) {
		pr_alarm(INIT_STATUS, "lost rtc device for alarms");
		rtc_irq_unregister(alarm_rtc_dev, &alarm_rtc_task);
		platform_device_unregister(alarm_platform_dev);
		alarm_rtc_dev = NULL;
	}
}

static struct class_interface rtc_alarm_interface = {
	.add_dev = &rtc_alarm_add_device,
	.remove_dev = &rtc_alarm_remove_device,
};

static struct platform_driver alarm_driver = {
	.suspend = alarm_suspend,
	.resume = alarm_resume,
	.driver = {
		.name = "alarm"
	}
};

static int __init alarm_driver_init(void)
{
	int err;
	int i;

	/* Initialize alarm bases */
	alarm_bases[ALARM_REALTIME].base_clockid = CLOCK_REALTIME;
	alarm_bases[ALARM_REALTIME].gettime = &ktime_get_real;
	alarm_bases[ALARM_BOOTTIME].base_clockid = CLOCK_BOOTTIME;
	alarm_bases[ALARM_BOOTTIME].gettime = &ktime_get_boottime;
	for (i = 0; i < ALARM_NUMTYPE; i++) {
		timerqueue_init_head(&alarm_bases[i].timerqueue);
		spin_lock_init(&alarm_bases[i].lock);
	}

	err = platform_driver_register(&alarm_driver);
	if (err < 0)
		goto err1;
	rtc_alarm_interface.class = rtc_class;
	err = class_interface_register(&rtc_alarm_interface);
	if (err < 0)
		goto err2;

	ws = wakeup_source_register("alarmtimer");
	return 0;

err2:
	platform_driver_unregister(&alarm_driver);
err1:
	return err;
}

static void  __exit alarm_exit(void)
{
	class_interface_unregister(&rtc_alarm_interface);
	platform_driver_unregister(&alarm_driver);
}

module_init(alarm_driver_init);
module_exit(alarm_exit);

