#include "stdafx.h"
#include "threads.h"

// see threads.cpp for problem definition

bool isChoiceCorrect(int choice) {
    return 1 <= choice && choice <= 5;
}

int main(int argc, char* argv[])
{
    // enable memory leaks detection
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF ); 

    srand(static_cast<unsigned int>(time(0))); // init RND generator
    
    int ret    = 0;
    int choice = 0;

    while (true) {

        cout << "Choose type of synchronisation objects (enter 1-5):" << endl << endl
             << "1. Critical sections (Procucer-Consumer)" << endl
             << "2. Critical sections and events (Procucer-Consumer)" << endl
             << "3. Mutex (Procucer-Consumer)" << endl
             << "4. Semaphore" << endl
             << "5. Exit" << endl;
        
        while ( (!(cin >> choice) || !isChoiceCorrect(choice)) ) {
            if (cin.fail()) { // not an integer
                cin.clear();  // clear failbit

                // ignore all input before <Enter>
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
            cout << "Please input an integer from 1 to 5:" << endl;
        }
        if (choice == 5)
            break;

        ret = runThreads(static_cast<SyncType>(choice));
        if (ret!=0) {
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
