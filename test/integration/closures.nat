//

let globalSet;
let globalGet;

let testSharedUpvalue = () => {
  let a = "initial";

  let set = () => { a = "updated"; };
  let get = () => { print a; };

  globalSet = set;
  globalGet = get;
};

testSharedUpvalue();
globalSet ();
globalGet();

let globalOne;
let globalTwo;

//

let testLoopClosure = () => {
  for (let a = 1; a <= 2; a = a + 1) {
    let closure = () => {
      print a;
    };

    if (globalOne == nil) {
      globalOne = closure;
    } else {
      globalTwo = closure;
    }
  }
};

testLoopClosure();
globalOne();
globalTwo();

//

let vector = (x, y) => {
  let object = (message) => {
    let add = (other) => {
      return vector(x + other("x"), y + other("y"));
    };

    if (message == "x") return x;
    if (message == "y") return y;
    if (message == "add") return add;
    print "unknown message";
  };

  return object;
};

let a = vector(1, 2);
let b = vector(3, 4);
let c = a("add")(b);
print c("x");
print c("y");