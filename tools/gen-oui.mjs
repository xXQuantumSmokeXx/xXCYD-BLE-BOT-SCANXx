// Regenerates src/modules/oui_table.h from the IEEE OUI registry.
//   node tools/gen-oui.mjs <oui.csv> [topN]
// Vendor label = first meaningful token of the organisation name, so the many
// spelling variants of Huawei/Samsung/etc. collapse into one entry.
import fs from 'fs';

const csvPath = process.argv[2] || 'oui.csv';
const TOP_N   = parseInt(process.argv[3] || '80', 10);
const outPath = new URL('../src/modules/oui_table.h', import.meta.url).pathname.replace(/^\//, '');

const LEGAL = new Set(['inc','llc','ltd','co','corp','corporation','company','gmbh','sa','bv','plc','pty','kk','oy','ab','as','srl','spa','sas','sarl','ag','nv','limited','ltda','pte','sl']);
const KEEP_TWO = new Set(['beijing','shenzhen','shanghai','guangzhou','guangdong','hangzhou','nanjing','chengdu','tianjin','wuhan','suzhou','xiamen','zhuhai','qingdao','texas','hewlett','raspberry','nordic','silicon','general','mitsubishi','johnson','murata','panasonic','toshiba','fujitsu','seiko','hong','shenzen']);
// Vendors worth carrying regardless of prefix count: bots, toys, audio, wearables, VR.
const CURATED = ['sphero','wowwee','anki','makeblock','ubtech','ozobot','wonder','vtech','hasbro','mattel','lego','nintendo','dji','bose','sennheiser','jabra','sonos','garmin','withings','oura','fitbit','valve','htc','pico','meta','oculus','gopro','ring','wyze','roku','sonoff','tuya','anker','soundcore','marshall','skullcandy','plantronics','poly','shure','bang','olufsen','denon','yamaha','harman','jbl','beats'];
// Registration-authority blocks carry no vendor information.
const SKIP = new Set(['ieee','iana']);

function fields(line){const o=[];let c='',q=false;for(let i=0;i<line.length;i++){const ch=line[i];if(q){if(ch==='"'){if(line[i+1]==='"'){c+='"';i++;}else q=false;}else c+=ch;}else if(ch==='"')q=true;else if(ch===','){o.push(c);c='';}else c+=ch;}o.push(c);return o;}

function labelOf(org) {
  let n = (org || '').replace(/\([^)]*\)/g, ' ').replace(/["']/g, ' ');
  let toks = n.split(/[\s,]+/).filter(Boolean);
  toks = toks.filter(t => !LEGAL.has(t.toLowerCase().replace(/[.]/g, '')));
  if (!toks.length) return '';
  const take = KEEP_TWO.has(toks[0].toLowerCase()) ? 2 : 1;
  let s = toks.slice(0, take).join(' ');
  if (s === s.toUpperCase() || s === s.toLowerCase()) {
    s = s.split(' ').map(w => (w.length <= 3 ? w.toUpperCase() : w[0].toUpperCase() + w.slice(1).toLowerCase())).join(' ');
  }
  return s.slice(0, 18).trim();
}

const raw = fs.readFileSync(csvPath, 'utf8');
const lines = raw.split(/\r?\n/).filter(Boolean).slice(1);
const byLabel = new Map();
let total = 0;
for (const line of lines) {
  const f = fields(line);
  if (f.length < 3) continue;
  const oui = (f[1] || '').trim().toUpperCase();
  if (!/^[0-9A-F]{6}$/.test(oui)) continue;
  const label = labelOf(f[2]);
  if (!label || SKIP.has(label.toLowerCase())) continue;
  total++;
  if (!byLabel.has(label)) byLabel.set(label, []);
  byLabel.get(label).push(parseInt(oui, 16));
}

const ranked = [...byLabel.entries()].sort((a, b) => b[1].length - a[1].length);
const chosen = new Map();
for (const [label, ouis] of ranked.slice(0, TOP_N)) chosen.set(label, ouis);
for (const [label, ouis] of ranked) {
  if (chosen.has(label)) continue;
  if (CURATED.some(k => label.toLowerCase().includes(k))) chosen.set(label, ouis);
}

const vendors = [...chosen.keys()].sort((a, b) => a.localeCompare(b));
const vendorIndex = new Map(vendors.map((v, i) => [v, i]));
if (vendors.length > 255) throw new Error('vendor count ' + vendors.length + ' exceeds uint8 index');

const entries = [];
for (const [label, ouis] of chosen) for (const o of ouis) entries.push([o, vendorIndex.get(label)]);
entries.sort((a, b) => a[0] - b[0]);
const dedup = [];
for (const e of entries) if (!dedup.length || dedup[dedup.length - 1][0] !== e[0]) dedup.push(e);

const esc = s => s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
const hex = v => '0x' + v.toString(16).toUpperCase().padStart(6, '0');
const out = [];
out.push('// OUI vendor table, generated from the IEEE OUI registry.');
out.push('// Regenerate with: node tools/gen-oui.mjs <oui.csv> [topN] -- do not hand-edit.');
out.push(`// Source rows: ${total} usable prefixes. Selected: ${dedup.length} prefixes across ${vendors.length} vendors.`);
out.push('#pragma once');
out.push('#include <stdint.h>');
out.push('');
out.push(`#define OUI_ENTRY_COUNT  ${dedup.length}`);
out.push(`#define OUI_VENDOR_COUNT ${vendors.length}`);
out.push('');
out.push('static const char* const OUI_VENDORS[OUI_VENDOR_COUNT] = {');
for (let i = 0; i < vendors.length; i += 6) out.push('    ' + vendors.slice(i, i + 6).map(v => `"${esc(v)}"`).join(', ') + (i + 6 < vendors.length ? ',' : ''));
out.push('};');
out.push('');
out.push('typedef struct __attribute__((packed)) { uint8_t b[3]; uint8_t v; } OuiRec;');
out.push('');
out.push('// Sorted by 24-bit OUI so lookupOUI() can binary search.');
out.push('static const OuiRec OUI_TABLE[OUI_ENTRY_COUNT] = {');
for (let i = 0; i < dedup.length; i += 6) {
  const chunk = dedup.slice(i, i + 6).map(([o, v]) => {
    const b0 = (o >> 16) & 0xFF, b1 = (o >> 8) & 0xFF, b2 = o & 0xFF;
    return `{{0x${b0.toString(16).toUpperCase().padStart(2,'0')},0x${b1.toString(16).toUpperCase().padStart(2,'0')},0x${b2.toString(16).toUpperCase().padStart(2,'0')}},${v}}`;
  }).join(', ');
  out.push('    ' + chunk + (i + 6 < dedup.length ? ',' : ''));
}
out.push('};');
fs.writeFileSync(outPath, out.join('\n') + '\n');
console.log(`entries ${dedup.length}, vendors ${vendors.length}, header ${(out.join('\n').length / 1024).toFixed(1)} KB`);
console.log('top vendors:', ranked.slice(0, 12).map(([l, o]) => `${l}(${o.length})`).join(', '));
