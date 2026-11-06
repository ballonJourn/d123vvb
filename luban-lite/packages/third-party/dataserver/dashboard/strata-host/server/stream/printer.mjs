/**
 * Console output that cannot stall the program.
 *
 * On Windows a click in the console window (selecting text, "QuickEdit") makes every write to the console
 * block until Esc is pressed, and Node writes console output on the thread that also sends the datagrams, so
 * the whole stream stops for as long as the selection lasts. Here the lines go through a pipe to a small child
 * process that does the blocking write: when the console is stuck the child waits and this process carries on.
 * If the child falls more than maxBacklog bytes behind, new lines are dropped (counted) instead of piling up.
 *
 * By default the child is used on a Windows console only; D133_PRINT_CHILD=1 / 0 forces it on / off.
 */
import { spawn } from 'node:child_process';

const COPY = "process.stdin.on('data', d => process.stdout.write(d)); process.stdin.on('end', () => process.exit(0));";

export function createPrinter({ child, childArgs = ['-e', COPY], maxBacklog = 256 * 1024 } = {}) {
  const forced = process.env.D133_PRINT_CHILD;
  child ??= forced === '1' || (forced !== '0' && process.platform === 'win32' && !!process.stdout.isTTY);
  let proc = null, alive = false, dropped = 0;
  if (child) {
    try {
      proc = spawn(process.execPath, childArgs, { stdio: ['pipe', 'inherit', 'inherit'], windowsHide: true });
      alive = true;
      const dead = () => { alive = false; };
      proc.on('error', dead); proc.on('exit', dead); proc.stdin.on('error', dead);
    } catch { alive = false; }
  }
  return {
    get dropped() { return dropped; },
    get usesChild() { return alive; },
    /** Never blocks. */
    log(line) {
      if (alive) {
        if (proc.stdin.writableLength > maxBacklog) { dropped++; return; }
        proc.stdin.write(line + '\n');
        return;
      }
      console.log(line);
    },
    /** Lets the child finish what it was given, but not for ever if the console is stuck. */
    close(timeoutMs = 1500) {
      if (!proc || !alive) return Promise.resolve();
      return new Promise(done => {
        const t = setTimeout(() => { try { proc.kill(); } catch { /* gone */ } done(); }, timeoutMs);
        proc.once('exit', () => { clearTimeout(t); done(); });
        proc.stdin.end();
      });
    },
  };
}
