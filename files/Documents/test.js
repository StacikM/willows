class A { #p = 5; get v(){ return this.#p * 2; } static s = 'st'; }
console.log('class', new A().v, A.s);
console.log('math', Math.pow(2, 0.5).toFixed(6), Math.sin(1).toFixed(6), Math.log(10).toFixed(6), Math.atan2(1, 2).toFixed(6), Math.exp(1.5).toFixed(6));
console.log('num', 0.1 + 0.2, 1e21, 123456789.123456789, (255).toString(16), parseFloat('3.14abc'), Number('1e-7'));
console.log('json', JSON.stringify({ a: [1, 2, { b: null }], c: 'x"y' }), JSON.parse('{"k":[1,2,3]}').k.length);
console.log('regex', 'a1b22c333'.replace(/\d+/g, m => '<' + m + '>'), /(\w+)@(\w+)\.com/.exec('me@site.com').slice(1).join('|'));
console.log('unicode', 'héllo wörld'.toUpperCase(), 'ß'.length, [...'a😀b'].length);
console.log('date', new Date(0).toISOString(), new Date(2024, 1, 29, 12).getDate(), Date.now() > 1.7e12);
console.log('spread', [...new Set([3, 1, 3, 2])].sort().join(), Object.entries({ x: 1, y: 2 }).map(([k, v]) => k + v).join());
console.log('bigint', (2n ** 100n).toString(), typeof 1n);
const gen = function* () { yield 1; yield* [2, 3]; }; console.log('gen', [...gen()].join());
let out = []; Promise.resolve(1).then(v => out.push('then' + v)); (async () => { await null; out.push('async'); console.log('order', out.join()); })(); out.push('sync');
try { null.x } catch (e) { console.log('error', e instanceof TypeError, e.message); }
let t0 = Date.now(), s = 0; for (let i = 0; i < 3e6; i++) s += i % 7; console.log('loop', s, (Date.now() - t0) + 'ms');
console.log('sort', Array.from({ length: 20000 }, (_, i) => (i * 7919) % 10007).sort((a, b) => a - b).slice(0, 5).join());
console.log('str', 'abc'.padStart(6, '-'), 'a,b,c'.split(',').reverse().join(''), `tpl ${1 + 1}`, encodeURIComponent('a b&c'));
