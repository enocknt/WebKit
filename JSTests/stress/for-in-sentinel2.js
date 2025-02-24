function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error('bad value: ' + actual);
}

var object = {
    $: 32,
    test: 33,
    hey: 34,
};

function test(object) {
    var string = '$';
    var count = 0;
    for (var i in object) {
        ++count;
    }
    for (var i in object) {
        ++count;
    }
    return count + string;
}
noInline(test);

for (var i = 0; i < testLoopCount; ++i)
    shouldBe(test(object), `6$`);
