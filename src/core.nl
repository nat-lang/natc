let fst = (seq) => { return seq[0]; };
let snd = (seq) => { return seq[1]; };

let fmap = (seq, fn) => {
  let ret = [];
  for (x in seq) { ret.add(fn(x)); }
  return ret;
};

let next = (seq, idx) => {
  if ("__next__" in seq) { return seq.__next__(idx); }

  // first iteration.
  if (idx == nil) { return 0; }
  // final iteration.
  if (idx == len(seq) - 1) { return false; }

  return idx + 1;
};

let curr = (seq, idx) => {
  if ("__curr__" in seq) { return seq.__curr__(idx); }

  return seq[idx];
};

class Sequence < __seq__ {
  __get__(idx) => { return this.values[idx]; }
  __set__(idx, val) => { this.values[idx] = val; }

  __len__() => { return len(this.values); }
}

class Map < __map__ {
  set(key, val) => { return this[key] = val; }
  get(key) => { return this[key]; }

  __len__() => { return len(this.entries()); }

  __curr__(idx) => {
    return this.entries()[idx];
  }

  keys() => { return fmap(this, fst); }
  values() => { return fmap(this, snd); }
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

