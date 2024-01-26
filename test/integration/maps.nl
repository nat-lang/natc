
let foo = (a) => { return a + 1; };

let map = {"a":1, "b": 2};
let a = "a";

print map["a"] == map[a];
print map[(() => { return "a"; })()] == map[a];
