#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>

struct kobject privos_kobj;
EXPORT_SYMBOL(privos_kobj);

static struct attribute privos_attr_test1w = {
    .name = "test1w",
    .mode = S_IWUGO,
};

static struct attribute privos_attr_test2w = {
    .name = "test2w",
    .mode = S_IWUGO,
};

static struct attribute privos_attr_test3r = {
    .name = "test3r",
    .mode = S_IRUGO,
};

static struct attribute* privos_default_attrs[] = {
    &privos_attr_test1w,
    &privos_attr_test2w,
    &privos_attr_test3r,
    NULL
};

static ssize_t privos_show(struct kobject *kobj, struct attribute *attr,
        char *buffer)
{
    if (attr != &privos_attr_test3r)
        return 0;
    strcpy(buffer, "HELLO: this is PrivOS!\n");
    return 23;
}

static ssize_t privos_store(struct kobject *kobj, struct attribute *attr,
        const char *buffer, size_t size)
{
    return size;
}

static struct sysfs_ops privos_sysfs_ops = {
    .show = &privos_show,
    .store = &privos_store
};

static struct kobj_type privos_ktype = {
    .sysfs_ops = &privos_sysfs_ops,
    .default_attrs = privos_default_attrs
};

static void init_kobj(void){
   kobject_init_and_add(&privos_kobj, &privos_ktype, NULL, "privos");
}

static int privos_init(void)
{
    init_kobj();
    return 0;
}
static void privos_exit(void)
{
    kobject_del(&privos_kobj);
    kobject_put(&privos_kobj);
}
module_init(privos_init);
module_exit(privos_exit);

MODULE_LICENSE("GPL");
