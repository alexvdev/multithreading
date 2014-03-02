#include "stdafx.h"
#include "threads.h"
#include "threadrunner.h"

extern MT::HandleWrapper g_hSemaphore;
extern long g_semCounter;
extern volatile LONG g_semThreadNum;

LONG getNextNumber() { // assign short order number to threads to increase readability
    // need to reset the counter in each new wait function, so use global which is reset
    // in primary thread (static would not play).
    ::InterlockedIncrement(&g_semThreadNum);
    return g_semThreadNum;
}

namespace MT {

unsigned __stdcall SemaphoreRunner::SemaphoreThreadFunction(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    const int threadNum   = getNextNumber();
    SyncTimerState tState = ST_WORK;
    CriticalSection sem_cs;

    while ( (tState = syncTimer.State())==ST_WORK ) {

        // checking the semaphore state to know if it is allowed to work 
        DWORD dwResult = ::WaitForSingleObject(g_hSemaphore, 
            0); // 0-timeout

        if (dwResult == WAIT_FAILED)
            return ERR_SYNC;

        if (dwResult == WAIT_OBJECT_0) { // semaphore is signalled
            {
                Lock lock(sem_cs);
                g_semCounter--; // semaphore counter was decremented
                stringstream ss;
                ss << "Thread " << threadNum << ": starting to work, counter: " << g_semCounter;
                Print(ss.str().c_str());
            }
            
            const int  produceFactor = rand()/10;
            Produce(produceFactor); // produce some work
            {
                Lock lock(sem_cs);
                g_semCounter++;
                stringstream ss;
                ss << "Thread " << threadNum << ": releasing, counter: " << g_semCounter;
                Print(ss.str().c_str());
            }

            ::ReleaseSemaphore( 
                    g_hSemaphore, // handle to semaphore
                    1,            // increase count by one
                    NULL);        // not interested in previous count

        } // else: WAIT_TIMEOUT -  semaphore was not signalled
    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    stringstream ss;
    ss << TIMEOUT    << syncTimer.GetTimeoutInsSec() << ". Thread N "
       <<  threadNum << ", thread Id: " << ::GetCurrentThreadId() << endl;
    Print(ss.str().c_str());
    return RET_OK;
}

} // namespace MT