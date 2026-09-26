import {chromium, firefox} from 'playwright';
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {setTimeout as sleep} from 'node:timers/promises';
const [fixture, artifact] = process.argv.slice(2);
await fs.mkdir(artifact, {recursive:true});
const statePath=path.join(artifact,'state.json'); await fs.rm(statePath,{force:true});
const env={...process.env,RENDER_MODULE_TEST_WEBRTC:'1'}; delete env.DISPLAY; delete env.WAYLAND_DISPLAY;
const server=spawn(fixture,[statePath],{env,stdio:['ignore','pipe','pipe']});
let log='',exitCode=null,browser,page;
const signalingTrace=[];
server.stdout.on('data', b=>{log=(log+b).slice(-65536);});
server.stderr.on('data', b=>{log=(log+b).slice(-65536);});
server.on('exit', c=>{exitCode=c;});
const token='0123456789abcdef0123456789abcdef', errors=[];
async function until(fn,label,ms=20000) {
    const end=Date.now()+ms;
    do { const value=await fn(); if(value) return value; await sleep(50); } while(Date.now()<end);
    throw Error(`Timeout: ${label}`);
}
async function state() { return JSON.parse(await fs.readFile(statePath,'utf8')); }
async function decoded(p) {
    return p.evaluate(()=>{const v=document.querySelector('#video'); return v.videoWidth>0 && v.videoHeight>0 && Number(v.dataset.framesDecoded)>2;});
}
try {
    const origin=await until(()=>log.match(/HTTP : (http:\/\/127\.0\.0\.1:\d+)\//)?.[1],'startup');
    const metrics=async()=> (await fetch(origin+'/api/version',{headers:{Authorization:`Bearer ${token}`}})).json();
    assert.equal((await fetch(origin+'/api/version')).status,401);
    const isFirefox=process.env.RENDER_MODULE_TEST_BROWSER==='firefox';
    browser=await (isFirefox?firefox:chromium).launch({headless:true,...(!isFirefox?{args:['--no-sandbox','--disable-gpu','--no-proxy-server']}: {})});
    const context=await browser.newContext({viewport:{width:1400,height:900}});
    await context.addInitScript(()=>{
        const Native=window.RTCPeerConnection; window.testPeers=[]; window.testDataChannels=0; window.testMediaTrace=[];
        window.RTCPeerConnection=new Proxy(Native,{construct(Target,args){
            const pc=new Target(...args); window.testPeers.push(pc);
            pc.addEventListener('track',()=>{window.testTrackSeen=true;});
            for(const method of ['setRemoteDescription','createAnswer','setLocalDescription']) {
                const native=pc[method].bind(pc);
                pc[method]=async(...a)=>{try {const result=await native(...a);
                    window.testMediaTrace.push({method,codec:result?.sdp?.split('\r\n').filter(l=>l.startsWith('a=fmtp:')||l.startsWith('m='))});
                    if(window.testMediaTrace.length>32)window.testMediaTrace.shift();return result;
                }catch(e){window.testMediaTrace.push({method,error:e.name});throw e;}};
            }
            pc.createDataChannel=()=>{++window.testDataChannels; throw Error('DataChannel outside Phase 7');}; return pc;
        }});
    });
    page=await context.newPage(); page.on('pageerror',e=>errors.push(String(e)));
    page.on('websocket', ws=>{
        for(const event of ['framesent','framereceived']) ws.on(event,({payload})=>{
            if(typeof payload!=='string') return;
            try { const m=JSON.parse(payload);
                if(['hello','offer','answer','ice-candidate','ice-complete','webrtc-state','error'].includes(m.type)) {
                    signalingTrace.push({event,type:m.type,state:m.state,code:m.code,
                        codec:m.sdp?.split('\r\n').filter(l=>l.startsWith('a=fmtp:') || l.startsWith('m=') || l==='a=recvonly'),
                        candidate:m.type==='ice-candidate'?{mid:m.mid,empty:!m.candidate,component:m.candidate?.split(' ')[1],
                            transport:m.candidate?.split(' ')[2],type:m.candidate?.split(' ')[7]}:undefined});
                    if(signalingTrace.length>100) signalingTrace.shift();
                }
            } catch { /* Never persist complete SDP or ICE credentials. */ }
        });
    });
    await page.goto(origin); await page.locator('#token').fill(token); await page.locator('#login button').click();
    await until(()=>decoded(page),'actual H264 video decode');
    await page.waitForFunction(()=>document.querySelector('#lease').dataset.control==='true');
    const cookie=(await context.cookies()).find(c=>c.name==='rm_auth'); assert.ok(cookie.httpOnly && cookie.sameSite==='Strict');
    assert.ok(!log.includes(token));
    const codec=await page.evaluate(()=>{
        const pc=window.testPeers.at(-1);
        return {offer:pc.remoteDescription.sdp.split('\r\n').filter(l=>l.startsWith('a=fmtp:')),
            answer:pc.localDescription.sdp.split('\r\n').filter(l=>l.startsWith('a=fmtp:')),
            application:pc.remoteDescription.sdp.includes('m=application'),audio:pc.remoteDescription.sdp.includes('m=audio')};
    });
    assert.ok(codec.offer.some(l=>l.includes('profile-level-id=42c028') && l.includes('packetization-mode=1')));
    assert.equal(codec.application,false); assert.equal(codec.audio,false);
    assert.equal((await metrics()).jpegEncoded,0);
    assert.equal(await page.locator('#picture').getAttribute('data-frame-id'),null);
    await until(async()=>{try {return (await state()).frame>0;} catch{return false;}},'fixture state');
    const colors=await page.evaluate(({canvas,view})=>{
        const v=document.querySelector('#video'),c=document.createElement('canvas'); c.width=v.videoWidth;c.height=v.videoHeight;
        const ctx=c.getContext('2d'); ctx.drawImage(v,0,0);
        return [canvas,view,[40,30]].map(([x,y])=>[...ctx.getImageData(Math.floor(x),Math.floor(y),1,1).data]);
    },await state());
    assert.ok(colors[0][0]>180 && colors[0][1]<80,'NanoVG red decoded');
    assert.ok(colors[1][1]>180 && colors[1][0]<80,'Magnum green decoded');
    assert.ok(colors[2][0]>100,'ImGui decoded');
    async function screen(point) {
        return page.evaluate(([x,y])=>{const v=document.querySelector('#video'),r=v.getBoundingClientRect();
            return {x:r.left+x/v.videoWidth*r.width,y:r.top+y/v.videoHeight*r.height};},point);
    }
    async function click(name) {const p=await screen((await state())[name]);await page.mouse.click(p.x,p.y);}
    await click('button'); await until(async()=>(await state()).clicks===1,'WebSocket ImGui click');
    await click('edit'); await page.keyboard.insertText('Phase 7 äöü ß 日本 🙂');
    await until(async()=>(await state()).text==='Phase 7 äöü ß 日本 🙂','WebSocket Unicode');
    const camera=(await state()).camera,p=await screen((await state()).view);
    await page.mouse.move(p.x,p.y);await page.mouse.down({button:'right'});await sleep(100);
    await page.mouse.move(p.x+65,p.y+35,{steps:8});await sleep(150);await page.mouse.up({button:'right'});
    await until(async()=>(await state()).camera.some((n,i)=>Math.abs(n-camera[i])>.1),'WebSocket orbit');
    const session=await page.locator('#connection').getAttribute('data-session');
    for(const [width,height] of [[1280,720],[1600,900],[1920,1080],[1280,720]]) {
        await page.locator('#display').evaluate((d,[w,h])=>{d.style.flex='none';d.style.width=w+'px';d.style.height=h+'px';},[width,height]);
        await until(()=>page.evaluate(([w,h])=>{const v=document.querySelector('#video');return v.videoWidth===w&&v.videoHeight===h;},[width,height]),`resize ${width}x${height}`);
        assert.equal(await page.locator('#connection').getAttribute('data-session'),session,'no reconnect for resize');
        assert.equal(await page.locator('#video').getAttribute('data-offers'),'1','no renegotiation for resize');
    }
    const watchContext=await browser.newContext({viewport:{width:500,height:900}}),watch=await watchContext.newPage();
    watch.on('pageerror',e=>errors.push(String(e)));
    await watch.goto(origin);await watch.locator('#token').fill(token);await watch.locator('#login button').click();
    await until(()=>decoded(watch),'second browser decoding');
    assert.equal(await watch.locator('#lease').getAttribute('data-control'),'false');
    let m=await metrics();assert.equal(m.webrtc_connected,2);assert.equal(new Set(m.webrtc_clients.map(c=>c.ssrc)).size,2);
    assert.ok(m.webrtc_clients.every(c=>c.pending<=1));
    const rejected=m.inputRejected, clicks=(await state()).clicks;
    await watch.evaluate(()=>new Promise(resolve=>{
        const url=new URL('/api/ws',location.href);url.protocol='ws:';const ws=new WebSocket(url);
        ws.onmessage=e=>{if(typeof e.data!=='string')return;const m=JSON.parse(e.data);
            if(m.type==='welcome')ws.send(JSON.stringify({v:1,seq:1,type:'mouse_button',button:'left',down:true}));
            if(m.type==='view_only'){ws.close();resolve();}};
    }));
    await until(async()=>(await metrics()).inputRejected>rejected,'server rejects viewer input');
    assert.equal((await state()).clicks,clicks);
    const ticks=Number(execFileSync('getconf',['CLK_TCK'],{encoding:'utf8'}));
    const cpu=async()=>{const s=await fs.readFile(`/proc/${server.pid}/stat`,'utf8'),p=s.slice(s.lastIndexOf(')')+2).split(' ');return Number(p[11])+Number(p[12]);};
    const begin=await metrics(),c0=await cpu(),t0=performance.now(); await sleep(3000);
    const end=await metrics(),seconds=(performance.now()-t0)/1000;
    const measurement={seconds,viewers:2,encoderFps:(end.encoder.frames-begin.encoder.frames)/seconds,
        rtpBitsPerSecond:(end.rtp_bytes_sent-begin.rtp_bytes_sent)*8/seconds,serverCpuPercent:(await cpu()-c0)/ticks/seconds*100};
    assert.ok(measurement.encoderFps>5 && measurement.encoderFps<=22,'one encode stream, not per-viewer encoders');
    if(process.env.RENDER_MODULE_TEST_SLOW_VIEWER) {
        const slowId=await watch.locator('#connection').getAttribute('data-session');
        assert.ok(end.webrtc_clients.find(c=>c.session===slowId).video_frames_rejected>10,'slow send worker exercises dependent-frame rejection');
        assert.ok(end.webrtc_clients.every(c=>c.pending<=1),'per-viewer queue remains bounded');
        assert.ok(JSON.parse(await page.locator('#video').getAttribute('data-stats')).framesPerSecond>=15,'slow viewer does not stall controller');
    }
    const before=Number(await page.locator('#video').getAttribute('data-frames-decoded'));
    await watchContext.close();await until(async()=>(await metrics()).webrtc_connected===1,'disconnect only viewer');
    await until(async()=>Number(await page.locator('#video').getAttribute('data-frames-decoded'))>before,'controller stream survives viewer disconnect');
    const loss=Number(process.env.RENDER_MODULE_TEST_LOSS||0);
    if(loss) {
        await until(async()=>{const s=await metrics();return s.rtcp_nacks>0 && s.rtp_retransmits>0 && s.test_packets_dropped>0;},'NACK recovery under packet loss');
    }
    if(process.env.RENDER_MODULE_TEST_TURN) {
        assert.equal((await metrics()).webrtc_clients[0].ice_candidate_type,'relay');
        const stats=JSON.parse(await page.locator('#video').getAttribute('data-stats'));
        assert.equal(stats.localCandidateType,'relay');assert.equal(stats.remoteCandidateType,'relay');
    }
    const mediaMetrics=await metrics();
    // Disconnect with held physical state and no final keyup/blur packet.
    await page.bringToFront();await click('edit');await page.keyboard.down('Control');await page.keyboard.down('a');
    await until(async()=>(await state()).ctrl && (await state()).keyHeld,'held keys');
    await page.evaluate(()=>document.querySelector('#reconnect').click());
    await until(async()=>!(await state()).ctrl && !(await state()).keyHeld,'disconnect releases input');
    await until(async()=>await page.locator('#connection').getAttribute('data-session')!==session && await decoded(page),'fresh peer after reconnect');
    await page.keyboard.up('a');await page.keyboard.up('Control');
    for(let n=0;n<2;++n) {await page.reload();await until(()=>decoded(page),'refresh decoding');}
    await until(async()=>!(await state()).mouseHeld && !(await state()).ctrl && !(await state()).keyHeld,'no stuck state');
    assert.equal(await page.evaluate(()=>window.testDataChannels),0);
    assert.equal((await metrics()).jpegEncoded,0);assert.equal((await metrics()).encoder.errors,0);
    assert.deepEqual(errors,[]);assert.equal((await state()).glError,0);
    const report={browser:browser.version(),engine:isFirefox?'firefox':'chromium',lossPercent:loss,
        forcedRelay:!!process.env.RENDER_MODULE_TEST_TURN,slowViewer:!!process.env.RENDER_MODULE_TEST_SLOW_VIEWER,measurement,twoViewerMetrics:end,metrics:mediaMetrics,
        browserStats:JSON.parse(await page.locator('#video').getAttribute('data-stats')),fmtp:codec.offer,nativeAnswerFmtp:codec.answer,
        answerFmtp:signalingTrace.find(m=>m.type==='answer')?.codec?.filter(l=>l.startsWith('a=fmtp:'))};
    await fs.writeFile(path.join(artifact,'metrics.json'),JSON.stringify(report,null,2));
    await page.screenshot({path:path.join(artifact,'browser.png')});
    console.log('WebRTC E2E passed: actual H264 decode/colors, WebSocket input, four sizes/one PC, two viewers, independent SSRC, viewer rejection, reconnect/refresh, bounded queues.');
    console.log(JSON.stringify(report));
} catch(error) {
    if(page)await page.screenshot({path:path.join(artifact,'failure.png')}).catch(()=>{});
    const diagnostic=page?await page.evaluate(()=>({connection:document.querySelector('#connection').textContent,
        video:{...document.querySelector('#video').dataset},trackSeen:window.testTrackSeen,
        receiverCodecs:RTCRtpReceiver.getCapabilities('video')?.codecs,mediaTrace:window.testMediaTrace,peers:window.testPeers?.map(p=>({ice:p.iceConnectionState,connection:p.connectionState,signaling:p.signalingState}))})).catch(()=>null):null;
    await fs.writeFile(path.join(artifact,'failure.json'),JSON.stringify({diagnostic,signalingTrace},null,2));
    console.error(log,errors,diagnostic,signalingTrace);throw error;
} finally {
    if(browser)await browser.close();server.kill('SIGTERM');
    await until(()=>exitCode!==null,'shutdown',6000).catch(()=>server.kill('SIGKILL'));
    await fs.writeFile(path.join(artifact,'server.log'),log);
}
assert.equal(exitCode,0,'clean shutdown');
