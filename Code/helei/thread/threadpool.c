/*************************************************************************
	> File Name: threadpool.c
	> Author: 
	> Mail: 
	> Created Time: 2020年06月05日 星期五 22时28分52秒
 ************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<pthread.h>
#include<signal.h>
#include<errno.h>
#include<assert.h>

#define DEFAULT_TIME  10      // 10s检测一次
#define MIN_WAIT_TASK_NUM 10 //如果queue_size > MIN_WAIT_TASK_NUM 添加新的线程到线程池
#define DEFAULT_THREAD_VARY 10    //每次创建和销毁线程的个数
#define true 1
#define false 0

typedef struct 
{
    void *(*function)(void * ); //函数指针，回调函数
    void * arg;//上面函数的参数
}threadpool_task_t; // 各子线程任务结构体

//描述线程池相关信息
typedef struct threadpool_t 
{
    pthread_mutex_t lock;    //用于锁住本结构体，和条件变量一起使用
    pthread_mutex_t thread_counter; //记录忙状态线程个数的锁
    pthread_cond_t queue_not_full; //当任务队列满时，添加任务的线程阻塞，等待此条件变量
    pthread_cond_t queue_not_empty; //任务队列里不为空时，通知线程池中等待任务的线程

    pthread_t * threads; //存放线程池中每个线程的tid 数组
    pthread_t adjust_tid; // 存放管理线程tid
    threadpool_task_t *task_queue; //任务队列
    int min_thr_num;              //线程池最小线程数
    int max_thr_num;             //线程池最大线程数
    int live_thr_num;           //当前存活线程个数
    int busy_thr_num;           //忙状态线程个数
    int wait_exit_thr_num;      //要销毁的线程个数

    int queue_front;           //task_queue 队头下标
    int queue_rear;            //           队尾下标
    int queue_size;            //           队中实际任务数
    int queue_max_size;        //           队列可容纳任务数上限
           
    int shutdown;              //           标志位，线程池使用状态，true或false
}threadpool_t;

//    最重要的是线程函数，里面包含主要步骤
//    当无任务进入，并且不关闭线程池的条件下，去阻塞等待，直到队列不为空  
//  ->判断是否有需要销毁的线程
//  ->判断是否需要关闭线程池
//  ->从任务队列中获取任务
//  ->对执行完的任务进行出队
//  ->告知其他线程任务队列不为满，可以添加新任务
//  ->执行任务


//对于条件变量相关的函数
//检测条件变天个cond，如果cond没有被设置，表示条件还不满足，别人还没有对con进行设置，此时pthread_cond_wait会进行休眠阻塞,直到别的线程设置cond表示条件准备好后，才会被休眠
//当调用pthread_cond_wait函数等待条件满足的线程只有一个时，就用pthread_cond_signal来唤醒，如果说有好多线程都调用pthread_cond_wait等待时，用pthread_cond_broadcast,它可将所有调用pthread_cond_wait而休眠的线程都唤醒，
//
//锁会与sleep产生冲突


void * threadpool_thread(void *threadpool);  //线程池中工作线程要做的事情
void *adjust_thread(void * threadpool);     //管理者线程要做的事情
int threadpool_free(threadpool_t * pool);   //销毁
threadpool_t * threadpool_create(int min_thr_num,int max_thr_num,int queue_max_size)
{
    int i;
    threadpool_t * pool = NULL;
    do
    {
        if((pool = ( threadpool_t * )malloc(sizeof(threadpool_t))) == NULL)
        {
            printf("malloc threadpool fail");
            break;    //跳出do while
        }
        pool->min_thr_num = min_thr_num ;   //线程池最小线程数
        pool->max_thr_num = max_thr_num ;   //线程池最大线程数
        pool->busy_thr_num = 0;             //忙状态的线程数初始值为0
        pool->live_thr_num = min_thr_num;   //活着的线程数，初值= 最小线程数
        pool->wait_exit_thr_num = 0;        //要销毁的线程个数初始值也初始为0

        pool->queue_size = 0;               //任务队列实际元素个数初始值为0
        pool->queue_max_size = queue_max_size;  //任务队列最大元素个数
        pool->queue_front = 0;
        pool->queue_rear = 0;
        pool->shutdown = false;     //不关闭线程池

        //根据最大线程上限数，给工作线程数组开辟空间，并归零
        pool->threads = (pthread_t *)malloc(sizeof(pthread_t)*max_thr_num );
        //线程池中线程最大个数
        if(pool->threads == NULL)
        {
            printf("malloc threads fail");
            break;
        }
        memset(pool->threads,0,sizeof(pthread_t) * max_thr_num);

        //队列开辟空间
        pool->task_queue = (threadpool_task_t *)malloc(sizeof(threadpool_task_t) * queue_max_size);
        if(pool->task_queue == NULL)
        {
            printf("malloc task_queue fail");
            break;

        }
        //初始化互斥锁，条件变量
        if(pthread_mutex_init(&(pool->lock),NULL) !=0 ||
          pthread_mutex_init(&(pool->thread_counter),NULL) !=0 ||
          pthread_cond_init(&(pool->queue_not_empty),NULL) != 0 ||
          pthread_cond_init(&(pool->queue_not_full),NULL) != 0)
        {
            printf("init the lock or cond fail");
            break;

        }
        //启动min_thr_num 个work thread
        for(i = 0;i < min_thr_num;i++)
        {
            pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void *)pool);     //pool指向当前线程池
            printf("start thread 0x%x...\n",(unsigned int )pool->threads[i]);//打印线程id

        }
        pthread_create(&(pool->adjust_tid),NULL,adjust_thread,(void *)pool);
        //启动管理者线程
        pthread_detach(pool->adjust_tid);//把管理线程设置为分离的，系统帮助回收资源
        printf("***************dajust_thread start******************\n");
        sleep(1); //等待线程创建完成再回到主函数中
        return pool;
    }while(0);
    threadpool_free(pool); //前面代码调用失败时，释放pool存储空间
    return NULL;
}
//线程池中各个工作线程
void * threadpool_thread(void * threadpool)
{
    threadpool_t * pool = ( threadpool_t * )threadpool;
    threadpool_task_t task;

    while(true)
    {
        //刚创建出线程，等待队列里有任务，否则阻塞等待任务队列里有任务后在唤醒接受任务
        pthread_mutex_lock(&(pool->lock));
        //queue_size == 0 说明没有任务，调wait阻塞在条件变量上，若有任务，跳过while
        while((pool->queue_size == 0) && (!pool->shutdown)) //线程池没有任务且不关闭线程池
        {
            printf("thread 0x%x is waiting\n",(unsigned int)pthread_self());
            pthread_cond_wait(&(pool->queue_not_empty),&(pool->lock)); //线程阻塞在这个条件变量上
            //清楚指定书目的空闲线程，如果要结束的线程个数大于0，结束线程
            if(pool->wait_exit_thr_num > 0) //要销毁的线程个数大于0
            {
                pool->wait_exit_thr_num --;
                //如果线程池里线程个数大于最小值时可以结束当前线程
                if(pool->live_thr_num > pool->min_thr_num )
                {
                    printf("thread 0x%x is exiting\n",(unsigned int )pthread_self());
                    pool->live_thr_num --;
                    pthread_mutex_unlock(&(pool->lock));
                    pthread_exit(NULL);
                }
            }
        }
        //如果指定为true，要关闭线程池里每个线程，自行退出处理
        if(pool->shutdown)
        {
            pthread_mutex_unlock(&(pool->lock));
            printf("thread 0x%x is exiting\n",(unsigned int)pthread_self());
            pthread_exit(NULL); //线程自行结束
        }
        //从任务队列里获取任务，是一个出队操作
        task.function = pool->task_queue[pool->queue_front].function;
        task.arg = pool->task_queue[pool->queue_front].arg;
        pool->queue_front = ( pool->queue_front + 1 ) % pool->queue_max_size;
        //对头指针向后移动
        pool->queue_size --;
        //任务队列中出了一个元素，还有位置，唤醒阻塞在这个条件变量上的线程。现在通知可以有新的任务添加进来
        pthread_cond_broadcast(&(pool->queue_not_full)); //queue_not_full 另一个条件变量
        //任务取出后，立即将线程池锁解锁
        pthread_mutex_unlock(&(pool->lock));

        //执行任务
        printf("thread 0x%x start working\n",(unsigned int)pthread_self());
        pthread_mutex_lock(&(pool->thread_counter)); //忙状态线程数变量锁
        pool->busy_thr_num ++;                       //忙状态线程数+1
        pthread_mutex_unlock(&(pool->thread_counter));
        (task.function)(task.arg);    //执行回调函数任务，相当于process（arg）

        //任务结束处理
        printf("thread 0x%x end working\n\n",(unsigned int )pthread_self());
        usleep(10000);
        pthread_mutex_lock(&(pool->thread_counter));
        pool->busy_thr_num --;          //处理掉一个任务，忙状态线程数-1
        pthread_mutex_unlock(&(pool->thread_counter));
    }
    pthread_exit(NULL);
}

int is_thread_alive(pthread_t tid)
{
    int kill_rc = pthread_kill(tid,0); //发0号信号，测试线程是否存活
    if(kill_rc == ESRCH)
    {
        return false;

    }
    return true;

}
void * adjust_thread(void * threadpool)
{
    int i;
    threadpool_t * pool = ( threadpool_t *)threadpool;
    while(!(pool->shutdown))    //线程池没有关闭
    {  
        sleep(DEFAULT_TIME); //定时对线程池管理
        printf("10s is finish,start test thread pool\n");
        pthread_mutex_lock(&(pool->lock));
        int queue_size = pool->queue_size;     //关注任务数
        int live_thr_num = pool->live_thr_num; //存活线程数
        pthread_mutex_unlock(&(pool->lock));
        pthread_mutex_lock(&(pool->thread_counter));
        int busy_thr_num = pool->busy_thr_num; //忙着的线程数
        printf("busy_thr_num is %d\n",busy_thr_num);
        pthread_mutex_unlock(&(pool->thread_counter));
        //创建新线程，算法： 任务书大于最小线程池个数，且存活的线程数少于最大线程个数时。如30>=10 && 40 < 100
        if(queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num)
        {
            printf("create nuw thread\n");
            pthread_mutex_lock(&(pool->lock));
            int add = 0;
            //一次增加DEFAULT_THREAD个线程
            for(i = 0;i< pool->max_thr_num && add < DEFAULT_THREAD_VARY &&pool->live_thr_num < pool->max_thr_num ; i++)
            {
                if(pool->threads[i] == 0 || !is_thread_alive(pool->threads[i]))
                {
                    pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void *)pool);
                    add ++;
                    pool->live_thr_num;
                }
            }
            pthread_mutex_unlock(&(pool->lock));

        }
        //销毁多余的空闲线程，忙线程×2 小于存货的线程数，且存活的线程数大于最小线程数时
        if((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num )
        {
            printf("delete pthread\n");
            //一次销毁DEFAULT_THREAD个线程。随机10个即可
            pthread_mutex_lock(&(pool->lock));
            pool->wait_exit_thr_num = DEFAULT_THREAD_VARY; //要销毁的线程数设为10
            pthread_mutex_unlock(&(pool->lock));

            for(i = 0;i< DEFAULT_THREAD_VARY; i++)
            {
                //通知处在空闲状态的线程，他们会自行终止
                pthread_cond_signal(&(pool->queue_not_empty));
            }
        }
    }
    return NULL;
}

//向任务队列中，添加一个任务
int threadpool_add(threadpool_t * pool,void *(*function) (void * arg),void *arg)
{
    pthread_mutex_lock(&(pool->lock));
    //为真，队列已经满，调wait阻塞
    while((pool->queue_size == pool->queue_max_size) && (!pool->shutdown))
    {
        pthread_cond_wait(&(pool->queue_not_full),&(pool->lock));
    }

    if(pool->shutdown)
    {
        pthread_mutex_unlock(&(pool->lock));

    }
    //清空工作线程，调用的灰调函数的参数arg
    if(pool->task_queue[pool->queue_rear].arg != NULL)
    {
        free(pool->task_queue[pool->queue_rear].arg);
        pool->task_queue[pool->queue_rear].arg = NULL;
    }
    //添加任务到任务队列里
    pool->task_queue[pool->queue_rear].function = function;
    pool->task_queue[pool->queue_rear].arg = arg;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_max_size;
    pool->queue_size ++;

    //添加完任务后，队列不为空，唤醒阻塞在为空的那个条件变量上中的线程
    pthread_cond_signal(&(pool->queue_not_empty));
    pthread_mutex_unlock(&(pool->lock));
    return 0;
}

int threadpool_free(threadpool_t * pool)
{
    if(pool == NULL)
    {
        return -1;
    }
    if(pool->task_queue)    //释放任务队列
    {
        free(pool->task_queue);
    }
    if(pool->threads) // 书房各种锁
    {
        free(pool->threads);
        pthread_mutex_unlock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_mutex_unlock(&(pool->thread_counter));
        pthread_mutex_destroy(&(pool->thread_counter));
        pthread_cond_destroy(&(pool->queue_not_empty));
        pthread_cond_destroy(&(pool->queue_not_full));
    }

    free(pool);
    pool = NULL;
    return 0;

}
int threadpool_destroy(threadpool_t * pool)
{
    int i;
    if(pool == NULL)
        return -1;
    pool->shutdown = true;
    for(i = 0;i < pool->live_thr_num;i++)
    {
        //通知所有的空闲线程
        pthread_cond_broadcast(&(pool->queue_not_empty));
    }
    for(i = 0; i< pool->live_thr_num ; i ++)
    {
        pthread_join(pool->threads[i],NULL);
    }
    threadpool_free(pool);
    return 0;
}

//线程池中的线程，模拟处理任务
void * process(void * arg)
{
    printf("thread 0x%x working on task %d\n",(unsigned int)pthread_self(),*(int *)arg);
    printf("task %d is end \n",*(int *)arg);
    return NULL;
}

int main()
{
    threadpool_t *thp = threadpool_create(3,100,100);
    //创建线程池，池里最小三个线程，最大100，任务队列最大100
    printf("pool inited\n");
    int num[20],i;
    for(i = 0;i < 20;i++)
    {
        num[i] = i;
        printf("add task %d \n",i);
        threadpool_add(thp,process,(void *)&num[i]); //向任务队列中添加任务
    }
    sleep(10);                      //等子线程完成任务
    printf("clean up thread pool \n");
    sleep(1);
    threadpool_destroy(thp);
    return 0;
}