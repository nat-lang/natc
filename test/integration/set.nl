
let nulSet = {};

let numSet = {1, 2, 3};
let strSet = {"a", "b", "c"};

print "a" in strSet and "b" in strSet;

print 1 in numSet;
print numSet(1);

print numSet != strSet;
print numSet == numSet;
print numSet == {1,2,3};

