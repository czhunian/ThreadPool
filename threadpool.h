#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <future>


const int TASK_MAX_THRESHHOLD = 2; 
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // 单位：秒


// 线程池支持的模式
enum class PoolMode
{
	MODE_FIXED,  // 固定数量线程
	MODE_CACHED, // 数量动态增长
};

// 线程类型
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;


	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{}


	~Thread() = default;
	void start()
	{
		std::thread t(func_, threadId_); 
		t.detach(); // 设置分离线程   
	}


	int getId()const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;  // 保存线程id
};



// 线程池类型
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	void setMode(PoolMode mode);
	void setTaskQueMaxThreshHold(int threshhold);
	void setThreadSizeThreshHold(int threshhold);

	void start(int initThreadSize = std::thread::hardware_concurrency());
	
	// 给线程池提交任务
	// 使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
	// pool.submitTask(sum1, 10, 20);
	// 返回值future<函数返回值>
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
		/*
			基础用法
			int sum(int a, int b) {return a + b;}
			packaged_task<int(int, int)> task(sum);
			future<int> res = task.get_future();
			thread t(std::move(task), 10, 20);
			t.detach();
			cout << res.get() << endl;
		
		*/
		
		// 打包任务，放入任务队列里面
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
				[&]()->bool { return taskQue_.size() < (size_t)taskQueSizeMax_; }))
		{
			// notFull_等待1s，条件依然没有满足
			perror("任务队列满，暂时不能提交任务\n");
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		taskQue_.emplace([task]() {(*task)();});
		taskSize_++;

		notEmpty_.notify_all();

		// cached模式 任务处理比较紧急 场景：小而快的任务 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeMax_)
		{
			std::cout << ">>> create new thread..." << std::endl;

			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();
			curThreadSize_++;
			idleThreadSize_++;
		}

		return result;
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	void threadFunc(int threadid);

	bool checkRunningState() const
	{
		return isPoolRunning_;
	}

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程列表

	int initThreadSize_;  				// 初始的线程数量
	int threadSizeMax_; 				// 线程数量上限阈值
	std::atomic_int curThreadSize_;		// 记录当前线程池里面线程的总数量
	std::atomic_int idleThreadSize_; 	// 记录空闲线程的数量

	
	using Task = std::function<void()>;	// Task任务 --> 函数对象
	std::queue<Task> taskQue_; 			// 任务队列
	std::atomic_int taskSize_; 			// 任务的数量
	int taskQueSizeMax_;  				// 任务队列数量上限阈值

	std::mutex taskQueMtx_; 			// 保证任务队列的线程安全
	std::condition_variable notFull_; 	// 表示任务队列不满
	std::condition_variable notEmpty_; 	// 表示任务队列不空
	std::condition_variable exitCond_; 	// 等到线程资源全部回收

	PoolMode poolMode_; 				// 当前线程池的工作模式
	std::atomic_bool isPoolRunning_; 	// 表示当前线程池的启动状态
};

