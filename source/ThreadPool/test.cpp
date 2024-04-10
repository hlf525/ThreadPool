// ThreadPool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。


#include "threadpool.h"
#include<chrono>
using namespace std;


/*
 如何能让线程池提交任务更加方便
    1.pool.submitTask(func, ..., ...);
      submitTask:可变参模板编程

    2.我们自己创造了一个Result以及相关类型，代码挺多
      C++11线程库  thread  package_task(function函数对象) async
      使用future来代替Result节省线程池代码

 */

int sum1(int a, int b)
{
    this_thread::sleep_for(chrono::seconds(3));

    return a + b;
}

int sum2(int a, int b, int c)
{
	this_thread::sleep_for(chrono::seconds(3));

    return a + b + c;
}

int main()
{
    ThreadPool pool;
    //pool.setMode(ThreadPoolMode::MODE_CACHED);
    pool.start(2);

    future<int> res1 = pool.submitTask(sum1, 1, 2);
    future<int> res2 = pool.submitTask(sum2, 1, 2, 3);
    future<int> res3 = pool.submitTask([](int b, int e)->int {
        int sum = 0;
        for (int i = b; i <= e; i++)
        {
            sum += i;
        }
        return sum;
        }, 1, 100);
	future<int> res4 = pool.submitTask(sum1, 1, 2);
    future<int> res5 = pool.submitTask(sum1, 1, 2);


    cout << res1.get() << endl;
	cout << res2.get() << endl;
	cout << res3.get() << endl;
	cout << res4.get() << endl;
	cout << res5.get() << endl;

    //packaged_task<int(int, int)> task(sum1);
    //// future <=> Result
    //future<int> res = task.get_future();
    ////task(10, 20);

    //thread t(move(task), 10, 20);
    //t.detach();

    //cout << res.get() << endl;

	/* thread t1(sum1, 10, 20);
	 thread t2(sum2, 1, 2, 3);

	 t1.join();
	 t2.join();*/
}
