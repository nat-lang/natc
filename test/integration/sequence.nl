
let seq_instance = [1,2,3];

// the underlying seq object itself.
let seq = seq_instance.values;

// subscript access.
print seq[0] == 1;
print seq[2] == 3;

// subscript assignment.
seq[0] = 2;
print seq[0] == 2;
seq[2] = 0;
print seq[2] == 0;

// index out of bounds.
seq[3];
seq[-1];
