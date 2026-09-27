import {keyByName} from './protocol_keys.mjs';
export const Type = Object.freeze({hello:1, move:2, button:3, wheel:4, key:5, text:6, focus:7, snapshot:8,
    viewport:9, acquire:10, release:11, stats:12, ping:13, pong:14, serverHello:128, viewportAccepted:129,
    controlState:130, resync:131, streamState:132, error:133});
export const MAX_PACKET = 512;
const buttons = ['left','right','middle','extra1','extra2'], sources = ['mouse','touch','pen'];
export function newer(a,b) { const delta=(a-b)>>>0; return delta!==0 && delta<0x80000000; }
const nowUs = () => BigInt(Math.floor(performance.now()*1000));
const modBits = m => (m?.ctrl?1:0)|(m?.shift?2:0)|(m?.alt?4:0)|(m?.super?8:0);
export function encode(type,p={},sequence=0,timestamp=nowUs()) {
    const bytes=new Uint8Array(MAX_PACKET), view=new DataView(bytes.buffer); let offset=16;
    const u8=n=>{if(!Number.isInteger(n)||n<0||n>255)throw Error('u8');view.setUint8(offset++,n);};
    const u16=n=>{if(!Number.isInteger(n)||n<0||n>65535)throw Error('u16');view.setUint16(offset,n);offset+=2;};
    const u32=n=>{view.setUint32(offset,n>>>0);offset+=4;};
    const u64=n=>{view.setBigUint64(offset,BigInt(n));offset+=8;};
    const f32=(n,min,max)=>{if(!Number.isFinite(n)||n<min||n>max)throw Error('float');view.setFloat32(offset,n);offset+=4;};
    const position=()=>{f32(p.x,0,1);f32(p.y,0,1);u32(p.fence);u32(p.epoch);};
    const viewport=()=>{u16(p.width);u16(p.height);f32(p.devicePixelRatio,.25,8);};
    const snapshot=()=>{
        position();u8(sources.indexOf(p.source||'mouse'));
        u8((p.buttons||[]).reduce((mask,b)=>{const id=buttons.indexOf(b);if(id<0)throw Error('button');return mask|(1<<id);},0));
        u8(modBits(p.mods));u8(p.focused?1:0);const mask=new Uint8Array(16);
        for(const key of p.keys||[]) { const id=keyByName.get(key);if(!id)throw Error('key');mask[id>>3]|=1<<(id&7); }
        bytes.set(mask,offset);offset+=16;
    };
    switch(type) {
    case Type.hello:u16(0);u32(1);viewport();snapshot();break;
    case Type.move:u32(p.fence);f32(p.x,0,1);f32(p.y,0,1);u8(sources.indexOf(p.source||'mouse'));u32(p.epoch);break;
    case Type.button:u8(buttons.indexOf(p.button));u8(p.down?1:0);position();u8(sources.indexOf(p.source||'mouse'));break;
    case Type.wheel:f32(p.horizontal,-1000,1000);f32(p.vertical,-1000,1000);position();u8(sources.indexOf(p.source||'mouse'));break;
    case Type.key:{const id=keyByName.get(p.key);if(!id)throw Error('key');u16(id);u8(p.down?1:0);u8(modBits(p.mods));u32(p.epoch);break;}
    case Type.text:{u32(p.epoch);const text=new TextEncoder().encode(p.text);
        if(!text.length||text.length>256||p.text.includes('\0')||!p.text.isWellFormed())throw Error('text');
        bytes.set(text,offset);offset+=text.length;break;}
    case Type.focus:u8(p.focused?1:0);u32(p.epoch);break;
    case Type.snapshot:snapshot();break;
    case Type.viewport:viewport();break;
    case Type.acquire:case Type.release:break;
    case Type.stats:u32(p.fastBuffered);u32(p.controlBuffered);break;
    case Type.ping:case Type.pong:u64(p.echoUs);break;
    default:throw Error('type');
    }
    view.setUint8(0,1);view.setUint8(1,type);view.setUint16(2,offset-16);view.setUint32(4,sequence>>>0);view.setBigUint64(8,timestamp);
    return bytes.slice(0,offset).buffer;
}
export function decode(data) {
    if(!(data instanceof ArrayBuffer)||data.byteLength<16||data.byteLength>MAX_PACKET)throw Error('size');
    const view=new DataView(data), type=view.getUint8(1);let offset=16;
    if(view.getUint8(0)!==1||view.getUint16(2)!==data.byteLength-16)throw Error('header');
    const u8=()=>view.getUint8(offset++),u16=()=>{const n=view.getUint16(offset);offset+=2;return n;},
        u32=()=>{const n=view.getUint32(offset);offset+=4;return n;},
        u64=()=>{const n=view.getBigUint64(offset);offset+=8;return n;},bool=()=>{const n=u8();if(n>1)throw Error('boolean');return !!n;};
    const p={type,sequence:view.getUint32(4),timestampUs:view.getBigUint64(8)};
    switch(type) {
    case Type.serverHello:p.minor=u16();p.capabilities=u32();p.epoch=u32();p.control=bool();p.maxPacket=u16();
        if(!(p.capabilities&1)||p.maxPacket<16||p.maxPacket>MAX_PACKET)throw Error('capabilities');break;
    case Type.viewportAccepted:p.width=u16();p.height=u16();if(!p.width||!p.height||p.width>2048||p.height>2048)throw Error('viewport');break;
    case Type.controlState:p.control=bool();p.enabled=bool();p.epoch=u32();if(p.enabled&&!p.control)throw Error('role');break;
    case Type.resync:p.epoch=u32();break;
    case Type.streamState:p.code=u16();if(p.code>2)throw Error('state');break;
    case Type.error:p.code=u16();if(p.code<1||p.code>8)throw Error('error');break;
    case Type.ping:case Type.pong:p.echoUs=u64();break;
    default:throw Error('type');
    }
    if(offset!==data.byteLength)throw Error('length');return p;
}
// Per-PeerConnection, deliberately no WebSocket fallback. One replaceable fast move;
// at most 64 reliable packets / 32 KiB plus <=16 KiB handed to the browser transport.
export class InputTransport {
    constructor(peer,hooks) {
        this.peer=peer;this.hooks=hooks;this.fast=null;this.control=null;this.fastSequence=0;this.sequence=0;
        this.epoch=0;this.ready=false;this.enabled=false;this.role=false;this.closed=false;this.pending=null;this.queue=[];
        this.stats={transport:'datachannel',coalesced:0,fastSent:0,controlSent:0,resyncs:0};
        this.lastResponse=performance.now();
    }
    attach(channel) {
        const fast=channel.label==='input-fast-v1';
        if(this.closed||(!fast&&channel.label!=='control-v1')||(fast?this.fast:this.control)||
           channel.protocol!=='render-module-input-v1'||channel.ordered===fast||
           (fast?channel.maxRetransmits!==0:channel.maxRetransmits!==null)||channel.maxPacketLifeTime!==null) {
            channel.close();this.fail();return;
        }
        if(fast)this.fast=channel;else this.control=channel;
        channel.binaryType='arraybuffer';channel.bufferedAmountLowThreshold=fast?1024:4096;
        channel.onbufferedamountlow=()=>fast?this.flushFast():this.drain();
        channel.onclose=channel.onerror=()=>this.fail();
        channel.onmessage=event=>{try{if(fast)throw Error('fast is client-only');this.receive(decode(event.data));}catch{this.fail();}};
        let opened=false;
        const open=()=>{if(this.closed||opened)return;opened=true;
            if(!fast)this.reliable(Type.hello,{...this.hooks.viewport(),...this.state()});else this.flushFast();};
        channel.onopen=open;if(channel.readyState==='open')open();
    }
    state() {return {...this.hooks.snapshot(),fence:this.fastSequence,epoch:this.epoch};}
    maximum() {const n=this.peer.sctp?.maxMessageSize;return Math.min(MAX_PACKET,n>0?n:MAX_PACKET);}
    reliable(type,p={}) {
        if(this.closed||this.control?.readyState!=='open')return false;
        try {
            const data=encode(type,p,++this.sequence);if(data.byteLength>this.maximum()||this.queue.length>=64)throw Error('queue');
            this.queue.push(data);this.drain();return true;
        } catch {this.fail();return false;}
    }
    drain() {
        if(this.closed||this.control?.readyState!=='open')return;
        try {while(this.queue.length&&this.control.bufferedAmount+this.queue[0].byteLength<=16384) {
            this.control.send(this.queue.shift());++this.stats.controlSent;
        }}catch{this.fail();}
    }
    flushFast() {
        if(this.closed||!this.enabled||!this.pending||this.fast?.readyState!=='open'||this.fast.bufferedAmount>4096)return;
        try {const data=this.pending;if(data.byteLength>this.maximum())throw Error('maximum');this.fast.send(data);this.pending=null;++this.stats.fastSent;}
        catch{this.fail();}
    }
    send(type,p={}) {
        if(!this.ready||this.closed)return false;
        if(type==='snapshot')return this.reliable(Type.snapshot,this.state());
        if(type==='viewport')return this.role&&this.reliable(Type.viewport,p);
        if(type==='acquire_control')return this.reliable(Type.acquire);
        if(type==='release_control')return this.reliable(Type.release);
        if(!this.enabled)return false;
        if(type==='mouse_move') {
            this.fastSequence=(this.fastSequence+1)>>>0;
            if(this.pending)++this.stats.coalesced;
            this.pending=encode(Type.move,{...p,fence:this.fastSequence,epoch:this.epoch},this.fastSequence);this.flushFast();return true;
        }
        if(type==='release_all')return true; // Focus(false) already performs emergency release + resync.
        if(type==='focus'&&p.focused)return this.reliable(Type.snapshot,this.state());
        const wire={mouse_button:Type.button,wheel:Type.wheel,key:Type.key,text:Type.text,focus:Type.focus}[type];
        if(!wire)return false;
        return this.reliable(wire,{...this.state(),...p});
    }
    reset(epoch,role) {
        this.epoch=epoch;this.role=role;this.enabled=false;this.pending=null;this.queue=[];
        this.hooks.reset();this.hooks.control(false);this.reliable(Type.snapshot,this.state());
    }
    receive(p) {
        if(this.closed)return;
        if(this.serverSequence!==undefined&&!newer(p.sequence,this.serverSequence))throw Error('sequence');
        this.serverSequence=p.sequence;this.lastResponse=performance.now();
        if(p.type===Type.serverHello) {
            if(this.ready)throw Error('duplicate hello');this.ready=true;this.reset(p.epoch,p.control);return;
        }
        if(!this.ready)throw Error('handshake');
        if(p.type===Type.controlState) {
            if(p.epoch!==this.epoch||p.control!==this.role) {this.reset(p.epoch,p.control);return;}
            if(this.enabled!==p.enabled) {this.enabled=p.enabled;this.hooks.control(p.enabled&&p.control);}
        } else if(p.type===Type.resync) {++this.stats.resyncs;this.reset(p.epoch,this.role);}
        else if(p.type===Type.viewportAccepted)this.hooks.viewportAccepted(p);
        else if(p.type===Type.ping)this.reliable(Type.pong,{echoUs:p.echoUs});
        else if(p.type===Type.pong&&p.echoUs===this.pingUs){this.stats.rttMs=Number(nowUs()-p.echoUs)/1000;this.pingUs=undefined;}
        else if(p.type===Type.streamState)this.stats.streamState=p.code;
        else if(p.type===Type.error)this.fail();
    }
    tick() {
        this.drain();this.flushFast();
        Object.assign(this.stats,{ready:this.ready,enabled:this.enabled,role:this.role,
            fastState:this.fast?.readyState||'absent',controlState:this.control?.readyState||'absent',
            fastBuffered:this.fast?.bufferedAmount||0,controlBuffered:this.control?.bufferedAmount||0,
            reliablePending:this.queue.length,fastPending:this.pending?1:0,maxMessageSize:this.peer.sctp?.maxMessageSize});
        if(this.ready&&performance.now()-this.lastResponse>5000){this.fail();return;}
        if(this.ready&&!this.queue.length) {
            if(this.pingUs===undefined||nowUs()-this.pingUs>5000000n) {
                this.pingUs=nowUs();this.reliable(Type.ping,{echoUs:this.pingUs});
            }
            this.reliable(Type.stats,this.stats);
        }
    }
    fail() {if(!this.closed){this.close();this.hooks.reset();this.hooks.control(false);this.hooks.failed();}}
    close() {
        if(this.closed)return;this.closed=true;this.enabled=false;this.ready=false;this.pending=null;this.queue=[];
        for(const c of [this.fast,this.control])if(c){c.onopen=c.onclose=c.onerror=c.onmessage=c.onbufferedamountlow=null;c.close();}
    }
}
