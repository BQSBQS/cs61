CS 61 Problem Set 6
===================

**Fill out both this file and `AUTHORS.md` before submitting.** We grade
anonymously, so put all personally identifying information in `AUTHORS.md`.

How do I run this?
----------------------
while in directory run
```
./pong61
```

Race conditions
---------------
Write a SHORT paragraph here explaining your strategy for avoiding
race conditions. No more than 400 words please.

As suggested for phase 3 I created a connection table as a linked list to check for available connections in given space. In this verify function that creates the linked list, it determines whether the connection has been made successfully or not and sleeps in between failed connections. This is all wrapped in a while loop where when you successfully make a connection you get flagged to break out. On occasion of a Red Stop Sign or critical locations, I wrap it in a mutex_lock/unlock to prevent any overlapping WRITEs. 




Grading notes (if any)
----------------------



Extra credit attempted (if any)
-------------------------------
