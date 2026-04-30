/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <compiler.h>
#include <kpmodule.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>

KPM_NAME("NetlinkDemo");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("FantasySR");
KPM_DESCRIPTION("Netlink send test (pid via CTL0)");

/* ---- 手动补充缺失的 errno ---- */
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef EIO
#define EIO   5
#endif

#define NETLINK_TEST 31
#define GFP_ATOMIC 0x20U

struct nlmsghdr {
    __u32 nlmsg_len;
    __u16 nlmsg_type;
    __u16 nlmsg_flags;
    __u32 nlmsg_seq;
    __u32 nlmsg_pid;
};
#define NLMSG_ALIGNTO    4
#define NLMSG_ALIGN(len) (((len) + NLMSG_ALIGNTO - 1) & ~(NLMSG_ALIGNTO - 1))
#define NLMSG_HDRLEN     ((int)NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_ALIGN(NLMSG_HDRLEN))
#define NLMSG_SPACE(len)  NLMSG_ALIGN(NLMSG_LENGTH(len))
#define NLMSG_DATA(nlh)   ((void *)(((char *)(nlh)) + NLMSG_LENGTH(0)))
#define NLMSG_DONE 3

struct netlink_kernel_cfg {
    void (*input)(void *);   // 用 void* 绕过 sk_buff 前向声明问题
};

typedef void *(*nl_create_t)(void *, int, struct netlink_kernel_cfg *);
typedef void (*nl_release_t)(void *);
typedef void *(*alloc_skb_t)(unsigned int, gfp_t);
typedef void (*kfree_skb_t)(void *);
typedef int (*nl_unicast_t)(void *, void *, u32);
typedef void *(*nl_put_t)(void *, u32, u32, int, int, int);
typedef void (*nl_end_t)(void *, void *);

static nl_create_t netlink_kernel_create_ptr = NULL;
static nl_release_t netlink_kernel_release_ptr = NULL;
static alloc_skb_t alloc_skb_ptr = NULL;
static kfree_skb_t kfree_skb_ptr = NULL;
static nl_unicast_t nlmsg_unicast_ptr = NULL;
static nl_put_t nlmsg_put_ptr = NULL;
static nl_end_t nlmsg_end_ptr = NULL;

static void *nl_sk = NULL;
static void *init_net_ptr = NULL;

static long netlink_control0(const char *args, char *__user out_msg, int outlen)
{
    if (!args || !nl_sk) {
        if (out_msg && outlen > 0) strncpy(out_msg, "not ready", outlen);
        return 0;
    }

    int target_pid = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') target_pid = target_pid * 10 + (*p++ - '0');
    if (*p != '\0' || target_pid <= 0) {
        if (out_msg && outlen > 0) strncpy(out_msg, "err:bad pid", outlen);
        return -EINVAL;
    }

    void *skb;
    void *nlh;
    const char *msg = "Hello from SukiSU Kernel!";
    int size = NLMSG_SPACE(32);

    skb = alloc_skb_ptr(size, GFP_ATOMIC);
    if (!skb) {
        printk(KERN_ERR "NL_DEMO: alloc_skb failed\n");
        if (out_msg && outlen > 0) strncpy(out_msg, "err:alloc", outlen);
        return -ENOMEM;
    }

    nlh = nlmsg_put_ptr(skb, 0, 0, NLMSG_DONE, 32, 0);
    if (!nlh) {
        kfree_skb_ptr(skb);
        if (out_msg && outlen > 0) strncpy(out_msg, "err:put", outlen);
        return -ENOMEM;
    }
    memcpy(NLMSG_DATA(nlh), msg, strlen(msg) + 1);
    nlmsg_end_ptr(skb, nlh);

    if (nlmsg_unicast_ptr(nl_sk, skb, target_pid) < 0) {
        printk(KERN_ERR "NL_DEMO: nlmsg_unicast to pid %d failed\n", target_pid);
        if (out_msg && outlen > 0) strncpy(out_msg, "err:send", outlen);
        return -EIO;
    }

    printk(KERN_INFO "NL_DEMO: sent message to pid %d\n", target_pid);
    if (out_msg && outlen > 0) strncpy(out_msg, "ok", outlen);
    return 0;
}

static long init(const char *args, const char *event, void *__user reserved)
{
    init_net_ptr = (void *)kallsyms_lookup_name("init_net");

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

    struct netlink_kernel_cfg cfg = { .input = NULL };
    nl_sk = netlink_kernel_create_ptr(init_net_ptr, NETLINK_TEST, &cfg);
    if (!nl_sk) {
        printk(KERN_ERR "NL_DEMO: netlink_kernel_create failed\n");
        return -1;
    }

    printk(KERN_INFO "NL_DEMO: Netlink socket created (proto=%d)\n", NETLINK_TEST);
    return 0;
}

static long exit(void *__user reserved)
{
    if (nl_sk) { netlink_kernel_release_ptr(nl_sk); nl_sk = NULL; }
    printk(KERN_INFO "NL_DEMO: unloaded\n");
    return 0;
}

KPM_INIT(init);
KPM_EXIT(exit);
KPM_CTL0(netlink_control0);