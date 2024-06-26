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


// 线程池支持的模式
enum class ThreadPoolMode
{
	MODE_FIXED, // 线程个数是固定不变
	MODE_CACHED // 线程个数是可动态增长
};

// Any 类型:可以接收任意数据的类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// 这个构造函数可以让Any类型接收任意其他的数据
	template<typename T> // T:int	Derive<int>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)) // new Derive<T>(data)
	{
	}

	// 这个方法能把Any对象里存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 我们怎么从base_找到它所指向的Derive对象，从他里面取出data成员变量
		// 基类指针 =》派生类指针 RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is incompatible!";
		}
		return pd->data_;
	}

private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default; // 在继承结构中如果基类是堆上创建 派生类的析构函数无法调用 此时基类析构函数使用虚函数 

	private:

	};

	// 派生类类型
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data) :data_(data)
		{
		}
		T data_; // 保存了任意的其他类型
	};

private:
	// 定义一个基类指针
	std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
	Semaphore(int limit = 0) :resLimit_(limit)
	{
	}

	~Semaphore() = default;

	// 获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		// 等待信号量有资源，没有资源的话，会阻塞当前线程
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
		resLimit_--;
	}

	// 增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// linux下，condition_variable的析构函数什么也没做
		// 导致这里状态已经失效，无故阻塞
		cond_.notify_all();
	}

private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

};

// Task Any类型的前置声明
class Task;
// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// setVal方法，获取任务执行完的返回值
	void setVal(Any any);

	// get方法，用户调用这个方法获取task返回值
	Any get();

private:
	Any any_; // 存储任务的返回值
	Semaphore sem_; // 线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; // 返回值是否有效
};

// 任务抽象基类
class Task
{
public:

	Task();
	~Task() = default;

	// 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理 
	virtual Any run() = 0;

	void exec();

	void setResult(Result* result);

private:
	// Result对象的生命周期 > Task对象
	Result* result_; // 这里不要使用智能指针 将会导致智能指针的交叉引用导致永远得不到释放 

};

// 线程类型
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

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
	Result submitTask(std::shared_ptr<Task> sp);

private:
	// 定义线程函数		用bind绑定器绑定成函数对象
	void threadFunc(int threadid);

	// 检查线程池启动状态
	bool checkRunningState() const;

private:
	//std::vector<Thread*> threads_; // 直接使用指针需要手动删除 避免内存泄漏 使用智能指针可以自动析构
	//std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSize_; // 初始的线程数量
	// cached模式
	std::atomic_int curThreadSize_; // 记录当前线程池里面线程数量
	int threadSizeThreshHold_; // 线程数量上限的阈值
	std::atomic_int idleThreadSize_; // 记录空闲线程的数量


	std::queue<std::shared_ptr<Task>> taskQue_;// 基类的指针或引用 实现多态 还需要保证任务的生命周期 使用智能指针
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
