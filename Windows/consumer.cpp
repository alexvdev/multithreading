#include "stdafx.h"
#include "threads.h"
#include <queue>

extern Queue<int> g_msgs;

extern CRITICAL_SECTION g_cs;
extern HANDLE g_hEmptyEvent;
extern HANDLE g_hFullEvent;
extern HANDLE g_hMutex;

bool isSignalled(HANDLE h, const std::string& hName, const std::string& who,
                bool diagnostics = false);

const char EMPTY_BUFFER[]     = "Consumer: empty buffer, waiting";
const char CONSUMER_WAKE_UP[] = "Consumer: waking up";

void consume(int msg) {
    wait( rand()%14 * 50 );
}

// Just locking shared data structure with Critical Sections
unsigned __stdcall Consumer_CS(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    const int emptyBufferWait = 1000; // 1 sec
    SyncTimerState tState = ST_WORK;

    while ((tState = syncTimer.State())==ST_WORK) {

        ::EnterCriticalSection(&g_cs);
        if (g_msgs.empty()) { // nothing to produce, need synchronisation
            Print(EMPTY_BUFFER);
            ::LeaveCriticalSection(&g_cs);

            wait(emptyBufferWait);
            continue; // wait until there will be some input in the buffer or timeout occurs
        }

        // we are now in critical section

        int cur_msg=0;
        try {
            cur_msg = g_msgs.front();
            g_msgs.pop();

        } catch(std::exception& ex) { // should catch all exceptions to avoid indefinite locks
            stringstream ss;
            ss << "error: " << ex.what();
            Print(ss.str().c_str());
            ::LeaveCriticalSection(&g_cs);
            return ERR_STD;
        } catch(...) {
            Print("Unknown error ");
            ::LeaveCriticalSection(&g_cs);
            return ERR_UNKNOWN;
        }

        stringstream ss;
        ss << "received:" <<  cur_msg;
        Print(ss.str().c_str());
        ::LeaveCriticalSection(&g_cs);

        consume(cur_msg);

    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
    return 0;
}

// Using Critical Sections and Events for synchronisation
// using RAAI idiom for aquiring locks
unsigned __stdcall Consumer_CS_Event(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    const int emptyBufferTimeout = 3000; // 3 sec
    SyncTimerState tState = ST_WORK;
    bool diagnostic=false; // debug messages

    while ((tState = syncTimer.State())==ST_WORK) {
        
        isSignalled(g_hEmptyEvent, "Consumer: ", "g_hEmptyEvent", diagnostic);
        isSignalled(g_hFullEvent,  "Consumer: ", "g_hFullEvent", diagnostic);

        bool isEmpty = false;
        {
            Lock lock(g_cs); // any access to writable shared memory should be protected by lock
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
            Lock lock(g_cs);
            try {
                cur_msg = g_msgs.front();
                g_msgs.pop();

            } catch(std::exception& ex) {
                stringstream ss;
                ss << "error: " << ex.what();
                Print(ss.str().c_str());
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

        consume(cur_msg);

    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
    return 0;
}

// Using mutex for synchronisation
unsigned __stdcall Consumer_Mutex(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    const int emptyBufferTimeout = 3000; // 3 sec
    SyncTimerState tState = ST_WORK;
    bool diagnostic = false; // debug messages

    while ((tState = syncTimer.State())==ST_WORK) {

        isSignalled(g_hEmptyEvent, "Consumer: ", "g_hEmptyEvent", diagnostic);
        isSignalled(g_hFullEvent,  "Consumer: ", "g_hFullEvent", diagnostic);

        DWORD dwResult = ::WaitForSingleObject(g_hMutex, INFINITE);
        if (dwResult != WAIT_OBJECT_0)
            return ERR_SYNC; // error
            
        if (g_msgs.empty()) {    // nothing to consume, need synchronisation
            Print(EMPTY_BUFFER); // protected by lock to synchonise output
            ::ReleaseMutex(g_hMutex);

            ::ResetEvent(g_hFullEvent);
            DWORD dwResult = ::WaitForSingleObject(g_hFullEvent, emptyBufferTimeout);
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
            stringstream ss;
            ss << "error: " << ex.what();
            Print(ss.str().c_str());

            ::ReleaseMutex(g_hMutex);
            return ERR_STD;
        } catch(...) {  
            Print("Unknown error");

            ::ReleaseMutex(g_hMutex);
            return ERR_UNKNOWN;
        }

        stringstream ss;
        ss << "received:" << cur_msg;
        Print(ss.str().c_str());

        ::ReleaseMutex(g_hMutex);
        ::SetEvent(g_hEmptyEvent);

        consume(cur_msg);

    } // while

    if (tState == ST_ERR)
        return ERR_SYNC;

    PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
    return 0;
}
