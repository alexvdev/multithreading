#include "stdafx.h"
#include "threads.h"
#include "threadrunner.h"

// A sample program demonstrating usage of basic Windows synchronisation objects
// by example of solving producer-consumer problem.
// ( http://en.wikipedia.org/wiki/Producer-consumer_problem )
//
// At start user chooses the type of synchronisation objects (main.cpp).
// Appropriate object of ThreadRunner class hierarchy (threadrunner.h)
// provides thread management (threadrunner.cpp) and thread function
// (consumer.cpp, producer.cpp).

// Threads are running until they all will finish or timeout occurs.
// Common SyncTimer object (threads.h) signals all threads to stop.
//
// Alexey Voytenko, alexvgml@gmail.com

int main(int argc, char* argv[])
{
    // enable memory leaks detection
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF ); 

    srand(static_cast<unsigned int>(time(0))); // init RND generator
    
    int ret    = RET_OK;
    int choice = 0;

    // primary thread of the application
    while (true) {

        cout << "Choose type of synchronisation objects (enter 1-5):" << endl << endl
             << "1. Critical sections (Producer-Consumer)" << endl
             << "2. Critical sections and events (Producer-Consumer)" << endl
             << "3. Mutex (Producer-Consumer)" << endl
             << "4. Semaphore" << endl
             << "5. Exit" << endl;
        
        while ( !(cin >> choice) || !(1 <= choice && choice <= 5) ) {
            if (cin.fail()) { // not an integer
                cin.clear();  // clear failbit

                // ignore all input before <Enter>
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
            cout << "Please input an integer from 1 to 5:" << endl;
        }
        if (choice == 5)
            break;

        // although auto_ptr is deprecated it can be used  here as scoped ptr (not using C++11 yet)
        std::auto_ptr <MT::ThreadRunner> spTR( 
                    MT::ThreadRunnerCreator::Create(static_cast<SyncType>(choice)) );

        ret = spTR->RunThreads();

        if (ret!=RET_OK) {
            if (ret==ERR_SYNC) {
                cout << endl << "Not all threads finished correctly, exiting." << endl;
            } else if (ret == ERR_API) {
                cout << endl << "WinApi error, exiting." << endl;
            } else {
                cout << endl << "Error ocurred with the code " << ret << ", exiting." << endl;
            }
            break;
        }
    }
    return ret;
}
