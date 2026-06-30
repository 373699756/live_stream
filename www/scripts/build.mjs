import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(scriptDir, '..');
const distDir = path.join(rootDir, 'dist');
const distAssetsDir = path.join(distDir, 'assets');
const publicAssetsDir = path.join(rootDir, 'public', 'assets');
const legacyEntryPattern = /^index-[A-Za-z0-9_-]+\.(?:js|css)$/;

function localBin(name) {
  const suffix = process.platform === 'win32' ? '.cmd' : '';
  return path.join(rootDir, 'node_modules', '.bin', `${name}${suffix}`);
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: rootDir,
    stdio: 'inherit',
    shell: false,
  });
  if (result.error) {
    console.error(result.error.message);
    process.exit(1);
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function removeLegacyEntries(directory) {
  if (!existsSync(directory)) {
    return;
  }
  for (const name of readdirSync(directory)) {
    if (legacyEntryPattern.test(name)) {
      rmSync(path.join(directory, name), { force: true });
    }
  }
}

function appendEntryVersion() {
  const indexPath = path.join(distDir, 'index.html');
  if (!existsSync(indexPath)) {
    return;
  }
  const version = Date.now().toString();
  const html = readFileSync(indexPath, 'utf8')
    .replace('/assets/index.js"', `/assets/index.js?v=${version}"`)
    .replace('/assets/index.css"', `/assets/index.css?v=${version}"`);
  writeFileSync(indexPath, html);
}

removeLegacyEntries(distAssetsDir);
removeLegacyEntries(publicAssetsDir);

run(localBin('tsc'), ['--noEmit']);
run(localBin('vite'), ['build']);
appendEntryVersion();
