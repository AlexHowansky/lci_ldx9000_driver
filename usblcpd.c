/*****************************************************************************
 *                          USBlcpd Kernel Driver                             *
 *                            Version 1.00                                   *
 *             (C) 2003 Logic Control, Inc.                  *
 *                                                                           *
 *     This file is licensed under the GPL. See COPYING in the package.      *
 *     10/13/03
 *     1.11: support the linux kernel 3.x
 *	     Change device to /dev/lcpd
 *
 *****************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>

#define DRIVER_VERSION "USBLCPD Driver Version 1.11"
//the major number of usb is 180
#define USBLCPD_MINOR		128 //this number should >64 and is multiple of 16

#define IOCTL_GET_HARD_VERSION	1001
#define IOCTL_GET_DRV_VERSION	1002

/* stall/wait timeout for USBLCPD */
#define NAK_TIMEOUT	(10*HZ)

#define IBUF_SIZE	0x1000
#define OBUF_SIZE	0x1000 //65,536

/* Use our own dbg macro */
//#define dbg_info(dev, format, arg...) do { if (debug) dev_info(dev , format , ## arg); } while (0)

#define warn printk
#define info printk


struct lcpd_usb_data {
	struct usb_device *lcpd_dev;	/* init: probe_lcpd */
	unsigned int ifnum;		/* Interface number of the USB device */
	int isopen;			/* nz if open */
	int present;			/* Device is present on the bus */
	char *obuf, *ibuf;		/* transfer buffers */
	char bulk_in_ep, bulk_out_ep;	/* Endpoint assignments */
	wait_queue_head_t wait_q;	/* for timeouts */
};

static struct lcpd_usb_data lcpd_instance;
//============================================================

//----------------------------------------------------------------------------
//open operation will call this function
static int open_lcpd(struct inode *inode, struct file *file)
{
	
	struct lcpd_usb_data *lcpd = &lcpd_instance;
	info("Enter USBLCPD open_lcpd.");
	if (lcpd->isopen /*|| !lcpd->present*/) {
		return -EBUSY;
	}
	lcpd->isopen = 1;

	init_waitqueue_head(&lcpd->wait_q);

	info("USBLCPD opened.");

	return 0;
}
//------------------------------------------------------------------------
//close() will call it
static int close_lcpd(struct inode *inode, struct file *file)
{
	struct lcpd_usb_data *lcpd = &lcpd_instance;

	lcpd->isopen = 0;

	info("USBLCPD closed.");
	return 0;
}
//------------------------------------------------------------------------
//
//static int
//ioctl_lcpd(struct inode *inode, struct file *file, unsigned int cmd,
//	  unsigned long arg)
static long
ioctl_lcpd(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct lcpd_usb_data *lcpd = &lcpd_instance;
	int i;
	char buf[128];
	memset(buf,0,128);
	/* Sanity check to make sure lcpd is connected, powered, etc */
	if (lcpd == NULL ||
	    lcpd->present == 0 ||
	    lcpd->lcpd_dev == NULL)
		return -1;

	switch (cmd) {
	case IOCTL_GET_HARD_VERSION:
		i = (lcpd->lcpd_dev)->descriptor.bcdDevice;
		sprintf(buf,"%1d%1d.%1d%1d",(i & 0xF000)>>12,(i & 0xF00)>>8,
			(i & 0xF0)>>4,(i & 0xF));
		
		if (copy_to_user((void __user *)arg,buf,strlen(buf))!=0)
			return -EFAULT;
		break;
	case IOCTL_GET_DRV_VERSION:
		{
			sprintf(buf,DRIVER_VERSION);

			info("IOCTL_GET_DRV_VERSION=%s", buf);
			if (copy_to_user((void __user *)arg,buf,strlen(buf))!=0)
				return -EFAULT;
		}
		break;
	default:
		return -ENOIOCTLCMD;
		break;
	}

	return 0;
}
//------------------------------------------------------------------------
static ssize_t
write_lcpd(struct file *file, const char *buffer,
	  size_t count, loff_t * ppos)
{
	DEFINE_WAIT(wait);
	struct lcpd_usb_data *lcpd = &lcpd_instance;

	unsigned long copy_size;
	unsigned long bytes_written = 0;
	unsigned int partial;
	unsigned long timeout = 0;

	int result = 0;
	int maxretry;

	/* Sanity check to make sure lcpd is connected, powered, etc */
	if (lcpd == NULL ||
	    lcpd->present == 0 ||
	    lcpd->lcpd_dev == NULL)
		return -1;

	do { //write data to usb port
		unsigned long thistime; //
		char *obuf = lcpd->obuf;

		thistime = copy_size =
		    (count >= OBUF_SIZE) ? OBUF_SIZE : count;
		//copy data from user space (void *to,const void *from, int c)
		if (copy_from_user(lcpd->obuf, buffer, copy_size))
			return -EFAULT;
		maxretry = 5;
		while (thistime) {
			if (!lcpd->lcpd_dev)
				return -ENODEV;
			//check if there are some signal waiting for operation.
			//if exist , exit, otherwise continue.
			if (signal_pending(current)) {
				return bytes_written ? bytes_written : -EINTR;
			}

			//Builds a bulk urb, sends it off and waits for completion

			result = usb_bulk_msg(lcpd->lcpd_dev,
					 		      usb_sndbulkpipe(lcpd->lcpd_dev, 2), //get send pipe
					                    obuf,
					                    thistime,
					                    &partial,
					                    10 * HZ); //HZ=100

			info("write stats: result:%d thistime:%lu partial:%u",
			     result, thistime, partial);

			if (result == -ETIMEDOUT) {	/* NAK - so hold for a while */
				if (!maxretry--) {
					return -ETIME;
				}
				prepare_to_wait(&lcpd->wait_q, &wait, TASK_INTERRUPTIBLE);
				timeout = schedule_timeout(timeout);
				finish_wait(&lcpd->wait_q, &wait);
				continue;
			} else if (!result && partial) {
				obuf += partial; //set next sending position
				thistime -= partial; //leaved data length
			} else //error happened
				break;
		};
		if (result) {
			info("Write Whoops - %x", result);
			return -EIO;
		}
		bytes_written += copy_size;
		count -= copy_size;
		buffer += copy_size;
	} while (count > 0);

	return bytes_written ? bytes_written : -EIO;
}

static struct
file_operations usb_lcpd_fops = {
	.owner =	THIS_MODULE,
//	.read =		read_lcpd,
	.write =	write_lcpd,
	//.ioctl =	ioctl_lcpd,
	.unlocked_ioctl =	ioctl_lcpd,
	//.compat_ioctl =		ioctl_lcpd,
	.open =		open_lcpd,
	.release =	close_lcpd,
	.llseek =	 noop_llseek,
};

static struct usb_class_driver usb_lcpd_class = {
	//.name =		"usb/lcpd%d",
	.name =		"lcpd",
	.fops =		&usb_lcpd_fops,
	/*.mode =		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,*/
	.minor_base =	USBLCPD_MINOR,
};

//------------------------------------------------------------------------
static int probe_lcpd(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = usb_get_dev( interface_to_usbdev(intf));


	struct lcpd_usb_data *lcpd = &lcpd_instance;
	int i;
	int retval;
	//get device product ID, Product ID (assigned by the manufacturer)
	//if (dev->descriptor.idProduct != 0xA030  ) {
	//	warn(KERN_INFO "USBLCPD model not supported.");
	//	return NULL;
	//}

	if (lcpd->present == 1) {
		warn(KERN_INFO "Multiple USBLCPDs are not supported!");
		return -ENODEV;
	}
	//Device release number in binary-codeddecimal
	i = dev->descriptor.bcdDevice;

	info("USBLCPD Version %1d%1d.%1d%1d found at address %d",
		(i & 0xF000)>>12,(i & 0xF00)>>8,(i & 0xF0)>>4,(i & 0xF),
		dev->devnum);

	lcpd->present = 1;
	lcpd->lcpd_dev = dev;
	//allocate the memory
	if (!(lcpd->obuf = (char *) kmalloc(OBUF_SIZE, GFP_KERNEL))) {
		info("probe_lcpd: Not enough memory for the output buffer");
		return -ENOMEM;
	}
	info("probe_lcpd: obuf address:%p", lcpd->obuf);

	if (!(lcpd->ibuf = (char *) kmalloc(IBUF_SIZE, GFP_KERNEL))) {
		info("probe_lcpd: Not enough memory for the input buffer");
		kfree(lcpd->obuf);
		return -ENOMEM;
	}
	info("probe_lcpd: ibuf address:%p", lcpd->ibuf);
	//linux 2.6.11 add
	retval = usb_register_dev(intf, &usb_lcpd_class);
	if (retval) {
		info("Not able to get a minor for this device.");
		kfree(lcpd->obuf);
		kfree(lcpd->ibuf);
		return -ENOMEM;
	}

	usb_set_intfdata (intf, lcpd);

	return 0;
}
//------------------------------------------------------------------------
static void disconnect_lcpd(struct usb_interface *intf)
{
	struct lcpd_usb_data *lcpd = usb_get_intfdata (intf);

	//struct lcpd_usb_data *lcpd = (struct lcpd_usb_data *) ptr;
	usb_set_intfdata (intf, NULL);
	if (lcpd) {
		usb_deregister_dev(intf, &usb_lcpd_class);

		if (lcpd->isopen) {
			lcpd->isopen = 0;
			/* better let it finish - the release will do whats needed */
			lcpd->lcpd_dev = NULL;
			return;
		}
		kfree(lcpd->ibuf);
		kfree(lcpd->obuf);

		info("USBLCPD disconnected.");

		lcpd->present = 0;
	}

}
/* * @id_table: USB drivers use ID table to support hotplugging.
 *      Export this with MODULE_DEVICE_TABLE(usb,...), or use NULL to
 *      say that probe() should be called for any unclaimed interface.
 *
 * USB drivers must provide a name, probe() and disconnect() methods,
 * and an id_table.  Other driver fields are optional.
 *
 * The id_table is used in hotplugging.  It holds a set of descriptors,
 * and specialized data may be associated with each entry.  That table
 * is used by both user and kernel mode hotplugging support.
 * The probe() and disconnect() methods are called in a context where
 * they can sleep, but they should avoid abusing the privilege.  Most
 * work to connect to a device should be done when the device is opened,
 * and undone at the last close.  The disconnect code needs to address
 * concurrency issues with respect to open() and close() methods, as
 * well as forcing all pending I/O requests to complete (by unlinking
 * them as necessary, and blocking until the unlinks complete).
 */

//Logic controls PD:
// idVendor: 0x0FA8
// idProduct: 0xA030
/*
static struct usb_device_id id_table [] = {
	{ .idVendor = 0x10D2, .match_flags = USB_DEVICE_ID_MATCH_VENDOR, },
	{},
};
*/
//=======================================================
static struct usb_device_id id_table [] = {
	{USB_DEVICE(0x0FA8,0xA030)},
	{USB_DEVICE(0x0FA8,0xA060)},
	{USB_DEVICE(0x0FA8,0xA090)},
	{USB_DEVICE(0x0FA8,0xA010)},
	{}
};

MODULE_DEVICE_TABLE (usb, id_table);


static struct
usb_driver lcpd_driver = {
	.name =		"usblcpd",
	.probe =	(void *)probe_lcpd,
	.disconnect =	disconnect_lcpd,
	.id_table =	id_table,
//	.minor =	USBLCPD_MINOR, //self define this minor number
};
int usb_lcpd_init(void)
{
	if (usb_register(&lcpd_driver) < 0)
		return -1;

	info("%s (C) Logic Controls, Inc.  http://www.logiccontrols.com", DRIVER_VERSION);
	info("USBLCPD linux driver registered.");
	return 0;
}


void usb_lcpd_cleanup(void)
{
	struct lcpd_usb_data *lcpd = &lcpd_instance;

	lcpd->present = 0;
	usb_deregister(&lcpd_driver);
}

module_init(usb_lcpd_init);
module_exit(usb_lcpd_cleanup);

MODULE_AUTHOR("Logic Controls, Inc.  <www.logiccontrols.com>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
