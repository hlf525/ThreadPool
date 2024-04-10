#include <iostream>
#include <chrono>

#include "threadpool.h"
using namespace std;

/*
��Щ��������ϣ����ȡ�߳�ִ������ķ���ֵ
����
1 + ... + 30000�ĺ�
......
main thread����ÿһ���̷߳����������䣬���ȴ��������귵�ؽ�����ϲ����յĽ��
*/

using uLong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin, int end): begin_(begin), end_(end)
	{

	}

	// ����1����ô���run��������ֵ���Ա�ʾ��������
	// Java Python Object�������������͵Ļ���
	// C++17 Any����
	Any run() // run�����������̳߳ط�����߳��й���
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

		// linux�ϣ�Result����Ҳ�Ǿֲ�������Ҫ�����ģ���
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		uLong sum1 = res1.get().cast_<uLong>();
		cout << "sum = " << sum1 << endl;
	}// Result������Ҫ�����ģ�����VS�£����������������ͷ���Ӧ��Դ
	
	cout << "main over!" << endl;

#if 0

	// ���⣺ThreadPool������������ô���̳߳���ص��߳���Դȫ�����գ�
	{
		ThreadPool pool;

		// �û��Լ������̵߳Ĺ���ģʽ
		pool.setMode(ThreadPoolMode::MODE_CACHED);
		// ��ʼ�����̳߳�
		pool.start(4);


		// ����2�������������result����
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		// ����task��ִ���꣬task����û�ˣ�������task�����result����Ҳû��
		uLong sum1 = res1.get().cast_<uLong>(); // get����һ��Any���ͣ���ôת�ɾ��������
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();

		// Master - Slave�߳�ģ��
		// Master �����ֽ�����Ȼ�������Slave�̷߳�������
		// �ȴ�����Slave�߳�ִ�������񣬷��ؽ��
		// Master �̺߳ϲ����������������
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