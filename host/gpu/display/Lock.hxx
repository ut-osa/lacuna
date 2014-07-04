/*
 * Lock.hxx
 *
 *  Created on: Dec 25, 2008
 *      Author: marks
 */

#ifndef LOCK_HXX_
#define LOCK_HXX_
#include <pthread.h>


#define SAFE_CALL(x) if (x) assert(0);

// java-like Lock which encapsulates conditional
class Lock{
	friend struct Locker;

public:

	class Conditional{

		friend class Lock;
		Lock& _myLock;
	public:
		void wait(){
			SAFE_CALL(pthread_cond_wait(&_conditional,&(_myLock._mutex)));
		}

		void signal(){
			SAFE_CALL(pthread_cond_signal(&_conditional));
		}

		~Conditional(){
			pthread_cond_destroy(&_conditional);
		}
	private:
		Conditional(Lock& myLock):_myLock(myLock){
			SAFE_CALL(pthread_cond_init(&_conditional,NULL));
		}
		///
		Conditional(const Conditional&);
		Conditional& operator=(const Conditional&);
		//

	private:
		pthread_cond_t _conditional;
	};


public:

	Conditional conditional;

public:

	Lock():conditional(*this){
		SAFE_CALL(pthread_mutex_init(&_mutex,NULL));
	}
	~Lock(){
		pthread_mutex_destroy(&_mutex);
	}

private:

	void lock(){
		SAFE_CALL(pthread_mutex_lock(&_mutex));
	}

	void release(){
		SAFE_CALL(pthread_mutex_unlock(&_mutex));
	}

	pthread_mutex_t _mutex;

	Lock(const Lock&);
	Lock& operator=(const Lock&);

};
// Locker must be used to lock or unlock -> this is the way to ensure that once Locker is destroyed,
//  the lock is unlocked - just the way "synchronized" works in java

struct Locker{
	private:
		Lock& _lock;
	public:
	Locker(Lock& toLock):_lock(toLock){
		_lock.lock();
	}
	~Locker(){
		_lock.release();
	}
};


#endif /* LOCK_HXX_ */
