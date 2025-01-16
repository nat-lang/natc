import main from './lib/nat.js';
process.stdout.write("?");
main({ arguments: ["test/integration/index"] });
main({ arguments: ["test/regression/index"] });
main({ arguments: ["test/trip/index"] });

