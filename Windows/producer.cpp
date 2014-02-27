#include "stdafx.h"
#include <queue>
#include "threads.h"
#include "threadrunner.h"

extern MT::Queue<int> g_msgs;
extern MT::HandleWrapper g_hEmptyEvent, g_hFullEvent, g_hEmptyMutEvent, g_hFullMutEvent, g_hMutex;

const char FULL_BUFFER[]      = "Producer: full buffer, waiting";
const char PRODUCER_WAKE_UP[] = "Producer: waking up";
const char TASKS_FINISHED[]   = "Producer: tasks finished, exiting.";

// diagnostics
bool isSignalled(const MT::HandleWrapper& h, const std::string& hName, const std::string& who, 
                 bool diagnostics = false) {

    DWORD dwResult = ::WaitForSingleObject(h, 0);
    bool res = (dwResult != WAIT_TIMEOUT);
    if (diagnostics) {
        stringstream ss;
        ss << who << " " << hName <<  " is " << ( res ? "" : "NOT" ) << " signalled";
        MT::ThreadRunner::Print(ss.str().c_str());
    }

    return res;
}

namespace MT {

// Just locking shared data structure with Critical Sections
unsigned __stdcall ProducerConsumerCSRunner::Producer(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    CriticalSection prod_cs;
    SyncTimerState tState = ST_WORK;

    // we will finish either when produce m_maxTasks or global timeout occurs
    for (int nTask = 1; nTask <= m_maxTasks; nTask++) {

        Produce(); // imitate work, exception safe
        const int fullBufferWait = 300; // 0.3 sec

        bool isFull = false;
        do {
            {   // all access to shared writable memory should be protected by exclusive lock
                Lock lock(prod_cs);    // acquire lock
                isFull = g_msgs.isFull();
            } // release lock
            if (isFull) {
                Print(FULL_BUFFER);   // buffer is full -
                Wait(fullBufferWait); // wait some period for consumer
            }
        } while ( (tState = syncTimer.State())==ST_WORK && isFull ) ; // check timeout waiting for free buffer

        if (tState != ST_WORK) { // check timeout
            if (tState == ST_ERR)
                return ERR_SYNC;
            PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
            return RET_OK;
        }

        {
            Lock lock(prod_cs);
            try {
                g_msgs.push(nTask);

            } catch(std::exception& ex) { // in case of uncaught exception Lock desctructor
                Print(ex.what());         // will release the lock
                return ERR_STD;
            } catch(...) {
                Print("Unknown error");
                return ERR_UNKNOWN;
            }
        }
        Print("sent: ", nTask);
    } // for

    PutThreadFinishMsg( TASKS_FINISHED );
    return RET_OK;
}

// Using Events for synchronisation
unsigned __stdcall ProducerConsumerEventRunner::Producer(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    CriticalSection prod_cs;
    SyncTimerState tState = ST_WORK;
    bool diagnostic = false; // debug messages

    // we will finish either when produce m_maxTasks or global timeout occurs
    for (int nTask = 1; nTask <= m_maxTasks; nTask++) {

        Produce(); // imitate work, exception safe

        isSignalled(g_hEmptyEvent, "Producer: ", "g_hEmptyEvent", diagnostic);
        isSignalled(g_hFullEvent,  "Producer: ", "g_hFullEvent",  diagnostic);

        const int fullBufferTimeout = 5000; // 5 sec
        bool isFull = true;
        while ( (tState = syncTimer.State())==ST_WORK && isFull) { // check timeout waiting for free buffer
            {
                Lock lock(prod_cs);
                isFull = g_msgs.isFull();
                if (isFull)
                    Print(FULL_BUFFER);
            } // release lock

            if (isFull) { // buffer is full, wait event from consumer
                ::ResetEvent(g_hEmptyEvent);
                DWORD dwResult = ::WaitForSingleObject(g_hEmptyEvent, fullBufferTimeout);
                if (dwResult == WAIT_FAILED)
                    return ERR_SYNC; // error, exiting

                if (dwResult == WAIT_TIMEOUT)
                    continue; // buffer is still full, check global timer

                // WAIT_OBJECT_0 - event signalled, buffer is free
                Print(PRODUCER_WAKE_UP);
                isFull = false;
            }
        } // while

        if (tState != ST_WORK) {
            if (tState == ST_ERR)
                return ERR_SYNC;
            PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
            return RET_OK;
        }

        {
            Lock lock(prod_cs);
            try {
                g_msgs.push(nTask);

            } catch(std::exception& ex) {
                Print(ex.what());
                return ERR_STD;
            } catch(...) {
                Print("Unknown error");
                return ERR_UNKNOWN;
            }
            Print("sent: ", nTask);
            ::SetEvent(g_hFullEvent);
        }

    } // for

    PutThreadFinishMsg( TASKS_FINISHED );
    return RET_OK;
}

// Using mutex for synchronisation
unsigned __stdcall  ProducerConsumerMutexRunner::Producer(void* args) {

    const SyncTimer& syncTimer  = SyncTimer::Instance();
    SyncTimerState tState = ST_WORK;
    bool diagnostic = false; // debug messages

    // we will finish either when produce m_maxTasks or global timeout occurs
    for (int nTask = 1; nTask <= m_maxTasks; nTask++) {

        Produce(); // imitate work, exception safe
        const int fullBufferTimeout = 5000; // 5 sec

        bool isFull = true;
        while ( (tState = syncTimer.State())==ST_WORK && isFull) { // check timeout waiting for free buffer

            isSignalled(g_hEmptyMutEvent, "Producer: ", "g_hEmptyEvent", diagnostic);
            isSignalled(g_hFullMutEvent,  "Producer: ", "g_hFullEvent",  diagnostic);

            Produce(); // imitate work, exception safe

            DWORD dwResult = ::WaitForSingleObject(g_hMutex, INFINITE);
            if (dwResult != WAIT_OBJECT_0)
                return ERR_SYNC; // error, exiting

            // now we own the mutex
            isFull = g_msgs.isFull();
            if (isFull) {  // buffer is full, wait event from consumer

                Print(FULL_BUFFER);
                ::ReleaseMutex(g_hMutex);

                ::ResetEvent(g_hEmptyMutEvent);
                DWORD dwResult = ::WaitForSingleObject(g_hEmptyMutEvent, fullBufferTimeout);
                if (dwResult == WAIT_FAILED)
                    return ERR_SYNC; // error, exiting

                if (dwResult == WAIT_TIMEOUT)
                    continue; // buffer is still full, check global timer

                // WAIT_OBJECT_0 - event signalled,  buffer is free
                Print(PRODUCER_WAKE_UP);
                isFull = false;
            }
        } // while

        // now we own the mutex

        if (tState != ST_WORK) {
            ::ReleaseMutex(g_hMutex);
            if (tState == ST_ERR)
                return ERR_SYNC;
            PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
            return RET_OK;
        }

        try {
            g_msgs.push(nTask);
        
        } catch(std::exception& ex) { // should catch all exception in the thread to avoid indefinite locks
            Print( ex.what());        // by not releasing mutex
            ::ReleaseMutex(g_hMutex);
            return ERR_STD;
        } catch(...) {  
            Print("Unknown error");
            ::ReleaseMutex(g_hMutex);
            return ERR_UNKNOWN;
        }

        Print("sent: ", nTask);
        ::ReleaseMutex(g_hMutex);
        ::SetEvent(g_hFullMutEvent);

    } // for

    PutThreadFinishMsg( TASKS_FINISHED );
    return RET_OK;
}

} // namespace MT
