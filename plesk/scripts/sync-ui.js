'use strict';

const fs = require('node:fs');
const path = require('node:path');

const source = path.resolve(__dirname, '../../web/index.html');
const target = path.resolve(__dirname, '../public/index.html');
fs.mkdirSync(path.dirname(target), { recursive: true });
fs.copyFileSync(source, target);
console.log(`Synced ${path.relative(process.cwd(), source)} -> ${path.relative(process.cwd(), target)}`);
