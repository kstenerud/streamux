Streamux
========

A minimalist, asynchronous, multiplexing, request-response protocol.


### Features

* Minimal Overhead (header can be as small as 1 byte per message chunk)
* Multiplexing (multiple data streams can be sent across a single channel)
* Asynchronous (client is informed asynchronously upon completion or error)
* Interruptible (requests may be canceled)
* Floating roles (both peers can operate as client and server at the same time)
* Multiple negotiation modes


### Contents

* [Use Case](#use-case)
* [General Operation](#general-operation)
* [Negotiation Phase](#negotiation-phase)
    * [Negotiation Modes](#negotiation-modes)
        * [Simple](#simple)
        * [Yield](#yield)
        * [Handshake](#handshake)
    * [Hard and Soft Failures](#hard-and-soft-failures)
    * [Negotiation Message Layout](#negotiation-message-layout)
    * [Payload Fields](#payload-fields)
    * [Mandatory Payload Fields](#mandatory-payload-fields)
        * [Field `_n_mode`](#field-_n_mode)
        * [Field `_protocol`](#field-_protocol)
        * [Field `_id_cap`](#field-_id_cap)
        * [Field `_length_cap`](#field-_length_cap)
        * [ID and Length Cap Total Bit Count](#id_and_length_cap_total_bit_count)
    * [Optional Payload Fields](#optional-payload-fields)
        * [Field `_n_allowed`](#field-_n_allowed)
        * [Field `_fixed_length`](#field-_fixed_length)
        * [Field `_padding`](#field-_padding)
        * [Field `_negotiation`](#field-_negotiation)
    * [Negotiation Examples](#negotiation-examples)
* [Messaging Phase](#messaging-phase)
    * [Message Encoding](#message-encoding)
        * [Chunk Header](#chunk-header)
            * [Length](#length)
            * [Request ID](#request-id)
            * [Response](#response)
            * [Termination](#termination)
        * [Fixed Data Field](#fixed-data-field)
        * [Payload Field](#payload-field)
    * [Empty Response](#empty-response)
* [Out Of Band Messages](#out-of-band-messages)
* [Request Cancellation](#request-cancellation)
* [Spurious Messages](#spurious-messages)



Use Case
--------

Streamux is designed as a point-to-point, bidirectional protocol foundation for you to build a messaging layer on top of. It handles everything between the marshaler and the send/receive channel.

```
+-------------------------------------+
|             application             |
+-------------------------------------+
|           [Your Protocol]           |
|                                     |
|                rules                |
|           message routing           |
|             marshaling              |
|                 +-------------------|
|   additional    |
|   negotiation   |
+-----------------+
                   +------------------+ \
                   |    standard      |  |
                   |    negotiation   |  |
+------------------+                  |  |
|              [Streamux]             |  |==> Provided by Streamux
|                                     |  |
|          session management         |  |
|             packetization           |  |
|             multiplexing            |  |
+-------------------------------------+ /

+-------------------------------------+
| priority queues  |                  |
+------------------+ receive channel  |
| send channel     |                  |
+-------------------------------------+
|         reliable transport          |
+-------------------------------------+
|        communications medium        |
+-------------------------------------+
```



General Operation
-----------------

Upon establishing a connection, each peer sends negotiation message(s), and then begins normal communications. Communication is asynchronous after negotiation completes. A typical session might look something like this:

| Peer X        | Peer Y        |
| ------------- | ------------- |
| Begin Session | Begin Session |
|               | Negotiate     |
| Negotiate     |               |
| Request A     |               |
| Request B     | Response A    |
|               | Request C     |
| Response C    | Response B    |
| ...           | ...           |
| End Session   | End Session   |



Negotiation Phase
-----------------

Before a peer can send normal messages, it must first negotiate the session parameters.


### Negotiation Modes

There are three modes of negotiation supported. An implementation is not required to support all of them. Your protocol design will dictate which mode(s) work best for your needs.


#### Simple

With simple negotiation, both peers are equal in every way. Each peer sends only one negotiation message, containing their proposed parameters for the session. Both peers follow the exact same negotiation algorithm, so there is no need for any further messages to complete negotiation. After one pair of messages, both peers will already know if negotiation has succeeded or failed.

[Soft failures](#hard-and-soft-failures) in simple mode are automatically upgraded to hard failures.

| Time | Peer X                       | Dir  | Peer Y                                |
| ---- | ---------------------------- | ---- | ------------------------------------- |
| T1   | Negotiate: P: simple, A: ... | ===> |                                       |
|      |                              | <=== | Negotiate: P: passive, A: simple, ... |
| T2   | (negotiation complete)       |      | (negotiation complete)                |
|      | Messages                     | <==> | Messages                              |

At `T1`, each peer sends a negotiation message. Peer X proposes `simple`. Peer Y proposes `passive` and accepts at least `simple`. Note that this negotiation would also succeed if peer Y proposed `simple` mode (see [Proposed Negotiation Mode](#field-_n_mode))

At `T2`, each peer employs the "simple" negotiation algorithm and reaches the same conclusion (success or failure). There is therefore no need for any further negotiation messages.


#### Yield

Yield negotiation is similar to simple negotiation. Each peer still sends only one negotiation message, but in this case the "yielding" peer yields to the "proposing" peer, accepting their proposed parameters.

The advantage of yield negotiation is that the proposing peer doesn't need to wait for the yielding peer's negotiation message before sending regular messages, thus eliminating a negotiation delay.

[Soft failures](#hard-and-soft-failures) in yield mode are automatically upgraded to hard failures.

| Time | Peer "Initiator"            | Dir  | Peer "Yielding"                      |
| ---- | --------------------------- | ---- | ------------------------------------ |
| T1   | Negotiate: P: yield, A: ... | ===> |                                      |
|      | Request X                   | ===> |                                      |
|      |                             | <=== | Negotiate: P: passive, A: yield, ... |
| T2   | (negotiation complete)      |      | (negotiation complete)               |
|      |                             | <=== | Response X                           |
|      | Messages                    | <==> | Messages                             |

At `T1`, the initiator peer proposes `yield`, and its accept list is ignored. The yielding peer proposes `passive`, and accepts at least `yield`. The initiator peer doesn't wait, and sends a request immediately, before either side has had a chance to receive or evaluate the others negotiate message.

At `T2`, both peers have received each others negotiation message, and have run the identical negotiation algorithm to determine the result. If negotiation succeeds, the yielding peer will process the propsing peer's request as normal. If negotiation fails, the request will neither be acted upon nor responded to because this is a [hard failure](#hard-and-soft-failures)


#### Handshake

With handshake negotiation, each peer sends negotiation messages in turn until a consensus is reached for all negotiation parameters, after which normal messaging may begin.

The contents of these negotiation messages depends on the needs of your protocol. The only requirement is that [certain information be present in the first negotiation message](#mandatory-payload-fields).

Handshake negotiation is most useful when incorporating encryption into your protocol.

| Time | Peer "Initiator"                | Dir  | Peer "Accepting"                         |
| ---- | ------------------------------- | ---- | ---------------------------------------- |
| T1   | Negotiate: P: handshake, A: ... | ===> |                                          |
|      |                                 | <=== | Negotiate: P: passive, A: handshake, ... |
| T2   |                                 | <=== | Negotiate: Handshake params              |
| T3   | Negotiate: Handshake params     | ===> |                                          |
| T4   |                                 | <=== | Negotiate: Handshake params              |
| ...  | ...                             |      | ...                                      |
| Tn-1 | Negotiate: Handshake params     | ===> |                                          |
| Tn   |                                 | <=== | Negotiate: Is complete                   |
| Tn+1 | (negotiation complete)          |      | (negotiation complete)                   |
|      | Messages                        | <==> | Messages                                 |

At `T1`, the initiator peer proposes `handshake`, and its accept list is ignored. It also includes all information necessary for the first handshake negotiation. The accepting peer sends only the most basic negotiation message, proposing `passive`, and accepting at least `handshake`.

At `T2`, the peers have successfully negotiated to use `handshake` mode. The accepting peer sends a "corrected" first negotiation message, containing a response to the initiator peer's handshake parameters.

At `T3`, the initiator peer evaluates the accepting peer's handshake message, and responds with another handshake message. This continues back and forth until one peer notifies the other that negotiation is complete (at `Tn`).


### Hard and Soft Failures

Some negotiation failures aren't necessarily terminal. Depending on your protocol design, certain kinds of failures can be recovered from.

A soft failure means that a particular negotiation attempt failed, but, if the negotiation mode or protocol allows it, the peers may make another attempt, or choose an alternative.

A hard failure is considered unrecovarable, regardless of the negotiation mode. After a hard failure, the session is dead, and the peers should disconnect.

Note: In simple and yield modes, all failures are hard failures.


### Negotiation Message Layout

Negotiation consists of the exchange of negotiation messages between peers. A negotiation message begins with an 8-byte identifier, followed by negotiation-specific data:

| Field          | Type         | Octets | Value   |
| -------------- | ------------ | ------ | ------- |
| Initiator      | bytes        |    2   | "pN"    |
| Identifier     | bytes        |    5   | "STRMX" |
| Version        | unsigned int |    1   | 1       |
| Fixed Data     | bytes        |    *   | *       |
| Payload Length | unsigned int |    2   | 0-65535 |
| Payload        | bytes        |    *   | *       |
| Padding        | bytes        |    *   | *       |

The protocol negotiation initiator value "pN" was chosen because it is unlikely to occur naturally in text, and is unlikely to trigger commands when overlaid on another protocol.

The 5-byte identifier "STRMX" identifies this as a Streamux negotiation message.

The Streamux version is currently 1.

The initiator, identifier, and version must match exactly between peers, otherwise it is a [hard failure](#hard-and-soft-failures).

The payload is encoded using [Concise Binary Encoding, version 1](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md), as an inlined map (containing only the key-value pairs, not the "begin map" or "end container" markers). Its contents may be encrypted at some point during the negotiation phase if encryption is used.

Fixed data has length 0 until otherwise negotiated. Once the [fixed data length](#field-_fixed_length) has been successfully negotiated to a value greater than 0, all future negotiation messages will contain a fixed data portion of the selected length.

Padding is 0 until otherwise negotiated. Once [padding](#field-_padding) has been successfully negotiated to a value greater than 0, all future negotiation messages will be padded to the next multiple of this value.


### Payload Fields

The payload section is an inlined map, containing key-value pairs. Keys and values may be anything supported by [Concise Binary Encoding, version 1](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md).

All string fields beginning with an underscore `_` are reserved for use by Streamux.


### Mandatory Payload Fields

The initial negotiation message must contain at least the following fields. For handshake mode, further negotiation messages may be sent according to your handshake protocol design, and do not need to contain these fields.

| Field         | Type   | Description                    |
| ------------- | ------ | ------------------------------ |
| `_n_mode`     | string | Negotiation mode               |
| `_protocol`   | map    | Overlaid protocol              |
| `_id_cap`     | map    | Maximum allowable ID value     |
| `_length_cap` | map    | Maximum allowable length value |


#### Field `_n_mode`

The proposed negotiation mode must be one of:
- `passive`
- `simple`
- `yield`
- `handshake`

When a peer proposes `passive`, it is indicating that it will accept whatever negotiation mode the other peer proposes, provided that the proposed mode is in this peer's [allowed negotiation modes](#field-_n_allowed).

Rules:

* When a peer proposes a negotiation mode other than `passive`, its own [allowed negotiation modes](#field-_n_allowed) list is ignored.
* Only one peer may propose a non-passive mode. If both propose a non-passive mode, it is a [hard failure](#hard-and-soft-failures).
* With the exception of `passive`, if the proposed mode doesn't exist in the other peer's [allowed negotiation modes](#field-_n_allowed), it is a [hard failure](#hard-and-soft-failures).
* If both peers propose `passive` mode, then negotiation defaults to `simple`, provided simple mode is in both peers' [allowed negotiation modes](#field-_n_allowed). Otherwise it is a [hard failure](#hard-and-soft-failures).
* As a special case, if both peers propose `simple`, then simple mode is automatically and successfully chosen, disregarding all other rules.


#### Field `_protocol`

The Protocol field contains the identifier and version of the proposed protocol to be overlaid on top of Streamux:

| Field | Type   |
| ----- | ------ |
| `id`  | string |
| `ver` | string |

`id` (Identifier) uniquely identifies the protocol that will be used on top of this protocol.

`ver` (Version) is a string following [Semantic Versioning 2.0.0](https://semver.org/). `MAJOR` version changes indicate a backwards incompatible change, and so only the `MAJOR` portion is considered when deciding compatibility between peers. `MINOR` and `PATCH` information are only used for diagnostic and debugging purposes.

ID and the `MAJOR` portion of the version must match exactly between peers, otherwise it is a [hard failure](#hard-and-soft-failures).


#### Field `_id_cap`

The ID Cap field negotiates the maximum allowed ID value in messages during this session.

| Field      | Type        | Valid Range   |
| ---------- | ----------- | ------------- |
| `min`      | signed int  | 0 - 32767     |
| `max`      | signed int  | 0 - 536870911 |
| `proposed` | signed int  | 0 - 536870911 |

The `min`, `max`, and `proposed` fields from the two peers ("us" and "them") are used to generate a negotiated value. The algorithm is as follows:

##### Min, max:

    min = maximum(us.min, them.min)
    max = minimum(us.max, them.max)
    if max < min: fail

##### Proposed:

    proposed = minimum(us.proposed, them.proposed)

##### Wildcards:

Peers may also use the "wildcard" value `-1` in the `proposed` field, meaning that they will defer to the other peer's proposed value. This changes how `proposed` is calculated:

    if us.proposed = wildcard: proposed = them.proposed
    if them.proposed = wildcard: proposed = us.proposed
    if both us and them use wildcard: proposed = (max-min)/2 + min, rounding up

##### Negotiated Value:

    negotiated value = minimum(maximum(proposed, min), max)


#### Field `_length_cap`

The Length Cap field negotiates the maximum allowed length of message payloads during this session.

| Field      | Type       | Valid Range    |
| ---------- | ---------- | -------------- |
| `min`      | signed int | 1 - 32767      |
| `max`      | signed int | 1 - 1073741823 |
| `proposed` | signed int | 1 - 1073741823 |

Length Cap follows the same negotiation algorithm as [ID cap](#field-_id_cap).


#### ID and Length Cap Total Bit Count

The larger the ID and length caps, the more bits are required to represent those value ranges in [message chunk headers](#chunk-header), and the larger the header will be during the current session.

The total of (id bits + length bits) must not exceed 30. If they do, they must be brought down to 30 using the following algorithm:

    if id_cap.bitcount + length_cap.bitcount > 30:
        if both are > 15: reduce both to 15
        else: (larger value) = 30 - (smaller value)

If a cap field's bit count is reduced, its new value will be the maximum value encodable in that number of bits.

Note: If the final negotiated cap values are not within the min and max values of BOTH peers, negotiation fails.

##### Considerations

The choice of ID and length ranges will affect the characteristics of the session. Depending on your use case and operating environment, certain aspects will be more important than others:

* Small ID range: Limits the maximum number of simultaneous outstanding requests.
* Large ID range: Requires more memory and complexity to keep track of outstanding requests.
* Small length range: Increases the marginal costs of the header, losing useful bandwidth to overhead.
* Large length range: Increases the buffer size required to support the maximum message chunk length.

Note: An ID bit count of 0 means that there can be only one message in-flight at a time (all messages are implicitly ID 0).



### Optional Payload Fields

There are other points of negotiation that are not absolutely required for successful communication, but may become necessary depending on the negotiation mode and the protocol you overlay:

| Field           | Type    |
| --------------- | ------- |
| `_n_allowed`    | list    |
| `_fixed_length` | map     |
| `_padding`      | map     |
| `_negotiation`  | boolean |


#### Field `_n_allowed`

This field is a whitelist of negotiation modes that this peer supports. It may include any combination of:
- `simple`
- `yield`
- `handshake`

If this field is not present, [`simple`] is assumed.

Negotiation can only succeed if the other peer's [`_n_mode` field](#field-_n_mode) (other than `passive`) is present in this peer's `_n_allowed` list. If not, it is a [hard failure](#hard-and-soft-failures).


#### Field `_fixed_length`

This field negotiates the length of the fixed-length portion in messages. It's primarily of use in supporting encryption channel or authentication data, for example a [message authentication code](https://en.wikipedia.org/wiki/Message_authentication_code).

| Field      | Type         | Valid Range |
| ---------- | ------------ | ----------- |
| `max`      | unsigned int | any         |
| `proposed` | unsigned int | any         |

`max` defines the maximum fixed length this peer will agree to.

`proposed` defines the length that this peer proposes to use.

The negotiation process chooses the higher of the two peer's `proposed` values. If the chosen value is greater than either of the peer's `max` values, this is a [soft failure](#hard-and-soft-failures).

* If this field is not present, `0` is assumed for both values.
* If `max` is not present, it is assumed to be equal to `proposed`.


#### Field `_padding`

This field negotiates how much padding will be applied to message payloads. It's primarily of use in supporting [block ciphers](https://en.wikipedia.org/wiki/Block_cipher).


| Field      | Type         | Valid Range |
| ---------- | ------------ | ----------- |
| `max`      | unsigned int | any         |
| `proposed` | unsigned int | any         |

`max` defines the maximum padding amount this peer will agree to.

`proposed` defines the padding that this peer proposes to use.

The negotiation process chooses the higher of the two peer's `proposed` values. If the chosen value is greater than either of the peer's `max` values, this is a [soft failure](#hard-and-soft-failures).

* If this field is not present, `0` is assumed for both values.
* If `max` is not present, then it is assumed to be equal to `proposed`.


#### Field `_negotiation`

This field signals to the other peer that negotiation is complete. A value of `true` indicates success, and a value of `false` indicates a [hard failure](#hard-and-soft-failures).

This field may only be used at the conclusion of a `handshake` negotiation.



### Negotiation Examples

#### Simple: All parameters are compatible

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |    6   |   12   |     8   |     100    |  1000000   |    100000   |
| Peer B |    -     | Simple  |    6   |   15   |     7   |      50    |   300000   |    300000   |
| Result |    -     |    -    |    6   |   12   |     7   |     100    |   300000   |    100000   |

Negotiation: **Success**

#### Simple: Min ID > Max ID

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |    6   |    8   |     8   |    1000    |    2000    |     2000    |
| Peer B |    -     | Simple  |   10   |   15   |    10   |    1000    |   30000    |    30000    |
| Result |    -     |    -    |   10   |    8   |     8   |    1000    |    2000    |     2000    |

The negotiated `ID Min` (10) is greater than the negotiated `ID Max` (8).

Negotiation: **Fail**

#### Simple: Wildcard, initial negotiated bits total > 30

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |    6   |   16   |    14   |      50    |  1000000   |         0   |
| Peer B |    -     | Simple  |    6   |   18   |    15   |   40001    |  1000000   |         0   |
| Result |    -     |    -    |    6   |   16   |    14   |   40001    |  1000000   |     65535   |

In this case, both peer A and B used wildcard values for proposed length cap, so we use the formula:

`length = (1000000-40001)/2 + 40001 = 520000.5`, rounded up to `520001`

The resulting bit counts (id 14 bits, length 19 bits) are greater than 30, so the larger value is reduced:

`length bits = 30 - 14 = 16 bits`
`length = 2^16-1 = 65535`

Negotiation: **Success**

#### Simple: All proposed values use wildcard

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Rec |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ---------- |
| Peer A |  Simple  |    -    |    6   |   16   |    31   |      50    |  1000000   |        0   |
| Peer B |    -     | Simple  |    6   |   18   |    31   |     250    |   200000   |        0   |
| Result |    -     |    -    |    6   |   16   |    11   |     250    |   200000   |   100125   |

`ID = (16-6)/2 + 6 = 11`
`Length = (200000-250)/2 + 250 = 100125`

Negotiation: **Success**

#### Yield: Everything is compatible

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Yield   |    -    |    8   |   15   |     8   |     1000   |   200000   |     8000    |
| Peer B |    -     |  Yield  |    6   |   18   |    10   |      200   |    30000   |     1000    |
| Result |    -     |    -    |    8   |   15   |     8   |      200   |    30000   |     8000    |

Since we are using quick init, Peer A's proposed values are chosen, and Peer B's proposed values are ignored.

Peer A's proposed values within the constraints of both peers.

Negotiation: **Success**

#### Yield: Peer A's values are not within range of Peer B's constraints

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Yield   |    -    |    8   |   15   |     8   |     1000   |   200000   |    60000    |
| Peer B |    -     |  Yield  |    6   |   18   |    10   |      200   |    30000   |     1000    |
| Result |    -     |    -    |    8   |   15   |     8   |     1000   |    30000   |     fail    |

Peer A's proposed length cap (60000) is higher than peer B's length max (30000).

Negotiation: **Fail**



Messaging Phase
---------------

Messages are sent in chunks. A multi-chunk message has its `termination` field cleared to `0` for all but the last chunk. Single chunk messages always have the `termination` field set.

The request ID is scoped to its sender. If both peers send a request with the same ID, they are considered to be distinct, and don't conflict with each other. A response message inverts the scope: A peer responds to a request by using the same request ID as it received from the requesting peer in its response message.

Message chunks with the same ID must be sent in-order (chunk 5 of message 42 must not be sent before chunk 4 of message 42). The message is considered complete once the `termination` field is set. Note that this does not require you to send all chunks for one message before sending chunks from another message. Chunks from different messages can be sent interleaved, like so:

* Message 10, chunk 0
* Message 11, chunk 0
* Message 12, chunk 0
* Message 11, chunk 1
* Message 10, chunk 1 (termination = true)
* Message 11, chunk 2
* Message 12, chunk 1 (termination = true)
* Message 11, chunk 3 (termination = true)

Your choice of message chunk sizing and scheduling will depend on your use case.



### Message Encoding

| Field          | Type         | Size                                |
| -------------- | ------------ | ----------------------------------- |
| Chunk Header   | unsigned int | 1-4 (determined during negotiation) |
| Fixed Data     | bytes        | determined during negotiation       |
| Payload        | bytes        | variable                            |
| Padding        | bytes        | determined during negotiation       |


#### Chunk Header

The message chunk header is treated as a single (8, 16, 24, or 32 bit) unsigned integer composed of bit fields, and is transmitted in little endian byte order.

The header fields contain information about what kind of message this is. The request ID and length fields are placed adjacent to each other, next to the response and termination bits. Any unused upper bits must be cleared to `0`.

| Field       | Bits      | Order     |
| ----------- | --------- | --------- |
| Unused      |  variable | High Bit  |
| Request ID  |  variable |           |
| Length      |  variable |           |
| Response    |         1 |           |
| Termination |         1 | Low Bit   |

The header size is determined by the bit widths of the [ID cap](#field-_id_cap) and [length Cap](#field-_length_cap) values decided in the negotiation phase.

Bit size of ID cap + bit size of length cap:

| Min Bit Size | Max Bit Size | Header Size |
| ------------ | ------------ | ----------- |
|       1      |       6      |      8      |
|       7      |      14      |     16      |
|      15      |      22      |     24      |
|      23      |      30      |     32      |

For example, a 10/14 header (10-bit ID, 14-bit length, which would result in a 32-bit header with 6 bits unused) would be conceptually viewed as:

    000000iiiiiiiiiillllllllllllllrt

A 5/9 header (5-bit ID, 9-bit length, which would result in a 16-bit header) would be conceptually viewed as:

    iiiiilllllllllrt

A 0/6 header (0-bit ID, 6-bit length, which would result in an 8-bit header) would be conceptually viewed as:

    llllllrt


##### Length

The length field refers to the number of useful octets in the payload portion of this chunk (the message chunk header itself does not count towards the length). The actual size of the payload section will be a multiple of the payload size if it was negotiated to a value other than 0 during the [negotiation phase](#negotiation-phase).

##### Request ID

The request ID field is a unique number that is generated by the requesting peer to differentiate the current request from other requests that are still in-flight (IDs that have been sent to the peer but have not yet received a response, or have been canceled but not yet cancel acked). The requesting peer must not re-use IDs that are currently in-flight.

Request IDs are scoped to the requesting peer. For example, request ID 22 from peer A is distinct from request ID 22 from peer B.

Note: The first chosen request ID in the session should be unpredictable in order to make known-plaintext attacks more difficult in cases where this protocol is overlaid on an encrypted transport (see [RFC 1750](https://tools.ietf.org/html/rfc1750)).

##### Response

The response bit is used to respond to a request sent by a peer. When set, the ID field refers to the ID of the original request sent by the requesting peer (scope is inverted).

##### Termination

The termination bit indicates that this is the final chunk for this request ID (as a request message or as a response message). For a large message that spans multiple chunks, you would clear this to 0 for all but the last chunk.


#### Fixed Data Field

The contents of the fixed data portion of the message are completely free-form, with a fixed data length decided during the [negotiation phase](#negotiation-phase).


#### Payload Field

The payload portion of the message contains the actual message data to pass to the next layer up. Its contents are application-specific, and its size is determined by the size field, as well as the padding amount determined during the [negotiation phase](#negotiation-phase). The length field denotes the number of useful octets in the payload field, and the actual size of the payload section will be a multiple of the padding amount (if padding amount is greater than 0).


### Empty Response

An `empty response` is a response message (with the response bit set to `1`) containing no data. `empty response` signals successful completion of the request, with no other data to report.



Out of Band Messages
--------------------

Out of band (OOB) messages are used for management of the protocol and session itself rather than for communication with the application (although they may affect the application's behavior). Normally, OOB messages are sent at a higher priority than all other messages.


### OOB Message Encoding

An OOB message looks almost identical to a regular message chunk, except that it will have a length of 0 and a termination bit of 0. Because the length field is 0, a secondary 16-bit payload length field is appended to the message chunk header, giving a maximum OOB payload size of 65535. The rest of the message looks and behaves the same as a regular message.

| Field          | Type         | Size                                |
| -------------- | ------------ | ----------------------------------- |
| Chunk Header   | unsigned int | 1-4 (determined during negotiation) |
| Fixed Data     | bytes        | determined during negotiation       |
| Payload Length | unsigned int | 2                                   |
| Payload        | bytes        | variable                            |
| Padding        | bytes        | determined during negotiation       |

TODO

The payload is encoded in CBE v1, as an inlined map (containing only the key-value pairs, not the "begin map" or "end container" markers). The map must always contain the type of the OOB message for OOB requests (not present in OOB responses), keyed to the empty string ("").

OOB messages may contain extra "filler" data (stored under the key "_") to aid in thwarting traffic analysis. Filler data is decoded and discarded by the receiver. Coordination of when and how to use filler data is beyond the scope of this document.

An empty OOB response signals successful completion, with no other data to report.

OOB Messages:
- `ping`: Requests a ping reply from the peer. The reply must be sent as soon as possible to aid in latency calculations.
- `alert`: Informs the other peer of an important event (warnings, errors, etc)
- `disconnect`: Informs the other peer that we are disconnecting from the session.
- `stop`: Requests that the other peer stop sending normal messages (OOB messages may still be sent).
- `start`: Informs the other peer that it may resume sending normal messages again.

TODO: Other possible types:
- renegotiate
- restart session
- save session => id?
- rejoin session [id]?
- list oob msg types?



### Special OOB Message: Cancel

A cancel (and cancel ack) message is an OOB message that is encoded in a special way. While OOB messages normally allocate a new message ID, cancel re-uses the ID of the message it intends to cancel. This ensures that cancel messages can still be sent even during ID exhaustion (where all available message IDs are in flight).

A cancel message can be identified by its payload length of 0. It won't have a message type encoded in the payload section.



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
