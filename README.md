# LITS_openSource

LITS is a learned index optimized for string keys.

## Build and Run

To run a simple example:

```shell
$ make example
$ ./example
```

To run simple benchmarks:

```shell
$ make testbench

# <str> can be 'idcards' or 'randstr'

# Case 1: search only test
$ ./testbench <str> 1

# Case 2: insert only test
$ ./testbench <str> 2

# Case 3: scan only test
$ ./testbench <str> 3
```
