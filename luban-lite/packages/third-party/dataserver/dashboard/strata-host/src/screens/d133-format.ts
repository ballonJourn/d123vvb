/** Number formatting shared by the D133 screens. Missing numbers always show as an em dash, never as 0. */
export const num = (v: number | null | undefined): v is number => typeof v === 'number' && Number.isFinite(v);
export const kb = (v: number | null | undefined) => num(v) ? v / 1024 : null;
export const rate = (v: number | null | undefined) => !num(v) ? '\u2014' : v >= 1048576 ? `${(v / 1048576).toFixed(2)} MB/s` : `${(v / 1024).toFixed(1)} KB/s`;
export const bytes = (v: number | null | undefined) => !num(v) ? '\u2014' : v < 1024 ? `${v} B` : v < 1048576 ? `${(v / 1024).toFixed(1)} KB` : v < 1073741824 ? `${(v / 1048576).toFixed(2)} MB` : `${(v / 1073741824).toFixed(2)} GB`;
/** bits per second, as links are quoted */
export const bps = (v: number | null | undefined) => !num(v) ? '\u2014' : v >= 1e6 ? `${(v / 1e6).toFixed(2)} Mbit/s` : `${(v / 1e3).toFixed(0)} kbit/s`;
export const pps = (v: number | null | undefined) => !num(v) ? '\u2014' : `${v >= 100 ? v.toFixed(0) : v.toFixed(1)} pkt/s`;
export const pct = (v: number | null | undefined, digits = 1) => !num(v) ? '\u2014' : `${(v * 100).toFixed(digits)} %`;
export const ms = (v: number | null | undefined, digits = 1) => !num(v) ? '\u2014' : `${v.toFixed(digits)} ms`;
export const last = (s: (number | null)[] | undefined) => { for (let i = (s?.length ?? 0) - 1; i >= 0; i--) if (num(s![i])) return s![i]; return null; };
export const busy = (s?: string) => s === 'CONNECTING' || s === 'RECEIVING';
