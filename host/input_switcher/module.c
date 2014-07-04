#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>

#include <linux/kvm_para.h>

#define HANDLER_NAME "iswitch"

static int bench_mode = 0;
module_param(bench_mode, int, 0444);

struct input_switcher_state {
	struct input_handle handle;

	u8 l_ctrl, l_alt;
};

struct toggle_work {
	struct work_struct work;
	struct input_dev *dev;
};
static struct toggle_work the_toggle_work;

static void do_toggle(struct work_struct *work);

static void schedule_toggle(struct input_dev *dev)
{
	/* XXX: worry about multiple schedule_toggle's occurring at
	   the same time? */
	INIT_WORK(&the_toggle_work.work, do_toggle);
	the_toggle_work.dev = dev;
	schedule_work(&the_toggle_work.work);
}

static void do_toggle(struct work_struct *work)
{
	struct toggle_work *tw = container_of(work, struct toggle_work, work);

	pr_debug("performing toggle\n");

	/* "unpress" keys that might be down.  I believe the following
	   function does what we want. */
	input_reset_device(tw->dev);
       
	/* XXX: Put this in a private OS function? */
	kvm_hypercall1(KVM_HC_TOGGLE, 0);
	kvm_hypercall0(KVM_HC_TOGGLE);	
}

static void input_switcher_event(struct input_handle *handle,
				 unsigned int type, unsigned int code, int value)
{
	int toward = 0;
	struct input_switcher_state *is = container_of(handle, struct input_switcher_state, handle);

	/* type is event type (e.g. keypress), for keypress, code is
	 * the key, and value is
	 * (0 = release, 1 = press, 2 = repeat)
	 */
	if (type == EV_KEY) {
		/* in bench mode, we want to know about every keypress */
		if (bench_mode)
			kvm_hypercall1(KVM_HC_TOGGLE, 2);

		if (code == KEY_LEFTCTRL && value != 2) {
			is->l_ctrl = value;
			toward = (toward || value);
			pr_debug("input_switcher: left-ctrl %s\n",
				 (value ? "pressed" : "released"));
		}
		if (code == KEY_LEFTALT && value != 2) {
			is->l_alt = value;
			toward = (toward || value);
			pr_debug("input_switcher: left-alt %s\n",
				 (value ? "pressed" : "released"));
		}
	} else if ((type == EV_REL) && bench_mode) {
		/* the parameter indicates the type of press */
		kvm_hypercall1(KVM_HC_TOGGLE, 1);
	}

	if (toward && is->l_ctrl && is->l_alt) {
		/* Some key was pressed leading to the key combination
		   we want, and the key combination is now complete */
		pr_debug("left-ctrl,left-alt key combination pressed!\n");

		/* Can't actually do the toggle here, because we are
		   holding the event lock, and clearing pressed keys
		   needs to acuiqre that lock */
		schedule_toggle(handle->dev);
	}
}

static int input_switcher_connect(struct input_handler *handler, struct input_dev *dev,
				  const struct input_device_id *id)
{
	int rc;
	struct input_switcher_state *is;

	is = kzalloc(sizeof(struct input_switcher_state), GFP_KERNEL);
	if (!is)
		return -ENOMEM;

	is->handle.dev = input_get_device(dev);
	is->handle.name = HANDLER_NAME;
	is->handle.handler = handler;

	rc = input_register_handle(&is->handle);
	if (rc)
		goto free;

	rc = input_open_device(&is->handle);
	if (rc)
		goto unregister;

	return 0;

 unregister:
	input_unregister_handle(&is->handle);
 free:
	kfree(is);
	return rc;
}

static void input_switcher_disconnect(struct input_handle *handle)
{
	struct input_switcher_state *is = container_of(handle, struct input_switcher_state, handle);

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(is);
}

/* Copied from the keyboard handler, I believe this matches all
 * devices that can produce EV_KEY events, which is what we care
 * about anyway
 *
 * XXX: Perhaps make this more specific?  Specify via sysfs?
 */
static struct input_device_id input_switcher_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_REL) },
	},
	{ },    /* Terminating entry */
};

static struct input_handler input_switcher_handler = {
	.event = input_switcher_event,
	.connect = input_switcher_connect,
	.disconnect = input_switcher_disconnect,
	.id_table = input_switcher_ids,
	.name = HANDLER_NAME,
};

static int input_switcher_init(void)
{
	return input_register_handler(&input_switcher_handler);
}

static void input_switcher_exit(void)
{
	input_unregister_handler(&input_switcher_handler);
}

module_init(input_switcher_init);
module_exit(input_switcher_exit);

MODULE_LICENSE("GPL");
