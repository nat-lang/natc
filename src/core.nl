let fst = (seq) => { return seq[0]; };
let snd = (seq) => { return seq[1]; };

let fmap = (seq, fn) => {
  let ret = [];
  for (x in seq) { ret.add(fn(x)); }
  return ret;
}

class Sequence < __seq__ {
  iterate() => { return this.values; }
}

class Map < __map__ {
  set(key, val) => {
    return this[key] = val;
  }
  get(key) => {
    return this[key];
  }
  keys() => {}
  values() => {}
}

class Set < __map__ {
  add(element) => {
    return this[element] = true;
  }
  call(element) => {
    return element in this;
  }
  union(x) => {}
  intersection(x) => {}
  complement(x) => {}
}

class Tree {
  init(children) => {
    this.children = children;
  }
}

