
let seq = [1,2,3].values;

print length(seq) == 3;

class Lengthy {
  init(a) => { this.length = a; }
  __length__() => { return this.length; }
}

print length(Lengthy(0)) == 0;