Reference Implementation for Streamux
=====================================

A C implementation to demonstrate Streamux.


Requirements
------------

  * Meson 0.49 or newer
  * Ninja 1.8.2 or newer
  * A C compiler
  * A C++ compiler (for the unit tests)



Dependencies
------------

 * stdbool.h: For bool type
 * stdint.h: Fot int types



Building
--------

    meson build
    ninja -C build



Running Tests
-------------

    ninja -C build test

For the full report:

    ./build/run_tests



Installing
----------

    ninja -C build install



Usage
-----

