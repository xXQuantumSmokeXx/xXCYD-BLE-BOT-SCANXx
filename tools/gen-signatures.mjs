// Generates src/modules/signatures.h from signatures.json.
//   node tools/gen-signatures.mjs [signatures.json]
import fs from 'fs';

const srcPath = process.argv[2] || new URL('../signatures.json', import.meta.url).pathname.replace(/^\//, '');
const outPath = new URL('../src/modules/signatures.h', import.meta.url).pathname.replace(/^\//, '');
const def = JSON.parse(fs.readFileSync(srcPath, 'utf8'));
const rules = def.signatures || [];
if (!rules.length) throw new Error('no signatures in ' + srcPath);
if (rules.length > 250) throw new Error('too many signatures');

const esc = s => String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
const out = [];
out.push('// Detection signatures, generated from signatures.json by tools/gen-signatures.mjs.');
out.push('// Do not hand-edit: change signatures.json and regenerate.');
out.push(`// ${rules.length} rules.`);
out.push('#pragma once');
out.push('#include <stdint.h>');
out.push('');
out.push(`#define SIG_COUNT ${rules.length}`);
out.push('');
out.push('typedef struct {');
out.push('    const char    *label;        // short class shown in the UI');
out.push('    const char    *name_prefix;  // "" = any');
out.push('    const char    *vendor_not;   // "" = no vendor exclusion');
out.push('    uint16_t       cid;          // 0 = any company id');
out.push('    uint16_t       svc16;        // 0 = any 16-bit service');
out.push('    const uint8_t *u128;         // nullptr = any 128-bit service');
out.push('} SigRule;');
out.push('');

const arrays = [];
rules.forEach((r, i) => {
  if (!r.u128) return;
  const hex = r.u128.replace(/[^0-9A-Fa-f]/g, '');
  if (hex.length !== 32) throw new Error(`rule ${i}: u128 must be 32 hex chars`);
  const bytes = hex.match(/../g).map(b => '0x' + b.toUpperCase()).join(', ');
  arrays.push(`static const uint8_t SIG_U128_${i}[16] = { ${bytes} };`);
});
arrays.forEach(a => out.push(a));
if (arrays.length) out.push('');

out.push('static const SigRule SIG_RULES[SIG_COUNT] = {');
rules.forEach((r, i) => {
  const cid = r.cid ? r.cid : '0x0000';
  const svc = r.svc16 ? r.svc16 : '0x0000';
  const name = r.name_prefix ? `"${esc(r.name_prefix)}"` : '""';
  const vnot = r.vendor_not ? `"${esc(r.vendor_not)}"` : '""';
  const u = r.u128 ? `SIG_U128_${i}` : 'nullptr';
  out.push(`    { "${esc(r.label)}", ${name}, ${vnot}, ${cid}, ${svc}, ${u} },`);
});
out.push('};');
fs.writeFileSync(outPath, out.join('\n') + '\n');
console.log(`signatures.h: ${rules.length} rules -> ${outPath}`);