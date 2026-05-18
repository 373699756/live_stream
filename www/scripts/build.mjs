import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(scriptDir, '..');
const distDir = path.join(rootDir, 'dist');
const distAssetsDir = path.join(distDir, 'assets');
const publicAssetsDir = path.join(rootDir, 'public', 'assets');
const legacyEntryPattern = /^index-[A-Za-z0-9_-]+\.js$/;

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

function readEntryFromIndex(indexPath) {
  if (!existsSync(indexPath)) {
    return '';
  }
  const html = readFileSync(indexPath, 'utf8');
  const match = html.match(/\/assets\/(index-[^"']+\.js)/);
  return match ? match[1] : '';
}

function listLegacyEntries() {
  if (!existsSync(publicAssetsDir)) {
    return [];
  }
  return readdirSync(publicAssetsDir).filter((name) => legacyEntryPattern.test(name));
}

function writeIfChanged(filePath, content) {
  if (existsSync(filePath) && readFileSync(filePath, 'utf8') === content) {
    return;
  }
  writeFileSync(filePath, content);
}

const previousEntry = readEntryFromIndex(path.join(distDir, 'index.html'));

run(localBin('tsc'), ['--noEmit']);
run(localBin('vite'), ['build']);

const currentEntry = readEntryFromIndex(path.join(distDir, 'index.html'));
if (!currentEntry) {
  console.error('Unable to find Vite entry bundle in dist/index.html');
  process.exit(1);
}

mkdirSync(publicAssetsDir, { recursive: true });
mkdirSync(distAssetsDir, { recursive: true });

const legacyEntries = new Set(listLegacyEntries());
if (previousEntry && previousEntry !== currentEntry) {
  legacyEntries.add(previousEntry);
}

const shim = `import('/assets/${currentEntry}');\n`;
for (const entry of legacyEntries) {
  if (entry === currentEntry) {
    continue;
  }
  writeIfChanged(path.join(publicAssetsDir, entry), shim);
  writeIfChanged(path.join(distAssetsDir, entry), shim);
}
