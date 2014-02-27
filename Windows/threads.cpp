#include "stdafx.h"
#include "threads.h"
#include "threadrunner.h"

MT::Queue<int> g_msgs(8); // queue with limitied size (8 items here) to model full buffer

// synchronisation objects - must be visible to all threads where they will be used
// see: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686908(v=vs.85).aspx

MT::HandleWrapper   g_hEmptyEvent, g_hFullEvent, g_hEmptyMutEvent, g_hFullMutEvent,
                    g_hMutex, g_hSemaphore;

namespace MT {

CriticalSection SyncTimer::m_cs;
CriticalSection ThreadRunner::m_cout_cs;

int ProducerConsumerEventRunner::InitSyncObjects() const {

    // see: http://msdn.microsoft.com/en-us/library/windows/desktop/ms686915(v=vs.85).aspx
    if (!g_hEmptyEvent.isValid())
        g_hEmptyEvent.SetHandle( ::CreateEvent(
            NULL,               // default security attributes
            FALSE,              // auto-reset (TRUE - manual-reset) event
            FALSE,              // initial state is nonsignaled
            TEXT("EmptyEvent")) // object name
        );

    if (!g_hEmptyEvent.isValid())
        return ERR_API;

    if (!g_hFullEvent.isValid())
        g_hFullEvent.SetHandle( ::CreateEvent(
            NULL,              // default security attributes
            FALSE,             // auto-reset event
            FALSE,             // initial state is nonsignaled
            TEXT("FullEvent")) // object name - must be different in all events, elsewhere they 
         );                    // cannot be distinguished by the system

    if (!g_hFullEvent.isValid())
        return ERR_API;
    return RET_OK;
}

int ProducerConsumerMutexRunner::InitSyncObjects() const {

    if (!g_hEmptyMutEvent.isValid())
        g_hEmptyMutEvent.SetHandle( ::CreateEvent(NULL,
                FALSE, FALSE, TEXT("EmptyMutEvent")) );

    if (!g_hEmptyMutEvent.isValid())
        return ERR_API;

    if (!g_hFullMutEvent.isValid())
        g_hFullMutEvent.SetHandle( ::CreateEvent(NULL,
                FALSE, FALSE, TEXT("FullMutEvent")) );

    if (!g_hFullMutEvent.isValid())
        return ERR_API;

    // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686927(v=vs.85).aspx
    if (!g_hMutex.isValid())
        g_hMutex.SetHandle( ::CreateMutex( 
            NULL,     // default security attributes
            FALSE,    // initially not owned
            NULL )    // unnamed mutex
        );

    if (!g_hMutex.isValid())
        return ERR_API;

    return RET_OK;
}

int SemaphoreRunner::InitSyncObjects() const {

    if (!g_hSemaphore.isValid())
        g_hSemaphore.SetHandle( ::CreateSemaphore(
            NULL,             // default security attributes
            m_semInitCount,   // initial count - no more than m_semInitCount of objects allowed
            m_totalThreads,   // maximum count = maximim number of objects working simultaneuously
           _T("SampleSemaphore")) // NULL - unnamed semaphore
        );

    if (!g_hSemaphore.isValid())
        return ERR_API;

    return RET_OK;
}

} // namespace MT