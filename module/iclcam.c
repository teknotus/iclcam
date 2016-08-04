#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/usb.h>
MODULE_LICENSE("GPL");

#define MAGIC 'i'
#define GET_TEMP _IO(MAGIC, 0)
#define GET_RESULT _IO(MAGIC, 1)

//static int howmany = 1;
//module_param(howmany, int, S_IRUGO);
//static dev_t dev;

//struct usb_device_id ivcam = USB_DEVICE(0x8086,0x0a66);
//ivcam.bInterfaceClass = 0xff; //FIXME
static const struct usb_device_id iclcam_table [] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x8086, 0x0a66, 0xff, 0x00, 0x00) },
	{}
};
MODULE_DEVICE_TABLE (usb, iclcam_table);

/*
 *  Keep track of /dev/cal# number allocations and such
struct iclcam_common{

};
*/
// FIXME Implement this... Use linked list?

struct iclcam_device {
	dev_t			dev_mm;
	struct device		sdev;
	struct usb_device	*udev;
//	struct usb_interface	*interface;
	struct cdev		cdev;
	__u8			bulk_out;
	__u8			bulk_in;
	size_t			bulk_in_size;
	unsigned char		*bulk_in_buffer;
	size_t			in_filled;
	spinlock_t		lock;
	struct mutex		mlock;
	bool			reading;
	wait_queue_head_t	in_q;
//	struct kref		kref; //FIXME?
};

static struct class video_calibration = {
	.name =	"kalibrate", // FIXME better name
};

int iclcam_open(struct inode * inode, struct file * filp)
{
	struct iclcam_device *dev;
	printk("iclcam: opened\n");
	dev = container_of(inode->i_cdev, struct iclcam_device, cdev);
	filp->private_data = dev;
	return 0;
}

int iclcam_release(struct inode * inode, struct file * filp)
{
	printk("iclcam: closed\n");
	return 0;
}
static void bulk_read_callback(struct urb *urb)
{
	struct iclcam_device *dev;
	dev = urb->context;
	printk("iclcam urb bulkread callback\n");
	if(urb->status){
		printk("urb status %d\n",urb->status);
	}
	printk("hello callback actual size %d\n", urb->actual_length);
	if(urb->actual_length >= 5)
	{
		printk("temperature: %d,%d,%d,%d,%d\n",
		       dev->bulk_in_buffer[0],
		       dev->bulk_in_buffer[1],
		       dev->bulk_in_buffer[2],
		       dev->bulk_in_buffer[3],
		       dev->bulk_in_buffer[4]
			);
	}
	spin_lock(&dev->lock);
	dev->in_filled = urb->actual_length;
	dev->reading = false;
	spin_unlock(&dev->lock);
	wake_up_interruptible(&dev->in_q);
}

int bulk_read(struct file *filp)
{
	int ret, lock_int;
	unsigned long flags;
	bool reading;
	struct iclcam_device *dev;
	struct urb *urb;
	dev = filp->private_data;
	lock_int = mutex_lock_interruptible(&dev->mlock);
	if(lock_int < 0){
		//FIXME real return value?
		return -EBUSY;
	}
	spin_lock_irqsave(&dev->lock, flags);
	reading = dev->reading;
	spin_unlock_irqrestore(&dev->lock, flags);
	if(!reading){
//		return -EBUSY;
	
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if(!urb)
		{
			printk("no got urb... Craptastic!\n");
			//FIXME Grrrr!
			goto exit;
		}
		usb_fill_bulk_urb(urb, dev->udev, usb_rcvbulkpipe(dev->udev, dev->bulk_in), dev->bulk_in_buffer, dev->bulk_in_size, bulk_read_callback, dev);
		//urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		spin_lock_irqsave(&dev->lock, flags);
		dev->reading = true;
		dev->in_filled = 0;
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret < 0)
		{
			printk("urb read sub death\n");
			spin_lock_irqsave(&dev->lock, flags);
			dev->reading = false;
			spin_unlock_irqrestore(&dev->lock, flags);
			//FIXME
			goto exit;
		}
		usb_free_urb(urb);
	}
	ret = wait_event_interruptible(dev->in_q, (!dev->reading));
	if(ret < 0){
		//FIXME What should I do here?
		printk("Woken up with negative status\n");
		goto exit;
	}
	printk("something woke me up? Maybe I have data?\n");
exit:
	mutex_unlock(&dev->mlock);
	if(dev->in_filled >= 5){
		return dev->bulk_in_buffer[4];
	} else {
		return 0;
	}
}

static void get_temp_callback(struct urb *urb)
{
	printk("iclcam urb callback!\n");
	usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
}

void get_temp(struct file *filp)
{
	int ret;
	unsigned char data[24] = {
		0x14, 0x00, 0xab, 0xcd, 0x52, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	struct iclcam_device *dev;
	struct urb *urb;
	unsigned char *buf;
	dev = filp->private_data;
	printk("iclcam: get_temp function\n");
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!urb)
	{
		printk("urb sky is falling!\n");
		//FIXME handle errors
		return;
	}
	buf = usb_alloc_coherent(dev->udev, 24, GFP_KERNEL, &urb->transfer_dma);
	if(!buf){
		printk("Oh crap couldn't get dma buffer\n");
		//FIXME handle errors
		return;
	}
	memcpy(buf, data, 24); // USE memset smaller copy instead?
	printk("buf[] %d,%d,%d,%d,%d,%d\n",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
	usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->bulk_out), buf, 24, get_temp_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret){
		printk("iclcam my urb submit died, now I'm sad\n");
		//FIXME handle errrrrr0rs
	}
	usb_free_urb(urb);
//	printk("iclcam: bulk_out: %d\n", dev->bulk_out);
}

long iclcam_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct iclcam_device *dev;
	dev = filp->private_data;
	printk("iclcam: ioctl\n");
	printk("iclcam: bulk_out: %d\n", dev->bulk_out);
	switch (cmd) {
	case GET_TEMP:
		printk("iclcam: get temp switch\n");
		get_temp(filp);
		break;
	case GET_RESULT:
		printk("iclcam: get result\n");
		return bulk_read(filp);
		break;
	default:
		printk("iclcam: ioctl this shouldn't happen!");
		break;
	}
	return 0;
}


static const struct file_operations iclcam_fops = {
	.owner = THIS_MODULE,
	.open = iclcam_open,
	.release = iclcam_release,
	.unlocked_ioctl = iclcam_ioctl,
};
/*
  static ssize_t hello_show_dev(struct device *sdev, struct device_attribute *attr, char *buf)
  {
  struct usb_interface *interface = container_of(sdev, struct usb_interface, dev);
  struct iclcam_device * dev = usb_get_intfdata(interface);
  return print_dev_t(buf, dev->cdev.dev);
  }

  static DEVICE_ATTR(dev, S_IRUGO, hello_show_dev, NULL);
*/
static int iclcam_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
        struct usb_host_interface * hi;
	int i, ret;
	//struct cdev * hello_cdev;
	struct iclcam_device * dev;
	dev = kzalloc(sizeof(struct iclcam_device), GFP_KERNEL);
	if(!dev){
		printk("iclcam: probe couldn't allocate memory!\n");
		goto error;
	}
	dev->udev = usb_get_dev(interface_to_usbdev(interface)); //TODO can usb_get_dev fail?
	//dev->interface = interface;
	printk(KERN_ALERT "iclcam: iclcam probe\n");
	printk("iclcam interface use count %d\n", atomic_read(&interface->pm_usage_cnt));
        hi = interface->cur_altsetting;
	printk("iclcam bNumEndpoints: %d\n", hi->desc.bNumEndpoints);
	for(i = 0 ; i < hi->desc.bNumEndpoints ; i++)
	{
		struct usb_endpoint_descriptor * ed;
		ed = &hi->endpoint[i].desc;
		if(usb_endpoint_is_bulk_out(ed))
		{
			printk("%d is bulk out\n",ed->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
			dev->bulk_out = ed->bEndpointAddress;
		}
		else if(usb_endpoint_is_bulk_in(ed))
		{
			printk("%d is bulk in\n", ed->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
			dev->bulk_in = ed->bEndpointAddress;
			dev->bulk_in_size = ed->wMaxPacketSize;
			dev->bulk_in_buffer = kzalloc(dev->bulk_in_size, GFP_KERNEL);
			if(!dev->bulk_in_buffer){
				printk(KERN_ALERT "iclcam: bulk in alloc super fail!!!!!\n");
				//FIXME error recovery
			}
		}
	}
	ret = alloc_chrdev_region(&dev->dev_mm, 0, 1, "hello");
	printk("iclcam dev_t: %d Major: %d Minor: %d\n", (int)dev->dev_mm, MAJOR(dev->dev_mm), MINOR(dev->dev_mm)); 
	//return -ENODEV;
	//hello_cdev = cdev_alloc();
	//hello_cdev->ops = &iclcam_fops;
	cdev_init(&dev->cdev, &iclcam_fops);
	ret = cdev_add(&dev->cdev, dev->dev_mm, 1);
	if(ret < 0)
	{
		printk("iclcam: could not add cdev\n");
		return ret;
	}
	dev->cdev.owner = THIS_MODULE; // FIXME should this be before cdev_add?
	usb_set_intfdata(interface, dev);
	spin_lock_init(&dev->lock);
	dev->reading = false;
	mutex_init(&dev->mlock);
	init_waitqueue_head(&dev->in_q);
	//device_create_file(&interface->dev, &dev_attr_dev);
	dev->sdev.class = &video_calibration;
	dev->sdev.devt = dev->dev_mm;
	dev->sdev.parent = &interface->dev;
	dev_set_name(&dev->sdev, "awesome"); //FIXME need to make this unique when multiple devices
	ret = device_register(&dev->sdev);
	if(ret < 0){
		printk("iclcam could not register device!!!\n");
		goto error; // TODO do I need to do anything special here?
	}
	return 0;
error:
	printk("iclcam: probe something blew up\n");
	usb_put_dev(dev->udev);
	kfree(dev);
	return -ENODEV;
}

static void iclcam_disconnect(struct usb_interface *interface)
{
	struct iclcam_device * dev;
	printk("iclcam: disconnect\n");
	dev = usb_get_intfdata(interface);
	unregister_chrdev_region(dev->dev_mm, 1);
	printk("iclcam interface use count %d\n", atomic_read(&interface->pm_usage_cnt));
	cdev_del(&dev->cdev);
	kfree(dev->bulk_in_buffer);
	usb_put_dev(dev->udev);
	kfree(dev);
}

static struct usb_driver iclcam_driver = {
	.name = "iclcam",
	.probe = iclcam_probe,
	.disconnect = iclcam_disconnect,
	.id_table = iclcam_table,
};


static int __init hello_init(void)
{
	int ret;//0,ret1;
	printk(KERN_ALERT "Kernel I'm in you!\n");
	printk("iclcam registering class\n");
	ret = class_register(&video_calibration);
	if(ret < 0){
		printk(KERN_ALERT "iclcam eeek could not class!\n");
		return -EIO;
	}
	printk("iclcam registering with USB\n");
	ret = usb_register(&iclcam_driver);
	if(ret){
		printk("iclcam something failed\n");
	}
	return ret;
}

static void __exit hello_exit(void)
{
	usb_deregister(&iclcam_driver);
	class_unregister(&video_calibration);
	printk(KERN_ALERT "I'm bored going home!\n");
}

module_init(hello_init);
module_exit(hello_exit);
