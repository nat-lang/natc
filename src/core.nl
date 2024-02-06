let fst = (seq) => { return seq[0]; };
let snd = (seq) => { return seq[1]; };

let fmap = (seq, fn) => {
  let ret = [];
  // for (x in seq) { ret.add(fn(x)); }
  return ret;
};

class Sequence < __seq__ {
  __get__(idx) => {
    return this.values[idx];
  }

  __set__(idx, val) => { return this.values[idx] = val; }
  // __len__() => { return this.values.length; }
}

class Iterator < Sequence {
  next(idx) => {
    if (idx == nil) { return 0; }
    if (idx == this.length) { return false; }

    return idx + 1;
  }
  curr(idx) => { return this[idx]; }
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

