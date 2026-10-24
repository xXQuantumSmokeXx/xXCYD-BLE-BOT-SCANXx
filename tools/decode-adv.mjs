// Decodes RAW advertisement dumps captured by the firmware.
//   node tools/decode-adv.mjs <file-with-raw-lines>
// Lines look like: RAW {"raw":"E8:F6:0A:CB:1C:D1","len":31,"hex":"020106..."}
// Prints the AD structures so name parsing can be checked against the raw bytes.
import fs from 'fs';

const path = process.argv[2];
if (!path) { console.error('usage: node tools/decode-adv.mjs <file>'); process.exit(1); }
const text = fs.readFileSync(path, 'utf8');

const TYPE = {
  0x01: 'flags', 0x02: 'uuid16', 0x03: 'uuid16', 0x04: 'uuid32', 0x05: 'uuid32',
  0x06: 'uuid128', 0x07: 'uuid128', 0x08: 'name-short', 0x09: 'name-complete',
  0x0A: 'tx-power', 0x0d: 'class-of-device', 0x16: 'service-data16', 0x19: 'appearance',
  0x1B: 'le-role', 0x2A: 'tx-power?', 0xFF: 'manufacturer',
};
const hex2 = b => b.toString(16).toUpperCase().padStart(2, '0');

function decode(bytes) {
  const out = [];
  let i = 0;
  while (i < bytes.length) {
    const len = bytes[i];
    if (len === 0) { out.push(`  [${i}] zero padding (${bytes.length - i} bytes)`); break; }
    if (i + 1 + len > bytes.length) {
      out.push(`  [${i}] MALFORMED: len=${len} but only ${bytes.length - i - 1} bytes remain: ${bytes.slice(i).map(hex2).join(' ')}`);
      break;
    }
    const type = bytes[i + 1];
    const data = bytes.slice(i + 2, i + 1 + len);
    const name = TYPE[type] || 'unknown';
    let detail = '';
    if (type === 0x08 || type === 0x09) {
      detail = `"${Buffer.from(data).toString('latin1')}"  (${data.length} bytes)`;
    } else if (type === 0xFF && data.length >= 2) {
      detail = `company=0x${(data[0] | (data[1] << 8)).toString(16).toUpperCase().padStart(4, '0')} rest=${data.slice(2).map(hex2).join(' ')}`;
    } else if (type === 0x01) {
      detail = `0x${data.map(hex2).join('')}`;
    } else {
      detail = data.map(hex2).join(' ');
    }
    out.push(`  [${i}] len=${len} type=0x${hex2(type)} ${name.padEnd(14)} ${detail}`);
    i += 1 + len;
  }
  return out;
}

let shown = 0;
for (const line of text.split(/\r?\n/)) {
  const m = line.match(/"raw":"([^"]+)","len":(\d+),"hex":"([0-9A-Fa-f]*)"/);
  if (!m) continue;
  const [, mac, len, hex] = m;
  const bytes = hex.match(/../g)?.map(b => parseInt(b, 16)) || [];
  console.log(`\n${mac}  len=${len}  hexlen=${bytes.length}`);
  for (const l of decode(bytes)) console.log(l);
  if (++shown >= 40) { console.log('\n... truncated at 40 records'); break; }
}
if (!shown) console.log('no RAW lines found - flash the payload-dump build and collect a little first');