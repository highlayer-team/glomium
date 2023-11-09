const Glomium = require('./');

// Create a new Glomium instance with custom configuration
const glomium = new Glomium({
    gas: {
        limit: 1000000,            // The maximum amount of gas the execution environment can use
        memoryByteCost: 1         // The gas cost per byte of memory used
    }
});

// Set global variables in the Duktape context
glomium.set('hello', 'world');

// Get global variables from the Duktape context
const world = glomium.get('hello');

// Run some JavaScript code
const result = glomium.run(`
  function greet(name) {
    return 'Hello, ' + name + '!';
  }
  greet(hello);
`);

console.log(result); // Should output 'Hello, world!'
console.log(glomium.get("hello"))
glomium.clear()
console.log(glomium.get("hello"))