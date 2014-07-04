#include <linux/kobject.h>

#include "priv-input.h"

/* XXX: does sysfs lock for setting? */
/* XXX: some sort of synchronization with input events for when this
   is being turned on/off? */
static int privacy_mode_on;

struct kobject *priv_kobject;

/* stolen from kernel/ksysfs.c */
#define ATTR_RW(_name) \
static struct kobj_attribute _name##_attr = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

int get_privacy_mode(void)
{
	return privacy_mode_on;
}

static void set_privacy_mode(int new_state)
{
	privacy_mode_on = new_state;
}

static ssize_t enabled_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", privacy_mode_on);
}
static ssize_t enabled_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	if (count > 0)
		set_privacy_mode(buf[0] != '0');

	return count;
}

ATTR_RW(enabled);

static struct attribute *priv_attrs[] = {
	&enabled_attr.attr,
	NULL
};

static struct attribute_group priv_attr_group = {
	.attrs = priv_attrs
};

/* XXX: Generic sysfs for all privacy mode? */
static int priv_init_sysfs(void)
{
	int rc = -1;

	priv_kobject = kobject_create_and_add("privacy", kernel_kobj);
	if (!priv_kobject) {
		rc = -ENOMEM;
		goto out;
	}
	rc = sysfs_create_group(priv_kobject, &priv_attr_group);
	if (rc)
		goto out_put;

	rc = 0;
	goto out;

 out_put:
	kobject_put(priv_kobject);
 out:
	return rc;
}

int priv_input_init(void)
{
	privacy_mode_on = 0;

	if (priv_init_sysfs() < 0)
		return -1;

	return 0;
}
