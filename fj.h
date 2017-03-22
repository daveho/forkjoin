// Parallel fork/join framework for pthreads

// Copyright (c) 2009, David H. Hovemeyer <david.hovemeyer@gmail.com>
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

#ifndef FJ_H
#define FJ_H

#include <deque>
#include <unistd.h>    // for usleep()
#include <pthread.h>

using namespace std;

typedef union {
	unsigned long val;
	void *ptr;
} result_t;

class Worker;
class WorkerGroup;

class Task {
private:
	friend class Worker;
	friend class WorkerGroup;

	bool m_isDone;
	result_t m_result;
	pthread_mutex_t m_statusLock;

public:
	// Public methods: any class may call these
	Task();
	virtual ~Task();

	virtual result_t execute() = 0;
	result_t getResult();

protected:
	// Task subclasses may call these methods
	void fork();
	result_t join(bool destroyTaskWhenComplete = true);

private:
	// Private methods: only called by framework classes (e.g., Worker)
	bool isDone();
	void setComplete(result_t result);
};


class Worker {
private:
	friend class WorkerGroup;

	WorkerGroup *m_group;
	pthread_t m_threadID;
	pthread_mutex_t m_queueLock;
	deque<Task *> m_taskQueue;
	drand48_data m_randState;
	useconds_t m_nextBackoff;
	bool m_idle;
	bool m_shutdown;

public:
	Worker(WorkerGroup *group);
	~Worker();

	void start();

	void fork(Task *t);
	result_t join(Task *t, bool destroyTaskWhenComplete);

private:
	static void *startFunc(void *arg);

	void run();

	Task *findWork();

	Task *tryDequeueTask();

	Task *tryStealFrom();

	void executeSynchronously(Task *t);
	void idleWhileJoining();
	void idle();
	void backoff();
};

class WorkerGroup {
private:
	friend class Worker;

	pthread_mutex_t m_lock;
	pthread_cond_t m_cond;

	Task *m_rootTask;
	Worker **m_workerList;
	int m_numWorkers;

	bool m_started;
	bool m_finished;

public:
	WorkerGroup(int numWorkers);
	~WorkerGroup();

	void start(Task *rootTask);
	void wait();

private:
	Task *trySteal(Worker *thief, long randVal);
	bool workerHasNoTasks();
};

#endif // FJ_H
