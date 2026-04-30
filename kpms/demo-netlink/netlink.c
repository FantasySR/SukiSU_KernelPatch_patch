/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Netlink Demo for SukiSU KernelPatch (自包含版)
 * 完全不依赖 linux/netlink.h 等外部头文件
 */

#include <compiler.h>
#include <kpmodule.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/* ========== 手动定义 Netlink 所需的所有结构体和常量 ========== */

#define NETLINK_TEST 31

/* 网络命名空间 (仅前向声明) */
struct net;

/* Netlink 消息头 (标准定义) */
struct nlmsghdr {
    __u32   nlmsg_len;      /* 消息总长度 (含头部) */
    __u16   nlmsg_type;     /* 消息类型 */
    __u16   nlmsg_flags;    /* 附加标志 */
    __u32   nlmsg_seq;      /* 序列号 */
    __u32   nlmsg_pid;      /* 发送者端口ID */
};

#define NLMSG_ALIGNTO    4
#define NLMSG_ALIGN(len) (((len) + NLMSG_ALIGNTO - 1) & ~(NLMSG_ALIGNTO - 1))
#define NLMSG_HDRLEN     ((int)NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_ALIGN(NLMSG_HDRLEN))
#define NLMSG_SPACE(len)  NLMSG_ALIGN(NLMSG_LENGTH(len))
#define NLMSG_DATA(nlh)   ((void *)(((char *)(nlh)) + NLMSG_LENGTH(0)))
#define NLMSG_OK(nlh, len) ((len) >= (int)sizeof(struct nlmsghdr) && \
                             (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
                             (int)(nlh)->nlmsg_len <= (len))
#define NLMSG_DONE 3

/* Netlink 内核配置 (简化版) */
struct netlink_kernel_cfg {
    void (*input)(struct sk_buff *skb);
};

/* 套接字缓冲区 (仅前向声明足以) */
struct sk_buff;
struct sock;

/* ========== 动态获取的函数指针 ========== */
typedef struct sock *(*nl_create_t)(struct net *, int, struct netlink_kernel_cfg *);
typedef void (*nl_release_t)(struct sock *);
typedef struct sk_buff *(*alloc_skb_t)(unsigned int, gfp_t);
typedef void (*kfree_skb_t)(struct sk_buff *);
typedef int (*nl_unicast_t)(struct sock *, struct sk_buff *, u32);
typedef struct nlmsghdr *(*nl_put_t)(struct sk_buff *, u32, u32, int, int, int);
typedef void (*nl_end_t)(struct sk_buff *, struct nlmsghdr *);

static nl_create_t netlink_kernel_create_ptr = NULL;
static nl_release_t netlink_kernel_release_ptr = NULL;
static alloc_skb_t alloc_skb_ptr = NULL;
static kfree_skb_t kfree_skb_ptr = NULL;
static nl_unicast_t nlmsg_unicast_ptr = NULL;
static nl_put_t nlmsg_put_ptr = NULL;
static nl_end_t nlmsg_end_ptr = NULL;

static struct sock *nl_sk = NULL;

KPM_NAME("NetlinkDemo");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("FantasySR");
KPM_DESCRIPTION("Netlink communication test (self-contained)");

/* ---- 收到用户态消息的回调 ---- */
static void nl_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;
    char *msg;
    int msg_len;
    struct sk_buff *reply_skb;
    struct nlmsghdr *reply_nlh;
    const char *reply_msg = "Hello from SukiSU Kernel!";
    int reply_size;

    nlh = (struct nlmsghdr *)skb->data;
    msg = (char *)NLMSG_DATA(nlh);
    msg_len = nlh->nlmsg_len - NLMSG_HDRLEN;

    printk(KERN_INFO "NL_DEMO: received %.*s\n", msg_len, msg);

    /* 构造回复 */
    reply_size = NLMSG_SPACE(32);
    reply_skb = alloc_skb_ptr(reply_size, GFP_ATOMIC);
    if (!reply_skb) {
        printk(KERN_ERR "NL_DEMO: alloc_skb failed\n");
        return;
    }

    reply_nlh = nlmsg_put_ptr(reply_skb, nlh->nlmsg_pid, 0, NLMSG_DONE, 32, 0);
    if (!reply_nlh) {
        kfree_skb_ptr(reply_skb);
        return;
    }
    memcpy(NLMSG_DATA(reply_nlh), reply_msg, strlen(reply_msg) + 1);
    nlmsg_end_ptr(reply_skb, reply_nlh);

    if (nlmsg_unicast_ptr(nl_sk, reply_skb, nlh->nlmsg_pid) < 0) {
        printk(KERN_ERR "NL_DEMO: nlmsg_unicast failed\n");
    }
}

static long init(const char *args, const char *event, void *__user reserved)
{
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv_msg,
    };

    /* 动态获取所有 Netlink 函数 */
    netlink_kernel_create_ptr = (nl_create_t)kallsyms_lookup_name("netlink_kernel_create");
    netlink_kernel_release_ptr = (nl_release_t)kallsyms_lookup_name("netlink_kernel_release");
    alloc_skb_ptr = (alloc_skb_t)kallsyms_lookup_name("alloc_skb");
    kfree_skb_ptr = (kfree_skb_t)kallsyms_lookup_name("kfree_skb");
    nlmsg_unicast_ptr = (nl_unicast_t)kallsyms_lookup_name("nlmsg_unicast");
    nlmsg_put_ptr = (nl_put_t)kallsyms_lookup_name("nlmsg_put");
    nlmsg_end_ptr = (nl_end_t)kallsyms_lookup_name("nlmsg_end");

    if (!netlink_kernel_create_ptr || !netlink_kernel_release_ptr ||
        !alloc_skb_ptr || !kfree_skb_ptr || !nlmsg_unicast_ptr ||
        !nlmsg_put_ptr || !nlmsg_end_ptr) {
        printk(KERN_ERR "NL_DEMO: failed to find required symbols\n");
        return -1;
    }

    nl_sk = netlink_kernel_create_ptr(&init_net, NETLINK_TEST, &cfg);
    if (!nl_sk) {
        printk(KERN_ERR "NL_DEMO: netlink_kernel_create failed\n");
        return -1;
    }

    printk(KERN_INFO "NL_DEMO: Netlink socket created (proto=%d)\n", NETLINK_TEST);
    return 0;
}

static long exit(void *__user reserved)
{
    if (nl_sk) {
        netlink_kernel_release_ptr(nl_sk);
        nl_sk = NULL;
    }
    printk(KERN_INFO "NL_DEMO: unloaded\n");
    return 0;
}

KPM_INIT(init);
KPM_EXIT(exit);