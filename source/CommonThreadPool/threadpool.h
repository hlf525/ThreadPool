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


// �̳߳�֧�ֵ�ģʽ
enum class ThreadPoolMode
{
	MODE_FIXED, // �̸߳����ǹ̶�����
	MODE_CACHED // �̸߳����ǿɶ�̬����
};

// Any ����:���Խ����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// ������캯��������Any���ͽ�����������������
	template<typename T> // T:int	Derive<int>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)) // new Derive<T>(data)
	{
	}

	// ��������ܰ�Any������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// ������ô��base_�ҵ�����ָ���Derive���󣬴�������ȡ��data��Ա����
		// ����ָ�� =��������ָ�� RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is incompatible!";
		}
		return pd->data_;
	}

private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default; // �ڼ̳нṹ����������Ƕ��ϴ��� ����������������޷����� ��ʱ������������ʹ���麯�� 

	private:

	};

	// ����������
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data) :data_(data)
		{
		}
		T data_; // �������������������
	};

private:
	// ����һ������ָ��
	std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
	Semaphore(int limit = 0) :resLimit_(limit)
	{
	}

	~Semaphore() = default;

	// ��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		// �ȴ��ź�������Դ��û����Դ�Ļ�����������ǰ�߳�
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
		resLimit_--;
	}

	// ����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// linux�£�condition_variable����������ʲôҲû��
		// ��������״̬�Ѿ�ʧЧ���޹�����
		cond_.notify_all();
	}

private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

};

// Task Any���͵�ǰ������
class Task;
// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// setVal��������ȡ����ִ����ķ���ֵ
	void setVal(Any any);

	// get�������û��������������ȡtask����ֵ
	Any get();

private:
	Any any_; // �洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; // ����ֵ�Ƿ���Ч
};

// ����������
class Task
{
public:

	Task();
	~Task() = default;

	// �û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ��������� 
	virtual Any run() = 0;

	void exec();

	void setResult(Result* result);

private:
	// Result������������� > Task����
	Result* result_; // ���ﲻҪʹ������ָ�� ���ᵼ������ָ��Ľ������õ�����Զ�ò����ͷ� 

};

// �߳�����
class Thread
{
public:
	// �̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

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
	Result submitTask(std::shared_ptr<Task> sp);

private:
	// �����̺߳���		��bind�����󶨳ɺ�������
	void threadFunc(int threadid);

	// ����̳߳�����״̬
	bool checkRunningState() const;

private:
	//std::vector<Thread*> threads_; // ֱ��ʹ��ָ����Ҫ�ֶ�ɾ�� �����ڴ�й© ʹ������ָ������Զ�����
	//std::vector<std::unique_ptr<Thread>> threads_; // �߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	int initThreadSize_; // ��ʼ���߳�����
	// cachedģʽ
	std::atomic_int curThreadSize_; // ��¼��ǰ�̳߳������߳�����
	int threadSizeThreshHold_; // �߳��������޵���ֵ
	std::atomic_int idleThreadSize_; // ��¼�����̵߳�����


	std::queue<std::shared_ptr<Task>> taskQue_;// �����ָ������� ʵ�ֶ�̬ ����Ҫ��֤������������� ʹ������ָ��
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
