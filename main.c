#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include "ioc_hw5.h"

MODULE_LICENSE("GPL");

// CONSTS
#define PREFIX_TITLE "OS_AS5"


// DEVICE
#define DEV_NAME "mydev"        // name for alloc_chrdev_region
#define DEV_BASEMINOR 0         // baseminor for alloc_chrdev_region
#define DEV_COUNT 1             // count for alloc_chrdev_region
static int dev_major;
static int dev_minor;
static struct cdev *dev_cdev;


// DMA
#define DMA_BUFSIZE 64
#define DMASTUIDADDR 0x0        // Student ID
#define DMARWOKADDR 0x4         // RW function complete
#define DMAIOCOKADDR 0x8        // ioctl function complete
#define DMAIRQOKADDR 0xc        // ISR function complete
#define DMACOUNTADDR 0x10       // interrupt count function complete
#define DMAANSADDR 0x14         // Computation answer
#define DMAREADABLEADDR 0x18    // READABLE variable for synchronize
#define DMABLOCKADDR 0x1c       // Blocking or non-blocking IO
#define DMAOPCODEADDR 0x20      // data.a opcode
#define DMAOPERANDBADDR 0x21    // data.b operand1
#define DMAOPERANDCADDR 0x25    // data.c operand2
void *dma_buf;


// Declaration for file operations
static ssize_t drv_read(struct file *filp, char __user *buffer, size_t, loff_t*);
static int drv_open(struct inode*, struct file*);
static ssize_t drv_write(struct file *filp, const char __user *buffer, size_t, loff_t*);
static int drv_release(struct inode*, struct file*);
static long drv_ioctl(struct file *, unsigned int , unsigned long );

// cdev file_operations
static struct file_operations fops = {
      owner: THIS_MODULE,
      read: drv_read,
      write: drv_write,
      unlocked_ioctl: drv_ioctl,
      open: drv_open,
      release: drv_release,
};

// in and out function
void myoutc(unsigned char data,unsigned short int port);

void myouts(unsigned short data,unsigned short int port);

void myouti(unsigned int data,unsigned short int port);

unsigned char myinc(unsigned short int port);

unsigned short myins(unsigned short int port);

unsigned int myini(unsigned short int port);

// Work routine
static struct work_struct *work;

// For input data structure
struct DataIn {
    char a;
    int b;
    short c;
} *dataIn;


// Arithmetic funciton
static void drv_arithmetic_routine(struct work_struct* ws);

//irq_handler_t handler;
int interrupt_cnt = 0;
static irqreturn_t  mydev_interrupt(int irq, void *dev_id){
    interrupt_cnt++;
    myouti(1, DMAIRQOKADDR);
    return IRQ_HANDLED;
}

// Input and output data from/to DMA
void myoutc(unsigned char data,unsigned short int port) {
    *(volatile unsigned char*)(dma_buf+port) = data;
}

void myouts(unsigned short data,unsigned short int port) {
    *(volatile unsigned short*)(dma_buf+port) = data;
}

void myouti(unsigned int data,unsigned short int port) {
    *(volatile unsigned int*)(dma_buf+port) = data;
}

unsigned char myinc(unsigned short int port) {
    return *(volatile unsigned char*)(dma_buf+port);
}

unsigned short myins(unsigned short int port) {
    return *(volatile unsigned short*)(dma_buf+port);
}

unsigned int myini(unsigned short int port) {
    return *(volatile unsigned int*)(dma_buf+port);
}

int prime(int base, short nth)
{
    int fnd=0;
    int i, num, isPrime;

    num = base;
    while(fnd != nth) {
        isPrime=1;
        num++;
        for(i=2;i<=num/2;i++) {
            if(num%i == 0) {
                isPrime=0;
                break;
            }
        }
        if(isPrime) {
            fnd++;
        }
    }
    return num;
}


static int drv_open(struct inode* ii, struct file* ff) {
	try_module_get(THIS_MODULE);
    printk("%s:%s(): device open\n", PREFIX_TITLE, __func__);
	return 0;
}

static int drv_release(struct inode* ii, struct file* ff) {
	module_put(THIS_MODULE);
    printk("%s:%s(): device close\n", PREFIX_TITLE, __func__);
	return 0;
}

static ssize_t drv_read(struct file *filp, char __user *buffer, size_t ss, loff_t* lo) {
	/* Implement read operation for your device */
    while (myini(DMAREADABLEADDR) != 1){
        msleep(5000);
    }
    int ans;
    ans = myini(DMAANSADDR);
    printk("%s:%s(): ans = %d\n", PREFIX_TITLE, __func__, ans);

    put_user(ans, (int *) buffer);

    myouti(1, DMARWOKADDR);

	return 0;
}

static ssize_t drv_write(struct file *filp, const char __user *buffer, size_t ss, loff_t* lo) {
	/* Implement write operation for your device */

	struct DataIn data;
	get_user(data.a, (char *)buffer);
    get_user(data.b, (int *)buffer+1);
    get_user(data.c, (int *)buffer+2);

    myoutc(data.a, DMAOPCODEADDR);
    myouti(data.b, DMAOPERANDBADDR);
    myouts(data.c, DMAOPERANDCADDR);

    int IOMode;
    IOMode = myini(DMABLOCKADDR);

    INIT_WORK(work, drv_arithmetic_routine);

    myouti(0, DMAREADABLEADDR);

    // Decide io mode
    if(IOMode) {
        // Blocking IO
        printk("%s:%s(): queue work\n", PREFIX_TITLE, __func__);
        schedule_work(work);
        printk("%s:%s(): block\n", PREFIX_TITLE, __func__);
        flush_scheduled_work();
    }
    else {
        // Non-locking IO
        printk("%s:%s(): queue work\n", PREFIX_TITLE, __func__);
        schedule_work(work);
    }

    myouti(1, DMARWOKADDR);

	return 0;
}

static long drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	/* Implement ioctl setting for your device */
    int value;
	get_user(value, (int *) arg);

	if (cmd == HW5_IOCSETSTUID){ // Set student ID: printk your student ID
        myouti(value, DMASTUIDADDR);
        printk("%s:%s(): Stu ID %d", PREFIX_TITLE, __func__, value);
    }

	else if (cmd == HW5_IOCSETRWOK){ // Set if RW OK: printk OK if you complete R/W function
        myouti(value, DMARWOKADDR);
        printk("%s:%s(): RW OK\n", PREFIX_TITLE, __func__);
	}

	else if (cmd == HW5_IOCSETIOCOK){ // Set if ioctl OK: printk OK if you complete ioctl function
        myouti(value, DMAIOCOKADDR);
        printk("%s:%s(): IOC OK\n", PREFIX_TITLE, __func__);
	}

	else if (cmd == HW5_IOCSETIRQOK){ // Set if IRQ OK: printk OK if you complete bonus
        myouti(value, DMAIRQOKADDR);
        printk("%s:%s(): IRQ OK\n", PREFIX_TITLE, __func__);
	}

	else if (cmd == HW5_IOCSETBLOCK){ // Set blocking or non-blocking: set write function mode
        if (value == 1) printk("%s:%s(): Blocking IO\n", PREFIX_TITLE, __func__);
        else printk("%s:%s(): Non blocking IO\n", PREFIX_TITLE, __func__);
        myouti(value, DMABLOCKADDR);
	}

	else if (cmd == HW5_IOCWAITREADABLE){ // Wait if readable now (synchronize function)
	    while (myini(DMAREADABLEADDR) == 0){
//            printk("%s:%s(): Not readable yet", PREFIX_TITLE, __func__);
            msleep(5000);
	    }
        printk("%s:%s(): Computation completed\n", PREFIX_TITLE, __func__);
        put_user(1, (int *) arg);
	}

    myouti(1, DMAIOCOKADDR);
	return 0;
}

static void drv_arithmetic_routine(struct work_struct* ws) {
	/* Implement arthemetic routine */
	int ans;
	struct DataIn data;

	data.a = myinc(DMAOPCODEADDR);
	data.b = myini(DMAOPERANDBADDR);
	data.c = myins(DMAOPERANDCADDR);

    switch(data.a) {
        case '+':
            ans=data.b+data.c;
            break;
        case '-':
            ans=data.b-data.c;
            break;
        case '*':
            ans=data.b*data.c;
            break;
        case '/':
            ans=data.b/data.c;
            break;
        case 'p':
            ans = prime(data.b, data.c);
            break;
        default:
            ans=0;
    }

    printk("%s:%s():%d %c %d = %d\n", PREFIX_TITLE, __func__, data.b, data.a, data.c, ans);

    myouti(ans, DMAANSADDR);

    myouti(1, DMAREADABLEADDR);

}

static int __init init_modules(void) {


    dev_t dev;

    printk("%s:%s():...............Start...............\n", PREFIX_TITLE, __func__);

    dev_cdev = cdev_alloc();

    /* Register chrdev */
    if(alloc_chrdev_region(&dev, DEV_BASEMINOR, DEV_COUNT, DEV_NAME) < 0) {
        printk(KERN_ALERT"Register chrdev failed!\n");
        return -1;
    } else {
        printk("%s:%s(): register chrdev(%i,%i)\n", PREFIX_TITLE, __func__, MAJOR(dev), MINOR(dev));
    }

    dev_major = MAJOR(dev);
    dev_minor = MINOR(dev);

    /* Init cdev and make it alive */
    dev_cdev->ops = &fops;
    dev_cdev->owner = THIS_MODULE;

    if(cdev_add(dev_cdev, dev, 1) < 0) {
        printk(KERN_ALERT"Add cdev failed!\n");
        return -1;
    }


    /* Allocate DMA buffer */
    dma_buf = kzalloc(DMA_BUFSIZE, GFP_KERNEL);
    printk("%s:%s():allocate dma buffer\n", PREFIX_TITLE, __func__);


    /* Allocate work routine */
    work = kmalloc(sizeof(typeof(*work)), GFP_KERNEL);
    printk("%s:%s():allocate work routine\n", PREFIX_TITLE, __func__);


    /* Bonus part*/
    int ret = request_irq(1, mydev_interrupt, IRQF_SHARED, "BonusDev", (void *) &dev_cdev);
    printk("%s:%s: request_irq %d return %d\n", PREFIX_TITLE, __func__, 1, ret);


	return 0;
}

static void __exit exit_modules(void) {

    dev_t dev;

    dev = MKDEV(dev_major, dev_minor);
    cdev_del(dev_cdev);


	/* Free DMA buffer when exit modules */
    kfree(dma_buf);
    printk("%s:%s(): free dma\n", PREFIX_TITLE, __func__);

    /* Delete character device */
    cdev_del(dev_cdev);


	/* Free work routine */
    kfree(work);
    printk("%s:%s(): unregister chrdev\n", PREFIX_TITLE, __func__);
    printk("%s:%s():..............End..............\n", PREFIX_TITLE, __func__);

    /* BonusPart */
    free_irq(1, (void *) &dev_cdev);
    printk("%s:%s():interrupt count = %d\n", PREFIX_TITLE, __func__, interrupt_cnt);

}

module_init(init_modules);
module_exit(exit_modules);
