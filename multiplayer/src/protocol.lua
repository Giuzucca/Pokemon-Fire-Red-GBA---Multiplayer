-- Framed, bounded transport for the FireRed presence prototype. No save data.
local P = { VERSION = 3, MAX_LINE = 600, MAX_BUFFER = 8192 }

function P.hello(build)
    assert(build:match('^[0-9a-f]+$') and #build == 64)
    return 'HELLO 3 ' .. build .. '\n'
end

function P.position(seq, s)
    local inviteState, invitation = s.inviteState, s.invitation
    -- A restored state or an older ROM can leave an invitation id behind
    -- while the invitation state is idle. Presence must not kill the socket.
    if inviteState == 0 or invitation == 0 then
        inviteState, invitation = 0, 0
    end
    return ('P %d %d %d %d %d %d %d %d %d %d %d %d %d\n'):format(
        seq, s.mapGroup, s.mapNum, s.x, s.y, s.direction,
        s.elevation, s.gender, s.ready, inviteState, invitation, s.duelPhase, s.duelId)
end

function P.block(session, seq, hex)
    return ('D %d %d %s\n'):format(session, seq, hex)
end
function P.ack(session, seq)
    return ('A %d %d\n'):format(session, seq)
end

function P.parse(line)
    if #line > P.MAX_LINE then return nil, 'mensagem longa' end
    local version, build = line:match('^HELLO (%d+) ([0-9a-f]+)$')
    if version then
        if version ~= '3' or #build ~= 64 then return nil, 'versao invalida' end
        return { kind = 'hello', build = build }
    end
    local session, seq, hex = line:match('^D (%d+) (%d+) ([0-9a-f]+)$')
    local kind = 'block'
    if not session then session,seq = line:match('^A (%d+) (%d+)$'); kind='ack' end
    if session then
        session,seq=tonumber(session),tonumber(seq)
        if session < 1 or session > 2147483647 or seq < 1 or seq > 2147483647
            or (hex and (#hex%2 ~= 0 or #hex < 2 or #hex > 512)) then
            return nil,'bloco invalido'
        end
        return {kind=kind,session=session,seq=seq,hex=hex}
    end
    local values = { line:match('^P (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+) (%d+)$') }
    if #values ~= 13 then return nil, 'mensagem invalida' end
    local limits = {2147483647, 255, 255, 1023, 1023, 4, 15, 1, 1, 9, 2147483647, 4, 2147483647}
    for i, v in ipairs(values) do
        v = tonumber(v)
        if not v or v > limits[i] then return nil, 'campo fora do limite' end
        values[i] = v
    end
    if values[6] < 1 then return nil, 'direcao invalida' end
    if values[10] == 0 or values[11] == 0 then
        values[10], values[11] = 0, 0
    end
    if (values[12] == 0) ~= (values[13] == 0) then return nil, 'duelo invalido' end
    return {kind='position', seq=values[1], mapGroup=values[2], mapNum=values[3],
        x=values[4], y=values[5], direction=values[6], elevation=values[7],
        gender=values[8], ready=values[9], inviteState=values[10], invitation=values[11],
        duelPhase=values[12],duelId=values[13]}
end

function P.feed(buffer, chunk)
    if type(chunk) ~= 'string' or #buffer + #chunk > P.MAX_BUFFER then
        return nil, nil, 'buffer excedido'
    end
    buffer = buffer .. chunk
    local messages = {}
    while true do
        local eol = buffer:find('\n', 1, true)
        if not eol then break end
        local msg, err = P.parse(buffer:sub(1, eol - 1))
        if not msg then return nil, nil, err end
        messages[#messages + 1] = msg
        buffer = buffer:sub(eol + 1)
    end
    if #buffer > P.MAX_LINE then return nil, nil, 'linha sem terminador' end
    return buffer, messages
end

return P
