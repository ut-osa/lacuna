#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/priv_crypt.h>
#include "../privos_kobject.h"

static struct kobject privnet_kobj;
static int delay;


struct mac_tree_node {
    void* next_byte [256];
};

struct mac_crypt_refs {
    priv_crypt_id_t in;
    struct priv_crypt_ctx_ref in_ref;
    priv_crypt_id_t out;
    struct priv_crypt_ctx_ref out_ref;
};


static struct mac_tree_node *mac_tree_root;

static struct mac_tree_node* alloc_mac_tree_node(void){
    struct mac_tree_node *node = kmalloc(sizeof(struct mac_tree_node), GFP_KERNEL);
    memset(node, 0, sizeof(struct mac_tree_node));
    return node;
}

static void __free_mac_tree_node_recursive(void *node, int indx){
    int i = 0;
    if (!node)
        return;
    if (indx < 6) {
        for(; i < 256; i++) {
            if (((struct mac_tree_node *)node)->next_byte[i] != NULL)
                __free_mac_tree_node_recursive(((struct mac_tree_node *)node)->next_byte[i],
                        indx + 1);
        }
    }

    kfree(node);

}

static void free_mac_tree_node(struct mac_tree_node *node)
{
    __free_mac_tree_node_recursive(node, 0);
}

static int setup_node(struct mac_crypt_refs *nid,
                      priv_crypt_id_t in, priv_crypt_id_t out)
{
    int rc = -1;

    nid->in = in;
    cctx_ref_init(&nid->in_ref);
    nid->out = out;
    cctx_ref_init(&nid->out_ref);
    if (use_ctx(in, &nid->in_ref) < 0) {
        printk(KERN_ERR "%s: use_ctx error\n", __func__);
        goto out;
    }
    if (use_ctx(out, &nid->out_ref) < 0) {
        printk(KERN_ERR "%s: use_ctx error\n", __func__);
        goto out_release1;
    }
    rc = 0;
    goto out;

 out_release1:
    unuse_ctx(&nid->in_ref);
 out:
    return rc;
}

static void teardown_node(struct mac_crypt_refs *nid)
{
    unuse_ctx(&nid->in_ref);
    unuse_ctx(&nid->out_ref);
}

/*
 * add a mac address to the tree
 * mac: 6-byte mac address
 */
static int privnet_add_mac(unsigned char *mac, priv_crypt_id_t in, priv_crypt_id_t out)
{
    int i = 0, rc = -1;
    struct mac_tree_node *node = mac_tree_root;
    struct mac_crypt_refs *nid;
    for (; i < 5; i++){
        if (node->next_byte[mac[i]] == NULL){
            struct mac_tree_node *nnd = alloc_mac_tree_node();
            node->next_byte[mac[i]] = nnd;
            node = nnd;
        }
        else {
            node = (struct mac_tree_node *)(node->next_byte[mac[i]]);
        }
    }
    if (node->next_byte[mac[5]] != NULL) {
        /* MAC already in use */
        printk(KERN_ERR "%s: MAC in use\n", __func__);
        rc = -EBUSY;
        goto out;
    }

    nid = kmalloc(sizeof(struct mac_crypt_refs), GFP_KERNEL);
    if (!nid) {
        rc = -ENOMEM;
        goto out;
    }

    rc = setup_node(nid, in, out);
    if (rc < 0)
        goto out_free;

    node->next_byte[mac[5]] = nid;
    rc = 0;
    goto out;

 out_free:
    kfree(nid);
 out:
    return rc;
}

//EXPORT_SYMBOL(privnet_add_mac);

/*
 * a recursive helper for remove_mac
 */
static bool __remove_mac_recursive(void *nptr, unsigned char *mac, int indx)
{
    int i = 0;
    int cnt = 0;
    struct mac_tree_node *node;

    if (indx == 6) {
        teardown_node(nptr);
        kfree(nptr);
        return true;
    }
    node = (struct mac_tree_node *)nptr;

    if (node->next_byte[mac[indx]] == NULL)
        return false;
    if (__remove_mac_recursive(node->next_byte[mac[indx]], mac, indx + 1)) {
        node->next_byte[mac[indx]] = NULL;
        for (; i < 256; i++){
            if (node->next_byte[i] != NULL)
                cnt++;
        }
        if (cnt == 0) {
            if (indx > 0)
                kfree(node);
            return true;
        }
        else return false;
    }
    else return false;
}
/*
 * remove a mac address from the tree
 * mac: 6-byte mac address
 */
static void privnet_remove_mac(unsigned char *mac)
{
    __remove_mac_recursive(mac_tree_root, mac, 0);
}
//EXPORT_SYMBOL(privnet_remove_mac);


/*
 * return the pointer to the mac_crypt_refs stucture
 * if mac exists in the tree
 */
void* privnet_find_mac(unsigned char *mac)
{
    int i = 0;
    struct mac_tree_node *node = mac_tree_root;
    for (; i < 6; i++){
        if (node->next_byte[mac[i]] != NULL){
            node = node->next_byte[mac[i]];
        }
        else return NULL;
    }
    return node;
}
EXPORT_SYMBOL(privnet_find_mac);




/*
 * {en,de}crypt, direction determined by the ctx
 */
static inline void __privnet_crypt(unsigned char *data, int len,
                                   struct priv_crypt_ctx_ref *ref)
{
    unsigned long flags;
#if 0
    /* XXX: offer xor as alternate encryption mode? */
    int i = 0;
    for (; i < len; i++){
        data[i] = ~data[i];
    }
#endif
    spin_lock_irqsave(&ref->lock, flags);
    /* XXX: technically should obey "activation/deactivation", but I
       don't think we'll need that for network */
    if (ref->cctx) {
        if (priv_crypt(ref->cctx, data, data, len) < 0) {
            printk(KERN_ERR "%s: crypt error!\n", __func__);
            /* XXX: what to do here? */
        }
    }
    spin_unlock_irqrestore(&ref->lock, flags);

    //delay
    udelay(delay);
}

void privnet_encrypt(unsigned char *data, int len, void* idptr)
{
    //encrypt in the kernel only happens for input
    __privnet_crypt(data, len, &((struct mac_crypt_refs *)idptr)->in_ref);
}
EXPORT_SYMBOL(privnet_encrypt);

void privnet_decrypt(unsigned char *data, int len, void* idptr)
{
    //decrypt in the kernel only happens for output
    __privnet_crypt(data, len, &((struct mac_crypt_refs *)idptr)->out_ref);
}
EXPORT_SYMBOL(privnet_decrypt);

static ssize_t __print_macs_recursive(struct mac_tree_node *node, unsigned char* mac, int indx, char **buff)
{
    int i = 0;
    ssize_t size = 0;
    ssize_t n = 0;
    if (indx == 5) {
        for (; i < 256; i++){
            if (node->next_byte[i] != NULL){
                mac[5] = i;
                n = sprintf(*buff, "PRIV-MAC: %02X:%02X:%02X:%02X:%02X:%02X, id_in: %0X, id_out:%X\n",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], 
                        ((struct mac_crypt_refs *)(node->next_byte[i]))->in,
                        ((struct mac_crypt_refs *)(node->next_byte[i]))->out);
                size += n;
                *buff += n;
            }
        }
        return size;
    }
    for (; i < 256; i++){
        if (node->next_byte[i] != NULL){
            mac[indx] = i;
            size += __print_macs_recursive(
                    (struct mac_tree_node *)(node->next_byte[i]), mac, indx + 1, buff);
        }
    }
    return size;

}

static ssize_t privnet_print_macs(char *buff)
{
    char mac[6];
    return __print_macs_recursive(mac_tree_root, mac, 0, &buff);
}


static struct attribute privnet_attr_add_mac = {
    .name = "add_mac",
    .mode = S_IWUGO,
};

static struct attribute privnet_attr_remove_mac = {
    .name = "remove_mac",
    .mode = S_IWUGO,
};

static struct attribute privnet_attr_show_macs = {
    .name = "show_macs",
    .mode = S_IRUGO,
};

static struct attribute privnet_attr_delay = {
    .name = "delay",
    .mode = S_IRUGO | S_IWUGO,
};

static struct attribute* privnet_default_attrs[] = {
    &privnet_attr_add_mac,
    &privnet_attr_remove_mac,
    &privnet_attr_show_macs,
    &privnet_attr_delay,
    NULL
};

static ssize_t itoa_dec(char *str, int num) {
    char buf[10];
    ssize_t len = 1;
    int i = 0;

    buf[0] = '0';
    while (num > 0) {
        buf[len - 1] = num % 10 + '0';
        num = num / 10;
        len++;
    }
    if (len > 1)
        len--;
    for (; i < len; i++) {
        str[i] = buf[len - 1 - i];
    }
    str[len] = '\n';
    return len + 1;
}

static ssize_t privnet_show(struct kobject *kobj, struct attribute *attr,
        char *buffer)
{
    if (attr == &privnet_attr_show_macs)
        return privnet_print_macs(buffer);
    if (attr == &privnet_attr_delay) {
        return itoa_dec(buffer, delay);
    }

    return 0;

}

static unsigned char hex2char (char h, char l)
{
    if (h >= 'A' && h <= 'F'){
        h = (h - 'A' + 10) << 4;
    }
    else if (h >= 'a' && h <= 'f'){
        h = (h - 'a' + 10) << 4;
    }
    else if (h >= '0' && h <= '9'){
        h = (h - '0') << 4;
    }
    else h = 0;

    if (l >= 'A' && l <= 'F'){
        l = l - 'A' + 10;
    }
    else if (l >= 'a' && l <= 'f'){
        l = l - 'a' + 10;
    }
    else if (l >= '0' && l <= '9'){
        l = l - '0';
    }
    else l = 0;
    return h + l;
}

static int atoi_dec(const char *str, int len) {
    int res = 0;
    int weight = 1;
    int i = len - 1;
    for (; i >=0; i--) {
        res += weight * (str[i] - '0');
        weight *= 10;
    }
    return res;
}

static inline bool is_hex_digit(char c) {
   return (c >= '0' && c <='9') || (c >= 'a' && c <= 'f')
      || (c >= 'A' && c <= 'F');
}


static unsigned char mac_buff[6];

//the input is text based. (hex)
//for add_mac: mac, id_in, id_out, e.g. a1:c3:11:22:33:ee,a1,a2 
//for remove_mac: mac
static ssize_t privnet_store(struct kobject *kobj, struct attribute *attr,
        const char *buffer, size_t size)
{
    int i = 0;
    priv_crypt_id_t id_in = 0;
    priv_crypt_id_t id_out = 0;
    ssize_t rc;

    if (attr == &privnet_attr_add_mac || attr == &privnet_attr_remove_mac) {
        if (size < 17)
            return size;
        for (; i < 6; i++) {
            mac_buff[i] = hex2char(buffer[i * 3], buffer[i * 3 + 1]);
        }
        if (attr == &privnet_attr_add_mac) {
            if (size < 21)
                return size;
            for (i = 18; is_hex_digit(buffer[i]); i++) {
                id_in <<= 4;
                id_in += hex2char('0', buffer[i]);
            }
            for (i++; is_hex_digit(buffer[i]); i++) {
                id_out <<= 4;
                id_out += hex2char('0', buffer[i]);
            }
            rc = privnet_add_mac(mac_buff, id_in, id_out);
            if (rc < 0)
                goto out;
        }
        else if (attr == &privnet_attr_remove_mac)
            privnet_remove_mac(mac_buff);
    }
    else if (attr == &privnet_attr_delay){
        delay = atoi_dec(buffer, size - 1);
    }
    rc = size;

 out:
    return rc;
}

static struct sysfs_ops privnet_sysfs_ops = {
    .show = &privnet_show,
    .store = &privnet_store
};

static struct kobj_type privnet_ktype = {
    //.release = privnet_release,
    .sysfs_ops = &privnet_sysfs_ops,
    .default_attrs = privnet_default_attrs
};

static void init_kobj(void){
    kobject_init_and_add(&privnet_kobj, &privnet_ktype, &privos_kobj, "privnet");
}

static int privnet_init(void)
{
    init_kobj();
    delay = 0;
    mac_tree_root = alloc_mac_tree_node();
    return 0;
}
static void privnet_exit(void)
{
    free_mac_tree_node(mac_tree_root);
    kobject_del(&privnet_kobj);
    kobject_put(&privnet_kobj);
}
module_init(privnet_init);
module_exit(privnet_exit);

MODULE_LICENSE("GPL");
