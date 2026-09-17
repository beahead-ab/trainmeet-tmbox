import { build } from 'esbuild';
import { cp, mkdir, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

const root = fileURLToPath(new URL('.', import.meta.url));
const output = resolve(root, '../dist/installer');
await mkdir(output, { recursive: true });
for (const path of ['index.html', 'style.css', 'app.js', 'model.js', 'assets']) {
  await cp(resolve(root, path), resolve(output, path), { recursive: true });
}
await cp(resolve(root, '../docs/INSTALLATION.md'), resolve(output, 'Installationsguide.md'));
await build({
  absWorkingDir: root,
  stdin: { contents: "import 'esp-web-tools/dist/web/install-button.js';", resolveDir: root },
  outfile: resolve(output, 'web-tools.js'),
  bundle: true, format: 'esm', minify: true, legalComments: 'linked',
});
await cp(resolve(root, 'node_modules/esp-web-tools/LICENSE'), resolve(output, 'assets/ESP-Web-Tools-LICENSE.txt'));
// The source preview has no binaries. Packaging replaces this with verified
// metadata from the actual MCU builds; never advertise an imaginary download.
await writeFile(resolve(output, 'catalog.json'), JSON.stringify({
  schema: 1, version: (await readFile(resolve(root, '../VERSION'), 'utf8')).trim(),
  profiles: [],
}, null, 2));
console.log(`Guide built: ${output}`);
