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
* [Messages](#messages)
* [Negotiation Phase](#negotiation-phase)
    * [Negotiation Modes](#negotiation-modes)
        * [Simple](#simple)
        * [Yield](#yield)
        * [Handshake](#handshake)
    * [Hard and Soft Failures](#hard-and-soft-failures)
    * [Negotiation Message Layout](#negotiation-message-layout)
    * [Payload Fields](#payload-fields)
    * [Mandatory Payload Fields](#mandatory-payload-fields)
        * [Field `_mode`](#field-_mode)
        * [Field `_protocol`](#field-_protocol)
        * [Field `_id_cap`](#field-_id_cap)
        * [Field `_length_cap`](#field-_length_cap)
        * [ID and Length Cap Total Bit Count](#id_and_length_cap_total_bit_count)
    * [Optional Payload Fields](#optional-payload-fields)
        * [Field `_allowed_modes`](#field-_allowed_modes)
        * [Field `_fixed_length`](#field-_fixed_length)
        * [Field `_padding`](#field-_padding)
        * [Field `_negotiation`](#field-_negotiation)
        * [Field `_`](#field-_)
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
    * [OOB Message Encoding](#oob-message-encoding)
        * [Field `_oob`](#field-_oob)
        * [Field `_`](#field-_)
    * [OOB Message Types](#oob-message-types)
        * [Ping Message](#ping-message)
        * [Alert Message](#alert-message)
            * [Field `_message`](#field-_message)
            * [Field `_severity`](#field-_severity)
        * [Disconnect Message](#disconnect-message)
        * [Stop Message](#stop-message)
        * [Start Message](#start-message)
        * [Cancel Message](#cancel-message)
* [Request Cancellation](#request-cancellation)
* [Spurious Messages](#spurious-messages)



Use Case
--------

Streamux is designed as a point-to-point, bidirectional protocol foundation for you to build a messaging layer on top of. It handles the middle parts which tend to operate in a similar way in most stream oriented protocols. Think of it as a protocol construction kit.

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
|              [Streamux]             |  |
|                                     |  |==> Provided by Streamux
|          session management         |  |
|    request tracking & cancellation  |  |
|             packetization           |  |
|          encryption support         |  |
|             multiplexing            |  |
+-------------------------------------+ /

+-------------------------------------+
|          ciphers (optional)         |
+-------------------------------------+
| priority queues  |                  |
+------------------+ receive channel  |
| send channel     |                  |
+-------------------------------------+
|         reliable transport          |
+-------------------------------------+
```



General Operation
-----------------

There are two main phases to a Streamux session: the negotiation phase and the messaging phase. During the negotiation phase, session parameters are agreed upon. Once this phase completes, the normal messaging phase begins.

Upon establishing a connection, each peer sends negotiation message(s), and then begins normal communications. Communication is asynchronous after negotiation completes. A typical session might look something like this:

| Peer X        | Peer Y        |
| ------------- | ------------- |
|               | Identifier    |
|               | Negotiate     |
| Identifier    |               |
| Negotiate     |               |
| Request A     |               |
| Request B     | Response A    |
|               | Request C     |
| Response C    | Response B    |
| ...           | ...           |
| Disconnect    |               |
| (end session) | (end session) |



Messages
--------

Streamux is message oriented, and has four main kinds of messages:

### Identifier Message

Each peer sends this message once and only once as their first message in order to identify the negotiation protocol, followed immediately by the first negotiation message.

### Negotiation Messages

These messages are exchanged only during the negotiation phase of the session. Once the session parameters have been successfully negotiated, negotiation messages are no longer sent.

Note: Triggering a new negotiation phase after normal messaging has begun is not expressly forbidden, as some protocols may require this. Details of such an operation would be protocol-specific, although this would best be done via OOB messages.

### Normal Messages

These messages are sent over the course of the session after negotiation has completed. Normal messages contain your application data.

### Out of Band Messages

These messages are also sent over the course of the normal messaging phase of the session, but are restricted to session management signaling. Error handling, flow control, operation cancellation, disconnects and such are handled via OOB messages.



Negotiation Phase
-----------------

Before a peer can send normal messages, it must first negotiate the session parameters.


### Negotiation Modes

There are three modes of negotiation supported. An implementation is not required to support all of them. Your protocol design will dictate which mode(s) work best for your needs.


#### Simple

With simple negotiation, both peers are equal in every way. Each peer sends only one negotiation message, containing their proposed parameters for the session. Both peers follow the exact same negotiation algorithm, so there is no need for any further messages in order to complete the negotiation.

[Soft failures](#hard-and-soft-failures) in simple mode are automatically upgraded to hard failures.

| Time | Peer X                       | Dir  | Peer Y                                |
| ---- | ---------------------------- | ---- | ------------------------------------- |
| T1   | Identifier                   | ===> |                                       |
|      | Negotiate: P: simple, A: ... | ===> |                                       |
|      |                              | <=== | Identifier                            |
|      |                              | <=== | Negotiate: P: passive, A: simple, ... |
| T2   | (negotiation complete)       |      | (negotiation complete)                |
|      | Messages                     | <==> | Messages                              |

At `T1`, each peer sends an identifier and negotiation message. Peer X proposes `simple`. Peer Y proposes `passive` and accepts at least `simple`. Note that simple mode negotiation would also succeed if peer Y had proposed `simple` mode (see [Proposed Negotiation Mode](#field-_mode))

At `T2`, each peer employs the "simple" negotiation algorithm and reaches the same conclusion (success or failure). There is therefore no need for any further negotiation messages.


#### Yield

Yield negotiation is similar to simple negotiation. Each peer still sends only one negotiation message, but in this case the "yielding" peer yields to the "proposing" peer, accepting their proposed parameters.

The advantage of yield negotiation is that the proposing peer doesn't need to wait for the yielding peer's negotiation message before sending regular messages, thus eliminating a negotiation delay.

[Soft failures](#hard-and-soft-failures) in yield mode are automatically upgraded to hard failures.

| Time | Peer "Initiator"            | Dir  | Peer "Yielding"                      |
| ---- | --------------------------- | ---- | ------------------------------------ |
| T1   | Identifier                  | ===> |                                      |
|      | Negotiate: P: yield, A: ... | ===> |                                      |
|      | Request X                   | ===> |                                      |
|      |                             | <=== | Identifier                           |
|      |                             | <=== | Negotiate: P: passive, A: yield, ... |
| T2   | (negotiation complete)      |      | (negotiation complete)               |
|      |                             | <=== | Response X                           |
|      | Messages                    | <==> | Messages                             |

At `T1`, the initiator peer proposes `yield`, and its accept list is ignored. The yielding peer proposes `passive`, and accepts at least `yield`. The initiator peer doesn't wait, and sends a request immediately, before either peer has had a chance to receive or evaluate the other's negotiate message.

At `T2`, both peers have received each other's negotiation message, and have run the identical negotiation algorithm to determine the result. If negotiation succeeds, the yielding peer will process the propsing peer's already sent request as normal. If negotiation fails, the request will neither be acted upon nor responded to because this is a [hard failure](#hard-and-soft-failures)


#### Handshake

With handshake negotiation, each peer sends negotiation messages in turn until a consensus is reached for all negotiation parameters, after which normal messaging may begin.

The contents of these negotiation messages depends on the needs of your protocol. The only requirement is that [certain information be present in the first negotiation message](#mandatory-payload-fields).

Handshake negotiation is only considered complete when one of the peers includes the [negotiation field](#field-_negotiation) in a neogtiation message. What leads to this is entirely up to your protocol.

Handshake negotiation is particularly useful when incorporating encryption into your protocol.

| Time | Peer "Initiator"                | Dir  | Peer "Accepting"                         |
| ---- | ------------------------------- | ---- | ---------------------------------------- |
| T1   | Identifier                      | ===> |                                          |
|      | Negotiate: P: handshake, A: ... | ===> |                                          |
|      |                                 | <=== | Identifier                               |
|      |                                 | <=== | Negotiate: P: passive, A: handshake, ... |
| T2   |                                 | <=== | Negotiate: Handshake params              |
| T3   | Negotiate: Handshake params     | ===> |                                          |
| T4   |                                 | <=== | Negotiate: Handshake params              |
| ...  | ...                             |      | ...                                      |
| Tn-1 | Negotiate: Handshake params     | ===> |                                          |
| Tn   |                                 | <=== | Negotiate: is complete: success/failure  |
| Tn+1 | (negotiation complete)          |      | (negotiation complete)                   |
|      | Messages                        | <==> | Messages                                 |

At `T1`, the initiator peer proposes `handshake`, and its accept list is ignored. It also includes all information necessary for the first handshake negotiation. The accepting peer sends only the most basic negotiation message, proposing `passive`, and accepting at least `handshake`.

At `T2`, the peers have successfully negotiated to use `handshake` mode. The accepting peer sends a "corrected" first negotiation message, containing a response to the initiator peer's handshake parameters.

At `T3`, the initiator peer evaluates the accepting peer's handshake message, and responds with another handshake message. This continues back and forth until one peer notifies the other that negotiation is complete using the [negotiation field](#field-_negotiation) (at `Tn`).


### Hard and Soft Failures

Some negotiation failures aren't necessarily fatal. Depending on your protocol design, certain kinds of failures can be recovered from.

A `soft failure` means that a particular negotiation attempt failed, but, if the negotiation mode or protocol allows it, the peers may make another attempt, or choose an alternative.

A `hard failure` is always considered unrecovarable. After a hard failure, the session is dead, and the peers should disconnect.

Note: Since `simple` and `yield` negotiation modes don't allow sending more than one negotiation message, all failures are hard failures in these modes.


### Identifier Message Layout

The 8-byte identifier message serves to identify the base protocol that will be used for negotiation.

| Field          | Type         | Octets | Value   |
| -------------- | ------------ | ------ | ------- |
| Initiator      | bytes        |    2   | "pN"    |
| Identifier     | bytes        |    5   | "STRMX" |
| Version        | unsigned int |    1   | 1       |

The initiator field is always the 2-byte UTF-8 sequence `pN`

The 5-byte identifier `STRMX` identifies this as a Streamux negotiation message.

The Streamux version is currently 1.

This message must match exactly between peers, otherwise it is a [hard failure](#hard-and-soft-failures).


### Negotiation Message Layout

The negotiation message facilitates negotiation of any parameters the peers must agree upon in order to communicate successfully.

| Field          | Type         | Octets | Notes                          |
| -------------- | ------------ | ------ | ------------------------------ |
| Fixed Data     | bytes        |    *   | Determined through negotiation |
| Payload Length | unsigned int |    4   |                                |
| Payload        | bytes        |    *   |                                |
| Padding        | bytes        |    *   | Determined through negotiation |

#### Fixed Data

The fixed data field has a length of 0 until otherwise negotiated. Once [fixed data length](#field-_fixed_length) has been successfully negotiated to a value greater than 0, all future negotiation messages will contain a fixed data portion of the selected length.

#### Payload Length

The payload length refers to the length of the payload field only (it doesn't include the length of the payload length field or the padding).

#### Payload

The payload field contains sub-fields with message-specific information (if any). If encryption is used, this field (and any padding) should be encrypted as soon as it is possible to do so (after negotiating encryption parameters).

#### Padding

Padding is 0 until otherwise negotiated. Once [padding](#field-_padding) has been successfully negotiated to a value greater than 0, this field in all future messages must contain the number of bytes required to pad out the payload field's length to a multiple of the padding amount. Block ciphers can then encrypt the payload + padding fields together. Padding does not count towards the payload length.


### Payload Sub-Fields

The payload is composed of sub-fields, encoded using [Concise Binary Encoding, version 1](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md), as an inlined map (containing only the key-value pairs, not the "begin map" or "end container" markers). Fields are recorded as key-value pairs in this map.

Map keys may be of any type, but all string typed keys beginning with an underscore `_` are reserved for use by Streamux.


### Mandatory Payload Fields

The first negotiation message sent by each peer must contain at least the following fields. For handshake mode, further negotiation messages may be sent according to your handshake protocol design, and do not need to contain these fields.

| Field         | Type   | Description                    |
| ------------- | ------ | ------------------------------ |
| `_mode`       | string | Negotiation mode               |
| `_protocol`   | map    | Overlaid protocol              |
| `_id_cap`     | map    | Maximum allowable ID value     |
| `_length_cap` | map    | Maximum allowable length value |


#### Field `_mode`

The proposed negotiation mode must be one of:
- `passive`
- `simple`
- `yield`
- `handshake`

When a peer proposes `passive`, it is indicating that it will accept whatever negotiation mode the other peer proposes, provided that the other peer's proposed mode is in this peer's [allowed negotiation modes](#field-_allowed_modes).

Rules:

* When a peer proposes a negotiation mode other than `passive`, its own [allowed negotiation modes](#field-_allowed_modes) list is ignored.
* Only one peer may propose a non-passive mode. If both propose a non-passive mode, it is a [hard failure](#hard-and-soft-failures).
* With the exception of `passive`, if the proposed mode doesn't exist in the other peer's [allowed negotiation modes](#field-_allowed_modes), it is a [hard failure](#hard-and-soft-failures).
* If both peers propose `passive` mode, then negotiation defaults to `simple`, provided simple mode is in both peers' [allowed negotiation modes](#field-_allowed_modes). Otherwise it is a [hard failure](#hard-and-soft-failures).
* As a special case, if both peers propose `simple`, then simple mode is automatically and successfully chosen, disregarding all other rules.


#### Field `_protocol`

The Protocol field contains the identifier and version of the proposed protocol to be overlaid on top of Streamux:

| Field | Type   |
| ----- | ------ |
| `id`  | string |
| `ver` | string |

`id` (Identifier) uniquely identifies the protocol that will be used on top of this protocol.

`ver` (Version) is a string following [Semantic Versioning 2.0.0](https://semver.org/). `MAJOR` version changes indicate a backwards incompatible change, and so only the `MAJOR` portion is considered when deciding compatibility between peers. `MINOR` and `PATCH` information are only used for diagnostic and debugging purposes.

The ID and the `MAJOR` portion of the version must match exactly between peers, otherwise it is a [hard failure](#hard-and-soft-failures).


#### Field `_id_cap`

The ID Cap field negotiates the maximum allowed ID value in messages (implying the maximum number of messages that may be in-flight at a time) during this session.

| Field      | Type        | Valid Range          |
| ---------- | ----------- | -------------------- |
| `min`      | signed int  | 0 - 32767            |
| `max`      | signed int  | 0 - 536870911        |
| `proposed` | signed int  | 0 - 536870911, or -1 |

Note: An ID cap of 0 means that there can be only one message in-flight at a time (the ID field will have bit width 0, and therefore all messages will implicitly be ID 0).

The `min`, `max`, and `proposed` sub-fields from the two peers ("us" and "them") are used to generate a negotiated value. The algorithm is as follows:

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

The Length Cap field negotiates the maximum allowed length of message payloads during this session. This affects only the maximum length of the payload field (padding is not included in the payload length calculation).

| Field      | Type       | Valid Range           |
| ---------- | ---------- | --------------------- |
| `min`      | signed int | 1 - 32767             |
| `max`      | signed int | 1 - 1073741823        |
| `proposed` | signed int | 1 - 1073741823, or -1 |

Length Cap follows the same negotiation algorithm as [ID cap](#field-_id_cap).


#### ID and Length Cap Total Bit Count

The larger the ID and length caps, the more bits will be required to represent those value ranges in [message chunk headers](#chunk-header), and the larger the header will be for the current session.

The total of (id bits + length bits) must not exceed 30. If they do, they must be brought down to 30 using the following algorithm:

    if id_cap.bitcount + length_cap.bitcount > 30:
        if both are > 15: reduce both to 15
        else: (larger value) = 30 - (smaller value)

If a cap field's bit count is reduced, its new value will be the maximum value encodable in that number of bits.

Note: If the final negotiated cap values are not within the min and max values of BOTH peers, it is a [hard failure](#hard-and-soft-failures).

The choice of ID and length ranges will affect the characteristics of the session. Depending on your use case and operating environment, certain aspects will be more important than others:

* Small ID range: Limits the maximum number of simultaneous outstanding (in-flight) requests.
* Large ID range: Requires more memory and complexity to keep track of outstanding requests.
* Small length range: Increases the marginal costs of the header, losing useful bandwidth to overhead.
* Large length range: Increases the buffer size required to support the maximum message chunk length.



### Optional Payload Fields

There are other fields that are not always required for successful negotiation, but may be necessary depending on the negotiation mode and your protocol design:

| Field            | Type    |
| ---------------- | ------- |
| `_allowed_modes` | list    |
| `_fixed_length`  | map     |
| `_padding`       | map     |
| `_negotiation`   | boolean |
| `_`              | any     |


#### Field `_allowed_modes`

This field is a whitelist of negotiation modes that this peer supports. It may include any combination of:
- `simple`
- `yield`
- `handshake`

If this field is not present, [`simple`] is assumed.

Negotiation can only succeed if the other peer's [proposed negotiation mode](#field-_mode) (other than `passive`) is present in this peer's `_allowed_modes` list. If not, it is a [hard failure](#hard-and-soft-failures).


#### Field `_fixed_length`

This field negotiates the length of the fixed-length portion in both normal and negotiation messages. It's primarily of use in supporting encryption channel or authentication data, for example a message authentication code.

| Field      | Type         | Valid Range |
| ---------- | ------------ | ----------- |
| `max`      | unsigned int | any         |
| `proposed` | unsigned int | any         |

`max` defines the maximum fixed length this peer will agree to.

`proposed` defines the length that this peer proposes to use.

The negotiation process chooses the higher of the two peer's `proposed` values. If the chosen value is greater than either of the peer's `max` values, this is a [soft failure](#hard-and-soft-failures).

To defer to the other peer, simply choose `0` for proposed, and nonzero for `max`.

* If this field is not present, `0` is assumed for both values.
* If `max` is not present, it is assumed to be equal to `proposed`.


#### Field `_padding`

This field negotiates how much padding will be applied to both normal and negotiation message payloads. It's primarily of use in supporting block ciphers.


| Field      | Type         | Valid Range |
| ---------- | ------------ | ----------- |
| `max`      | unsigned int | any         |
| `proposed` | unsigned int | any         |

`max` defines the maximum padding amount this peer will agree to.

`proposed` defines the padding that this peer proposes to use.

The negotiation process chooses the higher of the two peer's `proposed` values. If the chosen value is greater than either of the peer's `max` values, this is a [soft failure](#hard-and-soft-failures).

To defer to the other peer, simply choose `0` for proposed, and nonzero for `max`.

* If this field is not present, `0` is assumed for both values.
* If `max` is not present, then it is assumed to be equal to `proposed`.


#### Field `_negotiation`

The presence of this field signals to the other peer that negotiation is complete. A boolean value of `true` indicates success, and a value of `false` indicates a [hard failure](#hard-and-soft-failures).

This field is ignored in `simple` and `yield` modes, because success or failure is implied from the first (and only) message exchange.

This field is required in the final message of a `handshake` negotiation. If the receiving peer disagrees with the sending peer's assessment of the negotiation, it is a [hard failure](#hard-and-soft-failures).


#### Field `_`

The optional filler field is available to aid in thwarting traffic analysis, and implementations are encouraged to add a random amount of "filler" data to negotiation messages if encryption is used. The receiving peer must discard this field if encountered.



### Negotiation Examples

#### Simple: All parameters are compatible

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |   100  |  1000  |  1000   |     100    |  1000000   |    100000   |
| Peer B |    -     | Simple  |   100  |  8000  |   500   |      50    |   300000   |    300000   |
| Result |    -     |    -    |   100  |  1000  |   500   |     100    |   300000   |    100000   |

ID (500) bits = 9, Length (100000) bits = 17, total <= 30 bits

Negotiation: **Success**

#### Simple: Min ID > Max ID

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |    50  |    200 |    200  |    1000    |    2000    |     2000    |
| Peer B |    -     | Simple  |  1000  |  30000 |   1000  |    1000    |   30000    |    30000    |
| Result |    -     |    -    |  1000  |    200 |    200  |    1000    |    2000    |     2000    |

The negotiated `ID Min` (1000) is greater than the negotiated `ID Max` (200).

Negotiation: **Fail**

#### Simple: Wildcard, initial negotiated bits total > 30

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |   100  |  50000 |  10000  |       50   |  1000000   |        -1   |
| Peer B |    -     | Simple  |   100  | 200000 |  20000  |    40001   |  1000000   |        -1   |
| Result |    -     |    -    |   100  |  50000 |  10000  |    40001   |  1000000   |     65535   |

In this case, both peer A and B used wildcard values for proposed length cap, so we use the formula:

`length = (1000000-40001)/2 + 40001 = 520000.5`, rounded up to `520001`

The resulting bit counts (id 10000 requires 14 bits, length 520001 requires 19 bits) are greater than 30, so the larger value is reduced:

`length bits = 30 - 14 = 16 bits`
`length = 2^16-1 = 65535`

Negotiation: **Success**

#### Simple: All proposed values use wildcard

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Rec |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ---------- |
| Peer A |  Simple  |    -    |   100  |  10000 |     -1  |      50    |  1000000   |       -1   |
| Peer B |    -     | Simple  |   100  | 200000 |     -1  |     250    |   200000   |       -1   |
| Result |    -     |    -    |   100  |  10000 |   5050  |     250    |   200000   |   100125   |

`ID = (20000-100)/2 + 100 = 5050` (13 bits)
`Length = (200000-250)/2 + 250 = 100125` (17 bits)

Negotiation: **Success**

#### Yield: Everything is compatible

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Yield   |    -    |   500  |  10000 |    500  |     1000   |   200000   |     8000    |
| Peer B |    -     |  Yield  |   100  | 100000 |   1000  |      200   |    30000   |     1000    |
| Result |    -     |    -    |   500  |  10000 |    500  |      200   |    30000   |     8000    |

Since we are using yield mode, Peer A's proposed values are chosen, and Peer B's proposed values are ignored.

Peer A's proposed values are within the constraints of both peers.

Negotiation: **Success**

#### Yield: Peer A's values are not within range of Peer B's constraints

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Yield   |    -    |   500  |  10000 |    500  |     1000   |   200000   |    60000    |
| Peer B |    -     |  Yield  |   100  | 100000 |   1000  |      200   |    30000   |     1000    |
| Result |    -     |    -    |   500  |  10000 |    500  |     1000   |    30000   |     fail    |

Peer A's proposed length cap (60000) is higher than peer B's length cap max (30000).

Negotiation: **Fail**



Messaging Phase
---------------

Messages are sent in chunks. A multi-chunk message has its `termination` field cleared to `0` for all but the last chunk. Single chunk messages always have the `termination` field set.

The request ID is scoped to its sender. If both peers send a request with the same ID, they are considered to be distinct, and don't conflict with each other. A response message inverts the scope: A peer responds to a request by putting in its response message the same request ID it received from the requesting peer, and setting the `response` bit to `1`.

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


### Message Flight

While a peer is awaiting a response from the other peer, the ID of that message is considered "in-flight", and cannot be re-used in other messages until the cycle is complete (responded to or cancel-acknowledged). However, not all requests require a response. If a particular request doesn't require a response, it may be returned to the ID pool immediately.

Peers must agree about which requests don't require a response in your protocol, either through outside agreement, or encoded into the message payload somewhere in a protocol-dependent manner.

Note: If a request does not require a reply, it cannot be [canceled](#cancel-message). Choose carefully which message types will require no reply in your protocol.


### Message Encoding

| Field          | Type         | Size                                 |
| -------------- | ------------ | ------------------------------------ |
| Chunk Header   | unsigned int | 1-4 (determined during negotiation)  |
| Fixed Data     | bytes        | determined during negotiation        |
| Payload        | bytes        | variable (length is in chunk header) |
| Padding        | bytes        | determined during negotiation        |


#### Chunk Header

The message chunk header is treated as a single (8, 16, 24, or 32 bit) unsigned integer composed of bit fields, and is transmitted in little endian byte order.

The header fields contain information about the message ID, payload length, and what kind of message this is. The request ID and length fields are placed adjacent to each other, next to the response and termination bits. Any unused upper bits must be cleared to `0`.

| Field       | Bits                          | Order     |
| ----------- | ----------------------------- | --------- |
| Unused      | determined during negotiation | High Bit  |
| Request ID  | determined during negotiation |           |
| Length      | determined during negotiation |           |
| Response    | 1                             |           |
| Termination | 1                             | Low Bit   |

The header size is determined by the bit widths of the [ID cap](#field-_id_cap) and [length Cap](#field-_length_cap) values decided upon during the negotiation phase.

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


##### Request ID

The request ID field is a unique number that is generated by the requesting peer to differentiate the current request from other requests that are still in-flight (IDs that have been sent to the peer but have not yet received a response, or have been canceled but not yet cancel acked). The requesting peer must not re-use IDs that are currently in-flight.

Request IDs are scoped to the requesting peer. For example, request ID 22 from peer A is distinct from request ID 22 from peer B.

Note: The first chosen request ID in the session should be unpredictable in order to make known-plaintext attacks more difficult in cases where this protocol is overlaid on an encrypted transport (see [RFC 1750](https://tools.ietf.org/html/rfc1750)).

##### Length

The length field refers to the number of octets in the payload portion of this chunk (the message chunk header itself does not count towards the length).

##### Response

The response bit is used to respond to a request sent by a peer. When set to `1`, the ID field refers to the ID of the original request sent by the requesting peer (scope is inverted).

##### Termination

The termination bit indicates that this is the final chunk for this request ID (as a request message or as a response message). For a large message that spans multiple chunks, you would clear this to `0` for all but the last chunk.


#### Fixed Data Field

The contents of the fixed data portion of the message are completely free-form, with a fixed data length decided during the [negotiation phase](#negotiation-phase).


#### Payload Field

The payload portion of the message contains the actual message data to pass to the next layer up. Its contents are application-specific, and its length is determined by the length field in the chunk header.

Note: If encryption is used, applications are encouraged to structure messages to support filler data as a means to help thwart traffic analysis.


#### Padding Field

If a padding amount greater than 0 was chosen during the [negotiation phase](#negotiation-phase), this field must contain the number of bytes required to pad out the payload field's length field to a multiple of the padding amount. Block ciphers can then encrypt the payload + padding fields together. Padding does not count towards the payload length.



### Empty Response

An `empty response` is a response message (with the response bit set to `1`) containing no data. `empty response` signals successful completion of the request, with no other data to report.



Out of Band Messages
--------------------

Out of band (OOB) messages are used for management of the protocol and session itself rather than for communication with the application (although they may affect the application's behavior). The system must be capable of sending OOB messages at a higher priority than any normal message, athough not all OOB messages will necessarily require such a high priority.

Like in normal messages, not all OOB messages require a response, in which case their ID may be recycled immediately.


### OOB Message Encoding

An OOB message looks almost identical to a regular message chunk, except that it will have a length of `0` and a termination bit of `0`. Because the length field is 0, a secondary 16-bit payload length field is present, giving a maximum OOB payload size of 65535. The rest of the message looks and behaves the same as a regular message.

| Field              | Type         | Size                                |
| ------------------ | ------------ | ----------------------------------- |
| Chunk Header       | unsigned int | 1-4 (determined during negotiation) |
| Fixed Data         | bytes        | determined during negotiation       |
| OOB Payload Length | unsigned int | 2                                   |
| OOB Payload        | bytes        | variable                            |
| Padding            | bytes        | determined during negotiation       |

Like in the negotiation message, the OOB message payload is encoded using [Concise Binary Encoding, version 1](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md), as an inlined map (containing only the key-value pairs, not the "begin map" or "end container" markers). The number and type of fields in the payload are up to your protocol design. Only the `_oob` field is required in every message.

#### Field `_oob`

The OOB field determines the type of OOB message this is. This is the only required field in an OOB message payload.

#### Field `_`

The optional filler field is available to aid in thwarting traffic analysis, and implementations are encouraged to add a random amount of "filler" data to OOB messages if encryption is used. The receiving peer must discard this field if encountered.


### OOB Message Types

The following OOB message types must be present in Streamux based protocols. You are free to add other OOB message types as needed by your protocol.

#### `ping` Message

The ping message requests a ping response from the peer. The response must be sent as soon as possible to aid in latency calculations.

Ping messages (and responses) must be sent at the highest priority.

#### `alert` Message

An alert message informs the other peer of an important events regarding the session (such as warnings, errors, etc). This message should not be used to transport application level events.

An alert message does not have a response.

##### Field `_message`

A string containing the contents of the alert.

##### Field `_severity`

A string containing the severity of the alert:

* `error`: Something in the session layer is incorrect or malfunctioning.
* `warn`: Something may be incorrect or malfunctining in the session layer, or may not work as would normally be expected.
* `info`: Information that the other peer should know about. Use this sparingly.
* `debug`: Never use this in production.

Alert message priority is up to the implementation. Error messages should normally be given the highest priority to avoid potential data loss.

#### `disconnect` Message

The disconnect message informs the other peer that we are ending the session and disconnecting. No furter messages may be sent after a disconnect message.

A disconnect message must be sent at the highest priority.

A disconnect message does not have a response.

#### `stop` Message

The stop message requests that the other peer stop sending normal messages. It may still send OOB messages.

A stop message must be sent at the highest priority.

A stop message does not have a response.

#### `start` Message

The start message informs the other peer that it may begin sending normal messages again.

A start message must be sent at the highest priority.

A start message does not have a response.

#### Cancel Message

The cancel message requests that the other peer cancel an operation.

Cancel request and response messages are encoded in a special way: While OOB messages normally allocate a new message ID, the cancel message re-uses the ID of the request it intends to cancel. This ensures that cancel messages can still be sent even during ID exhaustion (where all available message IDs are in flight). Its OOB payload length is always 0, and it has no OOB payload contents.

| Field              | Type         | Size                                | Value |
| ------------------ | ------------ | ----------------------------------- | ----- |
| Chunk Header       | unsigned int | 1-4 (determined during negotiation) |   *   |
| Fixed Data         | bytes        | determined during negotiation       |   *   |
| OOB Payload Length | unsigned int | 2                                   |   0   |

Cancel requests and responses must be sent at the highest priority.



Request Cancellation
--------------------

There are times when a requesting peer might want to cancel a request-in-progress. Circumstances may change, or the operation may be taking too long, or the ID pool may be exhausted. A requesting peer may cancel an outstanding request by sending a cancel message, citing the request ID of the request to be canceled.

Upon receiving a cancel request, the receiving peer must immediately clear all response message chunks with the specified ID from its send queue, abort any in-progress operations the original request with that ID triggered, and send a cancel response at the highest priority.

Once a cancel request has been issued, the ID of the canceled request is locked. A locked request ID cannot be used in request messages, and all response chunks to that request ID must be discarded. Once a cancel response is received for that ID, it is returned to the ID pool and may be used again. Because of this, ALL CANCEL REQUESTS MUST BE RESPONDED TO, regardless of their legitimacy.

#### Example:

* Peer A sends request ID 19
* Peer B receives request ID 19 and begins servicing it
* Peer A times out
* Peer A locks ID 19 and sends `cancel` ID 19
* Peer B sends response ID 19
* Peer A discards response ID 19 (because Peer A's ID 19 is still locked)
* Peer B receives `cancel` ID 19
* Peer B sends `cancel response` ID 19
* Peer A receives `cancel response` ID 19 and returns ID 19 to the ID pool

If a `cancel response` is not received, it means that either there is a communication problem (such as lag or a broken connection), or the responding peer is operating incorrectly.



Spurious Messages
-----------------

There are situations where a peer may receive spurious (unexpected) messages. Spurious messages include:

* Response to a canceled request.
* `cancel` message for a request not in progress.

These situations can arise when the transmission medium is lagged, or as the result of a race condition. A response to a request may have already been en-route at the time of cancellation, or the operation may have already completed before the `cancel` message arrived. A response to a canceled request must be ignored and discarded. A peer must always send a response to a `cancel` request, even if there is no operation to cancel or the request was not found.

The following are error conditions:

* Response to a nonexistent request.
* `cancel response` for a request that wasn't canceled.

In the error case, the peer may elect to report an error and/or end the connection, depending on your use case.



Version History
---------------

 * April 10, 2019: Preview Version 1



License
-------

Copyright (c) 2019 Karl Stenerud. All rights reserved.

Distributed under the Creative Commons Attribution License: https://creativecommons.org/licenses/by/4.0/legalcode
License deed: https://creativecommons.org/licenses/by/4.0/
