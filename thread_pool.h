#ifndef thread_pool_H
#define thread_pool_H
#include <pthread.h>
#include <cstdio>
#include <list>
#include "locker.h"

template<typename T>
class thread_pool {
public:
    thread_pool(int thread_number=8, int max_requests=1e4);
    ~thread_pool();
    bool append(T* request);
    static void* worker(void *);
private:
    // 线程数量
    int m_thread_number;
    // 线程池id数组
    pthread_t* m_threads;
    // 请求队列的最大数量，类似生产者消费者模型中的缓冲上限
    int m_max_requests;
    // 请求队列
    std::list<T*> m_work_queue;
    // 访问请求队列的锁
    locker m_queue_locker;
    // 表示请求队列数量的信号量
    sem m_queue_stat;
    // 判断是否要终止
    bool m_stop;
};

template <typename T>
thread_pool<T>::thread_pool(int thread_number, int max_requests): 
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL) {
    if (thread_number < 0 || max_requests < 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    for (int i = 0; i < m_thread_number; ++i) {
        if (pthread_create(&m_threads[i], NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        printf("create [%d]th thread\n", i);
    }
}

template <typename T>
thread_pool<T>::~thread_pool() {
    delete [] m_threads;
    m_stop = true;
}
template <typename T>
bool thread_pool<T>::append(T* request) {
    m_queue_locker.lock();
    if (m_work_queue.size() >= m_max_requests || !request) {
        m_queue_locker.unlock();
        return false;
    }
    m_work_queue.push_back(request);
    m_queue_locker.unlock();
    m_queue_stat.post();
    return true;
}
/*消费者模型*/
template <typename T>
void* thread_pool<T>::worker(void* arg) {
    thread_pool* this_pool = (thread_pool*) arg;
    while(!this_pool->m_stop) {
        this_pool->m_queue_stat.wait();
        this_pool->m_queue_locker.lock();
        printf("get T\n");
        T*  request = this_pool->m_work_queue.front();
        this_pool->m_work_queue.pop_front();
        this_pool->m_queue_locker.unlock();
        request->process();
    }
    return this_pool;
}


// template <typename T>
// void thread_pool<T>::run() {
//     while(!m_stop) {
//         m_queue_stat.wait();
//         m_queue_locker.lock();
//         T*  request = m_work_queue.front();
//         m_work_queue.pop_front();
//         m_queue_locker.unlock();
//         request->process();
//     }
// }
#endif