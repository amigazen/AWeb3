/* AWebJS self-test — no browser host; uses global writeln/write/print and engine builtins only.
 *   awebjs PROGDIR:tests/awebjs_selftest.js
 *
 * Each check runs inside try/catch so thrown errors count as FAIL (not silent "all checks ok").
 *
 * Memory: global gc() runs the mark/sweep collector (same as host Jgarbagecollect). On process
 * exit, awebjs may print a teardown line when the build supports interpreter-pool stats.
 *
 * Does not cover: require() paths (cwd-dependent), readln (interactive), delay (timing),
 * ARexx.SendCommand (needs Rexx host), DOM/HTML host objects. */

 writeln("awebjs_selftest: start");

 var ok = true;
 
 function fail(msg) {
     ok = false;
     writeln("FAIL: " + msg);
 }
 
 function pass(msg) {
     writeln("PASS: " + msg);
 }
 
 /* (thunk) must be a function returning truthy/falsy for the test outcome. */
 function check(desc, thunk) {
     var cond;
     try {
         cond = thunk();
     } catch (e) {
         ok = false;
         writeln("FAIL: " + desc + " (exception)");
         return;
     }
     if (!cond) {
         fail(desc);
     } else {
         pass(desc);
     }
 }
 
 /* Expect (thunk) to throw; used for deliberate error-path coverage. */
 function checkThrows(desc, thunk) {
     var threw = false;
     try {
         thunk();
     } catch (e) {
         threw = true;
     }
     if (!threw) {
         fail(desc);
     } else {
         pass(desc);
     }
 }
 
 /* --- literals, typeof, void, null --- */
 check("typeof undefined", function () { return typeof (void 0) === "undefined"; });
 check("typeof null is object (legacy)", function () { return typeof null === "object"; });
 check("typeof true is boolean", function () { return typeof true === "boolean"; });
 check("typeof 0 is number", function () { return typeof 0 === "number"; });
 check("typeof '' is string", function () { return typeof "" === "string"; });
 check("strict eq null", function () { return null === null; });
 check("strict neq null undefined", function () { return null !== void 0; });
 
 /* --- arithmetic --- */
 check("add mul div sub", function () { return (2 + 3) * 4 / 2 - 1 === 9; });
 check("modulo", function () { return 17 % 5 === 2; });
 check("unary minus", function () { return -(-7) === 7; });
 
 /* --- comparisons (abstract and strict) --- */
 check("loose eq number string", function () { return 1 == "1"; });
 check("strict eq number", function () { return 2 === 2; });
 check("strict neq", function () { return 2 !== "2"; });
 check("lt chain", function () { return 1 < 2 && 2 <= 2 && 3 > 2; });
 
 /* --- logical --- */
 check("logical and or", function () { return (0 && 1) === 0 || (0 || 7) === 7; });
 check("logical not", function () { return !false === true; });
 
 /* --- bitwise (integers) --- */
 check("bit and or xor", function () { return (3 & 6) === 2 && (1 | 2) === 3 && (7 ^ 7) === 0; });
 check("bit not signature", function () { return (~0) < 0 || (~0) !== 0; });
 check("shift left signed right", function () { return (1 << 4) === 16 && (32 >> 1) === 16; });
 check("unsigned shift >>>", function () { return (-1 >>> 1) === 2147483647; });
 check("unsigned shift zero fill", function () { return (1 >>> 31) === 0; });
 
 /* --- strings --- */
 check("string length", function () { return "abcd".length === 4; });
 check("charAt", function () { return "xy".charAt(1) === "y"; });
 check("indexOf", function () { return "banana".indexOf("na") === 2; });
 check("substring", function () { return "abcdef".substring(2, 5) === "cde"; });
 check("string concat +", function () { return "a" + 1 === "a1"; });
 check("string toLowerCase toUpperCase", function () {
     return "Ab".toLowerCase() === "ab" && "cD".toUpperCase() === "CD";
 });
 check("string split", function () {
     var p = "a|b|c".split("|");
     return p.length === 3 && p[1] === "b";
 });
 check("string slice", function () { return "abcd".slice(1, 3) === "bc"; });
 check("string match", function () {
     var m = "abc".match(/b/);
     return m !== null && m[0] === "b";
 });
 check("string replace", function () { return "a-b-c".replace("-", ".") === "a.b-c"; });
 check("string search", function () { return "xyz".search("y") === 1; });
 
 /* --- global parse / numbers --- */
 check("parseInt decimal", function () { return parseInt("42", 10) === 42; });
 check("parseInt hex", function () { return parseInt("ff", 16) === 255; });
 check("parseFloat", function () { return Math.abs(parseFloat("3.5") - 3.5) < 0.0001; });
 check("isNaN true", function () { return isNaN(NaN) === true; });
 check("isNaN false for number", function () { return isNaN(0) === false; });
 
 /* --- escape / unescape (legacy globals) --- */
 check("escape identity ascii", function () { return unescape(escape("abc123")) === "abc123"; });
 
 /* --- Math --- */
 check("Math.PI positive", function () { return Math.PI > 3.1 && Math.PI < 3.2; });
 check("Math.abs", function () { return Math.abs(-9) === 9; });
 check("Math.floor ceil round", function () {
     return Math.floor(2.7) === 2 && Math.ceil(2.1) === 3 && Math.round(2.4) === 2 && Math.round(2.6) === 3;
 });
 check("Math.max min", function () { return Math.max(1, 5, 2) === 5 && Math.min(1, 5, 2) === 1; });
 check("Math.pow sqrt", function () { return Math.pow(2, 8) === 256 && Math.sqrt(9) === 3; });
 
 /* --- Object --- */
 check("object literal prop", function () {
     var o = { a: 2, "b": 3 };
     return o.a + o["b"] === 5;
 });
 check("delete property", function () {
     var o = { x: 1 };
     delete o.x;
     return typeof o.x === "undefined";
 });
 check("hasOwnProperty", function () {
     var o = { x: 1 };
     return o.hasOwnProperty("x") && !o.hasOwnProperty("y");
 });
 
 /* --- in / instanceof --- */
 check("in operator", function () {
     var o = { p: 1 };
     return ("p" in o) && !("q" in o);
 });
 check("instanceof Array", function () {
     var a = [];
     return a instanceof Array;
 });
 check("instanceof Object", function () {
     var o = {};
     return o instanceof Object;
 });
 
 /* --- Array --- */
 check("array literal length", function () { return [10, 20, 30].length === 3; });
 check("array index read", function () {
     var a = [5, 6];
     return a[0] === 5 && a[1] === 6;
 });
 check("array join", function () { return ["a", "b", "c"].join("-") === "a-b-c"; });
 check("array join default comma", function () { return [1, 2, 3].join() === "1,2,3"; });
 check("array push pop", function () {
     var a = [1];
     a.push(2, 3);
     var t = a.pop();
     return a.length === 2 && t === 3 && a[1] === 2;
 });
 check("array push no args", function () {
     var a = [7];
     var r = a.push();
     return r === 1 && a.length === 1 && a[0] === 7;
 });
 check("array pop empty", function () {
     var a = [];
     var u = a.pop();
     return a.length === 0 && typeof u === "undefined";
 });
 check("array shift", function () {
     var a = [1, 2, 3];
     var h = a.shift();
     return h === 1 && a.length === 2 && a[0] === 2;
 });
 check("array unshift", function () {
     var a = [2, 3];
     var n = a.unshift(1);
     return n === 3 && a[0] === 1 && a[2] === 3;
 });
 check("array reverse", function () {
     var a = [1, 2, 3];
     a.reverse();
     return a[0] === 3 && a[2] === 1 && a.length === 3;
 });
 check("array sort lexicographic", function () {
     var a = ["c", "a", "b"];
     a.sort();
     return a.join("") === "abc";
 });
 check("array sort compare fn", function () {
     var a = [3, 1, 4, 2];
     a.sort(function (x, y) { return x - y; });
     return a[0] === 1 && a[1] === 2 && a[2] === 3 && a[3] === 4;
 });
 check("array length truncate", function () {
     var a = [1, 2, 3];
     a.length = 1;
     return a.length === 1 && a[0] === 1;
 });
 check("array toString", function () { return [1, 2].toString() === "1,2"; });
 check("array concat", function () { return [1].concat([2, 3]).join("") === "123"; });
 check("array slice", function () { return [0, 1, 2, 3].slice(1, 3).join(",") === "1,2"; });
 check("array splice delete", function () {
     var a = [0, 1, 2, 3];
     a.splice(1, 2);
     return a.length === 2 && a[0] === 0 && a[1] === 3;
 });
 
 /* --- Boolean / Number / String constructors --- */
 check("Boolean()", function () { return Boolean(0) === false && Boolean(1) === true; });
 check("Number()", function () { return Number("3.14") > 3.13; });
 check("Number toString radix", function () { return (15).toString(16) === "f"; });
 check("String()", function () { return String(100) === "100"; });
 
 /* --- Date (UTC epoch only; no local DOM clock) --- */
 check("Date getTime", function () {
     var d = new Date(0);
     return typeof d.getTime === "function" && d.getTime() === 0;
 });
 check("Date.parse is function", function () { return typeof Date.parse === "function"; });
 check("Date.UTC is function", function () { return typeof Date.UTC === "function"; });
 /* ECMA-262: Date.prototype.toString returns an implementation-defined string for a finite date.
  * (Invalid Date may throw or return a sentinel in some engines; new Date(0) is valid.) */
 check("Date toString non-empty", function () {
     var d = new Date(0);
     if (typeof d.toString !== "function") {
         return false;
     }
     var s = d.toString();
     return typeof s === "string" && s.length > 0;
 });
 
 /* --- RegExp literals (lexer uses pa->newexpr: '/' after primaries is division) --- */
 check("RegExp test literal", function () {
     return /z/.test("abcz") === true && /z/.test("abc") === false;
 });
 check("RegExp exec literal", function () {
     var m = /(\d)/.exec("a7b");
     return m !== null && m[1] === "7";
 });
 check("RegExp constructor", function () {
     var r = new RegExp("a", "");
     return r.test("xa") === true && r.test("xb") === false;
 });
 
 /* --- Error + try/catch/finally + throw --- */
 check("try catch", function () {
     try {
         throw "x";
     } catch (e) {
         return e === "x";
     }
     return false;
 });
 check("try finally runs", function () {
     var n = 0;
     try {
         n = 1;
     } finally {
         n = n + 2;
     }
     return n === 3;
 });
 check("try catch finally on throw", function () {
     var n = 0;
     try {
         throw 1;
     } catch (e) {
         n = 1;
     } finally {
         n = n + 5;
     }
     return n === 6;
 });
 
 /* --- eval --- */
 check("eval math", function () { return eval("6 * 7") === 42; });
 
 /* --- functions: decl, expr, closure, recursion --- */
 check("function decl", function () {
     function g(n) {
         return n + 1;
     }
     return g(4) === 5;
 });
 check("closure", function () {
     var x = 2;
     return (function () { return x + 3; })() === 5;
 });
 check("recursion", function () {
     function f(n) {
         if (n <= 1) {
             return 1;
         }
         return n * f(n - 1);
     }
     return f(5) === 120;
 });
 check("function call", function () {
     return (function (a, b) { return a + b; }).call(null, 20, 22) === 42;
 });
 check("function apply", function () {
     return (function (a, b) { return a + b; }).apply(null, [1, 40]) === 41;
 });
 
 /* --- control flow --- */
 check("for loop", function () {
     var s = 0;
     var i;
     for (i = 1; i <= 5; i++) {
         s += i;
     }
     return s === 15;
 });
 check("while", function () {
     var n = 3;
     var s = 0;
     while (n > 0) {
         s++;
         n--;
     }
     return s === 3;
 });
 check("do while", function () {
     var n = 0;
     do {
         n++;
     } while (n < 2);
     return n === 2;
 });
 check("switch break", function () {
     var x = 2;
     var y = 0;
     switch (x) {
         case 1:
             y = 10;
             break;
         case 2:
             y = 20;
             break;
         default:
             y = 99;
     }
     return y === 20;
 });
 check("continue", function () {
     var s = 0;
     var i;
     for (i = 0; i < 5; i++) {
         if (i === 2) {
             continue;
         }
         s += i;
     }
     return s === 8;
 });
 check("ternary", function () { return (true ? 1 : 2) === 1 && (false ? 1 : 2) === 2; });
 check("comma operator", function () { return ((1, 2, 3)) === 3; });
 
 /* --- increment (value tests only if engine supports) --- */
 check("post increment", function () {
     var a = 1;
     var b = a++;
     return b === 1 && a === 2;
 });
 
 /* --- this in method style --- */
 check("this in object function", function () {
     var o = { n: 5, f: function () { return this.n + 1; } };
     return o.f() === 6;
 });
 
 /* --- for-in (order not asserted) --- */
 check("for in collects key", function () {
     var o = { k1: 1, k2: 2 };
     var seen = 0;
     var p;
     for (p in o) {
         if (p === "k1" || p === "k2") {
             seen++;
         }
     }
     return seen === 2;
 });
 
 /* --- ecmascript_test.html-style (core language, no document.* ) --- */
 check("string concat two-step", function () {
     var greeting = "Hello";
     var name = "World";
     var space = " ";
     var message1 = greeting + space;
     var message = message1 + name;
     var message2 = greeting + space + name;
     return message === "Hello World" && message2 === "Hello World";
 });
 check("new Array() length and index", function () {
     var arr = new Array(1, 2, 3, 4, 5);
     return arr[0] === 1 && arr[4] === 5 && arr.length === 5 && typeof arr === "object";
 });
 check("new Object() props", function () {
     var obj = new Object();
     obj.name = "Test";
     obj.value = 42;
     return obj.name === "Test" && obj.value === 42;
 });
 check("if else chain", function () {
     var score = 85;
     var grade = "F";
     if (score >= 90) {
         grade = "A";
     } else if (score >= 80) {
         grade = "B";
     } else if (score >= 70) {
         grade = "C";
     } else if (score >= 60) {
         grade = "D";
     }
     return grade === "B";
 });
 check("while concat 0 to 2", function () {
     var counter = 0;
     var loopResult = "";
     while (counter < 3) {
         loopResult = loopResult + counter + " ";
         counter = counter + 1;
     }
     return loopResult === "0 1 2 ";
 });
 check("for with i = i + 1", function () {
     var forResult = "";
     var i;
     for (i = 0; i < 3; i = i + 1) {
         forResult = forResult + i + " ";
     }
     return forResult === "0 1 2 ";
 });
 check("addAndMultiply nested calls", function () {
     function multiply(a, b) {
         return a * b;
     }
     function addAndMultiply(x, y, z) {
         return multiply(x + y, z);
     }
     return addAndMultiply(2, 3, 4) === 20;
 });
 check("typeof variable function", function () {
     var func = function () { return 1; };
     return typeof func === "function";
 });
 check("string indexOf lastIndexOf", function () {
     var str = "Hello World";
     return str.indexOf("o") === 4 && str.lastIndexOf("o") === 7 && str.indexOf("x") === -1;
 });
 check("substring one arg", function () {
     var str = "Hello World";
     return str.substring(0, 5) === "Hello" && str.substring(6) === "World";
 });
 check("join comma space then reverse", function () {
     var arr = new Array("a", "b", "c");
     var joined = arr.join(", ");
     arr.reverse();
     var reversed = arr.join(", ");
     return joined === "a, b, c" && reversed === "c, b, a";
 });
 check("operator precedence", function () {
     var a = 10;
     var b = 5;
     var c = 2;
     return a + b * c === 20 && (a + b) * c === 30 &&
         a - b / c === 7.5 && (a - b) / c === 2.5;
 });
 check("object multiple props", function () {
     var person = new Object();
     person.name = "John";
     person.age = 30;
     person.city = "New York";
     return person.name === "John" && person.age === 30 && person.city === "New York";
 });
 check("calculate multi stmt function", function () {
     function calculate(x, y) {
         var sum = x + y;
         var product = x * y;
         var result = sum + product;
         return result;
     }
     return calculate(3, 4) === 19;
 });
 check("array index assign", function () {
     var arr = new Array(1, 2, 3);
     arr[1] = 99;
     return arr[1] === 99 && arr.length === 3;
 });
 check("modulo trio", function () {
     return 10 % 3 === 1 && 15 % 5 === 0 && 7 % 2 === 1;
 });
 check("bitwise 5 and 3", function () {
     var a = 5;
     var b = 3;
     return (a & b) === 1 && (a | b) === 7 && (a ^ b) === 6;
 });
 check("shift 8", function () {
     var num = 8;
     return (num << 2) === 32 && (num >> 1) === 4;
 });
 check("assign += *= -=", function () {
     var x = 10;
     x += 5;
     if (x !== 15) {
         return false;
     }
     x = 10;
     x *= 2;
     if (x !== 20) {
         return false;
     }
     x = 10;
     x -= 3;
     return x === 7;
 });
 check("pre post inc dec", function () {
     var a = 5;
     var preInc = ++a;
     var postInc = a++;
     var incResult = a;
     var b = 5;
     var preDec = --b;
     var postDec = b--;
     var decResult = b;
     return preInc === 6 && postInc === 6 && incResult === 7 &&
         preDec === 4 && postDec === 4 && decResult === 3;
 });
 check("charCodeAt ABC", function () {
     var str = "ABC";
     return str.charCodeAt(0) === 65 && str.charCodeAt(1) === 66 && str.charCodeAt(2) === 67;
 });
 check("sort numeric strings join", function () {
     var arr = new Array(3, 1, 4, 1, 5);
     arr.sort();
     return arr.join(", ") === "1, 1, 3, 4, 5";
 });
 check("nested object property", function () {
     var person = new Object();
     person.name = "John";
     person.address = new Object();
     person.address.city = "New York";
     person.address.zip = "10001";
     return person.address.city === "New York" && person.address.zip === "10001";
 });
 check("function expression var", function () {
     var square = function (x) { return x * x; };
     return square(5) === 25;
 });
 check("max of two returns", function () {
     function max(a, b) {
         if (a > b) {
             return a;
         }
         return b;
     }
     return max(10, 7) === 10;
 });
 check("global and local scope", function () {
     var global = 10;
     function testScope() {
         var local = 20;
         return global + local;
     }
     return testScope() === 30;
 });
 check("Math.E", function () { return Math.E > 2.7 && Math.E < 2.72; });
 check("empty string length", function () { return "".length === 0; });
 check("String.prototype.concat", function () {
     var str1 = "Hello";
     var str2 = "World";
     return str1.concat(" ", str2) === "Hello World" && "A".concat("B", "C") === "ABC";
 });
 check("slice with one index", function () {
     var arr = new Array(1, 2, 3, 4, 5);
     var slice1 = arr.slice(1, 3);
     var slice2 = arr.slice(2);
     return slice1.join(", ") === "2, 3" && slice2.join(", ") === "3, 4, 5";
 });
 check("do while sum 0..4", function () {
     var count = 0;
     var sum = 0;
     do {
         sum += count;
         count++;
     } while (count < 5);
     return sum === 10;
 });
 check("arguments object sum", function () {
     function sumArgs() {
         var total = 0;
         var i;
         for (i = 0; i < arguments.length; i++) {
             total += arguments[i];
         }
         return total;
     }
     return sumArgs(1, 2, 3, 4, 5) === 15;
 });
 check("factorial recursive", function () {
     function factorial(n) {
         if (n <= 1) {
             return 1;
         }
         return n * factorial(n - 1);
     }
     return factorial(5) === 120;
 });
 check("delete one prop keep other", function () {
     var obj = new Object();
     obj.prop1 = "value1";
     obj.prop2 = "value2";
     var beforeDelete = obj.prop1;
     delete obj.prop1;
     return beforeDelete === "value1" && typeof obj.prop1 === "undefined" && obj.prop2 === "value2";
 });
 
 /* --- object churn, delete, explicit GC (teardown line on awebjs exit; Jgetjmemsessionstats for embedders) --- */
 check("gc is callable", function () {
     gc();
     return true;
 });
 check("gc after object churn and delete", function () {
     var hold;
     var i;
     hold = new Object();
     hold.a = new Array();
     for (i = 0; i < 50; i++) {
         hold.a[i] = new Object();
         hold.a[i].n = i;
     }
     delete hold.a;
     hold.k = 7;
     gc();
     return hold.k === 7 && typeof hold.a === "undefined";
 });
 check("many orphan objects then gc", function () {
     var i;
     for (i = 0; i < 120; i++) {
         new Object();
     }
     gc();
     return true;
 });
 
 check("for in three props", function () {
     var obj = new Object();
     obj.name = "John";
     obj.age = 30;
     obj.city = "New York";
     var n = 0;
     var prop;
     for (prop in obj) {
         n++;
     }
     return n === 3;
 });
 check("break in for", function () {
     var sum = 0;
     var i;
     for (i = 0; i < 10; i++) {
         if (i > 5) {
             break;
         }
         sum += i;
     }
     return sum === 15;
 });
 check("continue odd sum", function () {
     var sum = 0;
     var i;
     for (i = 0; i < 10; i++) {
         if (i % 2 === 0) {
             continue;
         }
         sum += i;
     }
     return sum === 25;
 });
 check("parseInt truncates float string", function () { return parseInt("3.14") === 3; });
 check("parseFloat int string", function () { return parseFloat("42") === 42; });
 check("logical && || yield operands", function () {
     return (true && true) === true && (true && false) === false &&
         (true || false) === true && (false || false) === false &&
         (5 && 3) === 3 && (0 || 7) === 7;
 });
 check("ternary strings and numbers", function () {
     return ((5 > 3) ? "yes" : "no") === "yes" &&
         ((2 > 5) ? "yes" : "no") === "no" &&
         ((10 > 5) ? 100 : 50) === 100;
 });
 
/* --- ES3 Tier-1 builtins (encodeURI, isFinite, Date UTC, Number format, RegExp) --- */
check("isFinite true for number", function () { return isFinite(42) === true; });
check("isFinite false for NaN", function () { return isFinite(NaN) === false; });
check("isFinite false for Infinity", function () { return isFinite(1 / 0) === false; });
check("encodeURI keeps reserved", function () {
    return encodeURI("http://a/b?x=1") === "http://a/b?x=1";
});
check("encodeURIComponent encodes slash", function () {
    return encodeURIComponent("/") === "%2F";
});
check("decodeURIComponent slash", function () {
    return decodeURIComponent("%2F") === "/";
});
check("Date UTC get/set round trip", function () {
    var d = new Date(0);
    d.setUTCHours(12, 30, 45, 0);
    return d.getUTCHours() === 12 && d.getUTCMinutes() === 30 && d.getUTCSeconds() === 45;
});
check("Date getUTCFullYear epoch", function () {
    return (new Date(0)).getUTCFullYear() === 1970;
});
check("Date toUTCString non-empty", function () {
    var s = (new Date(0)).toUTCString();
    return typeof s === "string" && s.length > 0;
});
check("Number toExponential", function () {
    return (12345).toExponential(2).indexOf("e") >= 0;
});
check("Number toPrecision", function () {
    return (3.14159).toPrecision(3) === "3.14";
});
check("RegExp literal global flag", function () {
    return /a/g.global === true && /a/i.ignoreCase === true;
});
check("RegExp global exec advances index", function () {
    var r = /a/g;
    var m1 = r.exec("aba");
    var m2 = r.exec("aba");
    return m1 !== null && m1.index === 0 && m2 !== null && m2.index === 1;
});
check("RegExp global lastIndex advances", function () {
    var r = /a/g;
    r.exec("aba");
    r.exec("aba");
    return r.lastIndex === 2;
});
check("RegExp global lastIndex reset on miss", function () {
    var r = /z/g;
    r.exec("aba");
    var m = r.exec("aba");
    return m === null && r.lastIndex === 0;
});

/* --- javascript_test.html: validateForm logic only (no document/forms host) --- */
function awebjs_mock_validateForm(form) {
     if (form && form.elements[0] && form.elements[0].value == "") {
         return false;
     }
     return true;
 }
 check("validateForm logic empty name", function () {
     var emptyForm = { elements: [{ value: "" }] };
     var okForm = { elements: [{ value: "Alice" }] };
     return awebjs_mock_validateForm(emptyForm) === false && awebjs_mock_validateForm(okForm) === true;
 });
 
 if (ok) {
     writeln("awebjs_selftest: all checks ok");
 } else {
     writeln("awebjs_selftest: some checks failed");
 }
 
 writeln("awebjs_selftest: provoking ReferenceError to trigger stacktrace...");
 
 function stack_c() {
     /* Undefined global — ReferenceError with call chain below */
     AWebJS_selftest_no_such_global();
 }
 function stack_b() {
     stack_c();
 }
 function stack_a() {
     stack_b();
 }
 stack_a();
 
 /* --- deliberate failures at end (exercise error paths) --- */
 checkThrows("throw statement is catchable", function () {
     throw 123;
 });
 checkThrows("TypeError calling non-function", function () {
     /* matches existing runtime error path: 'Not a function' */
     (0)();
 });
 