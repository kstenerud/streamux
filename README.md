Streamux
========

A minimalist, asynchronous, multiplexing, request-response protocol.

### Features

* Minimal Overhead (1 to 4 bytes per message chunk, depending on configuration)
* Multiplexing (multiple data streams can be sent across a single channel)
* Asynchronous (client is informed asynchronously upon completion or error)
* Interruptible (requests may be canceled)
* Floating roles (both peers can operate as client and server at the same time)
* Quick init mode, requiring no round-trips

It is expected that a more application-specific messaging protocol will be layered on top of this protocol. Even something simple like using JSON objects for message contents would work (albeit wastefully).



General Operation
-----------------

Upon establishing a connection, each peer sends an initiator request and response, and then begins normal communications. Communication is asynchronous after the peer has sent an initiator response. A typical session might look something like this:

| Peer X             | Peer Y             |
| ------------------ | ------------------ |
| Open Connection    | Open Connection    |
|                    | Initiator Request  |
| Initiator Request  |                    |
| Initiator Response |                    |
| Request A          | Initiator Response |
| Request B          | Response A         |
|                    | Request C          |
| Response C         | Response B         |
| ...                | ...                |
| Close Connection   | Close Connection   |



Initiator Phase
---------------

Before a peer can send any other messages, it must first send an initiator request, and then respond to the other peer's initiator request.


### Initiator Request

The initiator request negotiates various properties that will be used for the duration of the current session. It must be sent once and only once, as the first message to the other peer.

| Octet 0 | Octet 1          | Octet 2      | Octet 3 |
| ------- | ---------------- | ------------ | ------- |
| Version | Length Bit Count | ID Bit Count | Flags   |


#### Version

Requests a particular protocol version. The decided protocol version will be the minimum of the two peers. A peer may not support versions that low, and would in such a case send a reject response. `0` is an invalid value.

Currently, only version 1 exists.


#### Length and ID Bit Counts

Length and ID bit counts determine how many bits will be used for the length and ID fields in message chunk headers. The decided size for each field will be the minimum of the values provided by each peer.

[The message chunk header](#header-fields) consists of 2 flag bits, with the rest of the bits free for use as `length` and `id` fields. A message chunk header can be 8, 16, 24, or 32 bits wide, which leaves 6, 14, 22, or 30 bits for the length and ID fields. If the combined length and ID bit counts of the initiator request total 6 or less, messages headers will be 8 bits wide (14 or less: 16 bits wide, 22 bits or less: 24 bits wide). It is an error to specify a total bit count greater than 30.

Note: A length bit count of 0 is invalid. An ID bit count of 0 means that there can be only one message in-flight at a time (all messages are implicitly ID 0).

##### Bit Count Wildcard Values

The value `255`, which would normally be invalid, has special meaning as the "wildcard" value when specifying a bit count. When a peer uses a wildcard value for a bit count, it is stating that it doesn't care what the end value will be.

* If one peer uses the wildcard value and the other does not, the non-wildcard value is chosen.
* If one bit count field is set to the wildcard value by both peers, the result is 30 minus the other bit count field result, to a maximum of 15 bits.
* If both bit count fields are set to the wildcard value by both peers, the result is 15 length bits and 15 ID bits.

It is recommended for peers that provide primarily "server" functionality to use wildcard values, which allows client peers - who are likely to have more varying network conditions - to control these values.


##### Bit Count Considerations

The choice of bit counts will affect the characteristics of the session. Depending on your use case and operating environment, certain aspects will be more important than others:

* Small length bit count: Increases the marginal costs of the header, causing data wastage.
* Large length bit count: Increases the buffer size required to support the maximum message chunk length.
* Small ID bit count: Limits the maximum number of simultaneous outstanding requests.
* Large ID bit count: Requires more memory and complexity to keep track of outstanding requests.


##### Bit Count Examples

| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |      20     |    10   |
| Peer B  |      10     |    12   |
| Result  |      10     |    10   |


| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |     255     |     5   |
| Peer B  |       9     |     7   |
| Result  |       9     |     5   |


| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |      18     |   255   |
| Peer B  |      16     |   255   |
| Result  |      16     |    14   |


| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |     255     |   255   |
| Peer B  |     255     |   255   |
| Result  |      15     |    15   |


#### Flags

The flags enable or request certain features.

| Position | Meaning                |
| -------- | ---------------------- |
| 7 (0x80) | Quick Init Request     |
| 6 (0x40) | Quick Init Allowed     |
| 5 (0x20) | reserved, cleared to 0 |
| 4 (0x10) | reserved, cleared to 0 |
| 3 (0x08) | reserved, cleared to 0 |
| 2 (0x04) | reserved, cleared to 0 |
| 1 (0x02) | reserved, cleared to 0 |
| 0 (0x01) | reserved, cleared to 0 |

Quick init flags will be discussed in section [Quick Init](#quick-init).


### Initiator Response

An initiator response must be sent only once, in response to an initiator request.

| Octet 0                           |
| --------------------------------- |
| `0` (accept), or nonzero (reject) |

If a peer rejects the other's initiator request, the session is now considered "dead": All messages must be ignored, and the connection should be closed. This can happen if the negotiated parameters are something that the peer cannot or does not want to accommodate, or if the initiator request was malformed.

If a peer accepts the other's initiator request, this side of the session is now established, and the peer may begin sending normal messages.

| Peer A            | Peer B            | Notes                               |
| ----------------- | ----------------- | ----------------------------------- |
| Initiator Request | Initiator Request |                                     |
|                   | Initiator Accept  | Peer B may now send normal messages |
| Initiator Accept  |                   | Peer A may now send normal messages |


### Initiator Message Flow

The initiator flow is gated on each side to the other's initiator request, because it is needed in order to formulate an initiator response. Once a peer has sent an initiator response `accept`, it is free to begin sending normal messages, even if it hasn't yet received the other peer's response. If it turns out that the other peer has rejected the initiator request, the session is dead anyway, and none of the sent requests will have been processed.

#### Successful Flow

* Peer A: initiator request
* Peer B: initiator request
* Peer B: initiator accept
* Peer B: request ID 0
* Peer A: initiator accept
* Peer A: response ID 0

In this example, Peer A was a little slow to respond, and Peer B went ahead with messaging after responding to Peer A's initiator request. Since Peer A eventually accepted the initiator request, everything is OK, and request 0 gets processed.

#### Failure Flow

* Peer A: initiator request
* Peer B: initiator request
* Peer B: initiator accept
* Peer B: request ID 0
* Peer A: initiator reject

In this example, Peer A is once again slow to respond, and Peer B once again goes ahead with messaging, but it turns out that Peer A eventually rejects the initiator request. Request 0 never gets processed or responded to by Peer A because the session is dead. The peers must now disconnect.


### Quick Init

There may be times when the normal initiator flow is considered too chatty. In such a case, peers may elect to quick init, which eliminates gating on the initial request-response by automatically choosing the init values from the "client-y" peer. A "server-y" peer may elect to allow quick init, meaning that it is willing to disregard its own initiator request and use the client's recommendations instead (as if the server had used wildcard values for length and ID). Only the "server-y" side (the side that sets `quick init allowed` = 1) sends an `initiator response` during a quick init.

Note: Protocol version negotiation occurs the same as in the normal initiator flow (the lowest of the two versions is chosen).

#### Successful Flow

* Peer A: initiator request with `quick init request` = 1
* Peer A: request ID 0
* Peer B: initiator request with `quick init allowed` = 1
* Peer B: initiator accept
* Peer B: response ID 0

Peer A doesn't wait for Peer B's `initiator request` before sending normal messages, simply assuming everything will be fine. Once Peer B's request arrives, both sides understand that they are in agreement, and message processing continues normally. Peer B must of course wait for Peer A's `initiator request` before it can send any messages. Peer B may still choose to reject Peer A's initiator request if it cannot or will not accommodate the parameters.

#### Failure Flow

* Peer A: initiator request with `quick init request` = 1
* Peer A: request ID 0
* Peer B: initiator request with `quick init allowed` = 0
* Negotiation failed (no further messages sent)

In this case, Peer B doesn't allow quick init, and so session initialization fails. Request ID 0 was ignored, and the session is dead. Both sides disconnect. The client may of course elect to connect again with `quick init request` = 0, following the normal initiator flow instead.

#### Quick Init Rules

| Peer A QR | Peer B QR | Peer A QA | Peer B QA | Result                         |
| --------- | --------- | --------- | --------- | ------------------------------ |
|     0     |     0     |     -     |     -     | Normal initiator flow          |
|     1     |     1     |     -     |     -     | Negotiation Failure            |
|     1     |     -     |     -     |     0     | Negotiation Failure            |
|     -     |     1     |     0     |     -     | Negotiation Failure            |
|     1     |     0     |     -     |     1     | Quick init using Peer A values |
|     0     |     1     |     1     |     -     | Quick init using Peer B values |
|     1     |     -     |     1     |     -     | Invalid                        |
|     -     |     1     |     -     |     1     | Invalid                        |

* QR = `quick init request`
* QA = `quick init allowed`
* - = don't care

There must be agreement outside of the protocol about which peer shall be "client-y" and which shall be "server-y" before using quick connect. If the peers do not have a-priori agreement about their respective roles, they won't successfully negotiate a quick init session.



Normal Messages
---------------

Normal messages are sent in chunks, consisting of an 8, 16, 24, or 32 bit header (determined by the [initiator request](#length-and-id-bit-counts)) followed by a possible data payload. The payload contents are beyond the scope of this document.


### Message Chunk Layout

| Section | Octets   |
| ------- | -------- |
| Header  | 1 to 4   |
| Payload | variable |

The header is treated as a single (8, 16, 24, or 32 bit) unsigned integer composed of bit fields, and is transmitted in little endian format.

As noted earlier, the header size depends on the combined length and ID bit sizes chosen in the [initiator request](#initiator-request):

| Min Bit Size | Max Bit Size | Header Size |
| ------------ | ------------ | ----------- |
|       1      |       6      |      8      |
|       7      |      14      |     16      |
|      15      |      22      |     24      |
|      23      |      30      |     32      |


### Message Header Encoding

The header fields contain information about what kind of message this is. Some fields have variable widths, which are determined for the entirety of the session by the [initiator request](#initiator-request). The length and request ID fields are placed adjacent to each other, next to the response and termination bits. Any unused upper bits must be cleared to `0`.

| Field       | Bits      | Order     |
| ----------- | --------- | --------- |
| Unused      |  variable | High Bit  |
| Length      |  variable |           |
| Request ID  |  variable |           |
| Response    |         1 |           |
| Termination |         1 | Low Bit   |

For example, a 14/10 header (14-bit length, 10-bit ID, which would result in a 32-bit header with 6 bits unused) would be conceptually viewed as:

    000000lllllllllllllliiiiiiiiiirt

A 9/5 header (9-bit length, 5-bit ID, which would result in a 16-bit header) would be conceptually viewed as:

    llllllllliiiiirt

A 6/0 header (6-bit length, 0-bit ID, which would result in an 8-bit header) would be conceptually viewed as:

    llllllrt


### Message Fields

#### Length

The length field refers to the number of octets in the payload portion of this chunk (i.e. the message chunk header does not count towards the length).

#### Request ID

The request ID field is a unique number that is generated by the requesting peer to differentiate the current request from other requests that are still in-flight (IDs that have been sent to the peer but have not yet received a response, or have been canceled but not yet acked). The requesting peer must not re-use IDs that are currently in-flight.

Request IDs are scoped to the requesting peer. For example, request ID 22 from peer A is distinct from request ID 22 from peer B.

Note: The first chosen request ID in the session should be unpredictable in order to make known-plaintext attacks more difficult (see [RFC 1750](https://tools.ietf.org/html/rfc1750)).

#### Response

The response bit is used to respond to a request sent by a peer. When set, the ID field refers to the ID of the original request sent by the requesting peer (scope is inverted).

#### Termination

The termination bit indicates that this is the final chunk for this request ID (either as a request message or as a response message). For a large message that spans multiple chunks, you would clear this to 0 for all but the last chunk.


### Special Cases

#### Empty Termination

A zero-length chunk with the termination bit set is an `empty termination` type, and is processed as if the last non-zero-length chunk of the same ID would have had its termination bit set. This is sometimes necessary in streaming situations, where the encoding process cannot reliably know where the end of the stream is. For example:

    [ID 5, 3996 bytes] [ID 5, 4004 bytes] [ID 5, 0 bytes + termination]

vs

    [ID 5, 2844 bytes] [ID 5, 5000 bytes] [ID 5, 156 bytes + termination]

Note that interleaved chunks for other IDs don't affect this behavior:

    [ID 5, 4004 bytes] [ID x, y bytes] ... [ID 5, 0 bytes + termination]

If an `empty termination` is the first chunk for its ID (not preceded by non-empty, non-terminated chunks for the same ID), it is either an `empty response` (response = 1) or an [out of band](#out-of-band-messages) `ping` (response = 0):

    [ID 7, 0 bytes + termination]

#### Empty Response

An `empty response` is an `empty termination` response (length = 0, response = 1, termination = 1) with no preceding non-empty, non-terminated response chunks of the same ID.

`empty response` signals successful completion of the request with no other data to report.



Out of Band Messages
--------------------

This section lists out-of-band messages, which are necessary to the efficient operation of the session. Two of them overlap with normal messages (see [special cases](#special-cases)), and are considered out-of-band only in special circumstances.


### OOB Message Types

#### Cancel

The `cancel` message cancels a request in progress. The ID field specifies the request to cancel. Upon sending a `cancel` message, the ID is locked (cannot be used, and all responses to that ID must be discarded) until a `cancel ack` message for that ID is received.

#### Cancel Ack

Sent to acknowledge a `cancel` request. The operation is canceled, and all queued responses to that request ID are removed. Once this is done, the serving peer sends a `cancel ack`. If the request doesn't exist (possibly because it had already completed), the serving peer must still send a `cancel ack`, because the other peer's ID will remain locked until an ack is received.

#### Ping

A `ping` requests a `ping ack` from the peer. Upon receiving a `ping`, a peer must send `ping ack` as quickly as possible. `ping` is useful for gauging latency, and as a "keep-alive" mechanism over mediums that close idle sessions.

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

If an `empty termination` request is the first chunk (not preceded by non-empty, non-terminated request chunks of the same ID), it is considered a `ping` message.

#### Empty Response vs Ping Ack

If an `empty response` is responding to a `ping`, it is considered a `ping ack`.


### OOB Message Priority

Implementations of this protocol must include message queue priority functionality. OOB messages must be sent at a higher priority than all normal messages.



Sending Messages
----------------

Messages are sent in chunks. A multi-chunk message has its `termination bit` cleared to 0 for all but the last chunk. Single chunk messages always have the `termination bit` set.

The request ID is scoped to its sender. If both peers send a request with the same ID, they are considered to be distinct, and don't conflict with each other. The `response bit` inverts the scope: A peer responds to a request by using the same request ID as it received from the requesting peer, and setting the response bit.

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

Once a `cancel` request has been issued, the ID of the canceled request is locked. A locked request ID cannot be used, and all responses to that request ID must be discarded. Once a `cancel ack` is received, the request ID is unlocked and may be used again.

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
