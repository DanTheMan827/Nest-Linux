/* include/linux/alarmtimer.h
 *
 * Copyright (C) 2006-2007 Google, Inc.
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

#ifndef _LINUX_ALARMTIMER_H
#define _LINUX_ALARMTIMER_H

#include <linux/ioctl.h>
#include <linux/time.h>
#include <linux/timerqueue.h>

enum alarmtimer_type {
	ALARM_REALTIME,
	ALARM_BOOTTIME,

	ALARM_NUMTYPE,
};

#define ALARMTIMER_STATE_INACTIVE	0x00
#define ALARMTIMER_STATE_ENQUEUED	0x01

enum alarmtimer_restart {
	ALARMTIMER_NORESTART,
	ALARMTIMER_RESTART,
};

#ifdef __KERNEL__

#include <linux/ktime.h>
#include <linux/rbtree.h>
#include <linux/hrtimer.h>

/*
 * The alarm interface is similar to the hrtimer interface but adds support
 * for wakeup from suspend. It also adds an elapsed realtime clock that can
 * be used for periodic timers that need to keep runing while the system is
 * suspended and not be disrupted when the wall time is set.
 */

/**
 * struct alarm - Alarm timer structure
 * @node:	timerqueue node for adding to the event list this value
 *		also includes the expiration time.
 * @period:	Period for recuring alarms
 * @function:	Function pointer to be executed when the timer fires.
 * @type:	Alarm type (BOOTTIME/REALTIME)
 * @enabled:	Flag that represents if the alarm is set to fire or not
 * @data:	Internal data value.
 */
struct alarm {
	struct timerqueue_node	node;
	struct hrtimer		timer;
	enum alarmtimer_restart	(*function)(struct alarm *, ktime_t now);
	enum alarmtimer_type	type;
	int			state;
	void			*data;
};

/**
 * alarm_init - Initialize an alarm structure
 * @alarm: ptr to alarm to be initialized
 * @type: the type of the alarm
 * @function: callback that is run when the alarm fires
 */
void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t));

/**
 * alarm_start - Sets an absolute alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time to run the alarm
 */
int alarm_start(struct alarm *alarm, ktime_t start);

/**
 * alarm_start_relative - Sets a relative alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time relative to now to run the alarm
 */
int alarm_start_relative(struct alarm *alarm, ktime_t start);

/**
 * alarm_try_to_cancel - Tries to cancel an alarm timer
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not running,
 * and -1 if the callback was running
 */
int alarm_try_to_cancel(struct alarm *alarm);

/**
 * alarm_cancel - Spins trying to cancel an alarm timer until it is done
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not active.
 */
int alarm_cancel(struct alarm *alarm);

/**
 * alarm_restart - Restart the alarmtimer from the start.
 * @alarm: ptr to alarm to be restarted
 *
 * Returns void
 */
void alarm_restart(struct alarm *alarm);

/**
 * alarm_forward_now - Increment timer expire time by interval value w.r.t
 * current time.
 * @alarm: ptr to alarm to be forwarded
 *
 * Returns number of overrun or missed alarms.
 */
u64 alarm_forward_now(struct alarm *alarm, ktime_t interval);

/**
 * alarm_expires_remaining - time difference between expire time and current
 * time
 * @alarm: ptr to alarm to be expire remaining time
 *
 * Returns remaining time in ktime_t
 */
ktime_t alarm_expires_remaining(const struct alarm *alarm);

#endif /*__KERNEL__*/
#endif /*__LINUX_ALARMTIMER_H*/
