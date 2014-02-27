#pragma once

#include <queue>

// chose different synchronisation objects
enum SyncType {
    CS    = 1, // only critical sections
    CS_EVENT,  // critical sections with events
    MUTEX,     // mutex
    SEMAPHORE
};

// error return types
const int RET_OK      = 0;
const int ERR_SYNC    = 1; // error with thread syncshronisation
const int ERR_STD     = 2; // std::exception
const int ERR_API     = 3; // API function failed
const int ERR_UNKNOWN = 4; // catched by catch (...)

typedef unsigned (__stdcall THREAD_FUNCTION)(void*);  // function to pass to _beginthreadex

const char TIMEOUT[] = "Exiting thread, timeout: ";

namespace MT {

class HandleWrapper { // using RAAI idiom
public:
    HandleWrapper(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {
    }
    ~HandleWrapper() {
        if (isValid())               // with invalid handle values ::CloseHandle returns FALSE 
            ::CloseHandle(m_handle); // with ERROR_INVALIDE_HANDLE GetLastError() code
    }

    void SetHandle(HANDLE handle) { // needed for global objects. Although it is no acquisition,
        if (isValid())              // RAII idea is also releasing resources in the destructor
            ::CloseHandle(m_handle);
        m_handle = handle;
    }

    operator HANDLE() const {
        return m_handle;
    }

    bool isValid() const {
        return ( m_handle != INVALID_HANDLE_VALUE && m_handle != NULL );
    }

private:
    // disable copy constructor and assignment operator
    HandleWrapper(const HandleWrapper&);
    HandleWrapper& operator=(const HandleWrapper&);

    HANDLE m_handle;
};

class CriticalSection {
public:
    CriticalSection() {
        m_isValid = ( ::InitializeCriticalSectionAndSpinCount(&m_cs, 0x00000400) != 0 );
    }
    ~CriticalSection() {
        if (m_isValid)
            ::DeleteCriticalSection(&m_cs);
    }

    bool isValid() {
        return m_isValid;
    }

    bool Enter() {
        if (m_isValid)
            ::EnterCriticalSection(&m_cs);
        return m_isValid;
    }

    bool Leave() {
        if (m_isValid)
            ::LeaveCriticalSection(&m_cs);
        return m_isValid;
    }

private:
    CriticalSection(const CriticalSection&);
    CriticalSection& operator=(const CriticalSection&);

    CRITICAL_SECTION m_cs;
    bool m_isValid;
};


class Lock {

public:
   Lock(CriticalSection& cs) : m_cs(cs) { // RAAI idiom
       m_cs.Enter();
    }
    ~Lock() {
        m_cs.Leave();
    }
private:
    Lock(const Lock&);
    Lock& operator=(const Lock&);

    CriticalSection& m_cs;
};

// controlling the upper size of the queue
template <class T> class Queue : public std::queue<T> {
public:
    Queue(int _bs) : m_buf_size(_bs) {}
    bool isFull() const {        // exception safe
        return m_buf_size == size();
    }

    // crash-safe version
    const T& front() {
        if (empty())
            throw std::underflow_error("Queue buffer is empty");
        return std::queue<T>::front();
    }

    // for limiting Producer
    void push (const T& t) {
        if (isFull())    // disaster
            throw std::overflow_error("Queue buffer is full");
        std::queue<T>::push(t);
    }

private:
    const int m_buf_size;
};

enum SyncTimerState { ST_WORK, ST_STOP, ST_ERR };

// using Singleton GOF pattern
class SyncTimer {
public:
    // Thread-safe Singleton implementation.
    // see (Meyers, Alexandresku) http://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf
    // and (Raymond Chen) http://blogs.msdn.com/b/oldnewthing/archive/2004/03/08/85901.aspx
    // about initialisation of static variables in multithreaded environment

    // Alternative implementation with pointers and lazy initialisation
    // as described in GOF book involves that the destructor will not be called
    static SyncTimer& Instance() {
        Lock lock(m_cs);
        static SyncTimer syncTimer;
        return syncTimer;
    }

    SyncTimer::~SyncTimer() {
    }

    bool isValid() const {
        return m_hTimer.isValid();
    }

    bool SetTimer(const LARGE_INTEGER& t) {
        Lock lock(m_cs);
        m_timeout = t;
        convertTimeOutToSeconds();
        BOOL ret = ::SetWaitableTimer(m_hTimer, &m_timeout, 0, NULL, NULL, 0);
        return ret != 0;
    }

    unsigned int GetTimeoutInsSec() const { 
        return m_timeoutSec;
    }

    SyncTimerState State() const {
        switch (::WaitForSingleObject(m_hTimer, 0)) {
            case WAIT_OBJECT_0: // signalled
                return ST_STOP;
            case WAIT_TIMEOUT:
                return ST_WORK;
            case WAIT_FAILED:
            default:
                return ST_ERR;
        }
    }

protected:
    SyncTimer() : m_timeoutSec(0),
        m_hTimer ( ::CreateWaitableTimer(
            NULL,    // security attributes 
            TRUE,    // manual reset: will be signalled for all threads 
            _T("SyncTimer")
        ))
    {   
        m_timeout.QuadPart = 0;
    }

private:
    SyncTimer(const SyncTimer&);
    SyncTimer& operator=(const SyncTimer&);

    static CriticalSection m_cs;

    LARGE_INTEGER m_timeout;
    unsigned m_timeoutSec;
    HandleWrapper m_hTimer;

    void convertTimeOutToSeconds() {
        const int intervalsInSec = 10000000; // timeout is set in 100ns intervals (1 ns == 1,000,000,000)
        if (m_timeout.QuadPart == 0) {
            m_timeoutSec = 0;
        } else if (m_timeout.QuadPart < 0) {
            // received relative to current clock time
            m_timeoutSec = static_cast<unsigned int>( ( -m_timeout.QuadPart ) / intervalsInSec );
        } else {
            // received absolute time
            FILETIME fileTime;
            ULARGE_INTEGER current, timeoutInSec;
            ::GetSystemTimeAsFileTime(&fileTime);
            current.LowPart = fileTime.dwLowDateTime;
            current.HighPart= fileTime.dwHighDateTime;

            timeoutInSec.QuadPart = m_timeout.QuadPart - current.QuadPart;
            m_timeoutSec = static_cast<unsigned int>( timeoutInSec.QuadPart / intervalsInSec );
        }
    }
};

} // namespace Multithreading
