
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
