

CS 61 Problem Set 4
===================

**Fill out both this file and `AUTHORS.md` before submitting.** We grade
anonymously, so put all personally identifying information in `AUTHORS.md`.

How Do I run this?
----------------------
enter this directory and use
```
make run
```
This should present you with a GUI

To create a fork:
While still open in the GUI, click `f`. This should fork the process

To create infinite forks:
to create infinite forks for virtual memory click `e`


Grading notes (if any)
----------------------
Finishing step 7 has been the most stressful yet one of the best learning experiences I have had. It taught me so much more about debugging and making your own tests to ensure you fail at critical moments and backtrack any and all misgivings. This has been an amazing class. 

Extra credit attempted (if any)
-------------------------------
I wrote a bunch of asserts and dump routines while for the both processes and pagetables that should error out when owners are not the same. I have removed most of the asserts and took my dump routines out of my functions after I checked they all worked since they slow run time.

Should you be interested you should be able to throw the dump routines in an if like so
```
if(example_dump_routine() == -1 )
    assert(0)
```
which should fail out on any of those occasions it should fail and give you logs about which pid(processID) and owner of process/pagetable/etc is and gave you the location it is overwritting it. 

Sadly I was not able to implement another extra credit "WRITE", but I know I would need to implement it as a case in my SEGFAULT so should I try to write I should allow it given it pass certain parameters else continue with my SEGFAULT