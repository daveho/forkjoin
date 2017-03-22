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

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <cassert>
#include "fj.h"

#ifdef NDEBUG
#  define DEBUG(args...)
#else
#  define DEBUG(args...) printf(args)
#endif

// ----------------------------------------------------------------------
// The WorkerKey singleton class is responsible for setting up and
// accessing thread-specific data to allow worker threads to
// find their corresponding Worker object.
// ----------------------------------------------------------------------

class WorkerKey {
private:
	pthread_key_t m_key;

	static WorkerKey s_instance;

public:
	WorkerKey();

	void setWorker(Worker *w);
	Worker *getWorker();

	static WorkerKey *instance();
};

// The single static instance
WorkerKey WorkerKey::s_instance;

WorkerKey::WorkerKey()
{
	int rc;

	rc = pthread_key_create(&m_key, 0);

	if (rc < 0) {
		fprintf(stderr, "Could not create thread specific data key: error %d\n", rc);
		exit(1);
	}
}

void WorkerKey::setWorker(Worker *w)
{
	pthread_setspecific(m_key, (const void *) w);
}

Worker *WorkerKey::getWorker()
{
	return (Worker *) pthread_getspecific(m_key);
}

WorkerKey *WorkerKey::instance()
{
	return &s_instance;
}

// ----------------------------------------------------------------------
// Task base class
// ----------------------------------------------------------------------

Task::Task()
	: m_isDone(false)
{
	// Initialize result to something reasonable
	m_result.val = 0;

	pthread_mutex_init(&m_statusLock, 0);
}

Task::~Task()
{
	pthread_mutex_destroy(&m_statusLock);
}

void Task::fork()
{
	WorkerKey::instance()->getWorker()->fork(this);
}

result_t Task::join(bool destroy)
{
	return WorkerKey::instance()->getWorker()->join(this, destroy);
}

result_t Task::getResult()
{
	assert(isDone());

	result_t result;

	pthread_mutex_lock(&m_statusLock);
	result = m_result;
	pthread_mutex_unlock(&m_statusLock);

	return result;
}

bool Task::isDone()
{
	bool isDone;

	pthread_mutex_lock(&m_statusLock);
	isDone = m_isDone;
	pthread_mutex_unlock(&m_statusLock);

	return isDone;
}

// This method is called by the worker when the execution of
// a task is complete.
void Task::setComplete(result_t result)
{
	pthread_mutex_lock(&m_statusLock);
	m_result = result;
	m_isDone = true;
	pthread_mutex_unlock(&m_statusLock);
}

// ----------------------------------------------------------------------
// Worker class
// ----------------------------------------------------------------------

// When a worker becomes idle, its initial backoff sleep time is
// 25,000 microseconds (which is 25 milliseconds).
const useconds_t INITIAL_BACKOFF_US = ((useconds_t) 25000);

// Maximum idle backoff is 800,000 microseconds (800 milliseconds).
const useconds_t MAX_BACKOFF_US = ((useconds_t) 800000);

Worker::Worker(WorkerGroup *group)
	: m_group(group)
	, m_nextBackoff(INITIAL_BACKOFF_US)
	, m_idle(false)
	, m_shutdown(false)
{
	pthread_mutex_init(&m_queueLock, 0);

	// See the random number generator this worker will use
	srand48_r((long) time(0), &m_randState);
}

Worker::~Worker()
{
	pthread_mutex_destroy(&m_queueLock);
}

void Worker::start()
{
	int rc = pthread_create(&m_threadID, 0, &Worker::startFunc, (void*) this);
	if (rc < 0) {
		fprintf(stderr, "Warning: could not create Worker\n");
	}
}

void Worker::fork(Task *t)
{
	// Forking is simply adding a task to the queue

	pthread_mutex_lock(&m_queueLock);
	m_taskQueue.push_back(t);
	pthread_mutex_unlock(&m_queueLock);
}

result_t Worker::join(Task *t, bool destroy)
{
	while (!t->isDone()) {
		// Find something useful to do
		Task *t = findWork();

		if (t != 0) {
			// Found a Task: execute it right now
			executeSynchronously(t);
		} else {
			// Nothing to do right now
			idleWhileJoining();
		}
	}

	result_t result = t->getResult();

	if (destroy) {
		delete t;
	}

	return result;
}

void *Worker::startFunc(void *arg)
{
	Worker *w = (Worker *) arg;

	// Install a pointer to the worker in this thread's
	// thread-specific data.
	WorkerKey::instance()->setWorker(w);

	w->run();
	return 0;
}

void Worker::run()
{
	// Main loop for a Worker thread

	while (!m_shutdown) {
		Task *t = findWork();

		if (t != 0) {
			executeSynchronously(t);
		} else {
			// Not only does this worker not have any
			// tasks on its queue, it was unable to steal
			// a task from another thread, so it's
			// truly idle.
			idle();
		}
	}
}

Task *Worker::findWork()
{
	// See if there's a task on my queue
	Task *t = tryDequeueTask();

	if (t == 0) {
		// No task on my queue: try to steal
		long randVal;
		lrand48_r(&m_randState, &randVal);
		t = m_group->trySteal(this, randVal);
	}

	if (m_idle && t != 0) {
		m_idle = false;

		// Reset backoff timer
		m_nextBackoff = INITIAL_BACKOFF_US;
	}

	return t;
}

Task *Worker::tryDequeueTask()
{
	Task *t = 0;

	pthread_mutex_lock(&m_queueLock);
	if (m_taskQueue.size() > 0) {
		t = m_taskQueue.back();
		m_taskQueue.pop_back();
	}
	pthread_mutex_unlock(&m_queueLock);

	return t;
}

Task *Worker::tryStealFrom()
{
	Task *t = 0;

	pthread_mutex_lock(&m_queueLock);
	if (m_taskQueue.size() > 0) {
		// Tasks are stolen from the head of the queue,
		// since these are the oldest tasks (and thus least
		// likely to be closely related to the
		// task(s) the victim Worker is currently working on)
		t = m_taskQueue.front();
		m_taskQueue.pop_front();

		//DEBUG("Stole a task!\n");
	}
	pthread_mutex_unlock(&m_queueLock);

	return t;
}

void Worker::executeSynchronously(Task *t)
{
	t->setComplete(t->execute());
}

void Worker::idleWhileJoining()
{
	m_idle = true;
	backoff();
}

void Worker::idle()
{
	assert(m_taskQueue.empty());

	// Let WorkerGroup know that this worker has no
	// tasks, and see if computation has finished
	m_shutdown = m_group->workerHasNoTasks();

	if (!m_shutdown) {
		// Cool off for a bit
		m_idle = true;
		backoff();
	}
}

// Do an exponential backoff
void Worker::backoff()
{
	usleep(m_nextBackoff);

	if (m_nextBackoff < MAX_BACKOFF_US) {
		m_nextBackoff *= 2;
		assert(m_nextBackoff <= MAX_BACKOFF_US);
	}
}

// ----------------------------------------------------------------------
// WorkerGroup class
// ----------------------------------------------------------------------

WorkerGroup::WorkerGroup(int numWorkers)
	: m_rootTask(0)
	, m_workerList(0)
	, m_numWorkers(numWorkers)
	, m_started(false)
	, m_finished(false)
{
	pthread_mutex_init(&m_lock, 0);
	pthread_cond_init(&m_cond, 0);

	m_workerList = new Worker*[numWorkers];

	for (int i = 0; i < numWorkers; i++) {
		m_workerList[i] = new Worker(this);
	}
}

WorkerGroup::~WorkerGroup()
{
	assert(!m_started || m_finished);

	pthread_mutex_destroy(&m_lock);
	pthread_cond_destroy(&m_cond);

	for (int i = 0; i < m_numWorkers; i++) {
		delete m_workerList[i];
	}

	delete[] m_workerList;
}

void WorkerGroup::start(Task *rootTask)
{
	m_rootTask = rootTask;

	// Worker 0 is seeded with the root task
	m_workerList[0]->fork(rootTask);

	// Start workers
	for (int i = 0; i < m_numWorkers; i++) {
		m_workerList[i]->start();
	}
}

void WorkerGroup::wait()
{
	pthread_mutex_lock(&m_lock);

	while (!m_rootTask->isDone()) {
		pthread_cond_wait(&m_cond, &m_lock);
	}
	m_finished = true;

	pthread_mutex_unlock(&m_lock);
}

Task *WorkerGroup::trySteal(Worker *thief, long randVal)
{
	// Starting with a random worker (chosen based on randVal),
	// try workers in order, trying to steal a task.

	int i = (int) (randVal % m_numWorkers), count = 0;
	assert(i >= 0);

	Task *t = 0;

	while (count < m_numWorkers) {
		Worker *potentialVictim = m_workerList[i];

		if (potentialVictim != thief) {
			t = potentialVictim->tryStealFrom();
			if (t != 0) {
				break;
			}
		}

		i = (i + 1) % m_numWorkers;
		count++;
	}

	return t;
}

// Called by a worker that has no tasks.
// Returns true if the overall computation has finished.
bool WorkerGroup::workerHasNoTasks()
{
	bool isShutdown;

	pthread_mutex_lock(&m_lock);

	// Let the main thread (the one that started the computation)
	// know that at least one worker has no work
	pthread_cond_broadcast(&m_cond);

	// Check to see if the computation has finished
	isShutdown = m_finished;

	pthread_mutex_unlock(&m_lock);

	return isShutdown;
}

