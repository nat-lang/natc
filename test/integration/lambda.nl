// immediate invocation

print ((a) => { return a + 1; })(1);

// no parameters

(() => {
  print "no params";
})();

// as return values

let compose = (f, g) => {
  return (a) => { return f(g(a)); };
};

// as arguments

print compose(
  (a) => { return a + 1; },
  (a) => { return a - 1; }
)(1) == 1;