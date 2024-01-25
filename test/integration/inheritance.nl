
// resolve an immediate super call.

class A {
  method() => {
    print "A method";
  }
}

class B < A {
  method() => {
    print "B method";
  }

  test() => {
    super.method();
  }
}

class C < B {}

// Prints "A method".
C().test();

// close over a super method.

class A {
  method() => {
    print "A";
  }
}

class B < A {
  method() => {
    let closure = super.method;
    closure(); // Prints "A".
  }
}

B().method();

// 

class Doughnut {
  cook() => {
    print "Dunk in the fryer.";
    this.finish("sprinkles");
  }

  finish(ingredient) => {
    print "Finish with " + ingredient;
  }
}

class Cruller < Doughnut {
  finish(ingredient) => {
    // No sprinkles, always icing.
    super.finish("icing");
  }
}

Cruller().cook();