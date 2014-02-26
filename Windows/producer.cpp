#include "stdafx.h"
#include "threads.h"
#include <queue>

extern Queue<int> g_msgs;

extern CRITICAL_SECTION g_cs;
extern CRITICAL_SECTION g_cout_cs; 
extern HANDLE g_hEmptyEvent;
extern HANDLE g_hFullEvent;
extern HANDLE g_hMutex;

const char FULL_BUFFER[]      = "Producer: full buffer, waiting";
const char PRODUCER_WAKE_UP[] = "Producer: waking up";
const char TASKS_FINISHED[]   = "Producer: tasks finished, exiting.";

const int MAX_TASKS = 30; // number of tasks to produce, to demonstrate empty buffer condition

// helpers
void wait(int ms) {
    ::Sleep(ms);
}

void produce(int ms) {
    wait(ms);
}

void Print(const char* msg) {
    Lock lock(g_cout_cs);
    cout << msg << endl;
}

// diagnostics
bool isSignalled(HANDLE h, const std::string& hName, const std::string& who, 
                 bool diagnostics = false) {

    DWORD dwResult = ::WaitForSingleObject(h, 0);
    bool res = (dwResult != WAIT_TIMEOUT);
    if (diagnostics) {
        stringstream ss;
        ss << who << " " << hName <<  " is " << ( res ? "" : "NOT" ) << " signalled";
        Print(ss.str().c_str());
    }

    return res;
}

void PutThreadFinishMsg(const char* msg, unsigned int timeout) {
    Lock lock(g_cout_cs);
    cout << endl << msg;
    if (timeout != 0)
        cout << timeout << " sec.";
    cout << " Thread Id: " << ::GetCurrentThreadId() << endl << endl;
}

// Just locking shared data structure with Critical Sections
unsigned __stdcall Producer_CS(void* args) {

    const SyncTimer& syncTimer = SyncTimer::Instance();
    SyncTimerState tState = ST_WORK;

    // we will finish either when produce MAX_TASKS or global timeout occurs
    for (int nTask = 1; nTask <= MAX_TASKS; nTask++) {

        produce(); // imitate work, exception safe

        const int fullBufferWait = 300; // 0.3 sec
        bool isFull = true;
        while ( (tState = syncTimer.State())==ST_WORK && isFull) { // check timeout waiting for free buffer
            // all access to shared writable memory should be protected
            ::EnterCriticalSection(&g_cs);
            isFull = g_msgs.isFull();
            if (isFull) {
                // buffer is full,
                Print(FULL_BUFFER);
                ::LeaveCriticalSection(&g_cs);
                wait(fullBufferWait); // wait some period for consumer
            }
        } // while
        
        // we are now in critical section

        if (tState != ST_WORK) {
            ::LeaveCriticalSection(&g_cs);

            if (tState == ST_ERR)
                return ERR_SYNC;
            PutThreadFinishMsg( TIMEOUT, syncTimer.GetTimeoutInsSec() );
            return 0;
        }

        try {
            
            g_msgs.push(nTask);

        } catch(std::exception& ex) { // should catch all exceptions to leave CS 
            stringstream ss;          // and avoid indefinite lock
            ss << "error: " << ex.what();
            Print(ss.str().c_str());
            ::LeaveCriticalSection(&g_cs);
            return ERR_STD;
        } catch(...) {  
            Print("Unknown error");
            ::LeaveCriticalSection(&g_cs);
            return ERR_UNKNOWN;
        }
        stringstream ss;
        ss << "sent: " <<  nTask;
        Print(ss.str().c_str());
        ::LeaveCriticalSection(&g_cs);

    } // for

    PutThreadFinishMsg( TASKS_FINISHED );
    return 0;
}

// Using Critical Sections and Events for synchronisation
// using RAAI idiom for aquiring locks
unsigned __stdcall Producer_CS_Event(void* args) {

    SyncTimer& syncTimer = SyncTimer::Instance();
    SyncTimerState tState = ST_WORK;
    bool diagnostic = false; // debug messages

    // we will finish either when produce MAX_TASKS or global timeout occurs
    for (int nTask = 1; nTask <= MAX_TASKS; nTask++) {

        produce(); // imitate work, exception safe

        isSignalled(g_hEmptyEvent, "Producer: ", "g_hEmptyEvent", diagnostic);
        isSignalled(g_hFullEvent,  "Producer: ", "g_hFullEvent",  diagnostic);

        const int fullBufferTimeout = 5000; // 5 sec
        bool isFull = true;
        while ( (tState = syncTimer.State())==ST_WORK && isFull) { // check timeout waiting for free buffer
            {
                Lock lock(g_cs);
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
            return 0;
        }

        {
            Lock lock(g_cs);
            try {
                g_msgs.push(nTask);

            } catch(std::exception& ex) { // now lock would be released in Lock destructor
                stringstream ss;          // if the exception would not have been catched
                ss << "error: " << ex.what();
                Print(ss.str().c_str());
                return ERR_STD;
            } catch(...) {
                Print("Unknown error");
                return ERR_UNKNOWN;
            }

            stringstream ss;
            ss << "sent: " <<  nTask;
            Print(ss.str().c_str());
            ::SetEvent(g_hFullEvent);
        }

    } // for

    PutThreadFinishMsg( TASKS_FINISHED );
    return 0;
}

// Using mutex for synchronisation
unsigned __stdcall Producer_Mutex(void* args) {

    SyncTimer& syncTimer = SyncTimer::Instance();
    SyncTimerState tState = ST_WORK;
    bool diagnostic = false; // debug messages

    // we will finish either when produce MAX_TASKS or global timeout occurs
    for (int nTask = 1; nTask <= MAX_TASKS; nTask++) {

        produce(); // imitate work, exception safe
        const int fullBufferTimeout = 5000; // 5 sec

        bool isFull = true;
        while ( (tState = syncTimer.State())==ST_WORK && isFull) { // check timeout waiting for free buffer

            isSignalled(g_hEmptyEvent, "Producer: ", "g_hEmptyEvent", diagnostic);
            isSignalled(g_hFullEvent,  "Producer: ", "g_hFullEvent",  diagnostic);

            produce(); // imitate work, exception safe

            DWORD dwResult = ::WaitForSingleObject(g_hMutex, INFINITE);
            if (dwResult != WAIT_OBJECT_0)
                return ERR_SYNC; // error, exiting

            // now we own the mutex

            isFull = g_msgs.isFull();
            if (isFull) {  // buffer is full, wait event from consumer

                Print(FULL_BUFFER);
                ::ReleaseMutex(g_hMutex);

                ::ResetEvent(g_hEmptyEvent);
                DWORD dwResult = ::WaitForSingleObject(g_hEmptyEvent, fullBufferTimeout);
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
            return 0;
        }

        try {
            g_msgs.push(nTask);
        
        } catch(std::exception& ex) { // should catch all exception in the thread to avoid indefinite locks
            stringstream ss;          // by not releasing mutex
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
        ss << "sent: " <<  nTask;
        Print(ss.str().c_str());

        ::ReleaseMutex(g_hMutex);
        ::SetEvent(g_hFullEvent);

    } // for

    PutThreadFinishMsg( TASKS_FINISHED );
    return 0;
}
