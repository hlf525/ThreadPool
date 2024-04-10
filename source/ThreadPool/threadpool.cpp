#include "threadpool.h"

const int TASK_MAX_THRESHHOLD = 2;//INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // 单位s

ThreadPool::ThreadPool() : initThreadSize_(0), taskSize_(0), curThreadSize_(0), idleThreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(ThreadPoolMode::MODE_FIXED), isPoolRunning_(false)
{

}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	// 设置线程池的启动状态
	isPoolRunning_ = true;

	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		// 创建thread线程对象时，把线程函数给到thread线程对象
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc, this))); // 直接使用指针需要手动删除 避免内存泄漏 使用智能指针可以自动析构
		std::unique_ptr<Thread> uptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = uptr->getThreadId();
		threads_.emplace(threadId, std::move(uptr));
		//threads_.emplace_back(std::move(uptr)); // unique_ptr不允许使用左值拷贝构造，但允许右值拷贝构造 
	}

	// 启动所有线程	std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++; // 记录初始空闲线程的数量
	}

}

// 设置线程池模式
void ThreadPool::setMode(ThreadPoolMode mode)
{
	if (checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

// 设置task队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}

// 设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeMaxThreshHold(int threadthreshhold)
{
	if (checkRunningState())
	{
		return;
	}
	if (poolMode_ == ThreadPoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threadthreshhold;
	}
}

// 定义线程函数		线程池的所有线程从任务队列里面消费任务
// 线程函数返回，相应的线程也就结束了
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//while(isPoolRunning_)
	// 所有任务必须执行完成，线程池才可以回收线程资源
	for (;;)
	{
		Task task;
		{
			// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

			// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，多余线程结束回收
			// 超过initThreadSize_数量的线程要进行回收
			// 当前时间 - 上一次线程执行的时间 > 60s

			// 每一秒中返回一次 如何区分：超时返回 与 有任务待执行返回
			// 锁 + 双重判断 
			while (taskQue_.size() == 0)
			{
				// 线程池要结束，回收线程资源  死锁问题解决 1.pool现成先获取锁  2.线程池里面的线程先获取锁导致
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return; // 线程函数结束，线程结束
				}

				if (poolMode_ == ThreadPoolMode::MODE_CACHED)
				{
					// 条件变量超时返回
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto nowTime = std::chrono::high_resolution_clock().now();
						auto during = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
						if (during.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) // 否则将回收所有线程
						{
							// 开始回收线程
							// 记录线程数量的相关变量的值修改
							// 把线程对象线程列表容器中删除		没有办法确定 threadFunc 《=》 thread对象
							// threadId => thread对象 => 删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待notEmpty_
					notEmpty_.wait(lock); // [&]()->bool { return taskQue_.size() > 0; } 将lambda表达式领出来判断
				}
			}

			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务成功..." << std::endl;

			// 从任务队列中取出一个任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 任务取出后，进行通知，可以继续提交生成任务
			notFull_.notify_all();
		} // 把锁释放掉

		// 当前线程负责指向这个任务
		if (task != nullptr)
		{
			task(); // 执行function<void(int)>
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务时间
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	//notEmpty_.notify_all();

	// 等待线程池里面所有线程返回 有两种状态：阻塞 & 执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

//---------------------------线程方法实现-------------------
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func) : func_(func), threadId_(generateId_++)
{

}

// 启动线程
void Thread::start()
{
	// 创建一个线程来执行一个线程函数
	std::thread t(func_, threadId_); // C++11 线程对象t 和 线程函数func_
	// 设置分离线程 pthread_detach
	t.detach();
}

int Thread::getThreadId() const
{
	return threadId_;
}
