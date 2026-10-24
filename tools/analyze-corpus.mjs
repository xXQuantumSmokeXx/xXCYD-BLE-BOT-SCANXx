// Reads a ble.jsonl corpus and proposes detection signatures.
//   node tools/analyze-corpus.mjs <ble.jsonl>
// Output is a report: vendors, name prefixes, and the clustering keys we can match on
// (128-bit service UUID, company id, 16-bit service) with the devices seen for each.
import fs from 'fs';

const path = process.argv[2];
if (!path) { console.error('usage: node tools/analyze-corpus.mjs <ble.jsonl>'); process.exit(1); }

const recs = fs.readFileSync(path, 'utf8')
  .split(/\r?\n/).filter(Boolean)
  .map(l => { try { return JSON.parse(l); } catch { return null; } })
  .filter(Boolean);

const uniq = new Map();            // mac -> last record
for (const r of recs) uniq.set(r.mac, { ...(uniq.get(r.mac) || {}), ...r });

console.log(`records ${recs.length}, distinct MACs ${uniq.size}`);
const vendors = {};
for (const r of uniq.values()) vendors[r.vendor || '(none)'] = (vendors[r.vendor || '(none)'] || 0) + 1;
console.log('vendors :', Object.entries(vendors).sort((a,b)=>b[1]-a[1]).map(([k,v])=>`${k} ${v}`).join(', '));
const addrs = {};
for (const r of uniq.values()) addrs[r.addr || '?'] = (addrs[r.addr || '?'] || 0) + 1;
console.log('address :', Object.entries(addrs).map(([k,v])=>`${k} ${v}`).join(', '));

// name prefix: strip a trailing hex id / digits so "Govee_H805A_6698" -> "Govee_H805A"
function prefix(name) {
  if (!name) return '';
  let n = name.replace(/[_\- ]+[0-9A-Fa-f]{4,}$/, '');
  n = n.replace(/\s+$/,'');
  return n;
}
const byPrefix = {};
for (const r of uniq.values()) {
  const p = prefix(r.name);
  if (!p) continue;
  (byPrefix[p] ||= []).push(r);
}
console.log('\n--- name prefixes ---');
for (const [p, rs] of Object.entries(byPrefix).sort((a,b)=>b[1].length-a[1].length))
  console.log(`  "${p}"  x${rs.length}  vendor:${[...new Set(rs.map(r=>r.vendor||'-'))].join('/')}  u128:${[...new Set(rs.map(r=>r.u128||'-'))].join('/')}`);

function cluster(field, label) {
  const m = {};
  for (const r of uniq.values()) {
    const v = r[field];
    if (!v) continue;
    const key = Array.isArray(v) ? v.join(',') : v;
    if (!key || key === '0x0000') continue;
    (m[key] ||= []).push(r);
  }
  const rows = Object.entries(m).filter(([,rs]) => rs.length >= 1).sort((a,b)=>b[1].length-a[1].length);
  console.log(`\n--- ${label} ---`);
  for (const [k, rs] of rows.slice(0, 12))
    console.log(`  ${k}  x${rs.length}  names:${[...new Set(rs.map(r=>r.name||'-'))].join(',')}  vendors:${[...new Set(rs.map(r=>r.vendor||'-'))].join(',')}`);
}
cluster('u128', '128-bit service UUIDs');
cluster('cid', 'company ids');
cluster('svc', '16-bit services');

console.log('\n--- classic records ---');
const classic = recs.filter(r => r.src === 'classic');
console.log(classic.length ? JSON.stringify(classic.slice(0,5), null, 1) : '  none captured yet');