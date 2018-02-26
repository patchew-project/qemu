/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include "qemu/osdep.h"
#include "slirp.h"
#include "libslirp.h"
#include "monitor/monitor.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

#ifdef DEBUG
int slirp_debug = DBG_CALL|DBG_MISC|DBG_ERROR;
#endif

inline void
insque(void *a, void *b)
{
	register struct quehead *element = (struct quehead *) a;
	register struct quehead *head = (struct quehead *) b;
	element->qh_link = head->qh_link;
	head->qh_link = (struct quehead *)element;
	element->qh_rlink = (struct quehead *)head;
	((struct quehead *)(element->qh_link))->qh_rlink
	= (struct quehead *)element;
}

inline void
remque(void *a)
{
  register struct quehead *element = (struct quehead *) a;
  ((struct quehead *)(element->qh_link))->qh_rlink = element->qh_rlink;
  ((struct quehead *)(element->qh_rlink))->qh_link = element->qh_link;
  element->qh_rlink = NULL;
}

int add_exec(struct ex_list **ex_ptr, int do_pty, char *exec,
             struct in_addr addr, int port)
{
	struct ex_list *tmp_ptr;

	/* First, check if the port is "bound" */
	for (tmp_ptr = *ex_ptr; tmp_ptr; tmp_ptr = tmp_ptr->ex_next) {
		if (port == tmp_ptr->ex_fport &&
		    addr.s_addr == tmp_ptr->ex_addr.s_addr)
			return -1;
	}

	tmp_ptr = *ex_ptr;
	*ex_ptr = g_new(struct ex_list, 1);
	(*ex_ptr)->ex_fport = port;
	(*ex_ptr)->ex_addr = addr;
	(*ex_ptr)->ex_pty = do_pty;
	(*ex_ptr)->ex_exec = (do_pty == 3) ? exec : g_strdup(exec);
	(*ex_ptr)->ex_next = tmp_ptr;
	return 0;
}


#ifdef _WIN32

int
fork_exec(struct socket *so, const char *ex, int do_pty)
{
    /* not implemented */
    return 0;
}

#else

/*
 * XXX This is ugly
 * We create and bind a socket, then fork off to another
 * process, which connects to this socket, after which we
 * exec the wanted program.  If something (strange) happens,
 * the accept() call could block us forever.
 *
 * do_pty = 0   Fork/exec inetd style
 * do_pty = 1   Fork/exec using slirp.telnetd
 * do_ptr = 2   Fork/exec using pty
 */
int
fork_exec(struct socket *so, const char *ex, int do_pty)
{
	int s;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	int opt;
	const char *argv[256];
	/* don't want to clobber the original */
	char *bptr;
	const char *curarg;
	int c, i, ret;
	pid_t pid;

	DEBUG_CALL("fork_exec");
	DEBUG_ARG("so = %p", so);
	DEBUG_ARG("ex = %p", ex);
	DEBUG_ARG("do_pty = %x", do_pty);

	if (do_pty == 2) {
                return 0;
	} else {
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		addr.sin_addr.s_addr = INADDR_ANY;

		if ((s = qemu_socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
		    bind(s, (struct sockaddr *)&addr, addrlen) < 0 ||
		    listen(s, 1) < 0) {
			error_report("Error: inet socket: %s", strerror(errno));
			if (s >= 0) {
			    closesocket(s);
			}

			return 0;
		}
	}

	pid = fork();
	switch(pid) {
	 case -1:
		error_report("Error: fork failed: %s", strerror(errno));
		close(s);
		return 0;

	 case 0:
                setsid();

		/* Set the DISPLAY */
                getsockname(s, (struct sockaddr *)&addr, &addrlen);
                close(s);
                /*
                 * Connect to the socket
                 * XXX If any of these fail, we're in trouble!
                 */
                s = qemu_socket(AF_INET, SOCK_STREAM, 0);
                addr.sin_addr = loopback_addr;
                do {
                    ret = connect(s, (struct sockaddr *)&addr, addrlen);
                } while (ret < 0 && errno == EINTR);

		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		for (s = getdtablesize() - 1; s >= 3; s--)
		   close(s);

		i = 0;
		bptr = g_strdup(ex); /* No need to free() this */
		if (do_pty == 1) {
			/* Setup "slirp.telnetd -x" */
			argv[i++] = "slirp.telnetd";
			argv[i++] = "-x";
			argv[i++] = bptr;
		} else
		   do {
			/* Change the string into argv[] */
			curarg = bptr;
			while (*bptr != ' ' && *bptr != (char)0)
			   bptr++;
			c = *bptr;
			*bptr++ = (char)0;
			argv[i++] = g_strdup(curarg);
		   } while (c);

                argv[i] = NULL;
		execvp(argv[0], (char **)argv);

		/* Ooops, failed, let's tell the user why */
        fprintf(stderr, "Error: execvp of %s failed: %s\n",
                argv[0], strerror(errno));
		close(0); close(1); close(2); /* XXX */
		exit(1);

	 default:
		qemu_add_child_watch(pid);
                /*
                 * XXX this could block us...
                 * XXX Should set a timer here, and if accept() doesn't
                 * return after X seconds, declare it a failure
                 * The only reason this will block forever is if socket()
                 * of connect() fail in the child process
                 */
                do {
                    so->s = accept(s, (struct sockaddr *)&addr, &addrlen);
                } while (so->s < 0 && errno == EINTR);
                closesocket(s);
                socket_set_fast_reuse(so->s);
                opt = 1;
                qemu_setsockopt(so->s, SOL_SOCKET, SO_OOBINLINE, &opt, sizeof(int));
		qemu_set_nonblock(so->s);

		/* Append the telnet options now */
                if (so->so_m != NULL && do_pty == 1)  {
			sbappend(so, so->so_m);
                        so->so_m = NULL;
		}

		return 1;
	}
}
#endif

void usernet_get_info(Slirp *slirp, UsernetInfo *info)
{
    struct in_addr dst_addr;
    struct sockaddr_in src;
    socklen_t src_len;
    uint16_t dst_port;
    struct socket *so;
    UsernetConnectionList **p_next = &info->connections;

    for (so = slirp->tcb.so_next; so != &slirp->tcb; so = so->so_next) {
        UsernetConnection *conn = g_new0(UsernetConnection, 1);
        UsernetTCPConnection *tcp = &conn->u.tcp;
        UsernetConnectionList *list = g_new0(UsernetConnectionList, 1);

        list->value = conn;
        if (so->so_state & SS_HOSTFWD) {
            tcp->hostfwd = true;
            tcp->state = so->so_tcpcb->t_state;
        } else if (so->so_tcpcb) {
            tcp->state = so->so_tcpcb->t_state;
        } else {
            tcp->state = TCPS_NONE;
        }
        if (so->so_state & (SS_HOSTFWD | SS_INCOMING)) {
            src_len = sizeof(src);
            getsockname(so->s, (struct sockaddr *)&src, &src_len);
            dst_addr = so->so_laddr;
            dst_port = so->so_lport;
        } else {
            src.sin_addr = so->so_laddr;
            src.sin_port = so->so_lport;
            dst_addr = so->so_faddr;
            dst_port = so->so_fport;
        }
        tcp->fd = so->s;
        tcp->src_addr =
            g_strdup(src.sin_addr.s_addr ? inet_ntoa(src.sin_addr) : "*");
        tcp->src_port = ntohs(src.sin_port);
        tcp->dest_addr = g_strdup(inet_ntoa(dst_addr));
        tcp->dest_port = ntohs(dst_port);
        tcp->recv_buffered = so->so_rcv.sb_cc;
        tcp->send_buffered = so->so_snd.sb_cc;
        *p_next = list;
        p_next = &list->next;
    }
    for (so = slirp->udb.so_next; so != &slirp->udb; so = so->so_next) {
        UsernetConnection *conn = g_new0(UsernetConnection, 1);
        UsernetUDPConnection *udp = &conn->u.udp;
        UsernetConnectionList *list = g_new0(UsernetConnectionList, 1);

        list->value = conn;
        if (so->so_state & SS_HOSTFWD) {
            udp->hostfwd = true;
            src_len = sizeof(src);
            getsockname(so->s, (struct sockaddr *)&src, &src_len);
            dst_addr = so->so_laddr;
            dst_port = so->so_lport;
        } else {
            udp->expire_time_ms = so->so_expire - curtime;
            src.sin_addr = so->so_laddr;
            src.sin_port = so->so_lport;
            dst_addr = so->so_faddr;
            dst_port = so->so_fport;
        }
        udp->fd = so->s;
        udp->src_addr =
            g_strdup(src.sin_addr.s_addr ? inet_ntoa(src.sin_addr) : "*");
        udp->src_port = ntohs(src.sin_port);
        udp->dest_addr = g_strdup(inet_ntoa(dst_addr));
        udp->dest_port = ntohs(dst_port);
        udp->recv_buffered = so->so_rcv.sb_cc;
        udp->send_buffered = so->so_snd.sb_cc;
        *p_next = list;
        p_next = &list->next;
    }

    for (so = slirp->icmp.so_next; so != &slirp->icmp; so = so->so_next) {
        UsernetConnection *conn = g_new0(UsernetConnection, 1);
        UsernetICMPConnection *icmp = &conn->u.icmp;
        UsernetConnectionList *list = g_new0(UsernetConnectionList, 1);

        icmp->expire_time_ms = so->so_expire - curtime;
        src.sin_addr = so->so_laddr;
        dst_addr = so->so_faddr;
        icmp->fd = so->s;
        icmp->src_addr =
            g_strdup(src.sin_addr.s_addr ? inet_ntoa(src.sin_addr) : "*");
        icmp->dest_addr = g_strdup(inet_ntoa(dst_addr));
        icmp->recv_buffered = so->so_rcv.sb_cc;
        icmp->send_buffered = so->so_snd.sb_cc;
        *p_next = list;
        p_next = &list->next;
    }
}


void slirp_connection_info(Slirp *slirp, Monitor *mon)
{
    const char *state;
    char buf[20];
    UsernetInfo info = { };
    UsernetConnectionList *cl;

    monitor_printf(mon, "  Protocol[State]    FD  Source Address  Port   "
                        "Dest. Address  Port RecvQ SendQ\n");

    usernet_get_info(slirp, &info);
    for (cl = info.connections; cl && cl->value; cl = cl->next) {
        UsernetConnection *conn = cl->value;

        if (conn->type == USERNET_TYPE_TCP) {
            UsernetTCPConnection *tcp = &conn->u.tcp;

            if (tcp->hostfwd) {
                state = "HOST_FORWARD";
            } else {
                state = TCPS_str(tcp->state);
            }
            snprintf(buf, sizeof(buf), "  TCP[%s]", state);
            monitor_printf(mon, "%-19s %3" PRId64 " %15s %5" PRId64 " ",
                           buf, tcp->fd,
                           tcp->src_addr, tcp->src_port);
            monitor_printf(mon, "%15s %5" PRId64 " %5" PRId64 " %5" PRId64 "\n",
                           tcp->dest_addr, tcp->dest_port,
                           tcp->recv_buffered, tcp->send_buffered);
        } else if (conn->type == USERNET_TYPE_UDP) {
            UsernetUDPConnection *udp = &conn->u.udp;

            if (udp->hostfwd) {
                snprintf(buf, sizeof(buf), "  UDP[HOST_FORWARD]");
            } else {
                snprintf(buf, sizeof(buf), "  UDP[%" PRId64 " sec]",
                         udp->expire_time_ms / 1000);
            }
            monitor_printf(mon, "%-19s %3" PRId64 " %15s %5" PRId64 " ",
                           buf, udp->fd,
                           udp->src_addr, udp->src_port);
            monitor_printf(mon, "%15s %5" PRId64 " %5" PRId64 " %5" PRId64 "\n",
                           udp->dest_addr, udp->dest_port,
                           udp->recv_buffered, udp->send_buffered);
        } else {
            UsernetICMPConnection *icmp = &conn->u.icmp;

            assert(conn->type == USERNET_TYPE_ICMP);
            snprintf(buf, sizeof(buf), "  ICMP[%" PRId64 " sec]",
                     icmp->expire_time_ms / 1000);
            monitor_printf(mon, "%-19s %3" PRId64 " %15s  -    ", buf, icmp->fd,
                           icmp->src_addr);
            monitor_printf(mon, "%15s  -    %5" PRId64 " %5" PRId64 "\n",
                           icmp->dest_addr,
                           icmp->recv_buffered, icmp->send_buffered);
        }
    }
}
