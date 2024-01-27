class Set {
  init() {
    this.elements = Map();
  }
  add(element) {
    this.elements[element] = true;
  }
  del(element) {
    this.elements.del(element);
  }
  call(element) {
    return element in this.elements;
  }

  union(x) {}
  intersection(x) {}
  complement(x) {}
}


class Sequence {}
class Tree {}

