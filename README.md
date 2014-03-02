### Multithreading

Sample code demonstrating multithreading programming issues.

#### Windows

Application demonstrates usage of basic Windows synchronisation objects
by example of  
solving [producer-consumer problem](<http://en.wikipedia.org/wiki/Producer-consumer_problem>).

    At start user chooses the type of synchronisation objects (main.cpp).
    Appropriate object of ThreadRunner class hierarchy (threadrunner.h)
    provides thread management (threadrunner.cpp) and thread function
    (consumer.cpp, producer.cpp).
    
    Threads are running until they all will finish or timeout occurs.
    Common SyncTimer object (threads.h) signals all threads to stop.
    
    Initial commit showed the work with bare Windows API as it is described in MSDN.
    
Any comments or bug reports are welcome.

Alexey Voytenko  
<alexvgml@gmail.com>
