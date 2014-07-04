/*
 * Thread.hxx
 *
 *  Created on: Jan 1, 2009
 *      Author: marks
 */

#ifndef THREAD_HXX_
#define THREAD_HXX_

#include <pthread.h>

template<class ToRun>
class Thread {

	ToRun& _toRun;


	bool _started;
	pthread_t _threadId;

public:

	Thread(ToRun& toRun) :
		_toRun(toRun),_started(false),_threadId() {
	}

	typedef  void *(*start_routine)(void*);

	int start(){
		// check that we haven't been started yet
		if(_started){
			return -1;
		}

		_started=true;

		if(pthread_create(&_threadId, NULL, (start_routine)(ToRun::run), (void*)&_toRun)) return -1;
	}

	void join(){
		if (!_started) return;
		void* returnVal;
		pthread_join(_threadId,&returnVal);
	}

	~Thread(){
		join();
	}
};

#endif /* THREAD_HXX_ */
