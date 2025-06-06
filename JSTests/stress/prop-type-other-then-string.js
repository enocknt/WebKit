function foo(o) {
    return o.f == "hello";
}

function bar(o, v) {
    o.f = v;
}

noInline(foo);
noInline(bar);

var o = {};
for (var i = 0; i < 5; ++i)
    bar(o, null);

for (var i = 0; i < testLoopCount; ++i) {
    var result = foo(o);
    if (result !== false)
        throw "Error: bad result: " + result;
}

bar(o, "hello");
var result = foo(o);
if (result !== true)
    throw "Error: bad result at end: " + result;
