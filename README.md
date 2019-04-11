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
* Peers send initiator message replies, and begin normal messaging
* Close connection



Initiator message
-----------------

Before sending any other messages, each peer must send an initiator message. A peer may not send any more messages until it has responded to the *other* peer's initiator message.


### Message Layout

| Octet 1 | Octet 2 |
| ------- | ------- |
| Version | Sizing  |

#### Version Field

Currently 1

#### Sizing Field

The `sizing` field determines how many bits will be used for the `length` and `id` message header fields. `sizing` is structured as follows:

| Bits 7-6    | Bit 5                 | Bits 4-0               |
| ----------- | --------------------- | ---------------------- |
| Cleared (0) | 32-bit Message Header | Length Field Bit Count |

The `32-bit message header` bit determines if message headers in this session will be 16 or 32 bits wide:

| Value | Meaning               |
| ----- | --------------------- |
|   0   | 16 bit message header |
|   1   | 32 bit message header |

The `length field bit count` field determines how many bits of the message header will be used to denote the `length` field. The remaining bits will be used to denote the `id` field. Both the length and ID field must be at least 1 bit wide, which means that valid values are from 1 to (max-1). For 16 bit headers, there are 14 bits available for sizing, giving a valid range of 1-13. For 32-bit headers, there are 30 bits available for sizing, giving a valid range of 1-29.

Each peer may specify its own sizing. The other peer must adhere to these constraints when receiving and replying to messages, but may specify its own (likely different) parameters for messages it sends.


### Size Mirroring

A peer may signal that it will mirror the other peer's sizing by specifying `0` for the `length field bit count`. When one peer mirrors, both will use the settings of the peer that didn't mirror.

It is an error if both peers attempt to mirror. If a peer that has sent a sizing of 0 also receives a sizing of 0, it must [reject the message](#errors).

Only use mirroring if you've established an unambiguous convention for which peer shall mirror. For example, the peer that mostly takes on a "server" role is usually best suited to be the one to mirror.


### Sizing Considerations

A smaller length field bit count increases the marginal costs of the header. A smaller ID bit count limits the maximum number of simultaneous outstanding operations. A larger length field bit count increases the maximum sized buffer, which will require larger memory allocations to support sending and receiving messages. A larger ID bit count may require more memory or complexity to keep track of outstanding messages, depending on the implementation. Optimal settings will depend on your use case.


### Errors

It is possible that a peer may receive an initiator message that it cannot or will not accept. The message may contain invalid data, or may specify a sizing that the peer is unwilling or unable to accommodate (a length bits size allowing 500mb message chunks, for example).

To reject the initiator message, a peer sends a [cancel](#cancel) message with ID `0`. When an initiator message is rejected, the session is considered "dead" with no chance of recovery, and the connection should be closed.


### Example:

    0x01 0x0a

* Version: 1
* Message header size: 16 bits
* Usable bits for sizing: 14 (16 - 2)
* Length field bit count: 10
* ID field bit count: 4 (14 - 10)


### Flow

The initiator message is implicitly assigned the message ID `0`. This message ID will be considered "in flight" until a response is received: either an [empty reply](#empty-reply) for success, or a [cancel message](#cancel) for failure.

Once a peer has sent its own initiator message response, it may begin sending other messages without waiting for the other side to send an initiator message response. If there was an error, the session is dead anyway, as will be signified once the failure response is received.

#### Successful Flow

* Peer A: initiator message
* Peer B: initiator message
* Peer B: empty reply ID 0
* Peer B: message ID 1
* Peer A: empty reply ID 0
* Peer A: reply ID 1

In this example, Peer A was a little slow to respond, and Peer B went ahead with messaging after responding to Peer A's initiator message. Since Peer A was eventually happy with the initiator message, everything is OK, and message 1 gets processed.

#### Failure Flow

* Peer A: initiator message
* Peer B: initiator message
* Peer B: empty reply ID 0
* Peer B: message ID 1
* Peer A: cancel ID 0

In this example, Peer A is once again slow to respond, and Peer B once again goes ahead with messaging, but it turns out that Peer A eventually rejects the initiator message. Message 1 never gets processed or responded to by Peer A because the session is dead. The peers must now disconnect.



Regular Messages
----------------

Regular messages consist of a standard 16 or 32 bit header, followed by a possible data payload. This protocol does not concern itself with the contents of the payload.

### Message Layout

| Section | Octets   |
| ------- | -------- |
| Header  | 2 or 4   |
| Payload | variable |


### Header Fields

The header fields contain information about what kind of message this is. Some fields have variable widths, which are determined for the entirety of the session by the [initiator message](#initiator-message).

| Field       | Bits      |
| ----------- | --------- |
| Termination |         1 |
| Reply       |         1 |
| Length      |  variable |
| ID          |  variable |

#### Bit Layout

The header is sent over the wire as a little endian 16 or 32 bit unsigned integer.

For example, a 17/13 header (where [sizing-field](#sizing-field) was 17 in the [initiator message](#initiator-message)) would be conceptually viewed as:

    trllllllllllllllllliiiiiiiiiiiii

The header would actually be transmitted in little endian format:

    iiiiiiii llliiiii llllllll trllllll


### Fields

#### Termination

The termination bit indicates that this is the final chunk for this message ID. For a large message that spans multiple chunks, you would set this to 0 for all but the last chunk.

#### Reply

The reply bit is used to reply to a message sent by a peer. When set, the ID field refers to the ID of the original message sent by the peer, and the reply must use the same bit layout for length and ID as the other peer specified in its initiator message.

#### Length

The length field refers to the number of octets in the data portion of this chunk (i.e. the header portion does not count towards the length).

#### ID

The ID field is a unique number that is generated by the sender to differentiate the current message from other messages the sender has sent in the past. The sender must not use IDs that are currently in-flight (IDs that have been sent to the peer but have not yet received a reply, or have been canceled but not yet acked).



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

Sent in response to a cancel request. All queued replies to that ID are removed, and the operation is canceled. Once the cancel is completed, the server sends a Cancel Ack. If the operation doesn't exist (possibly because it had already completed), the server still sends a Cancel Ack.

### Ping

A ping message requests an empty reply from the peer. Upon receiving a ping message, a peer must respond as quickly as possible, sending an empty reply with the same message ID as the very next message (regardless of any existing queued messages). This allows peers to gauge the latency between them, and also provides for a "keep-alive" mechanism.

### Empty Reply

Can be sent in response to a message, indicating successful completion, but no other data to report.



Sending Messages
----------------

Messages are sent in chunks. A multi-chunk message has its `termination bit` cleared to 0 for all but the last chunk. Single chunk messages always have the `termination bit` set.

The message ID is scoped to the sender. If both sides of the channel send a message with the same ID, they are considered different messages, and don't interfere with each other. The `reply bit` inverts the scope: A peer replies to a message by using the same message ID as it received from the sender, and setting the reply bit.

### Flow

The message send and receive flow is as follows:

* Sender creates message ID 5 (with the reply bit cleared).
* Sender sends message ID 5 to the receiver.
* Receiver processes message ID 5
* Receiver creates response to message ID 5 (with the reply bit set).
* Receiver sends response ID 5 to the sender.

This works in both directions. Participants are peers, and can both initiate and respond to messages (acting as client and server simultaneously).

Responses may be sent in a different order than the requests were received.


### Multiplexing

Message chunks with the same ID must be sent in-order (chunk 5 must not be sent before chunk 4). The message is considered complete once the `termination bit` is set. Note that this does not require you to send all chunks for one message before sending chunks from another message. They can be interspersed, like so:

* Message 10, chunk 0
* Message 11, chunk 0
* Message 12, chunk 0
* Message 11, chunk 1
* Message 10, chunk 1 (termination)
* Message 11, chunk 2
* Message 12, chunk 1 (termination)
* Message 11, chunk 3 (termination)

How message chunks are sized and scheduled depends on your use case.



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

If a Cancel Ack is not received, it means that the server is either lagged, or is operating incorrectly.



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
