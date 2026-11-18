"""End-to-end: real index.html + bnp-core.js, Web Bluetooth mocked and bridged to
sim_device (the branch's ble_img_rx.c). Drops 3% of DATA writes."""
import asyncio, json, random, struct, sys, os
from playwright.async_api import async_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
MOCK = r"""
(() => {
  const listeners = [];
  const ch = (uuid) => ({
    uuid,
    writeValueWithResponse: (b) => window.simWrite(1, Array.from(b)),
    writeValueWithoutResponse: (b) => window.simWrite(2, Array.from(b)),
    startNotifications: async () => {},
    addEventListener: (t, f) => listeners.push(f),
  });
  window.__notify = (arr) => { const u = Uint8Array.from(arr);
    listeners.forEach((f) => f({ target: { value: new DataView(u.buffer) } })); };
  const svc = { getCharacteristic: async (u) => ch(u) };
  const dev = { name: 'D133-Navi', gatt: { connected: false,
    connect: async () => { dev.gatt.connected = true; return { getPrimaryService: async () => svc }; } },
    addEventListener: () => {} };
  Object.defineProperty(navigator, 'bluetooth', { value: { requestDevice: async () => dev } });
})();
"""

async def main():
    sim = await asyncio.create_subprocess_exec(os.path.join(HERE, 'sim_device'),
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    errors, stats = [], {'drop': 0}
    async with async_playwright() as p:
        b = await p.chromium.launch()
        page = await b.new_page(viewport={'width': 1280, 'height': 1250})
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.on('console', lambda m: m.type == 'error' and errors.append(m.text))

        async def sim_write(t, arr):
            if t == 2 and random.random() < 0.03:
                stats['drop'] += 1; return None
            sim.stdin.write(bytes([t]) + struct.pack('<H', len(arr)) + bytes(arr))
            await sim.stdin.drain()
        await page.expose_function('simWrite', sim_write)
        await page.add_init_script(MOCK)

        async def pump():
            while True:
                h = await sim.stdout.readexactly(2)
                body = await sim.stdout.readexactly(struct.unpack('<H', h)[0])
                await page.evaluate('a => window.__notify(a)', list(body))
        pumper = asyncio.create_task(pump())

        await page.goto('file://' + os.path.join(HERE, '..', '..', 'web', 'bnp-sender', 'index.html'))
        await page.click('#btnConnect')
        await page.wait_for_timeout(1600)
        await page.select_option('#fmt', '0')          # RGB565: 1.2 MB, many chunks for the map
        await page.click('#btnSend')
        await page.wait_for_function("document.getElementById('sFrames').textContent === '1'", timeout=120000)
        await page.select_option('#fmt', '1')
        await page.click('#btnSend')
        await page.wait_for_function("document.getElementById('sFrames').textContent === '2'", timeout=60000)
        await page.wait_for_timeout(700)
        await page.screenshot(path='/home/claude/work/e2e.png', full_page=True)
        log = await page.eval_on_selector('#log', 'e => e.value')
        mtu = await page.text_content('#pMtu')
        await b.close()
        pumper.cancel()
    sim.stdin.close()
    err = (await sim.stderr.read()).decode()
    print(log); print('MTU cell:', mtu); print('dropped writes:', stats['drop']); print(err.strip())
    print('page errors:', errors or 'none')
    frames = [l for l in err.splitlines() if l.startswith('FRAME')]
    sys.exit(0 if len(frames) == 2 and not errors else 1)

asyncio.run(main())
