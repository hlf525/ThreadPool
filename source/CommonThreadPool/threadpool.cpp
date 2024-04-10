#include "threadpool.h"

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // 单位s

ThreadPool::ThreadPool(): initThreadSize_(0), taskSize_(0), curThreadSize_(0), idleThreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(ThreadPoolMode::MODE_FIXED), isPoolRunning_(false)
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

//// 设置初始的线程数量
//void ThreadPool::setinitThreadSize(int size)
//{
//	initThreadSize_ = size;
//}

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

// 给线程池提交任务		用户调用该接口，传入任务对象，生成任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 线程的通信 等待
	// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	/*while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}*/
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {
		return taskQue_.size() < (size_t)taskQueMaxThreshHold_;}))
	{
		// 表示notFull_等待1s，条件依然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
		//return task->getResult(); // Task Result	线程执行完task，task对象就被析构掉了
		return Result(sp, false);
	}

	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
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
	return Result(sp);

}

// 定义线程函数		线程池的所有线程从任务队列里面消费任务
// 线程函数返回，相应的线程也就结束了
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now(); 

	//while(isPoolRunning_)
	// 所有任务必须执行完成，线程池才可以回收线程资源
	for(;;)
	{
		std::shared_ptr<Task> task;
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

				// 阻塞 线程池要结束，回收线程资源
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadid);
				//	std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return; // 结束线程函数，就是结束当前线程了！
				//}
			}
		
			//// 线程池要结束，回收线程资源  死锁问题解决 1.pool现成先获取锁  2.线程池里面的线程先获取锁导致
			//if (!isPoolRunning_)
			//{
			//	break;
			//}

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
			//task->run(); // 执行任务，把任务的返回值setVal方法给到Result
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务时间
	}

	//// 线程正在执行任务时，线程结束了
	//threads_.erase(threadid);
	//std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
	//exitCond_.notify_all();
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

Thread::Thread(ThreadFunc func): func_(func), threadId_(generateId_++)
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

Thread::~Thread()
{

}


//---------------------------Result方法实现-------------------
Result::Result(std::shared_ptr<Task> task, bool isValid): task_(task), isValid_(isValid)
{
	task_->setResult(this);
}

void Result::setVal(Any any)
{
	// 存储Task返回值
	this->any_ = std::move(any);
	sem_.post(); // 已经获取的任务返回值，增加信号量资源
}

// 用户调用
Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}

	sem_.wait(); // Task任务没执行完，会阻塞用户的线程
	return std::move(any_);
}


//---------------------------Task方法实现-------------------
Task::Task():result_(nullptr)
{

}


void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); // 这里发生多态调用
	}
}

void Task::setResult(Result* result)
{
	result_ = result;
}
