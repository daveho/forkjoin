This is a fork/join framework for pthreads written in C++.
It is free software available under the MIT license.
(See fj.h and fj.cpp.)

It is inspired by Doug Lea's fork/join framework for Java:

  http://gee.cs.oswego.edu/dl/concurrency-interest/index.html

  http://gee.cs.oswego.edu/dl/papers/fj.pdf

I mention Doug's work only to make it clear where the idea
for this code came from, not to suggest in any way that this
code is in the same league as his implementation for Java.

Please understand the following limitations:

  (1) This was written over the course of two afternoons,
      purely to see how easy it would be to get something
      working.

  (2) ONLY TRIVIAL TESTING HAS BEEN DONE.  There are probably
      horrible, horrible bugs.  Do not use for anything
      important!  (I.e., programs where getting a correct
      result is a high priority.)

  (3) NO OPTIMIZATION HAS BEEN DONE.  Due to the overhead of
      locking, it is unsuitable for very fine-grained task
      decompositions.  I think it might be useful for
      fairly coarse-grained decompositions, but (as mentioned
      in point (2)) I have not done any real testing.

  (4) I have only tried compiling under Linux (specifically,
      Kubuntu 8.04 using gcc 4.2.4).

An example program is provided (fib.cpp).

Here is some evidence that even with the locking, useful
parallelism can be obtained:

	[daveho@nobby]$ time ./fib 34 1
	fib(34) is 9227465
	
	real    0m19.548s
	user    0m19.161s
	sys     0m0.076s
	[daveho@nobby]$ time ./fib 34 2
	fib(34) is 9227465
	
	real    0m14.247s
	user    0m21.333s
	sys     0m0.052s

This is on a very low-end Pentium Dual Core (E2140).

Please drop me a line if you find this code useful, or
want to report a bug, send me a patch, send me a large amount of money,
etc.:

  David Hovemeyer <david.hovemeyer@gmail.com>
