class Pair {}

let pair = Pair();
pair.first = 1;
pair.second = 2;
print pair.first + pair.second; // 3.

class Scone {
  topping(first, second) => {
    print "scone with " + first + " and " + second;
  }
}

let scone = Scone();
scone.topping("berries", "cream");

class NestedFun {
  method() => {
    let function = () => {
      print this;
    };

    function();
  }
}

NestedFun().method();

class NestedClass {
  method() => {
    class InnerClass {}

    print InnerClass;
  }
}

NestedClass().method();

class Brunch {
  init(food, drink) => {
    (() => {
      print this;
    })();
  }
}

print Brunch("eggs", "coffee");

class CoffeeMaker {
  init(coffee) => {
    this.coffee = coffee;
  }

  brew() => {
    print "Enjoy your cup of " + this.coffee;

    // No reusing the grounds!
    this.coffee = nil;
  }
}

let maker = CoffeeMaker("coffee and chicory");
maker.brew();

maker.foo = () => { print "bar"; };
maker.foo();

// callability

class Callable {
  init(x) => { this.x = x; }
  call(y) => { return this.x + y; }
}

let c = Callable(1);

print c(2);