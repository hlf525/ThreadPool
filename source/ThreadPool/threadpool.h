#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <unordered_map>
#include <future>


// 线程池支持的模式
enum class ThreadPoolMode
{
	MODE_FIXED, // 线程个数是固定不变
	MODE_CACHED // 线程个数是可动态增长
};

// 线程类型
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread() = default;

	// 启动线程
	void start();

	// 获取线程ID
	int getThreadId() const;

private:
	ThreadFunc func_; // 线程函数对象
	static int generateId_;
	int threadId_;
};

/*
example:
ThreadPool pool;
pool.start();

class MyTask: public Task
{
public:
	void run(){ // 线程代码... }
};

pool.submitTask(std::make_shared<MyTask>());
*/
// 线程池类型
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// 禁止用户对线程池进行拷贝操作
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;


	// 启动线程池
	void start(int initThreadSize = std::thread::hardware_concurrency()); // 返回CPU的核心数量

	// 设置线程池模式
	void setMode(ThreadPoolMode mode);

	//// 设置初始的线程数量
	//void setinitThreadSize(int size);

	// 设置task队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeMaxThreshHold(int threadthreshhold);

	// 给线程池提交任务
	// 使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
	// 返回值future<>
	// 注意模板编程的函数实现不能放在.cpp文件下，否则链接不上。需要显示实例化
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		// 打包任务，放入任务队列里面
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// 获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			// 表示notFull_等待1s，条件依然没有满足
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		// 如果有空余，把任务放入任务队列中
		//taskQue_.emplace(sp);
		//using Task = std::function<void(int)>;
		taskQue_.emplace([task]() { (*task)(); });
		taskSize_++;

		// 因为新放了任务，任务队列肯定不空，在notEmpty_上进行通知，赶快分配线程执行任务
		notEmpty_.notify_all();

		// cached模式 任务处理比较紧急 场景：小而块的任务 需要根据任务数量和空闲线程数量，判断是否需要创建新的线程出来
		if (poolMode_ == ThreadPoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << ">>> create new thread!!!" << std::endl;
			// 创建新的线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)); // placeholders 参数占位符
			//threads_.emplace_back(std::move(ptr));
			int threadId = ptr->getThreadId();
			threads_.emplace(threadId, std::move(ptr));

			// 启动线程
			threads_[threadId]->start();
			// 修改线程个数相关的变量
			curThreadSize_++;
			idleThreadSize_++;
		}

		// 返回任务的Result对象
		//return task->getResult(); // Task Result 
		return result;
	}

private:
	// 定义线程函数		用bind绑定器绑定成函数对象
	void threadFunc(int threadid);

	// 检查线程池启动状态
	bool checkRunningState() const;

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSize_; // 初始的线程数量
	// cached模式
	std::atomic_int curThreadSize_; // 记录当前线程池里面线程数量
	int threadSizeThreshHold_; // 线程数量上限的阈值
	std::atomic_int idleThreadSize_; // 记录空闲线程的数量

	// Task任务 =》函数对象
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;// 基类的指针或引用 实现多态 还需要保证任务的生命周期 使用智能指针
	std::atomic_int taskSize_; // 任务数量 考虑到线程安全 用原子类型
	int taskQueMaxThreshHold_; // 任务队列数量上限的阈值

	std::mutex taskQueMtx_; // 保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable notEmpty_; // 表示任务队列不空
	std::condition_variable exitCond_; // 等待线程资源全部回收

	ThreadPoolMode poolMode_; // 线程池模式
	std::atomic_bool isPoolRunning_; // 表示当前线程池的启动状态

};
#endif
