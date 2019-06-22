Streamux
========

A minimalist, asynchronous, multiplexing, request-response protocol.


### Features

* Minimal Overhead (header can be as small as 2 bytes per message chunk)
* Multiplexing (multiple data streams can be sent across a single channel)
* Asynchronous (peers are informed asynchronously upon completion or error)
* Interruptible (requests may be canceled)
* Floating roles (both peers can operate as client and server at the same time)
* Multiple negotiation modes
* Little to no repetition in the message structure and content.


### Contents

* [Use Case](#use-case)
* [General Operation](#general-operation)
* [Message Types](#message-types)
    * [Identifier Message](#identifier-message)
    * [Negotiation Message](#negotiation-message)
    * [Application Message](#application-message)
    * [Out of Band Message](#out-of-band-message)
* [Message Encoding](#message-encoding)
    * [Identifier Message Encoding](#identifier-message-encoding)
    * [Negotiation Message Encoding](#negotiation-message-encoding)
        * [Mandatory Negotiation Fields](#mandatory-negotiation-fields)
            * [Mode Field](#_mode-field)
            * [Protocol Field](#_protocol-field)
            * [ID Cap Field](#_id_cap-field)
            * [Length Cap Field](#_length_cap-field)
        * [Optional Negotiation Fields](#optional-negotiation-fields)
            * [Allowed Modes Field](#_allowed_modes-field)
            * [Fixed Length Field](#_fixed_length-field)
            * [Padding Field](#_padding-field)
            * [Envelope Mode Field](#_envelope_mode-field)
            * [Negotiation Field](#_negotiation-field)
            * [`_` Field](#_-field)
    * [Single and Packed Chunk Envelopes](#single-and-packed-chunk-envelopes)
        * [Single Chunk Envelope Mode](#single-chunk-envelope-mode)
        * [Packed Chunk Envelope Mode](#packed-chunk-envelope-mode)
    * [Application Message Encoding](#application-message-encoding)
    * [OOB Message Encoding](#oob-message-encoding)
        * [OOB Field](#_oob-field)
        * [`_` Field](#_-field)
    * [OOB Message Types](#oob-message-types)
        * [Ping Message](#_ping-message)
        * [Alert Message](#_alert-message)
            * [Message Field](#_message-field)
            * [Severity Field](#_severity-field)
        * [Disconnect Message](#_disconnect-message)
        * [Stop Message](#_stop-message)
        * [Start Message](#_start-message)
        * [Cancel Message](#_cancel-message)
    * [Message Envelope](#message-envelope)
        * [Envelope Length](#envelope-length)
        * [Fixed Length Data](#fixed-length-data)
        * [Variable Length Data](#variable-length-data)
        * [Padding](#padding)
    * [Message Chunk](#message-chunk)
        * [Chunk Header](#chunk-header)
            * [Request ID](#request-id)
            * [OOB Bit](#oob-bit)
            * [Response Bit](#response-bit)
            * [Termination Bit](#termination-bit)
        * [Chunk Payload](#chunk-payload)
* [Negotiation Phase](#negotiation-phase)
    * [Hard and Soft Failures](#hard-and-soft-failures)
    * [Negotiation Modes](#negotiation-modes)
        * [Simple Mode](#simple-mode)
        * [Yield Mode](#yield-mode)
        * [Handshake Mode](#handshake-mode)
    * [Identifier Negotiation](#identifier-negotiation)
    * [Negotiation Mode Negotiation](#negotiation-mode-negotiation)
    * [Protocol Negotiation](#protocol-negotiation)
    * [Cap Negotiation](#cap-negotiation)
    * [Max Negotiation](#max-negotiation)
    * [Negotiation Examples](#negotiation-examples)
* [Application Phase](#application-phase)
    * [Message Flight](#message-flight)
        * [No-Response Messages](#no-response-messages)
* [Out Of Band Messages](#out-of-band-messages)
    * [Request Cancellation](#request-cancellation)
* [Spurious Messages](#spurious-messages)



Use Case
--------

Streamux is designed as a point-to-point, bidirectional protocol foundation for you to build a messaging layer on top of. It handles the middle parts which tend to operate in a similar way in most stream oriented protocols. Think of it as a protocol construction kit.

Streamux is designed to support encryption, minimizing or providing mitigations for repetitive known data patterns so as to defend against known-plaintext and traffic analysis attacks.

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
|     encryption structural support   |  |
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

There are two main phases to a Streamux session: the negotiation phase and the application phase.

The negotiation phase begins with each peer sending an identifier message, followed by at least one negotiation message. Communication in the negotiation phase is synchronous after the first negotiation message, and becomes asynchronous when the phase ends. Once negotiation has successfully completed, the application phase begins. A typical session might look something like this:

| Peer X        | Peer Y        |
| ------------- | ------------- |
|               | Identifier    |
| Identifier    | Negotiate     |
| Negotiate     |               |
| Request A     |               |
| Request B     | Response A    |
|               | Request C     |
| Response C    | Response B    |
| ...           | ...           |
| Disconnect    |               |
| (end session) | (end session) |



Message Types
-------------

Streamux has four main kinds of messages:

### Identifier Message

Each peer sends an identifier message once and only once as their first message in order to identify the negotiation protocol, followed immediately by the first negotiation message.

### Negotiation Message

Negotiation messages are exchanged only during the negotiation phase of the session. Once the session parameters have been successfully negotiated, negotiation messages are no longer sent.

Note: Triggering a new negotiation phase after the application phase has begun is not expressly forbidden, as some protocols may require this. Details of such an operation would be protocol-specific, although this would best be done via OOB messages.

### Application Message

Application messages are sent over the course of the application phase, and contain your application data.

### Out of Band Message

OOB messages are also sent over the course of the application phase, but are restricted to session management signaling. Error handling, flow control, operation cancellation, disconnects and such are handled via OOB messages.



Message Encoding
----------------

All messages except for the identifier message are encoded into a [message envelope](#message-envelope).


### Identifier Message Encoding

The 8-byte identifier message identifies the base protocol that will be used for negotiation.

| Field          | Type         | Octets | Value   |
| -------------- | ------------ | ------ | ------- |
| Initiator      | bytes        |    2   | "pN"    |
| Identifier     | bytes        |    5   | "STRMX" |
| Version        | unsigned int |    1   | 1       |

The initiator field is always the 2-byte UTF-8 sequence `pN`

The 5-byte identifier `STRMX` identifies this as a Streamux negotiation message.

The Streamux version is currently 1.


### Negotiation Message Encoding

A negotiation message is a [message envelope](#message-envelope) containing a [Concise Binary Encoding, version 1 inline map](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md#inline-containers) in the [`variable length data`](#variable-length-data) section. Fields are encoded as key-value pairs in this map.

Map keys may be of any type, but all string typed keys beginning with an underscore `_` are reserved for use by Streamux.


#### Mandatory Negotiation Fields

The first negotiation message sent by each peer must contain at least the following fields. For handshake mode, further negotiation messages may be sent according to your handshake protocol design, and do not need to contain these fields.

| Field         | Type   | Description                    |
| ------------- | ------ | ------------------------------ |
| `_mode`       | string | Negotiation mode               |
| `_protocol`   | map    | Overlaid protocol              |
| `_id_cap`     | map    | Maximum allowable ID value     |
| `_length_cap` | map    | Maximum allowable length value |

##### `_mode` Field

The proposed negotiation mode must be one of:
- `passive`
- `simple`
- `yield`
- `handshake`

##### `_protocol` Field

The Protocol field contains the identifier and version of the proposed protocol to be overlaid on top of Streamux:

| Field      | Type   |
| ---------- | ------ |
| `_id`      | string |
| `_version` | string |

`_id` uniquely identifies the protocol that will be used on top of this protocol.

`_version` is a string identifying the version of the protocol to use. Ideally, the version string should follow [Semantic Versioning 2.0.0](https://semver.org/), where only `MAJOR` version changes indicate a backwards incompatible change. If a semantic version is detected, only the `MAJOR` version must match (`MINOR` and `PATCH` are for diagnostic and debugging purposes only). If the version string is not a semantic version, then the entire string must match.

##### `_id_cap` Field

Negotiates the maximum allowed ID value (ID cap) in messages, implying the maximum number of messages that may be [in-flight](#message-flight) at a time during this session. An ID cap of 0 effectively means that there can be only one message (with ID 0) in-flight at any time.

| Field       | Type         | Meaning                                              |
| ----------- | ------------ | ---------------------------------------------------- |
| `_min`      | unsigned int | Maximum cap allowed by this peer                     |
| `_max`      | unsigned int | Minimum cap allowed by this peer                     |
| `_proposed` | signed int   | Proposed cap to the other peer (negative = wildcard) |

##### `_length_cap` Field

Negotiates the maximum [message envelope](#message-envelope) length. It is encoded the same way as the [ID cap negotiation field](#_id_cap-field).


#### Optional Negotiation Fields

There are other fields that are not always required for successful negotiation, but may be necessary depending on the negotiation mode and your protocol design:

| Field            | Type    |
| ---------------- | ------- |
| `_allowed_modes` | list    |
| `_fixed_length`  | map     |
| `_padding`       | map     |
| `_envelope_mode` | string  |
| `_negotiation`   | boolean |
| `_`              | any     |

##### `_allowed_modes` Field

This field is a whitelist of negotiation modes that this peer supports. It may include any combination of:
- `simple`
- `yield`
- `handshake`

If this field is never negotiated, the default of [`simple`] is assumed.

##### `_fixed_length` Field

This field negotiates the length of the fixed-length portion in message envelopes.

| Field       | Type         |
| ----------- | ------------ |
| `_max`      | unsigned int |
| `_proposed` | unsigned int |

`max` defines the maximum fixed length this peer will agree to.

`proposed` defines the length that this peer proposes to use.

To defer to the other peer, choose `0` for proposed, and nonzero for `max`.

* If this field is never negotiated, the default of `0` is assumed for both values.
* If `max` is not present, it is assumed to be equal to `proposed`.

Once fixed length has been negotiated, all future messages (including negotiation messages) must include a fixed length data field of the specified length.

#### `_padding` Field

Negotiates the multiple to which the `variable length data` portion of all [message envelopes](#message-envelope) must be [padded](#padding). This field is encoded in the same way as the [fixed length negotiation field](#_fixed_length-field).

Once padding has been negotiated, all future messages (including negotiation messages) must be padded to the amount negotiated.

#### `_envelope_mode` Field

Negotiates [how to chunk data in a message envelope](#single-and-packed-chunk-envelopes) during the [application phase](#application-phase). The allowed values are:

* `single`
* `packed`

If this field is never negotiated, the default of `single` is assumed.

#### `_negotiation` Field

During [handshake negotiation](#handshake-mode), the presence of this field marks the end of negotiation, with a boolean value of `true` for successful negotiation.

This field is unneeded and ignored in [simple](#simple-mode) and [yield](#yield-mode) modes, because success or failure is implied from the first (and only) message exchange.

#### `_` Field

This optional filler field is available to aid in thwarting traffic analysis, and implementations are encouraged to add a random amount of "filler" data to negotiation messages if encryption is used. The receiving peer must discard this field if encountered.


### Single and Packed Chunk Envelopes

Message envelopes for application and OOB messages can contain either a single or multiple [message chunks](#message-chunk). The chunk envelope mode is [negotiated once for the entire session](#_envelope_mode-field), and the encoding is slightly different depending on the mode.

The default mode if not negotiated is single chunk mode.

#### Single Chunk Envelope Mode

In single chunk mode, the [`variable length data`](#variable-length-data) field contains a [chunk header](#chunk-header) and a chunk payload containing a chunk of an application-specific message.

| Field         | Type  | Octets |
| ------------- | ----- | ------ |
| Chunk Header  | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ |
| Chunk Payload | bytes |   *    |

#### Packed Chunk Envelope Mode

In packed chunk mode, the [`variable length data`](#variable-length-data) field contains a series of message chunks, each prefixed with a length field:

| Field                | Type  | Octets | Notes                  |
| -------------------- | ----- | ------ | ---------------------- |
| Chunk Payload Length | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ | Length of payload only |
| Chunk Header         | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ |                        |
| Chunk Payload        | bytes |   *    |                        |
|                      |       |        |                        |
| Chunk Payload Length | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ | Length of payload only |
| Chunk Header         | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ |                        |
| Chunk Payload        | bytes |   *    |                        |
| ...                  | ...   |  ...   |                        |

Chunk payload length refers to the length of the `chunk payload` only. It does not include the length of any other field.

Care must be taken in the implementation of packed chunk envelope mode. Messages of different priorities might be handled differently during message queuing (for example, they might take a different communication channel), in which case they should not be packed together. Packed chunk envelopes must not be buffered for too long waiting to be filled. Packing message chunks comes at the cost of increased latency.



### Application Message Encoding

Application messages are [single or packed mode message envelopes](#single-and-packed-chunk-envelopes) containing application-specific data in their chunk payloads.

If encryption is used, applications should be encouraged to structure their application data to support filler data, which can aid in thwarting traffic analysis.



### OOB Message Encoding

OOB messages are [single or packed mode message envelopes](#single-and-packed-chunk-envelopes) containing out-of-band data in their chunk payloads.

The payload of an OOB message is encoded as a [Concise Binary Encoding, version 1 inline map](https://github.com/kstenerud/concise-encoding/blob/master/cbe-specification.md#inline-containers). Fields are encoded as key-value pairs in this map.

Map keys may be of any type, but all string typed keys beginning with an underscore `_` are reserved for use by Streamux. The following predefined fields are automatically recognized:

#### `_oob` Field

The OOB field specifies the [OOB message type](#oob-message-types).

This field is mandatory in all OOB messages.

#### `_` Field

This optional filler field is available to aid in thwarting traffic analysis, and implementations are encouraged to add a random amount of "filler" data to OOB messages if encryption is used. The receiving peer must discard this field if encountered.


### OOB Message Types

The following OOB message types must be present in Streamux based protocols. You are free to add other OOB message types needed by your protocol or add additional fields to existing types.

Message types beginning with an underscore `_` are reserved for use by Streamux.

#### `_ping` Message

The `_ping` message requests a ping response from the peer. The response must be sent as soon as possible to aid in latency calculations.

Ping messages (and responses) must be sent at the highest priority.

#### `_alert` Message

An `_alert` message informs the other peer of an important event regarding the session (such as warnings, errors, etc). This message should not be used to transport application level events.

An alert message does not have a response.

The alert message contains the following fields:

##### `_message` Field

A string containing the contents of the alert.

##### `_severity` Field

A string containing the severity of the alert:

* `error`: Something in the session layer is definitely incorrect or malfunctioning.
* `warn`: Something may be incorrect or malfunctioning in the session layer, or may not work as would normally be expected.
* `info`: Information that the other peer should know about. Use this sparingly.
* `debug`: Don't use this in production.

Alert message priority is up to your protocol design. Error alerts should normally be given the highest priority to avoid potential data loss.

#### `_disconnect` Message

The `_disconnect` message informs the other peer that we are ending the session and disconnecting. No further messages may be sent after a disconnect message.

A disconnect message must be sent at the highest priority.

A disconnect message does not have a response.

#### `_stop` Message

The `_stop` message requests that the other peer stop sending application messages. It may still send OOB messages.

A stop message must be sent at the highest priority.

A stop message does not have a response.

#### `_start` Message

The `_start` message informs the other peer that it may begin sending application messages again.

A start message must be sent at the highest priority.

A start message does not have a response.

#### `_cancel` Message

The `_cancel` message requests that the other peer cancel an operation and acknowledge.

While OOB and application messages normally allocate a new request ID, the cancel message re-uses the ID of the request it wants to cancel. This ensures that cancel messages can still be sent even during ID exhaustion (where all available request IDs are in flight).

Cancel requests and responses must be sent at the highest priority.


### Message Envelope

The message envelope consists of a length field, followed by possible fixed length data, and then the (possibly padded) remaining data, whose composition depends on the message type.

| Field                | Type   | Octets | Notes                                                 |
| -------------------- | ------ | ------ | ----------------------------------------------------- |
| Envelope Length      | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ | |
| Fixed Length Data    | bytes  |   *    | Length 0 until otherwise negotiated                   |
| Variable Length Data | bytes  |   *    |                                                       |
| Padding              | [varpad](https://github.com/kstenerud/varpad/blob/master/varpad-specification.md) |   1+   | Only present if [padding is enabled](#_padding-field) |

#### Envelope Length

This is the byte length of the entire message envelope (including the length field itself). Envelope length is encoded as a [VLQ unsigned integer](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md).

#### Fixed Length Data

The `fixed length data` field has a length of `0` until otherwise negotiated. Once [fixed length](#_fixed_length-field) has been successfully negotiated to a value greater than `0`, all future message envelopes must contain a `fixed length data` field of the selected length. The contents of the `fixed length data` field are protocol-specific, and beyond the scope of this document.

#### Variable Length Data

This is the envelope's main payload. Most commonly, it will contain a [message chunk](#message-chunk).

#### Padding

The padding field is of type [varpad](https://github.com/kstenerud/varpad/blob/master/varpad-specification.md), and pads the `variable length data` field to bring its length to a multiple of the padding amount.

The padding amount is negotiated via the [`_padding` field](#_padding-field).


### Message Chunk

A message chunk contains a portion of a message (or perhaps an entire message, if it's small enough), encoded as a chunk header and a payload.

| Field         | Type  | Octets |
| ------------- | ----- | ------ |
| Chunk Header  | [VLQ](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) | 1+ |
| Chunk Payload | bytes |   *    |

#### Chunk Header

The `chunk header` is a [VLQ unsigned integer](https://github.com/kstenerud/vlq/blob/master/vlq-specification.md) encoded unsigned integer containing bit-encoded subfields:

| Field       | Bits | Bit Order |
| ----------- | ---- | --------- |
| Request ID  | 4+   | High Bit  |
| OOB         | 1    |           |
| Response    | 1    |           |
| Termination | 1    | Low Bit   |

##### Request ID

The request ID field is a unique number that is generated by the requesting peer to differentiate the current request from other requests that are still [in-flight](#message-flight). The requesting peer must not re-use IDs that are currently in-flight.

Request IDs are scoped to the requesting peer. For example, request ID 22 from peer A is distinct from request ID 22 from peer B.

The [ID cap field](#_id_cap-field) determines the maximum allowed value of this field for the current session.

Note: The first chosen request ID in the session should be unpredictable in order to make known-plaintext attacks more difficult (see [RFC 1750](https://tools.ietf.org/html/rfc1750)).

##### OOB Bit

A value of `1` marks this as an [out of band message](#out-of-band-messages).

##### Response Bit

The response bit is used to respond to a request sent by a peer. When set to `1`, the ID field refers to the ID of the original request sent by the requesting peer (scope is inverted).

Note: A response message with no chunk payload (empty response) signals successful completion of the request, with no other data to report.

##### Termination Bit

The termination bit indicates that this is the final chunk for this request ID (as a request message or as a response message). For a large message that spans multiple chunks, you would clear this to `0` for all but the last chunk.

#### Chunk Payload

The chunk payload contains the actual message data, which is either an entire message, or part of one.



Negotiation Phase
-----------------

Before a peer can send application messages, it must first negotiate the session parameters.


### Hard and Soft Failures

Some negotiation failures aren't necessarily fatal. Depending on your protocol design, certain kinds of failures can be recovered from.

A `soft failure` means that a particular negotiation attempt failed, but, if the negotiation mode or protocol allows it, the peers may make another attempt, or choose an alternative.

A `hard failure` is always considered unrecoverable. After a hard failure, the session is dead, and the peers should disconnect.

Note: Since `simple` and `yield` negotiation modes don't allow sending more than one negotiation message, all failures are hard failures in these modes.


### Negotiation Modes

There are three modes of negotiation supported. A protocol is not required to support all of them. Your protocol design will dictate which mode(s) work best for your needs.


#### Simple Mode

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

At `T1`, each peer sends an identifier and negotiation message at the same time (the illustration merely shows the reality that one will be sent earlier than the other, even if only by a nanosecond). Peer X proposes `simple`. Peer Y proposes `passive` and accepts at least `simple`. Note that simple mode negotiation would also succeed if peer Y had proposed `simple` mode (see [Proposed Negotiation Mode](#_mode-field))

At `T2`, each peer employs the "simple" negotiation algorithm and reaches the same conclusion (success or failure). There is therefore no need for any further negotiation messages.


#### Yield Mode

Yield negotiation is similar to simple negotiation. Each peer still sends only one negotiation message, but in this case the "yielding" peer yields to the "initiator" peer, accepting their proposed parameters.

The advantage of yield negotiation is that the initiator peer doesn't need to wait for the yielding peer's negotiation message before sending regular messages, thus eliminating a negotiation delay.

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

At `T1`, both peers send an identifier and negotiation message at the same time, like in simple mode. The initiator peer proposes `yield`, and its accept list is ignored. The yielding peer proposes `passive`, and accepts at least `yield`. The initiator peer doesn't wait for the other's negotiation message, and sends its first request immediately.

At `T2`, both peers have received each other's negotiation message, and have run the identical negotiation algorithm to determine the result. If negotiation succeeds, the yielding peer will process the proposing peer's already sent request as normal. If negotiation fails, the request will neither be acted upon nor responded to because this is a [hard failure](#hard-and-soft-failures)


#### Handshake Mode

With handshake negotiation, each peer sends negotiation messages in turn until a consensus is reached for all negotiation parameters, after which application messaging may begin.

The contents of these negotiation messages depends on the needs of your protocol. The only requirement is that [certain information be present in the first negotiation message](#mandatory-negotiation-fields).

Handshake negotiation is only considered complete when one of the peers includes the [negotiation field](#_negotiation-field) in a negotiation message. What leads to this is entirely up to your protocol. If the receiving peer disagrees with the sending peer's assessment of the negotiation, it is a [hard failure](#hard-and-soft-failures).

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

At `T1`, both peers send an identifier and initial negotiation message at the same time, like in simple mode. The initiator peer proposes `handshake`, and its accept list is ignored. It also includes all information necessary for the first handshake negotiation. The accepting peer sends only the most basic negotiation message, proposing `passive`, and accepting at least `handshake`.

At `T2`, the peers have successfully negotiated to use `handshake` mode. The accepting peer sends a "corrected" first negotiation message, containing a response to the initiator peer's handshake parameters.

At `T3`, the initiator peer evaluates the accepting peer's handshake message, and responds with another handshake message. This continues back and forth until one peer notifies the other that negotiation is complete using the [negotiation field](#_negotiation-field) (at `Tn`).


### Identifier Negotiation

The [identifier message](#identifier-message-encoding) must match exactly between peers, otherwise it is a [hard failure](#hard-and-soft-failures).


### Negotiation Mode Negotiation

When a peer proposes the [negotiation mode](#_mode-field) `passive`, it is signaling that it will accept whatever negotiation mode the other peer proposes, provided that the other peer's proposed mode is in this peer's [allowed negotiation modes](#_allowed_modes-field).

Rules:

* When a peer proposes a negotiation mode other than `passive`, its own [allowed negotiation modes](#_allowed_modes-field) list is ignored.
* Only one peer may propose a non-passive mode. If both propose a non-passive mode, it is a [hard failure](#hard-and-soft-failures).
* With the exception of `passive`, if the proposed mode doesn't exist in the other peer's [allowed negotiation modes](#_allowed_modes-field), it is a [hard failure](#hard-and-soft-failures).
* If both peers propose `passive` mode, then negotiation defaults to `simple`, provided simple mode is in both peers' [allowed negotiation modes](#_allowed_modes-field). Otherwise it is a [hard failure](#hard-and-soft-failures).
* As a special case, if both peers propose `simple`, then simple mode is automatically and successfully chosen, disregarding all other rules.


### Protocol Negotiation

The ID field and the `MAJOR` portion of the version field in a peer's [protocol](#_protocol-field) proposal must match exactly with the other peer, otherwise it is a [soft failure](#hard-and-soft-failures).


### Cap Negotiation

This is used to negotiate the [ID cap](#_id_cap-field) and [length cap](#_length_cap-field).

The `min`, `max`, and `proposed` sub-fields from the two peers ("us" and "them") are used to generate a negotiated value. The algorithm is as follows:

#### Min, max:

    min = maximum(us.min, them.min)
    max = minimum(us.max, them.max)
    if max < min: fail

A failure here is a [soft failure](#hard-and-soft-failures).

#### Proposed:

    proposed = minimum(us.proposed, them.proposed)

#### Wildcards:

Peers may also use the "wildcard" value (any negative value) in the `proposed` field, meaning that they will defer to the other peer's proposed value. This changes how `proposed` is calculated:

    if us.proposed = wildcard: proposed = them.proposed
    if them.proposed = wildcard: proposed = us.proposed
    if both us and them use wildcard: proposed = (max-min)/2 + min, rounding up

#### Negotiated Value:

    negotiated value = minimum(maximum(proposed, min), max)



### Max Negotiation

This is used to negotiate [fixed length](#_fixed_length-field) and [padding](#_padding-field).

Recall that these fields have two unsigned integer subfields:
* `max` defines the maximum fixed length this peer will agree to.
* `proposed` defines the length that this peer proposes to use.

The negotiation process chooses the higher of the two peer's `proposed` values. If the chosen value is greater than either of the peer's `max` values, it is a [soft failure](#hard-and-soft-failures).



### Negotiation Examples

#### Simple: All parameters are compatible

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |   100  |  1000  |  1000   |     100    |  1000000   |    100000   |
| Peer B |    -     | Simple  |   100  |  8000  |   500   |      50    |   300000   |    300000   |
| Result |    -     |    -    |   100  |  1000  |   500   |     100    |   300000   |    100000   |

Negotiation: **Success**

#### Simple: Negotiated min ID > max ID

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |    50  |    200 |    200  |    1000    |    2000    |     2000    |
| Peer B |    -     | Simple  |  1000  |  30000 |   1000  |    1000    |   30000    |    30000    |
| Result |    -     |    -    |  1000  |    200 |    200  |    1000    |    2000    |     2000    |

The negotiated `ID Min` (1000) is greater than the negotiated `ID Max` (200).

Negotiation: **Fail**

#### Simple: One peer uses wildcard for length

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |   100  |  50000 |  10000  |       50   |  1000000   |        -1   |
| Peer B |    -     | Simple  |   100  | 200000 |  20000  |    40001   |  1000000   |     50000   |
| Result |    -     |    -    |   100  |  50000 |  10000  |    40001   |  1000000   |     50000   |

Negotiation: **Success**

#### Simple: Both peers use wildcard for length

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Prop |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ----------- |
| Peer A |  Simple  |    -    |   100  |  50000 |  10000  |       50   |  1000000   |        -1   |
| Peer B |    -     | Simple  |   100  | 200000 |  20000  |    40001   |  1000000   |        -1   |
| Result |    -     |    -    |   100  |  50000 |  10000  |    40001   |  1000000   |    520001   |

In this case, both peer A and B used wildcard values for proposed length cap, so we use the formula:

`length = (1000000-40001)/2 + 40001 = 520000.5`, rounded up to `520001`

Negotiation: **Success**

#### Simple: Both peers use wildcard for everything

| Peer   | Proposed | Allowed | ID Min | ID Max | ID Prop | Length Min | Length Max | Length Rec |
| ------ | -------- | ------- | ------ | ------ | ------- | ---------- | ---------- | ---------- |
| Peer A |  Simple  |    -    |   100  |  10000 |     -1  |      50    |  1000000   |       -1   |
| Peer B |    -     | Simple  |   100  | 200000 |     -1  |     250    |   200000   |       -1   |
| Result |    -     |    -    |   100  |  10000 |   5050  |     250    |   200000   |   100125   |

`ID = (20000-100)/2 + 100 = 5050`
`Length = (200000-250)/2 + 250 = 100125`

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



Application Phase
-----------------

Once both peers are in agreement that negotiation has completed successfully, the application phase begins.

Application and OOB messages are given their own request ID during their [flight time](#message-flight), and are split into multiple chunks if they are too large to fit in a single chunk. A multi-chunk message has its `termination` field cleared to `0` for all but the last chunk.

The request ID is scoped to its sender. If both peers send a request with the same ID, they are considered to be distinct, and don't conflict with each other. A response message inverts the scope: A peer responds to a request by putting in its response message the same request ID it received from the requesting peer, and setting the `response` bit to `1`.

Message chunks with the same ID must be sent in-order (chunk 5 of request ID 42 must not be sent before chunk 4 of request ID 42). The message is considered complete once the `termination` field is set. Note that this does not require you to send all chunks for one message before sending chunks from another message. Chunks from different messages can be sent interleaved, like so:

* Request ID 10, chunk 0
* Request ID 11, chunk 0
* Request ID 12, chunk 0
* Request ID 11, chunk 1
* Request ID 10, chunk 1 (termination = true)
* Request ID 11, chunk 2
* Request ID 12, chunk 1 (termination = true)
* Request ID 11, chunk 3 (termination = true)

Your choice of message chunk sizing and scheduling will depend on your use case.


### Message Flight

Every application and OOB message must be assigned a unique ID that will remain in use until the message has received a response or has been successfully [canceled](#request-cancellation). While the ID is in use, we say that it is "in-flight".

An "in-flight" ID cannot be re-used in other messages until its message cycle has completed (either it has been responded to, or it has been canceled and we have received a cancel response).

#### No-Response Messages

Messages may be designated in your protocol to not require a response. A "no-response" message is a request that is both guaranteed to succeed, and something that the requesting side will never care about the result of.

Upon sending a "no-response" message, its ID is returned to the ID pool immediately after being sent. This also means that "no-response" messages cannot be canceled. Choose carefully when designing your protocol.

The following message types must be identified as "no-response" in Streamux based protocols:

* [Alert](#_alert-message)
* [Disconnect](#_disconnect-message)
* [Stop](#_stop-message)
* [Start](#_start-message)



Out of Band Messages
--------------------

Out of band (OOB) messages are used for management of the protocol and session itself rather than for communication with the application (although they may affect the application's behavior). 

Because they affect the session itself, OOB messages have different requirements from application messages. OOB message contents are encoded in a [particular way](#oob-message-encoding), and the underlying system must be capable of sending OOB messages at a higher priority than any application message (although not all OOB messages will necessarily require such a high priority).


### Request Cancellation

There are times when a requesting peer might want to cancel a request-in-progress. Circumstances may change, or the operation may be taking too long, or the ID pool may be exhausted. A requesting peer may cancel an outstanding request by sending a [cancel message](#_cancel-message), citing the request ID of the request to be canceled.

Note: ["no-response" messages](#no-response-messages) cannot be canceled.

Upon receiving a cancel request, the receiving peer must immediately clear all response message chunks with the specified ID from its send queue, abort any in-progress operations the original request with that ID triggered, and send a cancel response at the highest priority.

When a cancel request is issued, the ID of the canceled request remains locked ["in-flight"](#message-flight) until a cancel response is received for that ID. All response chunks to that request ID must be discarded until a cancel response is received.

Because the request ID remains locked until a response is received, ALL CANCEL REQUESTS MUST BE RESPONDED TO, regardless of their legitimacy. If a peer believes the other is misbehaving, it can send an [alert message](#_alert-message) AFTER responding to the cancel request.

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

In the error case, the peer must send an [error message](#_alert-message) to alert the other peer. It may also possibly end the session, depending on your protocol design.



Version History
---------------

 * April 10, 2019: Preview Version 1



License
-------

Copyright (c) 2019 Karl Stenerud. All rights reserved.

Distributed under the Creative Commons Attribution License: https://creativecommons.org/licenses/by/4.0/legalcode
License deed: https://creativecommons.org/licenses/by/4.0/
