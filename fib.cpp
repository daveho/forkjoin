// A simple test for the fork/join framework

#include <cstdio>
#include <cstdlib>
#include "fj.h"

class FibTask : public Task {
private:
	unsigned long m_num;

public:
	FibTask(unsigned long num);
	virtual ~FibTask();

	virtual result_t execute();

};

FibTask::FibTask(unsigned long num)
	: m_num(num)
{
}

FibTask::~FibTask()
{
}

result_t FibTask::execute()
{
	result_t answer;

	if (m_num == 0 || m_num == 1) {
		// base case
		answer.val = 1;
	} else {
		// recursive case: spawn parallel sub-tasks
		FibTask *left = new FibTask(m_num - 2);
		FibTask *right = new FibTask(m_num - 1);

		left->fork();
		right->fork();

		result_t leftAnswer = left->join();
		result_t rightAnswer = right->join();

		answer.val = leftAnswer.val + rightAnswer.val;
	}

	return answer;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: fib <num> <num threads>\n");
		exit(1);
	}

	int num;
	int numThreads;

	num = atoi(argv[1]);
	numThreads = atoi(argv[2]);

	WorkerGroup group(numThreads);

	FibTask *rootTask = new FibTask((unsigned long) num);

	group.start(rootTask);
	group.wait();

	result_t answer = rootTask->getResult();

	printf("fib(%i) is %lu\n", num, answer.val);

	delete rootTask;

	return 0;
}

