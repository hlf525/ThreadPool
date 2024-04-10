#include <iostream>
#include <chrono>

#include "threadpool.h"
using namespace std;

/*
有些场景，是希望获取线程执行任务的返回值
举例
1 + ... + 30000的和
......
main thread：给每一个线程分配计算的区间，并等待他们算完返回结果，合并最终的结果
*/

using uLong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin, int end): begin_(begin), end_(end)
	{

	}

	// 问题1：怎么设计run函数返回值可以表示任意类型
	// Java Python Object是所有其他类型的基类
	// C++17 Any类型
	Any run() // run方法最终在线程池分配的线程中工作
	{
		std::cout << "tid: " << std::this_thread::get_id() << "begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		std::cout << "tid: " << std::this_thread::get_id() << "end!" << std::endl;
		return sum;
	}

private:
	int begin_;
	int end_;
};

int main()
{
	{
		ThreadPool pool;
		pool.setMode(ThreadPoolMode::MODE_CACHED);
		pool.start(2);

		// linux上，Result对象也是局部对象，需要析构的！！
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		uLong sum1 = res1.get().cast_<uLong>();
		cout << "sum = " << sum1 << endl;
	}// Result对象需要析构的！！在VS下，条件变量析构会释放相应资源
	
	cout << "main over!" << endl;

#if 0

	// 问题：ThreadPool对象析构后，怎么把线程池相关的线程资源全部回收？
	{
		ThreadPool pool;

		// 用户自己设置线程的工作模式
		pool.setMode(ThreadPoolMode::MODE_CACHED);
		// 开始启动线程池
		pool.start(4);


		// 问题2：如何设计这里的result机制
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		// 随着task被执行完，task对象没了，依赖于task对象的result对象也没了
		uLong sum1 = res1.get().cast_<uLong>(); // get返回一个Any类型，怎么转成具体的类型
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		// Master - Slave线程模型
		// Master 用来分解任务，然后给各个Slave线程分配任务
		// 等待各个Slave线程执行完任务，返回结果
		// Master 线程合并各个任务结果，输出
		std::cout << "sum = " << (sum1 + sum2 + sum3) << std::endl;
	}

	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());

	getchar();
#endif
}