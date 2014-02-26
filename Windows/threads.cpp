#include "stdafx.h"
#include "threads.h"

// Alexey Voytenko, alexvgml@gmail.com

// mutlithreaded access to shared read/write memory: producer-consumer problem
// http://en.wikipedia.org/wiki/Producer-consumer_problem

Queue<int> g_msgs(8); // queue with limitied size (10 items here)
                      // to model full buffer situation

// thread functions - pairs for different synchronisation primitives

// only locking shared memory with Critical Sections
unsigned __stdcall Producer_CS(void* args);
unsigned __stdcall Consumer_CS(void* args);

// using Events for synchronisation
unsigned __stdcall Producer_CS_Event(void*);
unsigned __stdcall Consumer_CS_Event(void*);

// using Mutex for synchronisation
unsigned __stdcall Producer_Mutex(void*);
unsigned __stdcall Consumer_Mutex(void*);

// sample usage of Semaphore
unsigned __stdcall SemThreadFunc(void* args);

// synchronisation objects - must be visible to all threads where they will be used
// see: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686908(v=vs.85).aspx

CRITICAL_SECTION g_cs;
CRITICAL_SECTION g_sem_cs;
CRITICAL_SECTION g_timer_cs;
CRITICAL_SECTION g_cout_cs; 

HANDLE g_hEmptyEvent = NULL;
HANDLE g_hFullEvent  = NULL;

HANDLE g_hMutex = NULL;
HANDLE g_hSemaphore = NULL;

int runProducerConsumer(SyncType syncType);
int runSemaphore();

int runThreads(SyncType syncType) 
{
    // A sample program demonstrating using of basic Windows synchronisation objects

    // now we are in the primary thread of the application
    // synchronise stopping of all threads after specified timeout 
    // using single common global WaitableTimer object


    if ( !::InitializeCriticalSectionAndSpinCount(&g_cout_cs, 0x00000400))
        return ERR_API;

    if ( !::InitializeCriticalSectionAndSpinCount(&g_timer_cs, 0x00000400))
        return ERR_API;

    SyncTimer& syncTimer = SyncTimer::Instance();
    if (!syncTimer.isHandle())
        return ERR_API;

    LARGE_INTEGER timeout; // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686289(v=vs.85).aspx
    timeout.QuadPart = -160000000LL; // if negative, the absolute value is interpreted as relative
                                     // to the current time on the clock, in 100ns interval

    if (!syncTimer.SetTimer(timeout))
        return ERR_API;

    int ret = 0;
    switch (syncType) {
        case SEMAPHORE:
            ret = runSemaphore();
            break;
        case CS:
        case CS_EVENT:
        case MUTEX:
        default:
            ret = runProducerConsumer(syncType);
            break;
    }

    ::DeleteCriticalSection(&g_timer_cs);
    ::DeleteCriticalSection(&g_cout_cs);
    return ret;
}

int runProducerConsumer(SyncType syncType)
{
    // init sync objects

    if (syncType == CS || syncType == CS_EVENT)
        if ( !::InitializeCriticalSectionAndSpinCount(&g_cs, 0x00000400) )
            return ERR_API;

    if (syncType == CS_EVENT || syncType == MUTEX) {
        // see: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686915(v=vs.85).aspx
        g_hEmptyEvent = ::CreateEvent( 
            NULL,               // default security attributes
            FALSE,              // auto-reset (TRUE - manual-reset) event
            FALSE,              // initial state is nonsignaled
            TEXT("EmptyEvent")  // object name
            );

        if (g_hEmptyEvent == NULL)
            return ERR_API;

        g_hFullEvent = ::CreateEvent( 
            NULL,               // default security attributes
            FALSE,              // auto-reset event
            FALSE,              // initial state is nonsignaled
            TEXT("FullEvent")   // object name - must be different in all events, elsewhere they 
            );                  // cannot be distinguished by the system

        if (g_hFullEvent == NULL) {
            ::CloseHandle(g_hEmptyEvent);
            return ERR_API;
        }
    }

    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686927(v=vs.85).aspx
    if (syncType == MUTEX) {
        g_hMutex = ::CreateMutex( 
            NULL,              // default security attributes
            FALSE,             // initially not owned
            NULL);             // unnamed mutex

        if (g_hMutex == NULL) {
            ::CloseHandle(g_hEmptyEvent);
            ::CloseHandle(g_hFullEvent);
            return ERR_API;
        }
    }

    // chose thread functions according to the sync type objects
    unsigned (__stdcall *Producer)(void*) = 0;
    unsigned (__stdcall *Consumer)(void*) = 0;

    switch(syncType) {
        case CS:
            Producer = Producer_CS;
            Consumer = Consumer_CS;
            break;
        case MUTEX:
            Producer = Producer_Mutex;
            Consumer = Consumer_Mutex;
            break;
        case CS_EVENT:
        default:
            Producer = Producer_CS_Event;
            Consumer = Consumer_CS_Event;
            break;
    }

    const unsigned totalThreads = 2; // producer and consumer

    HANDLE   threadHandles[totalThreads] = { 0 };
    unsigned threadIDs[totalThreads]     = { 0 };

    // in case a thread is not created - do not create remaining threads,
    // wait for created, cleen up and return
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
        for (int i=0; i<totalThreads; i++) {
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

    // clean up resourses
    if (syncType == CS || syncType == CS_EVENT)
        ::DeleteCriticalSection(&g_cs);

    if (syncType == CS_EVENT || syncType == MUTEX) {
        ::CloseHandle(g_hEmptyEvent);
        ::CloseHandle(g_hFullEvent);
    }

    if (syncType == MUTEX)
        ::CloseHandle(g_hMutex);

    if (createdThreads != totalThreads)
        return ERR_API;

    if (dwRet == WAIT_FAILED) // WaitForMultipleObject return code
        return ERR_API;
    if (syncType == MUTEX)
        if (dwRet == WAIT_ABANDONED)
            return ERR_SYNC;

    if (!allThreadsOK)
         ERR_SYNC;

    return 0;
}


const LONG g_SEM_INIT_COUNT = 2;     // initial semaphore object counter
int g_semCounter = g_SEM_INIT_COUNT; // our own debug semaphore counter tracking
volatile LONG g_semThreadNum = 0;    // short number for threads to increase readability

int runSemaphore() {

    // semaphore
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms685129(v=vs.85).aspx
    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686946(v=vs.85).aspx

    // Maintains a counter which is decreasing whe Wait function succeeds and increasing
    // when semaphore is released. The state is signaled when counter > 0.

    // For example, if the counter is set to 2, only two threads can work simultaneously:
    // both had succeed wait function and decremented the counter so it became 0 and 
    // the third thread wait function will not succeed - semaphore state is NOT signalled.

    const unsigned totalThreads = 3;  // 3 for semaphore test
    g_semThreadNum = 0;  // reset thread number counter (for next calls of runSemaphore)

    HANDLE threadHandles[totalThreads] = { 0 };
    unsigned threadIDs[totalThreads] = { 0 };

    // set a number of threads working simultaneuously to MAX_SEM_COUNT
    const LONG MAX_SEM_COUNT  = 3;
    
    if ( !::InitializeCriticalSectionAndSpinCount(&g_sem_cs, 0x00000400) )
        return ERR_API;

    g_hSemaphore = ::CreateSemaphore(
        NULL,             // default security attributes
        g_SEM_INIT_COUNT, // initial count - no more than 2 objects allowed
        MAX_SEM_COUNT,    // maximum count
        _T("SampleSemaphore")); // NULL - unnamed semaphore

    if (g_hSemaphore == NULL)
        return ERR_API;

    // in case a thread is not created - do not create remaining threads, 
    // wait for created, cleen up and return
    int createdThreads = 0;
    for (int i=0; i<totalThreads; i++, createdThreads++) { // try to create > MAX_SEM_COUNT threads
        threadHandles[i] = (HANDLE)  _beginthreadex( NULL, 0, &SemThreadFunc, 0, 0, &threadIDs[i] );

        if (threadHandles[i] == 0)
            break;
    }

    DWORD dwRet = 0;
    bool allThreadsOK = false;

    if (createdThreads > 0) {
        dwRet = ::WaitForMultipleObjects(
            createdThreads,
            threadHandles,
            TRUE, // wait all threads
            INFINITE
            );

        // check status and close thread handles
        allThreadsOK = true;
        for (int i=0; i<totalThreads; i++) {
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

    // clean up resourses

    ::CloseHandle(g_hSemaphore);
    ::DeleteCriticalSection(&g_sem_cs);

    if (createdThreads != totalThreads)
        return ERR_API;

    if (dwRet == WAIT_FAILED)
        return ERR_API;

    if (!allThreadsOK)
        ERR_SYNC;

    return 0;
}
