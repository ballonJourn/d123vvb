/**
 * A queue with a hard limit that keeps the newest: when it is full the OLDEST entries are
 * discarded to make room. Used at the sender (frames waiting to be sent) and at the receiver
 * (completed frames waiting for the application), so neither side can build up delay.
 */
export class BoundedQueue {
  constructor(maxItems, { maxBytes = Infinity, sizeOf = x => x.length ?? 0 } = {}) {
    if (!(maxItems >= 1)) throw new RangeError('maxItems must be >= 1');
    Object.assign(this, { maxItems, maxBytes, sizeOf, items: [], bytes: 0, dropped: 0 });
  }
  get length() { return this.items.length; }
  /** Adds item as the newest. Returns how many older entries were dropped to make room. */
  push(item) {
    const size = this.sizeOf(item);
    let dropped = 0;
    while (this.items.length && (this.items.length >= this.maxItems || this.bytes + size > this.maxBytes)) {
      this.bytes -= this.sizeOf(this.items.shift()); dropped++;
    }
    this.items.push(item); this.bytes += size; this.dropped += dropped;
    return dropped;
  }
  /** Removes and returns the oldest entry, or undefined. */
  shift() {
    const item = this.items.shift();
    if (item !== undefined) this.bytes -= this.sizeOf(item);
    return item;
  }
  clear() { this.items = []; this.bytes = 0; }
}
