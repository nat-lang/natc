
let seq = [1,2,3].values;

print len(seq) == 3;

class Lengthy {
  init(a) => { this.length = a; }
  __len__() => { return this.length; }
}

print len(Lengthy(0)) == 0;