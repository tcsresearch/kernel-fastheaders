// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NET4:	Sysctl interface to net af_unix subsystem.
 *
 * Authors:	Mike Shaver.
 */

#include <net/net_namespace_types.h>
#include <net/net_namespace.h>
#include <net/net_namespace_api.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <net/af_unix.h>

static struct ctl_table unix_table[] = {
	{
		.procname	= "max_dgram_qlen",
		.data		= &init_net.unx.sysctl_max_dgram_qlen,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{ }
};

int __net_init unix_sysctl_register(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(unix_table, sizeof(unix_table), GFP_KERNEL);
	if (table == NULL)
		goto err_alloc;

	table[0].data = &net->unx.sysctl_max_dgram_qlen;
	net->unx.ctl = register_net_sysctl(net, "net/unix", table);
	if (net->unx.ctl == NULL)
		goto err_reg;

	return 0;

err_reg:
	kfree(table);
err_alloc:
	return -ENOMEM;
}

void unix_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->unx.ctl->ctl_table_arg;
	unregister_net_sysctl_table(net->unx.ctl);
	kfree(table);
}
