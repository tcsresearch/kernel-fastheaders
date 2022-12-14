/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NET_SCM_API_H
#define __LINUX_NET_SCM_API_H

#include <net/scm_types.h>

#include <linux/limits.h>
#include <linux/net.h>
#include <linux/cred.h>
#include <linux/security.h>
#include <linux/pid.h>
#include <linux/nsproxy.h>

/* Well, we should have at least one descriptor open
 * to accept passed FDs 8)
 */
#define SCM_MAX_FD	253

struct scm_fp_list {
	short			count;
	short			max;
	struct user_struct	*user;
	struct file		*fp[SCM_MAX_FD];
};

struct scm_cookie {
	struct pid		*pid;		/* Skb credentials */
	struct scm_fp_list	*fp;		/* Passed files		*/
	struct scm_creds	creds;		/* Skb credentials	*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Passed security ID 	*/
#endif
};

void scm_detach_fds(struct msghdr *msg, struct scm_cookie *scm);
void scm_detach_fds_compat(struct msghdr *msg, struct scm_cookie *scm);
int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm);
void __scm_destroy(struct scm_cookie *scm);
struct scm_fp_list *scm_fp_dup(struct scm_fp_list *fpl);

#ifdef CONFIG_SECURITY_NETWORK
static __inline__ void unix_get_peersec_dgram(struct socket *sock, struct scm_cookie *scm)
{
	security_socket_getpeersec_dgram(sock, NULL, &scm->secid);
}
#else
static __inline__ void unix_get_peersec_dgram(struct socket *sock, struct scm_cookie *scm)
{ }
#endif /* CONFIG_SECURITY_NETWORK */

static __inline__ void scm_set_cred(struct scm_cookie *scm,
				    struct pid *pid, kuid_t uid, kgid_t gid)
{
	scm->pid  = get_pid(pid);
	scm->creds.pid = pid_vnr(pid);
	scm->creds.uid = uid;
	scm->creds.gid = gid;
}

static __inline__ void scm_destroy_cred(struct scm_cookie *scm)
{
	put_pid(scm->pid);
	scm->pid  = NULL;
}

static __inline__ void scm_destroy(struct scm_cookie *scm)
{
	scm_destroy_cred(scm);
	if (scm->fp)
		__scm_destroy(scm);
}

extern int scm_send(struct socket *sock, struct msghdr *msg,
		    struct scm_cookie *scm, bool forcecreds);

#ifdef CONFIG_SECURITY_NETWORK
static inline void scm_passec(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm)
{
	char *secdata;
	u32 seclen;
	int err;

	if (test_bit(SOCK_PASSSEC, &sock->flags)) {
		err = security_secid_to_secctx(scm->secid, &secdata, &seclen);

		if (!err) {
			put_cmsg(msg, SOL_SOCKET, SCM_SECURITY, seclen, secdata);
			security_release_secctx(secdata, seclen);
		}
	}
}
#else
static inline void scm_passec(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm)
{ }
#endif /* CONFIG_SECURITY_NETWORK */

extern void scm_recv(struct socket *sock, struct msghdr *msg,
		     struct scm_cookie *scm, int flags);

#endif /* __LINUX_NET_SCM_API_H */

