#pragma once

namespace MT { 

class ThreadRunner {
public:
    static const long long m_defInterval   = -160000000LL; // 16 seconds
    static int InitTimer(long long interval= m_defInterval);

    int Init() const;
    virtual int RunThreads() const =0;
    virtual int InitSyncObjects() const =0;

    // common helpers
    static void Wait(int ms) {
        ::Sleep(ms);
    }
    static void Produce(int ms = rand()%10 * 50) {
        Wait(ms);
    }

    // protected by lock to synchronise output
    static void Print(const char* msg) {
        Lock lock(m_cout_cs);
        cout << msg << endl;
    }
    static void Print(const char* msg, int value) {
        Lock lock(m_cout_cs);
        cout << msg << value << endl;
    }

    static void PutThreadFinishMsg(const char* msg, unsigned int timeout=0) {
        Lock lock(m_cout_cs);
        cout << endl << msg;
        if (timeout != 0)
            cout << timeout << " sec.";
        cout << " Thread Id: " << ::GetCurrentThreadId() << endl << endl;
    }

private:
    static CriticalSection m_cout_cs;
};

class  ThreadRunnerCreator {  // Factory Method GOF Pattern
public:
    static ThreadRunner* Create(SyncType syncType);
};

// mutlithreaded access to shared read/write memory: producer-consumer problem
// http://en.wikipedia.org/wiki/Producer-consumer_problem

class ProducerConsumerRunner : public ThreadRunner {
public:
    static void Consume(int msg) { // consume item #msg
        int ms = rand()%14 * 50;
        Wait(ms);
    }
    static const unsigned m_maxTasks     = 30; // number of tasks to produce (model empty buffer condition)

    virtual int RunThreads() const;
    virtual int InitSyncObjects() const = 0;
    virtual THREAD_FUNCTION* GetProducerThreadFunctionPtr() const = 0;
    virtual THREAD_FUNCTION* GetConsumerThreadFunctionPtr() const = 0;

private:
    static const unsigned m_totalThreads = 2;  // producer and consumer

};

// only locking shared memory with Critical Sections
class ProducerConsumerCSRunner : public ProducerConsumerRunner {
public:
    static THREAD_FUNCTION Producer;
    static THREAD_FUNCTION Consumer;

    virtual int InitSyncObjects() const {
        return RET_OK;
    }
    virtual THREAD_FUNCTION* GetProducerThreadFunctionPtr() const {
        return &Producer;
    }
    virtual THREAD_FUNCTION* GetConsumerThreadFunctionPtr() const {
        return &Consumer;
    }
};

// using Events for synchronisation
class ProducerConsumerEventRunner : public ProducerConsumerRunner {
public:
    static THREAD_FUNCTION Producer;
    static THREAD_FUNCTION Consumer;

    virtual int InitSyncObjects() const ;
    virtual THREAD_FUNCTION* GetProducerThreadFunctionPtr() const {
        return &Producer;
    }
    virtual THREAD_FUNCTION* GetConsumerThreadFunctionPtr() const {
        return &Consumer;
    }
};

// using Mutex for synchronisation
class ProducerConsumerMutexRunner : public ProducerConsumerRunner {
public:
    static THREAD_FUNCTION Producer;
    static THREAD_FUNCTION Consumer;

    virtual int InitSyncObjects() const;
    virtual THREAD_FUNCTION* GetProducerThreadFunctionPtr() const {
        return &Producer;
    }
    virtual THREAD_FUNCTION* GetConsumerThreadFunctionPtr() const {
        return &Consumer;
    }
};

class SemaphoreRunner : public ThreadRunner { // sample usage of Semaphore
public:
    static const int defTotalThreads = 3;
    static THREAD_FUNCTION SemaphoreThreadFunction;

    SemaphoreRunner(int totalThreads=defTotalThreads) : m_totalThreads(totalThreads),
        m_semInitCount(m_totalThreads-1) { // initialisation in the order of declaration
    }
    virtual int RunThreads() const;
    virtual int InitSyncObjects() const;

private:
    const int  m_totalThreads;
    const long m_semInitCount; // initial semaphore object counter
};

} // namespace MT
