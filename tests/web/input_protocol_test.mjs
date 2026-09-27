import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {encode,decode,Type,InputTransport,newer} from '../../web/input_protocol.mjs';
import {protocolKeys} from '../../web/protocol_keys.mjs';
assert.equal(new Set(protocolKeys.map(k=>k[0])).size,105);
assert.equal(new Set(protocolKeys.map(k=>k[1])).size,105);
assert.ok(newer(0,0xffffffff));assert.ok(!newer(0xffffffff,0));assert.ok(!newer(0x80000000,0));
const p={x:.25,y:.75,fence:0x12345678,epoch:0x11223344,focused:true,source:'pen',buttons:['left','middle','extra2'],
    mods:{ctrl:true,shift:true},keys:['A','KeypadEqual'],width:1280,height:720,devicePixelRatio:1.5,
    button:'extra2',down:true,key:'RightShift',horizontal:-2.5,vertical:3.25,text:'hé😀',
    echoUs:0x1122334455667788n,fastBuffered:4096,controlBuffered:8192};
if(process.argv[2]) {
    const vectors=JSON.parse(execFileSync(process.argv[2],['vectors'],{encoding:'utf8'}));
    for(const {type,hex} of vectors) {
        const data=Uint8Array.from(Buffer.from(hex,'hex')).buffer;
        if(type<128)assert.equal(Buffer.from(encode(type,p,0x12345678,0x0102030405060708n)).toString('hex'),hex,`C++/JS type ${type}`);
        if(type>=128||type===13||type===14) {
            const packet=decode(data);assert.equal(packet.type,type);assert.equal(packet.sequence,0x12345678);
            assert.equal(packet.timestampUs,0x0102030405060708n);
            for(let n=0;n<data.byteLength;++n)assert.throws(()=>decode(data.slice(0,n)));
            const mutated=data.slice(0);new Uint8Array(mutated)[0]=2;assert.throws(()=>decode(mutated));
        } else assert.throws(()=>decode(data));
    }
}
for(const text of ['', 'x'.repeat(257), '\0', '\ud800'])assert.throws(()=>encode(Type.text,{...p,text}));
assert.equal(encode(Type.text,{...p,text:'x'.repeat(256)}).byteLength,276);
for(const x of [NaN,Infinity,-.1,1.01])assert.throws(()=>encode(Type.move,{...p,x}));
function channel(fast) {
    return {label:fast?'input-fast-v1':'control-v1',protocol:'render-module-input-v1',ordered:!fast,
        maxRetransmits:fast?0:null,maxPacketLifeTime:null,readyState:'open',bufferedAmount:0,sent:[],
        send(data){this.sent.push(data);},close(){this.readyState='closed';}};
}
let failures=0,resets=0,enabled=false;
const hooks={snapshot:()=>p,viewport:()=>p,reset:()=>++resets,control:v=>{enabled=v;},viewportAccepted:()=>{},failed:()=>++failures};
const input=new InputTransport({sctp:{maxMessageSize:65536}},hooks),control=channel(false),fast=channel(true);
input.attach(control);control.onopen();assert.equal(control.sent.length,1,'already-open channel must not send two hellos');
input.attach(fast);input.receive({type:Type.serverHello,sequence:1,epoch:7,control:true});
assert.equal(resets,1);assert.equal(enabled,false);
input.receive({type:Type.controlState,sequence:2,epoch:7,control:true,enabled:true});assert.ok(enabled);
fast.bufferedAmount=5000;
for(let i=0;i<1000;++i)input.send('mouse_move',{x:i/1000,y:.5,source:'mouse'});
assert.equal(fast.sent.length,0);assert.equal(input.stats.coalesced,999);assert.ok(input.pending);
fast.bufferedAmount=0;fast.onbufferedamountlow();assert.equal(fast.sent.length,1);assert.equal(input.pending,null);
const move=new DataView(fast.sent[0]);assert.equal(move.getUint32(4),1000);assert.equal(move.getUint32(16),1000);
assert.ok(Math.abs(move.getFloat32(20)-.999)<.000001);
input.send('mouse_button',{button:'left',down:true});
const button=new DataView(control.sent.at(-1));assert.equal(button.getUint8(1),Type.button);
assert.equal(button.getUint32(26),1000,'button carries fast fence even when moves were coalesced');
input.receive({type:Type.resync,sequence:3,epoch:8});assert.equal(enabled,false);assert.equal(input.pending,null);
const sent=control.sent.length;input.send('key',{key:'A',down:true});assert.equal(control.sent.length,sent,'no edges before fresh snapshot ack');
input.receive({type:Type.controlState,sequence:4,epoch:8,control:true,enabled:true});
control.bufferedAmount=16384;
for(let i=0;i<64;++i)input.send('key',{key:'A',down:!!(i&1)});
assert.equal(input.queue.length,64);input.send('key',{key:'A',down:false});assert.ok(input.closed);assert.equal(failures,1);
assert.equal(input.queue.length,0);assert.equal(input.pending,null);
// Control channel loss is fatal rather than silently selecting WebSocket input.
const other=new InputTransport({},hooks),c=channel(false);other.attach(c);c.onclose();assert.ok(other.closed);assert.equal(failures,2);
console.log('DataChannel JS: cross-language vectors, strict bounds, wrap, handshake, one-slot motion coalescing, fences, resync, reliable saturation and closure passed');
