// Regenerates src/modules/company_table.h from the Bluetooth SIG company identifiers.
//   node tools/gen-cids.mjs <company_identifiers.yaml> [maxId]
import fs from 'fs';

const src = process.argv[2] || 'company_identifiers.yaml';
const maxId = parseInt(process.argv[3] || '0xFFFF', 16);
const outPath = new URL('../src/modules/company_table.h', import.meta.url).pathname.replace(/^\//, '');

const txt = fs.readFileSync(src, 'utf8');
const rows = [];
const re = /-\s*value:\s*(0x[0-9A-Fa-f]+)\s*\n\s*name:\s*'?([^'\n]+)'?/g;
let m;
while ((m = re.exec(txt)) !== null) {
  const id = parseInt(m[1], 16);
  if (id > maxId) continue;
  rows.push({ id, name: m[2].trim() });
}

const LEGAL = new Set(['inc','llc','ltd','co','corp','corporation','company','gmbh','sa','bv','plc','pty','kk','oy','ab','as','srl','spa','sas','sarl','ag','nv','limited','ltda','pte','sl','oyj','aps','kft','sro','spzoo','bhd','sdn']);
const CITY = new Set(['shenzhen','beijing','shanghai','guangzhou','hangzhou','nanjing','chengdu','tianjin','wuhan','suzhou','xiamen','zhuhai','qingdao','guangdong','dongguan','ningbo','fuzhou','hefei','changsha','zhenjiang','hsinchu','taipei','taichung','kaohsiung']);
function shortName(s) {
  let n = (s || '').replace(/\([^)]*\)/g, ' ').replace(/["']/g, ' ');
  let toks = n.split(/[\s,]+/).filter(Boolean);
  toks = toks.filter(t => !LEGAL.has(t.toLowerCase().replace(/[.]/g, '')));
  // drop leading city tokens so "Shenzhen DeepX Technology" reads as "DeepX"
  while (toks.length > 1 && CITY.has(toks[0].toLowerCase())) toks.shift();
  if (!toks.length) return '';
  let out = toks.slice(0, 2).join(' ');
  if (out.length > 16) out = out.slice(0, 16).trim();
  return out;
}

rows.sort((a, b) => a.id - b.id);
const names = [];
const nameIdx = new Map();
const ids = [];
const idxs = [];
for (const r of rows) {
  let nm = shortName(r.name);
  if (!nm) continue;
  if (!nameIdx.has(nm)) { nameIdx.set(nm, names.length); names.push(nm); }
  ids.push(r.id);
  idxs.push(nameIdx.get(nm));
}
if (names.length > 65535) throw new Error('too many names');

const esc = s => s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
const out = [];
out.push('// Bluetooth SIG company identifiers, generated from company_identifiers.yaml.');
out.push('// Regenerate with: node tools/gen-cids.mjs <company_identifiers.yaml> -- do not hand-edit.');
out.push(`// ${ids.length} identifiers, ${names.length} distinct names.`);
out.push('#pragma once');
out.push('#include <stdint.h>');
out.push('');
out.push(`#define CID_COUNT ${ids.length}`);
out.push('');
out.push('static const char* const CID_NAMES[] = {');
for (let i = 0; i < names.length; i += 8) out.push('    ' + names.slice(i, i + 8).map(n => `"${esc(n)}"`).join(', ') + (i + 8 < names.length ? ',' : ''));
out.push('};');
out.push('');
out.push('// Sorted by identifier so lookupCompanyId() can binary search.');
out.push('static const uint16_t CID_IDS[CID_COUNT] = {');
for (let i = 0; i < ids.length; i += 10) out.push('    ' + ids.slice(i, i + 10).map(v => '0x' + v.toString(16).toUpperCase().padStart(4, '0')).join(', ') + (i + 10 < ids.length ? ',' : ''));
out.push('};');
out.push('');
out.push('static const uint16_t CID_NAME_IDX[CID_COUNT] = {');
for (let i = 0; i < idxs.length; i += 16) out.push('    ' + idxs.slice(i, i + 16).join(', ') + (i + 16 < idxs.length ? ',' : ''));
out.push('};');
fs.writeFileSync(outPath, out.join('\n') + '\n');

const bytes = ids.length * 2 + idxs.length * 2 + names.reduce((a, n) => a + n.length + 1, 0);
console.log(`ids ${ids.length}, names ${names.length}, data ~${(bytes / 1024).toFixed(1)} KB`);
console.log('sample:', names.slice(0, 10).join(' | '));
