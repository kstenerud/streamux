Streamux
--------

A minimalist, asynchronous, multiplexing, request-response protocol.

Streamux is designed as a low level, point-to-point, bidirectional protocol for you to build a messaging layer on top of. It handles the nitty gritty things like initialization, multiplexing, asynchronous operation, and packetization of your messages.

The only additional components required are:

* A reliable communication transport (TCP, pipes, RS-232, etc)
* A message encoding format & marshaling scheme (for example [CBE](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md))
* Endpoints to receive the messages.



Features
--------

* Minimal Overhead (1 to 4 bytes per message chunk, depending on configuration)
* Multiplexing (multiple data streams can be sent across a single channel)
* Asynchronous (client is informed asynchronously upon completion or error)
* Interruptible (requests may be canceled)
* Floating roles (both peers can operate as client and server at the same time)
* Quick init mode for faster initialization



Specification
--------------

* [Streamux Specification](streamux-specification.md)



Implementations
---------------

* [Go implementation](https://github.com/kstenerud/go-streamux)



License
-------

Copyright 2019 Karl Stenerud

Specification released under Creative Commons Attribution 4.0 International Public License https://creativecommons.org/licenses/by/4.0/

Reference implementation released under MIT License https://opensource.org/licenses/MIT
