#include "stdafx.h"
#include <queue>
#include "threads.h"
#include "threadrunner.h"

extern MT::Queue<int> g_msgs;
extern MT::HandleWrapper g_hEmptyEvent, g_hFullEvent, g_hEmptyMutEvent, g_hFullMutEvent, g_hMutex;

bool isSignalled(const MT::HandleWrapper& h, const std::string& hName, const std::string& who,
                 bool diagnostics = false);

const char EMPTY_BUFFER[]     = "Consumer: empty buffer, waiting";
const char CONSUMER_WAKE_UP[] = "Consumer: waking up";

namespace MT {

// Just locking shared data structure with Critical Sections.
// Using RAAI idiom for aquiring locks
unsigned __stdcall ProducerConsumerCSRunner::Consumer(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    CriticalSection cons_cs;
    const int emptyBufferWait = 1000; // 1 sec
    SyncTimerState tState = ST_WORK;

    while ( (tState = syncTimer.State()) == ST_WORK ) {
        bool isEmpty = false;
        {
            Lock lock(cons_cs);      // acquire lock
            if (g_msgs.empty()) { // nothing to produce, need synchronisation
                isEmpty = true;
                Print(EMPTY_BUFFER);
            }
        } // release lock
        if (isEmpty) {
            Wait(emptyBufferWait);
            continue; // wait until there will be some input in the buffer or timeout occurs
        }

        int cur_msg=0;
        {
            Lock lock(cons_cs);
            try {
                cur_msg = g_msgs.front();
                g_msgs.pop();
            } catch(std::exception& ex) {
                Print(ex.what());
                return ERR_STD;
            } catch(...) {
                Print("Unknown error ");
                return ERR_UNKNOWN;
            }
            stringstream ss;
            ss << "received:" <<  cur_msg;
            Print(ss.str().c_str());
        }
        Consume(cur_msg);

    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
    return RET_OK;
}

// Using Critical Sections and Events for synchronisation
unsigned __stdcall ProducerConsumerEventRunner::Consumer(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    CriticalSection cons_cs;
    const int emptyBufferTimeout = 3000; // 3 sec
    SyncTimerState tState = ST_WORK;
    bool diagnostic=false; // debug messages

    while ( (tState = syncTimer.State())==ST_WORK ) {
        
        isSignalled(g_hEmptyEvent, "Consumer: ", "g_hEmptyEvent", diagnostic);
        isSignalled(g_hFullEvent,  "Consumer: ", "g_hFullEvent", diagnostic);

        bool isEmpty = false;
        {
            Lock lock(cons_cs); // any access to writable shared memory should be protected by lock
            isEmpty = g_msgs.empty();
            if (isEmpty)
                Print(EMPTY_BUFFER);
        }

        if (isEmpty) {
            ::ResetEvent(g_hFullEvent); // nothing to consume, need synchronisation
            DWORD dwResult = ::WaitForSingleObject(g_hFullEvent, emptyBufferTimeout);
            if (dwResult == WAIT_FAILED)
                return ERR_SYNC; // error, exiting

            if (dwResult == WAIT_TIMEOUT)
                continue; // check global timer

            // WAIT_OBJECT_0 - event signalled
            Print(CONSUMER_WAKE_UP);
        }

        int cur_msg = 0;
        {
            Lock lock(cons_cs);
            try {
                cur_msg = g_msgs.front();
                g_msgs.pop();

            } catch(std::exception& ex) {
                Print(ex.what());
                return ERR_STD;
            } catch(...) {
                Print("Unknown error ");
                return ERR_UNKNOWN;
            }

            stringstream ss;
            ss << "received:" <<  cur_msg;
            Print(ss.str().c_str());

            ::SetEvent(g_hEmptyEvent);
        }

        Consume(cur_msg);

    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
    return RET_OK;
}

// Using mutex for synchronisation
unsigned __stdcall ProducerConsumerMutexRunner::Consumer(void* args) {

    // not using Instance() each time to increase performance (no locks)
    const SyncTimer& syncTimer = SyncTimer::Instance();

    const int emptyBufferTimeout = 3000; // 3 sec
    SyncTimerState tState = ST_WORK;
    bool diagnostic = false; // debug messages

    while ( (tState = syncTimer.State())==ST_WORK ) {

        isSignalled(g_hEmptyMutEvent, "Consumer: ", "g_hEmptyEvent", diagnostic);
        isSignalled(g_hFullMutEvent,  "Consumer: ", "g_hFullEvent", diagnostic);

        DWORD dwResult = ::WaitForSingleObject(g_hMutex, INFINITE);
        if (dwResult != WAIT_OBJECT_0)
            return ERR_SYNC; // error
            
        if (g_msgs.empty()) {    // nothing to consume, need synchronisation
            Print(EMPTY_BUFFER); // protected by lock to synchonise output
            ::ReleaseMutex(g_hMutex);

            ::ResetEvent(g_hFullMutEvent);
            DWORD dwResult = ::WaitForSingleObject(g_hFullMutEvent, emptyBufferTimeout);
            if (dwResult == WAIT_FAILED)
                return ERR_SYNC;          // error, exiting
            if (dwResult == WAIT_TIMEOUT) // check global timer
                continue;

            Print(CONSUMER_WAKE_UP);

            dwResult = ::WaitForSingleObject(g_hMutex, INFINITE);
            if (dwResult != WAIT_OBJECT_0)
                return ERR_SYNC; // error
        }

        int cur_msg = 0;
        try {
            cur_msg = g_msgs.front();
            g_msgs.pop();

        } catch(std::exception& ex) {
            Print(ex.what());
            ::ReleaseMutex(g_hMutex);
            return ERR_STD;
        } catch(...) {  
            Print("Unknown error");
            ::ReleaseMutex(g_hMutex);
            return ERR_UNKNOWN;
        }

        Print("received:", cur_msg);
        ::ReleaseMutex(g_hMutex);
        ::SetEvent(g_hEmptyMutEvent);
        Consume(cur_msg);

    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
    return RET_OK;
}

} // namespace MT
