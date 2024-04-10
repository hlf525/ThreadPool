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


// �̳߳�֧�ֵ�ģʽ
enum class ThreadPoolMode
{
	MODE_FIXED, // �̸߳����ǹ̶�����
	MODE_CACHED // �̸߳����ǿɶ�̬����
};

// �߳�����
class Thread
{
public:
	// �̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread() = default;

	// �����߳�
	void start();

	// ��ȡ�߳�ID
	int getThreadId() const;

private:
	ThreadFunc func_; // �̺߳�������
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
	void run(){ // �̴߳���... }
};

pool.submitTask(std::make_shared<MyTask>());
*/
// �̳߳�����
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// ��ֹ�û����̳߳ؽ��п�������
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;


	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency()); // ����CPU�ĺ�������

	// �����̳߳�ģʽ
	void setMode(ThreadPoolMode mode);

	//// ���ó�ʼ���߳�����
	//void setinitThreadSize(int size);

	// ����task����������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeMaxThreshHold(int threadthreshhold);

	// ���̳߳��ύ����
	// ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
	// ����ֵfuture<>
	// ע��ģ���̵ĺ���ʵ�ֲ��ܷ���.cpp�ļ��£��������Ӳ��ϡ���Ҫ��ʾʵ����
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		// ������񣬷��������������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// ��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
		{
			// ��ʾnotFull_�ȴ�1s��������Ȼû������
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}

		// ����п��࣬������������������
		//taskQue_.emplace(sp);
		//using Task = std::function<void(int)>;
		taskQue_.emplace([task]() { (*task)(); });
		taskSize_++;

		// ��Ϊ�·�������������п϶����գ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
		notEmpty_.notify_all();

		// cachedģʽ ������ȽϽ��� ������С��������� ��Ҫ�������������Ϳ����߳��������ж��Ƿ���Ҫ�����µ��̳߳���
		if (poolMode_ == ThreadPoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
		{
			std::cout << ">>> create new thread!!!" << std::endl;
			// �����µ��̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1)); // placeholders ����ռλ��
			//threads_.emplace_back(std::move(ptr));
			int threadId = ptr->getThreadId();
			threads_.emplace(threadId, std::move(ptr));

			// �����߳�
			threads_[threadId]->start();
			// �޸��̸߳�����صı���
			curThreadSize_++;
			idleThreadSize_++;
		}

		// ���������Result����
		//return task->getResult(); // Task Result 
		return result;
	}

private:
	// �����̺߳���		��bind�����󶨳ɺ�������
	void threadFunc(int threadid);

	// ����̳߳�����״̬
	bool checkRunningState() const;

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSize_; // ��ʼ���߳�����
	// cachedģʽ
	std::atomic_int curThreadSize_; // ��¼��ǰ�̳߳������߳�����
	int threadSizeThreshHold_; // �߳��������޵���ֵ
	std::atomic_int idleThreadSize_; // ��¼�����̵߳�����

	// Task���� =����������
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;// �����ָ������� ʵ�ֶ�̬ ����Ҫ��֤������������� ʹ������ָ��
	std::atomic_int taskSize_; // �������� ���ǵ��̰߳�ȫ ��ԭ������
	int taskQueMaxThreshHold_; // ��������������޵���ֵ

	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в���
	std::condition_variable notEmpty_; // ��ʾ������в���
	std::condition_variable exitCond_; // �ȴ��߳���Դȫ������

	ThreadPoolMode poolMode_; // �̳߳�ģʽ
	std::atomic_bool isPoolRunning_; // ��ʾ��ǰ�̳߳ص�����״̬

};
#endif
