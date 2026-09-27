import {chromium} from 'playwright';
import {spawn,execFileSync} from 'node:child_process';
import {setTimeout as sleep} from 'node:timers/promises';
import fs from 'node:fs/promises';
import path from 'node:path';
const [fixture,artifact]=process.argv.slice(2);await fs.mkdir(artifact,{recursive:true});
const token='0123456789abcdef0123456789abcdef',sizes=[[1000,700],[1280,720],[1920,1080]];
const browser=await chromium.launch({headless:true,args:['--no-sandbox','--disable-gpu','--no-proxy-server']});
const results=[];
async function until(fn,label,ms=30000){const end=Date.now()+ms;do{const v=await fn();if(v)return v;await sleep(50);}while(Date.now()<end);throw Error(`Timeout: ${label}`);}
try {
for(const codec of ['jpeg','png']) for(const scene of ['static-ui','view3d-ui']) {
    const statePath=path.join(artifact,`${codec}-${scene}-state.json`);await fs.rm(statePath,{force:true});
    const env={...process.env,...(codec==='png'?{RENDER_MODULE_TEST_PNG:'1'}:{}),...(scene==='static-ui'?{RENDER_MODULE_TEST_STATIC_UI:'1'}:{})};
    delete env.DISPLAY;delete env.WAYLAND_DISPLAY;
    const server=spawn(fixture,[statePath],{env,stdio:['ignore','pipe','pipe']});let log='',exit=null;
    server.stdout.on('data',b=>{log+=b;});server.stderr.on('data',b=>{log+=b;});server.on('exit',c=>{exit=c;});
    try {
        const origin=await until(()=>log.match(/HTTP : (http:\/\/127\.0\.0\.1:\d+)\//)?.[1],'server');
        const metrics=async()=>(await fetch(origin+'/api/version',{headers:{Authorization:`Bearer ${token}`}})).json();
        const context=await browser.newContext({viewport:{width:2000,height:1200}}),page=await context.newPage();
        await page.goto(origin);await page.locator('#token').fill(token);await page.locator('#login button').click();
        await page.waitForFunction(expected=>document.querySelector('#picture').dataset.codec===expected,codec);
        const ticks=Number(execFileSync('getconf',['CLK_TCK'],{encoding:'utf8'}));
        const cpu=async()=>{const s=await fs.readFile(`/proc/${server.pid}/stat`,'utf8'),p=s.slice(s.lastIndexOf(')')+2).split(' ');return Number(p[11])+Number(p[12]);};
        for(const [width,height] of sizes) {
            await page.locator('#display').evaluate((d,[w,h])=>{d.style.flex='none';d.style.width=w+'px';d.style.height=h+'px';},[width,height]);
            await until(()=>page.evaluate(([w,h])=>{const c=document.querySelector('#picture');return c.width===w&&c.height===h;},[width,height]),`${codec} ${scene} ${width}`);
            await sleep(300);const before=await metrics(),c0=await cpu(),start=performance.now();await sleep(1500);
            const after=await metrics(),seconds=(performance.now()-start)/1000,frames=after.image_frames_encoded-before.image_frames_encoded;
            results.push({codec,scene,width,height,seconds,frames,effectiveFps:frames/seconds,
                meanEncodeMs:(after.encodeMicros-before.encodeMicros)/Math.max(frames,1)/1000,
                meanEncodedBytes:(after.image_bytes_encoded-before.image_bytes_encoded)/Math.max(frames,1),
                serverCpuPercent:(await cpu()-c0)/ticks/seconds*100});
        }
        await context.close();
    } finally {
        server.kill('SIGTERM');await until(()=>exit!==null,'shutdown',6000).catch(()=>server.kill('SIGKILL'));
        await fs.writeFile(path.join(artifact,`${codec}-${scene}.log`),log);
    }
}
} finally {await browser.close();}
await fs.writeFile(path.join(artifact,'metrics.json'),JSON.stringify(results,null,2));
console.log(JSON.stringify(results));
