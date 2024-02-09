let map = {"a":1, "b": 2};
let a = "a";

print map["a"] == map[a];
print map[(() => { return "a"; })()] == map[a];

map["a"] = 3;

print map["a"] == 2;
print map["a"] == 3;

print "a" in map;

map = {1: 1, 2: 2};

print map[1];

map = {true: 1, false: 0};

print map[true];

map = {nil: 1};

print map[nil];

map = {1: "a", 2: "b", 3: "c", 4: "d"};
for (x in map) {
  print x.values;
}

print map.keys().values;
print map.values().values;
