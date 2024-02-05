class Sequence < __seq__ {

}

class Map {
  set(key, val) => {
    return this[key] = val;
  }
  get(key) => {
    return this[key];
  }
}

class Set {
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
  // init(*children) => {}
}

