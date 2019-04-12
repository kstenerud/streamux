Streamux
========

A minimalist, multiplexing, asynchronous, bi-directional messaging and streaming protocol.

This is a protocol between peers. Each side may act in both a client and a server role, sending messages and replying to messages.

This protocol functions as a low level multiplexing, asynchronous, interruptible message chunking layer. It is expected that a messaging protocol will be layered on top of it. Even something primitive like using JSON objects as messages would work fine.



General Operation
-----------------

Upon establishing a connection, peers send initiator messages, and then begin normal communications. A typical session would look like this:

* Open connection
* Each peer sends an initiator message
* Each peer sends an initiator message reply and begins normal messaging
* Close connection



Initiator message
-----------------

Before sending any other messages, each peer must send an initiator message of its own, and reply to the other peer's initiator message.


### Initiator Request

| Octet 0 | Octet 1          | Octet 2      |
| ------- | ---------------- | ------------ |
| Version | Length Bit Count | ID Bit Count |

#### Version Field

Currently `1`

#### Length and ID Bit Counts

[The regular message header](#header-fields) consists of 2 flag bits, with the rest of the bits free for use as `length` and `id` fields. A message header can be either 16 or 32 bits wide. If the length and ID bit counts total 14 or less, message headers will be 16 bits wide. At most, 30 bits may be used for length and ID combined.

The length and ID bit count fields of the initiator message allow the peers to recommend how many bits should be used to encode those fields over the current session. The resulting size for each field will be the minimum of the values provided by each peer.

#### Wildcard Values

The value `0`, which would normally be invalid, has special meaning as the "wildcard" value. When a peer uses a wildcard value, it is stating that it doesn't care what the end value will be. If one peer uses the wildcard value and the other does not, the non-wildcard value is chosen. If both peers use the wildcard value for a field, (30 - the other field result) is chosen. If both fields (length bit count and ID bit count) are set to the wildcard value by both peers, the result is 15 / 15.

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


#### Sizing Considerations

* Small length bit count: Increases the marginal costs of the header, causing data wastage.
* Large length bit count: Increases the buffer size required to support the maximum message length.
* Small ID bit count: Limits the maximum number of simultaneous outstanding operations.
* Large ID bit count: Requires more memory and complexity to keep track of outstanding messages.

It is recommended for peers that serve primarily "server" functionality to use wildcard values, which allows client peers - who are likely to have more varying network conditions - to control these values.


### Initiator Response

| Octet 0                      |
| ---------------------------- |
| `0` (reject) or `1` (accept) |

If a peer is happy with the resultant sizing, it responds with an "accept" (`1`).

It is possible that a peer may receive an initiator message that it cannot or will not accept. The message may contain invalid data, or the resultant sizing may be such that the peer is unwilling or unable to accommodate it. In such a case, it responds with a "reject" (`0`), which destroys the session.


### Initiator Message Flow

The initiator flow is gated on each side only to the request message, because it is needed in order to formulate a response. Once a peer has sent an initiator response, it is free to begin sending normal messages, even if it hasn't yet received the other peer's response. If it turns out that the other peer has rejected the initiator message, the session is dead anyway, and none of the sent messages will have been processed.

#### Successful Flow

* Peer A: initiator request
* Peer B: initiator request
* Peer B: initiator accept
* Peer B: message ID 0
* Peer A: initiator accept
* Peer A: reply ID 0

In this example, Peer A was a little slow to respond, and Peer B went ahead with messaging after responding to Peer A's initiator message. Since Peer A eventually accepted the initiator message, everything is OK, and message 1 gets processed.

#### Failure Flow

* Peer A: initiator message
* Peer B: initiator message
* Peer B: initiator accept
* Peer B: message ID 0
* Peer A: initiator reject

In this example, Peer A is once again slow to respond, and Peer B once again goes ahead with messaging, but it turns out that Peer A eventually rejects the initiator message. Message 0 never gets processed or responded to by Peer A because the session is dead. The peers must now disconnect.



Regular Messages
----------------

Regular messages consist of a 16 or 32 bit header, followed by a possible data payload. This protocol does not concern itself with the contents of the payload.

### Message Layout

| Section | Octets   |
| ------- | -------- |
| Header  | 2 or 4   |
| Payload | variable |


### Header Fields

The header fields contain information about what kind of message this is. Some fields have variable widths, which are determined for the entirety of the session by the [initiator message](#initiator-message). The length and ID fields are placed adjacent to each other, next to the reply and termination bits. The unused upper bits must be cleared to `0`.

| Field       | Bits      |
| ----------- | --------- |
| Unused      |  variable |
| Length      |  variable |
| ID          |  variable |
| Reply       |         1 |
| Termination |         1 |

For example, a 14/10 (length 14 bits, ID 10 bits) header would be conceptually viewed as:

    000000lllllllllllllliiiiiiiiiirt

The header is transmitted in little endian format:

    iiiiiirt lllliiii llllllll 000000ll


### Fields

#### Length

The length field refers to the number of octets in the payload portion of this chunk (i.e. the header portion does not count towards the length).

#### ID

The ID field is a unique number that is generated by the sender to differentiate the current message from other messages the sender has sent in the past. The sender must not use IDs that are currently in-flight (IDs that have been sent to the peer but have not yet received a reply, or have been canceled but not yet acked).

#### Reply

The reply bit is used to reply to a message sent by a peer. When set, the ID field refers to the ID of the original message sent by the peer, and the reply must use the same bit layout for length and ID as the other peer specified in its initiator message.

#### Termination

The termination bit indicates that this is the final chunk for this message ID. For a large message that spans multiple chunks, you would set this to 0 for all but the last chunk.



Special Messages
----------------

A length of 0 confers special meaning to the message, depending on the reply and termination fields:

| Length | Reply | Termination | Meaning     |
| ------ | ----- | ----------- | ----------- |
|    0   |   0   |       0     | Cancel      |
|    0   |   1   |       0     | Cancel Ack  |
|    0   |   0   |       1     | Ping        |
|    0   |   1   |       1     | Empty Reply |

### Cancel

The cancel message cancels an operation in progress. The ID field specifies the operation to cancel. After sending a cancel message, the ID is locked (cannot be used, and all replies to that ID are discarded) until a Cancel Ack message for that ID is received.

### Cancel Ack

Sent in response to a cancel request. The operation is canceled, and all queued replies to that ID are removed. Once this is done, the server sends a Cancel Ack. If the operation doesn't exist (possibly because it had already completed), the server must still send a Cancel Ack.

### Ping

A ping message requests an empty reply from the peer. Upon receiving a ping message, a peer must respond as quickly as possible, sending an empty reply with the same message ID as the very next message (regardless of any existing queued messages). This allows peers to gauge the latency between them, and also provides for a "keep-alive" mechanism.

### Empty Reply

Can be sent in response to any message, indicating successful completion but no other data to report.



Sending Messages
----------------

Messages are sent in chunks. A multi-chunk message has its `termination bit` cleared to 0 for all but the last chunk. Single chunk messages always have the `termination bit` set.

The message ID is scoped to the sender. If both sides of the channel send a message with the same ID, they are considered different messages, and don't interfere with each other. The `reply bit` inverts the scope: A peer replies to a message by using the same message ID as it received from the sender, and setting the reply bit.

### Flow

The message send and receive flow is as follows (example, message id 5):

* Sender sends message ID 5 (with the reply bit cleared) to the receiver.
* Receiver processes message ID 5
* Receiver sends response ID 5 (with the reply bit set) to the sender.

This works in both directions. Participants are peers, and can both initiate and respond to messages (acting as client and server simultaneously).

Responses may be sent in a different order than the requests were received.


### Multiplexing

Message chunks with the same ID must be sent in-order (chunk 5 must not be sent before chunk 4). The message is considered complete once the `termination bit` is set. Note that this does not require you to send all chunks for one message before sending chunks from another message. They can be multiplexed, like so:

* Message 10, chunk 0
* Message 11, chunk 0
* Message 12, chunk 0
* Message 11, chunk 1
* Message 10, chunk 1 (termination = 1)
* Message 11, chunk 2
* Message 12, chunk 1 (termination = 1)
* Message 11, chunk 3 (termination = 1)

Your choice of message chunk sizing and scheduling will depend on your use case.



Message Cancellation
--------------------

There are times when a sender might want to cancel an operation-in-progress. Circumstances may change, or the operation may be taking too long. A peer may cancel an outstanding operation by sending a cancel message, citing the message ID of the operation to be canceled.

Once a cancel order has been issued, the ID of the canceled message is locked. A locked message ID cannot be used, and all replies to that message ID are discarded. Once a Cancel Ack reply is received, the message ID is unlocked and may be used again.

#### Example:

* Sender sends message ID 19
* Sender times out
* Sender sends cancel ID 19
* Receiver sends reply ID 19, which is discarded by the sender
* Receiver sends cancel ack ID 19
* Sender receives cancel ack and unlocks ID 19

If a Cancel Ack is not received, it means that either there is a communication problem (such as lag), or the server is operating incorrectly.



Spurious Messages
-----------------

If a spurious (unexpected) message or reply is received, the peer should discard the message and ignore it. Spurious messages include:

* Reply to a canceled message.
* Cancel message for an operation not in progress.

These situations can arise when the transmission medium is lagged, or as the result of a race condition. A reply to a message may have already been en-route at the time of cancellation, or the operation may have completed before the cancel message arrived.

The following are most likely error conditions:

* Reply to a nonexistent message.
* Cancel Ack for a message that wasn't canceled.

In the error case, the peer may elect to report an error (outside of the scope of this protocol) or end the connection, depending on your use case.



Version History
---------------

 * April 10, 2019: Preview Version 1



License
-------

Copyright (c) 2019 Karl Stenerud. All rights reserved.

Distributed under the Creative Commons Attribution License: https://creativecommons.org/licenses/by/4.0/legalcode
License deed: https://creativecommons.org/licenses/by/4.0/
