Streamux
========

A minimalist, asynchronous, multiplexing, request-response protocol.

### Features

* Minimal Overhead (2 or 4 bytes per message chunk, depending on configuration)
* Multiplexing (multiple data streams can be sent across a single channel)
* Asynchronous (client is informed asynchronously upon completion or error)
* Interruptible (requests may be canceled)
* Floating roles (both peers can operate as client and server at the same time)

It is expected that a more application-specific messaging protocol will be layered on top of this protocol. Even something simple like using JSON objects for message contents would work.



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

| Octet 0 | Octet 1          | Octet 2      |
| ------- | ---------------- | ------------ |
| Version | Length Bit Count | ID Bit Count |


#### Version

The version field determines the maximum protocol version that this peer can support. The decided protocol version will be the minumum of the values provided by each peer. `0` is an invalid value.

Currently, only version 1 exists.


#### Length and ID Bit Counts

Length and ID bit counts determine how many bits will be used for the length and ID fields in regular message headers. The decided size for each field will be the minimum of the values provided by each peer.

[The regular message header](#header-fields) consists of 2 flag bits, with the rest of the bits free for use as `length` and `id` fields. A message header can be either 16 or 32 bits wide, which leaves 14 or 30 bits for the length and ID fields. If the combined length and ID bit counts of the initiator request total 14 or less, message headers will be 16 bits wide. It is an error to specify a total bit count greater than 30.


#### Bit Count Wildcard Values

The value `0`, which would normally be invalid, has special meaning as the "wildcard" value when specifying a bit count. When a peer uses a wildcard value for a bit count, it is stating that it doesn't care what the end value will be. If one peer uses the wildcard value and the other does not, the non-wildcard value is chosen. If both peers use the wildcard value for a field, 30 minus the other bit count field result is chosen. If both fields (length bit count and ID bit count) are set to the wildcard value by both peers, the result is 15 length bits and 15 ID bits.

It is recommended for peers that serve primarily "server" functionality to use wildcard values, which allows client peers - who are likely to have more varying network conditions - to control these values.


#### Sizing Considerations

The choice of bit counts will affect the characteristics of the session. Depending on your use case and operating environment, certain aspects will be more important than others:

* Small length bit count: Increases the marginal costs of the header, causing data wastage.
* Large length bit count: Increases the buffer size required to support the maximum message length.
* Small ID bit count: Limits the maximum number of simultaneous outstanding requests.
* Large ID bit count: Requires more memory and complexity to keep track of outstanding requests.


#### Examples:

| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |      20     |    10   |
| Peer B  |      10     |    12   |
| Result  |      10     |    10   |


| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |       8     |     0   |
| Peer B  |       9     |     5   |
| Result  |       8     |     5   |


| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |       0     |    10   |
| Peer B  |       0     |     7   |
| Result  |      23     |     7   |


| Peer    | Length Bits | ID Bits |
| ------- | ----------- | ------- |
| Peer A  |       0     |     0   |
| Peer B  |       0     |     0   |
| Result  |      15     |    15   |


### Initiator Response

An initiator response must be sent only once, in response to an initiator request.

| Octet 0                           |
| --------------------------------- |
| `0` (accept), or nonzero (reject) |

Only two responses (`accept` or `reject`) are possible. If a peer rejects the other's initiator request, the session is now considered "dead", and the connection should be closed. This can happen if the negotiated parameters are something that the peer cannot accommodate, or if the initiator request was malformed.

If a peer accepts the other's initiator request, this side of the session is now established, and the peer may begin sending regular messages.

| Peer A             | Peer B             | Notes                                |
| ------------------ | ------------------ | ------------------------------------ |
| Initiator Request  | Initiator Request  |                                      |
|                    | Initiator Response | Peer B may now send regular messages |
| Initiator Response |                    | Peer A may now send regular messages |


### Initiator Message Flow

The initiator flow is gated on each side only to the request, because it is needed in order to formulate a response. Once a peer has sent an initiator response, it is free to begin sending normal messages, even if it hasn't yet received the other peer's response. If it turns out that the other peer has rejected the initiator request, the session is dead anyway, and none of the sent requests will have been processed.

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



Regular Messages
----------------

Regular messages consist of a 16 or 32 bit header (determined by the [initiator request](#length-and-id-bit-counts)), followed by a possible data payload. The header is transmitted as a 16 or 32 bit unsigned integer in little endian format. The payload contents are beyond the scope of this document.

### Message Layout

| Section | Octets   |
| ------- | -------- |
| Header  | 2 or 4   |
| Payload | variable |


### Header Fields

The header fields contain information about what kind of message this is. Some fields have variable widths, which are determined for the entirety of the session by the [initiator request](#initiator-request). The length and request ID fields are placed adjacent to each other, next to the response and termination bits. The unused upper bits must be cleared to `0`.

| Field       | Bits      |
| ----------- | --------- |
| Unused      |  variable |
| Length      |  variable |
| Request ID  |  variable |
| Response    |         1 |
| Termination |         1 |

For example, a 14/10 (length 14 bits, ID 10 bits) header would be conceptually viewed as:

    000000lllllllllllllliiiiiiiiiirt


### Fields

#### Length

The length field refers to the number of octets in the payload portion of this chunk (i.e. the message header does not count towards the length).

#### Request ID

The request ID field is a unique number that is generated by the sender to differentiate the current request from other requests the sender has sent in the past. The sender must not use IDs that are currently in-flight (IDs that have been sent to the peer but have not yet received a response, or have been canceled but not yet acked).

Request IDs are scoped to each peer. For example, request ID 22 from peer A is distinct from request ID 22 from peer B.

#### Response

The response bit is used to respond to a request sent by a peer. When set, the ID field refers to the ID of the original request sent by the peer.

#### Termination

The termination bit indicates that this is the final chunk for this request ID (either as a request or as a response). For a large message that spans multiple chunks, you would clear this to 0 for all but the last chunk.



Special Messages
----------------

A length of 0 confers special meaning to the message, depending on the response and termination fields:

| Length | Response | Termination | Meaning        |
| ------ | -------- | ----------- | -------------- |
|    0   |     0    |       0     | Cancel         |
|    0   |     1    |       0     | Cancel Ack     |
|    0   |     0    |       1     | Ping           |
|    0   |     1    |       1     | Empty Response |

### Cancel

The cancel message cancels a request in progress. The ID field specifies the request to cancel. After sending a cancel message, the ID is locked (cannot be used, and all responses to that ID are discarded) until a Cancel Ack message for that ID is received.

### Cancel Ack

Sent to acknowledge a cancel request. The request is canceled, and all queued responses to that request ID are removed. Once this is done, the server sends a Cancel Ack. If the request doesn't exist (possibly because it had already completed), the server must still send a Cancel Ack.

### Ping

A ping requests an empty response from the peer. Upon receiving a ping, a peer must send an empty response as quickly as possible (jumping ahead of any already queued messages). This allows peers to gauge the latency between them, and also provides for a "keep-alive" mechanism.

### Empty Response

Can be sent in response to any request except `cancel`, indicating successful completion but no other data to report.



Sending Messages
----------------

Messages are sent in chunks. A multi-chunk message has its `termination bit` cleared to 0 for all but the last chunk. Single chunk messages always have the `termination bit` set.

The request ID is scoped to its sender. If both peers send a request with the same ID, they are considered to be distinct, and don't conflict with each other. The `response bit` inverts the scope: A peer responds to a request by using the same request ID as it received from the sender, and setting the response bit.

### Flow

The message send and receive flow is as follows (using an example request id of 5):

* Sender sends request with ID 5 (with the response bit cleared) to the receiver.
* Receiver processes request ID 5
* Receiver sends response with ID 5 (with the response bit set) to the sender.

This works in both directions. Participants are peers, and can both initiate requests and respond to them (acting as client and server simultaneously).

Responses may be sent in a different order than the requests were received.


### Multiplexing

Message chunks with the same ID must be sent in-order (chunk 5 of message 42 must not be sent before chunk 4 of message 42). The message is considered complete once the `termination bit` is set. Note that this does not require you to send all chunks for one message before sending chunks from another message. They can be multiplexed, like so:

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

There are times when a sender might want to cancel a request-in-progress. Circumstances may change, or the operation may be taking too long. A peer may cancel an outstanding request by sending a cancel message, citing the request ID of the request to be canceled.

Once a cancel order has been issued, the ID of the canceled request is locked. A locked request ID cannot be used, and all responses to that request ID are discarded. Once a Cancel Ack is received, the request ID is unlocked and may be used again.

#### Example:

* Sender sends request ID 19
* Sender times out
* Sender sends cancel ID 19
* Receiver sends response ID 19, which is discarded by the sender
* Receiver sends cancel ack ID 19
* Sender receives cancel ack and unlocks ID 19

If a Cancel Ack is not received, it means that either there is a communication problem (such as lag), or the server is operating incorrectly.



Spurious Messages
-----------------

There are situations where a peer may receive spurious (unexpected) messages. Spurious messages include:

* Response to a canceled request.
* Cancel message for a request not in progress.

These situations can arise when the transmission medium is lagged, or as the result of a race condition. A response to a request may have already been en-route at the time of cancellation, or the operation may have completed before the cancel message arrived. A response to a canceled request can be ignored and discarded. A cancel message must always be responded to with a cancel ack.

The following are most likely error conditions:

* Response to a nonexistent request.
* Cancel Ack for a request that wasn't canceled.

In the error case, the peer may elect to report an error (outside of the scope of this protocol) or end the connection, depending on your use case.



Version History
---------------

 * April 10, 2019: Preview Version 1



License
-------

Copyright (c) 2019 Karl Stenerud. All rights reserved.

Distributed under the Creative Commons Attribution License: https://creativecommons.org/licenses/by/4.0/legalcode
License deed: https://creativecommons.org/licenses/by/4.0/
