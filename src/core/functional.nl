let fst = (seq) => { return seq[0]; };
let snd = (seq) => { return seq[1]; };

let fmap = (seq, fn) => {
  let ret = [];
  for (x in seq) { ret.add(fn(x)); }
  return ret;
};