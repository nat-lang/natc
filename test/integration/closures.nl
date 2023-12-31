//

var globalSet;
var globalGet;

fun testSharedUpvalue() {
  var a = "initial";

  fun set() { a = "updated"; }
  fun get() { print a; }

  globalSet = set;
  globalGet = get;
}

testSharedUpvalue();
globalSet();
globalGet();

var globalOne;
var globalTwo;

//

fun testLoopClosure() {
  for (var a = 1; a <= 2; a = a + 1) {
    fun closure() {
      print a;
    }
    if (globalOne == nil) {
      globalOne = closure;
    } else {
      globalTwo = closure;
    }
  }
}

testLoopClosure();
globalOne();
globalTwo();

//

fun vector(x, y) {
  fun object(message) {
    fun add(other) {
      return vector(x + other("x"), y + other("y"));
    }

    if (message == "x") return x;
    if (message == "y") return y;
    if (message == "add") return add;
    print "unknown message";
  }

  return object;
}

var a = vector(1, 2);
var b = vector(3, 4);
var c = a("add")(b);
print c("x");
print c("y");