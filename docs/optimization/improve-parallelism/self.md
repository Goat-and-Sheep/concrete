## Improve parallelism

This guide teaches the different options for parallelism in Concrete and how to utilize them to improve the execution time of Concrete circuits.

Modern CPUs have multiple cores to perform computation and utilizing multiple cores is a great way to boost performance.

There are two kinds of parallelism in Concrete:
- Loop parallelism to make tensor operations parallel, achieved by using [OpenMP](https://www.openmp.org/)
- Dataflow parallelism to make independent operations parallel, achieved by using [HPX](https://hpx.stellar-group.org/)

Loop parallelism is enabled by default, as it's supported on all platforms. Dataflow parallelism however is only supported on Linux, hence not enabled by default.