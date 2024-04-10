#include "threadpool.h"

const int TASK_MAX_THRESHHOLD = 2;//INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // ��λs

ThreadPool::ThreadPool() : initThreadSize_(0), taskSize_(0), curThreadSize_(0), idleThreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(ThreadPoolMode::MODE_FIXED), isPoolRunning_(false)
{

}

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// �����̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ����thread�̶߳���ʱ�����̺߳�������thread�̶߳���
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc, this))); // ֱ��ʹ��ָ����Ҫ�ֶ�ɾ�� �����ڴ�й© ʹ������ָ������Զ�����
		std::unique_ptr<Thread> uptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = uptr->getThreadId();
		threads_.emplace(threadId, std::move(uptr));
		//threads_.emplace_back(std::move(uptr)); // unique_ptr������ʹ����ֵ�������죬��������ֵ�������� 
	}

	// ���������߳�	std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++; // ��¼��ʼ�����̵߳�����
	}

}

// �����̳߳�ģʽ
void ThreadPool::setMode(ThreadPoolMode mode)
{
	if (checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

// ����task����������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}

// �����̳߳�cachedģʽ���߳���ֵ
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

// �����̺߳���		�̳߳ص������̴߳��������������������
// �̺߳������أ���Ӧ���߳�Ҳ�ͽ�����
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//while(isPoolRunning_)
	// �����������ִ����ɣ��̳߳زſ��Ի����߳���Դ
	for (;;)
	{
		Task task;
		{
			// ��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;

			// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s�������߳̽�������
			// ����initThreadSize_�������߳�Ҫ���л���
			// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s

			// ÿһ���з���һ�� ������֣���ʱ���� �� �������ִ�з���
			// �� + ˫���ж� 
			while (taskQue_.size() == 0)
			{
				// �̳߳�Ҫ�����������߳���Դ  ���������� 1.pool�ֳ��Ȼ�ȡ��  2.�̳߳�������߳��Ȼ�ȡ������
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return; // �̺߳����������߳̽���
				}

				if (poolMode_ == ThreadPoolMode::MODE_CACHED)
				{
					// ����������ʱ����
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto nowTime = std::chrono::high_resolution_clock().now();
						auto during = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
						if (during.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) // ���򽫻��������߳�
						{
							// ��ʼ�����߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳����߳��б�������ɾ��		û�а취ȷ�� threadFunc ��=�� thread����
							// threadId => thread���� => ɾ��
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
					// �ȴ�notEmpty_
					notEmpty_.wait(lock); // [&]()->bool { return taskQue_.size() > 0; } ��lambda���ʽ������ж�
				}
			}

			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����ɹ�..." << std::endl;

			// �����������ȡ��һ������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ����ȡ���󣬽���֪ͨ�����Լ����ύ��������
			notFull_.notify_all();
		} // �����ͷŵ�

		// ��ǰ�̸߳���ָ���������
		if (task != nullptr)
		{
			task(); // ִ��function<void(int)>
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // �����߳�ִ��������ʱ��
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

	// �ȴ��̳߳����������̷߳��� ������״̬������ & ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

//---------------------------�̷߳���ʵ��-------------------
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func) : func_(func), threadId_(generateId_++)
{

}

// �����߳�
void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳���
	std::thread t(func_, threadId_); // C++11 �̶߳���t �� �̺߳���func_
	// ���÷����߳� pthread_detach
	t.detach();
}

int Thread::getThreadId() const
{
	return threadId_;
}
