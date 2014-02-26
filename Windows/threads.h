#include <queue>

// chose different synchronisation objects
enum SyncType {
    CS    = 1, // only critical sections
    CS_EVENT,  // critical sections with events
    MUTEX,     // mutex
    SEMAPHORE
};

// error return types
const int ERR_SYNC    = 1; // error with thread syncshronisation
const int ERR_STD     = 2; // std::exception
const int ERR_API     = 3; // API function failed
const int ERR_UNKNOWN = 4; // catched by catch (...)

const char TIMEOUT[] = "Exiting thread, timeout: ";
void PutThreadFinishMsg(const char* msg, unsigned int timeout = 0);

int runThreads(SyncType syncType=CS_EVENT);

void produce(int ms = rand()%10 * 50);
void wait(int ms);
void Print(const char* msg);

class Lock {
    CRITICAL_SECTION& cs;
public:
    Lock(CRITICAL_SECTION& _cs) : cs(_cs) { // RAAI idiom
        ::EnterCriticalSection(&cs);
    }
    ~Lock() {
        ::LeaveCriticalSection(&cs);
    }
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
extern CRITICAL_SECTION g_timer_cs;

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
        Lock lock(g_timer_cs);
        static SyncTimer syncTimer;
        return syncTimer;
    }


    SyncTimer::~SyncTimer() {
        if (m_handle != NULL)        // ::CloseHandle(NULL) will fail (return FALSE)
            ::CloseHandle(m_handle); // will error code ERROR_INVALIDE_HANDLE (6), returned by GetLastError()
    }

    bool isHandle() const {
        return m_handle != NULL;
    }

    bool SetTimer(const LARGE_INTEGER& t) {
        Lock lock(g_timer_cs);
        m_timeout = t;
        convertTimeOutToSeconds();
        BOOL ret = ::SetWaitableTimer(m_handle, &m_timeout, 0, NULL, NULL, 0);
        return ret != 0;
    }

    unsigned int GetTimeoutInsSec() const { 
        return m_timeoutSec;
    }

    SyncTimerState State() const {
        switch (::WaitForSingleObject(m_handle, 0)) {
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
    SyncTimer() {
        m_timeout.QuadPart = 0;
        m_handle = ::CreateWaitableTimer(
            NULL,    // security attributes 
            TRUE,    // manual reset: will be signalled for all threads 
            _T("SyncTimer")
        );
    }

private:
    HANDLE m_handle;
    LARGE_INTEGER m_timeout;
    unsigned m_timeoutSec;

    SyncTimer(const SyncTimer&);            // disable copy constructor
    SyncTimer& operator=(const SyncTimer&); // and assignment operator

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
