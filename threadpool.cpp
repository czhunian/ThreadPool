#include "threadpool.h"

int Thread::generateId_ = 0;

ThreadPool::ThreadPool()
		: initThreadSize_(0)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, taskQueSizeMax_(TASK_MAX_THRESHHOLD)
		, threadSizeMax_(THREAD_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
	{}


ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;

    // 等待线程池里面所有的线程返回  有两种状态：阻塞 & 正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all(); 
    exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
        return;
    poolMode_ = mode;
}


void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
    if (checkRunningState())
        return;
    taskQueSizeMax_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
    if (checkRunningState())
        return;
    if (poolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeMax_ = threshhold;
    }
}


void ThreadPool::start(int initThreadSize)
{

    isPoolRunning_ = true;

    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    for (int i = 0; i < initThreadSize_; i++)
    {
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动所有线程  std::vector<Thread*> threads_;
    for (int i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start(); 
        idleThreadSize_++;    
    }
}


void ThreadPool::threadFunc(int threadid)
{
    auto lastTime = std::chrono::high_resolution_clock().now();

    // 所有任务必须执行完成，线程池才可以回收所有线程资源
    while(1)
    {
        Task task;
        {
            // 先获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            printf("threadid : %u, 尝试获取任务... \n", std::this_thread::get_id());
            // std::cout << "tid:" << std::this_thread::get_id() << " 尝试获取任务..." << std::endl;

            // cached模式下，可能创建了很多的线程
            // 当前时间 - 上一次线程执行的时间 > 60 回收
            // 锁 + 双重判断
            while (taskQue_.size() == 0)
            {
                // 线程池要结束，回收线程资源
                if (!isPoolRunning_)
                {
                    threads_.erase(threadid); // std::this_thread::getid()
                    printf("threadid : %u, exit! \n", std::this_thread::get_id());
                    // std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
                    exitCond_.notify_all();
                    return; // 线程函数结束，线程结束
                }

                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    // 条件变量，超时返回了
                    if (std::cv_status::timeout ==
                        notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME
                            && curThreadSize_ > initThreadSize_)
                        {
                            // 把线程对象从线程列表容器中删除   没有办法 threadFunc《=》thread对象
                            // threadid => thread对象 => 删除
                            threads_.erase(threadid); // std::this_thread::getid()
                            curThreadSize_--;
                            idleThreadSize_--;
                            printf("threadid : %u, exit! \n", std::this_thread::get_id());
                            // std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
                            return;
                        }
                    }
                }
                else
                {
                    // 等待notEmpty条件
                    notEmpty_.wait(lock);
                }
            }

            idleThreadSize_--;

            printf("threadid : %u, 获取任务成功... \n", std::this_thread::get_id());
            // std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功..." << std::endl;

            // 从任务队列种取一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            // 如果依然有剩余任务，继续通知其它得线程执行任务
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }

            // 取出一个任务，进行通知，通知可以继续提交生产任务
            notFull_.notify_all();
        } 

        // 当前线程负责执行这个任务
        if (task != nullptr)
        {
            task(); 
        }

        idleThreadSize_ ++;
        lastTime = std::chrono::high_resolution_clock().now(); 
    }
}