#include <streamux/streamux.h>

// #define KSLogger_LocalLevel DEBUG
#include "kslogger.h"

const int PROTOCOL_VERSION = 1;

typedef enum
{
    PROTOCOL_STATE_DECODING_INIT_REQUEST,
    PROTOCOL_STATE_DEAD,
} protocol_state;

enum
{
    INITIATOR_REQUEST_VERSION,
    INITIATOR_REQUEST_LENGTH_BIT_COUNT,
    INITIATOR_REQUEST_ID_BIT_COUNT,
    INITIATOR_REQUEST_FLAGS,
};

enum
{
    INITIATOR_RESPONSE_ACCEPT = 0,
    INITIATOR_RESPONSE_REJECT = 1,
};

enum
{
    HEADER_FLAG_TERMINATION = 1,
    HEADER_FLAG_RESPONSE = 2,
};

static const int MAX_NORMAL_MESSAGE_HEADER_LENGTH = 4;


struct streamux_context
{
    int max_chunk_length;
    int max_id;
    protocol_state state;
    int message_header_length;
    int length_mask;
    int length_shift;
    int id_mask;
    int id_shift;
};
typedef struct streamux_context streamux_context;


#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

const char* streamux_version()
{
    return EXPAND_AND_QUOTE(PROJECT_VERSION);
}

static int log_base_2(int value)
{
    const int bit_patterns[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
    const int bit_counts[] = {1, 2, 4, 8, 16};

    int result = 0;
    for(int i = 4; i >= 0; i--)
    {
        if (value & bit_patterns[i])
        {
            value >>= bit_counts[i];
            result |= bit_counts[i];
        } 
    }

    return result;
}

static void transition_protocol_state(streamux_context* context, protocol_state new_state)
{
    // TODO: Make sure transition is valid
    context->state = new_state;
}

static int generate_request_id(streamux_context* context)
{
    return 0; // TODO
}

static streamux_status send_message_chunk(streamux_context* context,
                                          int priority,
                                          const uint8_t* header,
                                          int header_length,
                                          const uint8_t* payload,
                                          int payload_length)
{
    return STREAMUX_STATUS_OK; // TODO
}

static streamux_status send_oob_message(streamux_context* context,
                                        const uint8_t* header,
                                        int header_length)
{
    return send_message_chunk(context, PRIORITY_OOB, header, header_length, NULL, 0);
}


static void fill_normal_message_header(streamux_context* context,
                                       uint8_t* header,
                                       int id,
                                       int payload_length,
                                       int flags)
{
    // TODO: Sanity checks?
    uint32_t value = 0;
    value |= (id && context->id_mask) << context->id_shift;
    value |= (payload_length && context->length_mask) << context->length_shift;
    value |= flags;

    // TODO: endianness
    *((uint32_t*)header) = value;
}

static streamux_status send_initiator_request(streamux_context* context)
{
    uint8_t header[4];
    header[INITIATOR_REQUEST_VERSION] = PROTOCOL_VERSION;
    header[INITIATOR_REQUEST_LENGTH_BIT_COUNT] = log_base_2(context->max_chunk_length);
    header[INITIATOR_REQUEST_ID_BIT_COUNT] = log_base_2(context->max_id);
    header[INITIATOR_REQUEST_FLAGS] = 0; // TODO

    return send_oob_message(context, header, sizeof(header));
}

static streamux_status send_initiator_accept(streamux_context* context)
{
    uint8_t header[1];
    header[0] = INITIATOR_RESPONSE_ACCEPT;

    return send_oob_message(context, header, sizeof(header));
}

static streamux_status send_initiator_reject(streamux_context* context)
{
    uint8_t header[1];
    header[0] = INITIATOR_RESPONSE_REJECT;

    return send_oob_message(context, header, sizeof(header));
}

static streamux_status send_cancel(streamux_context* context, int id)
{
    uint8_t header[MAX_NORMAL_MESSAGE_HEADER_LENGTH];
    fill_normal_message_header(context, header, id, 0, 0);
    return send_oob_message(context, header, context->message_header_length);
}

static streamux_status send_cancel_ack(streamux_context* context, int id)
{
    uint8_t header[MAX_NORMAL_MESSAGE_HEADER_LENGTH];
    fill_normal_message_header(context, header, id, 0, HEADER_FLAG_RESPONSE);
    return send_oob_message(context, header, context->message_header_length);
}

static streamux_status send_ping_request(streamux_context* context, int id)
{
    uint8_t header[MAX_NORMAL_MESSAGE_HEADER_LENGTH];
    fill_normal_message_header(context, header, id, 0, HEADER_FLAG_TERMINATION);
    return send_oob_message(context, header, context->message_header_length);
}

static streamux_status send_ping_response(streamux_context* context, int id)
{
    uint8_t header[MAX_NORMAL_MESSAGE_HEADER_LENGTH];
    fill_normal_message_header(context, header, id, 0, HEADER_FLAG_RESPONSE | HEADER_FLAG_TERMINATION);
    return send_oob_message(context, header, context->message_header_length);
}

static streamux_status send_request(streamux_context* context,
                                    uint8_t* message,
                                    int message_length)
{
    int id = generate_request_id(context);
    // TODO: chunkify
    // TODO: choose ID
    // TODO: build header
    // TODO: send message
    return 0;
}


streamux_status streamux_init(streamux_context* context,
                              int max_chunk_length,
                              int max_id)
{
    context->max_chunk_length = max_chunk_length;
    context->max_id = max_id;

    streamux_status status = STREAMUX_STATUS_OK;

    // call encode_init_request(min version, max version, length bits, id bits)
    // state [decoding init request]

    return status;
}

/*
public streamux_encode_request(context, priority, request_data)
{
    generate id
    while data available:
        choose chunk range
        fill header
        call on_message_data_encoded(priority, header, chunk data)
        loop

    return result_code
}

public streamux_encode_response(context, priority, id, response_data)
{
    while data available:
        choose chunk range
        fill header
        call on_message_data_encoded(priority, header, chunk data)
        loop

    return result_code
}

public streamux_cancel_request(id)
{
    build message
    call on_message_data_encoded(max priority, header, null)
}

public streamux_ping(callback?)
{
    build message
    call on_message_data_encoded(max priority, header, null)
}

internal decode_init_phase_feed(context, data)
{
    while data available:

        if [decoding init request]:
            decode init request, then:
                decide sizing, version
                if session invalid:
                    call encode_init_response(reject)
                    bail: we rejected
                else:
                    fill context
                    call encode_init_response(accept)
                    result_code = normal messages allowed
                    state [decoding init response]
                    loop

        if [decoding init response]:
            decode init response, then:
                if rejected:
                    bail: they rejected
                else:
                    state [decoding header]
                    loop

    return status
}

public streamux_decode_feed(context, data)
{
    result_code = ok

    if in init phase:
        decode_init_phase(context, data)
        loop

    while data available:

        if [decoding header]:
            decode header, then:
                if zero length:
                    if cancel:
                        call on_cancel_request(id)
                        call encode_cancel_ack(id)
                    elif cancel_ack:
                        unlock id
                    elif ping:
                        call encode_pong(id)
                    elif empty_response:
                        call on_response_data_decoded(id, null, true)
                    state [decoding header]
                else:
                    state [decoding payload]
                loop

        if [decoding payload]:
            decode payload piece, then:
                if is response:
                    call on_response_data_decoded(id, payload data, is end)
                else if not discarding:
                    call on_request_data_decoded(id, payload data, is end)
                if end of chunk:
                    state [decoding header]
                    loop

    return result_code
}
*/
