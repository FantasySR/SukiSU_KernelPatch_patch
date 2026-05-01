/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <compiler.h>
#include <kpmodule.h>
#include <linux/printk.h>

KPM_NAME("NetlinkDemo");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("FantasySR");
KPM_DESCRIPTION("Minimal kallsyms test");

static long init(const char *args, const char *event, void *__user reserved)
{
    unsigned long printk_addr = kallsyms_lookup_name("printk");
    printk(KERN_INFO "NL_DEMO: printk address = %lx\n", printk_addr);
    if (printk_addr) {
        printk(KERN_INFO "NL_DEMO: kallsyms_lookup_name works!\n");
    } else {
        printk(KERN_ERR "NL_DEMO: kallsyms_lookup_name returned NULL\n");
    }
    return 0;
}

static long exit(void *__user reserved)
{
    printk(KERN_INFO "NL_DEMO: unloaded\n");
    return 0;
}

KPM_INIT(init);
KPM_EXIT(exit);