local P = dofile('multiplayer/src/protocol.lua')
local id = string.rep('a', 64)
local s = {mapGroup=3,mapNum=0,x=12,y=8,direction=2,elevation=3,gender=0,ready=1,inviteState=2,invitation=1234,duelPhase=2,duelId=1234}
local stream = P.hello(id) .. P.position(1,s) .. P.position(2,s) .. P.block(1234,1,string.rep('ab',256)) .. P.ack(1234,1)
-- Every possible split, including coalesced and partial TCP messages.
for split=0,#stream do
    local b,a = P.feed('', stream:sub(1,split))
    assert(b and a)
    local b2,c = P.feed(b,stream:sub(split+1))
    assert(b2 == '' and #a + #c == 5)
end
local b,m = P.feed('',stream)
assert(m[1].build == id and m[2].x == 12 and m[3].seq == 2)
assert(m[2].inviteState == 2 and m[2].invitation == 1234)
assert(m[4].kind == 'block' and #m[4].hex == 512 and m[5].kind == 'ack')
-- A nearby wild encounter in the old ROM could publish an id while still
-- idle; keep the players connected and ignore that incomplete invitation.
local stale = P.parse('P 3 3 0 12 8 2 3 0 1 0 1073741826 0 0')
assert(stale and stale.inviteState == 0 and stale.invitation == 0)
local incomplete = P.parse('P 4 3 0 12 8 2 3 0 1 2 0 0 0')
assert(incomplete and incomplete.inviteState == 0 and incomplete.invitation == 0)
local oldState, oldId = s.inviteState, s.invitation
s.inviteState, s.invitation = 0, 1073741826
local normalized = P.parse(P.position(5,s):sub(1,-2))
assert(normalized and normalized.inviteState == 0 and normalized.invitation == 0)
s.inviteState, s.invitation = oldState, oldId
for _,bad in ipairs({'D 1 1 a', 'D 0 1 aa', 'D 1 0 aa', 'D 1 1 '..string.rep('00',257),
    'D 1 1 zz', 'D 2147483648 1 aa', 'A 1 2147483648', 'A -1 1', 'A 1 1 extra', 'HELLO 2 '..id}) do
    assert(not P.parse(bad),bad)
end
for _,bad in ipairs({'HELLO 1 '..id, 'HELLO 2 a', 'P 1 0 0 -1 1 1 0 0 1',
    'P 1 0 0 1024 1 1 0 0 1', 'P 1 0 0 1 1 0 0 0 1', 'P 1 0 0 1 1 1 0 0 2',
    'P 1 0 0 1 1 1 0 0 1 extra', string.rep('x',161)}) do
    assert(not P.parse(bad), bad)
end
for _,bad in ipairs({'P 1 0 0 1 1 1 0 0 1 10 1', 'P 1 0 0 1 1 1 0 0 1 2 0',
    'P 1 0 0 1 1 1 0 0 1 0 1', 'P 1 0 0 1 1 1 0 0 1 2 2147483648'}) do
    assert(not P.parse(bad), bad)
end
-- Verify each position field at its boundary using the current wire format.
for _,field in ipairs({'mapGroup','mapNum','x','y','direction','elevation','gender','ready','inviteState','invitation','duelPhase','duelId'}) do
    local limits={mapGroup=255,mapNum=255,x=1023,y=1023,direction=4,elevation=15,gender=1,ready=1,inviteState=9,invitation=2147483647,duelPhase=4,duelId=2147483647}
    local original=s[field]
    s[field]=limits[field]
    assert(P.parse(P.position(1,s):sub(1,-2)))
    s[field]=limits[field]+1
    assert(not P.parse(P.position(1,s):sub(1,-2)),field)
    s[field]=original
end
assert(not P.feed('',string.rep('x',8193)))
assert(not P.feed('',string.rep('x',601)))
assert(not P.feed('', 'garbage\n'))
print('protocol: all TCP split points and invalid input checks passed')
