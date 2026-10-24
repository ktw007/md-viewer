const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const tempDir = path.join(__dirname, '.temp_prism');
if (!fs.existsSync(tempDir)) {
  fs.mkdirSync(tempDir, { recursive: true });
}

console.log('Installing prismjs in temporary location...');
execSync('npm install --prefix "' + tempDir + '" prismjs', { stdio: 'inherit' });

const prismDir = path.join(tempDir, 'node_modules', 'prismjs');

// Order matters for Prism dependencies:
// core -> clike -> c -> cpp / csharp / java
// markup -> css / javascript -> typescript / jsx / tsx
const components = [
  'components/prism-core.min.js',
  'components/prism-markup.min.js',
  'components/prism-css.min.js',
  'components/prism-clike.min.js',
  'components/prism-javascript.min.js',
  'components/prism-c.min.js',
  'components/prism-cpp.min.js',
  'components/prism-csharp.min.js',
  'components/prism-java.min.js',
  'components/prism-python.min.js',
  'components/prism-rust.min.js',
  'components/prism-go.min.js',
  'components/prism-typescript.min.js',
  'components/prism-bash.min.js',
  'components/prism-json.min.js',
  'components/prism-yaml.min.js',
  'components/prism-sql.min.js',
  'components/prism-markdown.min.js'
];

let bundled = '/* PrismJS bundled for md-viewer (C, C++, C#, Java, Python, Rust, Go, JS, TS, Bash, JSON, YAML, SQL, Markup, CSS, Markdown) */\n';

for (const comp of components) {
  const file = path.join(prismDir, comp);
  if (fs.existsSync(file)) {
    console.log('Bundling:', comp);
    bundled += fs.readFileSync(file, 'utf8') + '\n;';
  } else {
    console.warn('Missing component:', comp);
  }
}

const outPath = path.join(__dirname, '..', 'page', 'prism.min.js');
fs.writeFileSync(outPath, bundled, 'utf8');
console.log('Successfully generated:', outPath, 'Size:', (fs.statSync(outPath).size / 1024).toFixed(1), 'KB');

// Cleanup
fs.rmSync(tempDir, { recursive: true, force: true });
console.log('Cleaned up temporary files.');
