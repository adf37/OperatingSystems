The following project runs four different algorithms on traces of memory references. The goal is to test
and see which of the various page replacement algorithms is most effective.

Opt – Simulate what the optimal page replacement algorithm would choose if it had perfect knowledge
Clock – Do the circular queue improvement to the second chance algorithm
Aging – Implement the aging algorithm that approximates LRU with an 8-bit counter
Working Set Clock – A time periodic refresh as in aging to reset valid pages' R bits back to zero

The following arguments are accepted:
-n <numframes> -a <opt|clock|aging|work> [-r <refresh>] [-t <tau>] <tracefile>

When the trace is over, a printed out summary of statistics is printed in the following format:

Clock
Number of frames:       8
Total memory accesses:  1000000
Total page faults:      181856
Total writes to disk:   29401

