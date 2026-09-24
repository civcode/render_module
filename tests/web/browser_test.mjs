import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {setTimeout as sleep} from 'node:timers/promises';
const [fixture, artifact] = process.argv.slice(2);
await fs.mkdir(artifact, {recursive: true});
const statePath = path.join(artifact, 'state.json');
await fs.rm(statePath, {force: true});
const env = {...process.env}; delete env.DISPLAY; delete env.WAYLAND_DISPLAY;
const server = spawn(fixture, [statePath], {env, stdio: ['ignore', 'pipe', 'pipe']});
let log = '', page, browser, exitCode = null;
server.on('exit', code => { exitCode = code; });
server.stdout.on('data', b => { log = (log + b).slice(-65536); });
server.stderr.on('data', b => { log = (log + b).slice(-65536); });
async function until(fn, label, ms = 12000) {
    const end = Date.now() + ms;
    do { const value = await fn(); if (value) return value; await sleep(50); } while (Date.now() < end);
    throw Error(`Timeout: ${label}`);
}
async function state() { return JSON.parse(await fs.readFile(statePath, 'utf8')); }
const token = '0123456789abcdef0123456789abcdef';
let origin;
const browserErrors = [], pageErrors = [];
try {
    origin = await until(() => log.match(/HTTP : (http:\/\/127\.0\.0\.1:\d+)\//)?.[1], 'server startup');
    await until(async () => { try { return await state(); } catch { return false; } }, 'fixture state');
    const health = await fetch(origin + '/healthz'); assert.equal(health.status, 200);
    const authHeaders = {Authorization: `Bearer ${token}`};
    const metrics = async () => (await fetch(origin + '/api/version', {headers: authHeaders})).json();
    assert.equal((await fetch(origin + '/api/version')).status, 401);
    assert.equal((await metrics()).jpegEncoded, 0, 'No viewers: no JPEG encoding');
    browser = await chromium.launch({headless: true, args: ['--no-sandbox', '--disable-gpu', '--no-proxy-server']});
    const context = await browser.newContext({viewport: {width: 900, height: 700}, permissions: ['clipboard-read', 'clipboard-write']});
    page = await context.newPage();
    page.on('pageerror', error => { pageErrors.push(String(error)); browserErrors.push(String(error)); });
    page.on('console', message => { if (message.type() === 'error') browserErrors.push(message.text()); });
    await page.goto(origin);
    await page.locator('#token').fill('incorrect0123456789'); await page.locator('#login button').click();
    await until(() => page.locator('#connection').textContent().then(t => t.includes('Authentication failed')), 'wrong token rejection');
    await page.locator('#token').fill(token); await page.locator('#login button').click();
    await page.waitForFunction(() => document.querySelector('#lease').dataset.control === 'true');
    await page.waitForFunction(() => Number(document.querySelector('#picture').dataset.frameId) > 0);
    assert.ok(!(await page.evaluate(() => document.cookie)).includes('rm_auth'));
    const cookie = (await context.cookies()).find(c => c.name === 'rm_auth');
    assert.ok(cookie.httpOnly && cookie.sameSite === 'Strict');
    assert.ok(!log.includes(token));
    await until(async () => {
        const s = await state();
        return await page.evaluate(({width, height}) => {
            const c = document.querySelector('#picture'), d = document.querySelector('#display');
            return c.width === width && c.height === height && Number(d.dataset.width) === width && Number(d.dataset.height) === height;
        }, s);
    }, 'initial viewport/frame dimensions');
    let current = await state();
    const colors = await page.evaluate(({canvas, view}) => {
        const ctx = document.querySelector('#picture').getContext('2d');
        return [canvas, view, [40, 30]].map(([x,y]) => [...ctx.getImageData(Math.floor(x), Math.floor(y), 1, 1).data]);
    }, current);
    assert.ok(colors[0][0] > 180 && colors[0][1] < 80, 'NanoVG red pixels survived JPEG');
    assert.ok(colors[1][1] > 180 && colors[1][0] < 80, 'View3D green pixels survived JPEG');
    assert.ok(colors[2][0] > 100, 'ImGui window pixels survived JPEG');
    async function screen(point) {
        return page.evaluate(([x,y]) => {
            const c = document.querySelector('#picture'), r = c.getBoundingClientRect();
            return {x: r.left + x/c.width*r.width, y: r.top + y/c.height*r.height};
        }, point);
    }
    async function click(name) {
        const p = await screen((await state())[name]); await page.mouse.click(p.x, p.y);
    }
    await click('button'); await until(async () => (await state()).clicks === 1, 'real ImGui button click');
    await click('edit'); await sleep(200);
    await page.keyboard.type('ASCII '); await page.keyboard.insertText('äöüÄÖÜß é 🙂');
    await until(async () => (await state()).text === 'ASCII äöüÄÖÜß é 🙂', 'committed Unicode');
    await page.evaluate(() => navigator.clipboard.writeText(' paste ß 日本'));
    await page.keyboard.press('Control+V');
    await until(async () => (await state()).text === 'ASCII äöüÄÖÜß é 🙂 paste ß 日本', 'real clipboard paste');
    const beforeComposition = (await state()).text;
    await page.locator('#text').evaluate(element => {
        element.dispatchEvent(new CompositionEvent('compositionstart', {data: ''}));
        element.value = '漢字';
        element.dispatchEvent(new CompositionEvent('compositionupdate', {data: '漢字'}));
        element.dispatchEvent(new InputEvent('input', {data: '漢字', inputType: 'insertCompositionText', isComposing: true}));
    });
    await sleep(150); assert.equal((await state()).text, beforeComposition, 'uncommitted IME text stays local');
    await page.locator('#text').evaluate(element => {
        element.dispatchEvent(new CompositionEvent('compositionend', {data: '漢字'}));
        element.dispatchEvent(new InputEvent('input', {data: '漢字', inputType: 'insertFromComposition', isComposing: false}));
    });
    await until(async () => (await state()).text === beforeComposition + '漢字', 'synthetic CJK composition commit');
    await page.keyboard.press('Control+A'); await page.keyboard.insertText('Replaced 🙂');
    await until(async () => (await state()).text === 'Replaced 🙂', 'Ctrl+A selection');
    const camera = (await state()).camera, p = await screen((await state()).view);
    await page.mouse.move(p.x, p.y); await page.mouse.down({button: 'right'}); await sleep(150);
    await page.mouse.move(p.x+70, p.y+40, {steps: 8}); await sleep(250); await page.mouse.up({button: 'right'});
    await until(async () => (await state()).camera.some((x, i) => Math.abs(x-camera[i]) > .1), 'View3D orbit');
    await page.mouse.move(p.x, p.y); const zoom = (await state()).camera;
    await page.mouse.wheel(0, -200);
    await until(async () => (await state()).camera.some((x, i) => Math.abs(x-zoom[i]) > .2), 'View3D wheel zoom');
    // Capture must retain the drag after leaving the displayed image, and release cleanly.
    await page.mouse.move(p.x, p.y); await page.mouse.down({button: 'left'}); await sleep(100);
    await page.mouse.move(2, 2); await page.mouse.up({button: 'left'});
    await until(async () => !(await state()).mouseHeld, 'captured mouse release outside image');
    const oldGeneration = (await state()).generation;
    for (const [width, height] of [[800,600],[1400,900],[1100,750],[2400,1600],[1000,760]]) {
        await page.setViewportSize({width, height}); await sleep(170);
    }
    await until(async () => {
        const s = await state();
        return s.generation > oldGeneration && await page.evaluate(({width,height}) => {
            const c = document.querySelector('#picture'), d = document.querySelector('#display');
            const scale = Math.min(1, 1920/d.clientWidth, 1080/d.clientHeight);
            return width === Math.floor(d.clientWidth*scale) && height === Math.floor(d.clientHeight*scale) &&
                c.width === width && c.height === height && Number(d.dataset.width) === width && Number(d.dataset.height) === height;
        }, s);
    }, 'rapid resize transaction and matching JPEG');
    assert.ok((await state()).width <= 1920 && (await state()).height <= 1080);
    // A second authenticated browser gets frames but never input authority.
    const watchContext = await browser.newContext({viewport: {width: 500, height: 900}});
    const watcher = await watchContext.newPage(); await watcher.goto(origin);
    await watcher.locator('#token').fill(token); await watcher.locator('#login button').click();
    await watcher.waitForFunction(() => Number(document.querySelector('#picture').dataset.frameId) > 0);
    assert.equal(await watcher.locator('#lease').getAttribute('data-control'), 'false');
    const clicks = (await state()).clicks;
    await watcher.locator('#input').click({position: {x: 25, y: 25}}); await sleep(200);
    assert.equal((await state()).clicks, clicks);
    await watchContext.close(); await page.bringToFront();
    // Focus loss and disconnect never depend on delivery of the final keyup.
    await click('edit'); await page.keyboard.down('Control'); await page.keyboard.down('a');
    await until(async () => (await state()).ctrl && (await state()).keyHeld, 'held shortcut before blur');
    await page.locator('#reconnect').focus();
    await until(async () => !(await state()).ctrl && !(await state()).keyHeld, 'element blur releases held state');
    await page.keyboard.up('a'); await page.keyboard.up('Control');
    await click('edit'); await page.keyboard.down('Control'); await page.keyboard.down('a');
    await until(async () => (await state()).ctrl && (await state()).keyHeld, 'held shortcut before disconnect');
    const session = await page.locator('#connection').getAttribute('data-session');
    const lastFrame = Number(await page.locator('#picture').getAttribute('data-frame-id'));
    // Programmatic activation doesn't move DOM focus: no final blur/keyup packet.
    await page.evaluate(() => document.querySelector('#reconnect').click());
    await until(async () => !(await state()).ctrl && !(await state()).keyHeld, 'server disconnect release-all');
    await page.waitForFunction(old => document.querySelector('#connection').dataset.session !== old &&
        document.querySelector('#lease').dataset.control === 'true', session);
    await page.waitForFunction(old => Number(document.querySelector('#picture').dataset.frameId) > old, lastFrame);
    await until(async () => !(await state()).keyHeld && !(await state()).ctrl && !(await state()).mouseHeld, 'reconnect unstuck');
    await page.keyboard.up('a'); await page.keyboard.up('Control');
    await click('button'); await until(async () => (await state()).clicks === clicks+1, 'interaction after reconnect');
    const cpu = async () => {
        const stat = await fs.readFile(`/proc/${server.pid}/stat`, 'utf8');
        const fields = stat.slice(stat.lastIndexOf(')')+2).split(' ');
        return Number(fields[11]) + Number(fields[12]);
    };
    const ticks = Number(execFileSync('getconf', ['CLK_TCK'], {encoding: 'utf8'}));
    const start = await metrics(), cpuStart = await cpu(), t0 = performance.now();
    await sleep(2000);
    const end = await metrics(), seconds = (performance.now()-t0)/1000;
    const report = {chromium: browser.version(), width: end.width, height: end.height,
        jpegFps: (end.jpegEncoded-start.jpegEncoded)/seconds, serverCpuPercent: (await cpu()-cpuStart)/ticks/seconds*100,
        averageReadbackEncodeMicros: (end.encodeMicros-start.encodeMicros)/(end.jpegEncoded-start.jpegEncoded),
        metrics: end, syntheticIME: true};
    assert.equal((await state()).glError, 0); assert.deepEqual(pageErrors, [], 'no uncaught browser errors');
    await fs.writeFile(path.join(artifact, 'metrics.json'), JSON.stringify(report, null, 2));
    await page.screenshot({path: path.join(artifact, 'browser.png')});
    await fs.writeFile(path.join(artifact, 'console.json'), JSON.stringify(browserErrors, null, 2));
    console.log('Chromium E2E passed: auth, JPEG pixels, widget click, Unicode, paste, CJK commit, orbit/zoom, capture, resize, viewer lease, blur, reconnect.');
    console.log(JSON.stringify(report));
} catch (error) {
    if (page) await page.screenshot({path: path.join(artifact, 'failure.png')}).catch(() => {});
    console.error(browserErrors);
    throw error;
} finally {
    if (browser) await browser.close();
    server.kill('SIGTERM');
    await until(() => exitCode !== null, 'server graceful shutdown', 4000).catch(() => server.kill('SIGKILL'));
    await fs.writeFile(path.join(artifact, 'server.log'), log);
}
assert.equal(exitCode, 0, 'clean server shutdown');
