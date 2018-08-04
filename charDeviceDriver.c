/*
 *  chardev.c: Creates a read-only char device that says how many times
 *  you've read from the dev file
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <asm/uaccess.h>	/* for put_user */
#include <charDeviceDriver.h>

MODULE_LICENSE("GPL");

DEFINE_MUTEX  (devLock);

typedef struct node{
  char* data;
  size_t size;
  struct node* next;
} node;

typedef struct queue{
  node * head;
  node * tail;
} queue;


static struct queue *q;
static ssize_t qSize = 0;


int pop(queue * l,char** data, size_t* size){
	struct node *currentNode;
	mutex_lock (&devLock);
	currentNode = l->head;
	if(l->head != NULL){
		l->head = currentNode->next;
		qSize -= currentNode->size;
		mutex_unlock (&devLock);
		*size = currentNode->size;
		*data = currentNode->data;
		kfree(currentNode);
		return 0;
	} else {
		mutex_unlock (&devLock);
		return -1;
	}
}

int push(queue * l, char* data, size_t len){
	struct node* newNode;
	//Make new Node
	newNode = (node*)kmalloc(sizeof(struct node),GFP_KERNEL);
	if(newNode == NULL){
		return -1;
	}
	memset(newNode,0,sizeof(struct node));
	newNode->data = data;
	newNode->size = len;
	newNode->next = NULL;
	mutex_lock (&devLock);
	// list empty
	if (l->head == NULL) {
		l->head = newNode;
		l->tail = newNode;
	} //List Not empty 
	else {
		l->tail->next = newNode;
		l->tail = newNode;
	}
	qSize += len;
	mutex_unlock (&devLock);
	return 0;
}

void init(queue * l){
  l->head = NULL;
  l->tail = NULL;
}

void destroy(queue *l){
  node* next = l->head;
  node* toFree;
  while (next != NULL) {
    toFree = next;
    next = next->next;
    kfree(toFree);
  }
  kfree(l);
  return;
}

static long device_ioctl(struct file *file,	/* see include/linux/fs.h */
		 unsigned int ioctl_num,	/* number and param for ioctl */
		 unsigned long ioctl_param)
{
	if (ioctl_num == 0) {
		if (ioctl_param > maxSize || ioctl_param > qSize) {
				mutex_lock (&devLock);
				maxSize = ioctl_param;
				mutex_unlock (&devLock);
				return 0;
		
		}
	} 
	return -EINVAL;
}


/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
    Major = register_chrdev(0, DEVICE_NAME, &fops);

	if (Major < 0) {
	  printk(KERN_ALERT "Registering char device failed with %d\n", Major);
	  return Major;
	}

	printk(KERN_INFO "I was assigned major number %d. To talk to\n", Major);
	printk(KERN_INFO "the driver, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
	q = (queue*) 
	kmalloc(sizeof(struct queue),GFP_KERNEL);
	if (q == NULL) {
		printk(KERN_ALERT "Failed to initialise module\n");
		return -EAGAIN;
	}
	init(q);
	return 0;
}

/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
	destroy(q);
	/*  Unregister the device */
	unregister_chrdev(Major, DEVICE_NAME);
}

/*
 * Methods
 */

/* 
 * Called when a process tries to open the device file, like
 * "cat /dev/mycharfile"
 */
static int device_open(struct inode *inode, struct file *file)
{
    
    try_module_get(THIS_MODULE);
    
    return 0;
}

/* Called when a process closes the device file. */
static int device_release(struct inode *inode, struct file *file)
{
    module_put(THIS_MODULE);

	return 0;
}

/* 
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t device_read(struct file *filp,	/* see include/linux/fs.h   */
			   char *buffer,	/* buffer to fill with data */
			   size_t length,	/* length of the buffer     */
			   loff_t * offset)
{
	
	char* msg;
	size_t size;
	if (pop(q,&msg, &size) == -1) {
		return -EAGAIN;
	}
	
	if (size < length) {
		if (copy_to_user(buffer,msg,size))
			return -EFAULT;
		return size;
	} else {
		if (copy_to_user(buffer,msg,length))
			return -EFAULT;
		return length;
	}

}

/* Called when a process writes to dev file: echo "hi" > /dev/hello  */
static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	char* msg = kmalloc(len,GFP_KERNEL);
	if(msg == NULL){
		return -1;
	}
	if (len > maxMsgSize) 
		return -EINVAL;
	if (qSize + len > maxSize)
		return -EAGAIN;
	if (copy_from_user(msg, buff, len))
		return -EFAULT;
	if (push(q,msg,len) == -1) {
		return -EFAULT;
	}
	return len;
    
}
