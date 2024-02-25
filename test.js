const Glomium = require('./');

// Create a new Glomium instance with custom configuration

// Set global variables in the Duktape context
function wait(ms) {
  return new Promise(res => {
    setTimeout(res,ms)
  })
};

(async () => {
  const glomium = new Glomium({
    gas: {
      limit: 20000000,            // The maximum amount of gas the execution environment can use
      memoryByteCost: 1         // The gas cost per byte of memory used
    }
  });
  // Run some JavaScript code
  await glomium.set("log", function log(r) {
    console.log(r)
  });
  await glomium.set("wait", wait);

   glomium.run(`
(function (a,b){
var i=0;
 while(i<10000000){
  i++;
  if(i%10000==0){
    log(i);
  }
 }

  }
  )()`).catch(e => {
    console.log("Error happened while executing code!",e)
  })


})()
