#include "stdafx.h"
#include "threads.h"
#include "threadrunner.h"

volatile LONG g_semThreadNum = 0; // short number of semaphore threads to increase readability
long g_semCounter = 0;

namespace MT {

// Factory Method
ThreadRunner* ThreadRunnerCreator::Create(SyncType syncType)
{
    switch (syncType) {
        case SEMAPHORE:
            return new SemaphoreRunner;
        case CS:
            return new ProducerConsumerCSRunner;
        case CS_EVENT:
            return new ProducerConsumerEventRunner;
        case MUTEX:
        default:
            return new ProducerConsumerMutexRunner;
    }
}

int ThreadRunner::InitTimer(long long interval) {

    // synchronise stopping of all threads after specified timeout 
    // using single common global WaitableTimer object

    SyncTimer& syncTimer = SyncTimer::Instance();
    if (!syncTimer.isValid())
        return ERR_API;

    LARGE_INTEGER timeout; // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686289(v=vs.85).aspx
    timeout.QuadPart = interval; // if negative, the absolute value is interpreted as relative
                                 // to the current time on the clock, in 100ns interval
    if (!syncTimer.SetTimer(timeout))
        return ERR_API;
    return RET_OK;
}

int ThreadRunner::Init() const {
    int ret = InitTimer();
    if (ret != RET_OK)
        return ret;
    return InitSyncObjects(); // derived object virtual function call - type is known at runtime
                              // runtime polymorphism
}

int ProducerConsumerRunner::RunThreads() const {

    int ret = Init();
    if (ret != RET_OK)
        return ret;

    HANDLE   threadHandles[m_totalThreads] = { 0 };
    unsigned threadIDs[m_totalThreads]     = { 0 };

   // get thread functions of current object (virtual functions calls)
    THREAD_FUNCTION *Producer = GetProducerThreadFunctionPtr();
    THREAD_FUNCTION *Consumer = GetConsumerThreadFunctionPtr();

    // in case a thread is not created - do not create remaining threads,
    // wait for created and return

    int createdThreads = 0;
    threadHandles[0] = (HANDLE) _beginthreadex(
        NULL,     // securtiy
        0,        // thread stack size, 0 - default size (1 Mb)
        Producer, // start address of the thread function: unsigned ( __stdcall *start_address )( void * )
        0,        // arguments for the thread function (void*)
        0,        // Initflag - Initial state of a new thread 
                  // (0 for running or CREATE_SUSPENDED for suspended);
        &threadIDs[0] // output: receive thread id
        );        // see: http://msdn.microsoft.com/en-us/library/kdzttdcb(v=vs.90).aspx

    if (threadHandles[0] != 0) {
        createdThreads++;
        threadHandles[1] = (HANDLE) _beginthreadex( NULL, 0, Consumer, 0, 0, &threadIDs[1] );
        if (threadHandles[1] != 0)
            createdThreads++;
    }

    // It may happen that only one of the threads was created.
    // In such case we will wait while this thread will correctly exit by timeout 

    DWORD dwRet = 0;
    bool allThreadsOK = false;
    if (createdThreads > 0) {
        dwRet = ::WaitForMultipleObjects( // wait all created threads to exit

            createdThreads, // number of handles of created threads in threadHandles
            threadHandles,  // const HANDLE* - an array of handles to thread, process, mutex, event, semaphore
                    // waitable timer, change notification, console input, memory resource notification.
                    // see also: http://msdn.microsoft.com/en-us/library/windows/desktop/ms687025(v=vs.85).aspx
            TRUE ,  // bWaitALl
            INFINITE  // DWORD dwMilliseconds. INFINITE - wait all threads to finish.
            );

        // check status and close created thread handles
        allThreadsOK = true;
        for (int i=0; i<m_totalThreads; i++) {
            if (threadHandles[i] == 0) { // some threads were not created - nothning was done
                allThreadsOK = false;
                continue;
            }
            if (allThreadsOK) {
                DWORD code = 0;
                ::GetExitCodeThread(threadHandles[i], &code);
                if (code != 0)
                    allThreadsOK = false;
            }
            ::CloseHandle(threadHandles[i]);
        }
    } 

    // resourses will be auto cleaned up
    if (createdThreads != m_totalThreads)
        return ERR_API;

    if (dwRet == WAIT_FAILED) // WaitForMultipleObject return code
        return ERR_API;

    if (dynamic_cast<const ProducerConsumerMutexRunner*>(this) != 0)
        if (dwRet == WAIT_ABANDONED)
            return ERR_SYNC;

    if (!allThreadsOK)
         ERR_SYNC;

    return RET_OK;
}

int SemaphoreRunner::RunThreads() const {

    // semaphore
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms685129(v=vs.85).aspx
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686946(v=vs.85).aspx

    // Maintains a counter which is decreasing whe Wait function succeeds and increasing
    // when semaphore is released. The state is signaled when counter > 0.

    // For example, if the counter is set to 2, only two threads can work simultaneously:
    // both had succeed wait function and decremented the counter so it became 0 and 
    // the third thread wait function will not succeed - semaphore state is NOT signalled.

    int ret = Init();
    if (ret != RET_OK)
        return ret;

    g_semCounter   = m_semInitCount;
    g_semThreadNum = 0;  // reset thread number counter (for next calls of SemaphoreThreadFunction)

    std::vector<HANDLE> threadHandles(m_totalThreads);
    std::vector<unsigned> threadIDs(m_totalThreads);

    // in case a thread is not created - do not create remaining threads, 
    // wait for created, cleen up and return
    int createdThreads = 0;
    for (int i=0; i<m_totalThreads; i++, createdThreads++) { // try to create > MAX_SEM_COUNT threads
        threadHandles[i] = (HANDLE)  _beginthreadex( NULL, 0, &SemaphoreThreadFunction, 
            (void*)&m_semInitCount, 0, &threadIDs[i] );

        if (threadHandles[i] == 0)
            break;
    }

    DWORD dwRet = 0;
    bool allThreadsOK = false;

    if (createdThreads > 0) {
        dwRet = ::WaitForMultipleObjects(
            createdThreads,
            &threadHandles[0],
            TRUE, // wait all threads
            INFINITE
            );

        // check status and close thread handles
        allThreadsOK = true;
        for (int i=0; i<m_totalThreads; i++) {
            if (threadHandles[i] == 0) { // some threads were not created - nothning was done
                allThreadsOK = false;
                continue;
            }
            if (allThreadsOK) {
                DWORD code = 0;
                ::GetExitCodeThread(threadHandles[i], &code);
                if (code != 0)
                    allThreadsOK = false;
            }
            ::CloseHandle(threadHandles[i]);
        }
    }

    // resourses will be auto cleaned up
    if (createdThreads != m_totalThreads)
        return ERR_API;

    if (dwRet == WAIT_FAILED)
        return ERR_API;

    if (!allThreadsOK)
        ERR_SYNC;

    return RET_OK;
}

} // namespace MT