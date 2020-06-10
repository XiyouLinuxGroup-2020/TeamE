/*************************************************************************
	> File Name: product.c
	> Author: 
	> Mail: 
	> Created Time: 2020年05月29日 星期五 21时54分40秒
 ************************************************************************/

#include<stdio.h>
#include<pthread.h>
#include<unistd.h>
#include<stdlib.h>
#define N 100
#define true 1
#define producerNum 10
#define consumerNum 5
#define sleepTime 1

typedef int semaphore;
typedef int item;
item buffer[N] = { 0 };
int in = 0;
int out = 0;
int proCount = 0;
semaphore mutex = 1,empty = N,full = 0,proCmutex = 1;
void * producer(void * a)
{
    while(true)
    {
        /*
        while(proCmutex <= 0);
        proCmutex --;
        proCount ++;
        printf("生产一个产品ID %d，缓冲区位置为%d\n",proCount,in);
        proCmutex ++ ;
        */
        while(empty <= 0)
        {
            printf("缓冲区已满！\n");
        }
        empty --;
        while(mutex <= 0);
        mutex --;
        proCount++;
        buffer[in] = proCount;
        in = (in + 1) % N;
        printf("生产一个产品ID %d，缓冲区位置为%d\n",proCount,in);
        mutex ++;
        full ++;
        sleep(sleepTime);
    }
}
void * consumer(void * b)
{
    while(true)
    {
        while(full <= 0)
            printf("缓冲区为空!\n");
        full --;
        while(mutex <= 0);
        mutex--;
        int nextc = buffer[out];
        buffer[out] = 0; //消费完将缓冲区设置为0
        out = (out + 1) % N;
        mutex ++;
        empty ++;
        printf("\t\t\t\t消费一个产品ID %d,缓冲区位置为%d\n",nextc,out);
        sleep(sleepTime);
    }
}

int main()
{
    pthread_t threadPool[producerNum + consumerNum];
    int i;
    for(i = 0;i<producerNum;i++)
    {
        pthread_t temp;
        if(pthread_create(&temp,NULL,producer,NULL) == -1)
        {
            printf("ERROR,fail to create producer %d\n",i);
            exit(1);
        }
        threadPool[i] = temp;

    }//创建生产者进程放入线程池
    for(i = 0;i<consumerNum;i++)
    {
        pthread_t temp;
        if(pthread_create(&temp,NULL,consumer,NULL) == -1)
        {
            printf("ERROR,fail to create consumer %d\n",i);
            exit(1);
        }
        threadPool[i+producerNum] = temp;
    }//创建消费者进程放入线程池
    while(1);
    void * result;
    for(i = 0;i<producerNum + consumerNum;i++)
    {
        if(pthread_join(threadPool[i],&result) == -1)
        {
            printf("fail to recollect\n");
            exit(1);
        }
    }//运行线程池
    return 0;
}