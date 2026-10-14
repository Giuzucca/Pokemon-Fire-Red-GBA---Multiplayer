-- Generated for FireRed-MP. Change only ip/port for your private network.
local CONFIG = {role="host", ip="127.0.0.1", port=4096, mailbox=0x0203e924, sendBuffer=0x02021618, recvBuffer=0x02021118, build="c94da68cd0fb29f95fc0233c01b7c60609e96aa4b02f37c2ede2e6e1b25c04bd", crc32="b6ec4e1b"}
local Protocol = (function()
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

end)()
-- CONFIG and Protocol are prepended by package.py; this is a standalone mGBA script.
if FRMP_STOP then FRMP_STOP() end
local address = CONFIG.mailbox
local listener, peer
local input, output = '', ''
local authenticated, stopped = false, false
local sequence, received, frame = 0, -1, 0
-- mGBA does not restore Lua variables when a save state is loaded. Keep the
-- game-side frame so a restored mailbox can invalidate old TCP sequence data.
local lastGameFrame
local lastReceive = os.time()
-- Give slow emulators and fast-forward scheduling room to catch up before
-- treating an otherwise-open TCP peer as disconnected.
local TCP_IDLE_TIMEOUT_SECONDS = 30
local frameCallback, resetCallback, shutdownCallback
local blocks = {}
local rxSession, rxBlock, txSession, txBlock = 0, 0, 0, 0
local MAX_PENDING_BATTLE_BLOCKS = 30

local function log(s) console:log('[FireRed MP] ' .. s) end
local function validMailbox()
    return emu and emu:read32(address) == 0x504D5246
        and emu:read16(address+4) == 3 and emu:read16(address+6) == 84
end
local function clearRemote()
    if validMailbox() then
        emu:write8(address+12,0)
        emu:write8(address+28,0)
        emu:write32(address+48,0)
        emu:write8(address+53,0)
        emu:write32(address+60,0)
        emu:write8(address+75,0)
    end
end
local function drop(reason)
    if peer then peer:close() end
    peer = nil
    authenticated, input, output = false, '', ''
    received, sequence = -1, 0
    blocks = {}
    rxSession,rxBlock,txSession,txBlock = 0,0,0,0
    lastGameFrame = nil
    clearRemote()
    collectgarbage('collect')
    if reason then log(reason) end
end

local function observeGameFrame()
    if not validMailbox() then return false end
    local current = emu:read32(address+8)
    if lastGameFrame ~= nil then
        -- The game-side counter is monotonic while the ROM is running. A
        -- decrease means an earlier state was restored. A large forward jump
        -- covers a state made in a previous session while ignoring normal
        -- one-frame callback ordering differences.
        local backward = current < lastGameFrame
        local forward = current > lastGameFrame and current - lastGameFrame > 8
        if backward or forward then
            drop('Save state carregado; sessao TCP reiniciada.')
            if CONFIG.role == 'client' then
                log('Tentando reconectar automaticamente ao host.')
                FRMP_CONNECT()
            else
                log('Host pronto; aguardando a reconexao do cliente.')
            end
            lastGameFrame = current
            return true
        end
    end
    lastGameFrame = current
    return false
end

function FRMP_STOP()
    if stopped then return end
    stopped = true
    drop()
    if listener then listener:close() end
    listener = nil
    if frameCallback then callbacks:remove(frameCallback) end
    if resetCallback then callbacks:remove(resetCallback) end
    if shutdownCallback then callbacks:remove(shutdownCallback) end
    collectgarbage('collect')
end

local function attach(s)
    peer = s
    authenticated, input, output = false, '', Protocol.hello(CONFIG.build)
    received, sequence, lastReceive = -1, 0, os.time()
end

local function process(msg)
    if msg.kind == 'hello' then
        if authenticated or msg.build ~= CONFIG.build then
            drop('Conexao recusada: ROM ou protocolo diferente.')
            return false
        end
        authenticated = true
        log('Amigo conectado. Entre no mesmo mapa para se encontrar.')
    elseif msg.kind == 'block' or msg.kind == 'ack' then
        if not authenticated or not validMailbox() then drop('Bloco sem sessao.'); return false end
        local duel = emu:read32(address+56)
        if msg.kind == 'ack' then
            if msg.session == duel then
                if msg.seq > emu:read32(address+64) then drop('ACK invalido.'); return false end
                if msg.seq == emu:read32(address+64) then emu:write32(address+68,msg.seq) end
            end
        elseif msg.session == emu:read32(address+44) and emu:read8(address+52) >= 4
            and emu:read8(address+52) <= 7 and not (msg.session == duel and emu:read8(address+74) == 4) then
            if rxSession ~= msg.session then rxSession,rxBlock=msg.session,0; blocks={} end
            if msg.seq ~= rxBlock+1 or #blocks >= MAX_PENDING_BATTLE_BLOCKS then drop('Sequencia de blocos invalida.'); return false end
            rxBlock=msg.seq
            blocks[#blocks+1]=msg
        end
    else
        if not authenticated or msg.seq <= received then
            drop('Conexao recusada: mensagem fora de ordem.')
            return false
        end
        received = msg.seq
        if validMailbox() then
            local a = address+28
            emu:write8(a,0)
            emu:write8(a+1,msg.mapGroup)
            emu:write8(a+2,msg.mapNum)
            emu:write8(a+3,msg.direction)
            emu:write16(a+4,msg.x)
            emu:write16(a+6,msg.y)
            emu:write8(a+8,msg.elevation)
            emu:write8(a+9,msg.gender)
            emu:write32(address+48,msg.invitation)
            emu:write8(address+53,msg.inviteState)
            emu:write8(address+54,CONFIG.role == 'host' and 1 or 2)
            emu:write32(address+60,msg.duelId)
            emu:write8(address+75,msg.duelPhase)
            emu:write8(a,msg.ready)
            emu:write32(address+40,msg.seq)
            emu:write8(address+12,1)
        end
    end
    lastReceive = os.time()
    return true
end

local function readLocal()
    local a = address+16
    if not validMailbox() then
        return {ready=0,mapGroup=0,mapNum=0,x=0,y=0,direction=1,elevation=0,gender=0,inviteState=0,invitation=0,duelPhase=0,duelId=0}
    end
    return {ready=emu:read8(a),mapGroup=emu:read8(a+1),mapNum=emu:read8(a+2),
        direction=math.max(1,emu:read8(a+3)),x=emu:read16(a+4),y=emu:read16(a+6),
        elevation=emu:read8(a+8),gender=emu:read8(a+9),
        inviteState=emu:read8(address+52),invitation=emu:read32(address+44),
        duelPhase=emu:read8(address+74),duelId=emu:read32(address+56)}
end

local function pumpBattle()
    if not validMailbox() then return end
    local phase,duel = emu:read8(address+74),emu:read32(address+56)
    if phase < 1 or phase > 3 then return end
    if txSession ~= duel then txSession,txBlock=duel,0 end
    local seq,size=emu:read32(address+64),emu:read16(address+72)
    if seq ~= txBlock then
        if size < 1 or size > 256 or seq ~= txBlock+1 then drop('Bloco local invalido.'); return end
        local hex={}
        for i=0,size-1 do hex[#hex+1]=('%02x'):format(emu:read8(CONFIG.sendBuffer+i)) end
        output=output..Protocol.block(duel,seq,table.concat(hex))
        txBlock=seq
    end
    local other = CONFIG.role == 'host' and 1 or 0
    local bit = 2^other
    local mask=emu:read8(address+76)
    local msg=blocks[1]
    if msg and msg.session == duel and math.floor(mask/bit)%2 == 0 then
        local dest=CONFIG.recvBuffer+other*256
        -- Zero the entire native slot so short startup blocks cannot expose stale data.
        for i=0,255 do emu:write8(dest+i,0) end
        for i=1,#msg.hex,2 do emu:write8(dest+(i-1)/2,tonumber(msg.hex:sub(i,i+1),16)) end
        emu:write16(address+80+other*2,#msg.hex/2)
        emu:write8(address+76,mask+bit)
        output=output..Protocol.ack(duel,msg.seq)
        table.remove(blocks,1)
    end
end

local function tick()
    if stopped then return end
    frame = frame+1
    if observeGameFrame() then return end
    if not peer and listener and listener:hasdata() then
        local s,err = listener:accept()
        if s then attach(s) else log('Falha ao aceitar: '..tostring(err)) end
    end
    if not peer then return end
    -- Bounded work per emulator frame; feed handles partial/coalesced TCP reads.
    for i=1,4 do
        if not peer:hasdata() then break end
        local data,err = peer:receive(1024)
        if not data or #data == 0 then drop('Amigo desconectado: '..tostring(err)); return end
        local pending,messages,parseError = Protocol.feed(input,data)
        if not pending then drop('Protocolo invalido: '..parseError); return end
        input = pending
        for _,msg in ipairs(messages) do if not process(msg) then return end end
    end
    if os.time()-lastReceive > TCP_IDLE_TIMEOUT_SECONDS then drop('Conexao expirou. Execute FRMP_CONNECT() para reconectar.'); return end
    if authenticated then pumpBattle(); if not peer then return end end
    if authenticated and frame%4 == 0 then
        sequence = sequence+1
        if sequence > 2147483647 then drop('Reinicie a conexao.'); return end
        output = output .. Protocol.position(sequence,readLocal())
    end
    if #output > 8192 then drop('Conexao lenta: fila de envio excedida.'); return end
    if #output > 0 then
        local sent,err = peer:send(output)
        if sent then output = output:sub(sent+1)
        elseif err ~= socket.ERRORS.AGAIN then drop('Falha no envio: '..tostring(err)) end
    end
end

function FRMP_CONNECT()
    if stopped then log('Recarregue o script.'); return end
    if CONFIG.role ~= 'client' then log('Este script hospeda a sessao.'); return end
    drop()
    local s = socket:tcp()
    local ok,err = s:connect(CONFIG.ip,CONFIG.port)
    if ok == nil then s:close(); log('Nao conectou: '..tostring(err)..'. Confira IP/porta e tente FRMP_CONNECT().'); return end
    attach(s)
end

-- Use this after loading a state created before this script started observing
-- the mailbox frame (for example, a state from another session). The host
-- keeps listening; the client opens a fresh TCP session.
function FRMP_STATE_RECOVER()
    if stopped then log('Recarregue o script.'); return end
    drop('Recuperando a sessao apos carregar save state.')
    if CONFIG.role == 'client' then FRMP_CONNECT() else log('Host pronto; aguardando FRMP_CONNECT() do cliente.') end
end

assert(emu, 'Abra a ROM FireRed-MP antes de carregar o script.')
local crc = emu:checksum(C.CHECKSUM.CRC32):gsub('.',function(c) return ('%02x'):format(c:byte()) end)
assert(crc == CONFIG.crc32, 'ROM diferente da compilacao deste script. Use o pacote completo correspondente.')
clearRemote()
frameCallback = callbacks:add('frame',tick)
resetCallback = callbacks:add('reset',function() drop('ROM reiniciada; reconecte o cliente.') end)
shutdownCallback = callbacks:add('shutdown',FRMP_STOP)
if CONFIG.role == 'host' then
    listener = socket:tcp()
    local ok,err = listener:bind(CONFIG.ip,CONFIG.port)
    if ok == nil then FRMP_STOP(); error('Nao foi possivel hospedar: '..tostring(err)) end
    ok,err = listener:listen(1)
    if ok == nil then FRMP_STOP(); error('Falha ao escutar: '..tostring(err)) end
    log('Aguardando amigo em '..CONFIG.ip..':'..CONFIG.port)
else
    FRMP_CONNECT()
end
