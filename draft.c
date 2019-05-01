callback on_message_data_encoded(priority, header, data)
callback on_request_data_decoded(id, payload data, is end)
callback on_response_data_decoded(id, payload data, is end)
callback on_cancel_request(id)

internal encode_message_header(context, id, data length, is response, is terminator)
{

}

internal encode_request(context, id, data length, is terminator)
{

}
 
internal encode_init_request(context, min version, max version, length bits, id bits)
{
    build message
    call on_message_data_encoded(max priority, header, null)
}

internal encode_init_response(context, is accept)
{
    build message
    call on_message_data_encoded(max priority, header, null)
}

internal encode_cancel_ack()
{
    build message
    call on_message_data_encoded(normal priority, header, null)
}

internal encode_pong()
{
    build message
    call on_message_data_encoded(max priority, header, null)
}

public streamux_init(context, length bits, id bits)
{
    fill context
    call encode_init_request(min version, max version, length bits, id bits)
    state [decoding init request]

    return result_code
}

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
