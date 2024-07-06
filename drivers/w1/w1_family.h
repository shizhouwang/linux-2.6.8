/*
 * 	w1_family.h
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __W1_FAMILY_H
#define __W1_FAMILY_H

#include <linux/types.h>
#include <linux/device.h>
#include <asm/atomic.h>

#define W1_FAMILY_DEFAULT	0
#define W1_FAMILY_THERM		0x10
#define W1_FAMILY_IBUT		0xff /* ? */

#define MAXNAMELEN		32

struct w1_family_ops
{
	ssize_t (* rname)(struct device *, char *);
	ssize_t (* rbin)(struct kobject *, char *, loff_t, size_t);
	
	ssize_t (* rval)(struct device *, char *);
	unsigned char rvalname[MAXNAMELEN];
};

struct w1_family
{
	struct list_head	family_entry;
	u8			fid;
	
	struct w1_family_ops	*fops;
	
	atomic_t		refcnt;
	u8			need_exit;
};

extern spinlock_t w1_flock;

void w1_family_get(struct w1_family *);
void w1_family_put(struct w1_family *);
void __w1_family_get(struct w1_family *);
void __w1_family_put(struct w1_family *);
struct w1_family * w1_family_registered(u8);
void w1_unregister_family(struct w1_family *);
int w1_register_family(struct w1_family *);

#endif /* __W1_FAMILY_H */