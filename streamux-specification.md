Streamux
========

A minimalist, asynchronous, multiplexing, request-response protocol.

### Features

* Minimal Overhead (1 to 4 bytes per message chunk, depending on configuration)
* Multiplexing (multiple data streams can be sent across a single channel)
* Asynchronous (client is informed asynchronously upon completion or error)
* Interruptible (requests may be canceled)
* Floating roles (both peers can operate as client and server at the same time)
* Quick init mode for faster initialization



Use Case
--------

Streamux is designed as a low level, point-to-point, bidirectional protocol for you to build a messaging layer on top of. It handles the nitty gritty things like initialization, multiplexing, asynchronous operation, and packetization of your messages.

The only additional components required are:

* A reliable communication transport (TCP, pipes, RS-232, etc)
* A message encoding format & marshaling scheme (for example [CBE](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md))
* Endpoints to receive the messages.



General Operation
-----------------

Upon establishing a connection, each peer sends an initialize message, and then begins normal communications. Communication is asynchronous after initialization completes. A typical session might look something like this:

| Peer X           | Peer Y           |
| ---------------- | ---------------- |
| Open Connection  | Open Connection  |
|                  | Initialize       |
| Initialize       |                  |
| Request A        |                  |
| Request B        | Response A       |
|                  | Request C        |
| Response C       | Response B       |
| ...              | ...              |
| Close Connection | Close Connection |



Initialization Phase
--------------------

Before a peer can send any other messages, it must first send and receive an initialize message. The initialize message negotiates various parameters that will be used for the duration of the current session. It must be sent once and only once, as the first message to the other peer.


### Initialize Message Layout

The initialize message is a string of 40 bits, containing the following fields in the following order:

| Field                   | Bits | Min | Max |
| ----------------------- | ---- | --- | --- |
| Protocol Version        |   8  |  1  | 255 |
| Unused (reserved)       |   2  |  0  |   0 |
| Quick Init Request      |   1  |  0  |   1 |
| Quick Init Allowed      |   1  |  0  |   1 |
| Min ID Bits             |   4  |  0  |  15 |
| Max ID Bits             |   5  |  0  |  29 |
| Recommended ID Bits     |   5  |  0  |  29 |
| Min Length Bits         |   4  |  1  |  15 |
| Max Length Bits         |   5  |  1  |  30 |
| Recommended Length Bits |   5  |  1  |  30 |

#### Protocol Version

The protocol version the peer expects to use. In this specification, it is protocol version 1.

#### Quick Init Request, Allowed

These fields are used to negotiate a [Quick Init](#quick-init).

#### ID and Length Bits: Min, Max, Recommended

These fields negotiate how many bits will be used for the ID and length fields in [message chunk headers](#message-header-encoding). Minimum values are capped at 15 to simplify processing and make a number of edge cases impossible. A peer must not set a `max` value to be smaller than its corresponding `min` value. Its `recommended` value must be within its own valid range `min` to `max` (unless it is the "wildcard" value `31`). Invalid values automatically cause negotiation to fail.

#### Recommended Wildcard Value

Recommended values may also be set to the "wildcard" value `31`, which defers to the other peer's recommended value.

### Negotiation Phase

When a peer has received the other peer's initialize message, negotiation of their parameters begins. Both sides follow the same negotiation logic, so there is no need for a response to the initialize message. Negotiation will either succeed or fail, and both peers will follow the same algorithm to reach an identical conclusion.

If negotiation succeeds, the peers may begin sending normal messages. If negotiation fails, the session is dead, and the peers should disconnect.

#### Protocol Version

The peers must agree about the protocol version to use. If they do not, negotiation fails.

#### ID and Length Bit Count

This part of the negotiation determines how many bits will be used to represent the `id` and `length` field field in all [message chunk headers](#message-header-encoding) for this session.

Negotiation of the `id` and `length` fields uses the corresponding `min`, `max`, and `recommended` fields in the initialize message.

Min, max (for `id` and `length`):

    min = maximum(us.min, them.min)
    max = minimum(us.max, them.max)
    if max < min: fail

Recommended (for `id` and `length`):

    recommended = minimum(us.recommended, them.recommended)

Peers may also use the "wildcard" value `31` in their `recommended` fields, meaning that they offer no recommendation, and will defer to the other peer. This changes how `recommended` is calculated:

    if us.recommended = wildcard: recommended = them.recommended
    if them.recommended = wildcard: recommended = us.recommended
    if both us and them use wildcard: recommended = (max-min)/2 + min, rounding up

Negotiated value (for `id` and `length`):

    negotiated value = minimum(maximum(recommended, min), max)

After the initial ID and length bit width negotiations are completed, the total bits must be brought down to 30 if they happen to be over:

    if id.negotiated + length.negotiated > 30:
        if both are > 15: reduce both to 15
        else: (larger value) = 30 - (smaller value)

#### Bit Count Considerations

The choice of bit counts will affect the characteristics of the session. Depending on your use case and operating environment, certain aspects will be more important than others:

* Small ID bit count: Limits the maximum number of simultaneous outstanding requests.
* Large ID bit count: Requires more memory and complexity to keep track of outstanding requests.
* Small length bit count: Increases the marginal costs of the header, losing useful bandwidth to overhead.
* Large length bit count: Increases the buffer size required to support the maximum message chunk length.

Note: An ID bit count of 0 means that there can be only one message in-flight at a time (all messages are implicitly ID 0). A length bit count of 0 is invalid.

The total number of bits negotiated (ID bits + length bits) will determine the [message chunk header](#message-header-encoding) size for the duration of the session:

| Total Bits Negotiated | Message Header Size |
| --------------------- | ------------------- |
| 1-6                   | 1 octet             |
| 7-14                  | 2 octets            |
| 15-22                 | 3 octets            |
| 23-30                 | 4 octets            |


### Quick Init

There may be times when the normal initialization message gating delay is unacceptable. In such a case, peers may elect to quick init. Quick init eliminates the session startup delay before a "client" peer can start sending normal messages, but increases the risk of a failed negotiation (which can be mitigated by outside knowledge).

I will describe a "client" peer as a peer with `quick init request` set to true, and a "server" peer as a peer with `quick init allowed` set to true. A peer must not have both `quick init request` and `quick init allowed` set to true. A peer must not use wildcard values if its `quick init request` is set to true.

On a successful quick init, the "server" peer disregards its own recommended values and chooses the "client" peer's recommended values instead (as if the server had used wildcard values for ID and length). This means that the "client" peer doesn't need to wait for the "server" peer's initialization message to arrive before it can start sending normal messages, because it can precompute the parameters that will be negotiated (assuming negotiation succeeds).

A "client" peer requesting a quick init makes the following assumptions:

- The potential "server" peer will have `quick init allowed` set to true.
- The "client" peer's recommended values will be within the "server" peer's chosen minimums and maximums.

If any of the assumptions prove false, the negotiation will fail. Because of this, quick init should only be used when a "client" peer has a good idea of the parameters the "server" peer will use.

| Peer A QR | Peer B QR | Peer A QA | Peer B QA | Result                         |
| --------- | --------- | --------- | --------- | ------------------------------ |
|     0     |     0     |     -     |     -     | Normal initialization flow     |
|     1     |     0     |     0     |     1     | Quick init using Peer A values |
|     0     |     1     |     1     |     0     | Quick init using Peer B values |
|     1     |     -     |     -     |     0     | Negotiation Failure            |
|     -     |     1     |     0     |     -     | Negotiation Failure            |
|     1     |     1     |     -     |     -     | Negotiation Failure            |
|     1     |     -     |     1     |     -     | Invalid (negotiation failure)  |
|     -     |     1     |     -     |     1     | Invalid (negotiation failure)  |

* QR  = `quick init request`
* QA  = `quick init allowed`
* `-` = don't care


#### Quick Init vs Normal Initialization Flow

With normal initialization flow, both peers are gated on the other's initialize message, which induces some delay before useful message flow begins:

| "Client" Peer | "Server" Peer |
| ------------- | ------------- |
| Initialize    |               |
|               | Initialize    |
| Request A     |               |
| Request B     | Response A    |
|               | Response B    |

Quick init allows the "client" peer to begin sending messages immediately:

| "Client" Peer | "Server" Peer |
| ------------- | ------------- |
| Initialize    |               |
| Request A     |               |
| Request B     | Initialize    |
|               | Response A    |
|               | Response B    |

In this contrived example, the "server" peer is slow to respond for some reason, but that doesn't stop the "client" peer from sending requests before receiving the "server" peer's initialize message.

Should the negotiation ultimately fail, the "server" peer would only send the initialize message, and then ignore everything else:

| "Client" Peer | "Server" Peer |
| ------------- | ------------- |
| Initialize    |               |
| Request A     |               |
| Request B     | Initialize    |

Upon receiving the "server" peer's initialize message, the "client" peer would run the same negotiation algorithm, realize that initialization failed, and end the session.


### Negotiation Examples

#### Case: All parameters are compatible

| Peer   | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |    6   |   12   |    8   |      6     |     20     |     14     |
| Peer B |    6   |   15   |    7   |      5     |     15     |     15     |
| Result |    6   |   12   |    7   |      6     |     15     |     14     |

Negotiation: **Success**

#### Case: Max ID < Min ID

| Peer   | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |    6   |    8   |    8   |      5     |     12     |     12     |
| Peer B |   10   |   15   |   10   |      5     |     15     |     15     |
| Result |   10   |    8   |    8   |      5     |     12     |     12     |

Negotiation: **Fail**

#### Case: Wildcard, initial negotiated values total > 30

| Peer   | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |    6   |   16   |   14   |      6     |     20     |     31     |
| Peer B |    6   |   18   |   15   |     15     |     18     |     31     |
| Result |    6   |   16   |   14   |     15     |     18     |     16     |

In this case, both peer A and B used wildcard values for recommended length, resulting in (18-15)/2 + 15, rounded up, which is 17. The resulting values (length 17, id 14) are greater than 30, so the larger value is reduced (30 - 14 = 16).

Negotiation: **Success**

#### Case: All recommended values use wildcard

| Peer   | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |    6   |   16   |   31   |      6     |     20     |     31     |
| Peer B |    6   |   18   |   31   |      8     |     15     |     31     |
| Result |    6   |   16   |   11   |      8     |     15     |     12     |

Length is (15-8)/2 + 8, rounded up = 12.

ID is (16-6)/2 + 6 = 11.

Negotiation: **Success**

#### Case: Peer A requests quick init, and peer B allows quick init

| Peer   | Quick Req | Quick Allow | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | --------- | ----------- | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |     1     |      0      |    8   |   15   |    8   |     10     |     18     |     14     |
| Peer B |     0     |      1      |    6   |   18   |   10   |      8     |     15     |     10     |
| Result |     -     |      -      |    8   |   15   |    8   |     10     |     15     |     14     |

Since we are using quick init, Peer A's recommended values are chosen, and Peer B's recommended values are ignored (as if they were wildcard values).

Negotiation: **Success**

#### Case: Peer A requests quick init using values peer B doesn't support

| Peer   | Quick Req | Quick Allow | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | --------- | ----------- | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |     1     |      0      |    8   |   15   |    8   |     10     |     18     |     16     |
| Peer B |     0     |      1      |    6   |   18   |   10   |      8     |     15     |     10     |
| Result |     -     |      -      |    8   |   15   |    8   |     10     |     18     |    fail    |

Peer A's recommendeded length (16) is higher than peer B's max (15).

Negotiation: **Fail**

#### Case: Peer B allows quick init, but peer A doesn't request quick init

| Peer   | Quick Req | Quick Allow | ID Min | ID Max | ID Rec | Length Min | Length Max | Length Rec |
| ------ | --------- | ----------- | ------ | ------ | ------ | ---------- | ---------- | ---------- |
| Peer A |     0     |      0      |    8   |   15   |    8   |     10     |     18     |     14     |
| Peer B |     0     |      1      |    6   |   18   |   10   |      8     |     15     |     10     |
| Result |     -     |      -      |    8   |   15   |    8   |     10     |     15     |     10     |

Since quick init wasn't requested, Peer B's `quick allow` has no effect, and negotiation follows the normal flow.

Negotiation: **Success**



Normal Messages
---------------

Normal messages are sent in chunks, consisting of a message chunk header, followed by a possible data payload (whose contents are application-specific).

| Section | Octets   |
| ------- | -------- |
| Header  | 1 to 4   |
| Payload | variable |


### Message Header Encoding

The message chunk header is treated as a single (8, 16, 24, or 32 bit) unsigned integer composed of bit fields, and is transmitted in little endian byte order. The header size is determined by the field length choices in the [initialization phase](#initialization-phase):

| Min Bit Size | Max Bit Size | Header Size |
| ------------ | ------------ | ----------- |
|       1      |       6      |      8      |
|       7      |      14      |     16      |
|      15      |      22      |     24      |
|      23      |      30      |     32      |

The header fields contain information about what kind of message this is. The request ID and length fields are placed adjacent to each other, next to the response and termination bits. Any unused upper bits must be cleared to `0`.

| Field       | Bits      | Order     |
| ----------- | --------- | --------- |
| Unused      |  variable | High Bit  |
| Request ID  |  variable |           |
| Length      |  variable |           |
| Response    |         1 |           |
| Termination |         1 | Low Bit   |

For example, a 10/14 header (10-bit ID, 14-bit length, which would result in a 32-bit header with 6 bits unused) would be conceptually viewed as:

    000000iiiiiiiiiillllllllllllllrt

A 5/9 header (5-bit ID, 9-bit length, which would result in a 16-bit header) would be conceptually viewed as:

    iiiiilllllllllrt

A 0/6 header (0-bit ID, 6-bit length, which would result in an 8-bit header) would be conceptually viewed as:

    llllllrt


### Message Fields

#### Length

The length field refers to the number of octets in the payload portion of this chunk (the message chunk header itself does not count towards the length).

#### Request ID

The request ID field is a unique number that is generated by the requesting peer to differentiate the current request from other requests that are still in-flight (IDs that have been sent to the peer but have not yet received a response, or have been canceled but not yet cancel acked). The requesting peer must not re-use IDs that are currently in-flight.

Request IDs are scoped to the requesting peer. For example, request ID 22 from peer A is distinct from request ID 22 from peer B.

Note: The first chosen request ID in the session should be unpredictable in order to make known-plaintext attacks more difficult (see [RFC 1750](https://tools.ietf.org/html/rfc1750)).

#### Response

The response bit is used to respond to a request sent by a peer. When set, the ID field refers to the ID of the original request sent by the requesting peer (scope is inverted).

#### Termination

The termination bit indicates that this is the final chunk for this request ID (as a request message or as a response message). For a large message that spans multiple chunks, you would clear this to 0 for all but the last chunk.


### Special Cases

#### Empty Termination

A zero-length chunk with the termination bit set is an `empty termination` type, and is processed as if the last non-zero-length chunk of the same ID would have had its termination bit set. This is sometimes necessary in streaming situations, where the encoding process cannot reliably know where the end of the stream is. For example:

    [ID 5, 3996 bytes] [ID 5, 4004 bytes] [ID 5, 0 bytes + termination]

vs

    [ID 5, 2844 bytes] [ID 5, 5000 bytes] [ID 5, 156 bytes + termination]

Note that interleaved chunks for other IDs don't affect this behavior:

    [ID 5, 4004 bytes] [ID 2, 1020 bytes] ... [ID 5, 0 bytes + termination]

If an `empty termination` is the only chunk for its ID (i.e. the entire message is 1 chunk, 0 bytes long), it is an `empty message`. An `empty message` can either be an [out of band](#out-of-band-messages) `ping` (response = 0), or an `empty response` (response = 1).

#### Empty Response

An `empty response` is an `empty message` with the response bit set to 1. `empty response` signals successful completion of the request, with no other data to report.



Out of Band Messages
--------------------

This section lists out-of-band messages, which are necessary to the efficient operation of the session. Two of them overlap with normal messages (see [special cases](#special-cases)), and are considered out-of-band only in special circumstances. OOB messages are sent with a higher priority than all other messages.


### OOB Message Types

#### Cancel

The `cancel` message cancels a request in progress. The ID field specifies the request to cancel. Upon sending a `cancel` message, the ID is locked (cannot be used, and all responses to that ID must be discarded) until a `cancel ack` message for that ID is received.

#### Cancel Ack

Sent to acknowledge a `cancel` request. The serving peer cancels the operation, removes all queued response chunks to that request ID, and then sends a `cancel ack`. If the request doesn't exist (possibly because it had already completed), the serving peer must still send a `cancel ack`, because the other peer's ID will remain locked until an ack is received.

#### Ping

A `ping` requests a `ping ack` from the peer. Upon receiving a `ping`, a peer must send a `ping ack` as early as possible. `ping` is useful for gauging latency, and as a "keep-alive" mechanism over mediums that close idle sessions.

#### Ping Ack

A `ping ack` acknowledges a `ping` request.


### OOB Message Encoding

OOB messages are signified by a message chunk length of 0, and also certain circumstances. The response and termination bits determine the message type:

| ID  | Length | Response | Termination | Normal Meaning    | OOB Circumstance | OOB Meaning |
| --- | ------ | -------- | ----------- | ----------------- | ---------------- | ----------- |
| Any |    0   |     0    |       0     |                   | Always           | Cancel      |
| Any |    0   |     1    |       0     |                   | Always           | Cancel Ack  |
| Any |    0   |     0    |       1     | Empty Termination | First Chunk      | Ping        |
| Any |    0   |     1    |       1     | Empty Response    | Response to Ping | Ping Ack    |

### OOB Special Circumstances

The OOB message types `ping` and `ping ack` share encodings with request `empty termination`, and with `empty response`. It is only in certain circumstances that they become OOB messages:

#### Empty Termination vs Ping

An `empty message` request (an `empty termination` where the entire message is 0 bytes long, and `response` = 0) is considered a `ping` message.

#### Empty Response vs Ping Ack

If an `empty response` is responding to a `ping`, it is considered a `ping ack`.


### OOB Message Priority

Implementations of this protocol must include message queue priority functionality. OOB messages must be sent at a higher priority than all normal messages.



Sending Messages
----------------

Messages are sent in chunks. A multi-chunk message has its `termination bit` cleared to `0` for all but the last chunk. Single chunk messages always have the `termination bit` set.

The request ID is scoped to its sender. If both peers send a request with the same ID, they are considered to be distinct, and don't conflict with each other. The `response bit` inverts the scope: A peer responds to a request by using the same request ID as it received from the requesting peer, and setting the response bit to `1`.

### Flow

The message send and receive flow is as follows (using an example request ID of 5):

* Sender sends request with ID 5 (with the response bit cleared) to the receiver.
* Receiver processes request ID 5
* Receiver sends response with ID 5 (with the response bit set) to the sender.

This works in both directions. Participants are peers, and can both initiate requests and respond to them (acting as client and server simultaneously).

Responses may be sent in a different order than the requests were received.


### Multiplexing

Message chunks with the same ID must be sent in-order (chunk 5 of message 42 must not be sent before chunk 4 of message 42). The message is considered complete once the `termination bit` is set. Note that this does not require you to send all chunks for one message before sending chunks from another message. Chunks from different messages can be sent interleaved, like so:

* Message 10, chunk 0
* Message 11, chunk 0
* Message 12, chunk 0
* Message 11, chunk 1
* Message 10, chunk 1 (termination = 1)
* Message 11, chunk 2
* Message 12, chunk 1 (termination = 1)
* Message 11, chunk 3 (termination = 1)

Your choice of message chunk sizing and scheduling will depend on your use case.



Request Cancellation
--------------------

There are times when a requesting peer might want to cancel a request-in-progress. Circumstances may change, or the operation may be taking too long. A requesting peer may cancel an outstanding request by sending a `cancel` message, citing the request ID of the request to be canceled.

Once a `cancel` request has been issued, the ID of the canceled request is locked. A locked request ID cannot be used, and all response chunks to that request ID must be discarded. Once a `cancel ack` is received, the request ID is unlocked and may be used again.

#### Example:

* Peer A sends request ID 19
* Peer B receives request ID 19 and begins servicing it
* Peer A times out
* Peer A locks ID 19 and sends `cancel` ID 19
* Peer B sends response ID 19
* Peer A discards response ID 19 (because Peer A's ID 19 is still locked)
* Peer B receives `cancel` ID 19
* Peer B sends `cancel ack` ID 19
* Peer A receives `cancel ack` ID 19 and unlocks ID 19

If a `cancel ack` is not received, it means that either there is a communication problem (such as lag or a broken connection), or the serving peer is operating incorrectly.



Spurious Messages
-----------------

There are situations where a peer may receive spurious (unexpected) messages. Spurious messages include:

* Response to a canceled request.
* `cancel` message for a request not in progress.

These situations can arise when the transmission medium is lagged, or as the result of a race condition. A response to a request may have already been en-route at the time of cancellation, or the operation may have already completed before the `cancel` message arrived. A response to a canceled request must be ignored and discarded. A peer must always respond to a `cancel` request with a `cancel ack`, even if there is no operation to cancel.

The following are error conditions:

* Response to a nonexistent request.
* `cancel ack` for a request that wasn't canceled.

In the error case, the peer may elect to report an error (outside of the scope of this protocol) or end the connection, depending on your use case.



Version History
---------------

 * April 10, 2019: Preview Version 1



License
-------

Copyright (c) 2019 Karl Stenerud. All rights reserved.

Distributed under the Creative Commons Attribution License: https://creativecommons.org/licenses/by/4.0/legalcode
License deed: https://creativecommons.org/licenses/by/4.0/
